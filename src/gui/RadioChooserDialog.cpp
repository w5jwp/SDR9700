#include "RadioChooserDialog.h"
#include "AppSettings.h"
#include "DialogPlacement.h"
#include "SettingsDialog.h"
#include "RadioProfile.h"

#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>

RadioChooserDialog::RadioChooserDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle("Radio Chooser");
    setMinimumWidth(360);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);

    auto* title = new QLabel("Select a saved radio:");
    root->addWidget(title);

    m_list = new QListWidget;
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setMinimumHeight(140);
    root->addWidget(m_list, 1);

    auto* btnRow = new QHBoxLayout;

    auto* settingsBtn = new QPushButton("Radio Setup");
    btnRow->addWidget(settingsBtn);
    btnRow->addStretch(1);

    auto* cancelBtn = new QPushButton("Cancel");
    m_connectBtn = new QPushButton("Connect");
    m_connectBtn->setDefault(true);
    m_connectBtn->setEnabled(false);
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(m_connectBtn);

    root->addLayout(btnRow);

    connect(m_list, &QListWidget::currentRowChanged, this, &RadioChooserDialog::onSelectionChanged);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { onConnect(); });
    connect(m_connectBtn, &QPushButton::clicked, this, &RadioChooserDialog::onConnect);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(settingsBtn, &QPushButton::clicked, this, &RadioChooserDialog::onOpenSettings);

    m_autoConnectCheck = new QCheckBox("Auto-connect previous radio on startup");
    m_autoConnectCheck->setChecked(AppSettings::instance().value("AutoConnect", "True").toBool());
    connect(m_autoConnectCheck, &QCheckBox::toggled, this, &RadioChooserDialog::onAutoConnectToggled);
    root->addWidget(m_autoConnectCheck);

    rebuildList();
}

void RadioChooserDialog::rebuildList()
{
    const QUuid lastId = RadioProfileStore::instance().lastProfileId();

    m_list->blockSignals(true);
    m_list->clear();
    for (const RadioProfile& p : RadioProfileStore::instance().profiles())
    {
        const QString label =
            QString("%1  —  %2").arg(p.name.isEmpty() ? "(unnamed)" : p.name, p.host.isEmpty() ? "?" : p.host);
        auto* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, p.id.toString());
        m_list->addItem(item);
        if (p.id == lastId)
        {
            m_list->setCurrentItem(item);
        }
    }
    m_list->blockSignals(false);

    if (!m_list->currentItem() && m_list->count() > 0)
    {
        m_list->setCurrentRow(0);
    }

    onSelectionChanged();
}

void RadioChooserDialog::onSelectionChanged()
{
    m_connectBtn->setEnabled(m_list->currentItem() != nullptr);
}

void RadioChooserDialog::onConnect()
{
    auto* item = m_list->currentItem();
    if (!item)
    {
        return;
    }

    const QUuid id(item->data(Qt::UserRole).toString());
    const RadioProfile* p = RadioProfileStore::instance().profileById(id);
    if (!p)
    {
        return;
    }

    if (!RadioProfileStore::instance().setLastProfileId(id))
    {
        QMessageBox::warning(this, "Radio Chooser", "Could not save the last selected radio profile.");
    }
    emit connectRequested(id);
    accept();
}

void RadioChooserDialog::onOpenSettings()
{
    SettingsDialog settings(this);
    sdr9700::ui::centerWindowOn(&settings, parentWidget() ? parentWidget()->window() : this);
    settings.exec();
    rebuildList();
}

void RadioChooserDialog::onAutoConnectToggled(bool on)
{
    AppSettings::instance().setValue("AutoConnect", on);
}
