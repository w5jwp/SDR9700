#include "VfoSelectionPanel.h"

#include "UiTheme.h"

#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace
{
constexpr int kPanelWidth = 148;
constexpr int kPanelHeight = 218;
constexpr int kPanelMargin = 6;
constexpr int kButtonHeight = 18;
constexpr int kButtonWidth = 108;
constexpr int kVfoButtonWidth = 52;
constexpr int kVfoButtonSpacing = 4;
constexpr int kButtonSpacing = 10;

QString buttonStyle()
{
    return QStringLiteral("QPushButton { background: #080b0f; border: 1px solid %1; border-radius: 1px; color: %2; "
                          "font-size: 9px; font-weight: bold; padding: 0px 5px; } "
                          "QPushButton:hover { border-color: %3; color: %4; } "
                          "QPushButton[active=\"true\"] { background: %5; border-color: %3; color: %4; }")
        .arg(UiTheme::Color::Border, UiTheme::Color::TextStatusSecondary, UiTheme::Color::Accent,
             UiTheme::Color::TextBright, UiTheme::Color::AccentDark);
}

void refreshStyle(QPushButton* button)
{
    button->style()->unpolish(button);
    button->style()->polish(button);
    button->update();
}
} // namespace

VfoSelectionPanel::VfoSelectionPanel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("vfoSelectionPanel"));
    setAccessibleName(QStringLiteral("VFO selection controls"));
    setFixedSize(kPanelWidth, kPanelHeight);
    QPalette chromePalette = palette();
    chromePalette.setColor(QPalette::Window, QColor(QString::fromLatin1(UiTheme::Color::WindowChrome)));
    setPalette(chromePalette);
    setAutoFillBackground(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kPanelMargin, kPanelMargin, kPanelMargin, kPanelMargin);
    layout->setSpacing(0);

    m_mainButton = new QPushButton(QStringLiteral("MAIN"), this);
    m_subButton = new QPushButton(QStringLiteral("SUB"), this);
    m_exchangeButton = new QPushButton(QStringLiteral("MAIN ↔ SUB"), this);
    m_dualWatchButton = new QPushButton(QStringLiteral("DUAL WATCH"), this);
    for (auto* button : {m_mainButton, m_subButton, m_exchangeButton, m_dualWatchButton})
    {
        button->setFixedHeight(kButtonHeight);
        button->setStyleSheet(buttonStyle());
    }
    m_mainButton->setFixedWidth(kVfoButtonWidth);
    m_subButton->setFixedWidth(kVfoButtonWidth);
    m_exchangeButton->setFixedWidth(kButtonWidth);
    m_dualWatchButton->setFixedWidth(kButtonWidth);
    m_mainButton->setAccessibleName(QStringLiteral("Select MAIN VFO"));
    m_subButton->setAccessibleName(QStringLiteral("Select SUB VFO"));
    m_exchangeButton->setAccessibleName(QStringLiteral("Exchange MAIN and SUB VFOs"));
    m_dualWatchButton->setAccessibleName(QStringLiteral("Toggle dual watch"));

    auto* vfoButtonRow = new QWidget(this);
    vfoButtonRow->setFixedSize(kButtonWidth, kButtonHeight);
    auto* vfoButtonLayout = new QHBoxLayout(vfoButtonRow);
    vfoButtonLayout->setContentsMargins(0, 0, 0, 0);
    vfoButtonLayout->setSpacing(kVfoButtonSpacing);
    vfoButtonLayout->addWidget(m_mainButton);
    vfoButtonLayout->addWidget(m_subButton);

    auto* exchangeDivider = new QWidget(this);
    exchangeDivider->setObjectName(QStringLiteral("exchangeDivider"));
    exchangeDivider->setFixedSize(kButtonWidth, 1);
    exchangeDivider->setStyleSheet(QStringLiteral("background: %1;").arg(UiTheme::Color::Border));

    layout->addStretch();
    layout->addWidget(vfoButtonRow, 0, Qt::AlignHCenter);
    layout->addSpacing(kButtonSpacing);
    layout->addWidget(m_exchangeButton, 0, Qt::AlignHCenter);
    layout->addSpacing(kButtonSpacing);
    layout->addWidget(exchangeDivider, 0, Qt::AlignHCenter);
    layout->addSpacing(kButtonSpacing);
    layout->addWidget(m_dualWatchButton, 0, Qt::AlignHCenter);
    layout->addStretch();

    connect(m_mainButton, &QPushButton::clicked, this, [this]() { emit vfoRequested(Vfo::Main); });
    connect(m_subButton, &QPushButton::clicked, this, [this]() { emit vfoRequested(Vfo::Sub); });
    connect(m_exchangeButton, &QPushButton::clicked, this, &VfoSelectionPanel::exchangeRequested);
    connect(m_dualWatchButton, &QPushButton::clicked, this, [this]() { emit dualWatchRequested(!m_dualWatchEnabled); });
    updateControlsEnabled();
    updateButtonStyles();
}

void VfoSelectionPanel::setExchangePending(bool pending)
{
    m_exchangePending = pending;
    updateControlsEnabled();
}

void VfoSelectionPanel::setReceiverContextReady(bool ready)
{
    m_receiverContextReady = ready;
    updateControlsEnabled();
}

void VfoSelectionPanel::setDualWatchPending(bool pending)
{
    m_dualWatchPending = pending;
    updateControlsEnabled();
}

void VfoSelectionPanel::updateControlsEnabled()
{
    const bool contextAvailable = m_receiverContextReady && !m_exchangePending && !m_dualWatchPending;
    m_mainButton->setEnabled(contextAvailable);
    m_subButton->setEnabled(contextAvailable);
    m_dualWatchButton->setEnabled(contextAvailable);
    // Exchanging the two operating sides is only meaningful while both are
    // active. It also avoids starting a receiver-context transaction while
    // SUB is intentionally unavailable.
    m_exchangeButton->setEnabled(contextAvailable && m_dualWatchEnabled);
}

void VfoSelectionPanel::setPttButton(QPushButton* button)
{
    if (!button || m_pttButton == button)
    {
        return;
    }

    m_pttButton = button;
    m_pttButton->setParent(this);
    m_pttButton->setFixedSize(kButtonWidth, 36);
    m_pttButton->setProperty("pttButton", true);
    layout()->activate();
    m_pttButton->move((width() - m_pttButton->width()) / 2,
                      m_dualWatchButton->geometry().bottom() + 1 + kButtonSpacing);
    m_pttButton->show();
    m_pttButton->raise();
}

void VfoSelectionPanel::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.fillRect(rect(), QColor(QString::fromLatin1(UiTheme::Color::WindowChrome)));
}

void VfoSelectionPanel::setSelectedVfo(Vfo vfo)
{
    m_selectedVfo = vfo;
    updateButtonStyles();
}

void VfoSelectionPanel::setDualWatchEnabled(bool enabled)
{
    m_dualWatchEnabled = enabled;
    updateControlsEnabled();
    updateButtonStyles();
}

void VfoSelectionPanel::updateButtonStyles()
{
    m_mainButton->setProperty("active", m_selectedVfo == Vfo::Main);
    m_subButton->setProperty("active", m_selectedVfo == Vfo::Sub);
    m_dualWatchButton->setProperty("active", m_dualWatchEnabled);
    refreshStyle(m_mainButton);
    refreshStyle(m_subButton);
    refreshStyle(m_exchangeButton);
    refreshStyle(m_dualWatchButton);
}
