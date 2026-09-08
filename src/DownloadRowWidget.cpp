#include "DownloadRowWidget.h"
#include <QHBoxLayout>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>

static QString fmtBytes(long long n) {
    if (n < 0) return "—";
    const char* units[] = {"B", "KB", "MB", "GB"};
    double v = static_cast<double>(n);
    int i = 0;
    while (v >= 1024.0 && i < 3) { v /= 1024.0; i++; }
    return QString::number(v, 'f', 1) + " " + units[i];
}

static QString fmtSpeed(long long n) {
    if (n <= 0) return "0 KB/s";
    return fmtBytes(n) + "/s";
}

DownloadRowWidget::DownloadRowWidget(DownloadTask* task, QWidget* parent)
    : QWidget(parent), m_task(task) {
    setObjectName("card");
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(
        "#card { background: #1f232b; border: 1px solid #2a2f3a; border-radius: 12px; }"
        "QLabel { color: #e6e8ec; }"
        "QLabel.sub { color: #8a8f98; font-size: 11px; }"
        "QLabel.seg { color: #8a8f98; font-size: 10px; }"
        "QProgressBar { background: #14161b; border: none; border-radius: 4px; height: 8px; text-align: center; }"
        "QProgressBar::chunk { background: #3b82f6; border-radius: 4px; }"
        "QPushButton { background: #2a2e37; color: white; border: none; border-radius: 7px; padding: 6px 12px; font-size: 11px; }"
        "QPushButton:hover { background: #363c49; }"
    );

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 12, 14, 12);
    outer->setSpacing(6);

    auto* headerRow = new QHBoxLayout();
    m_titleLabel = new QLabel(QFileInfo(task->filepath()).fileName());
    m_titleLabel->setStyleSheet("font-weight: 600; font-size: 13px;");
    m_speedLabel = new QLabel("—");
    m_speedLabel->setStyleSheet("font-weight: 600; font-size: 12px; color: #60a5fa;");
    headerRow->addWidget(m_titleLabel, 1);
    headerRow->addWidget(m_speedLabel);
    outer->addLayout(headerRow);

    m_subLabel = new QLabel("Probing…");
    m_subLabel->setProperty("class", "sub");
    m_subLabel->setStyleSheet("color: #8a8f98; font-size: 11px;");
    outer->addWidget(m_subLabel);

    m_segmentsLayout = new QVBoxLayout();
    m_segmentsLayout->setSpacing(4);
    outer->addLayout(m_segmentsLayout);

    auto* btnRow = new QHBoxLayout();
    m_pauseResumeBtn = new QPushButton("Pause");
    m_cancelBtn = new QPushButton("Cancel");
    btnRow->addStretch();
    btnRow->addWidget(m_pauseResumeBtn);
    btnRow->addWidget(m_cancelBtn);
    outer->addLayout(btnRow);

    connect(m_pauseResumeBtn, &QPushButton::clicked, this, [this]() {
        auto st = m_task->status();
        if (st == TaskStatus::Downloading) m_task->pause();
        else if (st == TaskStatus::Paused || st == TaskStatus::Error) m_task->resume();
        else if (st == TaskStatus::Completed) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_task->filepath()).absolutePath()));
        }
    });
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        m_task->cancel();
    });

    connect(task, &DownloadTask::updated, this, &DownloadRowWidget::refresh, Qt::QueuedConnection);
    refresh();
}

void DownloadRowWidget::buildSegmentRows() {
    int n = m_task->segmentCount();
    if (n <= 0 || !m_segmentBars.empty()) return;

    // Cap visible rows for very high segment counts so the card stays compact.
    int visible = std::min(n, 8);
    for (int i = 0; i < visible; ++i) {
        auto* row = new QHBoxLayout();
        auto* label = new QLabel(QString("seg %1").arg(i + 1));
        label->setFixedWidth(42);
        label->setStyleSheet("color: #8a8f98; font-size: 10px;");
        auto* bar = new QProgressBar();
        bar->setRange(0, 100);
        bar->setTextVisible(false);
        bar->setFixedHeight(8);
        row->addWidget(label);
        row->addWidget(bar, 1);
        m_segmentsLayout->addLayout(row);
        m_segmentBars.push_back(bar);
        m_segmentLabels.push_back(label);
    }
}

void DownloadRowWidget::refresh() {
    buildSegmentRows();

    auto status = m_task->status();
    QString statusText;
    switch (status) {
        case TaskStatus::Queued: statusText = "queued"; break;
        case TaskStatus::Probing: statusText = "probing…"; break;
        case TaskStatus::Downloading: statusText = "downloading"; break;
        case TaskStatus::Paused: statusText = "paused"; break;
        case TaskStatus::Completed: statusText = "completed"; break;
        case TaskStatus::Error: statusText = "error: " + m_task->errorMessage(); break;
        case TaskStatus::Canceled: statusText = "canceled"; break;
    }

    m_subLabel->setText(QString("%1 / %2 · %3% · %4")
        .arg(fmtBytes(m_task->downloadedBytes()))
        .arg(fmtBytes(m_task->totalBytes()))
        .arg(QString::number(m_task->overallPercent(), 'f', 0))
        .arg(statusText));

    m_speedLabel->setText(status == TaskStatus::Downloading ? fmtSpeed(m_task->speedBytesPerSec()) : "");

    for (size_t i = 0; i < m_segmentBars.size(); ++i) {
        m_segmentBars[i]->setValue(static_cast<int>(m_task->segmentPercent(static_cast<int>(i))));
    }

    if (status == TaskStatus::Downloading) {
        m_pauseResumeBtn->setText("Pause");
        m_pauseResumeBtn->setEnabled(true);
    } else if (status == TaskStatus::Paused || status == TaskStatus::Error) {
        m_pauseResumeBtn->setText("Resume");
        m_pauseResumeBtn->setEnabled(true);
    } else if (status == TaskStatus::Completed) {
        m_pauseResumeBtn->setText("Open folder");
        m_pauseResumeBtn->setEnabled(true);
    } else {
        m_pauseResumeBtn->setEnabled(false);
    }
    m_cancelBtn->setEnabled(status != TaskStatus::Completed && status != TaskStatus::Canceled);
}
