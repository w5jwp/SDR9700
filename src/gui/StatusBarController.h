#pragma once

#include "MainWindow.h"
#include "SystemStats.h"

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
    void showToast(const QString& msg, int durationMs);
    void showToast(const QString& msg, int durationMs, MainWindow::ToastKind kind);
    void clearPersistentToast(const QString& expectedMessage);
    void updateNetworkQuality(int rttMs);
    void setAutomationEnabled(bool enabled);
    void setAutomationClientCount(int count);

  private:
    void applyToast(const QString& message, MainWindow::ToastKind kind);

    MainWindow* m_window{nullptr};
    SystemStatsProvider m_systemStatsProvider;
    QString m_persistentMessage;
    MainWindow::ToastKind m_persistentKind{MainWindow::ToastKind::Info};
};
