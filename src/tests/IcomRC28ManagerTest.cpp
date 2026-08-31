#include "IcomRC28Manager.h"

#include <QSignalSpy>
#include <QTest>

class IcomRC28ManagerTest : public QObject
{
    Q_OBJECT

  private slots:
    void ledReportMatchesRc28Protocol();
    void serialDescriptorOmitsProductPrefix();
    void buttonTransitionsPreservePttRelease();
    void closeReleasesHeldButtons();
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

void IcomRC28ManagerTest::serialDescriptorOmitsProductPrefix()
{
    QCOMPARE(IcomRC28Manager::normalizedSerialNumber(QStringLiteral("RC-28 01234567")), QStringLiteral("01234567"));
    QCOMPARE(IcomRC28Manager::normalizedSerialNumber(QStringLiteral("  RC-28 01234567  ")), QStringLiteral("01234567"));
    QCOMPARE(IcomRC28Manager::normalizedSerialNumber(QStringLiteral("01234567")), QStringLiteral("01234567"));
    QVERIFY(IcomRC28Manager::normalizedSerialNumber(QStringLiteral("RC-28")).isEmpty());
}

void IcomRC28ManagerTest::buttonTransitionsPreservePttRelease()
{
    using Transition = IcomRC28Manager::ButtonTransition;

    QCOMPARE(IcomRC28Manager::buttonTransitions(0x07, 0x06), QVector<Transition>({{3, 0}}));
    QCOMPARE(IcomRC28Manager::buttonTransitions(0x06, 0x04), QVector<Transition>({{1, 0}}));
    QCOMPARE(IcomRC28Manager::buttonTransitions(0x04, 0x05), QVector<Transition>({{3, 1}}));
    QCOMPARE(IcomRC28Manager::buttonTransitions(0x05, 0x07), QVector<Transition>({{1, 1}}));

    QCOMPARE(IcomRC28Manager::buttonTransitions(0x07, 0x00), QVector<Transition>({{3, 0}, {1, 0}, {2, 0}}));
    QCOMPARE(IcomRC28Manager::buttonTransitions(0x00, 0x07), QVector<Transition>({{3, 1}, {1, 1}, {2, 1}}));
}

void IcomRC28ManagerTest::closeReleasesHeldButtons()
{
    IcomRC28Manager manager;
    QSignalSpy buttonSpy(&manager, &IcomRC28Manager::buttonPressed);
    manager.m_prevButtons = 0x04; // PTT and F1 held.

    manager.close();

    QCOMPARE(buttonSpy.count(), 2);
    QCOMPARE(buttonSpy.at(0), QVariantList({3, 1}));
    QCOMPARE(buttonSpy.at(1), QVariantList({1, 1}));
    QCOMPARE(manager.m_prevButtons, uint8_t(0x07));
}

QTEST_GUILESS_MAIN(IcomRC28ManagerTest)

#include "IcomRC28ManagerTest.moc"
