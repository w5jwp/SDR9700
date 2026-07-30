#include "radio/UdpStatusMessages.h"

#include <QtTest>

class UdpStatusMessagesTest : public QObject
{
    Q_OBJECT

  private slots:
    void identifiesBusyStation();
    void handlesMissingStationName();
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

QTEST_APPLESS_MAIN(UdpStatusMessagesTest)
#include "UdpStatusMessagesTest.moc"
