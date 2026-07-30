#pragma once

#include <QDateTime>
#include <QDir>
#include <QString>

namespace sdr9700::memory
{
inline QString memoryExportFileName(const QDateTime& dateTime)
{
    return QStringLiteral("sdr9700-memories-%1.csv").arg(dateTime.toString(QStringLiteral("yyyy-MM-dd_HHmmss")));
}

inline QString memoryExportPath(const QString& directory, const QDateTime& dateTime)
{
    return QDir(directory).filePath(memoryExportFileName(dateTime));
}
} // namespace sdr9700::memory
