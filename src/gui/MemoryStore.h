// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QString>
#include <QVector>

#include "Types.h"

inline constexpr int kMemoryNameMaxChars = 22;

struct MemoryRecord
{
    QString id;
    quint16 group{0};
    quint16 channel{0};
    QString name;
    QString band;
    int bandKey{-1};
    quint64 receiveHz{0};
    int mode{modeFM};
    int filter{1};
    int dataMode{0};
    int scan{0};
    QString shift;
    int duplexMode{dmSimplex};
    quint64 offsetHz{0};
    QString toneOption;
    QString toneFrequency;
    QString tone;
    QString tsql;
    int toneMode{ratrNN};
    ushort toneValue{0};
    int dsql{0};
    ushort dtcs{23};
    int dtcsPolarity{0};
    ushort dtcsB{23};
    int dtcsPolarityB{0};
    int dvSql{0};
    QString urCall;
    QString r1Call;
    QString r2Call;
    QString notes;
};

int memoryBandKeyForHz(quint64 hz);
QString memoryBandLabelForGroup(quint16 group);
QByteArray memoriesExportCsv(const QVector<MemoryRecord>& memories);
QVector<MemoryRecord> memoriesFromCsv(const QByteArray& data);
