#include "MemoryEditorController.h"
#include "MemoryEditorForm.h"

#include "ConfirmationDialog.h"
#include "MemoryController.h"
#include "MainWindow.h"
#include "UtilityWindow.h"
#include "models/RadioModel.h"
#include "models/VfoModel.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <initializer_list>

MemoryEditorController::MemoryEditorController(MemoryController* owner) : QObject(owner), m_owner(owner)
{
    m_form = new MemoryEditorForm(owner);
}

void MemoryEditorController::editSelectedMemory()
{
    const QString id = m_owner->selectedMemoryId();
    if (id.isEmpty())
    {
        sdr9700::ui::showInformation(m_owner->popupParent(), QStringLiteral("Edit Memory"),
                                     QStringLiteral("Please select a memory channel."));
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
    if (!memoryId.isEmpty())
    {
        bool found = false;
        const MemoryRecord memory = m_owner->memoryForId(memoryId, &found);
        if (found && memory.readOnly)
        {
            QMessageBox::information(m_owner->popupParent(), QStringLiteral("Edit Memory"),
                                     QStringLiteral("Scan-edge, call, and satellite memories are read-only."));
            return;
        }
    }
    m_form->show(memoryId);
}
