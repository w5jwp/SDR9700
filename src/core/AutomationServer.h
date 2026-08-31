#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>

#include <functional>

class QLocalServer;
class QLocalSocket;

// AutomationServer is a deliberately small, local-only JSON-lines transport.
// It knows nothing about widgets or radio commands; the GUI supplies one
// allowlisted request handler. Keeping transport and action policy separate
// prevents a future generic QObject invocation feature from accidentally
// exposing PTT or another transmit path.
class AutomationServer final : public QObject
{
    Q_OBJECT

  public:
    using RequestHandler = std::function<QJsonObject(const QJsonObject&)>;

    explicit AutomationServer(QObject* parent = nullptr);
    ~AutomationServer() override;

    bool start(const RequestHandler& handler);
    void stop();
    QString socketName() const;
    QString discoveryFilePath() const { return m_discoveryFilePath; }
    int clientCount() const { return m_clients.size(); }

  signals:
    void clientCountChanged(int count);

  private:
    void acceptPendingConnections();
    void readClient(QLocalSocket* socket);
    void removeClient(QLocalSocket* socket);
    void processLine(QLocalSocket* socket, const QByteArray& line);
    bool writeDiscoveryFile();
    void removeDiscoveryFile();

    QLocalServer* m_server{nullptr};
    QHash<QLocalSocket*, QByteArray> m_clients;
    RequestHandler m_handler;
    QString m_discoveryFilePath;
};
