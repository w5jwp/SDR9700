#include "MeterController.h"

#include <QTimer>
#include <QtGlobal>

namespace
{
// Keep GUI updates coalesced to approximately one display frame without
// adding a noticeable delay after each 10 Hz radio meter reply.
constexpr int kMeterFlushIntervalMs = 16;
} // namespace

MeterController::MeterController(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<MeterSnapshot>("MeterSnapshot");

    m_flushTimer = new QTimer(this);
    m_flushTimer->setSingleShot(true);
    m_flushTimer->setInterval(kMeterFlushIntervalMs);
    m_flushTimer->setTimerType(Qt::PreciseTimer);
    connect(m_flushTimer, &QTimer::timeout, this, &MeterController::flush);
}

void MeterController::reset()
{
    if (m_flushTimer)
    {
        m_flushTimer->stop();
    }
    m_snapshot = {};
    m_dirty = false;
    emit snapshotChanged(m_snapshot);
}

void MeterController::resetTransmitMeters()
{
    m_snapshot.powerWatts = 0.0;
    m_snapshot.powerValid = false;
    m_snapshot.swr = 1.0;
    m_snapshot.swrValid = false;
    m_snapshot.alc = 0.0;
    m_snapshot.alcValid = false;
    m_snapshot.compressionDb = 0.0;
    m_snapshot.compressionValid = false;
    m_snapshot.voltageVolts = 0.0;
    m_snapshot.voltageValid = false;
    m_snapshot.currentAmps = 0.0;
    m_snapshot.currentValid = false;
    m_snapshot.txAudioPeak = 0;
    m_snapshot.txAudioRms = 0;
    scheduleFlush();
}

void MeterController::setSMeter(int value)
{
    m_snapshot.sMeter = qBound(0, value, 255);
    m_snapshot.sMeterValid = true;
    scheduleFlush();
}

void MeterController::setPowerMeter(double watts)
{
    m_snapshot.powerWatts = qBound(0.0, watts, 120.0);
    m_snapshot.powerValid = true;
    scheduleFlush();
}

void MeterController::setSwr(double swr)
{
    m_snapshot.swr = qBound(1.0, swr, 6.0);
    m_snapshot.swrValid = true;
    scheduleFlush();
}

void MeterController::setAlc(double alc)
{
    m_snapshot.alc = qBound(0.0, alc, 2.0);
    m_snapshot.alcValid = true;
    scheduleFlush();
}

void MeterController::setCompressionMeter(double db)
{
    m_snapshot.compressionDb = qBound(0.0, db, 25.5);
    m_snapshot.compressionValid = true;
    scheduleFlush();
}

void MeterController::setVoltageMeter(double volts)
{
    m_snapshot.voltageVolts = qBound(0.0, volts, 16.0);
    m_snapshot.voltageValid = true;
    scheduleFlush();
}

void MeterController::setCurrentMeter(double amps)
{
    m_snapshot.currentAmps = qBound(0.0, amps, 20.0);
    m_snapshot.currentValid = true;
    scheduleFlush();
}

void MeterController::setTransmitAudioLevel(int peak, int rms)
{
    m_snapshot.txAudioPeak = qBound(0, peak, 255);
    m_snapshot.txAudioRms = qBound(0, rms, 255);
    scheduleFlush();
}

void MeterController::scheduleFlush()
{
    m_dirty = true;
    if (m_flushTimer && !m_flushTimer->isActive())
    {
        m_flushTimer->start();
    }
}

void MeterController::flush()
{
    if (!m_dirty)
    {
        return;
    }
    m_dirty = false;
    emit snapshotChanged(m_snapshot);
}
