#pragma once

#include <QObject>
#include <QString>

class MemoryController;

class MemoryEditorController : public QObject
{
    Q_OBJECT

  public:
    explicit MemoryEditorController(MemoryController* owner);

    void showMemoryEditor(const QString& memoryId);

  private:
    MemoryController* m_owner{nullptr};
};
