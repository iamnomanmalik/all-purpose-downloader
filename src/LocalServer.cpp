#include "LocalServer.h"
#include "httplib.h"
#include "json.hpp"
#include <QMetaObject>
#include <QCoreApplication>

using json = nlohmann::json;

LocalServer::LocalServer(int port, AddDownloadFn onAddDownload)
    : m_port(port), m_onAddDownload(std::move(onAddDownload)) {}

LocalServer::~LocalServer() { stop(); }

void LocalServer::start() {
    m_running = true;
    m_thread = std::thread([this]() {
        httplib::Server svr;
        m_serverHandle = &svr;

        svr.set_default_headers({
            {"Access-Control-Allow-Origin", "*"},
            {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"},
            {"Access-Control-Allow-Headers", "Content-Type"}
        });

        svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
            res.status = 204;
        });

        svr.Get("/ping", [](const httplib::Request&, httplib::Response& res) {
            res.set_content(R"({"ok":true})", "application/json");
        });

        svr.Post("/add-download", [this](const httplib::Request& req, httplib::Response& res) {
            try {
                json body = json::parse(req.body);
                QString url = QString::fromStdString(body.value("url", ""));
                QString filename = QString::fromStdString(body.value("filename", ""));
                QString referer = QString::fromStdString(body.value("pageUrl", ""));

                if (url.isEmpty()) {
                    res.status = 400;
                    res.set_content(R"({"ok":false,"error":"Missing url"})", "application/json");
                    return;
                }

                QString resultId;
                // Hop onto the Qt/main thread to touch the engine safely,
                // and block this worker thread until it's done (fast op).
                QMetaObject::invokeMethod(qApp, [this, url, filename, referer, &resultId]() {
                    resultId = m_onAddDownload(url, filename, referer);
                }, Qt::BlockingQueuedConnection);

                json resp = {{"ok", true}, {"id", resultId.toStdString()}};
                res.set_content(resp.dump(), "application/json");
            } catch (const std::exception& e) {
                res.status = 400;
                json resp = {{"ok", false}, {"error", e.what()}};
                res.set_content(resp.dump(), "application/json");
            }
        });

        svr.listen("127.0.0.1", m_port);
        m_serverHandle = nullptr;
    });
}

void LocalServer::stop() {
    if (!m_running.exchange(false)) return;
    if (m_serverHandle) {
        static_cast<httplib::Server*>(m_serverHandle)->stop();
    }
    if (m_thread.joinable()) m_thread.join();
}
