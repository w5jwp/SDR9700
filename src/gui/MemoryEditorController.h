#pragma once

#include <QObject>
#include <QString>

class MemoryController;
class MemoryEditorForm;

class MemoryEditorController : public QObject
{
    Q_OBJECT

  public:
    explicit MemoryEditorController(MemoryController* owner);

    void editSelectedMemory();
    void storeCurrentMemory();
    void showMemoryEditor(const QString& memoryId);

  private:
    MemoryController* m_owner{nullptr};
    MemoryEditorForm* m_form{nullptr};
};
