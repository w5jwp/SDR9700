#pragma once

#include <QMessageLogContext>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QVector>

class ApplicationLog
{
  public:
    struct Entry
    {
        QString category;
        QString text;
        quint64 sequence{0};
    };

    static ApplicationLog& instance();

    QString append(QtMsgType type, const QMessageLogContext& context, const QString& message);
    QVector<Entry> entries() const;
    QStringList categories() const;
    QVector<Entry> entriesAfter(quint64 sequence, const QString& category, bool* resetRequired, quint64* latestSequence,
                                QStringList* categoryNames) const;
    void setCivTrafficRetentionEnabled(bool enabled);
    void clear();

  private:
    ApplicationLog() = default;

    mutable QMutex m_mutex;
    QVector<Entry> m_entries;
    QHash<QString, qsizetype> m_categoryCounts;
    qsizetype m_textSize{0};
    quint64 m_nextSequence{1};
    bool m_civTrafficRetentionEnabled{false};
};
