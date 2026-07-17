#pragma once

#include <QObject>
#include <QVector>

class MainWindow;
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

  private:
    MainWindow* m_window{nullptr};
};
