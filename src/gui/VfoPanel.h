#pragma once

#include <QGroupBox>
#include <QPoint>

class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;
class QWidget;
class QColor;

class VfoPanel : public QGroupBox
{
    Q_OBJECT

  public:
    explicit VfoPanel(const QString& title, QWidget* parent = nullptr);

    void setControlsEnabled(bool enabled);
    void setMeterEnabled(bool enabled);
    void setStepText(const QString& text);
    QPoint stepMenuPosition() const;
    void setSMeterValue(int value);
    void setTransmitPowerMode(bool on);
    void setTransmitPowerMeter(double watts);
    void setLanMod(int value);
    QWidget* lanModControl() const { return m_lanModControl; }

  signals:
    void stepClicked();
    void lanModChanged(int value);

  private:
    QPushButton* makeSelectorButton(const QString& primary, const QString& secondary, const QString& name,
                                    const QString& description);
    QWidget* makeSliderRow(const QString& labelText, int value, QSlider** sliderOut, QLabel** valueLabelOut);
    void setSliderValue(QSlider* slider, QLabel* valueLabel, int value);
    void updateSliderValueLabel(QLabel* label, int value);

    QPushButton* m_stepButton{nullptr};
    QProgressBar* m_signalMeter{nullptr};
    QWidget* m_signalScale{nullptr};
    QSlider* m_lanModSlider{nullptr};
    QWidget* m_lanModControl{nullptr};
    QLabel* m_lanModValueLabel{nullptr};
    bool m_meterEnabled{false};
};
