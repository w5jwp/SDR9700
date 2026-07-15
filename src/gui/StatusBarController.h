#pragma once

#include "MainWindow.h"

#include <QObject>

class StatusBarController : public QObject
{
    Q_OBJECT

  public:
    explicit StatusBarController(MainWindow* window);

    void updateTxIndicator(bool on);
    void updateTxDurationLabel();
    void updateStatusClock();
    void toggleStatusClockMode();
    void updateSystemStats();
    void buildStatusBar();
    void showToast(const QString& msg, int durationMs, MainWindow::ToastKind kind);
    void updateNetworkQuality(int rttMs);

  private:
    MainWindow* m_window{nullptr};
};
