#pragma once

#include <QAudioDevice>
#include <QList>
#include <QWidget>

class QComboBox;
class QMediaDevices;

class AudioDevicesSettingsPanel : public QWidget
{
    Q_OBJECT

  public:
    explicit AudioDevicesSettingsPanel(QWidget* parent = nullptr);

  private:
    void refreshInputDevices();
    void refreshOutputDevices();
    static void repopulateDeviceCombo(QComboBox* combo, const QList<QAudioDevice>& devices,
                                      const QByteArray& preferredID);

    QComboBox* m_inputCombo{nullptr};
    QComboBox* m_outputCombo{nullptr};
    QComboBox* m_outputChannelsCombo{nullptr};
    QMediaDevices* m_mediaDevices{nullptr};
};
