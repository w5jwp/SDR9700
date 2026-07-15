#include "MouseSettingsPanel.h"

#include "AppSettings.h"
#include "SettingsPanelStyle.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

namespace
{
constexpr auto kReverseMouseWheelTuningSettingsKey = "ReverseMouseWheelTuning";
}

MouseSettingsPanel::MouseSettingsPanel(QWidget* parent) : QWidget(parent)
{
    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(12, 4, 12, 0);
    vbox->setSpacing(8);

    auto* group = new QGroupBox("Mouse Wheel", this);
    group->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(10, 12, 10, 10);
    layout->setSpacing(6);

    m_invertBandscopeMouseWheelCheck = new QCheckBox("Reverse bandscope wheel pan direction", group);
    m_invertBandscopeMouseWheelCheck->setChecked(
        AppSettings::instance().value(QString::fromLatin1(kReverseMouseWheelTuningSettingsKey), "False").toBool());
    layout->addWidget(m_invertBandscopeMouseWheelCheck);

    auto* note = new QLabel("When enabled, physical wheel up pans the bandscope down in frequency and physical wheel "
                            "down pans it up in frequency.",
                            group);
    note->setWordWrap(true);
    note->setStyleSheet("QLabel { color: palette(mid); }");
    layout->addWidget(note);

    connect(m_invertBandscopeMouseWheelCheck, &QCheckBox::toggled, this,
            [this](bool checked)
            {
                AppSettings::instance().setValue(QString::fromLatin1(kReverseMouseWheelTuningSettingsKey), checked);
                Q_EMIT reverseMouseWheelTuningChanged(checked);
            });

    vbox->addWidget(group);
    vbox->addStretch(1);
}
