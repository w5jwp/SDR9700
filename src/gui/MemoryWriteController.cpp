#include "MemoryWriteController.h"

#include "MainWindow.h"
#include "LogCategories.h"
#include "MemoryController.h"
#include "MemoryConstants.h"
#include "MemoryRecordHelpers.h"
#include "MemorySyncPolicy.h"
#include "models/RadioModel.h"

#include <QTimer>

using namespace sdr9700::memory;

MemoryWriteController::MemoryWriteController(MemoryController* owner) : QObject(owner), m_owner(owner)
{
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    m_timeoutTimer->setInterval(kRadioMemoryWriteReadbackTimeoutMs);
    connect(m_timeoutTimer, &QTimer::timeout, this,
            [this]()
            {
                if (m_waitingForReadback)
                {
                    finish(true);
                }
            });
}

void MemoryWriteController::queueWrites(const QVector<MemoryType>& memories, int startDelayMs,
                                        const QString& progressLabel, Completion completion)
{
    if (!m_owner->m_window->m_model || !m_owner->m_window->m_model->isConnected())
    {
        if (completion)
        {
            completion(false);
        }
        return;
    }

    QTimer::singleShot(qMax(0, startDelayMs), this,
                       [this, memories, progressLabel, completion = std::move(completion)]() mutable
                       { startWrites(memories, progressLabel, std::move(completion)); });
}

void MemoryWriteController::startWrites(const QVector<MemoryType>& memories, const QString& progressLabel,
                                        Completion completion)
{
    if (!m_owner->m_window->m_model || !m_owner->m_window->m_model->isConnected())
    {
        if (completion)
        {
            completion(false);
        }
        return;
    }
    if (active())
    {
        m_owner->m_window->showToast(QStringLiteral("Memory write already in progress"), 5000,
                                     MainWindow::ToastKind::Warning);
        if (completion)
        {
            completion(false);
        }
        return;
    }
    if (memories.isEmpty())
    {
        if (completion)
        {
            completion(true);
        }
        return;
    }

    m_memories = memories;
    m_index = 0;
    m_progressLabel = progressLabel;
    m_completion = std::move(completion);
    m_owner->setMemoryProgress(m_progressLabel, 0, m_memories.size());
    writeNext();
}

void MemoryWriteController::writeNext()
{
    if (!m_owner->m_window->m_model || !m_owner->m_window->m_model->isConnected())
    {
        finish(true);
        return;
    }
    if (m_index >= m_memories.size())
    {
        finish(false);
        return;
    }

    const MemoryType memory = m_memories.at(m_index);
    m_expectedKey = radioMemoryKey(memory.group, memory.channel);
    m_waitingForReadback = true;
    m_owner->m_window->m_model->writeRadioMemory(memory);
    m_timeoutTimer->start();
}

void MemoryWriteController::handleReadback(quint32 key, const MemoryType& memory)
{
    if (!sdr9700::memoryReadbackExpected(m_waitingForReadback, m_expectedKey, key))
    {
        return;
    }
    if (m_index >= m_memories.size() || !radioMemoryReadbackMatches(m_memories.at(m_index), memory))
    {
        qWarning(logGui()) << "Radio memory write readback did not match requested contents for"
                           << memoryBandLabelForGroup(memory.group) << "channel" << memory.channel;
        return;
    }

    m_timeoutTimer->stop();
    m_waitingForReadback = false;
    ++m_index;
    m_owner->setMemoryProgress(m_progressLabel, m_index, m_memories.size());
    QTimer::singleShot(kRadioMemoryWriteIntervalMs, this, &MemoryWriteController::writeNext);
}

bool MemoryWriteController::active() const
{
    return m_waitingForReadback || !m_memories.isEmpty();
}

void MemoryWriteController::finish(bool failed)
{
    m_timeoutTimer->stop();
    const Completion completion = std::move(m_completion);
    const QString label = m_progressLabel;
    m_memories.clear();
    m_index = 0;
    m_expectedKey = 0;
    m_waitingForReadback = false;
    m_progressLabel.clear();
    m_completion = {};
    m_owner->clearMemoryProgress();
    if (failed)
    {
        m_owner->m_window->showToast(label.isEmpty() ? QStringLiteral("Memory write timed out")
                                                     : QStringLiteral("%1 timed out").arg(label),
                                     5000, MainWindow::ToastKind::Warning);
    }
    if (completion)
    {
        completion(!failed);
    }
}
