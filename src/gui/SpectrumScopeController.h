#pragma once

#include "Vfo.h"

#include <QObject>
#include <QVector>

class MainWindow;
class QComboBox;
class QVBoxLayout;

class SpectrumScopeController : public QObject
{
  public:
    explicit SpectrumScopeController(MainWindow* window);

    void buildSpectrumScope(QVBoxLayout* vbox);
    void updateSpectrumVfoMarker();
    void updateSpectrumScopeBandLimits(quint64 hz);
    void applySpectrumScopeSettings();
    quint64 roundFrequencyToStep(quint64 hz) const;
    void panSpectrumScopeToCenter(quint64 centerHz);
    quint64 clampSpectrumScopeCenterHz(quint64 hz, double bandwidthMhz) const;
    quint64 clampFrequencyHzToActiveBand(quint64 hz) const;
    void scheduleSpectrumScopeTune(quint64 hz);
    void onSpectrumReady(const QVector<float>& levels, double start, double end, bool outOfRange);
    void onSpectrumClicked(double freqMhz);
    void onWheelStepRequested(int steps);
    void updateTuningStepSelector();

  private:
    quint64 activeVfoFrequencyHz() const;
    void onActiveVfoFrequencyChanged(Vfo vfo, quint64 hz);
    void recenterActiveVfo(bool clearDisplay);
    void tuneActiveVfo(quint64 hz);
    void scheduleSpectrumScopeTune(quint64 hz, bool snapToTuningStep, bool commitImmediately, bool clearStaleDisplay);

    MainWindow* m_window{nullptr};
    QComboBox* m_tuningStepSelector{nullptr};
    quint64 m_lastSpectrumScopeLimitStartHz{0};
    quint64 m_lastSpectrumScopeLimitEndHz{0};
    quint64 m_pendingMainRecenterHz{0};
    quint64 m_pendingSubRecenterHz{0};
    bool m_hasLastSpectrumScopeLimits{false};
    bool m_hasCenteredActiveVfo{false};
};
