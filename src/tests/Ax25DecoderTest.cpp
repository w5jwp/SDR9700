#include "Ax25Decoder.h"

#include <QTest>
#include <cmath>
#include <numbers>

namespace
{
QByteArray encodedAddress(const QByteArray& call, int ssid, bool last)
{
    QByteArray padded = call.leftJustified(6, ' ', true);
    QByteArray result;
    for (const char value : padded)
    {
        result.append(static_cast<char>(static_cast<quint8>(value) << 1));
    }
    result.append(static_cast<char>(0x60U | ((ssid & 0x0f) << 1) | (last ? 1 : 0)));
    return result;
}

QVector<bool> hdlcBits(const QByteArray& frame)
{
    QVector<bool> bits;
    auto appendByte = [&bits](quint8 value)
    {
        for (int bit = 0; bit < 8; ++bit)
        {
            bits.append((value & (1U << bit)) != 0);
        }
    };
    appendByte(0x7e);
    int ones = 0;
    for (const char value : frame)
    {
        for (int bit = 0; bit < 8; ++bit)
        {
            const bool one = (static_cast<quint8>(value) & (1U << bit)) != 0;
            bits.append(one);
            if (one)
            {
                if (++ones == 5)
                {
                    bits.append(false);
                    ones = 0;
                }
            }
            else
            {
                ones = 0;
            }
        }
    }
    appendByte(0x7e);
    return bits;
}

QVector<bool> nrziTones(const QVector<bool>& bits)
{
    QVector<bool> tones;
    bool tone = false;
    tones.append(tone);
    for (const bool bit : bits)
    {
        if (!bit)
        {
            tone = !tone;
        }
        tones.append(tone);
    }
    return tones;
}

QByteArray pcmForTones(const QVector<bool>& tones, int channels)
{
    constexpr int sampleRate = 48000;
    constexpr int samplesPerSymbol = sampleRate / 1200;
    QByteArray pcm;
    pcm.reserve(tones.size() * samplesPerSymbol * channels * 2);
    double phase = 0.0;
    for (const bool mark : tones)
    {
        const double increment = 2.0 * std::numbers::pi * (mark ? 1200.0 : 2200.0) / sampleRate;
        for (int sampleIndex = 0; sampleIndex < samplesPerSymbol; ++sampleIndex)
        {
            const qint16 sample = static_cast<qint16>(std::sin(phase) * 20000.0);
            phase += increment;
            for (int channel = 0; channel < channels; ++channel)
            {
                pcm.append(static_cast<char>(sample & 0xff));
                pcm.append(static_cast<char>((static_cast<quint16>(sample) >> 8) & 0xff));
            }
        }
    }
    return pcm;
}
} // namespace

class Ax25DecoderTest : public QObject
{
    Q_OBJECT

  private slots:
    void crcKnownCheck();
    void decodesUiFrame();
    void rejectsBadFcs();
    void decodesStereoAudio();
};

void Ax25DecoderTest::crcKnownCheck()
{
    QCOMPARE(Ax25Decoder::frameCheckSequence(QByteArrayLiteral("123456789")), quint16(0x906e));
}

void Ax25DecoderTest::decodesUiFrame()
{
    QByteArray frame = encodedAddress("APRS", 0, false) + encodedAddress("N0CALL", 7, true);
    frame.append(char(0x03));
    frame.append(char(0xf0));
    frame.append("Test packet");
    const quint16 fcs = Ax25Decoder::frameCheckSequence(frame);
    frame.append(static_cast<char>(fcs & 0xff));
    frame.append(static_cast<char>(fcs >> 8));

    Ax25Decoder decoder;
    const QVector<Ax25Frame> decoded = decoder.processNrziTones(nrziTones(hdlcBits(frame)));
    QCOMPARE(decoded.size(), 1);
    QCOMPARE(decoded.first().source, QStringLiteral("N0CALL-7"));
    QCOMPARE(decoded.first().destination, QStringLiteral("APRS"));
    QCOMPARE(decoded.first().type, QStringLiteral("UI"));
    QCOMPARE(decoded.first().payload, QStringLiteral("Test packet"));
    QCOMPARE(decoded.first().protocol, QStringLiteral("AX.25 (1200)"));
    QCOMPARE(decoder.stats().decoded, quint64(1));
    QCOMPARE(decoder.stats().candidates, quint64(1));
}

void Ax25DecoderTest::rejectsBadFcs()
{
    QByteArray frame = encodedAddress("APRS", 0, false) + encodedAddress("N0CALL", 0, true);
    frame.append(QByteArray::fromHex("03f00000"));
    Ax25Decoder decoder;
    QCOMPARE(decoder.processNrziTones(nrziTones(hdlcBits(frame))).size(), 0);
    QCOMPARE(decoder.stats().fcsFailures, quint64(1));
}

void Ax25DecoderTest::decodesStereoAudio()
{
    QByteArray frame = encodedAddress("APRS", 0, false) + encodedAddress("N0CALL", 0, true);
    frame.append(QByteArray::fromHex("03f0"));
    frame.append("Audio test");
    const quint16 fcs = Ax25Decoder::frameCheckSequence(frame);
    frame.append(static_cast<char>(fcs & 0xff));
    frame.append(static_cast<char>(fcs >> 8));

    Ax25Decoder decoder;
    const QVector<Ax25Frame> decoded = decoder.processPcm16(pcmForTones(nrziTones(hdlcBits(frame)), 2), 48000, 2);
    QVERIFY(!decoded.isEmpty());
    QCOMPARE(decoded.first().payload, QStringLiteral("Audio test"));
    QVERIFY(decoder.stats().audioLevel > 0);
    QCOMPARE(decoder.stats().candidates, quint64(1));
}

QTEST_APPLESS_MAIN(Ax25DecoderTest)
#include "Ax25DecoderTest.moc"
