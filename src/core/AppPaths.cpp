#include "AppPaths.h"

#include <QDir>
#include <QStandardPaths>
#include <QtGlobal>

namespace sdr9700
{
QString configDirectory()
{
#if defined(Q_OS_MAC)
    QString root = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (root.isEmpty())
    {
        root = QDir::homePath() + QStringLiteral("/Library/Application Support");
    }
#else
    QString root = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (root.isEmpty())
    {
        root = QDir::homePath() + QStringLiteral("/.config");
    }
#endif
    return QDir(root).filePath(QStringLiteral("SDR9700"));
}
} // namespace sdr9700
