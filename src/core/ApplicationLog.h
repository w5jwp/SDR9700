#pragma once

#include <QMessageLogContext>
#include <QMutex>
#include <QString>
#include <QVector>

class ApplicationLog
{
  public:
    struct Entry
    {
        QString category;
        QString text;
    };

    static ApplicationLog& instance();

    QString append(QtMsgType type, const QMessageLogContext& context, const QString& message);
    QVector<Entry> entries() const;
    QStringList categories() const;
    void clear();

  private:
    ApplicationLog() = default;

    mutable QMutex m_mutex;
    QVector<Entry> m_entries;
    qsizetype m_textSize{0};
};
