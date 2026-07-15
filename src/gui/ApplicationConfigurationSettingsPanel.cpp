#include "ApplicationConfigurationSettingsPanel.h"

#include "ConfigurationManager.h"
#include "SettingsPanelStyle.h"
#include "UiTheme.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace
{
QPushButton* addActionButton(QHBoxLayout* row, QWidget* parent, const QString& text)
{
    auto* button = new QPushButton(text, parent);
    row->addWidget(button);
    return button;
}

QLineEdit* createPathEdit(QWidget* parent, const QString& path)
{
    auto* edit = new QLineEdit(path, parent);
    edit->setReadOnly(true);
    edit->setMinimumWidth(0);
    edit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    edit->setStyleSheet(QStringLiteral("QLineEdit { background: %1; border: 1px solid %2; color: %3; }")
                            .arg(QLatin1String(UiTheme::Color::Field), QLatin1String(UiTheme::Color::BorderMedium),
                                 QLatin1String(UiTheme::Color::TextField)));
    return edit;
}
} // namespace

ApplicationConfigurationSettingsPanel::ApplicationConfigurationSettingsPanel(QWidget* parent) : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 4, 12, 0);
    root->setSpacing(8);

    auto* applicationGroup = new QGroupBox(QStringLiteral("Application"), this);
    applicationGroup->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* applicationLayout = new QVBoxLayout(applicationGroup);
    applicationLayout->setContentsMargins(10, 12, 10, 10);
    applicationLayout->setSpacing(8);

    auto* pathRow = new QFormLayout;
    pathRow->setSpacing(8);
    auto* pathEdit = createPathEdit(applicationGroup, ConfigurationManager::configPath());
    pathRow->addRow(QStringLiteral("File:"), pathEdit);
    applicationLayout->addLayout(pathRow);

    auto* applicationButtonRow = new QHBoxLayout;
    auto* backupConfigurationButton = addActionButton(applicationButtonRow, applicationGroup, QStringLiteral("Backup"));
    auto* restoreConfigurationButton =
        addActionButton(applicationButtonRow, applicationGroup, QStringLiteral("Restore"));
    auto* exportConfigurationButton =
        addActionButton(applicationButtonRow, applicationGroup, QStringLiteral("Export..."));
    auto* importConfigurationButton =
        addActionButton(applicationButtonRow, applicationGroup, QStringLiteral("Import..."));
    auto* resetConfigurationButton =
        addActionButton(applicationButtonRow, applicationGroup, QStringLiteral("Reset..."));
    applicationButtonRow->addStretch(1);
    applicationButtonRow->setSizeConstraint(QLayout::SetNoConstraint);
    applicationLayout->addLayout(applicationButtonRow);
    root->addWidget(applicationGroup);

    auto* memoriesGroup = new QGroupBox(QStringLiteral("Memories"), this);
    memoriesGroup->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* memoriesLayout = new QVBoxLayout(memoriesGroup);
    memoriesLayout->setContentsMargins(10, 12, 10, 10);
    memoriesLayout->setSpacing(8);

    auto* memoriesPathRow = new QFormLayout;
    memoriesPathRow->setSpacing(8);
    auto* memoriesPathEdit = createPathEdit(memoriesGroup, ConfigurationManager::memoriesPath());
    memoriesPathRow->addRow(QStringLiteral("File:"), memoriesPathEdit);
    memoriesLayout->addLayout(memoriesPathRow);

    auto* memoriesButtonRow = new QHBoxLayout;
    auto* backupMemoriesButton = addActionButton(memoriesButtonRow, memoriesGroup, QStringLiteral("Backup"));
    auto* restoreMemoriesButton = addActionButton(memoriesButtonRow, memoriesGroup, QStringLiteral("Restore"));
    auto* exportMemoriesButton = addActionButton(memoriesButtonRow, memoriesGroup, QStringLiteral("Export..."));
    auto* importMemoriesButton = addActionButton(memoriesButtonRow, memoriesGroup, QStringLiteral("Import..."));
    auto* resetMemoriesButton = addActionButton(memoriesButtonRow, memoriesGroup, QStringLiteral("Reset..."));
    memoriesButtonRow->addStretch(1);
    memoriesButtonRow->setSizeConstraint(QLayout::SetNoConstraint);
    memoriesLayout->addLayout(memoriesButtonRow);
    root->addWidget(memoriesGroup);
    root->addStretch(1);

    connect(backupConfigurationButton, &QPushButton::clicked, this,
            [this]() { ConfigurationManager::backupConfiguration(this); });
    connect(restoreConfigurationButton, &QPushButton::clicked, this,
            [this]() { ConfigurationManager::restoreConfigurationAndRestart(this); });
    connect(exportConfigurationButton, &QPushButton::clicked, this,
            [this]() { ConfigurationManager::exportConfiguration(this); });
    connect(importConfigurationButton, &QPushButton::clicked, this,
            [this]() { ConfigurationManager::importConfigurationAndRestart(this); });
    connect(resetConfigurationButton, &QPushButton::clicked, this,
            [this]() { ConfigurationManager::resetConfigurationAndRestart(this); });

    connect(backupMemoriesButton, &QPushButton::clicked, this,
            [this]() { ConfigurationManager::backupMemories(this); });
    connect(restoreMemoriesButton, &QPushButton::clicked, this,
            [this]()
            {
                const MemoryImportResult result = ConfigurationManager::restoreMemories(this);
                if (result.success)
                {
                    Q_EMIT memoriesChanged(QStringLiteral("Restored %1 memories").arg(result.importedCount));
                }
            });
    connect(exportMemoriesButton, &QPushButton::clicked, this,
            [this]() { ConfigurationManager::exportMemories(this); });
    connect(importMemoriesButton, &QPushButton::clicked, this,
            [this]()
            {
                const MemoryImportResult result = ConfigurationManager::importMemories(this);
                if (result.success)
                {
                    Q_EMIT memoriesChanged(QStringLiteral("Imported %1 memories").arg(result.importedCount));
                }
            });
    connect(resetMemoriesButton, &QPushButton::clicked, this,
            [this]()
            {
                if (ConfigurationManager::resetMemories(this))
                {
                    Q_EMIT memoriesChanged(QStringLiteral("Memories reset"));
                }
            });
}
