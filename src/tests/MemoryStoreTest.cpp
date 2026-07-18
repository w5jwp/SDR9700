// QtTest invokes private slots through the generated meta-object.
#include "MemoryStore.h"

#include <QtTest>

class MemoryStoreTest : public QObject
{
    Q_OBJECT

  private slots:
    void wholeNumberToneRoundTrips();
    void malformedCsvIsRejected();
    void duplicateBandChannelIsRejected();
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

void MemoryStoreTest::malformedCsvIsRejected()
{
    QByteArray csv = memoriesExportCsv({validMemory()});
    csv.replace("Test Memory", "\"unterminated");

    QStringList errors;
    QVERIFY(memoriesFromCsv(csv, &errors).isEmpty());
    QVERIFY(!errors.isEmpty());
    QVERIFY(errors.join(QLatin1Char('\n')).contains(QStringLiteral("unterminated"), Qt::CaseInsensitive));
}

void MemoryStoreTest::duplicateBandChannelIsRejected()
{
    const MemoryRecord memory = validMemory();
    QStringList errors;
    const QVector<MemoryRecord> imported = memoriesFromCsv(memoriesExportCsv({memory, memory}), &errors);

    QVERIFY(imported.size() < 2);
    QVERIFY(errors.join(QLatin1Char('\n')).contains(QStringLiteral("duplicate band/channel")));
}

QTEST_GUILESS_MAIN(MemoryStoreTest)
#include "MemoryStoreTest.moc"
