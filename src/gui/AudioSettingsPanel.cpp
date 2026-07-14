#include "AudioSettingsPanel.h"

#include "AppSettings.h"
#include "SettingsPanelStyle.h"

#include <QAudioDevice>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaDevices>
#include <QSlider>
#include <QVBoxLayout>

AudioSettingsPanel::AudioSettingsPanel(QWidget* parent) : QWidget(parent)
{
    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(12, 12, 12, 12);
    vbox->setSpacing(12);

    auto* group = new QGroupBox("Audio Devices", this);
    group->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* form = new QFormLayout(group);
    form->setSpacing(8);

    m_inputCombo = new QComboBox(group);
    m_outputCombo = new QComboBox(group);
    m_outputChannelsCombo = new QComboBox(group);
    m_outputChannelsCombo->addItem(QStringLiteral("LPCM 16-bit, 1 channel"), 1);
    m_outputChannelsCombo->addItem(QStringLiteral("LPCM 16-bit, 2 channels"), 2);

    for (const QAudioDevice& dev : QMediaDevices::audioInputs())
    {
        m_inputCombo->addItem(dev.description(), dev.id());
    }

    for (const QAudioDevice& dev : QMediaDevices::audioOutputs())
    {
        m_outputCombo->addItem(dev.description(), dev.id());
    }

    const AppSettings& settings = AppSettings::instance();
    const QByteArray savedIn = settings.value("AudioInputDeviceId").toString().toUtf8();
    const QByteArray savedOut = settings.value("AudioOutputDeviceId").toString().toUtf8();
    const int savedOutputChannels = qBound(1, settings.value("AudioOutputChannels", 2).toInt(), 2);

    if (!savedIn.isEmpty())
    {
        const int idx = m_inputCombo->findData(savedIn);
        if (idx >= 0)
        {
            m_inputCombo->setCurrentIndex(idx);
        }
    }
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

    form->addRow("Input device:", m_inputCombo);
    form->addRow("Output device:", m_outputCombo);
    vbox->addWidget(group);

    auto* localAudioGroup = new QGroupBox("Local Audio", this);
    localAudioGroup->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* localAudioForm = new QFormLayout(localAudioGroup);
    localAudioForm->setSpacing(8);
    localAudioForm->addRow("Output Codec:", m_outputChannelsCombo);
    vbox->addWidget(localAudioGroup);

    auto* radioAudioGroup = new QGroupBox("Radio Audio", this);
    radioAudioGroup->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* radioAudioLayout = new QVBoxLayout(radioAudioGroup);
    radioAudioLayout->setSpacing(6);

    auto* lanRow = new QHBoxLayout;
    auto* lanLabel = new QLabel("LAN MOD level", radioAudioGroup);
    m_lanModLevelSlider = new QSlider(Qt::Horizontal, radioAudioGroup);
    m_lanModLevelSlider->setRange(0, 255);
    m_lanModLevelSlider->setValue(AppSettings::instance().value("LanModLevel", 128).toInt());
    m_lanModLevelSlider->setMinimumWidth(220);
    m_lanModLevelValue = new QLabel(QString("%1%").arg(m_lanModLevelSlider->value() * 100 / 255), radioAudioGroup);
    m_lanModLevelValue->setMinimumWidth(38);
    m_lanModLevelValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lanRow->addWidget(lanLabel);
    lanRow->addWidget(m_lanModLevelSlider, 1);
    lanRow->addWidget(m_lanModLevelValue);
    radioAudioLayout->addLayout(lanRow);

    auto* lanNote = new QLabel("This is the audio level sent from SDR9700 to the IC-9700. Mic gain then controls "
                               "the level of amplification to the transmitter.",
                               radioAudioGroup);
    lanNote->setWordWrap(true);
    lanNote->setStyleSheet("QLabel { color: palette(mid); }");
    radioAudioLayout->addWidget(lanNote);

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

    connect(m_lanModLevelSlider, &QSlider::valueChanged, this,
            [this](int value)
            {
                AppSettings::instance().setValue("LanModLevel", value);
                if (m_lanModLevelValue)
                {
                    m_lanModLevelValue->setText(QString("%1%").arg(value * 100 / 255));
                }
                Q_EMIT lanModLevelChanged(value);
            });
    vbox->addWidget(radioAudioGroup);
}
