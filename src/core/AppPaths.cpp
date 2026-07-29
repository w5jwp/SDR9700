#include "AppPaths.h"

#include <QDir>
#include <QStandardPaths>

namespace sdr9700
{
QString configDirectory()
{
    QString root = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (root.isEmpty())
    {
        root = QDir::homePath() + QStringLiteral("/.config");
    }
    return QDir(root).filePath(QStringLiteral("SDR9700"));
}
} // namespace sdr9700
