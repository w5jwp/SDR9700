#pragma once

#include <QObject>
#include <QString>

class MemoryController;

class MemoryEditorForm : public QObject
{
    Q_OBJECT

  public:
    explicit MemoryEditorForm(MemoryController* owner);

    void show(const QString& memoryId);

  private:
    MemoryController* m_owner{nullptr};
};
