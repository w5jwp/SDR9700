#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace sdr9700::audio
{
QByteArray generateDtmfPcm(const QString& digits, quint32 sampleRate = 16000);
}
