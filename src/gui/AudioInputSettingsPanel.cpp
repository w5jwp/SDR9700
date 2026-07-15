#include "AudioInputSettingsPanel.h"

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

AudioInputSettingsPanel::AudioInputSettingsPanel(QWidget* parent) : QWidget(parent)
{
    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(12, 4, 12, 0);
    vbox->setSpacing(8);

    auto* inputGroup = new QGroupBox("Audio Input", this);
    inputGroup->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* inputForm = new QFormLayout(inputGroup);
    inputForm->setSpacing(8);

    m_inputCombo = new QComboBox(inputGroup);
    for (const QAudioDevice& dev : QMediaDevices::audioInputs())
    {
        m_inputCombo->addItem(dev.description(), dev.id());
    }

    const QByteArray savedIn = AppSettings::instance().value("AudioInputDeviceId").toString().toUtf8();
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

    auto* txMeterGroup = new QGroupBox("Audio Level", this);
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

    auto* txMeterNote = new QLabel(
        "These meters show local microphone input before the radio LAN input level is applied.", txMeterGroup);
    txMeterNote->setWordWrap(true);
    txMeterNote->setStyleSheet("QLabel { color: palette(mid); }");
    txMeterLayout->addRow(txMeterNote);
    vbox->addWidget(txMeterGroup);
    vbox->addStretch(1);

    connect(m_inputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
            { AppSettings::instance().setValue("AudioInputDeviceId", m_inputCombo->currentData().toByteArray()); });
}

void AudioInputSettingsPanel::setTransmitAudioLevel(int peak, int rms)
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
    }
}
