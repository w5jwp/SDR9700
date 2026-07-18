#pragma once

#include "MainWindow.h"

#include <QObject>

class StatusBarController : public QObject
{
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
    void applyToast(const QString& message, MainWindow::ToastKind kind);

    MainWindow* m_window{nullptr};
    QString m_persistentMessage;
    MainWindow::ToastKind m_persistentKind{MainWindow::ToastKind::Info};
};
