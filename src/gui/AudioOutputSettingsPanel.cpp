#include "AudioOutputSettingsPanel.h"

#include "AppSettings.h"
#include "SettingsPanelStyle.h"

#include <QAudioDevice>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QMediaDevices>
#include <QVBoxLayout>

AudioOutputSettingsPanel::AudioOutputSettingsPanel(QWidget* parent) : QWidget(parent)
{
    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(12, 4, 12, 0);
    vbox->setSpacing(8);

    auto* outputGroup = new QGroupBox("Audio Output", this);
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

    const AppSettings& settings = AppSettings::instance();
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

    outputForm->addRow("Output device:", m_outputCombo);
    outputForm->addRow("Output codec:", m_outputChannelsCombo);
    vbox->addWidget(outputGroup);
    vbox->addStretch(1);

    connect(m_outputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
            { AppSettings::instance().setValue("AudioOutputDeviceId", m_outputCombo->currentData().toByteArray()); });
    connect(m_outputChannelsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int)
            {
                AppSettings::instance().setValue("AudioOutputChannels",
                                                 qBound(1, m_outputChannelsCombo->currentData().toInt(), 2));
            });
}
