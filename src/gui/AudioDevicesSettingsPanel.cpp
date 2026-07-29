#include "AudioDevicesSettingsPanel.h"

#include "AppSettings.h"
#include "SettingsPanelStyle.h"

#include <QAudioDevice>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QMediaDevices>
#include <QSignalBlocker>
#include <QVBoxLayout>

AudioDevicesSettingsPanel::AudioDevicesSettingsPanel(QWidget* parent) : QWidget(parent)
{
    m_mediaDevices = new QMediaDevices(this);
    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(12, 4, 12, 0);
    vbox->setSpacing(8);

    auto* inputGroup = new QGroupBox("Input (Computer to Radio)", this);
    inputGroup->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* inputForm = new QFormLayout(inputGroup);
    inputForm->setSpacing(8);

    m_inputCombo = new QComboBox(inputGroup);
    const AppSettings& settings = AppSettings::instance();
    const QByteArray savedIn = settings.value("audioInputDeviceID").toString().toUtf8();
    repopulateDeviceCombo(m_inputCombo, QMediaDevices::audioInputs(), savedIn);

    inputForm->addRow("Device:", m_inputCombo);
    vbox->addWidget(inputGroup);

    auto* outputGroup = new QGroupBox("Output (Radio to Computer)", this);
    outputGroup->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* outputForm = new QFormLayout(outputGroup);
    outputForm->setSpacing(8);

    m_outputCombo = new QComboBox(outputGroup);

    m_outputChannelsCombo = new QComboBox(outputGroup);
    m_outputChannelsCombo->addItem(QStringLiteral("LPCM 16-bit, 1 channel"), 1);
    m_outputChannelsCombo->addItem(QStringLiteral("LPCM 16-bit, 2 channels"), 2);

    const QByteArray savedOut = settings.value("audioOutputDeviceID").toString().toUtf8();
    const int savedOutputChannels = qBound(1, settings.value("audioOutputChannels", 2).toInt(), 2);
    repopulateDeviceCombo(m_outputCombo, QMediaDevices::audioOutputs(), savedOut);
    if (const int idx = m_outputChannelsCombo->findData(savedOutputChannels); idx >= 0)
    {
        m_outputChannelsCombo->setCurrentIndex(idx);
    }

    outputForm->addRow("Device:", m_outputCombo);
    outputForm->addRow("Codec:", m_outputChannelsCombo);
    vbox->addWidget(outputGroup);
    vbox->addStretch(1);

    connect(m_inputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
            { AppSettings::instance().setValue("audioInputDeviceID", m_inputCombo->currentData().toByteArray()); });
    connect(m_outputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
            { AppSettings::instance().setValue("audioOutputDeviceID", m_outputCombo->currentData().toByteArray()); });
    connect(m_outputChannelsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int)
            {
                AppSettings::instance().setValue("audioOutputChannels",
                                                 qBound(1, m_outputChannelsCombo->currentData().toInt(), 2));
            });
    connect(m_mediaDevices, &QMediaDevices::audioInputsChanged, this, &AudioDevicesSettingsPanel::refreshInputDevices);
    connect(m_mediaDevices, &QMediaDevices::audioOutputsChanged, this,
            &AudioDevicesSettingsPanel::refreshOutputDevices);
}

void AudioDevicesSettingsPanel::repopulateDeviceCombo(QComboBox* combo, const QList<QAudioDevice>& devices,
                                                      const QByteArray& preferredID)
{
    if (!combo)
    {
        return;
    }
    const QSignalBlocker blocker(combo);
    combo->clear();
    for (const QAudioDevice& device : devices)
    {
        combo->addItem(device.description(), device.id());
    }
    const int preferredIndex = combo->findData(preferredID);
    if (preferredIndex >= 0)
    {
        combo->setCurrentIndex(preferredIndex);
    }
}

void AudioDevicesSettingsPanel::refreshInputDevices()
{
    const QByteArray selectedID = m_inputCombo->currentData().toByteArray();
    repopulateDeviceCombo(m_inputCombo, QMediaDevices::audioInputs(), selectedID);
}

void AudioDevicesSettingsPanel::refreshOutputDevices()
{
    const QByteArray selectedID = m_outputCombo->currentData().toByteArray();
    repopulateDeviceCombo(m_outputCombo, QMediaDevices::audioOutputs(), selectedID);
}
