// QtTest invokes private slots through the generated meta-object.
#include "AudioConverter.h"

#include <QtTest>

#include <cstring>

class AudioConverterTest : public QObject
{
    Q_OBJECT

  private slots:
    void stereoInputAveragesBothChannels();
    void pcmuEncoderUsesStandardEdgeCodes();
    void pcmuDecoderPreservesSampleSign();
};

void AudioConverterTest::stereoInputAveragesBothChannels()
{
    QAudioFormat input;
    input.setSampleRate(48000);
    input.setChannelCount(2);
    input.setSampleFormat(QAudioFormat::Int16);

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
