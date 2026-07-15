#include "MemoryStore.h"

#include "AppSettings.h"
#include "RadioCapabilities.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>

namespace
{
QString memoryBandLabelForHz(quint64 hz)
{
    return sdr9700::radioBandShortLabel(sdr9700::radioBandForFrequency(hz));
}

QJsonObject memoryToJson(const MemoryRecord& memory)
{
    QJsonObject object;
    object.insert(QStringLiteral("ID"), memory.id);
    object.insert(QStringLiteral("number"), memoryNumberLabel(memory.number));
    object.insert(QStringLiteral("name"), memory.name);
    object.insert(QStringLiteral("band"), memory.band);
    object.insert(QStringLiteral("bandKey"), memory.bandKey);
    object.insert(QStringLiteral("receiveHZ"), QString::number(memory.receiveHz));
    object.insert(QStringLiteral("shift"), memory.shift);
    object.insert(QStringLiteral("duplexMode"), memory.duplexMode);
    object.insert(QStringLiteral("offsetHZ"), QString::number(memory.offsetHz));
    object.insert(QStringLiteral("toneOption"), memory.toneOption);
    object.insert(QStringLiteral("toneFrequency"), memory.toneFrequency);
    object.insert(QStringLiteral("toneMode"), memory.toneMode);
    object.insert(QStringLiteral("toneValue"), memory.toneValue);
    object.insert(QStringLiteral("notes"), memory.notes);
    return object;
}

MemoryRecord memoryFromJson(const QJsonObject& object)
{
    MemoryRecord memory;
    memory.id = object.value(QStringLiteral("ID")).toString();
    memory.number = object.value(QStringLiteral("number")).toInt(0);
    memory.name = object.value(QStringLiteral("name")).toString();
    memory.band = object.value(QStringLiteral("band")).toString();
    memory.bandKey = object.value(QStringLiteral("bandKey")).toInt(-1);
    memory.receiveHz = object.value(QStringLiteral("receiveHZ")).toVariant().toULongLong();
    memory.shift = object.value(QStringLiteral("shift")).toString();
    memory.duplexMode = object.value(QStringLiteral("duplexMode")).toInt(dmSimplex);
    memory.offsetHz = object.value(QStringLiteral("offsetHZ")).toVariant().toULongLong();
    memory.toneOption = object.value(QStringLiteral("toneOption")).toString();
    memory.toneFrequency = object.value(QStringLiteral("toneFrequency")).toString();
    memory.toneMode = object.value(QStringLiteral("toneMode")).toInt(ratrNN);
    memory.toneValue = static_cast<ushort>(object.value(QStringLiteral("toneValue")).toInt());
    memory.notes = object.value(QStringLiteral("notes")).toString();
    return memory;
}

QJsonArray memoriesArray(const QVector<MemoryRecord>& memories)
{
    QJsonArray array;
    for (const MemoryRecord& memory : memories)
    {
        array.append(memoryToJson(memory));
    }
    return array;
}

QJsonArray memoriesArrayFromValue(const QJsonValue& value)
{
    if (value.isArray())
    {
        return value.toArray();
    }
    if (value.isString())
    {
        const QJsonDocument doc = QJsonDocument::fromJson(value.toString().toUtf8());
        if (doc.isArray())
        {
            return doc.array();
        }
        if (doc.isObject())
        {
            return memoriesArrayFromValue(doc.object());
        }
    }
    if (value.isObject())
    {
        const QJsonObject object = value.toObject();
        QJsonArray array = memoriesArrayFromValue(object.value(QStringLiteral("memories")));
        if (!array.isEmpty())
        {
            return array;
        }
        return memoriesArrayFromValue(object.value(QStringLiteral("settings")));
    }
    return {};
}
} // namespace

int memoryBandKeyForHz(quint64 hz)
{
    return sdr9700::radioBandMemoryKey(sdr9700::radioBandForFrequency(hz));
}

QString memoryNumberLabel(int number)
{
    return QString::number(std::max(0, number)).rightJustified(3, QLatin1Char('0'));
}

QVector<MemoryRecord> normalizedMemoryNumbers(QVector<MemoryRecord> memories)
{
    std::stable_sort(memories.begin(), memories.end(),
                     [](const MemoryRecord& left, const MemoryRecord& right)
                     {
                         static constexpr int kMissingNumberSortValue = 1000000000;
                         const int leftNumber = left.number > 0 ? left.number : kMissingNumberSortValue;
                         const int rightNumber = right.number > 0 ? right.number : kMissingNumberSortValue;
                         return leftNumber < rightNumber;
                     });

    for (int i = 0; i < memories.size(); ++i)
    {
        memories[i].number = i + 1;
    }
    return memories;
}

QJsonDocument memoriesExportDocument(const QVector<MemoryRecord>& memories)
{
    QJsonObject root;
    root.insert(QStringLiteral("application"), QStringLiteral("SDR9700"));
    root.insert(QStringLiteral("memories"), memoriesArray(normalizedMemoryNumbers(memories)));
    return QJsonDocument(root);
}

QVector<MemoryRecord> memoriesFromDocument(const QJsonDocument& doc)
{
    QJsonArray array;
    if (doc.isArray())
    {
        array = doc.array();
    }
    else if (doc.isObject())
    {
        array = memoriesArrayFromValue(doc.object());
    }

    QVector<MemoryRecord> memories;
    memories.reserve(array.size());
    for (const QJsonValue& value : array)
    {
        if (!value.isObject())
        {
            continue;
        }
        MemoryRecord memory = memoryFromJson(value.toObject());
        if (memory.id.isEmpty())
        {
            memory.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        if (memory.bandKey == -1)
        {
            memory.bandKey = memoryBandKeyForHz(memory.receiveHz);
        }
        if (memory.band.isEmpty())
        {
            memory.band = memoryBandLabelForHz(memory.receiveHz);
        }
        if (memory.receiveHz > 0)
        {
            memories.append(memory);
        }
    }
    return memories;
}

QString memoriesPath()
{
    return QDir(QFileInfo(AppSettings::configPath()).absolutePath()).filePath(QStringLiteral("sdr9700-memories.json"));
}

QVector<MemoryRecord> loadMemories()
{
    QFile file(memoriesPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return {};
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray() && !doc.isObject())
    {
        return {};
    }

    return normalizedMemoryNumbers(memoriesFromDocument(doc));
}

bool saveMemories(const QVector<MemoryRecord>& memories)
{
    const QVector<MemoryRecord> normalized = normalizedMemoryNumbers(memories);
    const QString path = memoriesPath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return false;
    }

    const QByteArray data = memoriesExportDocument(normalized).toJson(QJsonDocument::Indented);
    if (file.write(data) != static_cast<qint64>(data.size()))
    {
        return false;
    }
    return file.commit();
}
