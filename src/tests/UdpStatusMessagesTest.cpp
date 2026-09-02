#include "radio/UdpStatusMessages.h"

#include <QtTest>

class UdpStatusMessagesTest : public QObject
{
    Q_OBJECT

  private slots:
    void identifiesBusyStation();
    void handlesMissingStationName();
    void preservesSpecificConnectionFailures();
    void describesAutomaticReconnect();
};

void UdpStatusMessagesTest::identifiesBusyStation()
{
    QCOMPARE(sdr9700::waitingForBusyRadioMessage(QStringLiteral("IC-9700"), QStringLiteral("W5JWP"),
                                                 QStringLiteral("192.0.2.10")),
             QStringLiteral("Waiting for IC-9700; in use by W5JWP (192.0.2.10)"));
}

void UdpStatusMessagesTest::handlesMissingStationName()
{
    QCOMPARE(sdr9700::waitingForBusyRadioMessage(QStringLiteral("IC-9700"), {}, QStringLiteral("192.0.2.10")),
             QStringLiteral("Waiting for IC-9700; in use by station at 192.0.2.10"));
    QCOMPARE(sdr9700::waitingForBusyRadioMessage({}, {}, {}),
             QStringLiteral("Waiting for radio; in use by another station"));
}

void UdpStatusMessagesTest::preservesSpecificConnectionFailures()
{
    QCOMPARE(sdr9700::connectionErrorMessage(
                 errorType(false, QStringLiteral("192.0.2.1"),
                           QStringLiteral("CI-V stream did not respond to radio commands; reconnecting."),
                           ErrorCode::Disconnected)),
             QStringLiteral("CI-V stream did not respond to radio commands; reconnecting."));
    QCOMPARE(sdr9700::connectionErrorMessage(errorType(false, {}, {}, ErrorCode::Disconnected)),
             QStringLiteral("Radio disconnected"));
    QCOMPARE(sdr9700::connectionErrorMessage(errorType(
                 true, {}, QStringLiteral("The radio rejected the username or password."), ErrorCode::AuthFailure)),
             QStringLiteral("Login denied; check the radio username and password"));
    QCOMPARE(sdr9700::connectionErrorMessage(errorType(true, {}, {}, ErrorCode::ConnectionFailed)),
             QStringLiteral("Radio connection failed"));
}

void UdpStatusMessagesTest::describesAutomaticReconnect()
{
    QCOMPARE(sdr9700::reconnectingMessage({}), QStringLiteral("Radio connection lost; reconnecting"));
    QCOMPARE(sdr9700::reconnectingMessage(QStringLiteral("Unable to bind the CI-V UDP socket.")),
             QStringLiteral("Unable to bind the CI-V UDP socket; reconnecting"));
    QCOMPARE(sdr9700::reconnectingMessage(QStringLiteral("Radio communication stalled; reconnecting.")),
             QStringLiteral("Radio communication stalled; reconnecting"));
}

QTEST_APPLESS_MAIN(UdpStatusMessagesTest)
#include "UdpStatusMessagesTest.moc"
