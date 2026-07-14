#include "AppSettings.h"
#include "LogCategories.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QList>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <algorithm>

namespace
{
bool settingStoresJson(const QString& key)
{
    static const QStringList kJsonSettings = {QStringLiteral("Memories"), QStringLiteral("RadioProfiles"),
                                              QStringLiteral("IcomRC28Settings"), QStringLiteral("RC28Settings")};
    return kJsonSettings.contains(key);
}
} // namespace

AppSettings& AppSettings::instance()
{
    static AppSettings settings;
    return settings;
}

AppSettings::AppSettings()
{
    load();
}

QVariant AppSettings::value(const QString& key, const QVariant& defaultValue) const
{
    const auto it = m_values.constFind(key);
    if (it == m_values.constEnd())
    {
        return defaultValue;
    }
    return *it;
}

bool AppSettings::setValue(const QString& key, const QVariant& settingValue)
{
    const bool hadPreviousValue = m_values.contains(key);
    const QString previousValue = m_values.value(key);
    m_values.insert(key, encodeValue(settingValue));
    if (!save())
    {
        if (hadPreviousValue)
        {
            m_values.insert(key, previousValue);
        }
        else
        {
            m_values.remove(key);
        }
        return false;
    }
    return true;
}

bool AppSettings::contains(const QString& key) const
{
    return m_values.contains(key);
}

bool AppSettings::remove(const QString& key)
{
    const bool hadPreviousValue = m_values.contains(key);
    const QString previousValue = m_values.value(key);
    m_values.remove(key);
    if (!save())
    {
        if (hadPreviousValue)
        {
            m_values.insert(key, previousValue);
        }
        return false;
    }
    return true;
}

bool AppSettings::save() const
{
    const QString path = settingsPath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return false;
    }

    QJsonObject settings;
    QList<QString> keys = m_values.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString& key : keys)
    {
        const QString storedValue = m_values.value(key);
        if (settingStoresJson(key))
        {
            QJsonParseError error;
            const QJsonDocument nested = QJsonDocument::fromJson(storedValue.toUtf8(), &error);
            if (error.error == QJsonParseError::NoError && nested.isObject())
            {
                settings.insert(key, nested.object());
                continue;
            }
            if (error.error == QJsonParseError::NoError && nested.isArray())
            {
                settings.insert(key, nested.array());
                continue;
            }
        }

        settings.insert(key, storedValue);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("settings"), settings);

    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(data) != static_cast<qint64>(data.size()))
    {
        return false;
    }
    if (!file.commit())
    {
        return false;
    }

    if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner))
    {
        qWarning(logSystem()) << "Could not set owner-only permissions on settings file:" << path;
    }
    return true;
}

void AppSettings::load()
{
    loadJson(settingsPath());
}

bool AppSettings::loadJson(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return false;
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
    {
        return false;
    }

    const QJsonObject root = doc.object();
    const QJsonObject settings = root.value(QStringLiteral("settings")).toObject(root);
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it)
    {
        if (it.value().isObject())
        {
            m_values.insert(it.key(),
                            QString::fromUtf8(QJsonDocument(it.value().toObject()).toJson(QJsonDocument::Compact)));
        }
        else if (it.value().isArray())
        {
            m_values.insert(it.key(),
                            QString::fromUtf8(QJsonDocument(it.value().toArray()).toJson(QJsonDocument::Compact)));
        }
        else
        {
            m_values.insert(it.key(), it.value().toVariant().toString());
        }
    }

    return true;
}

QString AppSettings::settingsPath()
{
    QString configRoot = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (configRoot.isEmpty())
    {
        configRoot = QDir::homePath() + "/.config";
    }
    return QDir(configRoot).filePath("SDR9700/config.json");
}

QString AppSettings::encodeValue(const QVariant& value)
{
    if (value.typeId() == QMetaType::Bool)
    {
        return value.toBool() ? QStringLiteral("True") : QStringLiteral("False");
    }
    if (value.typeId() == QMetaType::QByteArray)
    {
        return QString::fromUtf8(value.toByteArray());
    }
    return value.toString();
}
