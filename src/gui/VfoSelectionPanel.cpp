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
constexpr int kSelectorButtonHeight = 30;
constexpr int kExchangeButtonHeight = 30;
constexpr int kDualWatchButtonHeight = 34;
constexpr int kButtonWidth = 108;
constexpr int kVfoButtonWidth = 54;
constexpr int kRoutingDividerSpacing = 18;
constexpr int kPttDividerSpacing = 15;
constexpr int kButtonRadius = 3;
constexpr const char* kSilverGradient =
    "qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #414a53, stop:0.48 #333b43, stop:1 #262c32)";
constexpr const char* kSilverHoverGradient =
    "qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4c5863, stop:0.48 #3a454e, stop:1 #2c343b)";
constexpr const char* kCompoundBorder = "#71808e";
constexpr const char* kSilverPressed = "#252c33";
constexpr const char* kBlueSteelGradient =
    "qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #52728c, stop:0.48 #3e5d75, stop:1 #2e485d)";

enum class SegmentPosition
{
    TopLeft,
    TopRight,
    Middle,
    Bottom
};

QString selectionButtonStyle(SegmentPosition position, int fontPixelSize = 10)
{
    const QString innerBorder = position == SegmentPosition::TopRight ? QStringLiteral("border-left: none; ")
                                : position == SegmentPosition::Middle || position == SegmentPosition::Bottom
                                    ? QStringLiteral("border-top: none; ")
                                    : QString();
    QString corners;
    if (position == SegmentPosition::TopLeft)
    {
        corners = QStringLiteral("border-top-left-radius: %1px; ").arg(kButtonRadius);
    }
    else if (position == SegmentPosition::TopRight)
    {
        corners = QStringLiteral("border-top-right-radius: %1px; ").arg(kButtonRadius);
    }
    else if (position == SegmentPosition::Bottom)
    {
        corners =
            QStringLiteral("border-bottom-left-radius: %1px; border-bottom-right-radius: %1px; ").arg(kButtonRadius);
    }
    return QStringLiteral("QPushButton { background: %1; border: 1px solid %2; %3%4color: %5; "
                          "font-size: %12px; font-weight: bold; padding: 0px 5px; } "
                          "QPushButton:hover { background: %6; border-color: %7; color: %8; } "
                          "QPushButton:pressed { background: %9; } "
                          "QPushButton[active=\"true\"] { background: %10; border-color: %11; color: %8; }")
        .arg(kSilverGradient)
        .arg(kCompoundBorder)
        .arg(innerBorder)
        .arg(corners)
        .arg(UiTheme::Color::TextPrimary)
        .arg(kSilverHoverGradient)
        .arg(kCompoundBorder)
        .arg(UiTheme::Color::TextBright)
        .arg(kSilverPressed)
        .arg(kBlueSteelGradient)
        .arg(kCompoundBorder)
        .arg(fontPixelSize);
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
    m_dualWatchButton = new QPushButton(QStringLiteral("DUAL"), this);
    m_mainButton->setFixedHeight(kSelectorButtonHeight);
    m_subButton->setFixedHeight(kSelectorButtonHeight);
    m_exchangeButton->setFixedHeight(kExchangeButtonHeight);
    m_dualWatchButton->setFixedHeight(kDualWatchButtonHeight);
    m_mainButton->setStyleSheet(selectionButtonStyle(SegmentPosition::TopLeft));
    m_subButton->setStyleSheet(selectionButtonStyle(SegmentPosition::TopRight));
    m_exchangeButton->setStyleSheet(selectionButtonStyle(SegmentPosition::Middle));
    m_dualWatchButton->setStyleSheet(selectionButtonStyle(SegmentPosition::Bottom, 12));
    m_mainButton->setFixedWidth(kVfoButtonWidth);
    m_subButton->setFixedWidth(kVfoButtonWidth);
    m_exchangeButton->setFixedWidth(kButtonWidth);
    m_dualWatchButton->setFixedWidth(kButtonWidth);
    m_mainButton->setAccessibleName(QStringLiteral("Select MAIN VFO"));
    m_subButton->setAccessibleName(QStringLiteral("Select SUB VFO"));
    m_exchangeButton->setAccessibleName(QStringLiteral("Exchange MAIN and SUB VFOs"));
    m_dualWatchButton->setAccessibleName(QStringLiteral("Toggle dual watch"));

    auto* vfoButtonRow = new QWidget(this);
    vfoButtonRow->setFixedSize(kButtonWidth, kSelectorButtonHeight + kExchangeButtonHeight + kDualWatchButtonHeight);
    auto* vfoButtonLayout = new QVBoxLayout(vfoButtonRow);
    vfoButtonLayout->setContentsMargins(0, 0, 0, 0);
    vfoButtonLayout->setSpacing(0);
    auto* selectorRow = new QWidget(vfoButtonRow);
    selectorRow->setFixedSize(kButtonWidth, kSelectorButtonHeight);
    auto* selectorLayout = new QHBoxLayout(selectorRow);
    selectorLayout->setContentsMargins(0, 0, 0, 0);
    selectorLayout->setSpacing(0);
    selectorLayout->addWidget(m_mainButton);
    selectorLayout->addWidget(m_subButton);
    vfoButtonLayout->addWidget(selectorRow);
    vfoButtonLayout->addWidget(m_exchangeButton);
    vfoButtonLayout->addWidget(m_dualWatchButton);

    layout->addStretch();
    layout->addWidget(vfoButtonRow, 0, Qt::AlignHCenter);
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

void VfoSelectionPanel::setRadioReady(bool ready)
{
    m_radioReady = ready;
    updateControlsEnabled();
    updateButtonStyles();
}

void VfoSelectionPanel::setControlsEnabled(bool enabled)
{
    m_controlsEnabled = enabled;
    updateControlsEnabled();
}

void VfoSelectionPanel::setDualWatchPending(bool pending)
{
    m_dualWatchPending = pending;
    updateControlsEnabled();
}

void VfoSelectionPanel::updateControlsEnabled()
{
    const bool contextAvailable =
        m_controlsEnabled && m_radioReady && m_receiverContextReady && !m_exchangePending && !m_dualWatchPending;
    const bool routingAvailable = contextAvailable && m_dualWatchEnabled;
    m_mainButton->setEnabled(routingAvailable);
    m_subButton->setEnabled(routingAvailable);
    m_dualWatchButton->setEnabled(contextAvailable);
    // Exchanging the two operating sides is only meaningful while both are
    // active. It also avoids starting a receiver-context transaction while
    // SUB is intentionally unavailable.
    m_exchangeButton->setEnabled(routingAvailable);
}

void VfoSelectionPanel::setPttButton(QPushButton* button)
{
    if (!button || m_pttButton == button)
    {
        return;
    }

    m_pttButton = button;
    m_pttButton->setParent(this);
    m_pttButton->setFixedSize(kButtonWidth, 44);
    m_pttButton->setProperty("pttButton", true);
    auto* panelLayout = qobject_cast<QVBoxLayout*>(layout());
    const int bottomStretchIndex = panelLayout->count() - 1;
    auto* dualPttDivider = new QWidget(this);
    dualPttDivider->setObjectName(QStringLiteral("dualPttDivider"));
    dualPttDivider->setFixedSize(kButtonWidth, 1);
    dualPttDivider->setStyleSheet(QStringLiteral("background: %1;").arg(UiTheme::Color::Border));
    panelLayout->insertSpacing(bottomStretchIndex, kRoutingDividerSpacing);
    panelLayout->insertWidget(bottomStretchIndex + 1, dualPttDivider, 0, Qt::AlignHCenter);
    panelLayout->insertSpacing(bottomStretchIndex + 2, kPttDividerSpacing);
    panelLayout->insertWidget(bottomStretchIndex + 3, m_pttButton, 0, Qt::AlignHCenter);
    m_pttButton->show();
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
    // Selection is meaningful only after the backend and initial radio-state
    // synchronization are ready. Retaining an active highlight while
    // disconnected makes the last local belief look like live radio state.
    m_mainButton->setProperty("active", m_radioReady && m_selectedVfo == Vfo::Main);
    m_subButton->setProperty("active", m_radioReady && m_selectedVfo == Vfo::Sub);
    m_dualWatchButton->setProperty("active", m_radioReady && m_dualWatchEnabled);
    refreshStyle(m_mainButton);
    refreshStyle(m_subButton);
    refreshStyle(m_exchangeButton);
    refreshStyle(m_dualWatchButton);
}
