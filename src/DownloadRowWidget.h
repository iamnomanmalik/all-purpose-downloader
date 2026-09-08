#pragma once

#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <vector>
#include "DownloadTask.h"

// One "card" in the download list: title/speed header, an overall
// percent line, one thin progress row per active segment, and
// pause/resume/cancel controls — matching the "stacked rows" mockup.
class DownloadRowWidget : public QWidget {
    Q_OBJECT
public:
    explicit DownloadRowWidget(DownloadTask* task, QWidget* parent = nullptr);

public slots:
    void refresh();

signals:
    void openFolderRequested(const QString& filepath);

private:
    void buildSegmentRows();

    DownloadTask* m_task;
    QLabel* m_titleLabel;
    QLabel* m_speedLabel;
    QLabel* m_subLabel;
    QVBoxLayout* m_segmentsLayout;
    QPushButton* m_pauseResumeBtn;
    QPushButton* m_cancelBtn;
    std::vector<QProgressBar*> m_segmentBars;
    std::vector<QLabel*> m_segmentLabels;
};
