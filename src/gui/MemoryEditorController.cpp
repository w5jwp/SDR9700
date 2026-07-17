#include "MemoryEditorController.h"

#include "MemoryController.h"

#include <QMessageBox>

MemoryEditorController::MemoryEditorController(MemoryController* owner) : QObject(owner), m_owner(owner) {}

void MemoryEditorController::editSelectedMemory()
{
    if (m_owner->memoryEditorVisible())
    {
        m_owner->closeMemoryEditorFromController();
        return;
    }

    const QString id = m_owner->selectedMemoryId();
    if (id.isEmpty())
    {
        m_owner->clearMemoryEditButtonChecked();
        QMessageBox::information(m_owner->popupParent(), QStringLiteral("Edit Memory"),
                                 QStringLiteral("Choose one memory first."));
        return;
    }
    showMemoryEditor(id);
}

void MemoryEditorController::storeCurrentMemory()
{
    showMemoryEditor(QString());
}

void MemoryEditorController::showMemoryEditor(const QString& memoryId)
{
    m_owner->showMemoryEditorPane(memoryId);
}
