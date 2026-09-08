#include "DownloadEngine.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <QDateTime>
#include <QUrl>

DownloadEngine::DownloadEngine(QString downloadDir, QObject* parent)
    : QObject(parent), m_downloadDir(std::move(downloadDir)) {
    QDir().mkpath(m_downloadDir);
}

QString DownloadEngine::uniquePath(const QString& desiredPath) const {
    if (!QFileInfo::exists(desiredPath)) return desiredPath;
    QFileInfo fi(desiredPath);
    QString base = fi.completeBaseName();
    QString ext = fi.suffix();
    QString dir = fi.absolutePath();
    int n = 1;
    QString candidate;
    do {
        candidate = ext.isEmpty()
            ? QString("%1/%2 (%3)").arg(dir, base).arg(n)
            : QString("%1/%2 (%3).%4").arg(dir, base).arg(n).arg(ext);
        n++;
    } while (QFileInfo::exists(candidate));
    return candidate;
}

QString DownloadEngine::addDownload(const QString& url, QString filename, const QString& referer) {
    if (filename.isEmpty()) {
        QString path = QUrl(url).path();
        filename = path.section('/', -1);
        if (filename.isEmpty()) filename = "download";
    }
    filename.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

    QString fullPath = uniquePath(m_downloadDir + "/" + filename);
    QString id = QString("dl_%1_%2").arg(QDateTime::currentMSecsSinceEpoch()).arg(m_nextId++);

    auto task = std::make_unique<DownloadTask>(id, url, fullPath, referer, this);
    DownloadTask* raw = task.get();
    m_tasks.push_back(std::move(task));

    raw->start(8); // 8 parallel segments by default
    emit taskAdded(raw);
    return id;
}

DownloadTask* DownloadEngine::find(const QString& id) {
    for (auto& t : m_tasks) {
        if (t->id() == id) return t.get();
    }
    return nullptr;
}
