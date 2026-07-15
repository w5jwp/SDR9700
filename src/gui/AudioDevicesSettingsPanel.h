#pragma once

#include <QWidget>

class QComboBox;

class AudioDevicesSettingsPanel : public QWidget
{
    Q_OBJECT

  public:
    explicit AudioDevicesSettingsPanel(QWidget* parent = nullptr);

  private:
    QComboBox* m_inputCombo{nullptr};
    QComboBox* m_outputCombo{nullptr};
    QComboBox* m_outputChannelsCombo{nullptr};
};
