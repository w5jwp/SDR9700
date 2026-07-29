#include "MemorySelectionController.h"

#include "MainWindow.h"
#include "MemoryController.h"
#include "MemoryConstants.h"
#include "MemoryRecordHelpers.h"
#include "VfoPanel.h"
#include "models/RadioModel.h"
#include "models/VfoModel.h"

#include <QComboBox>
#include <QMessageBox>
#include <QTableWidget>
#include <QTimer>

using namespace sdr9700::memory;

MemorySelectionController::MemorySelectionController(MemoryController* owner) : QObject(owner), m_owner(owner) {}

QString MemorySelectionController::selectedMemoryId() const
{
    if (!m_owner->m_window->m_memoryTable)
    {
        return QString();
    }

    const int row = m_owner->m_window->m_memoryTable->currentRow();
    if (row < 0)
    {
        return QString();
    }

    const auto* idItem = m_owner->m_window->m_memoryTable->item(row, kMemoryIdColumn);
    return idItem ? idItem->text() : QString();
}


void MemorySelectionController::selectCheckedMemory()
{
    const QString id = selectedMemoryId();
    if (id.isEmpty())
    {
        QMessageBox::information(m_owner->popupParent(), "Select Memory", "Choose one memory first.");
        return;
    }

    selectMemoryById(id, true);
}


void MemorySelectionController::selectMemoryById(const QString& id, bool showDialogOnFailure)
{
    if (id.isEmpty())
    {
        return;
    }
    if (m_owner->m_window->m_controlsLocked)
    {
        if (showDialogOnFailure)
        {
            QMessageBox::information(m_owner->popupParent(), "Select Memory",
                                     "Unlock controls before selecting a memory.");
        }
        else
        {
            m_owner->m_window->showToast(QStringLiteral("Controls are locked"), 4000, MainWindow::ToastKind::Warning);
        }
        return;
    }

    bool found = false;
    const MemoryRecord memory = m_owner->memoryForId(id, &found);
    if (!found)
    {
        return;
    }
    if (!m_owner->m_window->m_model->isReady() || !m_owner->m_window->m_vfo)
    {
        if (showDialogOnFailure)
        {
            QMessageBox::information(m_owner->popupParent(), "Select Memory",
                                     "Connect to the radio and wait for sync before selecting a memory.");
        }
        else
        {
            m_owner->m_window->showToast(QStringLiteral("Connect to the radio before selecting a memory"), 4000,
                                         MainWindow::ToastKind::Warning);
        }
        return;
    }

    quint16 group = 0;
    quint16 channel = 0;
    if (!m_owner->parseRadioMemoryId(memory.id, &group, &channel))
    {
        return;
    }

    m_owner->m_window->m_applyingMemorySelection = true;
    m_owner->m_window->m_activeMemorySelectionReleaseScheduled = false;
    const int generation = ++m_owner->m_window->m_memorySelectionGeneration;
    m_owner->m_window->setActiveMemory(memory.id, memory.name, memory.receiveHz, memory.duplexMode, memory.offsetHz,
                                       memory.toneMode, memory.toneValue);
    m_owner->m_window->m_model->selectRadioMemory(group, channel);
    m_owner->m_window->checkIfMemorySelectionComplete();
    // Timeout guard: release the memory-selection protection after 3 s in case
    // the radio never confirms the selected channel.
    QTimer::singleShot(3000, this,
                       [this, generation]()
                       {
                           if (m_owner->m_window->m_memorySelectionGeneration != generation)
                           {
                               return;
                           }
                           m_owner->m_window->m_applyingMemorySelection = false;
                           m_owner->m_window->m_activeMemorySelectionReleaseScheduled = false;
                       });
    m_owner->m_window->showToast(QStringLiteral("Selected memory: %1").arg(memory.name));
}


void MemorySelectionController::applyMemoryToVfo(const MemoryRecord& memory)
{
    if (!m_owner->m_window->m_vfo)
    {
        return;
    }

    m_owner->m_window->m_vfo->applyFrequency(memory.receiveHz);
    m_owner->m_window->m_vfo->applyMode(memoryModeLabel(memory.mode));
    m_owner->m_window->m_vfo->applyRepeaterOffsetHz(memory.offsetHz);
    m_owner->m_window->m_vfo->applyDuplexMode(static_cast<duplexMode_t>(memory.duplexMode));
    if (isDtcsToneMode(static_cast<rptAccessTxRx_t>(memory.toneMode)))
    {
        m_owner->m_window->m_vfo->applyDtcsCode(memory.toneValue);
    }
    else if (memory.toneMode != ratrNN)
    {
        m_owner->m_window->m_vfo->applyToneFrequency(memory.toneValue);
    }
    m_owner->m_window->m_vfo->applyToneAccessMode(static_cast<rptAccessTxRx_t>(memory.toneMode));
}


void MemorySelectionController::copySelectedMemory()
{
    const QString id = selectedMemoryId();
    if (id.isEmpty())
    {
        QMessageBox::information(m_owner->popupParent(), "Copy Memory", "Choose one memory first.");
        return;
    }

    bool found = false;
    MemoryRecord copy = m_owner->memoryForId(id, &found);
    if (!found)
    {
        return;
    }

    copy.name = (copy.name.isEmpty() ? QStringLiteral("Copy") : QStringLiteral("%1 Copy").arg(copy.name))
                    .left(kRadioMemoryNameMaxChars);

    quint16 group = kRadioMemoryFirstGroup;
    quint16 channel = 0;
    if (!m_owner->parseRadioMemoryId(id, &group, nullptr) || !m_owner->firstOpenChannelForGroup(group, &channel))
    {
        QMessageBox::warning(m_owner->popupParent(), "Copy Memory",
                             "No empty user memory channel is available on this band.");
        return;
    }

    m_owner->writeMemoryRecord(copy, group, channel,
                               [this, name = copy.name](bool success)
                               {
                                   if (success)
                                   {
                                       m_owner->m_window->showToast(QStringLiteral("Copied memory: %1").arg(name));
                                   }
                               });
}


void MemorySelectionController::removeSelectedMemory()
{
    const QString id = selectedMemoryId();
    if (id.isEmpty())
    {
        QMessageBox::information(m_owner->popupParent(), "Remove Memory", "Choose one memory first.");
        return;
    }

    bool found = false;
    const MemoryRecord memory = m_owner->memoryForId(id, &found);
    if (!found)
    {
        return;
    }

    quint16 group = 0;
    quint16 channel = 0;
    if (!m_owner->parseRadioMemoryId(id, &group, &channel))
    {
        return;
    }
    if (QMessageBox::question(m_owner->popupParent(), "Remove Memory",
                              QStringLiteral("Remove memory \"%1\"?").arg(memory.name)) != QMessageBox::Yes)
    {
        return;
    }

    m_owner->deleteRadioMemory(group, channel,
                               [this](bool success)
                               {
                                   if (success)
                                   {
                                       m_owner->m_window->showToast(QStringLiteral("Memory removed"));
                                   }
                               });
}


void MemorySelectionController::moveSelectedMemory(int direction)
{
    const QString id = selectedMemoryId();
    if (id.isEmpty())
    {
        QMessageBox::information(m_owner->popupParent(), "Move Memory", "Choose one memory first.");
        return;
    }

    QVector<MemoryRecord> memories = m_owner->currentMemories();
    const QString bandFilter = m_owner->m_window->m_memoryBandFilter
                                   ? m_owner->m_window->m_memoryBandFilter->currentData().toString()
                                   : QString();
    if (!bandFilter.isEmpty())
    {
        QMessageBox::information(m_owner->popupParent(), "Move Memory", "Switch to All memories before reordering.");
        return;
    }

    QVector<int> visibleIndexes;
    for (int i = 0; i < memories.size(); ++i)
    {
        visibleIndexes.append(i);
    }

    int visiblePosition = -1;
    for (int i = 0; i < visibleIndexes.size(); ++i)
    {
        if (memories.at(visibleIndexes.at(i)).id == id)
        {
            visiblePosition = i;
            break;
        }
    }

    const int targetPosition = visiblePosition + direction;
    if (visiblePosition < 0 || targetPosition < 0 || targetPosition >= visibleIndexes.size())
    {
        return;
    }

    const MemoryRecord source = memories.at(visibleIndexes.at(visiblePosition));
    const MemoryRecord target = memories.at(visibleIndexes.at(targetPosition));
    quint16 sourceGroup = 0;
    quint16 sourceChannel = 0;
    quint16 targetGroup = 0;
    quint16 targetChannel = 0;
    if (!m_owner->parseRadioMemoryId(source.id, &sourceGroup, &sourceChannel) ||
        !m_owner->parseRadioMemoryId(target.id, &targetGroup, &targetChannel))
    {
        return;
    }
    m_owner->queueRadioMemoryWrites({radioMemoryFromRecord(source, targetGroup, targetChannel),
                                     radioMemoryFromRecord(target, sourceGroup, sourceChannel)},
                                    0, QStringLiteral("Reordering memories"),
                                    [this](bool success)
                                    {
                                        if (success)
                                        {
                                            m_owner->m_window->showToast(QStringLiteral("Memories reordered"));
                                        }
                                    });
}
