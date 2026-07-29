// QtTest invokes private slots through the generated meta-object.
#include "MemoryStore.h"

#include <QtTest>

class MemoryStoreTest : public QObject
{
    Q_OBJECT

  private slots:
    void wholeNumberToneRoundTrips();
    void quotedTextRoundTrips();
    void malformedCsvIsRejected();
    void missingRequiredColumnIsRejected();
    void frequencyOutsideSelectedBandIsRejected();
    void overlongNameIsRejected();
    void duplicateBandChannelIsRejected();
    void arbitraryCsvInputNeverCrashes();
};

namespace
{
MemoryRecord validMemory()
{
    MemoryRecord memory;
    memory.group = 1;
    memory.channel = 5;
    memory.name = QStringLiteral("Test Memory");
    memory.receiveHz = 146520000;
    memory.mode = modeFM;
    memory.filter = 1;
    memory.dataMode = 0;
    memory.scan = 0;
    memory.duplexMode = dmSimplex;
    memory.offsetHz = 0;
    memory.toneMode = ratrTN;
    memory.tone = QStringLiteral("100");
    memory.tsql.clear();
    memory.dsql = 0;
    memory.dtcs = 23;
    memory.dtcsB = 23;
    return memory;
}
} // namespace

void MemoryStoreTest::wholeNumberToneRoundTrips()
{
    const QByteArray csv = memoriesExportCsv({validMemory()});
    QStringList errors;
    const QVector<MemoryRecord> imported = memoriesFromCsv(csv, &errors);

    QCOMPARE(errors, QStringList());
    QCOMPARE(imported.size(), 1);
    QCOMPARE(imported.constFirst().group, quint16(1));
    QCOMPARE(imported.constFirst().channel, quint16(5));
    QCOMPARE(imported.constFirst().tone, QStringLiteral("100.0"));
    QCOMPARE(imported.constFirst().toneMode, int(ratrTN));
}

void MemoryStoreTest::quotedTextRoundTrips()
{
    MemoryRecord memory = validMemory();
    memory.name = QStringLiteral("A,\"B\",C");

    QStringList errors;
    const QVector<MemoryRecord> imported = memoriesFromCsv(memoriesExportCsv({memory}), &errors);

    QCOMPARE(errors, QStringList());
    QCOMPARE(imported.size(), 1);
    QCOMPARE(imported.constFirst().name, memory.name);
}

void MemoryStoreTest::malformedCsvIsRejected()
{
    QByteArray csv = memoriesExportCsv({validMemory()});
    csv.replace("Test Memory", "\"unterminated");

    QStringList errors;
    QVERIFY(memoriesFromCsv(csv, &errors).isEmpty());
    QVERIFY(!errors.isEmpty());
    QVERIFY(errors.join(QLatin1Char('\n')).contains(QStringLiteral("unterminated"), Qt::CaseInsensitive));
}

void MemoryStoreTest::missingRequiredColumnIsRejected()
{
    QByteArray csv = memoriesExportCsv({validMemory()});
    csv.replace("receiveHZ", "otherHZ");

    QStringList errors;
    QVERIFY(memoriesFromCsv(csv, &errors).isEmpty());
    QVERIFY(errors.contains(QStringLiteral("Missing CSV column: receiveHZ")));
}

void MemoryStoreTest::frequencyOutsideSelectedBandIsRejected()
{
    QByteArray csv = memoriesExportCsv({validMemory()});
    csv.replace("146520000", "440000000");

    QStringList errors;
    QVERIFY(memoriesFromCsv(csv, &errors).isEmpty());
    QVERIFY(errors.join(QLatin1Char('\n')).contains(QStringLiteral("not in the selected radio band")));
}

void MemoryStoreTest::overlongNameIsRejected()
{
    MemoryRecord memory = validMemory();
    memory.name = QString(kMemoryNameMaxChars + 1, QLatin1Char('X'));

    QStringList errors;
    QVERIFY(memoriesFromCsv(memoriesExportCsv({memory}), &errors).isEmpty());
    QVERIFY(errors.join(QLatin1Char('\n')).contains(QStringLiteral("name is limited")));
}

void MemoryStoreTest::duplicateBandChannelIsRejected()
{
    const MemoryRecord memory = validMemory();
    QStringList errors;
    const QVector<MemoryRecord> imported = memoriesFromCsv(memoriesExportCsv({memory, memory}), &errors);

    QVERIFY(imported.size() < 2);
    QVERIFY(errors.join(QLatin1Char('\n')).contains(QStringLiteral("duplicate band/channel")));
}

void MemoryStoreTest::arbitraryCsvInputNeverCrashes()
{
    quint32 state = 0x5d9700;
    for (int iteration = 0; iteration < 500; ++iteration)
    {
        state = state * 1103515245U + 12345U;
        QByteArray input(int(state % 512U), Qt::Uninitialized);
        for (int i = 0; i < input.size(); ++i)
        {
            state = state * 1103515245U + 12345U;
            input[i] = char(state >> 24);
        }
        QStringList errors;
        const QVector<MemoryRecord> records = memoriesFromCsv(input, &errors);
        QVERIFY(records.size() <= input.count('\n') + 1);
    }
}

QTEST_GUILESS_MAIN(MemoryStoreTest)
#include "MemoryStoreTest.moc"
