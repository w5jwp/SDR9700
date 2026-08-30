#include "MemoryManagerSettingsPanel.h"

#include "AppSettings.h"
#include "MainWindowHelpers.h"
#include "SettingsPanelStyle.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace sdr9700::ui::main_window;

MemoryManagerSettingsPanel::MemoryManagerSettingsPanel(QWidget* parent) : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 4, 12, 0);
    root->setSpacing(8);

    auto* syncGroup = new QGroupBox(QStringLiteral("Radio Sync"), this);
    syncGroup->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* syncLayout = new QFormLayout(syncGroup);
    syncLayout->setContentsMargins(10, 12, 10, 10);
    syncLayout->setSpacing(8);

    auto* const pollIntervalSpin = new QSpinBox(syncGroup);
    pollIntervalSpin->setRange(kMemoryPollIntervalMinSeconds, kMemoryPollIntervalMaxSeconds);
    pollIntervalSpin->setSingleStep(30);
    pollIntervalSpin->setSuffix(QStringLiteral(" seconds"));
    pollIntervalSpin->setValue(
        AppSettings::instance()
            .value(QString::fromLatin1(kMemoryPollIntervalSecondsSettingsKey), kDefaultMemoryPollIntervalSeconds)
            .toInt());
    syncLayout->addRow(QStringLiteral("Poll interval:"), pollIntervalSpin);

    root->addWidget(syncGroup);
    root->addStretch(1);

    connect(pollIntervalSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [](int seconds)
            { AppSettings::instance().setValue(QString::fromLatin1(kMemoryPollIntervalSecondsSettingsKey), seconds); });
    connect(pollIntervalSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &MemoryManagerSettingsPanel::pollIntervalSecondsChanged);
}
