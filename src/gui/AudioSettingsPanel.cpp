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
#include <QProgressBar>
#include <QSlider>
#include <QVBoxLayout>

namespace
{
QString meterStyle(const char* color)
{
    return QStringLiteral("QProgressBar { background: #0f141a; border: 1px solid #3f4752; border-radius: 3px; }"
                          "QProgressBar::chunk { background: %1; border-radius: 2px; }")
        .arg(QString::fromLatin1(color));
}

QString statusStyle(const char* color)
{
    return QStringLiteral("QLabel { color: %1; font-weight: bold; }").arg(QString::fromLatin1(color));
}
} // namespace

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

    auto* radioAudioGroup = new QGroupBox("Transmit Audio", this);
    radioAudioGroup->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* radioAudioLayout = new QVBoxLayout(radioAudioGroup);
    radioAudioLayout->setSpacing(6);

    auto* lanRow = new QHBoxLayout;
    auto* lanLabel = new QLabel("LAN input level", radioAudioGroup);
    m_lanModLevelSlider = new QSlider(Qt::Horizontal, radioAudioGroup);
    m_lanModLevelSlider->setRange(0, 255);
    m_lanModLevelSlider->setValue(AppSettings::instance().value("LanModLevel", 128).toInt());
    m_lanModLevelSlider->setMinimumWidth(220);
    m_lanModLevelSlider->setToolTip(QStringLiteral("IC-9700 SET > Connectors > MOD Input > LAN MOD Level"));
    m_lanModLevelValue = new QLabel(QString("%1%").arg(m_lanModLevelSlider->value() * 100 / 255), radioAudioGroup);
    m_lanModLevelValue->setMinimumWidth(38);
    m_lanModLevelValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lanRow->addWidget(lanLabel);
    lanRow->addWidget(m_lanModLevelSlider, 1);
    lanRow->addWidget(m_lanModLevelValue);
    radioAudioLayout->addLayout(lanRow);

    auto* lanNote = new QLabel("Sets the IC-9700 LAN MOD Level used for SDR9700 transmit audio. Reduce this if the "
                               "radio ALC is high; raise it if clean transmit audio is too low.",
                               radioAudioGroup);
    lanNote->setWordWrap(true);
    lanNote->setStyleSheet("QLabel { color: palette(mid); }");
    radioAudioLayout->addWidget(lanNote);

    auto* txMeterGroup = new QGroupBox("Transmit Input Level", this);
    txMeterGroup->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* txMeterLayout = new QFormLayout(txMeterGroup);
    txMeterLayout->setSpacing(8);

    auto makeMeterRow = [txMeterGroup](QProgressBar** meterOut, QLabel** valueOut)
    {
        auto* row = new QWidget(txMeterGroup);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);

        auto* meter = new QProgressBar(row);
        meter->setRange(0, 100);
        meter->setValue(0);
        meter->setTextVisible(false);
        meter->setMinimumWidth(220);
        meter->setFixedHeight(14);
        meter->setStyleSheet(meterStyle("#4dd87a"));

        auto* value = new QLabel("--", row);
        value->setMinimumWidth(38);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value->setStyleSheet("QLabel { color: palette(mid); }");

        rowLayout->addWidget(meter, 1);
        rowLayout->addWidget(value);

        *meterOut = meter;
        *valueOut = value;
        return row;
    };

    txMeterLayout->addRow("Average level:", makeMeterRow(&m_txAverageMeter, &m_txAverageValue));
    txMeterLayout->addRow("Peak level:", makeMeterRow(&m_txPeakMeter, &m_txPeakValue));
    m_txLevelStatus = new QLabel("Inactive", txMeterGroup);
    m_txLevelStatus->setStyleSheet(statusStyle("#506070"));
    txMeterLayout->addRow("Status:", m_txLevelStatus);

    auto* txMeterNote = new QLabel("Use the audio device input level and LAN input level to keep transmit audio out of "
                                   "the high and clipping ranges.",
                                   txMeterGroup);
    txMeterNote->setWordWrap(true);
    txMeterNote->setStyleSheet("QLabel { color: palette(mid); }");
    txMeterLayout->addRow(txMeterNote);

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
    vbox->addWidget(txMeterGroup);
}

void AudioSettingsPanel::setTransmitAudioLevel(int peak, int rms)
{
    const int peakPct = qBound(0, qRound(qBound(0, peak, 255) * 100.0 / 255.0), 100);
    const int averagePct = qBound(0, qRound(qBound(0, rms, 255) * 100.0 / 255.0), 100);

    if (m_txAverageMeter)
    {
        m_txAverageMeter->setValue(averagePct);
    }
    if (m_txPeakMeter)
    {
        m_txPeakMeter->setValue(peakPct);
    }
    if (m_txAverageValue)
    {
        m_txAverageValue->setText(peakPct == 0 && averagePct == 0 ? QStringLiteral("--")
                                                                  : QStringLiteral("%1%").arg(averagePct));
    }
    if (m_txPeakValue)
    {
        m_txPeakValue->setText(peakPct == 0 && averagePct == 0 ? QStringLiteral("--")
                                                               : QStringLiteral("%1%").arg(peakPct));
    }

    QString status = QStringLiteral("Inactive");
    const char* color = "#506070";
    if (peakPct >= 95)
    {
        status = QStringLiteral("Clipping");
        color = "#ff4d4d";
    }
    else if (peakPct >= 85 || averagePct >= 60)
    {
        status = QStringLiteral("High");
        color = "#ffb84d";
    }
    else if (peakPct > 0 || averagePct > 0)
    {
        status = averagePct < 5 && peakPct < 20 ? QStringLiteral("Low") : QStringLiteral("Good");
        color = status == QStringLiteral("Low") ? "#8ea8c0" : "#4dd87a";
    }

    if (m_txLevelStatus)
    {
        m_txLevelStatus->setText(status);
        m_txLevelStatus->setStyleSheet(statusStyle(color));
        m_txLevelStatus->setToolTip(QStringLiteral("Average level %1%, peak level %2%.").arg(averagePct).arg(peakPct));
    }
    const QString meterCss = meterStyle(color);
    if (m_txAverageMeter)
    {
        m_txAverageMeter->setStyleSheet(meterCss);
    }
    if (m_txPeakMeter)
    {
        m_txPeakMeter->setStyleSheet(meterCss);
    }
}
