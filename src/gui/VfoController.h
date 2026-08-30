#pragma once

#include "Vfo.h"
#include "radio/RadioCapabilities.h"

#include <QObject>
#include <QTimer>
#include <array>
#include <optional>

class IRadioBackend;
class VfoDisplay;
class QWidget;

class VfoController : public QObject
{
    Q_OBJECT

  public:
    explicit VfoController(Vfo vfo, IRadioBackend* backend, QWidget* displayParent, QObject* parent = nullptr);

    Vfo vfo() const { return m_vfo; }
    VfoDisplay* display() const { return m_display; }
    void setFrequencyHz(quint64 hz);
    void clearFrequency();
    void setOperatingEnabled(bool enabled);
    void setUserInteractionEnabled(bool enabled);
    void setSelected(bool selected);
    void setTransmitting(bool transmitting);
    void captureExchangeableControlState();
    void discardCapturedExchangeableControlState();
    void applyCapturedControlExchange(VfoController* other);
    availableBands band() const { return m_band; }
    quint64 frequencyHz() const { return m_confirmedFrequencyHz.value_or(0); }
    bool hasPublishedState() const { return stateReady() && (!m_backend || m_initialStatePublished); }
    void selectBand(availableBands requestedBand);

  signals:
    void bandMenuRequested(Vfo vfo, const QPoint& position);
    void toneMenuRequested(Vfo vfo, const QPoint& position);
    void offsetMenuRequested(Vfo vfo, const QPoint& position);
    void compressorMenuRequested(Vfo vfo, const QPoint& position);
    void selectionRequested(Vfo vfo);
    void frequencyChanged(quint64 hz);
    void frequencyRecenterRequested(Vfo vfo, quint64 hz);
    void statePublished(Vfo vfo);

  private:
    struct ExchangeableControlState
    {
        int agcMode{0};
        bool attenuatorEnabled{false};
        bool nbEnabled{false};
        bool autoNotchEnabled{false};
        bool manualNotchEnabled{false};
        bool nrEnabled{false};
        int preampLevel{0};
        int squelch{0};
        duplexMode_t duplexMode{dmSimplex};
        quint64 repeaterOffsetHz{0};
        rptAccessTxRx_t toneAccessMode{ratrNN};
        ushort toneFrequency{670};
        ushort dtcsCode{23};
    };

    void showReceiverControlMenu(const QString& control);
    void showModeMenu();
    bool stateReady() const;
    void publishConfirmedState();
    void updateDisplayEnabled();
    void updateReceiverControlDisplay();
    void applyExchangeableControlState(const ExchangeableControlState& state);

    const Vfo m_vfo;
    IRadioBackend* m_backend{nullptr};
    VfoDisplay* m_display{nullptr};
    std::optional<quint64> m_confirmedFrequencyHz;
    std::optional<quint64> m_publishedFrequencyHz;
    availableBands m_band{bandUnknown};
    std::array<quint64, std::size(sdr9700::kRadioUiBandOrder)> m_lastBandFrequencyHz{};
    int m_agcMode{0};
    bool m_attenuatorEnabled{false};
    bool m_nbEnabled{false};
    bool m_autoNotchEnabled{false};
    bool m_manualNotchEnabled{false};
    bool m_nrEnabled{false};
    int m_preampLevel{0};
    int m_rfGain{0};
    int m_squelch{0};
    int m_txPower{0};
    duplexMode_t m_duplexMode{dmSimplex};
    quint64 m_repeaterOffsetHz{0};
    rptAccessTxRx_t m_toneAccessMode{ratrNN};
    ushort m_toneFrequency{670};
    ushort m_dtcsCode{23};
    bool m_xfcEnabled{false};
    bool m_compressorEnabled{false};
    QString m_mode;
    bool m_operatingEnabled{true};
    bool m_userInteractionEnabled{false};
    bool m_initialStatePublished{false};
    QTimer m_initialPublishTimer;
    std::optional<ExchangeableControlState> m_capturedExchangeState;
};
