#pragma once

#include <QJsonObject>
#include <QObject>

class AutomationServer;
class AutomationUiDriver;
class MainWindow;

// AutomationController is the complete policy boundary for external control.
// Semantic radio requests and bounded UI-control requests both pass through
// this class. AutomationUiDriver supports known Qt editor operations but never
// arbitrary QObject method invocation, and it rejects PTT-producing controls.
class AutomationController final : public QObject
{
    Q_OBJECT

  public:
    explicit AutomationController(MainWindow* window);

    bool start();
    QJsonObject execute(const QJsonObject& request);

  private:
    QJsonObject stateSnapshot() const;
    QJsonObject reject(const QString& code, const QString& message) const;
    bool receiveControlReady() const;

    MainWindow* m_window{nullptr};
    AutomationServer* m_server{nullptr};
    AutomationUiDriver* m_uiDriver{nullptr};
};
