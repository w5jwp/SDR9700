#pragma once

#include <QString>

namespace sdr9700
{
inline QString radioDisplayName(const QString& deviceName)
{
    const QString name = deviceName.trimmed();
    return name.isEmpty() ? QStringLiteral("radio") : name;
}

inline QString waitingForBusyRadioMessage(const QString& deviceName, const QString& stationName,
                                          const QString& stationAddress)
{
    const QString radio = radioDisplayName(deviceName);
    const QString station = stationName.trimmed();
    const QString address = stationAddress.trimmed();
    if (!station.isEmpty() && !address.isEmpty())
    {
        return QStringLiteral("Waiting for %1; in use by %2 (%3)").arg(radio, station, address);
    }
    if (!station.isEmpty())
    {
        return QStringLiteral("Waiting for %1; in use by %2").arg(radio, station);
    }
    if (!address.isEmpty())
    {
        return QStringLiteral("Waiting for %1; in use by station at %2").arg(radio, address);
    }
    return QStringLiteral("Waiting for %1; in use by another station").arg(radio);
}
} // namespace sdr9700
