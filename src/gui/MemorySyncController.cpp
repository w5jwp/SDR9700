#include "MemorySyncController.h"

#include "MemoryController.h"

MemorySyncController::MemorySyncController(MemoryController* owner) : QObject(owner), m_owner(owner) {}

void MemorySyncController::forceRadioMemorySync()
{
    m_owner->forceRadioMemorySyncDirect();
}

void MemorySyncController::setMemoryPollIntervalSeconds(int seconds)
{
    m_owner->setMemoryPollIntervalSecondsDirect(seconds);
}

bool MemorySyncController::initialMemorySyncComplete() const
{
    return m_owner->initialMemorySyncCompleteDirect();
}
