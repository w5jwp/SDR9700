#include "MemoryDatabase.h"

#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

class MemoryDatabaseTest : public QObject
{
    Q_OBJECT

  private slots:
    void recordsAreIsolatedByProfile();
    void repeatedUpdatesRemainBoundedAndDurable();
    void deletionRemovesOnlyTheRequestedSlot();
    void completeNativePayloadRoundTrips();
};

namespace
{
MemoryType testMemory(quint16 group, quint16 channel, quint64 hz)
{
    MemoryType memory;
    memory.group = group;
    memory.channel = channel;
    memory.frequency.Hz = hz;
    memory.frequency.MHzDouble = static_cast<double>(hz) / 1'000'000.0;
    memory.frequency.VFO = inactiveVFO;
    memory.mode = 5;
    memory.filter = 2;
    memory.tone = QStringLiteral("100.0");
    const QByteArray name = QStringLiteral("Memory %1").arg(channel).toLatin1();
    std::copy(name.cbegin(), name.cend(), memory.name);
    return memory;
}
} // namespace

void MemoryDatabaseTest::recordsAreIsolatedByProfile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MemoryDatabase database(directory.filePath(QStringLiteral("memories.sqlite3")));
    QString error;
    QVERIFY2(database.open(&error), qPrintable(error));
    const QUuid first = QUuid::createUuid();
    const QUuid second = QUuid::createUuid();

    QVERIFY2(database.store(first, testMemory(1, 1, 145000000), &error), qPrintable(error));
    QVERIFY2(database.store(second, testMemory(2, 1, 435000000), &error), qPrintable(error));

    QCOMPARE(database.memories(first, &error).size(), 1);
    QCOMPARE(database.memories(first, &error).constFirst().frequency.Hz, quint64(145000000));
    QCOMPARE(database.memories(second, &error).size(), 1);
    QCOMPARE(database.memories(second, &error).constFirst().frequency.Hz, quint64(435000000));
}

void MemoryDatabaseTest::repeatedUpdatesRemainBoundedAndDurable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("memories.sqlite3"));
    const QUuid profile = QUuid::createUuid();
    {
        MemoryDatabase database(path);
        QString error;
        QVERIFY2(database.open(&error), qPrintable(error));
        for (int pass = 0; pass < 100; ++pass)
        {
            for (quint16 channel = 1; channel <= 99; ++channel)
            {
                QVERIFY2(database.store(profile, testMemory(1, channel, 145000000 + pass * 100 + channel), &error),
                         qPrintable(error));
            }
        }
        QCOMPARE(database.memories(profile, &error).size(), 99);
    }
    MemoryDatabase reopened(path);
    QString error;
    QVERIFY2(reopened.open(&error), qPrintable(error));
    const QVector<MemoryType> records = reopened.memories(profile, &error);
    QCOMPARE(records.size(), 99);
    QCOMPARE(records.constFirst().frequency.Hz, quint64(145009901));
}

void MemoryDatabaseTest::deletionRemovesOnlyTheRequestedSlot()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MemoryDatabase database(directory.filePath(QStringLiteral("memories.sqlite3")));
    QString error;
    QVERIFY2(database.open(&error), qPrintable(error));
    const QUuid profile = QUuid::createUuid();
    QVERIFY(database.store(profile, testMemory(1, 1, 145000000), &error));
    QVERIFY(database.store(profile, testMemory(1, 2, 145100000), &error));

    QVERIFY2(database.remove(profile, 1, 1, &error), qPrintable(error));
    const QVector<MemoryType> records = database.memories(profile, &error);
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.constFirst().channel, quint16(2));
}

void MemoryDatabaseTest::completeNativePayloadRoundTrips()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MemoryDatabase database(directory.filePath(QStringLiteral("memories.sqlite3")));
    QString error;
    QVERIFY2(database.open(&error), qPrintable(error));
    const QUuid profile = QUuid::createUuid();
    MemoryType expected = testMemory(3, 99, 1296000000);
    expected.sat = true;
    expected.frequencyB.Hz = 435100000;
    expected.modeB = 17;
    expected.dtcsB = 754;
    expected.ipplusB = true;
    std::copy_n("CQCQCQ", 6, expected.UR);
    std::copy_n("REPEATER", 8, expected.R2B);

    QVERIFY2(database.store(profile, expected, &error), qPrintable(error));
    const MemoryType actual = database.memories(profile, &error).constFirst();
    QCOMPARE(actual.group, expected.group);
    QCOMPARE(actual.channel, expected.channel);
    QCOMPARE(actual.frequency.Hz, expected.frequency.Hz);
    QCOMPARE(actual.frequencyB.Hz, expected.frequencyB.Hz);
    QCOMPARE(actual.modeB, expected.modeB);
    QCOMPARE(actual.dtcsB, expected.dtcsB);
    QCOMPARE(actual.ipplusB, expected.ipplusB);
    QCOMPARE(QByteArray(actual.UR, sizeof actual.UR), QByteArray(expected.UR, sizeof expected.UR));
    QCOMPARE(QByteArray(actual.R2B, sizeof actual.R2B), QByteArray(expected.R2B, sizeof expected.R2B));
    QCOMPARE(QByteArray(actual.name, sizeof actual.name), QByteArray(expected.name, sizeof expected.name));
}

QTEST_GUILESS_MAIN(MemoryDatabaseTest)
#include "MemoryDatabaseTest.moc"
