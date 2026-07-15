#pragma once

#include <QGroupBox>
#include <QPoint>

class QLabel;
class QLineEdit;
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

    QString frequencyText() const;
    bool frequencyHasFocus() const;
    void clearFrequencyFocus();
    void setFrequencyText(const QString& text);
    void setFrequencyReadOnly(bool readOnly);
    void setMemoryName(const QString& text, const QString& tooltip);
    void setBandText(const QString& text);
    void setModeText(const QString& text);
    void setControlsEnabled(bool enabled);
    void setMeterEnabled(bool enabled);
    void setStepText(const QString& text);
    QPoint stepMenuPosition() const;
    void setSMeterValue(int value);
    void setAlcMode(bool on);
    void setAlc(double alc);
    void setTxPower(int value);
    void setVolume(int value);
    void setVolumeVisible(bool visible);
    void setSquelch(int value);
    int volume() const;
    QPoint bandMenuPosition() const;
    QPoint modeMenuPosition() const;

  signals:
    void frequencyReturnPressed();
    void bandClicked();
    void modeClicked();
    void stepClicked();
    void txPowerChanged(int value);
    void volumeChanged(int value);
    void squelchChanged(int value);

  private:
    QPushButton* makeSelectorButton(const QString& primary, const QString& secondary, const QString& name,
                                    const QString& description);
    QWidget* makeSliderRow(const QString& labelText, int value, QSlider** sliderOut, QLabel** valueLabelOut);
    void setSliderValue(QSlider* slider, QLabel* valueLabel, int value);
    void updateSliderValueLabel(QLabel* label, int value);

    QLineEdit* m_frequencyEdit{nullptr};
    QLineEdit* m_memoryNameLabel{nullptr};
    QPushButton* m_bandButton{nullptr};
    QPushButton* m_modeButton{nullptr};
    QPushButton* m_stepButton{nullptr};
    QProgressBar* m_signalMeter{nullptr};
    QWidget* m_signalScale{nullptr};
    QWidget* m_volumeRow{nullptr};
    QSlider* m_txPowerSlider{nullptr};
    QSlider* m_volumeSlider{nullptr};
    QSlider* m_squelchSlider{nullptr};
    QLabel* m_txPowerValueLabel{nullptr};
    QLabel* m_volumeValueLabel{nullptr};
    QLabel* m_squelchValueLabel{nullptr};
    bool m_meterEnabled{false};
};
