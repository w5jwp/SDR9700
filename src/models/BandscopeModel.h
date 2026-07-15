// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QObject>
#include <QVector>

class BandscopeModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(double centerMhz READ centerMhz NOTIFY rangeChanged)
    Q_PROPERTY(double bandwidthMhz READ bandwidthMhz NOTIFY rangeChanged)

  public:
    explicit BandscopeModel(QObject* parent = nullptr);

    double centerMhz() const { return m_centerMhz; }
    double bandwidthMhz() const { return m_bandwidthMhz; }
    double startMhz() const { return m_centerMhz - m_bandwidthMhz / 2.0; }
    double endMhz() const { return m_centerMhz + m_bandwidthMhz / 2.0; }
    double sourceStartMhz() const { return m_sourceCenterMhz - m_sourceBandwidthMhz / 2.0; }
    double sourceEndMhz() const { return m_sourceCenterMhz + m_sourceBandwidthMhz / 2.0; }
    bool isUserZoomed() const { return m_userZoomed; }

    void centerOnFrequency(double freqMhz);
    void zoomInAt(double focusMhz);
    void zoomOut();
    void setFrequencyLimits(double startMhz, double endMhz);
    void clearFrequencyLimits();
    void holdDisplayCenter(double centerMhz);
    void clearDisplayCenterHold();

    void ingestSpectrum(const QVector<float>& binsDbm, double startMhz, double endMhz, bool outOfRange);

  signals:
    void rangeChanged(double centerMhz, double bandwidthMhz);
    void spectrumReady(const QVector<float>& binsDbm, double startMhz, double endMhz, bool outOfRange);

  private:
    double constrainedBandwidth(double bandwidthMhz) const;
    double constrainedCenter(double centerMhz, double bandwidthMhz) const;
    void constrainDisplayRange();

    double m_centerMhz{145.0};
    double m_bandwidthMhz{1.0};
    double m_sourceCenterMhz{145.0};
    double m_sourceBandwidthMhz{1.0};
    double m_limitStartMhz{0.0};
    double m_limitEndMhz{0.0};
    double m_heldCenterMhz{0.0};
    bool m_hasFrequencyLimits{false};
    bool m_userZoomed{false};
    bool m_hasDisplayCenterHold{false};
};
