#pragma once

#include "Vfo.h"

#include <QWidget>
#include <QHash>
#include <optional>

class QLineEdit;
class QLabel;
class QPushButton;
class QTimer;
class VfoSMeter;

class VfoDisplay : public QWidget
{
    Q_OBJECT

  public:
    explicit VfoDisplay(Vfo vfo, QWidget* parent = nullptr);

    Vfo vfo() const { return m_vfo; }
    QString frequencyText() const;
    void setFrequencyHz(quint64 hz);
    void setTransmitFrequencyHz(quint64 hz);
    void clearTransmitFrequency();
    void clearFrequency();
    void setOperatingEnabled(bool enabled);
    void setTuningEnabled(bool enabled);
    void setBandText(const QString& text);
    void setModeText(const QString& text);
    void setSMeterValue(int rawValue);
    void setTransmitPowerMode(bool enabled);
    void setTransmitPowerWatts(double watts);
    void setTransmitSwr(double swr);
    void setMaxTransmitPowerWatts(double watts);
    void setSelected(bool selected);
    void setTransmitting(bool transmitting);
    void setReceiverControlState(const QString& control, const QString& value, bool active);
    void setReceiverControlToolTip(const QString& control, const QString& toolTip);
    QPoint bandMenuPosition() const;
    QPoint modeMenuPosition() const;
    QPoint receiverControlMenuPosition(const QString& control) const;

  signals:
    void frequencySubmitted(const QString& text);
    void vfoClicked();
    void bandClicked();
    void modeClicked();
    void receiverControlClicked(const QString& control);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

  private:
    void finishFrequencyEditing();

    const Vfo m_vfo;
    QLabel* m_txBadge{nullptr};
    QPushButton* m_identityButton{nullptr};
    QPushButton* m_bandButton{nullptr};
    QPushButton* m_modeButton{nullptr};
    QLineEdit* m_frequencyEdit{nullptr};
    QTimer* m_frequencyEditTimer{nullptr};
    QLabel* m_transmitFrequencyLabel{nullptr};
    VfoSMeter* m_sMeter{nullptr};
    QHash<QString, QPushButton*> m_receiverControlButtons;
    bool m_operatingEnabled{true};
    bool m_tuningEnabled{true};
    bool m_transmitting{false};
    bool m_transmitSwrValid{false};
    double m_transmitSwr{1.0};
    bool m_frequencyEditing{false};
    bool m_deferredFrequencyClear{false};
    std::optional<quint64> m_deferredFrequencyHz;
};
