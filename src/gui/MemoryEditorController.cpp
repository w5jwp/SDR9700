#include "MemoryEditorController.h"

#include "MemoryController.h"

MemoryEditorController::MemoryEditorController(MemoryController* owner) : QObject(owner), m_owner(owner) {}

void MemoryEditorController::showMemoryEditor(const QString& memoryId)
{
    m_owner->showMemoryEditorDirect(memoryId);
}
