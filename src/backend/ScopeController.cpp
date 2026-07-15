#include "ScopeController.h"
#include "LogCategories.h"
#include "ScopeAdapter.h"

#include <QElapsedTimer>
#include <QTimer>
#include <limits>

namespace
{
constexpr int kScopeFlushIntervalMs = 16;
}

ScopeController::ScopeController(QObject* parent) : QObject(parent)
{
    m_flushTimer = new QTimer(this);
    m_flushTimer->setSingleShot(true);
    m_flushTimer->setInterval(kScopeFlushIntervalMs);
    connect(m_flushTimer, &QTimer::timeout, this, &ScopeController::flushLatestFrame);
}

void ScopeController::reset()
{
    if (m_flushTimer)
    {
        m_flushTimer->stop();
    }
    m_pendingFrame = {};
    m_hasPendingFrame = false;
}

void ScopeController::acceptScopeData(const ScopeData& data)
{
    qDebug(logBandscope()) << "ScopeWaveData: valid=" << data.valid << "dataLen=" << data.data.size()
                           << "start=" << data.startFreq << "end=" << data.endFreq;
    if (!data.valid || data.data.isEmpty())
    {
        return;
    }

    m_pendingFrame = data;
    m_hasPendingFrame = true;
    scheduleFlush();
}

void ScopeController::scheduleFlush()
{
    if (m_flushTimer && !m_flushTimer->isActive())
    {
        m_flushTimer->start();
    }
}

void ScopeController::flushLatestFrame()
{
    if (!m_hasPendingFrame)
    {
        return;
    }

    const ScopeData frame = m_pendingFrame;
    m_hasPendingFrame = false;
    emit scopeDataReceived();

    const QVector<float> levels = ScopeAdapter::toLevels(frame.data);
    if (logBandscope().isDebugEnabled())
    {
        static QElapsedTimer statsTimer;
        if (!statsTimer.isValid() || statsTimer.elapsed() >= 1000)
        {
            int rawMin = std::numeric_limits<int>::max();
            int rawMax = std::numeric_limits<int>::min();
            int rawZeros = 0;
            qint64 rawTotal = 0;
            for (const unsigned char raw : frame.data)
            {
                const int value = static_cast<int>(raw);
                rawMin = qMin(rawMin, value);
                rawMax = qMax(rawMax, value);
                rawTotal += value;
                if (value == 0)
                {
                    ++rawZeros;
                }
            }
            qDebug(logBandscope()).nospace()
                << "Bandscope stats: range=" << frame.startFreq << "-" << frame.endFreq << "MHz"
                << " levels[min=" << rawMin << " max=" << rawMax
                << " avg=" << (double(rawTotal) / double(frame.data.size())) << " zeros=" << rawZeros << "/"
                << frame.data.size() << "]";
            statsTimer.restart();
        }
    }

    emit spectrumDataReady(levels, frame.startFreq, frame.endFreq, frame.oor);
}
