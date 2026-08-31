#include "AutomationServer.h"

#include <QFile>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTest>

class AutomationServerTest final : public QObject
{
    Q_OBJECT

  private:
    static QJsonObject exchange(QLocalSocket& socket, const QJsonObject& request)
    {
        socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
        socket.flush();
        QByteArray response;
        for (int attempt = 0; attempt < 50 && !response.contains('\n'); ++attempt)
        {
            QCoreApplication::processEvents();
            response.append(socket.readAll());
            if (!response.contains('\n'))
            {
                QTest::qWait(10);
            }
        }
        return QJsonDocument::fromJson(response.trimmed()).object();
    }

  private slots:
    void localTransportHandlesSustainedRequestLoad()
    {
        AutomationServer server;
        QVERIFY(server.start(
            [](const QJsonObject& request)
            {
                return QJsonObject{{QStringLiteral("ok"), true},
                                   {QStringLiteral("sequence"), request.value(QStringLiteral("sequence"))}};
            }));
        QVERIFY(QFile::exists(server.discoveryFilePath()));

        QSignalSpy clientCountSpy(&server, &AutomationServer::clientCountChanged);
        QLocalSocket client;
        client.connectToServer(server.socketName());
        QVERIFY(client.waitForConnected(2000));
        QTRY_COMPARE(server.clientCount(), 1);

        // A thousand serialized requests intentionally exceeds realistic UI
        // use so transport framing, buffering, and response identity are
        // exercised under the kind of burst an automated test can generate.
        for (int sequence = 0; sequence < 1000; ++sequence)
        {
            const QJsonObject response =
                exchange(client, QJsonObject{{QStringLiteral("action"), QStringLiteral("echo")},
                                             {QStringLiteral("sequence"), sequence},
                                             {QStringLiteral("id"), sequence}});
            QVERIFY(response.value(QStringLiteral("ok")).toBool());
            QCOMPARE(response.value(QStringLiteral("sequence")).toInt(), sequence);
            QCOMPARE(response.value(QStringLiteral("id")).toInt(), sequence);
        }

        client.disconnectFromServer();
        QVERIFY(client.waitForDisconnected(2000) || client.state() == QLocalSocket::UnconnectedState);
        QTRY_COMPARE(server.clientCount(), 0);
        QVERIFY(clientCountSpy.count() >= 2);
        const QString discoveryPath = server.discoveryFilePath();
        server.stop();
        QVERIFY(!QFile::exists(discoveryPath));
    }

    void malformedInputIsRejectedAndConnectionSurvives()
    {
        AutomationServer server;
        QVERIFY(server.start([](const QJsonObject&) { return QJsonObject{{QStringLiteral("ok"), true}}; }));
        QLocalSocket client;
        client.connectToServer(server.socketName());
        QVERIFY(client.waitForConnected(2000));

        client.write("not-json\n");
        client.flush();
        QTRY_VERIFY_WITH_TIMEOUT(client.canReadLine(), 2000);
        const QJsonObject invalid = QJsonDocument::fromJson(client.readLine().trimmed()).object();
        QCOMPARE(invalid.value(QStringLiteral("error")).toString(), QStringLiteral("invalid_json"));

        const QJsonObject valid = exchange(client, QJsonObject{{QStringLiteral("action"), QStringLiteral("ping")}});
        QVERIFY(valid.value(QStringLiteral("ok")).toBool());
    }
};

QTEST_GUILESS_MAIN(AutomationServerTest)
#include "AutomationServerTest.moc"
