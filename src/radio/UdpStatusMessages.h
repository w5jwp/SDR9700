#pragma once

#include "Types.h"

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

inline QString preparingRadioConnectionMessage(const QString& deviceName)
{
    return QStringLiteral("Found %1; preparing connection").arg(radioDisplayName(deviceName));
}

inline QString radioLoginAcceptedMessage()
{
    return QStringLiteral("Radio login accepted; requesting CI-V and audio streams");
}

inline QString recoveringRetainedSessionMessage(const QString& deviceName)
{
    return QStringLiteral("Recovering retained SDR9700 session on %1").arg(radioDisplayName(deviceName));
}

inline QString connectionErrorMessage(const errorType& error)
{
    if (error.code == ErrorCode::AuthFailure)
    {
        return QStringLiteral("Login denied; check the radio username and password");
    }

    const QString detail = error.message.trimmed();
    if (!detail.isEmpty())
    {
        return detail;
    }
    if (error.code == ErrorCode::Disconnected)
    {
        return QStringLiteral("Radio disconnected");
    }
    return QStringLiteral("Radio connection failed");
}

inline QString reconnectingMessage(QString message)
{
    message = message.trimmed();
    while (!message.isEmpty() && QStringLiteral(".!?;:").contains(message.back()))
    {
        message.chop(1);
    }
    if (message.isEmpty())
    {
        return QStringLiteral("Radio connection lost; reconnecting");
    }
    if (message.contains(QStringLiteral("reconnect"), Qt::CaseInsensitive))
    {
        return message;
    }
    return message + QStringLiteral("; reconnecting");
}
} // namespace sdr9700
