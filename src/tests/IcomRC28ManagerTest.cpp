#include "IcomRC28Manager.h"

#include <QTest>

class IcomRC28ManagerTest : public QObject
{
    Q_OBJECT

  private slots:
    void ledReportMatchesRc28Protocol();
};

void IcomRC28ManagerTest::ledReportMatchesRc28Protocol()
{
    const QByteArray report =
        IcomRC28Manager::ledReport(IcomRC28Manager::kLedsAllOff & static_cast<uint8_t>(~IcomRC28Manager::kLedBitLink));
    QCOMPARE(report.size(), 33);
    QCOMPARE(static_cast<quint8>(report.at(0)), quint8(0x00));
    QCOMPARE(static_cast<quint8>(report.at(1)), quint8(0x01));
    QCOMPARE(static_cast<quint8>(report.at(2)), quint8(0x07));
    QCOMPARE(report.mid(3), QByteArray(30, '\0'));
}

QTEST_GUILESS_MAIN(IcomRC28ManagerTest)

#include "IcomRC28ManagerTest.moc"
