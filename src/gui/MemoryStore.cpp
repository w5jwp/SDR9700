#include "MemoryStore.h"

#include "AppSettings.h"
#include "MainWindowHelpers.h"
#include "RadioCapabilities.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QUuid>

namespace
{
const QStringList& memoryCsvHeaders()
{
    static const QStringList headers{
        QStringLiteral("band"),         QStringLiteral("channel"),  QStringLiteral("name"),
        QStringLiteral("receiveHZ"),    QStringLiteral("mode"),     QStringLiteral("filter"),
        QStringLiteral("dataMode"),     QStringLiteral("scan"),     QStringLiteral("duplexMode"),
        QStringLiteral("offsetHZ"),     QStringLiteral("toneMode"), QStringLiteral("tone"),
        QStringLiteral("tsql"),         QStringLiteral("dsql"),     QStringLiteral("dtcs"),
        QStringLiteral("dtcsPolarity"), QStringLiteral("dtcsB"),    QStringLiteral("dtcsPolarityB"),
        QStringLiteral("dvSql"),        QStringLiteral("urCall"),   QStringLiteral("r1Call"),
        QStringLiteral("r2Call")};
    return headers;
}

QString memoryBandLabelForHz(quint64 hz)
{
    return sdr9700::radioBandShortLabel(sdr9700::radioBandForFrequency(hz));
}

quint16 memoryGroupForBandLabel(QString band)
{
    band = band.trimmed().toLower();
    bool ok = false;
    const uint numericBand = band.toUInt(&ok);
    if (ok && numericBand >= 1 && numericBand <= 3)
    {
        return static_cast<quint16>(numericBand);
    }
    if (band == QLatin1String("2m"))
    {
        return 1;
    }
    if (band == QLatin1String("70cm"))
    {
        return 2;
    }
    if (band == QLatin1String("23cm"))
    {
        return 3;
    }
    return 0;
}

QString csvEscaped(QString value)
{
    const bool quote = value.contains(QLatin1Char(',')) || value.contains(QLatin1Char('"')) ||
                       value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r'));
    if (!quote)
    {
        return value;
    }
    value.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(value);
}

QString csvLine(const QStringList& fields)
{
    QStringList escaped;
    escaped.reserve(fields.size());
    for (const QString& field : fields)
    {
        escaped.append(csvEscaped(field));
    }
    return escaped.join(QLatin1Char(','));
}

QVector<QStringList> parseCsvRows(const QString& text)
{
    QVector<QStringList> rows;
    QStringList row;
    QString field;
    bool quoted = false;
    for (qsizetype i = 0; i < text.size(); ++i)
    {
        const QChar ch = text.at(i);
        if (quoted)
        {
            if (ch == QLatin1Char('"'))
            {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"'))
                {
                    field.append(ch);
                    ++i;
                }
                else
                {
                    quoted = false;
                }
            }
            else
            {
                field.append(ch);
            }
            continue;
        }

        if (ch == QLatin1Char('"'))
        {
            quoted = true;
        }
        else if (ch == QLatin1Char(','))
        {
            row.append(field);
            field.clear();
        }
        else if (ch == QLatin1Char('\n') || ch == QLatin1Char('\r'))
        {
            if (ch == QLatin1Char('\r') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n'))
            {
                ++i;
            }
            row.append(field);
            rows.append(row);
            row.clear();
            field.clear();
        }
        else
        {
            field.append(ch);
        }
    }
    if (!field.isEmpty() || !row.isEmpty())
    {
        row.append(field);
        rows.append(row);
    }
    return rows;
}

QString csvValue(const QStringList& row, const QHash<QString, int>& indexes, const QString& key)
{
    const int index = indexes.value(key, -1);
    return index >= 0 && index < row.size() ? row.at(index) : QString();
}

int csvInt(const QStringList& row, const QHash<QString, int>& indexes, const QString& key, int defaultValue = 0)
{
    bool ok = false;
    const int value = csvValue(row, indexes, key).toInt(&ok);
    return ok ? value : defaultValue;
}

ushort toneValueFromRadioText(const QString& text)
{
    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok)
    {
        return 0;
    }
    return static_cast<ushort>(value * 10.0 + 0.5);
}

ushort memoryToneValueFromFields(rptAccessTxRx_t toneMode, const QString& tone, const QString& tsql, ushort dtcs)
{
    if (isDtcsToneMode(toneMode))
    {
        return dtcs;
    }
    if (toneMode == ratrNN)
    {
        return 0;
    }
    if (toneMode == ratrNT)
    {
        return toneValueFromRadioText(!tsql.isEmpty() ? tsql : tone);
    }
    return toneValueFromRadioText(!tone.isEmpty() ? tone : tsql);
}

QString normalizedToneText(QString text)
{
    text = text.trimmed();
    if (text.isEmpty())
    {
        return QString();
    }

    const ushort value = toneValueFromRadioText(text);
    return value > 0 ? sdr9700::ui::main_window::toneFrequencyLabel(value) : text;
}

quint64 csvUInt64(const QStringList& row, const QHash<QString, int>& indexes, const QString& key)
{
    bool ok = false;
    const quint64 value = csvValue(row, indexes, key).toULongLong(&ok);
    return ok ? value : 0;
}

QJsonObject memoryToJson(const MemoryRecord& memory)
{
    QJsonObject object;
    object.insert(QStringLiteral("ID"), memory.id);
    object.insert(QStringLiteral("group"), memory.group);
    object.insert(QStringLiteral("channel"), memory.channel);
    object.insert(QStringLiteral("name"), memory.name);
    object.insert(QStringLiteral("band"), memory.band);
    object.insert(QStringLiteral("bandKey"), memory.bandKey);
    object.insert(QStringLiteral("receiveHZ"), QString::number(memory.receiveHz));
    object.insert(QStringLiteral("mode"), memory.mode);
    object.insert(QStringLiteral("filter"), memory.filter);
    object.insert(QStringLiteral("dataMode"), memory.dataMode);
    object.insert(QStringLiteral("scan"), memory.scan);
    object.insert(QStringLiteral("shift"), memory.shift);
    object.insert(QStringLiteral("duplexMode"), memory.duplexMode);
    object.insert(QStringLiteral("offsetHZ"), QString::number(memory.offsetHz));
    object.insert(QStringLiteral("toneOption"), memory.toneOption);
    object.insert(QStringLiteral("toneFrequency"), memory.toneFrequency);
    object.insert(QStringLiteral("tone"), memory.tone);
    object.insert(QStringLiteral("tsql"), memory.tsql);
    object.insert(QStringLiteral("toneMode"), memory.toneMode);
    object.insert(QStringLiteral("toneValue"), memory.toneValue);
    object.insert(QStringLiteral("dsql"), memory.dsql);
    object.insert(QStringLiteral("dtcs"), memory.dtcs);
    object.insert(QStringLiteral("dtcsPolarity"), memory.dtcsPolarity);
    object.insert(QStringLiteral("dtcsB"), memory.dtcsB);
    object.insert(QStringLiteral("dtcsPolarityB"), memory.dtcsPolarityB);
    object.insert(QStringLiteral("dvSql"), memory.dvSql);
    object.insert(QStringLiteral("urCall"), memory.urCall);
    object.insert(QStringLiteral("r1Call"), memory.r1Call);
    object.insert(QStringLiteral("r2Call"), memory.r2Call);
    return object;
}

int intFromJson(const QJsonObject& object, const QString& key, int defaultValue = 0)
{
    bool ok = false;
    const int value = object.value(key).toVariant().toInt(&ok);
    return ok ? value : defaultValue;
}

MemoryRecord memoryFromJson(const QJsonObject& object)
{
    MemoryRecord memory;
    memory.id = object.value(QStringLiteral("ID")).toString();
    memory.group = static_cast<quint16>(intFromJson(object, QStringLiteral("group")));
    memory.channel = static_cast<quint16>(intFromJson(object, QStringLiteral("channel")));
    memory.name = object.value(QStringLiteral("name")).toString();
    memory.band = object.value(QStringLiteral("band")).toString();
    memory.bandKey = intFromJson(object, QStringLiteral("bandKey"), -1);
    memory.receiveHz = object.value(QStringLiteral("receiveHZ")).toVariant().toULongLong();
    memory.mode = intFromJson(object, QStringLiteral("mode"), modeFM);
    memory.filter = intFromJson(object, QStringLiteral("filter"), 1);
    memory.dataMode = intFromJson(object, QStringLiteral("dataMode"));
    memory.scan = intFromJson(object, QStringLiteral("scan"));
    memory.shift = object.value(QStringLiteral("shift")).toString();
    memory.duplexMode = intFromJson(object, QStringLiteral("duplexMode"), dmSimplex);
    memory.offsetHz = object.value(QStringLiteral("offsetHZ")).toVariant().toULongLong();
    memory.toneOption = object.value(QStringLiteral("toneOption")).toString();
    memory.toneFrequency = object.value(QStringLiteral("toneFrequency")).toString();
    memory.tone = object.value(QStringLiteral("tone")).toString();
    memory.tsql = object.value(QStringLiteral("tsql")).toString();
    memory.toneMode = intFromJson(object, QStringLiteral("toneMode"), ratrNN);
    memory.toneValue = static_cast<ushort>(intFromJson(object, QStringLiteral("toneValue")));
    memory.dsql = intFromJson(object, QStringLiteral("dsql"));
    memory.dtcs = static_cast<ushort>(
        intFromJson(object, QStringLiteral("dtcs"),
                    isDtcsToneMode(static_cast<rptAccessTxRx_t>(memory.toneMode)) ? memory.toneValue : 23));
    memory.dtcsPolarity = intFromJson(object, QStringLiteral("dtcsPolarity"));
    memory.dtcsB = static_cast<ushort>(intFromJson(object, QStringLiteral("dtcsB"), memory.dtcs));
    memory.dtcsPolarityB = intFromJson(object, QStringLiteral("dtcsPolarityB"), memory.dtcsPolarity);
    memory.dvSql = intFromJson(object, QStringLiteral("dvSql"));
    memory.urCall = object.value(QStringLiteral("urCall")).toString();
    memory.r1Call = object.value(QStringLiteral("r1Call")).toString();
    memory.r2Call = object.value(QStringLiteral("r2Call")).toString();
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

} // namespace

int memoryBandKeyForHz(quint64 hz)
{
    return sdr9700::radioBandMemoryKey(sdr9700::radioBandForFrequency(hz));
}

QString memoryBandLabelForGroup(quint16 group)
{
    switch (group)
    {
    case 1:
        return QStringLiteral("2M");
    case 2:
        return QStringLiteral("70CM");
    case 3:
        return QStringLiteral("23CM");
    default:
        return QString();
    }
}

QJsonDocument memoriesExportDocument(const QVector<MemoryRecord>& memories)
{
    QJsonObject root;
    root.insert(QStringLiteral("memories"), memoriesArray(memories));
    return QJsonDocument(root);
}

QByteArray memoriesExportCsv(const QVector<MemoryRecord>& memories)
{
    QStringList lines;
    lines.append(csvLine(memoryCsvHeaders()));
    for (const MemoryRecord& memory : memories)
    {
        lines.append(csvLine({QString::number(memory.group),
                              QString::number(memory.channel),
                              memory.name,
                              QString::number(memory.receiveHz),
                              QString::number(memory.mode),
                              QString::number(memory.filter),
                              QString::number(memory.dataMode),
                              QString::number(memory.scan),
                              QString::number(memory.duplexMode),
                              QString::number(memory.offsetHz),
                              QString::number(memory.toneMode),
                              memory.tone,
                              memory.tsql,
                              QString::number(memory.dsql),
                              QString::number(memory.dtcs),
                              QString::number(memory.dtcsPolarity),
                              QString::number(memory.dtcsB),
                              QString::number(memory.dtcsPolarityB),
                              QString::number(memory.dvSql),
                              memory.urCall,
                              memory.r1Call,
                              memory.r2Call}));
    }
    return lines.join(QLatin1Char('\n')).append(QLatin1Char('\n')).toUtf8();
}

QVector<MemoryRecord> memoriesFromDocument(const QJsonDocument& doc)
{
    if (!doc.isObject())
    {
        return {};
    }

    const QJsonArray array = doc.object().value(QStringLiteral("memories")).toArray();
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
        if (memory.group > 0 && memory.channel > 0 && memory.receiveHz > 0)
        {
            memories.append(memory);
        }
    }
    return memories;
}

QVector<MemoryRecord> memoriesFromCsv(const QByteArray& data)
{
    const QVector<QStringList> rows = parseCsvRows(QString::fromUtf8(data));
    if (rows.isEmpty())
    {
        return {};
    }

    QHash<QString, int> indexes;
    const QStringList headers = rows.first();
    for (int i = 0; i < headers.size(); ++i)
    {
        indexes.insert(headers.at(i).trimmed(), i);
    }

    QVector<MemoryRecord> memories;
    memories.reserve(rows.size() - 1);
    for (int i = 1; i < rows.size(); ++i)
    {
        const QStringList& row = rows.at(i);
        MemoryRecord memory;
        memory.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        memory.group = memoryGroupForBandLabel(csvValue(row, indexes, QStringLiteral("band")));
        memory.channel = static_cast<quint16>(csvInt(row, indexes, QStringLiteral("channel")));
        memory.name = csvValue(row, indexes, QStringLiteral("name"));
        memory.receiveHz = csvUInt64(row, indexes, QStringLiteral("receiveHZ"));
        memory.mode = csvInt(row, indexes, QStringLiteral("mode"), modeFM);
        memory.filter = csvInt(row, indexes, QStringLiteral("filter"), 1);
        memory.dataMode = csvInt(row, indexes, QStringLiteral("dataMode"));
        memory.scan = csvInt(row, indexes, QStringLiteral("scan"));
        memory.duplexMode = csvInt(row, indexes, QStringLiteral("duplexMode"), dmSimplex);
        memory.offsetHz = csvUInt64(row, indexes, QStringLiteral("offsetHZ"));
        memory.toneMode = csvInt(row, indexes, QStringLiteral("toneMode"), ratrNN);
        memory.tone = normalizedToneText(csvValue(row, indexes, QStringLiteral("tone")));
        memory.tsql = normalizedToneText(csvValue(row, indexes, QStringLiteral("tsql")));
        memory.dsql = csvInt(row, indexes, QStringLiteral("dsql"));
        memory.dtcs = static_cast<ushort>(csvInt(row, indexes, QStringLiteral("dtcs"), 23));
        memory.dtcsPolarity = csvInt(row, indexes, QStringLiteral("dtcsPolarity"));
        memory.dtcsB = static_cast<ushort>(csvInt(row, indexes, QStringLiteral("dtcsB"), memory.dtcs));
        memory.dtcsPolarityB = csvInt(row, indexes, QStringLiteral("dtcsPolarityB"), memory.dtcsPolarity);
        memory.toneValue = memoryToneValueFromFields(static_cast<rptAccessTxRx_t>(memory.toneMode), memory.tone,
                                                     memory.tsql, memory.dtcs);
        memory.dvSql = csvInt(row, indexes, QStringLiteral("dvSql"));
        memory.urCall = csvValue(row, indexes, QStringLiteral("urCall"));
        memory.r1Call = csvValue(row, indexes, QStringLiteral("r1Call"));
        memory.r2Call = csvValue(row, indexes, QStringLiteral("r2Call"));
        memory.bandKey = memoryBandKeyForHz(memory.receiveHz);
        memory.band = memoryBandLabelForHz(memory.receiveHz);
        memory.shift =
            sdr9700::ui::main_window::offsetModeLabel(static_cast<duplexMode_t>(memory.duplexMode), memory.offsetHz);
        memory.toneOption = sdr9700::ui::main_window::toneOptionLabel(static_cast<rptAccessTxRx_t>(memory.toneMode));
        memory.toneFrequency = sdr9700::ui::main_window::memoryToneFrequencyLabel(
            static_cast<rptAccessTxRx_t>(memory.toneMode), memory.toneValue);
        if (memory.group > 0 && memory.channel > 0 && memory.receiveHz > 0)
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
    if (!doc.isObject())
    {
        return {};
    }

    return memoriesFromDocument(doc);
}

bool saveMemories(const QVector<MemoryRecord>& memories)
{
    const QString path = memoriesPath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return false;
    }

    const QByteArray data = memoriesExportDocument(memories).toJson(QJsonDocument::Indented);
    if (file.write(data) != static_cast<qint64>(data.size()))
    {
        return false;
    }
    return file.commit();
}
