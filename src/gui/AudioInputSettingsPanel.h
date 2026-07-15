#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QProgressBar;

class AudioInputSettingsPanel : public QWidget
{
    Q_OBJECT

  public:
    explicit AudioInputSettingsPanel(QWidget* parent = nullptr);
    void setTransmitAudioLevel(int peak, int rms);

  private:
    QComboBox* m_inputCombo{nullptr};
    QProgressBar* m_txAverageMeter{nullptr};
    QProgressBar* m_txPeakMeter{nullptr};
    QLabel* m_txLevelStatus{nullptr};
    QLabel* m_txAverageValue{nullptr};
    QLabel* m_txPeakValue{nullptr};
};
