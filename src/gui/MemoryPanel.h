#pragma once

#include <QGroupBox>
#include <QString>
#include <QVector>

#include "MemoryStore.h"

class QTableWidget;

class MemoryPanel : public QGroupBox
{
    Q_OBJECT

  public:
    explicit MemoryPanel(QWidget* parent = nullptr);

    void setMemories(const QVector<MemoryRecord>& memories, const QString& activeMemoryId);
    void setActiveMemoryId(const QString& activeMemoryId);

  signals:
    void memoryActivated(const QString& memoryId);

  private:
    void rebuildList();
    void applyActiveSelection();

    QTableWidget* m_table{nullptr};
    QVector<MemoryRecord> m_memories;
    QString m_activeMemoryId;
};
