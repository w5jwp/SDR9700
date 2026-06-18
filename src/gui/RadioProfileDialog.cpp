#include "RadioProfileDialog.h"
#include "UiTheme.h"

#include <QListWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QFrame>
#include <QMessageBox>

RadioProfileWidget::RadioProfileWidget(QWidget* parent) : QWidget(parent)
{
    auto* root = new QHBoxLayout(this);
    root->setSpacing(12);
    root->setContentsMargins(0, 0, 0, 0);

    auto* leftPanel = new QWidget;
    auto* leftVbox = new QVBoxLayout(leftPanel);
    leftVbox->setContentsMargins(0, 0, 0, 0);
    leftVbox->setSpacing(4);
    leftPanel->setFixedWidth(170);

    m_list = new QListWidget;
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    leftVbox->addWidget(m_list, 1);

    auto* listBtns = new QHBoxLayout;
    m_addBtn = new QPushButton("Add");
    m_removeBtn = new QPushButton("Remove");
    m_removeBtn->setEnabled(false);
    listBtns->addWidget(m_addBtn);
    listBtns->addWidget(m_removeBtn);
    leftVbox->addLayout(listBtns);

    root->addWidget(leftPanel);

    auto* line = new QFrame;
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Sunken);
    root->addWidget(line);

    auto* rightVbox = new QVBoxLayout;
    rightVbox->setSpacing(8);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->setSpacing(8);

    m_nameEdit = new QLineEdit;
    m_nameEdit->setPlaceholderText("e.g. Home IC-9700");
    form->addRow("Name:", m_nameEdit);

    m_hostEdit = new QLineEdit;
    m_hostEdit->setPlaceholderText("192.168.1.x");
    form->addRow("Host / IP:", m_hostEdit);

    m_portSpin = new QSpinBox;
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(50001);
    form->addRow("Control port:", m_portSpin);

    m_userEdit = new QLineEdit;
    form->addRow("Username:", m_userEdit);

    auto* passRow = new QHBoxLayout;
    m_passEdit = new QLineEdit;
    m_passEdit->setEchoMode(QLineEdit::Password);
    m_showPassBtn = new QPushButton("Show");
    m_showPassBtn->setFixedWidth(52);
    m_showPassBtn->setCheckable(true);
    passRow->addWidget(m_passEdit);
    passRow->addWidget(m_showPassBtn);
    form->addRow("Password:", passRow);

    rightVbox->addLayout(form);

    m_saveBtn = new QPushButton("Save");
    m_saveBtn->setEnabled(false);
    m_saveBtn->setStyleSheet(
        QStringLiteral("QPushButton { background: %1; border: 1px solid %2; border-radius: 3px;"
                       " color: %3; padding: 4px 14px; }"
                       "QPushButton:hover { background: %4; border-color: %5; }"
                       "QPushButton:disabled { background: %6; border-color: %7; color: %8; }")
            .arg(QLatin1String(UiTheme::Color::Accent), QLatin1String(UiTheme::Color::AccentBright),
                 QLatin1String(UiTheme::Color::PanelDark), QLatin1String(UiTheme::Color::AccentHover),
                 QLatin1String(UiTheme::Color::AccentBright), QLatin1String(UiTheme::Color::Button),
                 QLatin1String(UiTheme::Color::Border), QLatin1String(UiTheme::Color::TextMuted)));
    auto* saveRow = new QHBoxLayout;
    saveRow->addStretch();
    saveRow->addWidget(m_saveBtn);
    rightVbox->addLayout(saveRow);

    rightVbox->addStretch(1);

    auto* closeBtn = new QPushButton("Close");
    rightVbox->addWidget(closeBtn, 0, Qt::AlignRight);
    connect(closeBtn, &QPushButton::clicked, this, [this]() { window()->close(); });

    root->addLayout(rightVbox, 1);

    setFormEnabled(false);

    connect(m_list, &QListWidget::currentRowChanged, this, &RadioProfileWidget::onSelectionChanged);
    connect(m_addBtn, &QPushButton::clicked, this, &RadioProfileWidget::onAddProfile);
    connect(m_removeBtn, &QPushButton::clicked, this, &RadioProfileWidget::onRemoveProfile);
    connect(m_saveBtn, &QPushButton::clicked, this, &RadioProfileWidget::onSaveProfile);

    connect(m_showPassBtn, &QPushButton::toggled, this,
            [this](bool on)
            {
                m_passEdit->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
                m_showPassBtn->setText(on ? "Hide" : "Show");
            });

    auto markDirty = [this]()
    {
        m_formDirty = true;
        m_saveBtn->setEnabled(!m_currentId.isNull());
    };
    connect(m_nameEdit, &QLineEdit::textChanged, this, markDirty);
    connect(m_hostEdit, &QLineEdit::textChanged, this, markDirty);
    connect(m_portSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, markDirty);
    connect(m_userEdit, &QLineEdit::textChanged, this, markDirty);
    connect(m_passEdit, &QLineEdit::textChanged, this, markDirty);

    rebuildList();
}

void RadioProfileWidget::rebuildList()
{
    const QUuid selected = m_currentId;
    m_list->blockSignals(true);
    m_list->clear();
    for (const RadioProfile& p : RadioProfileStore::instance().profiles())
    {
        auto* item = new QListWidgetItem(p.name.isEmpty() ? "(unnamed)" : p.name);
        item->setData(Qt::UserRole, p.id.toString());
        m_list->addItem(item);
    }
    m_list->blockSignals(false);

    for (int i = 0; i < m_list->count(); ++i)
    {
        if (QUuid(m_list->item(i)->data(Qt::UserRole).toString()) == selected)
        {
            m_list->setCurrentRow(i);
            return;
        }
    }
    if (m_list->count() > 0)
    {
        m_list->setCurrentRow(0);
    }
    else
    {
        onSelectionChanged();
    }
}

void RadioProfileWidget::loadProfileIntoForm(const RadioProfile& p)
{
    QSignalBlocker b1(m_nameEdit), b2(m_hostEdit), b3(m_portSpin), b4(m_userEdit), b5(m_passEdit);
    m_nameEdit->setText(p.name);
    m_hostEdit->setText(p.host);
    m_portSpin->setValue(p.port);
    m_userEdit->setText(p.username);
    m_passEdit->setText(p.password);
    m_formDirty = false;
    m_saveBtn->setEnabled(false);
}

RadioProfile RadioProfileWidget::profileFromForm() const
{
    RadioProfile p;
    p.id = m_currentId;
    p.name = m_nameEdit->text().trimmed();
    p.host = m_hostEdit->text().trimmed();
    p.port = static_cast<quint16>(m_portSpin->value());
    p.username = m_userEdit->text().trimmed();
    p.password = m_passEdit->text();
    return p;
}

void RadioProfileWidget::setFormEnabled(bool enabled)
{
    m_nameEdit->setEnabled(enabled);
    m_hostEdit->setEnabled(enabled);
    m_portSpin->setEnabled(enabled);
    m_userEdit->setEnabled(enabled);
    m_passEdit->setEnabled(enabled);
    m_showPassBtn->setEnabled(enabled);
    m_saveBtn->setEnabled(false);
    m_removeBtn->setEnabled(enabled);
}

void RadioProfileWidget::onSelectionChanged()
{
    auto* item = m_list->currentItem();
    if (!item)
    {
        m_currentId = QUuid();
        setFormEnabled(false);
        QSignalBlocker b1(m_nameEdit), b2(m_hostEdit), b3(m_portSpin), b4(m_userEdit), b5(m_passEdit);
        m_nameEdit->clear();
        m_hostEdit->clear();
        m_portSpin->setValue(50001);
        m_userEdit->clear();
        m_passEdit->clear();
        return;
    }
    m_currentId = QUuid(item->data(Qt::UserRole).toString());
    const RadioProfile* p = RadioProfileStore::instance().profileById(m_currentId);
    if (p)
    {
        loadProfileIntoForm(*p);
        setFormEnabled(true);
    }
}

void RadioProfileWidget::onAddProfile()
{
    RadioProfile p;
    p.id = QUuid::createUuid();
    p.name = "New Radio";
    p.port = 50001;
    if (!RadioProfileStore::instance().addProfile(p))
    {
        QMessageBox::warning(this, "Save Radio", "Could not save the new radio profile.");
        return;
    }
    m_currentId = p.id;
    rebuildList();
    m_nameEdit->setFocus();
    m_nameEdit->selectAll();
}

void RadioProfileWidget::onRemoveProfile()
{
    if (m_currentId.isNull())
    {
        return;
    }
    const RadioProfile* p = RadioProfileStore::instance().profileById(m_currentId);
    const QString name = p ? p->name : "this radio";
    if (QMessageBox::question(this, "Remove Radio", QString("Remove \"%1\"?").arg(name),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
    {
        return;
    }
    if (!RadioProfileStore::instance().removeProfile(m_currentId))
    {
        QMessageBox::warning(this, "Remove Radio", "Could not remove the selected radio profile.");
        return;
    }
    m_currentId = QUuid();
    rebuildList();
}

void RadioProfileWidget::onSaveProfile()
{
    if (m_currentId.isNull())
    {
        return;
    }
    RadioProfile p = profileFromForm();
    if (p.name.isEmpty())
    {
        p.name = p.host.isEmpty() ? "Unnamed" : p.host;
    }
    if (!RadioProfileStore::instance().updateProfile(p))
    {
        QMessageBox::warning(this, "Save Radio", "Could not save the radio profile.");
        return;
    }
    m_formDirty = false;
    m_saveBtn->setEnabled(false);
    rebuildList();
}
