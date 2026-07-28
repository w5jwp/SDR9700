#include "MemorySyncController.h"

#include "MemoryController.h"
#include "MainWindowHelpers.h"
#include "MemorySyncPolicy.h"

#include <QMessageBox>

using namespace sdr9700::ui::main_window;

MemorySyncController::MemorySyncController(MemoryController* owner) : QObject(owner), m_owner(owner) {}

void MemorySyncController::forceRadioMemorySync()
{
    if (!m_owner->radioConnected())
    {
        QMessageBox::information(m_owner->popupParent(), QStringLiteral("Sync Memories"),
                                 QStringLiteral("Connect to the radio before syncing memories."));
        return;
    }

    if (m_owner->memoryRefreshInProgress())
    {
        m_owner->cancelMemoryRefresh();
    }
    if (m_owner->memoryOperationInProgress())
    {
        QMessageBox::information(m_owner->popupParent(), QStringLiteral("Sync Memories"),
                                 QStringLiteral("Wait for the current memory operation to finish before syncing."));
        return;
    }

    m_owner->requestRadioMemoryRefreshFromController();
    m_owner->reloadMemoryTable();
    m_owner->showMemoryToast(QStringLiteral("Radio memory sync started"));
}

void MemorySyncController::setMemoryPollIntervalSeconds(int seconds)
{
    m_owner->setMemoryPollTimerIntervalSeconds(sdr9700::clampMemoryPollIntervalSeconds(seconds));
}
