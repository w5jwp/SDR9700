#pragma once
// SDR9700 preferences: transient connection/audio settings consumed by the
// runtime radio stack. RadioProfile owns saved profile persistence and may map
// overlapping fields into this struct when opening a connection.
#include <QString>
#include <QColor>
#include "Types.h"
#include "AudioConverter.h"

struct Preferences
{
    QString RadioHostName;
    quint16 RadioPort = 50001;
    QString Username;
    QString Password;
    quint16 CivAddr = 0xA2;
    audioSetup RxSetup;
    audioSetup TxSetup;
};
