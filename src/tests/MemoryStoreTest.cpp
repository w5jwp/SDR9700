// QtTest invokes private slots through the generated meta-object.
#include "MemoryStore.h"
#include "MemoryConstants.h"
#include "MemoryEditorPolicy.h"
#include "MemoryRecordHelpers.h"

using namespace sdr9700::memory;

#include <QTemporaryDir>
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
    void radioMemoryConversionRoundTrips();
    void editorPolicyNormalizesOffsets();
    void csvFileRoundTrips();
    void csvFileIoFailuresAreReported();
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

void MemoryStoreTest::radioMemoryConversionRoundTrips()
{
    MemoryRecord memory = validMemory();
    memory.name = QStringLiteral("SAT TEST");
    memory.receiveHz = 145825000;
    memory.mode = modeFM;
    memory.filter = 2;
    memory.dataMode = 1;
    memory.scan = 2;
    memory.duplexMode = dmDupMinus;
    memory.offsetHz = 600000;
    memory.toneMode = ratrTT;
    memory.toneValue = 1000;
    memory.tone = QStringLiteral("100.0");
    memory.tsql = QStringLiteral("100.0");

    const MemoryRecord converted = recordFromRadioMemory(radioMemoryFromRecord(memory, 1, 5));

    QCOMPARE(converted.group, quint16(1));
    QCOMPARE(converted.channel, quint16(5));
    QCOMPARE(converted.name, memory.name);
    QCOMPARE(converted.receiveHz, memory.receiveHz);
    QCOMPARE(converted.mode, memory.mode);
    QCOMPARE(converted.filter, memory.filter);
    QCOMPARE(converted.dataMode, memory.dataMode);
    QCOMPARE(converted.scan, memory.scan);
    QCOMPARE(converted.duplexMode, memory.duplexMode);
    QCOMPARE(converted.offsetHz, memory.offsetHz);
    QCOMPARE(converted.toneMode, memory.toneMode);
    QCOMPARE(converted.tone, memory.tone);
    QCOMPARE(converted.tsql, memory.tsql);
}

void MemoryStoreTest::editorPolicyNormalizesOffsets()
{
    QCOMPARE(normalizedOffsetForModeAndHz(dmSimplex, 600000, 145000000), quint64(0));
    QCOMPARE(normalizedOffsetForModeAndHz(dmDupPlus, 0, 145000000), quint64(600000));
    QCOMPARE(normalizedOffsetForModeAndHz(dmDupMinus, 123000, 145000000), quint64(123000));
    QVERIFY(modeSupportsMemoryOffset(modeFM));
    QVERIFY(modeSupportsMemoryOffset(modeDV));
    QVERIFY(!modeSupportsMemoryOffset(modeUSB));
}

void MemoryStoreTest::csvFileRoundTrips()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("memories.csv"));

    QString writeError;
    QVERIFY2(writeMemoriesCsvFile(path, {validMemory()}, &writeError), qPrintable(writeError));

    QStringList importErrors;
    QString fileError;
    const QVector<MemoryRecord> imported = readMemoriesCsvFile(path, &importErrors, &fileError);
    QCOMPARE(fileError, QString());
    QCOMPARE(importErrors, QStringList());
    QCOMPARE(imported.size(), 1);
    QCOMPARE(imported.constFirst().name, validMemory().name);
    QCOMPARE(imported.constFirst().receiveHz, validMemory().receiveHz);
}

void MemoryStoreTest::csvFileIoFailuresAreReported()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    QString writeError;
    QVERIFY(!writeMemoriesCsvFile(directory.path(), {validMemory()}, &writeError));
    QVERIFY(!writeError.isEmpty());

    QStringList importErrors;
    QString fileError;
    QVERIFY(
        readMemoriesCsvFile(directory.filePath(QStringLiteral("missing.csv")), &importErrors, &fileError).isEmpty());
    QVERIFY(!fileError.isEmpty());
    QCOMPARE(importErrors, QStringList());
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
