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
    pathRow->addRow(pathEdit);
    applicationLayout->addLayout(pathRow);

    auto* applicationButtonRow = new QHBoxLayout;
    auto* backupConfigurationButton = addActionButton(applicationButtonRow, applicationGroup, QStringLiteral("Backup"));
    auto* restoreConfigurationButton =
        addActionButton(applicationButtonRow, applicationGroup, QStringLiteral("Restore"));
    applicationButtonRow->addStretch(1);
    auto* resetConfigurationButton = addActionButton(applicationButtonRow, applicationGroup, QStringLiteral("Reset"));
    resetConfigurationButton->setStyleSheet(QStringLiteral("QPushButton { color: %1; }").arg(UiTheme::Color::Danger));
    applicationButtonRow->setSizeConstraint(QLayout::SetNoConstraint);
    applicationLayout->addLayout(applicationButtonRow);
    root->addWidget(applicationGroup);
    root->addStretch(1);

    connect(backupConfigurationButton, &QPushButton::clicked, this,
            [this]() { ConfigurationManager::backupConfiguration(this); });
    connect(restoreConfigurationButton, &QPushButton::clicked, this,
            [this]() { ConfigurationManager::restoreConfigurationAndRestart(this); });
    connect(resetConfigurationButton, &QPushButton::clicked, this,
            [this]() { ConfigurationManager::resetConfigurationAndRestart(this); });
}
