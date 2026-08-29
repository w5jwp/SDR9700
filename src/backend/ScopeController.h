#pragma once

#include "Types.h"

#include <QObject>
#include <QElapsedTimer>
#include <QVector>

class QTimer;

class ScopeController : public QObject
{
    Q_OBJECT

  public:
    explicit ScopeController(QObject* parent = nullptr);

  public slots:
    void reset();
    void acceptScopeData(const ScopeData& data);

  signals:
    void spectrumDataReady(const QVector<float>& levels, double startMhz, double endMhz, bool outOfRange);
    void scopeDataReceived();

  private:
    void scheduleFlush();
    void flushLatestFrame();

    QTimer* m_flushTimer{nullptr};
    ScopeData m_pendingFrame;
    QVector<float> m_levelsScratch;
    bool m_hasPendingFrame{false};
    QElapsedTimer m_frameArrivalClock;
};
