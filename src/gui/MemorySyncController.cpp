#include "MemorySyncController.h"

#include "AppSettings.h"
#include "LogCategories.h"
#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "MemoryController.h"
#include "MemoryConstants.h"
#include "MemoryRecordHelpers.h"
#include "MemoryEditorPolicy.h"
#include "MemorySyncPolicy.h"
#include "MemoryViewController.h"
#include "models/RadioModel.h"

#include <QMessageBox>
#include <QThread>
#include <QTimer>

using namespace sdr9700::ui::main_window;
using namespace sdr9700::memory;

MemorySyncController::MemorySyncController(MemoryController* owner) : QObject(owner), m_owner(owner)
{
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(kRadioMemoryRefreshIntervalMs);
    connect(m_refreshTimer, &QTimer::timeout, this, &MemorySyncController::requestNextRadioMemory);

    m_periodicRefreshTimer = new QTimer(this);
    setMemoryPollIntervalSeconds(
        AppSettings::instance()
            .value(QString::fromLatin1(kMemoryPollIntervalSecondsSettingsKey), kDefaultMemoryPollIntervalSeconds)
            .toInt());
    connect(m_periodicRefreshTimer, &QTimer::timeout, this, &MemorySyncController::requestRadioMemoryRefresh);

    m_syncTimeoutTimer = new QTimer(this);
    m_syncTimeoutTimer->setSingleShot(true);
    connect(m_syncTimeoutTimer, &QTimer::timeout, this, [this]() { finishRadioMemoryRefresh(true); });

    m_replyGraceTimer = new QTimer(this);
    m_replyGraceTimer->setSingleShot(true);
    connect(m_replyGraceTimer, &QTimer::timeout, this, [this]() { finishRadioMemoryRefresh(false); });
}

void MemorySyncController::forceRadioMemorySync()
{
    if (!m_owner->m_window->m_model || !m_owner->m_window->m_model->isConnected())
    {
        QMessageBox::information(m_owner->popupParent(), QStringLiteral("Sync Memories"),
                                 QStringLiteral("Connect to the radio before syncing memories."));
        return;
    }

    if (m_refreshInProgress)
    {
        cancelRadioMemoryRefresh();
    }
    if (m_owner->memoryOperationInProgress())
    {
        QMessageBox::information(m_owner->popupParent(), QStringLiteral("Sync Memories"),
                                 QStringLiteral("Wait for the current memory operation to finish before syncing."));
        return;
    }

    requestRadioMemoryRefresh();
    m_owner->reloadMemoryTable();
    m_owner->showMemoryToast(QStringLiteral("Radio memory sync started"));
}

void MemorySyncController::setMemoryPollIntervalSeconds(int seconds)
{
    m_periodicRefreshTimer->setInterval(sdr9700::clampMemoryPollIntervalSeconds(seconds) * 1000);
}

void MemorySyncController::handleRadioReadyChanged(bool ready)
{
    qInfo(logGui()) << "MemorySyncController observed radio readyChanged:" << ready;
    if (ready)
    {
        if (m_initialSyncComplete)
        {
            m_initialSyncComplete = false;
            emit m_owner->initialMemorySyncChanged(false);
        }
        m_owner->m_window->showToast(QStringLiteral("Syncing radio memories..."), 0);
        requestRadioMemoryRefresh();
        m_periodicRefreshTimer->start();
        return;
    }

    m_refreshTimer->stop();
    m_periodicRefreshTimer->stop();
    m_owner->m_memoryViewController->stopScheduledRefresh();
    finishRadioMemoryRefresh(false);
    if (m_initialSyncComplete)
    {
        m_initialSyncComplete = false;
        emit m_owner->initialMemorySyncChanged(false);
    }
    m_owner->m_radioMemoriesByKey.clear();
    clearReceivedMemories();
    m_owner->rebuildMemoryViews();
}

void MemorySyncController::requestRadioMemoryRefresh()
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, &MemorySyncController::requestRadioMemoryRefresh, Qt::QueuedConnection);
        return;
    }

    if (!m_owner->m_window->m_model)
    {
        qInfo(logGui()) << "Radio memory sync skipped; radio model is not available";
        return;
    }
    if (!m_owner->m_window->m_model->isConnected())
    {
        qInfo(logGui()) << "Radio memory sync skipped; radio model is not connected";
        return;
    }
    if (m_refreshInProgress)
    {
        qInfo(logGui()) << "Radio memory sync skipped; refresh is already in progress";
        return;
    }

    m_refreshGroup = kRadioMemoryFirstGroup;
    m_refreshChannel = kRadioMemoryFirstChannel;
    m_currentGroup = 0;
    m_currentChannel = 0;
    m_refreshInProgress = true;
    m_receivedMemoryKeys.clear();
    m_expectedMemoryKeys.clear();
    for (quint16 group = kRadioMemoryFirstGroup; group <= kRadioMemoryLastGroup; ++group)
    {
        for (quint16 channel = kRadioMemoryFirstChannel; channel <= kRadioMemoryLastChannel; ++channel)
        {
            m_expectedMemoryKeys.insert(radioMemoryKey(group, channel));
        }
    }

    m_syncTimeoutTimer->start(radioMemorySyncTimeoutMs());
    m_replyGraceTimer->stop();
    qInfo(logGui()) << "Radio memory sync started; polling" << kRadioMemorySyncTotal << "slots";
    requestNextRadioMemory();
    m_owner->rebuildMemoryViews();
}

void MemorySyncController::requestRadioMemoryRefreshForOperation(Completion completion)
{
    if (!m_owner->m_window->m_model || !m_owner->m_window->m_model->isConnected() || m_refreshInProgress)
    {
        if (completion)
        {
            completion(false);
        }
        return;
    }

    m_operationCompletion = std::move(completion);
    requestRadioMemoryRefresh();
}

void MemorySyncController::requestNextRadioMemory()
{
    if (!m_refreshInProgress)
    {
        return;
    }
    if (!m_owner->m_window->m_model || !m_owner->m_window->m_model->isConnected())
    {
        finishRadioMemoryRefresh(false);
        return;
    }
    if (m_refreshGroup > kRadioMemoryLastGroup)
    {
        m_refreshTimer->stop();
        if (allExpectedRadioMemoriesReceived())
        {
            qInfo(logGui()) << "Radio memory sync received all expected replies";
            finishRadioMemoryRefresh(false);
            return;
        }
        qInfo(logGui()) << "Radio memory sync poll pass complete; received" << m_receivedMemoryKeys.size() << "of"
                        << m_expectedMemoryKeys.size() << "possible replies, waiting briefly for late replies";
        m_owner->setMemoryProgress(QStringLiteral("Finalizing radio memory sync"), m_receivedMemoryKeys.size(),
                                   m_expectedMemoryKeys.size());
        if (!m_replyGraceTimer->isActive())
        {
            m_replyGraceTimer->start(kRadioMemorySyncReplyGraceMs);
        }
        return;
    }

    m_currentGroup = m_refreshGroup;
    m_currentChannel = m_refreshChannel;
    if (m_currentChannel == kRadioMemoryFirstChannel)
    {
        qInfo(logGui()) << "Radio memory sync polling" << memoryBandLabelForGroup(m_currentGroup);
    }
    else if (m_currentChannel <= 5 || (m_currentChannel % 25) == 0)
    {
        qInfo(logGui()) << "Radio memory sync polling" << memoryBandLabelForGroup(m_currentGroup) << "channel"
                        << m_currentChannel;
    }
    const int syncIndex = sdr9700::memorySyncProgressIndex(m_currentGroup, m_currentChannel, kRadioMemoryFirstGroup,
                                                           kRadioMemoryFirstChannel, kRadioMemoryLastChannel);
    m_owner->setMemoryProgress(QStringLiteral("Syncing %1 channel %2")
                                   .arg(memoryBandLabelForGroup(m_currentGroup))
                                   .arg(m_currentChannel, 3, 10, QLatin1Char('0')),
                               syncIndex, kRadioMemorySyncTotal);
    m_owner->m_window->m_model->requestRadioMemory(m_refreshGroup, m_refreshChannel);
    sdr9700::advanceMemorySyncSlot(m_refreshGroup, m_refreshChannel, kRadioMemoryFirstChannel, kRadioMemoryLastChannel);
    m_refreshTimer->start();
}

void MemorySyncController::handleRadioMemoryReceived(quint32 key)
{
    m_receivedMemoryKeys.insert(key);
    if (!m_refreshInProgress)
    {
        return;
    }

    m_refreshTimer->stop();
    if (m_refreshGroup > kRadioMemoryLastGroup)
    {
        m_owner->setMemoryProgress(QStringLiteral("Finalizing radio memory sync"), m_receivedMemoryKeys.size(),
                                   m_expectedMemoryKeys.size());
    }
    if (allExpectedRadioMemoriesReceived())
    {
        finishRadioMemoryRefresh(false);
        return;
    }
    requestNextRadioMemory();
}

void MemorySyncController::finishRadioMemoryRefresh(bool timedOut)
{
    m_refreshTimer->stop();
    m_syncTimeoutTimer->stop();
    m_replyGraceTimer->stop();
    m_owner->m_memoryViewController->stopScheduledRefresh();
    const bool wasInProgress = m_refreshInProgress;
    const bool completedPollPass = wasInProgress && !timedOut && m_owner->m_window->m_model &&
                                   m_owner->m_window->m_model->isConnected() && m_refreshGroup > kRadioMemoryLastGroup;
    const bool receivedAllExpected = wasInProgress && allExpectedRadioMemoriesReceived();
    const bool noRadioReplies = completedPollPass && m_receivedMemoryKeys.isEmpty();
    timedOut = timedOut || noRadioReplies;
    m_refreshInProgress = false;
    m_currentGroup = 0;
    m_currentChannel = 0;
    m_expectedMemoryKeys.clear();
    m_owner->clearMemoryProgress();

    if (timedOut && wasInProgress)
    {
        qWarning(logGui()) << "Radio memory sync failed after receiving" << m_receivedMemoryKeys.size()
                           << "memory replies" << (noRadioReplies ? "(no CI-V memory replies)" : "(timeout)");
        m_owner->m_window->showToast(m_initialSyncComplete ? QStringLiteral("Radio memory sync timed out")
                                                           : QStringLiteral("Radio memory sync timed out; retrying"),
                                     5000, MainWindow::ToastKind::Warning);
        if (!m_initialSyncComplete && m_owner->m_window->m_model && m_owner->m_window->m_model->isConnected())
        {
            QTimer::singleShot(kRadioMemoryInitialSyncRetryDelayMs, this,
                               [this]()
                               {
                                   if (!m_initialSyncComplete)
                                   {
                                       requestRadioMemoryRefresh();
                                   }
                               });
        }
    }
    else if (completedPollPass && !m_initialSyncComplete)
    {
        m_initialSyncComplete = true;
        qInfo(logGui()) << "Initial radio memory sync complete with" << m_owner->m_radioMemoriesByKey.size()
                        << "stored memories";
        emit m_owner->initialMemorySyncChanged(true);
    }
    else if (completedPollPass)
    {
        qInfo(logGui()) << "Radio memory sync complete with" << m_owner->m_radioMemoriesByKey.size()
                        << "stored memories";
    }
    m_owner->rebuildMemoryViews();

    Completion operationCompletion = std::move(m_operationCompletion);
    m_operationCompletion = {};
    if (operationCompletion)
    {
        operationCompletion(completedPollPass && !timedOut && receivedAllExpected);
    }
}

void MemorySyncController::cancelRadioMemoryRefresh()
{
    finishRadioMemoryRefresh(false);
}

void MemorySyncController::clearReceivedMemories()
{
    m_receivedMemoryKeys.clear();
    m_expectedMemoryKeys.clear();
}

bool MemorySyncController::allExpectedRadioMemoriesReceived() const
{
    return sdr9700::memorySyncComplete(m_expectedMemoryKeys, m_receivedMemoryKeys);
}

bool MemorySyncController::initialMemorySyncComplete() const
{
    return m_initialSyncComplete;
}

bool MemorySyncController::refreshInProgress() const
{
    return m_refreshInProgress;
}

bool MemorySyncController::hasReceivedMemory(quint32 key) const
{
    return m_receivedMemoryKeys.contains(key);
}

quint16 MemorySyncController::currentGroup() const
{
    return m_currentGroup;
}

quint16 MemorySyncController::currentChannel() const
{
    return m_currentChannel;
}
