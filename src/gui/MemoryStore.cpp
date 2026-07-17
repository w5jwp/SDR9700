#include "MemoryStore.h"

#include "MainWindowHelpers.h"
#include "RadioCapabilities.h"

#include <QHash>
#include <QUuid>

#include <algorithm>

namespace
{
constexpr quint16 kCsvFirstRadioMemoryChannel = 1;
constexpr quint16 kCsvLastUserRadioMemoryChannel = 99;

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

bool csvHasValue(const QStringList& row, const QHash<QString, int>& indexes, const QString& key)
{
    return !csvValue(row, indexes, key).trimmed().isEmpty();
}

bool csvRowIsBlank(const QStringList& row)
{
    return std::all_of(row.cbegin(), row.cend(), [](const QString& field) { return field.trimmed().isEmpty(); });
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
        return toneValueFromRadioText(tsql);
    }
    // For TX-capable tone modes, toneValue tracks the TX tone shown in compact
    // labels. Do not infer it from RX tone; CSV import rejects missing required
    // tone fields instead of repairing them at runtime.
    return toneValueFromRadioText(tone);
}

void validateMemoryRecord(const MemoryRecord& memory, const QStringList& row, const QHash<QString, int>& indexes,
                          int rowNumber, QStringList* errors)
{
    if (!errors)
    {
        return;
    }

    auto addError = [errors, rowNumber](const QString& message)
    { errors->append(QStringLiteral("Row %1: %2").arg(rowNumber).arg(message)); };

    if (memory.group == 0)
    {
        addError(QStringLiteral("band must be 1, 2, 3, 2M, 70CM, or 23CM"));
    }
    if (memory.channel < kCsvFirstRadioMemoryChannel || memory.channel > kCsvLastUserRadioMemoryChannel)
    {
        addError(QStringLiteral("channel must be 1-99"));
    }
    if (memory.receiveHz == 0)
    {
        addError(QStringLiteral("receiveHZ must be a positive integer"));
    }

    const auto toneMode = static_cast<rptAccessTxRx_t>(memory.toneMode);
    switch (toneMode)
    {
    case ratrNN:
        break;
    case ratrTN:
        if (!csvHasValue(row, indexes, QStringLiteral("tone")))
        {
            addError(QStringLiteral("tone is required when toneMode is TONE TX"));
        }
        break;
    case ratrNT:
        if (!csvHasValue(row, indexes, QStringLiteral("tsql")))
        {
            addError(QStringLiteral("tsql is required when toneMode is TONE RX"));
        }
        break;
    case ratrTT:
    case ratrTD:
        if (!csvHasValue(row, indexes, QStringLiteral("tone")) || !csvHasValue(row, indexes, QStringLiteral("tsql")))
        {
            addError(QStringLiteral("tone and tsql are required when toneMode uses TX and RX tones"));
        }
        break;
    case ratrDN:
        if (!csvHasValue(row, indexes, QStringLiteral("dtcs")))
        {
            addError(QStringLiteral("dtcs is required when toneMode is DTCS TX"));
        }
        break;
    case ratrDD:
        if (!csvHasValue(row, indexes, QStringLiteral("dtcs")) || !csvHasValue(row, indexes, QStringLiteral("dtcsB")))
        {
            addError(QStringLiteral("dtcs and dtcsB are required when toneMode uses TX and RX DTCS"));
        }
        break;
    case ratrDT:
        if (!csvHasValue(row, indexes, QStringLiteral("dtcs")) || !csvHasValue(row, indexes, QStringLiteral("tsql")))
        {
            addError(QStringLiteral("dtcs and tsql are required when toneMode uses DTCS TX and tone RX"));
        }
        break;
    default:
        addError(QStringLiteral("toneMode is not supported by SDR9700 memory import"));
        break;
    }
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


QVector<MemoryRecord> memoriesFromCsv(const QByteArray& data, QStringList* errors)
{
    if (errors)
    {
        errors->clear();
    }

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
    for (const QString& header : memoryCsvHeaders())
    {
        if (!indexes.contains(header) && errors)
        {
            errors->append(QStringLiteral("Missing CSV column: %1").arg(header));
        }
    }
    if (errors && !errors->isEmpty())
    {
        return {};
    }

    QVector<MemoryRecord> memories;
    memories.reserve(rows.size() - 1);
    for (int i = 1; i < rows.size(); ++i)
    {
        const QStringList& row = rows.at(i);
        if (csvRowIsBlank(row))
        {
            continue;
        }

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
        validateMemoryRecord(memory, row, indexes, i + 1, errors);
        if (!errors || errors->isEmpty())
        {
            memories.append(memory);
        }
    }
    return memories;
}
