#pragma once

#include <QByteArray>
#include <QVector>

// Converts raw IC-9700 scope bytes (CI-V 0x27 response data) to
// calibrated dBm float values for the waterfall renderer.
//
// The IC-9700 encodes each scope point as a byte 0x00-0xA0 (0-160),
// where 0 = minDbm and 160 = maxDbm. See the IC-9700 CI-V Reference Guide,
// command 27h scope data output.
namespace ScopeAdapter
{

inline QVector<float> toDbm(const QByteArray& raw, float minDbm = -130.0f, float maxDbm = -10.0f)
{
    QVector<float> bins;
    bins.reserve(raw.size());
    const float range = maxDbm - minDbm;
    for (unsigned char byte : raw)
    {
        const unsigned char clamped = byte > 160 ? static_cast<unsigned char>(160) : byte;
        bins.append(minDbm + (clamped / 160.0f) * range);
    }
    return bins;
}

} // namespace ScopeAdapter
