#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QProgressBar;
class QSlider;

class AudioSettingsPanel : public QWidget
{
    Q_OBJECT

  public:
    explicit AudioSettingsPanel(QWidget* parent = nullptr);
    void setTransmitAudioLevel(int peak, int rms);

  signals:
    void lanModLevelChanged(int level);

  private:
    QSlider* m_lanModLevelSlider{nullptr};
    QLabel* m_lanModLevelValue{nullptr};
    QProgressBar* m_txAverageMeter{nullptr};
    QProgressBar* m_txPeakMeter{nullptr};
    QLabel* m_txLevelStatus{nullptr};
    QLabel* m_txAverageValue{nullptr};
    QLabel* m_txPeakValue{nullptr};
    QComboBox* m_inputCombo{nullptr};
    QComboBox* m_outputCombo{nullptr};
    QComboBox* m_outputChannelsCombo{nullptr};
};
