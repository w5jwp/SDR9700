#pragma once

#include "Vfo.h"
#include "radio/RadioCapabilities.h"

#include <QObject>
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
    void setSelected(bool selected);
    void setTransmitting(bool transmitting);
    void captureExchangeableControlState();
    void applyCapturedControlExchange(VfoController* other);
    availableBands band() const { return m_band; }
    quint64 frequencyHz() const { return m_confirmedFrequencyHz.value_or(0); }
    void selectBand(availableBands requestedBand);

  signals:
    void bandMenuRequested(Vfo vfo, const QPoint& position);
    void selectionRequested(Vfo vfo);
    void frequencyChanged(quint64 hz);

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
    };

    void showReceiverControlMenu(const QString& control);
    void showModeMenu();
    void updateReceiverControlDisplay();
    void applyExchangeableControlState(const ExchangeableControlState& state);

    const Vfo m_vfo;
    IRadioBackend* m_backend{nullptr};
    VfoDisplay* m_display{nullptr};
    std::optional<quint64> m_confirmedFrequencyHz;
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
    QString m_mode;
    std::optional<ExchangeableControlState> m_capturedExchangeState;
};
