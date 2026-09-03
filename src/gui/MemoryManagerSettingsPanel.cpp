#include "MemoryManagerSettingsPanel.h"

#include "AppSettings.h"
#include "MainWindowHelpers.h"
#include "SettingsPanelStyle.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
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

    auto* const pollIntervalCombo = new QComboBox(syncGroup);
    pollIntervalCombo->setObjectName(QStringLiteral("memoryManagerPollInterval"));
    pollIntervalCombo->addItem(QStringLiteral("Off"), 0);
    pollIntervalCombo->addItem(QStringLiteral("5 Minutes"), 5 * 60);
    pollIntervalCombo->addItem(QStringLiteral("10 Minutes"), 10 * 60);
    pollIntervalCombo->addItem(QStringLiteral("15 Minutes"), 15 * 60);
    pollIntervalCombo->addItem(QStringLiteral("30 Minutes"), 30 * 60);
    pollIntervalCombo->addItem(QStringLiteral("60 Minutes"), 60 * 60);
    const int storedPollInterval =
        AppSettings::instance()
            .value(QString::fromLatin1(kMemoryPollIntervalSecondsSettingsKey), kDefaultMemoryPollIntervalSeconds)
            .toInt();
    const int storedIndex = pollIntervalCombo->findData(storedPollInterval);
    pollIntervalCombo->setCurrentIndex(
        storedIndex >= 0 ? storedIndex : pollIntervalCombo->findData(kDefaultMemoryPollIntervalSeconds));
    syncLayout->addRow(QStringLiteral("Poll interval:"), pollIntervalCombo);

    root->addWidget(syncGroup);

    auto* visibilityGroup = new QGroupBox(QStringLiteral("Memory Visibility"), this);
    visibilityGroup->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* visibilityLayout = new QVBoxLayout(visibilityGroup);
    visibilityLayout->setContentsMargins(10, 12, 10, 10);
    visibilityLayout->setSpacing(6);

    auto* const showSpecialCheck =
        new QCheckBox(QStringLiteral("Show special memories in Memory Manager"), visibilityGroup);
    showSpecialCheck->setObjectName(QStringLiteral("memoryManagerShowSpecialMemories"));
    showSpecialCheck->setChecked(
        AppSettings::instance()
            .value(QString::fromLatin1(kMemoryShowSpecialMemoriesSettingsKey), QStringLiteral("False"))
            .toBool());
    visibilityLayout->addWidget(showSpecialCheck);
    auto* specialDescription = new QLabel(
        QStringLiteral("Special memories are the IC-9700 band scan-edge pairs (1A–3B) and call channels (C1–C2)."),
        visibilityGroup);
    specialDescription->setWordWrap(true);
    specialDescription->setStyleSheet(QStringLiteral("QLabel { color: palette(mid); }"));
    visibilityLayout->addWidget(specialDescription);
    visibilityLayout->addSpacing(8);

    auto* const showSatelliteCheck =
        new QCheckBox(QStringLiteral("Show satellite memories in Memory Manager"), visibilityGroup);
    showSatelliteCheck->setObjectName(QStringLiteral("memoryManagerShowSatelliteMemories"));
    showSatelliteCheck->setChecked(
        AppSettings::instance()
            .value(QString::fromLatin1(kMemoryShowSatelliteMemoriesSettingsKey), QStringLiteral("False"))
            .toBool());
    visibilityLayout->addWidget(showSatelliteCheck);
    auto* satelliteDescription = new QLabel(
        QStringLiteral("Satellite memories are paired receive/transmit records used by the IC-9700 satellite mode."),
        visibilityGroup);
    satelliteDescription->setWordWrap(true);
    satelliteDescription->setStyleSheet(QStringLiteral("QLabel { color: palette(mid); }"));
    visibilityLayout->addWidget(satelliteDescription);

    root->addWidget(visibilityGroup);
    root->addStretch(1);

    connect(pollIntervalCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, pollIntervalCombo](int index)
            {
                const int seconds = pollIntervalCombo->itemData(index).toInt();
                AppSettings::instance().setValue(QString::fromLatin1(kMemoryPollIntervalSecondsSettingsKey), seconds);
                emit pollIntervalSecondsChanged(seconds);
            });
    connect(showSpecialCheck, &QCheckBox::toggled, this, [](bool show)
            { AppSettings::instance().setValue(QString::fromLatin1(kMemoryShowSpecialMemoriesSettingsKey), show); });
    connect(showSpecialCheck, &QCheckBox::toggled, this, &MemoryManagerSettingsPanel::showSpecialMemoriesChanged);
    connect(showSatelliteCheck, &QCheckBox::toggled, this, [](bool show)
            { AppSettings::instance().setValue(QString::fromLatin1(kMemoryShowSatelliteMemoriesSettingsKey), show); });
    connect(showSatelliteCheck, &QCheckBox::toggled, this, &MemoryManagerSettingsPanel::showSatelliteMemoriesChanged);
}
