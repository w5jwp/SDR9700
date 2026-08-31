#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>

class MainWindow;

// AutomationUiDriver exposes a bounded set of ordinary Qt control operations.
// It does not invoke arbitrary methods: callers may list controls, activate
// buttons/actions/items, or set values on known editor classes. PTT-producing
// controls are rejected before an operation is queued.
class AutomationUiDriver final : public QObject
{
  public:
    explicit AutomationUiDriver(MainWindow* window);

    QJsonObject listControls();
    QJsonObject activate(const QJsonObject& request);
    QJsonObject setValue(const QJsonObject& request);

  private:
    void rebuildRegistry();
    QString registerObject(QObject* object);
    QObject* resolve(const QJsonObject& request);
    bool isInteractive(QObject* object) const;
    bool isPttProducing(QObject* object) const;
    QJsonObject describe(QObject* object, const QString& id) const;
    static QJsonObject reject(const QString& code, const QString& message);

    MainWindow* m_window{nullptr};
    QHash<QString, QPointer<QObject>> m_objects;
    quint64 m_nextId{1};
};
