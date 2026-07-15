#pragma once

#include <QWidget>

class QComboBox;

class AudioOutputSettingsPanel : public QWidget
{
    Q_OBJECT

  public:
    explicit AudioOutputSettingsPanel(QWidget* parent = nullptr);

  private:
    QComboBox* m_outputCombo{nullptr};
    QComboBox* m_outputChannelsCombo{nullptr};
};
