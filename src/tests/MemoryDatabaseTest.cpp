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
    void partialSnapshotPreservesUnansweredSlotsAtomically();
    void completeSnapshotHandlesFullRadioVolume();
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

void compareCompleteMemory(const MemoryType& actual, const MemoryType& expected)
{
    QCOMPARE(actual.group, expected.group);
    QCOMPARE(actual.channel, expected.channel);
    QCOMPARE(actual.split, expected.split);
    QCOMPARE(actual.skip, expected.skip);
    QCOMPARE(actual.scan, expected.scan);
    QCOMPARE(actual.vfo, expected.vfo);
    QCOMPARE(actual.vfoB, expected.vfoB);
    QCOMPARE(actual.frequency.Hz, expected.frequency.Hz);
    QCOMPARE(actual.frequency.MHzDouble, expected.frequency.MHzDouble);
    QCOMPARE(actual.frequency.VFO, expected.frequency.VFO);
    QCOMPARE(actual.frequencyB.Hz, expected.frequencyB.Hz);
    QCOMPARE(actual.frequencyB.MHzDouble, expected.frequencyB.MHzDouble);
    QCOMPARE(actual.frequencyB.VFO, expected.frequencyB.VFO);
    QCOMPARE(actual.clarifier, expected.clarifier);
    QCOMPARE(actual.clarRX, expected.clarRX);
    QCOMPARE(actual.clarTX, expected.clarTX);
    QCOMPARE(actual.mode, expected.mode);
    QCOMPARE(actual.modeB, expected.modeB);
    QCOMPARE(actual.filter, expected.filter);
    QCOMPARE(actual.filterB, expected.filterB);
    QCOMPARE(actual.datamode, expected.datamode);
    QCOMPARE(actual.datamodeB, expected.datamodeB);
    QCOMPARE(actual.duplex, expected.duplex);
    QCOMPARE(actual.duplexB, expected.duplexB);
    QCOMPARE(actual.tonemode, expected.tonemode);
    QCOMPARE(actual.tonemodeB, expected.tonemodeB);
    QCOMPARE(actual.tone, expected.tone);
    QCOMPARE(actual.toneB, expected.toneB);
    QCOMPARE(actual.tsql, expected.tsql);
    QCOMPARE(actual.tsqlB, expected.tsqlB);
    QCOMPARE(actual.dsql, expected.dsql);
    QCOMPARE(actual.dsqlB, expected.dsqlB);
    QCOMPARE(actual.dtcs, expected.dtcs);
    QCOMPARE(actual.dtcsB, expected.dtcsB);
    QCOMPARE(actual.dtcsp, expected.dtcsp);
    QCOMPARE(actual.dtcspB, expected.dtcspB);
    QCOMPARE(actual.dvsql, expected.dvsql);
    QCOMPARE(actual.dvsqlB, expected.dvsqlB);
    QCOMPARE(actual.duplexOffset.Hz, expected.duplexOffset.Hz);
    QCOMPARE(actual.duplexOffset.MHzDouble, expected.duplexOffset.MHzDouble);
    QCOMPARE(actual.duplexOffset.VFO, expected.duplexOffset.VFO);
    QCOMPARE(actual.duplexOffsetB.Hz, expected.duplexOffsetB.Hz);
    QCOMPARE(actual.duplexOffsetB.MHzDouble, expected.duplexOffsetB.MHzDouble);
    QCOMPARE(actual.duplexOffsetB.VFO, expected.duplexOffsetB.VFO);
    QCOMPARE(QByteArray(actual.UR, sizeof actual.UR), QByteArray(expected.UR, sizeof expected.UR));
    QCOMPARE(QByteArray(actual.URB, sizeof actual.URB), QByteArray(expected.URB, sizeof expected.URB));
    QCOMPARE(QByteArray(actual.R1, sizeof actual.R1), QByteArray(expected.R1, sizeof expected.R1));
    QCOMPARE(QByteArray(actual.R2, sizeof actual.R2), QByteArray(expected.R2, sizeof expected.R2));
    QCOMPARE(QByteArray(actual.R1B, sizeof actual.R1B), QByteArray(expected.R1B, sizeof expected.R1B));
    QCOMPARE(QByteArray(actual.R2B, sizeof actual.R2B), QByteArray(expected.R2B, sizeof expected.R2B));
    QCOMPARE(actual.tuningStep, expected.tuningStep);
    QCOMPARE(actual.tuningStepB, expected.tuningStepB);
    QCOMPARE(actual.progTs, expected.progTs);
    QCOMPARE(actual.progTsB, expected.progTsB);
    QCOMPARE(actual.atten, expected.atten);
    QCOMPARE(actual.attenB, expected.attenB);
    QCOMPARE(actual.preamp, expected.preamp);
    QCOMPARE(actual.preampB, expected.preampB);
    QCOMPARE(actual.antenna, expected.antenna);
    QCOMPARE(actual.antennaB, expected.antennaB);
    QCOMPARE(actual.ipplus, expected.ipplus);
    QCOMPARE(actual.ipplusB, expected.ipplusB);
    QCOMPARE(QByteArray(actual.name, sizeof actual.name), QByteArray(expected.name, sizeof expected.name));
    QCOMPARE(actual.sat, expected.sat);
    QCOMPARE(actual.del, expected.del);
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
    // Satellite replies use their own 1A 07 namespace and do not carry the
    // normal memory-band group byte. Group zero preserves that distinction in
    // the database key while the sat flag selects the two-sided payload.
    MemoryType expected = testMemory(0, 99, 1296000000);
    expected.split = 1;
    expected.skip = 2;
    expected.scan = 3;
    expected.vfo = 4;
    expected.vfoB = 5;
    expected.sat = true;
    expected.frequencyB.Hz = 435100000;
    expected.frequencyB.MHzDouble = 435.1;
    expected.frequencyB.VFO = inactiveVFO;
    expected.clarifier = -1250;
    expected.clarRX = true;
    expected.clarTX = true;
    expected.mode = 12;
    expected.modeB = 17;
    expected.filter = 3;
    expected.filterB = 2;
    expected.datamode = 1;
    expected.datamodeB = 1;
    expected.duplex = 2;
    expected.duplexB = 3;
    expected.tonemode = 1;
    expected.tonemodeB = 2;
    expected.tone = QStringLiteral("88.5");
    expected.toneB = QStringLiteral("100.0");
    expected.tsql = QStringLiteral("123.0");
    expected.tsqlB = QStringLiteral("151.4");
    expected.dsql = 1;
    expected.dsqlB = 2;
    expected.dtcs = 245;
    expected.dtcsB = 754;
    expected.dtcsp = 3;
    expected.dtcspB = 2;
    expected.dvsql = 45;
    expected.dvsqlB = 67;
    expected.duplexOffset.Hz = 600000;
    expected.duplexOffset.MHzDouble = 0.6;
    expected.duplexOffset.VFO = activeVFO;
    expected.duplexOffsetB.Hz = 5000000;
    expected.duplexOffsetB.MHzDouble = 5.0;
    expected.duplexOffsetB.VFO = inactiveVFO;
    std::copy_n("DESTB", 5, expected.URB);
    expected.ipplusB = true;
    std::copy_n("CQCQCQ", 6, expected.UR);
    std::copy_n("RPTONE", 6, expected.R1);
    std::copy_n("RPTTWO", 6, expected.R2);
    std::copy_n("RPT1B", 5, expected.R1B);
    std::copy_n("REPEATER", 8, expected.R2B);
    expected.tuningStep = 6;
    expected.tuningStepB = 7;
    expected.progTs = 125;
    expected.progTsB = 250;
    expected.atten = 1;
    expected.attenB = 2;
    expected.preamp = 1;
    expected.preampB = 2;
    expected.antenna = 1;
    expected.antennaB = 2;
    expected.ipplus = true;

    QVERIFY2(database.store(profile, expected, &error), qPrintable(error));
    const MemoryType actual = database.memories(profile, &error).constFirst();
    compareCompleteMemory(actual, expected);
}

void MemoryDatabaseTest::partialSnapshotPreservesUnansweredSlotsAtomically()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MemoryDatabase database(directory.filePath(QStringLiteral("memories.sqlite3")));
    QString error;
    QVERIFY2(database.open(&error), qPrintable(error));
    const QUuid profile = QUuid::createUuid();
    QVERIFY(database.store(profile, testMemory(1, 1, 145000000), &error));
    QVERIFY(database.store(profile, testMemory(1, 2, 145100000), &error));
    QVERIFY(database.store(profile, testMemory(1, 3, 145200000), &error));

    MemoryType updated = testMemory(1, 1, 146000000);
    MemoryType explicitlyEmpty;
    explicitlyEmpty.group = 1;
    explicitlyEmpty.channel = 2;
    explicitlyEmpty.del = true;
    QVERIFY2(database.applySyncSnapshot(profile, {updated, explicitlyEmpty}, 3, &error), qPrintable(error));

    const QVector<MemoryType> records = database.memories(profile, &error);
    QCOMPARE(records.size(), 2);
    QCOMPARE(records.at(0).channel, quint16(1));
    QCOMPARE(records.at(0).frequency.Hz, quint64(146000000));
    QCOMPARE(records.at(1).channel, quint16(3));
    QCOMPARE(records.at(1).frequency.Hz, quint64(145200000));
    const MemoryDatabaseSyncState state = database.syncState(profile, &error);
    QVERIFY(state.completedAt.isValid());
    QCOMPARE(state.expectedSlotCount, 3);
    QCOMPARE(state.receivedSlotCount, 2);
    QVERIFY(!state.complete);

    // Invalid snapshots are rejected before a transaction starts and cannot
    // partially replace the last committed generation.
    QVERIFY(!database.applySyncSnapshot(profile, {updated, updated}, 2, &error));
    QCOMPARE(database.memories(profile).size(), 2);
    QCOMPARE(database.syncState(profile).receivedSlotCount, 2);
}

void MemoryDatabaseTest::completeSnapshotHandlesFullRadioVolume()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MemoryDatabase database(directory.filePath(QStringLiteral("memories.sqlite3")));
    QString error;
    QVERIFY2(database.open(&error), qPrintable(error));
    const QUuid profile = QUuid::createUuid();
    QVector<MemoryType> replies;
    replies.reserve(297);
    for (quint16 group = 1; group <= 3; ++group)
    {
        for (quint16 channel = 1; channel <= 99; ++channel)
        {
            MemoryType memory = testMemory(group, channel, 144000000ULL + group * 100000000ULL + channel);
            memory.del = (channel % 10) != 0;
            replies.append(memory);
        }
    }
    QCOMPARE(replies.size(), 297);
    QVERIFY2(database.applySyncSnapshot(profile, replies, replies.size(), &error), qPrintable(error));
    QCOMPARE(database.memories(profile, &error).size(), 27);
    const MemoryDatabaseSyncState state = database.syncState(profile, &error);
    QCOMPARE(state.expectedSlotCount, 297);
    QCOMPARE(state.receivedSlotCount, 297);
    QVERIFY(state.complete);
}

QTEST_GUILESS_MAIN(MemoryDatabaseTest)
#include "MemoryDatabaseTest.moc"
