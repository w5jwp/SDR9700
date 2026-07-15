#include "ConfigurationManager.h"

#include "AppSettings.h"
#include "MemoryStore.h"

#include <QCoreApplication>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMessageBox>
#include <QProcess>
#include <QSaveFile>
#include <QTimer>
#include <algorithm>
#include <limits>

namespace
{
constexpr auto kMemoryFileFilter = "SDR9700 memories (*.json);;JSON files (*.json);;All files (*)";
constexpr auto kConfigFileFilter = "SDR9700 configuration (*.json);;JSON files (*.json);;All files (*)";
constexpr auto kMemoryBackupFilter = "SDR9700 memory backups (sdr9700-memories-backup-*.json);;JSON files (*.json)";
constexpr auto kConfigBackupFilter = "SDR9700 configuration backups (sdr9700-backup-*.json);;JSON files (*.json)";

QString backupDirectoryPath()
{
    return QDir(QFileInfo(AppSettings::configPath()).absolutePath()).filePath(QStringLiteral("backups"));
}

QString timestamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
}

QString memoryBackupPath()
{
    return QDir(backupDirectoryPath()).filePath(QStringLiteral("sdr9700-memories-backup-%1.json").arg(timestamp()));
}

QString configurationBackupPath()
{
    return QDir(backupDirectoryPath()).filePath(QStringLiteral("sdr9700-backup-%1.json").arg(timestamp()));
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return false;
    }
    if (file.write(data) != static_cast<qint64>(data.size()))
    {
        return false;
    }
    return file.commit();
}

bool copyFileAtomic(const QString& sourcePath, const QString& targetPath)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly))
    {
        return false;
    }

    QDir().mkpath(QFileInfo(targetPath).absolutePath());
    return writeFile(targetPath, source.readAll());
}

bool boolStringValue(const QJsonValue& value, QJsonValue* normalized)
{
    if (value.isBool())
    {
        *normalized = value.toBool() ? QStringLiteral("True") : QStringLiteral("False");
        return true;
    }

    const QString text = value.toVariant().toString();
    if (text.compare(QStringLiteral("True"), Qt::CaseInsensitive) == 0 ||
        text.compare(QStringLiteral("False"), Qt::CaseInsensitive) == 0)
    {
        *normalized = text.compare(QStringLiteral("True"), Qt::CaseInsensitive) == 0 ? QStringLiteral("True")
                                                                                     : QStringLiteral("False");
        return true;
    }
    return false;
}

bool intStringValue(const QJsonValue& value, int min, int max, QJsonValue* normalized)
{
    bool ok = false;
    const int number = value.toVariant().toInt(&ok);
    if (!ok || number < min || number > max)
    {
        return false;
    }

    *normalized = QString::number(number);
    return true;
}

bool colorStringValue(const QJsonValue& value, QJsonValue* normalized)
{
    const QString color = value.toVariant().toString();
    if (!QColor::isValidColorName(color))
    {
        return false;
    }

    *normalized = QColor(color).name(QColor::HexRgb);
    return true;
}

bool stringValue(const QJsonValue& value, QJsonValue* normalized)
{
    if (value.isObject() || value.isArray() || value.isUndefined() || value.isNull())
    {
        return false;
    }

    *normalized = value.toVariant().toString();
    return true;
}

bool objectValue(const QJsonValue& value, QJsonValue* normalized)
{
    if (value.isObject())
    {
        *normalized = value.toObject();
        return true;
    }
    if (value.isString())
    {
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(value.toString().toUtf8(), &error);
        if (error.error == QJsonParseError::NoError && doc.isObject())
        {
            *normalized = doc.object();
            return true;
        }
    }
    return false;
}

QJsonObject sourceSettingsObject(const QJsonDocument& doc)
{
    if (!doc.isObject())
    {
        return {};
    }

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("settings")).isObject())
    {
        return root.value(QStringLiteral("settings")).toObject();
    }
    return root;
}

void insertCleanSetting(QJsonObject* group, const QJsonObject& source, const QString& key,
                        bool (*validator)(const QJsonValue&, QJsonValue*))
{
    QJsonValue normalized;
    if (validator(source.value(key), &normalized))
    {
        group->insert(key, normalized);
    }
}

QJsonObject cleanRadioProfiles(const QJsonObject& source)
{
    const QJsonObject radioChooser = source.value(QStringLiteral("radioChooser")).toObject();
    const QJsonValue value = radioChooser.value(QStringLiteral("radioProfiles"));
    QJsonValue normalized;
    if (!objectValue(value, &normalized))
    {
        return {};
    }

    const QJsonObject profileRoot = normalized.toObject();
    QJsonObject cleaned;
    QJsonValue lastProfileId;
    if (stringValue(profileRoot.value(QStringLiteral("lastProfileID")), &lastProfileId))
    {
        cleaned.insert(QStringLiteral("lastProfileID"), lastProfileId);
    }

    const QJsonArray profiles = profileRoot.value(QStringLiteral("profiles")).toArray();
    QJsonArray cleanedProfiles;
    for (const QJsonValue& profileValue : profiles)
    {
        if (!profileValue.isObject())
        {
            continue;
        }

        const QJsonObject profile = profileValue.toObject();
        QJsonObject cleanedProfile;
        for (const QString& key : {QStringLiteral("ID"), QStringLiteral("name"), QStringLiteral("host"),
                                   QStringLiteral("username"), QStringLiteral("password")})
        {
            QJsonValue normalized;
            if (stringValue(profile.value(key), &normalized))
            {
                cleanedProfile.insert(key, normalized);
            }
        }

        QJsonValue port;
        if (intStringValue(profile.value(QStringLiteral("port")), 1, 65535, &port))
        {
            cleanedProfile.insert(QStringLiteral("port"), port.toString().toInt());
        }

        if (cleanedProfile.contains(QStringLiteral("ID")) && cleanedProfile.contains(QStringLiteral("host")) &&
            cleanedProfile.contains(QStringLiteral("port")))
        {
            cleanedProfiles.append(cleanedProfile);
        }
    }
    cleaned.insert(QStringLiteral("profiles"), cleanedProfiles);
    return cleaned;
}

QJsonObject cleanIcomRC28ButtonMapping(const QJsonObject& source)
{
    const QJsonObject accessories = source.value(QStringLiteral("accessories")).toObject();
    QJsonValue value;
    if (!objectValue(accessories.value(QStringLiteral("ICOMRC28ButtonMapping")), &value))
    {
        return {};
    }

    const QJsonObject mapping = value.toObject();
    QJsonObject cleaned;
    for (const QString& key : {QStringLiteral("F1Press"), QStringLiteral("F1Hold"), QStringLiteral("F2Press"),
                               QStringLiteral("F2Hold"), QStringLiteral("PTTMode"), QStringLiteral("autoSnap")})
    {
        QJsonValue normalized;
        if (stringValue(mapping.value(key), &normalized))
        {
            cleaned.insert(key, normalized);
        }
    }

    QJsonValue sensitivity;
    if (intStringValue(mapping.value(QStringLiteral("sensitivity")), 1, 10, &sensitivity))
    {
        cleaned.insert(QStringLiteral("sensitivity"), sensitivity);
    }
    return cleaned;
}

QJsonObject cleanConfigurationSettings(const QJsonDocument& doc)
{
    const QJsonObject source = sourceSettingsObject(doc);
    QJsonObject settings;

    const QJsonObject radioChooserSource = source.value(QStringLiteral("radioChooser")).toObject();
    QJsonObject radioChooser;
    QJsonValue autoConnect;
    if (boolStringValue(radioChooserSource.value(QStringLiteral("autoConnect")), &autoConnect))
    {
        radioChooser.insert(QStringLiteral("autoConnect"), autoConnect);
    }

    const QJsonObject radioProfiles = cleanRadioProfiles(source);
    if (!radioProfiles.isEmpty())
    {
        radioChooser.insert(QStringLiteral("radioProfiles"), radioProfiles);
    }
    if (!radioChooser.isEmpty())
    {
        settings.insert(QStringLiteral("radioChooser"), radioChooser);
    }

    QJsonObject accessories;
    const QJsonObject icomMapping = cleanIcomRC28ButtonMapping(source);
    if (!icomMapping.isEmpty())
    {
        accessories.insert(QStringLiteral("ICOMRC28ButtonMapping"), icomMapping);
    }
    if (!accessories.isEmpty())
    {
        settings.insert(QStringLiteral("accessories"), accessories);
    }

    const QJsonObject audioSource = source.value(QStringLiteral("audio")).toObject();
    QJsonObject audio;
    insertCleanSetting(&audio, audioSource, QStringLiteral("inputDeviceID"), stringValue);
    insertCleanSetting(&audio, audioSource, QStringLiteral("outputDeviceID"), stringValue);
    insertCleanSetting(&audio, audioSource, QStringLiteral("outputChannels"),
                       [](const QJsonValue& value, QJsonValue* normalized)
                       { return intStringValue(value, 1, 2, normalized); });
    if (!audio.isEmpty())
    {
        settings.insert(QStringLiteral("audio"), audio);
    }

    const QJsonObject bandscopeSource = source.value(QStringLiteral("bandScope")).toObject();
    QJsonObject bandscope;
    insertCleanSetting(&bandscope, bandscopeSource, QStringLiteral("backgroundColor"), colorStringValue);
    insertCleanSetting(&bandscope, bandscopeSource, QStringLiteral("centerLineColor"), colorStringValue);
    insertCleanSetting(&bandscope, bandscopeSource, QStringLiteral("gridLineColor"), colorStringValue);
    insertCleanSetting(&bandscope, bandscopeSource, QStringLiteral("gridDensity"),
                       [](const QJsonValue& value, QJsonValue* normalized)
                       { return intStringValue(value, 0, 2, normalized); });
    insertCleanSetting(&bandscope, bandscopeSource, QStringLiteral("invertMouseWheel"), boolStringValue);
    insertCleanSetting(&bandscope, bandscopeSource, QStringLiteral("spanHZ"),
                       [](const QJsonValue& value, QJsonValue* normalized)
                       { return intStringValue(value, 1, std::numeric_limits<int>::max(), normalized); });
    insertCleanSetting(&bandscope, bandscopeSource, QStringLiteral("spectrumHeight"),
                       [](const QJsonValue& value, QJsonValue* normalized)
                       { return intStringValue(value, 1, std::numeric_limits<int>::max(), normalized); });
    if (!bandscope.isEmpty())
    {
        settings.insert(QStringLiteral("bandScope"), bandscope);
    }

    const QJsonObject mainWindowSource = source.value(QStringLiteral("mainWindow")).toObject();
    QJsonObject mainWindow;
    insertCleanSetting(&mainWindow, mainWindowSource, QStringLiteral("positionX"),
                       [](const QJsonValue& value, QJsonValue* normalized)
                       {
                           return intStringValue(value, std::numeric_limits<int>::min(),
                                                 std::numeric_limits<int>::max(), normalized);
                       });
    insertCleanSetting(&mainWindow, mainWindowSource, QStringLiteral("positionY"),
                       [](const QJsonValue& value, QJsonValue* normalized)
                       {
                           return intStringValue(value, std::numeric_limits<int>::min(),
                                                 std::numeric_limits<int>::max(), normalized);
                       });
    insertCleanSetting(&mainWindow, mainWindowSource, QStringLiteral("statusClockUTC"), boolStringValue);
    if (!mainWindow.isEmpty())
    {
        settings.insert(QStringLiteral("mainWindow"), mainWindow);
    }

    const QJsonObject memoryWindowSource = source.value(QStringLiteral("memoryWindow")).toObject();
    QJsonObject memoryWindow;
    insertCleanSetting(&memoryWindow, memoryWindowSource, QStringLiteral("closeOnSelect"), boolStringValue);
    if (!memoryWindow.isEmpty())
    {
        settings.insert(QStringLiteral("memoryWindow"), memoryWindow);
    }

    const QJsonObject radioSource = source.value(QStringLiteral("radio")).toObject();
    QJsonObject radio;
    insertCleanSetting(&radio, radioSource, QStringLiteral("LANModLevel"),
                       [](const QJsonValue& value, QJsonValue* normalized)
                       { return intStringValue(value, 0, 255, normalized); });
    insertCleanSetting(&radio, radioSource, QStringLiteral("tuningStepHZ"),
                       [](const QJsonValue& value, QJsonValue* normalized)
                       { return intStringValue(value, 1, std::numeric_limits<int>::max(), normalized); });
    insertCleanSetting(&radio, radioSource, QStringLiteral("volumeLevel"),
                       [](const QJsonValue& value, QJsonValue* normalized)
                       { return intStringValue(value, 0, 255, normalized); });
    if (!radio.isEmpty())
    {
        settings.insert(QStringLiteral("radio"), radio);
    }

    return settings;
}

QJsonObject configurationSettingsFromDocument(const QJsonDocument& doc)
{
    return cleanConfigurationSettings(doc);
}

bool looksLikeConfiguration(const QJsonDocument& doc)
{
    return !configurationSettingsFromDocument(doc).isEmpty();
}

QByteArray configurationDataFromDocument(const QJsonDocument& doc)
{
    QJsonObject root;
    root.insert(QStringLiteral("settings"), configurationSettingsFromDocument(doc));
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QJsonDocument readJsonFile(QWidget* parent, const QString& path, const QString& title)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::warning(parent, QStringLiteral("%1 Failed").arg(title),
                             QStringLiteral("%1 failed. Could not open the selected file.").arg(title));
        return {};
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError)
    {
        QMessageBox::warning(parent, QStringLiteral("%1 Failed").arg(title),
                             QStringLiteral("%1 failed. The selected file is not valid JSON.").arg(title));
        return {};
    }

    return doc;
}

bool restartApplication(QWidget* parent)
{
    QStringList arguments = QCoreApplication::arguments();
    if (!arguments.isEmpty())
    {
        arguments.removeFirst();
    }

    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments,
                                 QCoreApplication::applicationDirPath()))
    {
        QMessageBox::warning(parent, QStringLiteral("Restart SDR9700"),
                             QStringLiteral("Could not restart SDR9700. Close and reopen the application manually."));
        return false;
    }

    QTimer::singleShot(0, QCoreApplication::instance(), &QCoreApplication::quit);
    return true;
}
} // namespace

QString ConfigurationManager::configPath()
{
    return AppSettings::configPath();
}

QString ConfigurationManager::memoriesPath()
{
    return ::memoriesPath();
}

bool ConfigurationManager::backupMemories(QWidget* parent)
{
    const QString path = memoryBackupPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!writeFile(path, memoriesExportDocument(loadMemories()).toJson(QJsonDocument::Indented)))
    {
        QMessageBox::warning(parent, QStringLiteral("Backup Memories Failed"),
                             QStringLiteral("Memory backup failed. Could not create the backup file."));
        return false;
    }

    QMessageBox::information(parent, QStringLiteral("Backup Memories Successful"),
                             QStringLiteral("Memory backup successful.\n\nSaved to:\n%1").arg(path));
    return true;
}

MemoryImportResult ConfigurationManager::restoreMemories(QWidget* parent)
{
    const QString path = QFileDialog::getOpenFileName(parent, QStringLiteral("Restore Memories"), backupDirectoryPath(),
                                                      QString::fromLatin1(kMemoryBackupFilter));
    if (path.isEmpty())
    {
        return {};
    }

    if (QMessageBox::question(parent, QStringLiteral("Restore Memories"),
                              QStringLiteral("Restore this memory backup? Current memories will be replaced.")) !=
        QMessageBox::Yes)
    {
        return {};
    }

    const QJsonDocument doc = readJsonFile(parent, path, QStringLiteral("Restore Memories"));
    if (doc.isNull())
    {
        return {true, false, 0};
    }

    const QVector<MemoryRecord> restored = memoriesFromDocument(doc);
    if (!saveMemories(restored))
    {
        QMessageBox::warning(parent, QStringLiteral("Restore Memories Failed"),
                             QStringLiteral("Memory restore failed. Could not save the restored memories."));
        return {true, false, 0};
    }

    const int restoredCount = restored.size() > std::numeric_limits<int>::max() ? std::numeric_limits<int>::max()
                                                                                : static_cast<int>(restored.size());
    QMessageBox::information(parent, QStringLiteral("Restore Memories Successful"),
                             QStringLiteral("Memory restore successful.\n\nRestored %1 memories.").arg(restoredCount));
    return {true, true, restoredCount};
}

bool ConfigurationManager::exportMemories(QWidget* parent)
{
    const QString path =
        QFileDialog::getSaveFileName(parent, QStringLiteral("Export Memories"), QStringLiteral("sdr9700-memories.json"),
                                     QString::fromLatin1(kMemoryFileFilter));
    if (path.isEmpty())
    {
        return false;
    }

    if (!writeFile(path, memoriesExportDocument(loadMemories()).toJson(QJsonDocument::Indented)))
    {
        QMessageBox::warning(parent, QStringLiteral("Export Memories Failed"),
                             QStringLiteral("Memory export failed. Could not save the selected file."));
        return false;
    }
    QMessageBox::information(parent, QStringLiteral("Export Memories Successful"),
                             QStringLiteral("Memory export successful.\n\nSaved to:\n%1").arg(path));
    return true;
}

MemoryImportResult ConfigurationManager::importMemories(QWidget* parent)
{
    const QString path = QFileDialog::getOpenFileName(parent, QStringLiteral("Import Memories"), QString(),
                                                      QString::fromLatin1(kMemoryFileFilter));
    if (path.isEmpty())
    {
        return {};
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::warning(parent, QStringLiteral("Import Memories Failed"),
                             QStringLiteral("Memory import failed. Could not open the selected file."));
        return {true, false, 0};
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError)
    {
        QMessageBox::warning(parent, QStringLiteral("Import Memories Failed"),
                             QStringLiteral("Memory import failed. The selected file is not valid JSON."));
        return {true, false, 0};
    }

    QVector<MemoryRecord> existing = loadMemories();
    const QVector<MemoryRecord> imported = memoriesFromDocument(doc);
    for (MemoryRecord memory : imported)
    {
        auto current = std::find_if(existing.begin(), existing.end(),
                                    [&memory](const MemoryRecord& record) { return record.id == memory.id; });
        if (current != existing.end())
        {
            *current = memory;
        }
        else
        {
            existing.append(memory);
        }
    }

    if (!saveMemories(existing))
    {
        QMessageBox::warning(parent, QStringLiteral("Import Memories Failed"),
                             QStringLiteral("Memory import failed. Could not save the imported memories."));
        return {true, false, 0};
    }

    const int importedCount = imported.size() > std::numeric_limits<int>::max() ? std::numeric_limits<int>::max()
                                                                                : static_cast<int>(imported.size());
    QMessageBox::information(parent, QStringLiteral("Import Memories Successful"),
                             QStringLiteral("Memory import successful.\n\nImported %1 memories.").arg(importedCount));
    return {true, true, importedCount};
}

bool ConfigurationManager::resetMemories(QWidget* parent)
{
    if (QMessageBox::question(parent, QStringLiteral("Reset Memories"),
                              QStringLiteral("Remove all local SDR9700 memory channels?")) != QMessageBox::Yes)
    {
        return false;
    }

    if (!saveMemories({}))
    {
        QMessageBox::warning(parent, QStringLiteral("Reset Memories"),
                             QStringLiteral("Could not reset local memory channels."));
        return false;
    }

    return true;
}

bool ConfigurationManager::backupConfiguration(QWidget* parent)
{
    if (!AppSettings::instance().save())
    {
        QMessageBox::warning(parent, QStringLiteral("Backup Configuration Failed"),
                             QStringLiteral("Configuration backup failed. Could not save the current settings before "
                                            "creating a backup."));
        return false;
    }

    const QString sourcePath = configPath();
    if (!QFileInfo::exists(sourcePath))
    {
        QMessageBox::warning(parent, QStringLiteral("Backup Configuration Failed"),
                             QStringLiteral("Configuration backup failed. The SDR9700 configuration file does not "
                                            "exist yet."));
        return false;
    }

    const QString path = configurationBackupPath();
    if (!copyFileAtomic(sourcePath, path))
    {
        QMessageBox::warning(parent, QStringLiteral("Backup Configuration Failed"),
                             QStringLiteral("Configuration backup failed. Could not create the backup file."));
        return false;
    }

    QMessageBox::information(parent, QStringLiteral("Backup Configuration Successful"),
                             QStringLiteral("Configuration backup successful.\n\nSaved to:\n%1").arg(path));
    return true;
}

bool ConfigurationManager::restoreConfigurationAndRestart(QWidget* parent)
{
    const QString path = QFileDialog::getOpenFileName(parent, QStringLiteral("Restore Configuration"),
                                                      backupDirectoryPath(), QString::fromLatin1(kConfigBackupFilter));
    if (path.isEmpty())
    {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::warning(parent, QStringLiteral("Restore Configuration Failed"),
                             QStringLiteral("Configuration restore failed. Could not open the selected backup."));
        return false;
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !looksLikeConfiguration(doc))
    {
        QMessageBox::warning(parent, QStringLiteral("Restore Configuration Failed"),
                             QStringLiteral("Configuration restore failed. The selected file does not look like an "
                                            "SDR9700 configuration backup."));
        return false;
    }

    if (QMessageBox::question(parent, QStringLiteral("Restore Configuration"),
                              QStringLiteral("Restore this configuration backup and restart SDR9700? Current "
                                             "configuration will be replaced.")) != QMessageBox::Yes)
    {
        return false;
    }

    const QString targetPath = configPath();
    QDir().mkpath(QFileInfo(targetPath).absolutePath());
    if (!writeFile(targetPath, configurationDataFromDocument(doc)))
    {
        QMessageBox::warning(parent, QStringLiteral("Restore Configuration Failed"),
                             QStringLiteral("Configuration restore failed. Could not write the restored "
                                            "configuration."));
        return false;
    }

    QMessageBox::information(parent, QStringLiteral("Restore Configuration Successful"),
                             QStringLiteral("Configuration restore successful.\n\nSDR9700 will restart now."));
    return restartApplication(parent);
}

bool ConfigurationManager::exportConfiguration(QWidget* parent)
{
    const QString sourcePath = configPath();
    if (!QFileInfo::exists(sourcePath))
    {
        QMessageBox::warning(parent, QStringLiteral("Export Configuration Failed"),
                             QStringLiteral("Configuration export failed. The SDR9700 configuration file does not "
                                            "exist yet."));
        return false;
    }

    const QString path =
        QFileDialog::getSaveFileName(parent, QStringLiteral("Export Configuration"), QStringLiteral("sdr9700.json"),
                                     QString::fromLatin1(kConfigFileFilter));
    if (path.isEmpty())
    {
        return false;
    }

    if (!copyFileAtomic(sourcePath, path))
    {
        QMessageBox::warning(parent, QStringLiteral("Export Configuration Failed"),
                             QStringLiteral("Configuration export failed. Could not save the selected file."));
        return false;
    }
    QMessageBox::information(parent, QStringLiteral("Export Configuration Successful"),
                             QStringLiteral("Configuration export successful.\n\nSaved to:\n%1").arg(path));
    return true;
}

bool ConfigurationManager::importConfigurationAndRestart(QWidget* parent)
{
    const QString path = QFileDialog::getOpenFileName(parent, QStringLiteral("Import Configuration"), QString(),
                                                      QString::fromLatin1(kConfigFileFilter));
    if (path.isEmpty())
    {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::warning(parent, QStringLiteral("Import Configuration Failed"),
                             QStringLiteral("Configuration import failed. Could not open the selected file."));
        return false;
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !looksLikeConfiguration(doc))
    {
        QMessageBox::warning(parent, QStringLiteral("Import Configuration Failed"),
                             QStringLiteral("Configuration import failed. The selected file does not look like a "
                                            "valid SDR9700 configuration file."));
        return false;
    }

    if (QMessageBox::question(parent, QStringLiteral("Import Configuration"),
                              QStringLiteral("Importing this configuration will replace the current SDR9700 "
                                             "configuration and restart the application. Continue?")) !=
        QMessageBox::Yes)
    {
        return false;
    }

    const QString targetPath = configPath();
    QDir().mkpath(QFileInfo(targetPath).absolutePath());
    if (!writeFile(targetPath, configurationDataFromDocument(doc)))
    {
        QMessageBox::warning(parent, QStringLiteral("Import Configuration Failed"),
                             QStringLiteral("Configuration import failed. Could not replace the SDR9700 "
                                            "configuration."));
        return false;
    }

    QMessageBox::information(parent, QStringLiteral("Import Configuration Successful"),
                             QStringLiteral("Configuration import successful.\n\nSDR9700 will restart now."));
    return restartApplication(parent);
}

bool ConfigurationManager::resetConfigurationAndRestart(QWidget* parent)
{
    if (QMessageBox::question(parent, QStringLiteral("Reset Configuration"),
                              QStringLiteral("Reset SDR9700 configuration and restart the application? This removes "
                                             "local settings and radio profiles. Memories are stored separately.")) !=
        QMessageBox::Yes)
    {
        return false;
    }

    const QString path = configPath();
    if (QFileInfo::exists(path) && !QFile::remove(path))
    {
        QMessageBox::warning(parent, QStringLiteral("Reset Configuration"),
                             QStringLiteral("Could not remove the SDR9700 configuration file."));
        return false;
    }

    return restartApplication(parent);
}
