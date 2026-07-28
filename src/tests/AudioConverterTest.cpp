// QtTest invokes private slots through the generated meta-object.
#include "AudioConverter.h"

#include <QtTest>

#include <cstring>

class AudioConverterTest : public QObject
{
    Q_OBJECT

  private slots:
    void rejectsInvalidFormat();
    void rejectsConversionBeforeInitialization();
    void rejectsMalformedSampleData_data();
    void rejectsMalformedSampleData();
    void stereoInputAveragesBothChannels();
    void monoInputDuplicatesToBothChannels();
    void pcmuEncoderUsesStandardEdgeCodes();
    void pcmuDecoderPreservesSampleSign();
};

namespace
{
QAudioFormat audioFormat(int channels, QAudioFormat::SampleFormat sampleFormat)
{
    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(channels);
    format.setSampleFormat(sampleFormat);
    return format;
}
} // namespace

void AudioConverterTest::rejectsInvalidFormat()
{
    AudioConverter converter;
    QSignalSpy failedSpy(&converter, &AudioConverter::initFailed);

    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral("Invalid audio converter format.*")));
    QVERIFY(!converter.init(QAudioFormat(), LPCM, audioFormat(1, QAudioFormat::Int16), LPCM, 7, 4));
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.constFirst().constFirst().toString(), QStringLiteral("Invalid audio converter format"));
}

void AudioConverterTest::rejectsConversionBeforeInitialization()
{
    AudioConverter converter;

    QTest::ignoreMessage(QtWarningMsg, "AudioConverter::convert() called before successful initialization");
    QVERIFY(!converter.convert(audioPacket{}));
}

void AudioConverterTest::rejectsMalformedSampleData_data()
{
    QTest::addColumn<QAudioFormat::SampleFormat>("sampleFormat");
    QTest::addColumn<int>("byteCount");
    QTest::addColumn<QString>("warningPattern");

    QTest::newRow("partial-int16") << QAudioFormat::Int16 << 1
                                   << QStringLiteral("Dropping malformed Int16 audio packet.*");
    QTest::newRow("partial-int32") << QAudioFormat::Int32 << 3
                                   << QStringLiteral("Dropping malformed Int32 audio packet.*");
    QTest::newRow("partial-float") << QAudioFormat::Float << 3
                                   << QStringLiteral("Dropping malformed float audio packet.*");
}

void AudioConverterTest::rejectsMalformedSampleData()
{
    QFETCH(QAudioFormat::SampleFormat, sampleFormat);
    QFETCH(int, byteCount);
    QFETCH(QString, warningPattern);

    const QAudioFormat format = audioFormat(1, sampleFormat);
    AudioConverter converter;
    QVERIFY(converter.init(format, LPCM, format, LPCM, 7, 4));

    audioPacket packet;
    packet.data = QByteArray(byteCount, '\0');
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(warningPattern));
    QVERIFY(!converter.convert(packet));
}

void AudioConverterTest::stereoInputAveragesBothChannels()
{
    QAudioFormat input = audioFormat(2, QAudioFormat::Int16);

    QAudioFormat output = input;
    output.setChannelCount(1);

    AudioConverter converter;
    QVERIFY(converter.init(input, LPCM, output, LPCM, 7, 4));

    const qint16 samples[] = {0, 10000, -10000, 0};
    audioPacket packet;
    packet.data.resize(sizeof(samples));
    std::memcpy(packet.data.data(), samples, sizeof(samples));

    audioPacket converted;
    connect(&converter, &AudioConverter::converted, this,
            [&converted](const audioPacket& result) { converted = result; });
    QVERIFY(converter.convert(packet));
    QCOMPARE(converted.data.size(), qsizetype(2 * sizeof(qint16)));

    qint16 mono[2]{};
    std::memcpy(mono, converted.data.constData(), sizeof(mono));
    QCOMPARE(mono[0], qint16(5000));
    QCOMPARE(mono[1], qint16(-5000));
}

void AudioConverterTest::monoInputDuplicatesToBothChannels()
{
    QAudioFormat input = audioFormat(1, QAudioFormat::Int16);
    QAudioFormat output = input;
    output.setChannelCount(2);

    AudioConverter converter;
    QVERIFY(converter.init(input, LPCM, output, LPCM, 7, 4));

    const qint16 samples[] = {1000, -2000};
    audioPacket packet;
    packet.data.resize(sizeof(samples));
    std::memcpy(packet.data.data(), samples, sizeof(samples));

    audioPacket converted;
    connect(&converter, &AudioConverter::converted, this,
            [&converted](const audioPacket& result) { converted = result; });
    QVERIFY(converter.convert(packet));

    qint16 stereo[4]{};
    QCOMPARE(converted.data.size(), qsizetype(sizeof(stereo)));
    std::memcpy(stereo, converted.data.constData(), sizeof(stereo));
    QCOMPARE(stereo[0], qint16(1000));
    QCOMPARE(stereo[1], qint16(1000));
    QCOMPARE(stereo[2], qint16(-2000));
    QCOMPARE(stereo[3], qint16(-2000));
}

void AudioConverterTest::pcmuEncoderUsesStandardEdgeCodes()
{
    QAudioFormat format;
    format.setSampleRate(8000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Float);

    AudioConverter converter;
    QVERIFY(converter.init(format, LPCM, format, PCMU, 7, 4));

    const float samples[] = {0.0f, 1.0f, -1.0f};
    audioPacket packet;
    packet.data.resize(sizeof(samples));
    std::memcpy(packet.data.data(), samples, sizeof(samples));

    audioPacket converted;
    connect(&converter, &AudioConverter::converted, this,
            [&converted](const audioPacket& result) { converted = result; });
    QVERIFY(converter.convert(packet));
    QCOMPARE(converted.data.toHex(), QByteArrayLiteral("ff8000"));
}

void AudioConverterTest::pcmuDecoderPreservesSampleSign()
{
    QAudioFormat format;
    format.setSampleRate(8000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Float);

    AudioConverter converter;
    QVERIFY(converter.init(format, PCMU, format, LPCM, 7, 4));

    audioPacket packet;
    packet.data = QByteArray::fromHex("8000");
    audioPacket converted;
    connect(&converter, &AudioConverter::converted, this,
            [&converted](const audioPacket& result) { converted = result; });
    QVERIFY(converter.convert(packet));
    QCOMPARE(converted.data.size(), qsizetype(2 * sizeof(float)));

    float decoded[2]{};
    std::memcpy(decoded, converted.data.constData(), sizeof(decoded));
    QVERIFY(decoded[0] > 0.9f);
    QVERIFY(decoded[1] < -0.9f);
}

QTEST_GUILESS_MAIN(AudioConverterTest)
#include "AudioConverterTest.moc"
