#pragma once

#include <QObject>
#include <QString>
#include <QDir>
#include <vector>
#include <memory>
#include "DownloadTask.h"

// Owns every DownloadTask, and is the single point both the GUI and the
// local bridge server talk to.
class DownloadEngine : public QObject {
    Q_OBJECT
public:
    explicit DownloadEngine(QString downloadDir, QObject* parent = nullptr);

    // Adds a new download and starts it immediately. Returns the task id.
    QString addDownload(const QString& url, QString filename, const QString& referer);

    DownloadTask* find(const QString& id);
    const std::vector<std::unique_ptr<DownloadTask>>& tasks() const { return m_tasks; }

signals:
    void taskAdded(DownloadTask* task);

private:
    QString uniquePath(const QString& desiredPath) const;

    QString m_downloadDir;
    std::vector<std::unique_ptr<DownloadTask>> m_tasks;
    long long m_nextId = 1;
};
