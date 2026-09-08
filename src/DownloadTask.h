#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <vector>
#include <thread>
#include <memory>
#include <mutex>

// One byte-range slice of a file, downloaded by its own worker thread.
struct Segment {
    int index = 0;
    long long startByte = 0;
    long long endByte = 0;      // inclusive
    std::atomic<long long> downloaded{0};
    std::atomic<bool> done{false};
    std::atomic<bool> failed{false};
    std::thread worker;

    long long rangeSize() const { return endByte - startByte + 1; }
    double percent() const {
        long long size = rangeSize();
        if (size <= 0) return 0.0;
        return 100.0 * static_cast<double>(downloaded.load()) / static_cast<double>(size);
    }
};

enum class TaskStatus {
    Queued,
    Probing,       // HEAD request in progress to get size / range support
    Downloading,
    Paused,
    Completed,
    Error,
    Canceled
};

// A single download (one URL -> one output file), internally split into
// N segments for parallel/multi-connection downloading, mirroring how
// IDM-style managers achieve higher throughput than a single connection.
class DownloadTask : public QObject {
    Q_OBJECT
public:
    DownloadTask(QString id, QString url, QString filepath, QString referer, QObject* parent = nullptr);
    ~DownloadTask() override;

    void start(int segmentCount = 8);
    void pause();
    void resume();
    void cancel();

    QString id() const { return m_id; }
    QString url() const { return m_url; }
    QString filepath() const { return m_filepath; }
    TaskStatus status() const { return m_status.load(); }
    long long totalBytes() const { return m_totalBytes.load(); }
    long long downloadedBytes() const;
    double overallPercent() const;
    long long speedBytesPerSec() const { return m_speed.load(); }
    QString errorMessage() const { return m_errorMessage; }
    bool supportsRanges() const { return m_supportsRanges.load(); }
    int segmentCount() const { return static_cast<int>(m_segments.size()); }
    double segmentPercent(int i) const;

signals:
    void updated();   // emitted (thread-safe, queued) whenever progress/state changes

private:
    void probeAndStart(int segmentCount);
    void runSegment(Segment* seg);
    void runSingleStream(); // fallback when server doesn't support Range
    void checkAllSegmentsDone();
    void speedTicker();

    QString m_id;
    QString m_url;
    QString m_filepath;
    QString m_referer;

    std::atomic<TaskStatus> m_status{TaskStatus::Queued};
    std::atomic<long long> m_totalBytes{-1};
    std::atomic<bool> m_supportsRanges{false};
    std::atomic<bool> m_pauseRequested{false};
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<long long> m_speed{0};

    std::vector<std::unique_ptr<Segment>> m_segments;
    std::thread m_probeThread;
    std::thread m_speedThread;
    std::atomic<bool> m_speedThreadRunning{false};

    long long m_lastTickBytes = 0;
    QString m_errorMessage;
    std::mutex m_stateMutex;
};
