#pragma once

#include <QByteArray>
#include <QVector>

// Converts raw IC-9700 scope bytes (CI-V 0x27 response data) to native
// display levels for the bandscope and waterfall renderers.
//
// The IC-9700 encodes each scope point as a byte 0x00-0xA0 (0-160),
// where 0 is the display floor and 160 is the display ceiling.
namespace ScopeAdapter
{

inline QVector<float> toLevels(const QByteArray& raw)
{
    QVector<float> levels;
    levels.reserve(raw.size());
    for (unsigned char byte : raw)
    {
        const unsigned char clamped = byte > 160 ? static_cast<unsigned char>(160) : byte;
        levels.append(static_cast<float>(clamped));
    }
    return levels;
}

} // namespace ScopeAdapter
