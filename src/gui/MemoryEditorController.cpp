#include "MemoryEditorController.h"
#include "MemoryEditorForm.h"

#include "MemoryController.h"
#include "MemoryControllerHelpers.h"
#include "MainWindow.h"
#include "UtilityWindow.h"
#include "VfoPanel.h"
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
    m_form->show(memoryId);
}
