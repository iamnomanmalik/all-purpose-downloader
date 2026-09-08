#include "MainWindow.h"
#include "DownloadRowWidget.h"
#include <QWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QInputDialog>
#include <QLineEdit>
#include <QStandardPaths>
#include <QDir>
#include <QScrollBar>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("All-Purpose Downloader");
    resize(480, 640);
    setStyleSheet(
        "QMainWindow { background: #14161b; }"
        "QLabel { color: #e7e9ee; }"
        "QPushButton { background: #3b82f6; color: white; border: none; border-radius: 7px; padding: 8px 16px; font-size: 12px; font-weight: 600; }"
        "QPushButton:hover { background: #2563eb; }"
        "QScrollArea { border: none; background: transparent; }"
    );

    auto* central = new QWidget();
    auto* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    auto* headerRow = new QHBoxLayout();
    auto* title = new QLabel("Downloads");
    title->setStyleSheet("font-size: 16px; font-weight: 700;");
    auto* addBtn = new QPushButton("+ Add URL");
    headerRow->addWidget(title, 1);
    headerRow->addWidget(addBtn);
    mainLayout->addLayout(headerRow);

    auto* portLabel = new QLabel("Bridge server: 127.0.0.1:38471 (used by the browser extension)");
    portLabel->setStyleSheet("color: #6b7280; font-size: 10.5px;");
    mainLayout->addWidget(portLabel);

    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("background: #14161b; border: none;");
    scrollArea->viewport()->setStyleSheet("background: #14161b;");
    auto* listContainer = new QWidget();
    listContainer->setStyleSheet("background: #14161b;");
    m_listLayout = new QVBoxLayout(listContainer);
    m_listLayout->setSpacing(10);
    m_listLayout->addStretch();
    scrollArea->setWidget(listContainer);
    mainLayout->addWidget(scrollArea, 1);

    m_emptyState = new QLabel("Koi download nahi hai abhi.\nExtension se bhejein ya + Add URL dabayein.");
    m_emptyState->setAlignment(Qt::AlignCenter);
    m_emptyState->setStyleSheet("color: #6b7280; font-size: 12.5px; padding: 60px 20px;");
    m_listLayout->insertWidget(0, m_emptyState);

    setCentralWidget(central);

    QString downloadDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/AllPurposeDownloader";
    m_engine = std::make_unique<DownloadEngine>(downloadDir);
    connect(m_engine.get(), &DownloadEngine::taskAdded, this, &MainWindow::onTaskAdded);

    connect(addBtn, &QPushButton::clicked, this, &MainWindow::promptAddUrl);

    m_server = std::make_unique<LocalServer>(38471,
        [this](const QString& url, const QString& filename, const QString& referer) -> QString {
            return m_engine->addDownload(url, filename, referer);
        });
    m_server->start();
}

MainWindow::~MainWindow() {
    if (m_server) m_server->stop();
}

void MainWindow::promptAddUrl() {
    bool ok = false;
    QString url = QInputDialog::getText(this, "Add download", "File/video URL:", QLineEdit::Normal, "", &ok);
    if (ok && !url.isEmpty()) {
        m_engine->addDownload(url, "", "");
    }
}

void MainWindow::onTaskAdded(DownloadTask* task) {
    m_emptyState->setVisible(false);
    auto* row = new DownloadRowWidget(task);
    m_listLayout->insertWidget(0, row); // newest first
}
