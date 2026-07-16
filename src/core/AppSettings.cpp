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
bool audioSetting(const QString& key)
{
    static const QStringList kAudioSettings = {
        QStringLiteral("audioInputDeviceID"),
        QStringLiteral("audioOutputChannels"),
        QStringLiteral("audioOutputDeviceID"),
    };
    return kAudioSettings.contains(key);
}

bool bandscopeSetting(const QString& key)
{
    static const QStringList kBandscopeSettings = {
        QStringLiteral("bandScopeBackgroundColor"),  QStringLiteral("bandScopeCenterLineColor"),
        QStringLiteral("bandScopeGridDensity"),      QStringLiteral("bandScopeGridLineColor"),
        QStringLiteral("bandScopeInvertMouseWheel"), QStringLiteral("bandScopeSpanHZ"),
        QStringLiteral("bandScopeSpectrumHeight"),
    };
    return kBandscopeSettings.contains(key);
}

bool mainWindowSetting(const QString& key)
{
    static const QStringList kMainWindowSettings = {
        QStringLiteral("mainWindowPositionX"),
        QStringLiteral("mainWindowPositionY"),
        QStringLiteral("statusClockUTC"),
    };
    return kMainWindowSettings.contains(key);
}

bool radioSetting(const QString& key)
{
    static const QStringList kRadioSettings = {
        QStringLiteral("LANModLevel"),
        QStringLiteral("tuningStepHZ"),
        QStringLiteral("volumeLevel"),
    };
    return kRadioSettings.contains(key);
}

bool radioChooserSetting(const QString& key)
{
    static const QStringList kRadioChooserSettings = {
        QStringLiteral("autoConnect"),
        QStringLiteral("radioProfiles"),
    };
    return kRadioChooserSettings.contains(key);
}

bool accessoriesSetting(const QString& key)
{
    static const QStringList kAccessoriesSettings = {
        QStringLiteral("ICOMRC28ButtonMapping"),
    };
    return kAccessoriesSettings.contains(key);
}

bool settingStoresJson(const QString& key)
{
    static const QStringList kJsonSettings = {QStringLiteral("radioProfiles"), QStringLiteral("ICOMRC28ButtonMapping")};
    return kJsonSettings.contains(key);
}

void insertStoredSetting(QJsonObject* target, const QString& key, const QString& storedValue)
{
    if (settingStoresJson(key))
    {
        QJsonParseError error;
        const QJsonDocument nested = QJsonDocument::fromJson(storedValue.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError && nested.isObject())
        {
            target->insert(key, nested.object());
            return;
        }
        if (error.error == QJsonParseError::NoError && nested.isArray())
        {
            target->insert(key, nested.array());
            return;
        }
    }

    target->insert(key, storedValue);
}

QString audioStoredKey(const QString& key)
{
    if (key == QStringLiteral("audioInputDeviceID"))
    {
        return QStringLiteral("inputDeviceID");
    }
    if (key == QStringLiteral("audioOutputDeviceID"))
    {
        return QStringLiteral("outputDeviceID");
    }
    if (key == QStringLiteral("audioOutputChannels"))
    {
        return QStringLiteral("outputChannels");
    }
    return {};
}

QString bandscopeStoredKey(const QString& key)
{
    if (key == QStringLiteral("bandScopeBackgroundColor"))
    {
        return QStringLiteral("backgroundColor");
    }
    if (key == QStringLiteral("bandScopeCenterLineColor"))
    {
        return QStringLiteral("centerLineColor");
    }
    if (key == QStringLiteral("bandScopeGridDensity"))
    {
        return QStringLiteral("gridDensity");
    }
    if (key == QStringLiteral("bandScopeGridLineColor"))
    {
        return QStringLiteral("gridLineColor");
    }
    if (key == QStringLiteral("bandScopeInvertMouseWheel"))
    {
        return QStringLiteral("invertMouseWheel");
    }
    if (key == QStringLiteral("bandScopeSpanHZ"))
    {
        return QStringLiteral("spanHZ");
    }
    if (key == QStringLiteral("bandScopeSpectrumHeight"))
    {
        return QStringLiteral("spectrumHeight");
    }
    return {};
}

QString mainWindowStoredKey(const QString& key)
{
    if (key == QStringLiteral("mainWindowPositionX"))
    {
        return QStringLiteral("positionX");
    }
    if (key == QStringLiteral("mainWindowPositionY"))
    {
        return QStringLiteral("positionY");
    }
    if (key == QStringLiteral("statusClockUTC"))
    {
        return key;
    }
    return {};
}

void loadStoredSetting(QHash<QString, QString>* values, const QString& key, const QJsonValue& value)
{
    if (value.isUndefined() || value.isNull())
    {
        return;
    }
    if (value.isObject())
    {
        values->insert(key, QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact)));
    }
    else if (value.isArray())
    {
        values->insert(key, QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)));
    }
    else
    {
        values->insert(key, value.toVariant().toString());
    }
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
    const QString path = configPath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return false;
    }

    QJsonObject settings;
    QJsonObject audioSettings;
    QJsonObject bandscopeSettings;
    QJsonObject mainWindowSettings;
    QJsonObject radioSettings;
    QJsonObject radioChooserSettings;
    QJsonObject accessoriesSettings;
    QList<QString> keys = m_values.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString& key : keys)
    {
        const QString storedValue = m_values.value(key);
        if (audioSetting(key))
        {
            insertStoredSetting(&audioSettings, audioStoredKey(key), storedValue);
            continue;
        }
        if (bandscopeSetting(key))
        {
            insertStoredSetting(&bandscopeSettings, bandscopeStoredKey(key), storedValue);
            continue;
        }
        if (mainWindowSetting(key))
        {
            insertStoredSetting(&mainWindowSettings, mainWindowStoredKey(key), storedValue);
            continue;
        }
        if (radioSetting(key))
        {
            insertStoredSetting(&radioSettings, key, storedValue);
            continue;
        }
        if (radioChooserSetting(key))
        {
            insertStoredSetting(&radioChooserSettings, key, storedValue);
            continue;
        }
        if (accessoriesSetting(key))
        {
            insertStoredSetting(&accessoriesSettings, key, storedValue);
        }
    }
    if (!accessoriesSettings.isEmpty())
    {
        settings.insert(QStringLiteral("accessories"), accessoriesSettings);
    }
    if (!audioSettings.isEmpty())
    {
        settings.insert(QStringLiteral("audio"), audioSettings);
    }
    if (!bandscopeSettings.isEmpty())
    {
        settings.insert(QStringLiteral("bandScope"), bandscopeSettings);
    }
    if (!mainWindowSettings.isEmpty())
    {
        settings.insert(QStringLiteral("mainWindow"), mainWindowSettings);
    }
    if (!radioSettings.isEmpty())
    {
        settings.insert(QStringLiteral("radio"), radioSettings);
    }
    if (!radioChooserSettings.isEmpty())
    {
        settings.insert(QStringLiteral("radioChooser"), radioChooserSettings);
    }

    const QByteArray data = QJsonDocument(settings).toJson(QJsonDocument::Indented);
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
    loadJson(configPath());
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

    const QJsonObject settings = doc.object();
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it)
    {
        if (it.key() == QStringLiteral("audio") && it.value().isObject())
        {
            const QJsonObject audio = it.value().toObject();
            loadStoredSetting(&m_values, QStringLiteral("audioInputDeviceID"),
                              audio.value(QStringLiteral("inputDeviceID")));
            loadStoredSetting(&m_values, QStringLiteral("audioOutputChannels"),
                              audio.value(QStringLiteral("outputChannels")));
            loadStoredSetting(&m_values, QStringLiteral("audioOutputDeviceID"),
                              audio.value(QStringLiteral("outputDeviceID")));
            continue;
        }

        if (it.key() == QStringLiteral("bandScope") && it.value().isObject())
        {
            const QJsonObject bandscope = it.value().toObject();
            loadStoredSetting(&m_values, QStringLiteral("bandScopeBackgroundColor"),
                              bandscope.value(QStringLiteral("backgroundColor")));
            loadStoredSetting(&m_values, QStringLiteral("bandScopeCenterLineColor"),
                              bandscope.value(QStringLiteral("centerLineColor")));
            loadStoredSetting(&m_values, QStringLiteral("bandScopeGridDensity"),
                              bandscope.value(QStringLiteral("gridDensity")));
            loadStoredSetting(&m_values, QStringLiteral("bandScopeGridLineColor"),
                              bandscope.value(QStringLiteral("gridLineColor")));
            loadStoredSetting(&m_values, QStringLiteral("bandScopeInvertMouseWheel"),
                              bandscope.value(QStringLiteral("invertMouseWheel")));
            loadStoredSetting(&m_values, QStringLiteral("bandScopeSpanHZ"), bandscope.value(QStringLiteral("spanHZ")));
            loadStoredSetting(&m_values, QStringLiteral("bandScopeSpectrumHeight"),
                              bandscope.value(QStringLiteral("spectrumHeight")));
            continue;
        }

        if (it.key() == QStringLiteral("mainWindow") && it.value().isObject())
        {
            const QJsonObject mainWindow = it.value().toObject();
            loadStoredSetting(&m_values, QStringLiteral("mainWindowPositionX"),
                              mainWindow.value(QStringLiteral("positionX")));
            loadStoredSetting(&m_values, QStringLiteral("mainWindowPositionY"),
                              mainWindow.value(QStringLiteral("positionY")));
            loadStoredSetting(&m_values, QStringLiteral("statusClockUTC"),
                              mainWindow.value(QStringLiteral("statusClockUTC")));
            continue;
        }

        if (it.key() == QStringLiteral("radio") && it.value().isObject())
        {
            const QJsonObject radio = it.value().toObject();
            for (auto radioIt = radio.constBegin(); radioIt != radio.constEnd(); ++radioIt)
            {
                if (radioSetting(radioIt.key()))
                {
                    loadStoredSetting(&m_values, radioIt.key(), radioIt.value());
                }
            }
            continue;
        }

        if (it.key() == QStringLiteral("radioChooser") && it.value().isObject())
        {
            const QJsonObject radioChooser = it.value().toObject();
            loadStoredSetting(&m_values, QStringLiteral("autoConnect"),
                              radioChooser.value(QStringLiteral("autoConnect")));
            loadStoredSetting(&m_values, QStringLiteral("radioProfiles"),
                              radioChooser.value(QStringLiteral("radioProfiles")));
            continue;
        }

        if (it.key() == QStringLiteral("accessories") && it.value().isObject())
        {
            const QJsonObject accessories = it.value().toObject();
            loadStoredSetting(&m_values, QStringLiteral("ICOMRC28ButtonMapping"),
                              accessories.value(QStringLiteral("ICOMRC28ButtonMapping")));
        }
    }

    return true;
}

QString AppSettings::configPath()
{
    QString configRoot = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (configRoot.isEmpty())
    {
        configRoot = QDir::homePath() + "/.config";
    }
    return QDir(configRoot).filePath("SDR9700/sdr9700.json");
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
