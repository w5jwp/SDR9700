// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QJsonDocument>
#include <QString>
#include <QVector>

#include "Types.h"

inline constexpr int kMemoryNameMaxChars = 22;

struct MemoryRecord
{
    QString id;
    int number{0};
    QString name;
    QString band;
    int bandKey{-1};
    quint64 receiveHz{0};
    QString shift;
    int duplexMode{dmSimplex};
    quint64 offsetHz{0};
    QString toneOption;
    QString toneFrequency;
    int toneMode{ratrNN};
    ushort toneValue{0};
    QString notes;
};

int memoryBandKeyForHz(quint64 hz);
QString memoryNumberLabel(int number);
QVector<MemoryRecord> normalizedMemoryNumbers(QVector<MemoryRecord> memories);
QJsonDocument memoriesExportDocument(const QVector<MemoryRecord>& memories);
QVector<MemoryRecord> memoriesFromDocument(const QJsonDocument& doc);
QString memoriesPath();
QVector<MemoryRecord> loadMemories();
bool saveMemories(const QVector<MemoryRecord>& memories);
