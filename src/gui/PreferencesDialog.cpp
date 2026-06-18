#include "PreferencesDialog.h"
#include "AppSettings.h"
#include "FramelessTitleBar.h"
#include "RadioProfileDialog.h"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QFormLayout>
#include <QSlider>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QCheckBox>

namespace
{
constexpr auto kReverseMouseWheelTuningSettingsKey = "ReverseMouseWheelTuning";
} // namespace

PreferencesDialog::PreferencesDialog(QWidget* parent) : PreferencesDialog(Page::RadioSetup, parent) {}

PreferencesDialog::PreferencesDialog(Page page, QWidget* parent) : QDialog(parent)
{
    QWidget* content = nullptr;
    QString title;
    switch (page)
    {
    case Page::RadioSetup:
        title = QStringLiteral("Radio Setup");
        setMinimumSize(720, 480);
        content = buildRadioConnectionsTab();
        break;
    case Page::Audio:
        title = QStringLiteral("Audio");
        setMinimumSize(620, 420);
        content = buildAudioTab();
        break;
    case Page::Application:
        title = QStringLiteral("Application");
        setMinimumSize(520, 300);
        content = buildSoftwareTab();
        break;
    }
    setWindowTitle(title);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setStyleSheet(QStringLiteral("PreferencesDialog { background: %1; border: 1px solid %2; }")
                      .arg(QLatin1String(UiTheme::Color::Panel), QLatin1String(UiTheme::Color::Border)));

    auto* titleBar = new FramelessTitleBar(title, this);
    connect(titleBar->closeButton(), &QPushButton::clicked, this, &QDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(titleBar);
    root->addWidget(content, 1);
}

QWidget* PreferencesDialog::buildRadioConnectionsTab()
{
    auto* outer = new QWidget;
    auto* vbox = new QVBoxLayout(outer);
    vbox->setContentsMargins(12, 12, 12, 12);
    vbox->setSpacing(10);

    vbox->addWidget(new RadioProfileWidget(outer), 1);

    return outer;
}

QWidget* PreferencesDialog::buildAudioTab()
{
    auto* outer = new QWidget;
    auto* vbox = new QVBoxLayout(outer);
    vbox->setContentsMargins(12, 12, 12, 12);
    vbox->setSpacing(12);

    auto* group = new QGroupBox("Audio Devices");
    auto* form = new QFormLayout(group);
    form->setSpacing(8);

    m_inputCombo = new QComboBox;
    m_outputCombo = new QComboBox;
    m_outputChannelsCombo = new QComboBox;
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
        int idx = m_inputCombo->findData(savedIn);
        if (idx >= 0)
        {
            m_inputCombo->setCurrentIndex(idx);
        }
    }
    if (!savedOut.isEmpty())
    {
        int idx = m_outputCombo->findData(savedOut);
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

    auto* localAudioGroup = new QGroupBox("Local Audio");
    auto* localAudioForm = new QFormLayout(localAudioGroup);
    localAudioForm->setSpacing(8);
    localAudioForm->addRow("Output Codec:", m_outputChannelsCombo);
    vbox->addWidget(localAudioGroup);

    auto* radioAudioGroup = new QGroupBox("Radio Audio");
    auto* radioAudioLayout = new QVBoxLayout(radioAudioGroup);
    radioAudioLayout->setSpacing(6);

    auto* lanRow = new QHBoxLayout;
    auto* lanLabel = new QLabel("LAN MOD level");
    m_lanModLevelSlider = new QSlider(Qt::Horizontal);
    m_lanModLevelSlider->setRange(0, 255);
    m_lanModLevelSlider->setValue(AppSettings::instance().value("LanModLevel", 128).toInt());
    m_lanModLevelSlider->setMinimumWidth(220);
    m_lanModLevelValue = new QLabel(QString("%1%").arg(m_lanModLevelSlider->value() * 100 / 255));
    m_lanModLevelValue->setMinimumWidth(38);
    m_lanModLevelValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lanRow->addWidget(lanLabel);
    lanRow->addWidget(m_lanModLevelSlider, 1);
    lanRow->addWidget(m_lanModLevelValue);
    radioAudioLayout->addLayout(lanRow);

    auto* lanNote = new QLabel("This is the audio level sent from SDR9700 to the IC-9700. Mic gain then controls "
                               "the level of amplification to the transmitter.");
    lanNote->setWordWrap(true);
    lanNote->setStyleSheet("QLabel { color: palette(mid); }");
    radioAudioLayout->addWidget(lanNote);

    connect(m_lanModLevelSlider, &QSlider::valueChanged, this,
            [this](int value)
            {
                AppSettings::instance().setValue("LanModLevel", value);
                if (m_lanModLevelValue)
                {
                    m_lanModLevelValue->setText(QString("%1%").arg(value * 100 / 255));
                }
            });
    vbox->addWidget(radioAudioGroup);

    auto* saveBtn = new QPushButton("Save");
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    btnRow->addWidget(saveBtn);
    vbox->addLayout(btnRow);
    connect(saveBtn, &QPushButton::clicked, this, &PreferencesDialog::saveAudioSettings);

    vbox->addStretch(1);
    return outer;
}

QWidget* PreferencesDialog::buildSoftwareTab()
{
    auto* outer = new QWidget;
    auto* vbox = new QVBoxLayout(outer);
    vbox->setContentsMargins(12, 12, 12, 12);
    vbox->setSpacing(12);

    auto* group = new QGroupBox("Mouse wheel");
    auto* layout = new QVBoxLayout(group);
    layout->setSpacing(8);

    m_invertPanadapterMouseWheelCheck = new QCheckBox("Reverse mouse-wheel tuning direction");
    m_invertPanadapterMouseWheelCheck->setChecked(
        AppSettings::instance().value(QString::fromLatin1(kReverseMouseWheelTuningSettingsKey), "False").toBool());
    layout->addWidget(m_invertPanadapterMouseWheelCheck);

    auto* note = new QLabel("When enabled, physical wheel up tunes the frequency down and physical wheel down tunes "
                            "the frequency up. Use this when the panadapter or VFO tuning feels reversed.");
    note->setWordWrap(true);
    note->setStyleSheet("QLabel { color: palette(mid); }");
    layout->addWidget(note);

    connect(m_invertPanadapterMouseWheelCheck, &QCheckBox::toggled, this, [](bool checked)
            { AppSettings::instance().setValue(QString::fromLatin1(kReverseMouseWheelTuningSettingsKey), checked); });

    vbox->addWidget(group);
    vbox->addStretch(1);
    return outer;
}

void PreferencesDialog::saveAudioSettings()
{
    AppSettings& settings = AppSettings::instance();
    settings.setValue("AudioInputDeviceId", m_inputCombo->currentData().toByteArray());
    settings.setValue("AudioOutputDeviceId", m_outputCombo->currentData().toByteArray());
    settings.setValue("AudioOutputChannels", qBound(1, m_outputChannelsCombo->currentData().toInt(), 2));
}
