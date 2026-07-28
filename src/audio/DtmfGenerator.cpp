#include "DtmfGenerator.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace
{
struct DtmfEntry
{
    char digit;
    double rowFrequency;
    double columnFrequency;
};

constexpr DtmfEntry kDtmfTable[] = {
    {'0', 941.0, 1336.0}, {'1', 697.0, 1209.0}, {'2', 697.0, 1336.0}, {'3', 697.0, 1477.0},
    {'4', 770.0, 1209.0}, {'5', 770.0, 1336.0}, {'6', 770.0, 1477.0}, {'7', 852.0, 1209.0},
    {'8', 852.0, 1336.0}, {'9', 852.0, 1477.0}, {'*', 941.0, 1209.0}, {'#', 941.0, 1477.0},
    {'A', 697.0, 1633.0}, {'B', 770.0, 1633.0}, {'C', 852.0, 1633.0}, {'D', 941.0, 1633.0},
};

constexpr int kToneMs = 200;
constexpr int kGapMs = 200;
constexpr double kAmplitude = 0.45;
constexpr double kPi = 3.14159265358979323846;
} // namespace

QByteArray sdr9700::audio::generateDtmfPcm(const QString& digits, quint32 sampleRate)
{
    if (sampleRate == 0)
    {
        return {};
    }

    const int toneSamples = static_cast<int>(sampleRate) * kToneMs / 1000;
    const int gapSamples = static_cast<int>(sampleRate) * kGapMs / 1000;
    QByteArray pcm;
    pcm.reserve(digits.size() * (toneSamples + gapSamples) * int(sizeof(qint16)));

    for (const QChar qch : digits)
    {
        const char digit = qch.toUpper().toLatin1();
        const auto entry = std::find_if(std::begin(kDtmfTable), std::end(kDtmfTable),
                                        [digit](const DtmfEntry& item) { return item.digit == digit; });
        if (entry == std::end(kDtmfTable))
        {
            continue;
        }

        for (int i = 0; i < toneSamples; ++i)
        {
            const double time = double(i) / sampleRate;
            const double value = kAmplitude * (std::sin(2.0 * kPi * entry->rowFrequency * time) +
                                               std::sin(2.0 * kPi * entry->columnFrequency * time));
            const qint16 sample = qint16(std::clamp(value, -1.0, 1.0) * 32767.0);
            pcm.append(reinterpret_cast<const char*>(&sample), sizeof(sample));
        }
        pcm.append(gapSamples * int(sizeof(qint16)), '\0');
    }
    return pcm;
}
