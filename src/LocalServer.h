#pragma once

#include <QObject>
#include <thread>
#include <atomic>
#include <functional>

// Wraps a tiny local HTTP server (cpp-httplib) that the Chrome extension
// posts detected video/file URLs to. Runs on its own thread; hands work
// back to the Qt/main thread via a callback that itself uses a queued
// connection under the hood (DownloadEngine::addDownload is called
// through a thread-safe invoke in MainWindow).
class LocalServer {
public:
    // onAddDownload(url, filename, referer) -> task id
    using AddDownloadFn = std::function<QString(const QString&, const QString&, const QString&)>;

    LocalServer(int port, AddDownloadFn onAddDownload);
    ~LocalServer();

    void start();
    void stop();

private:
    int m_port;
    AddDownloadFn m_onAddDownload;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    void* m_serverHandle = nullptr; // httplib::Server*, opaque to avoid header leaking everywhere
};
