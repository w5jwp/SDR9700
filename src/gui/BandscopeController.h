#pragma once

#include <QObject>
#include <QVector>

class MainWindow;
class QVBoxLayout;

class BandscopeController : public QObject
{
  public:
    explicit BandscopeController(MainWindow* window);

    void buildBandscope(QVBoxLayout* vbox);
    void updateSpectrumVfoMarker();
    void updateBandscopeBandLimits(quint64 hz);
    void applyBandscopeSettings();
    quint64 roundFrequencyToStep(quint64 hz) const;
    void panBandscopeToCenter(quint64 centerHz);
    quint64 clampBandscopeCenterHz(quint64 hz, double bandwidthMhz) const;
    quint64 clampFrequencyHzToActiveBand(quint64 hz) const;
    void scheduleBandscopeTune(quint64 hz);
    void onSpectrumReady(const QVector<float>& levels, double start, double end, bool outOfRange);
    void onSpectrumClicked(double freqMhz);
    void onWheelStepRequested(int steps);

  private:
    MainWindow* m_window{nullptr};
};
