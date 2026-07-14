#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QSlider;

class AudioSettingsPanel : public QWidget
{
    Q_OBJECT

  public:
    explicit AudioSettingsPanel(QWidget* parent = nullptr);

  signals:
    void lanModLevelChanged(int level);

  private:
    QSlider* m_lanModLevelSlider{nullptr};
    QLabel* m_lanModLevelValue{nullptr};
    QComboBox* m_inputCombo{nullptr};
    QComboBox* m_outputCombo{nullptr};
    QComboBox* m_outputChannelsCombo{nullptr};
};
