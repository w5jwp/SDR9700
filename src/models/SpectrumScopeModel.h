// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QObject>
#include <QVector>

class SpectrumScopeModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(double bandwidthMhz READ bandwidthMhz NOTIFY rangeChanged)

  public:
    explicit SpectrumScopeModel(QObject* parent = nullptr);

    double bandwidthMhz() const { return m_bandwidthMhz; }
    double startMhz() const { return m_centerMhz - m_bandwidthMhz / 2.0; }
    double endMhz() const { return m_centerMhz + m_bandwidthMhz / 2.0; }
    double sourceStartMhz() const { return m_sourceCenterMhz - m_sourceBandwidthMhz / 2.0; }
    double sourceEndMhz() const { return m_sourceCenterMhz + m_sourceBandwidthMhz / 2.0; }

    void centerOnFrequency(double freqMhz);
    void setFrequencyLimits(double lowerMhz, double upperMhz);
    void clearFrequencyLimits();
    void holdDisplayCenter(double displayCenterMhz, double expectedSourceCenterMhz);
    void clearDisplayCenterHold();

    void ingestSpectrum(const QVector<float>& levels, double lowerMhz, double upperMhz, bool outOfRange);

  signals:
    void rangeChanged(double centerMhz, double bandwidthMhz);
    void spectrumReady(const QVector<float>& levels, double startMhz, double endMhz, bool outOfRange);

  private:
    double constrainedBandwidth(double requestedBandwidthMhz) const;
    double constrainedCenter(double requestedCenterMhz, double requestedBandwidthMhz) const;
    void constrainDisplayRange();

    double m_centerMhz{145.0};
    double m_bandwidthMhz{1.0};
    double m_sourceCenterMhz{145.0};
    double m_sourceBandwidthMhz{1.0};
    double m_limitStartMhz{0.0};
    double m_limitEndMhz{0.0};
    double m_heldCenterMhz{0.0};
    double m_heldSourceCenterMhz{0.0};
    bool m_hasFrequencyLimits{false};
    bool m_hasDisplayCenterHold{false};
};
