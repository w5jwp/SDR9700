#include "MemoryController.h"
#include "MemoryControllerHelpers.h"

#include "DialogPlacement.h"
#include "LogCategories.h"
#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "MemoryCsvController.h"
#include "MemoryEditorController.h"
#include "MemorySelectionController.h"
#include "MemoryPanel.h"
#include "MemorySyncController.h"
#include "MemoryViewController.h"
#include "MemoryWriteController.h"
#include "RadioCapabilities.h"
#include "VfoPanel.h"
#include "UtilityWindow.h"
#include "models/RadioModel.h"
#include "models/VfoModel.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStringList>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <algorithm>
#include <cstring>
#include <initializer_list>

using namespace sdr9700::ui::main_window;


MemoryController::MemoryController(MainWindow* window) : QObject(window), m_window(window)
{
    m_memoryCsvController = new MemoryCsvController(this);
    m_memoryEditorController = new MemoryEditorController(this);
    m_memorySelectionController = new MemorySelectionController(this);
    m_memorySyncController = new MemorySyncController(this);
    m_memoryViewController = new MemoryViewController(this);
    m_memoryWriteController = new MemoryWriteController(this);

    connect(m_window->m_model, &RadioModel::radioMemoryReceived, this, &MemoryController::handleRadioMemoryReceived,
            Qt::QueuedConnection);
    connect(m_window->m_model, &RadioModel::readyChanged, m_memorySyncController,
            &MemorySyncController::handleRadioReadyChanged);
}

void MemoryController::forceRadioMemorySync()
{
    m_memorySyncController->forceRadioMemorySync();
}

void MemoryController::setMemoryPollIntervalSeconds(int seconds)
{
    m_memorySyncController->setMemoryPollIntervalSeconds(seconds);
}

bool MemoryController::initialMemorySyncComplete() const
{
    return m_memorySyncController->initialMemorySyncComplete();
}

bool MemoryController::memoryRefreshInProgress() const
{
    return m_memorySyncController->refreshInProgress();
}

void MemoryController::buildMemoryWindow()
{
    m_memoryViewController->buildMemoryWindow();
}

void MemoryController::showMemoryWindow()
{
    m_memoryViewController->showMemoryWindow();
}

QString MemoryController::selectedMemoryId() const
{
    return m_memorySelectionController->selectedMemoryId();
}

void MemoryController::selectCheckedMemory()
{
    m_memorySelectionController->selectCheckedMemory();
}

void MemoryController::selectMemoryById(const QString& id, bool showDialogOnFailure)
{
    m_memorySelectionController->selectMemoryById(id, showDialogOnFailure);
}

void MemoryController::copySelectedMemory()
{
    m_memorySelectionController->copySelectedMemory();
}

void MemoryController::removeSelectedMemory()
{
    m_memorySelectionController->removeSelectedMemory();
}

void MemoryController::moveSelectedMemory(int direction)
{
    m_memorySelectionController->moveSelectedMemory(direction);
}

void MemoryController::applyMemoryToVfo(const MemoryRecord& memory)
{
    m_memorySelectionController->applyMemoryToVfo(memory);
}

QWidget* MemoryController::popupParent() const
{
    if (m_window && m_window->m_memoryWindow && m_window->m_memoryWindow->isVisible())
    {
        return m_window->m_memoryWindow;
    }
    return m_window;
}

void MemoryController::requestRadioMemoryRefresh()
{
    m_memorySyncController->requestRadioMemoryRefresh();
}

bool MemoryController::memoryOperationInProgress() const
{
    return m_memoryViewController->operationInProgress();
}

bool MemoryController::memoryEditorVisible() const
{
    return m_memoryEditorPane && m_memoryEditorPane->isVisible();
}

void MemoryController::clearMemoryEditButtonChecked()
{
    if (m_memoryEditButton)
    {
        m_memoryEditButton->setChecked(false);
    }
}

void MemoryController::closeMemoryEditorFromController()
{
    m_memoryViewController->closeEditorPane();
}

void MemoryController::showMemoryToast(const QString& message)
{
    m_window->showToast(message);
}

void MemoryController::handleRadioMemoryReceived(MemoryType memory)
{
    if (memory.group < kRadioMemoryFirstGroup || memory.group > kRadioMemoryLastGroup ||
        memory.channel < kRadioMemoryFirstChannel || memory.channel > kRadioMemoryLastChannel)
    {
        return;
    }

    const quint32 key = radioMemoryKey(memory.group, memory.channel);
    if (m_memorySyncController->refreshInProgress() && (memory.channel <= 5 || (memory.channel % 25) == 0))
    {
        qInfo(logGui()) << "Radio memory sync received" << memoryBandLabelForGroup(memory.group) << "channel"
                        << memory.channel;
    }
    if (radioMemoryIsStored(memory))
    {
        m_radioMemoriesByKey.insert(key, memory);
        if (m_window->m_activeMemoryId == radioMemoryId(memory.group, memory.channel))
        {
            const MemoryRecord activeMemory = recordFromRadioMemory(memory);
            m_window->setActiveMemory(activeMemory.id, activeMemory.name, activeMemory.receiveHz,
                                      activeMemory.duplexMode, activeMemory.offsetHz, activeMemory.toneMode,
                                      activeMemory.toneValue);
            applyMemoryToVfo(activeMemory);
        }
    }
    else
    {
        if (m_window->m_activeMemoryId == radioMemoryId(memory.group, memory.channel))
        {
            if (m_window->m_applyingMemorySelection)
            {
                m_memorySyncController->handleRadioMemoryReceived(key);
                m_memoryWriteController->handleReadback(key, memory);
                return;
            }
            m_window->clearActiveMemory();
        }
        m_radioMemoriesByKey.remove(key);
    }
    m_memorySyncController->handleRadioMemoryReceived(key);
    scheduleMemoryViewsRebuild();
    m_memoryWriteController->handleReadback(key, memory);
}

QVector<MemoryRecord> MemoryController::currentMemories() const
{
    QVector<MemoryRecord> memories;
    memories.reserve(m_radioMemoriesByKey.size());
    for (const MemoryType& radioMemory : m_radioMemoriesByKey)
    {
        memories.append(recordFromRadioMemory(radioMemory));
    }
    std::sort(memories.begin(), memories.end(),
              [](const MemoryRecord& left, const MemoryRecord& right)
              {
                  if (left.group == right.group)
                  {
                      return left.channel < right.channel;
                  }
                  return left.group < right.group;
              });
    return memories;
}

MemoryRecord MemoryController::memoryForId(const QString& id, bool* found) const
{
    if (found)
    {
        *found = false;
    }
    bool haveRadioMemory = false;
    const MemoryType radioMemory = radioMemoryForId(id, &haveRadioMemory);
    if (!haveRadioMemory)
    {
        return {};
    }
    if (found)
    {
        *found = true;
    }
    return recordFromRadioMemory(radioMemory);
}

MemoryType MemoryController::radioMemoryForId(const QString& id, bool* found) const
{
    if (found)
    {
        *found = false;
    }

    quint16 group = 0;
    quint16 channel = 0;
    if (!parseRadioMemoryId(id, &group, &channel))
    {
        return {};
    }

    const quint32 key = radioMemoryKey(group, channel);
    auto it = m_radioMemoriesByKey.constFind(key);
    if (it == m_radioMemoriesByKey.cend())
    {
        return {};
    }

    if (found)
    {
        *found = true;
    }
    return it.value();
}

bool MemoryController::parseRadioMemoryId(const QString& id, quint16* group, quint16* channel) const
{
    const QStringList parts = id.split(QLatin1Char(':'));
    if (parts.size() != 3 || parts.at(0) != QLatin1String("radio"))
    {
        return false;
    }

    bool groupOk = false;
    bool channelOk = false;
    const uint parsedGroup = parts.at(1).toUInt(&groupOk);
    const uint parsedChannel = parts.at(2).toUInt(&channelOk);
    if (!groupOk || !channelOk || parsedGroup < kRadioMemoryFirstGroup || parsedGroup > kRadioMemoryLastGroup ||
        parsedChannel < kRadioMemoryFirstChannel || parsedChannel > kRadioMemoryLastChannel)
    {
        return false;
    }

    if (group)
    {
        *group = static_cast<quint16>(parsedGroup);
    }
    if (channel)
    {
        *channel = static_cast<quint16>(parsedChannel);
    }
    return true;
}

void MemoryController::writeMemoryRecord(const MemoryRecord& memory, quint16 group, quint16 channel,
                                         MemoryWriteCompletion completion)
{
    if (!m_window->m_model || !m_window->m_model->isConnected())
    {
        if (completion)
        {
            completion(false);
        }
        return;
    }
    queueRadioMemoryWrites({radioMemoryFromRecord(memory, group, channel)}, 0, QStringLiteral("Writing memory"),
                           std::move(completion));
}

void MemoryController::deleteRadioMemory(quint16 group, quint16 channel, MemoryWriteCompletion completion)
{
    if (!m_window->m_model || !m_window->m_model->isConnected())
    {
        if (completion)
        {
            completion(false);
        }
        return;
    }
    queueRadioMemoryWrites({deletedRadioMemory(group, channel)}, 0, QStringLiteral("Removing memory"),
                           std::move(completion));
}

void MemoryController::queueRadioMemoryWrites(const QVector<MemoryType>& memories, int startDelayMs,
                                              const QString& progressLabel, MemoryWriteCompletion completion)
{
    m_memoryWriteController->queueWrites(memories, startDelayMs, progressLabel, std::move(completion));
}

bool MemoryController::firstOpenChannelForGroup(quint16 group, quint16* channel) const
{
    if (group < kRadioMemoryFirstGroup || group > kRadioMemoryLastGroup)
    {
        return false;
    }

    for (quint16 candidate = kRadioMemoryFirstChannel; candidate <= kRadioMemoryLastChannel; ++candidate)
    {
        const quint32 key = radioMemoryKey(group, candidate);
        if (!m_memorySyncController->hasReceivedMemory(key))
        {
            return false;
        }
        if (!m_radioMemoriesByKey.contains(key))
        {
            if (channel)
            {
                *channel = candidate;
            }
            return true;
        }
    }
    return false;
}

bool MemoryController::exportRadioMemories()
{
    return m_memoryCsvController->exportRadioMemories();
}

void MemoryController::importRadioMemories()
{
    m_memoryCsvController->importRadioMemories();
}

void MemoryController::editSelectedMemory()
{
    m_memoryEditorController->editSelectedMemory();
}

void MemoryController::moveSelectedMemoryUp()
{
    moveSelectedMemory(-1);
}

void MemoryController::moveSelectedMemoryDown()
{
    moveSelectedMemory(1);
}

void MemoryController::storeCurrentMemory()
{
    m_memoryEditorController->storeCurrentMemory();
}

void MemoryController::showMemoryEditor(const QString& memoryId)
{
    m_memoryEditorController->showMemoryEditor(memoryId);
}

void MemoryController::reloadMemoryTable()
{
    m_memoryViewController->rebuild();
}

void MemoryController::scheduleMemoryViewsRebuild()
{
    m_memoryViewController->scheduleRebuild();
}

void MemoryController::setMemoryProgress(const QString& label, int value, int maximum)
{
    m_memoryViewController->setProgress(label, value, maximum);
}

void MemoryController::clearMemoryProgress()
{
    m_memoryViewController->clearProgress();
}

void MemoryController::rebuildMemoryViews()
{
    m_memoryViewController->rebuild();
}

void MemoryController::closeMemoryEditorPane(bool resizeWindow)
{
    m_memoryViewController->closeEditorPane(resizeWindow);
}
