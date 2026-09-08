#pragma once

#include <QMainWindow>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <memory>
#include "DownloadEngine.h"
#include "LocalServer.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void onTaskAdded(DownloadTask* task);
    void promptAddUrl();

    std::unique_ptr<DownloadEngine> m_engine;
    std::unique_ptr<LocalServer> m_server;
    QVBoxLayout* m_listLayout;
    QLabel* m_emptyState;
};
