#include "DownloadTask.h"
#include <curl/curl.h>
#include <fstream>
#include <cstdio>
#include <chrono>
#include <cmath>
#include <algorithm>

// ---- libcurl callbacks -----------------------------------------------

struct WriteCtx {
    std::fstream* file;
    Segment* seg;
};

static size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<WriteCtx*>(userdata);
    size_t bytes = size * nmemb;
    ctx->file->write(ptr, static_cast<std::streamsize>(bytes));
    if (!ctx->file->good()) return 0; // signals error to curl
    ctx->seg->downloaded.fetch_add(static_cast<long long>(bytes));
    return bytes;
}

struct ProgressCtx {
    std::atomic<bool>* pauseFlag;
    std::atomic<bool>* cancelFlag;
};

static int progressCallback(void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* ctx = static_cast<ProgressCtx*>(clientp);
    if (ctx->pauseFlag->load() || ctx->cancelFlag->load()) return 1; // abort transfer
    return 0;
}

// HEAD-style probe: get total size + whether the server advertises Range support.
static bool probeUrl(const QString& url, const QString& referer,
                      long long& outSize, bool& outSupportsRanges, QString& outError) {
    CURL* curl = curl_easy_init();
    if (!curl) { outError = "curl init failed"; return false; }

    curl_easy_setopt(curl, CURLOPT_URL, url.toUtf8().constData());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                      "Mozilla/5.0 AllPurposeDownloader/1.0");
    if (!referer.isEmpty())
        curl_easy_setopt(curl, CURLOPT_REFERER, referer.toUtf8().constData());

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        outError = curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        return false;
    }

    curl_off_t len = -1;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &len);
    outSize = static_cast<long long>(len);

    // Ask a byte-range HEAD to check if server honors it (206 means yes).
    curl_easy_setopt(curl, CURLOPT_RANGE, "0-0");
    long httpCode = 0;
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    outSupportsRanges = (res == CURLE_OK && httpCode == 206);

    curl_easy_cleanup(curl);
    return true;
}

// ---- DownloadTask -------------------------------------------------------

DownloadTask::DownloadTask(QString id, QString url, QString filepath, QString referer, QObject* parent)
    : QObject(parent), m_id(std::move(id)), m_url(std::move(url)),
      m_filepath(std::move(filepath)), m_referer(std::move(referer)) {}

DownloadTask::~DownloadTask() {
    m_cancelRequested = true;
    m_pauseRequested = true;
    m_speedThreadRunning = false;
    if (m_probeThread.joinable()) m_probeThread.join();
    if (m_speedThread.joinable()) m_speedThread.join();
    for (auto& seg : m_segments) {
        if (seg->worker.joinable()) seg->worker.join();
    }
}

long long DownloadTask::downloadedBytes() const {
    long long sum = 0;
    for (auto& seg : m_segments) sum += seg->downloaded.load();
    return sum;
}

double DownloadTask::overallPercent() const {
    long long total = m_totalBytes.load();
    if (total <= 0) return 0.0;
    return 100.0 * static_cast<double>(downloadedBytes()) / static_cast<double>(total);
}

double DownloadTask::segmentPercent(int i) const {
    if (i < 0 || i >= static_cast<int>(m_segments.size())) return 0.0;
    return m_segments[i]->percent();
}

void DownloadTask::start(int segmentCount) {
    m_status = TaskStatus::Probing;
    emit updated();

    m_speedThreadRunning = true;
    m_speedThread = std::thread(&DownloadTask::speedTicker, this);

    m_probeThread = std::thread(&DownloadTask::probeAndStart, this, segmentCount);
    m_probeThread.detach();
}

void DownloadTask::probeAndStart(int segmentCount) {
    long long size = -1;
    bool supportsRanges = false;
    QString err;

    bool ok = probeUrl(m_url, m_referer, size, supportsRanges, err);

    if (m_cancelRequested.load()) return;

    if (!ok || size <= 0) {
        // Can't determine size (or probe failed) — fall back to a plain
        // single-connection streamed download; still resumable via Range.
        m_supportsRanges = false;
        m_totalBytes = -1;
        runSingleStream();
        return;
    }

    m_totalBytes = size;
    m_supportsRanges = supportsRanges;

    if (!supportsRanges || size < 2 * 1024 * 1024) {
        // Small file or server won't honor ranges: one segment covering it all.
        segmentCount = 1;
    }

    // Pre-allocate the output file to full size so segments can write
    // into their own byte region independently and safely.
    {
        std::ofstream out(m_filepath.toStdString(), std::ios::binary | std::ios::trunc);
        if (!out.good()) {
            m_status = TaskStatus::Error;
            m_errorMessage = "Could not create output file";
            emit updated();
            return;
        }
        out.seekp(static_cast<std::streamoff>(size - 1));
        char zero = 0;
        out.write(&zero, 1);
    }

    long long chunk = size / segmentCount;
    for (int i = 0; i < segmentCount; ++i) {
        auto seg = std::make_unique<Segment>();
        seg->index = i;
        seg->startByte = i * chunk;
        seg->endByte = (i == segmentCount - 1) ? (size - 1) : (seg->startByte + chunk - 1);
        m_segments.push_back(std::move(seg));
    }

    m_status = TaskStatus::Downloading;
    emit updated();

    for (auto& seg : m_segments) {
        seg->worker = std::thread(&DownloadTask::runSegment, this, seg.get());
        seg->worker.detach();
    }
}

void DownloadTask::runSegment(Segment* seg) {
    if (seg->done.load() || m_cancelRequested.load()) return;

    std::fstream file(m_filepath.toStdString(), std::ios::binary | std::ios::in | std::ios::out);
    if (!file.good()) {
        seg->failed = true;
        emit updated();
        return;
    }

    long long resumeStart = seg->startByte + seg->downloaded.load();
    if (resumeStart > seg->endByte) {
        seg->done = true;
        checkAllSegmentsDone();
        return;
    }
    file.seekp(static_cast<std::streamoff>(resumeStart));

    CURL* curl = curl_easy_init();
    if (!curl) {
        seg->failed = true;
        emit updated();
        return;
    }

    WriteCtx wctx{&file, seg};
    ProgressCtx pctx{&m_pauseRequested, &m_cancelRequested};

    QString range = QString("%1-%2").arg(resumeStart).arg(seg->endByte);

    curl_easy_setopt(curl, CURLOPT_URL, m_url.toUtf8().constData());
    curl_easy_setopt(curl, CURLOPT_RANGE, range.toUtf8().constData());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &wctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &pctx);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 AllPurposeDownloader/1.0");
    if (!m_referer.isEmpty())
        curl_easy_setopt(curl, CURLOPT_REFERER, m_referer.toUtf8().constData());

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    file.close();

    if (res == CURLE_OK) {
        seg->done = true;
        checkAllSegmentsDone();
    } else if (res == CURLE_ABORTED_BY_CALLBACK) {
        // Paused or canceled — leave partial progress, thread just exits.
    } else {
        seg->failed = true;
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_errorMessage = curl_easy_strerror(res);
        m_status = TaskStatus::Error;
    }
    emit updated();
}

void DownloadTask::runSingleStream() {
    // Fallback path: no known size / no range support. Single connection,
    // still resumable by re-requesting with Range: <existing-file-size>-.
    if (m_segments.empty()) {
        auto seg = std::make_unique<Segment>();
        seg->index = 0;
        seg->startByte = 0;
        seg->endByte = -1; // unknown
        m_segments.push_back(std::move(seg));
    }
    Segment* seg = m_segments[0].get();

    std::ios_base::openmode mode = std::ios::binary | std::ios::out;
    long long existing = 0;
    {
        std::ifstream check(m_filepath.toStdString(), std::ios::binary | std::ios::ate);
        if (check.good()) existing = static_cast<long long>(check.tellg());
    }
    if (existing > 0) mode |= std::ios::in;

    std::fstream file(m_filepath.toStdString(), mode);
    if (!file.good()) {
        m_status = TaskStatus::Error;
        m_errorMessage = "Could not open output file";
        emit updated();
        return;
    }
    file.seekp(static_cast<std::streamoff>(existing));
    seg->downloaded = existing;

    CURL* curl = curl_easy_init();
    if (!curl) {
        m_status = TaskStatus::Error;
        emit updated();
        return;
    }

    WriteCtx wctx{&file, seg};
    ProgressCtx pctx{&m_pauseRequested, &m_cancelRequested};

    curl_easy_setopt(curl, CURLOPT_URL, m_url.toUtf8().constData());
    if (existing > 0) {
        QString range = QString("%1-").arg(existing);
        curl_easy_setopt(curl, CURLOPT_RANGE, range.toUtf8().constData());
    }
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &wctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &pctx);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 AllPurposeDownloader/1.0");
    if (!m_referer.isEmpty())
        curl_easy_setopt(curl, CURLOPT_REFERER, m_referer.toUtf8().constData());

    m_status = TaskStatus::Downloading;
    emit updated();

    CURLcode res = curl_easy_perform(curl);

    curl_off_t total = 0;
    curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &total);
    if (m_totalBytes.load() <= 0 && total > 0) {
        m_totalBytes = existing + static_cast<long long>(total);
        seg->endByte = m_totalBytes.load() - 1;
    }

    curl_easy_cleanup(curl);
    file.close();

    if (res == CURLE_OK) {
        seg->done = true;
        if (m_totalBytes.load() <= 0) m_totalBytes = seg->downloaded.load();
        m_status = TaskStatus::Completed;
    } else if (res == CURLE_ABORTED_BY_CALLBACK) {
        // paused/canceled, leave as-is
    } else {
        m_status = TaskStatus::Error;
        m_errorMessage = curl_easy_strerror(res);
    }
    emit updated();
}

void DownloadTask::checkAllSegmentsDone() {
    bool allDone = true;
    for (auto& seg : m_segments) {
        if (!seg->done.load()) { allDone = false; break; }
    }
    if (allDone && !m_segments.empty() && m_status.load() != TaskStatus::Canceled) {
        m_status = TaskStatus::Completed;
        emit updated();
    }
}

void DownloadTask::pause() {
    if (m_status.load() != TaskStatus::Downloading) return;
    m_pauseRequested = true;
    // Let worker threads notice the abort flag and exit; they were
    // detached, so we just wait briefly on a background thread and
    // then flip status once bytes stop moving.
    std::thread([this]() {
        long long before = -1;
        int stableTicks = 0;
        while (stableTicks < 3) {
            long long now = this->downloadedBytes();
            if (now == before) stableTicks++; else stableTicks = 0;
            before = now;
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
        }
        if (m_status.load() != TaskStatus::Canceled) {
            m_status = TaskStatus::Paused;
            emit updated();
        }
    }).detach();
}

void DownloadTask::resume() {
    if (m_status.load() != TaskStatus::Paused && m_status.load() != TaskStatus::Error) return;
    m_pauseRequested = false;
    m_status = TaskStatus::Downloading;
    emit updated();

    if (m_totalBytes.load() <= 0 || m_segments.empty()) {
        // was using the single-stream fallback
        std::thread(&DownloadTask::runSingleStream, this).detach();
        return;
    }

    for (auto& seg : m_segments) {
        if (seg->done.load()) continue;
        if (seg->worker.joinable()) seg->worker.join();
        seg->worker = std::thread(&DownloadTask::runSegment, this, seg.get());
        seg->worker.detach();
    }
}

void DownloadTask::cancel() {
    m_cancelRequested = true;
    m_pauseRequested = true;
    m_status = TaskStatus::Canceled;
    emit updated();
    std::thread([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        std::remove(m_filepath.toStdString().c_str());
    }).detach();
}

void DownloadTask::speedTicker() {
    m_lastTickBytes = 0;
    while (m_speedThreadRunning.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        if (m_status.load() == TaskStatus::Downloading) {
            long long now = downloadedBytes();
            long long delta = now - m_lastTickBytes;
            m_speed = delta > 0 ? (delta * 1000 / 800) : 0;
            m_lastTickBytes = now;
            emit updated();
        } else {
            m_speed = 0;
        }
        auto st = m_status.load();
        if (st == TaskStatus::Completed || st == TaskStatus::Canceled || st == TaskStatus::Error) {
            break;
        }
    }
}
