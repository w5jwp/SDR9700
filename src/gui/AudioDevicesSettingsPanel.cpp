#include "AudioDevicesSettingsPanel.h"

#include "AppSettings.h"
#include "SettingsPanelStyle.h"

#include <QAudioDevice>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QMediaDevices>
#include <QVBoxLayout>

AudioDevicesSettingsPanel::AudioDevicesSettingsPanel(QWidget* parent) : QWidget(parent)
{
    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(12, 4, 12, 0);
    vbox->setSpacing(8);

    auto* inputGroup = new QGroupBox("Input (Computer to Radio)", this);
    inputGroup->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* inputForm = new QFormLayout(inputGroup);
    inputForm->setSpacing(8);

    m_inputCombo = new QComboBox(inputGroup);
    for (const QAudioDevice& dev : QMediaDevices::audioInputs())
    {
        m_inputCombo->addItem(dev.description(), dev.id());
    }

    const AppSettings& settings = AppSettings::instance();
    const QByteArray savedIn = settings.value("AudioInputDeviceId").toString().toUtf8();
    if (!savedIn.isEmpty())
    {
        const int idx = m_inputCombo->findData(savedIn);
        if (idx >= 0)
        {
            m_inputCombo->setCurrentIndex(idx);
        }
    }

    inputForm->addRow("Device:", m_inputCombo);
    vbox->addWidget(inputGroup);

    auto* outputGroup = new QGroupBox("Output (Radio to Computer)", this);
    outputGroup->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* outputForm = new QFormLayout(outputGroup);
    outputForm->setSpacing(8);

    m_outputCombo = new QComboBox(outputGroup);
    for (const QAudioDevice& dev : QMediaDevices::audioOutputs())
    {
        m_outputCombo->addItem(dev.description(), dev.id());
    }

    m_outputChannelsCombo = new QComboBox(outputGroup);
    m_outputChannelsCombo->addItem(QStringLiteral("LPCM 16-bit, 1 channel"), 1);
    m_outputChannelsCombo->addItem(QStringLiteral("LPCM 16-bit, 2 channels"), 2);

    const QByteArray savedOut = settings.value("AudioOutputDeviceId").toString().toUtf8();
    const int savedOutputChannels = qBound(1, settings.value("AudioOutputChannels", 2).toInt(), 2);

    if (!savedOut.isEmpty())
    {
        const int idx = m_outputCombo->findData(savedOut);
        if (idx >= 0)
        {
            m_outputCombo->setCurrentIndex(idx);
        }
    }
    if (const int idx = m_outputChannelsCombo->findData(savedOutputChannels); idx >= 0)
    {
        m_outputChannelsCombo->setCurrentIndex(idx);
    }

    outputForm->addRow("Device:", m_outputCombo);
    outputForm->addRow("Codec:", m_outputChannelsCombo);
    vbox->addWidget(outputGroup);
    vbox->addStretch(1);

    connect(m_inputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
            { AppSettings::instance().setValue("AudioInputDeviceId", m_inputCombo->currentData().toByteArray()); });
    connect(m_outputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
            { AppSettings::instance().setValue("AudioOutputDeviceId", m_outputCombo->currentData().toByteArray()); });
    connect(m_outputChannelsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int)
            {
                AppSettings::instance().setValue("AudioOutputChannels",
                                                 qBound(1, m_outputChannelsCombo->currentData().toInt(), 2));
            });
}
