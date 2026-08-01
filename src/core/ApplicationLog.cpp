#include "ApplicationLog.h"

#include <QDateTime>
#include <QMutexLocker>
#include <QSet>

namespace
{
constexpr qsizetype kMaximumLogTextSize = 5 * 1024 * 1024;
constexpr qsizetype kMaximumLogEntries = 50000;

QString logLevelName(QtMsgType type)
{
    switch (type)
    {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARN");
    case QtCriticalMsg:
        return QStringLiteral("ERROR");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }
    return QStringLiteral("LOG");
}
} // namespace

ApplicationLog& ApplicationLog::instance()
{
    static ApplicationLog log;
    return log;
}

QString ApplicationLog::append(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    const QString category = context.category ? QString::fromLatin1(context.category) : QStringLiteral("default");
    const QString line = QStringLiteral("%1 %2 [%3] %4")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                                  logLevelName(type), category, message);

    QMutexLocker lock(&m_mutex);
    m_entries.append({category, line, m_nextSequence++});
    ++m_categoryCounts[category];
    m_textSize += line.size();
    while ((m_textSize > kMaximumLogTextSize || m_entries.size() > kMaximumLogEntries) && m_entries.size() > 1)
    {
        const Entry& removedEntry = m_entries.front();
        m_textSize -= removedEntry.text.size();
        auto categoryCount = m_categoryCounts.find(removedEntry.category);
        if (categoryCount != m_categoryCounts.end() && --categoryCount.value() == 0)
        {
            m_categoryCounts.erase(categoryCount);
        }
        m_entries.removeFirst();
    }
    return line;
}

QVector<ApplicationLog::Entry> ApplicationLog::entries() const
{
    QMutexLocker lock(&m_mutex);
    return m_entries;
}

QStringList ApplicationLog::categories() const
{
    QMutexLocker lock(&m_mutex);
    QStringList result(m_categoryCounts.keyBegin(), m_categoryCounts.keyEnd());
    result.sort(Qt::CaseInsensitive);
    return result;
}

QVector<ApplicationLog::Entry> ApplicationLog::entriesAfter(quint64 sequence, const QString& category,
                                                            bool* resetRequired, quint64* latestSequence,
                                                            QStringList* categoryNames) const
{
    QMutexLocker lock(&m_mutex);
    const bool historyUnavailable =
        sequence != 0 &&
        (m_entries.isEmpty() ? sequence + 1 < m_nextSequence : sequence + 1 < m_entries.constFirst().sequence);
    if (resetRequired)
    {
        *resetRequired = historyUnavailable;
    }
    if (latestSequence)
    {
        *latestSequence = m_nextSequence - 1;
    }
    if (categoryNames)
    {
        *categoryNames = QStringList(m_categoryCounts.keyBegin(), m_categoryCounts.keyEnd());
        categoryNames->sort(Qt::CaseInsensitive);
    }

    QVector<Entry> result;
    for (const Entry& entry : m_entries)
    {
        if ((sequence == 0 || entry.sequence > sequence) && (category.isEmpty() || entry.category == category))
        {
            result.append(entry);
        }
    }
    return result;
}

void ApplicationLog::clear()
{
    QMutexLocker lock(&m_mutex);
    m_entries.clear();
    m_categoryCounts.clear();
    m_textSize = 0;
}
