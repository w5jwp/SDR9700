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
    m_entries.append({category, line});
    m_textSize += line.size();
    while ((m_textSize > kMaximumLogTextSize || m_entries.size() > kMaximumLogEntries) && m_entries.size() > 1)
    {
        m_textSize -= m_entries.front().text.size();
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
    QSet<QString> uniqueCategories;
    for (const Entry& entry : m_entries)
    {
        uniqueCategories.insert(entry.category);
    }
    QStringList result(uniqueCategories.begin(), uniqueCategories.end());
    result.sort(Qt::CaseInsensitive);
    return result;
}

void ApplicationLog::clear()
{
    QMutexLocker lock(&m_mutex);
    m_entries.clear();
    m_textSize = 0;
}
