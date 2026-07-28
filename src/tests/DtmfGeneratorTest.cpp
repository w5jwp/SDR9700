#include "DtmfGenerator.h"

#include <QTest>
#include <algorithm>
#include <cstring>

class DtmfGeneratorTest : public QObject
{
    Q_OBJECT

  private slots:
    void generatesExpectedDuration();
    void acceptsLowercaseAndAllSupportedSymbols();
    void skipsUnsupportedCharacters();
    void appendsSilenceAfterEachTone();
    void rejectsZeroSampleRate();
};

void DtmfGeneratorTest::generatesExpectedDuration()
{
    constexpr quint32 sampleRate = 8000;
    const QByteArray pcm = sdr9700::audio::generateDtmfPcm(QStringLiteral("12"), sampleRate);
    QCOMPARE(pcm.size(), int(2 * sampleRate * 400 / 1000 * sizeof(qint16)));
    QVERIFY(std::any_of(pcm.cbegin(), pcm.cbegin() + int(sampleRate * 200 / 1000 * sizeof(qint16)),
                        [](char byte) { return byte != 0; }));
}

void DtmfGeneratorTest::acceptsLowercaseAndAllSupportedSymbols()
{
    const QString symbols = QStringLiteral("0123456789*#abcd");
    const QByteArray pcm = sdr9700::audio::generateDtmfPcm(symbols, 1000);
    QCOMPARE(pcm.size(), symbols.size() * 400 * int(sizeof(qint16)));
}

void DtmfGeneratorTest::skipsUnsupportedCharacters()
{
    QCOMPARE(sdr9700::audio::generateDtmfPcm(QStringLiteral("x1y"), 1000),
             sdr9700::audio::generateDtmfPcm(QStringLiteral("1"), 1000));
    QVERIFY(sdr9700::audio::generateDtmfPcm(QStringLiteral("xyz"), 1000).isEmpty());
}

void DtmfGeneratorTest::appendsSilenceAfterEachTone()
{
    constexpr int sampleRate = 1000;
    const QByteArray pcm = sdr9700::audio::generateDtmfPcm(QStringLiteral("1"), sampleRate);
    const int gapOffset = sampleRate * 200 / 1000 * int(sizeof(qint16));
    QVERIFY(std::all_of(pcm.cbegin() + gapOffset, pcm.cend(), [](char byte) { return byte == 0; }));
}

void DtmfGeneratorTest::rejectsZeroSampleRate()
{
    QVERIFY(sdr9700::audio::generateDtmfPcm(QStringLiteral("1"), 0).isEmpty());
}

QTEST_GUILESS_MAIN(DtmfGeneratorTest)

#include "DtmfGeneratorTest.moc"
