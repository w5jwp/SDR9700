#include "VfoDisplay.h"

#include "MainWindowHelpers.h"
#include "UiTheme.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{
constexpr int kDisplayHeight = 171;
constexpr int kDisplayHorizontalInset = 12;
constexpr int kDisplayTopLayoutInset = 10;
constexpr int kDisplayBottomLayoutInset = 7;
constexpr int kHeaderFrequencySpacing = 60;
constexpr int kTitleFontPixelSize = 10;
constexpr int kFrequencyFontPixelSize = 30;
constexpr int kTxBadgeWidth = 30;
constexpr int kTxBadgeHeight = 18;
constexpr int kHeaderButtonHeight = 18;
constexpr int kBandButtonWidth = 52;
constexpr int kModeButtonWidth = 52;
constexpr int kSquelchButtonWidth = 58;
constexpr int kTxPowerButtonWidth = 70;
constexpr int kHeaderControlGroupSpacing = 30;
constexpr int kReceiverControlHeight = 18;
constexpr int kReceiverControlSpacing = 4;
constexpr int kFrequencyReceiverControlSpacing = 10;

QString vfoTitle(Vfo vfo)
{
    return vfo == Vfo::Main ? QStringLiteral("MAIN VFO") : QStringLiteral("SUB VFO");
}

QString vfoName(Vfo vfo)
{
    return vfo == Vfo::Main ? QStringLiteral("MAIN") : QStringLiteral("SUB");
}

QString receiverControlStyle()
{
    return QStringLiteral("QPushButton { background: #080b0f; border: 1px solid %1; border-radius: 1px; color: %2; "
                          "font-size: 9px; font-weight: bold; padding: 0px 5px; } "
                          "QPushButton:hover { border-color: %3; color: %4; } "
                          "QPushButton[active=\"true\"] { background: %5; border-color: %3; color: %4; } "
                          "QPushButton:disabled { background: #080b0f; border-color: %1; color: %2; }")
        .arg(UiTheme::Color::Border, UiTheme::Color::TextStatusSecondary, UiTheme::Color::Accent,
             UiTheme::Color::TextBright, UiTheme::Color::AccentDark);
}
} // namespace

VfoDisplay::VfoDisplay(Vfo vfo, QWidget* parent) : QWidget(parent), m_vfo(vfo)
{
    const QString title = vfoTitle(vfo);
    setObjectName(vfo == Vfo::Main ? QStringLiteral("mainVfoDisplay") : QStringLiteral("subVfoDisplay"));
    setAccessibleName(title);
    setFixedHeight(kDisplayHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* layout = new QVBoxLayout(this);
    // Qt's styled button row contributes 3 px below its controls on macOS.
    // Compensate here so the rendered border-to-control gap matches the top.
    const int leftInset = vfo == Vfo::Main ? kDisplayHorizontalInset : 20;
    const int rightInset = vfo == Vfo::Main ? 20 : kDisplayHorizontalInset;
    layout->setContentsMargins(leftInset, kDisplayTopLayoutInset, rightInset, kDisplayBottomLayoutInset);
    layout->setSpacing(0);

    auto* headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(6);

    if (vfo == Vfo::Main)
    {
        m_txBadge = new QLabel(QStringLiteral("TX"), this);
        m_txBadge->setObjectName(QStringLiteral("vfoTxBadge"));
        m_txBadge->setFixedSize(kTxBadgeWidth, kTxBadgeHeight);
        m_txBadge->setAlignment(Qt::AlignCenter);
        m_txBadge->setAccessibleName(QStringLiteral("%1 transmit indicator").arg(title));
        m_txBadge->setStyleSheet(
            QStringLiteral("QLabel { background: #080b0f; border: 1px solid %1; border-radius: 1px; color: %1; "
                           "font-size: %3px; font-weight: bold; } "
                           "QLabel[transmitting=\"true\"] { background: %2; border-color: %1; color: %4; }")
                .arg(UiTheme::Color::Danger, UiTheme::Color::PttActive)
                .arg(kTitleFontPixelSize)
                .arg(UiTheme::Color::TextBright));
    }

    m_identityButton = new QPushButton(vfoName(vfo), this);
    m_identityButton->setObjectName(QStringLiteral("vfoIdentityButton"));
    m_identityButton->setFixedHeight(kHeaderButtonHeight);
    m_identityButton->setCursor(Qt::PointingHandCursor);
    m_identityButton->setAccessibleName(QStringLiteral("Select %1").arg(title));
    m_identityButton->setStyleSheet(receiverControlStyle());
    connect(m_identityButton, &QPushButton::clicked, this, &VfoDisplay::vfoClicked);

    m_bandButton = new QPushButton(QStringLiteral("--"), this);
    m_bandButton->setObjectName(QStringLiteral("vfoBandButton"));
    m_bandButton->setFixedHeight(kHeaderButtonHeight);
    m_bandButton->setFixedWidth(kBandButtonWidth);
    m_bandButton->setCursor(Qt::PointingHandCursor);
    m_bandButton->setAccessibleName(QStringLiteral("%1 band").arg(title));
    m_bandButton->setStyleSheet(receiverControlStyle());
    connect(m_bandButton, &QPushButton::clicked, this, &VfoDisplay::bandClicked);

    m_modeButton = new QPushButton(QStringLiteral("--"), this);
    m_modeButton->setObjectName(QStringLiteral("vfoModeButton"));
    m_modeButton->setFixedSize(kModeButtonWidth, kHeaderButtonHeight);
    m_modeButton->setCursor(Qt::PointingHandCursor);
    m_modeButton->setAccessibleName(QStringLiteral("%1 mode").arg(title));
    m_modeButton->setStyleSheet(receiverControlStyle());
    connect(m_modeButton, &QPushButton::clicked, this, &VfoDisplay::modeClicked);

    auto createHeaderControl = [this, &title](const QString& control, int width)
    {
        auto* button = new QPushButton(control, this);
        button->setObjectName(QStringLiteral("vfo%1Button").arg(control).remove(QLatin1Char(' ')));
        button->setFixedSize(width, kHeaderButtonHeight);
        button->setCursor(Qt::PointingHandCursor);
        button->setAccessibleName(QStringLiteral("%1 %2 control").arg(title, control));
        button->setStyleSheet(receiverControlStyle());
        connect(button, &QPushButton::clicked, this, [this, control]() { emit receiverControlClicked(control); });
        m_receiverControlButtons.insert(control, button);
        return button;
    };
    QPushButton* squelchButton = createHeaderControl(QStringLiteral("SQL"), kSquelchButtonWidth);
    QPushButton* txPowerButton =
        vfo == Vfo::Main ? createHeaderControl(QStringLiteral("TX PWR"), kTxPowerButtonWidth) : nullptr;

    if (m_txBadge)
    {
        headerLayout->addWidget(m_txBadge);
    }
    headerLayout->addStretch();
    if (txPowerButton)
    {
        headerLayout->addWidget(txPowerButton);
    }
    headerLayout->addWidget(squelchButton);
    headerLayout->addSpacing(kHeaderControlGroupSpacing);
    headerLayout->addWidget(m_bandButton);
    headerLayout->addWidget(m_modeButton);
    headerLayout->addSpacing(kHeaderControlGroupSpacing);
    headerLayout->addWidget(m_identityButton);

    m_frequencyEdit = new QLineEdit(this);
    m_frequencyEdit->setObjectName(QStringLiteral("vfoFrequency"));
    m_frequencyEdit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_frequencyEdit->setFocusPolicy(Qt::ClickFocus);
    m_frequencyEdit->setAccessibleName(QStringLiteral("%1 frequency").arg(title));
    m_frequencyEdit->setAccessibleDescription(QStringLiteral("Enter frequency in MHz, then press Enter."));
    m_frequencyEdit->setToolTip(QStringLiteral("Enter frequency in MHz, then press Enter"));
    m_frequencyEdit->setStyleSheet(
        QStringLiteral("QLineEdit { background: transparent; border: none; padding: 0px; color: %1; }")
            .arg(UiTheme::Color::TextField));
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    QFont frequencyFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    frequencyFont.setFeature(QFont::Tag("tnum"), 1);
#else
    QFont frequencyFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
#endif
    frequencyFont.setPixelSize(kFrequencyFontPixelSize);
    frequencyFont.setBold(true);
    m_frequencyEdit->setFont(frequencyFont);
    connect(m_frequencyEdit, &QLineEdit::returnPressed, this,
            [this]()
            {
                emit frequencySubmitted(m_frequencyEdit->text());
                m_frequencyEdit->clearFocus();
            });

    layout->addLayout(headerLayout);
    layout->addSpacing(kHeaderFrequencySpacing);
    layout->addWidget(m_frequencyEdit, 1);
    layout->addSpacing(kFrequencyReceiverControlSpacing);

    auto* receiverControlLayout = new QHBoxLayout;
    receiverControlLayout->setContentsMargins(0, 0, 0, 0);
    receiverControlLayout->setSpacing(kReceiverControlSpacing);
    const QStringList controls = {
        QStringLiteral("AGC"), QStringLiteral("ATT"), QStringLiteral("NB"),  QStringLiteral("NOTCH"),
        QStringLiteral("NR"),  QStringLiteral("PRE"), QStringLiteral("RFG"),
    };
    for (const QString& control : controls)
    {
        auto* button = new QPushButton(control, this);
        button->setObjectName(QStringLiteral("vfo%1Button").arg(control).remove(QLatin1Char(' ')));
        button->setFixedHeight(kReceiverControlHeight);
        button->setCursor(Qt::PointingHandCursor);
        button->setAccessibleName(QStringLiteral("%1 %2 control").arg(title, control));
        button->setStyleSheet(receiverControlStyle());
        connect(button, &QPushButton::clicked, this, [this, control]() { emit receiverControlClicked(control); });
        m_receiverControlButtons.insert(control, button);
        receiverControlLayout->addWidget(button);
    }
    layout->addLayout(receiverControlLayout);
    setOperatingEnabled(true);
    clearFrequency();
}

QString VfoDisplay::frequencyText() const
{
    return m_frequencyEdit ? m_frequencyEdit->text() : QString();
}

void VfoDisplay::setFrequencyHz(quint64 hz)
{
    if (!m_frequencyEdit)
    {
        return;
    }
    m_frequencyEdit->setText(sdr9700::ui::main_window::formatFrequency(hz));
}

void VfoDisplay::clearFrequency()
{
    if (m_frequencyEdit)
    {
        m_frequencyEdit->setText(QStringLiteral("---.---.---"));
    }
    setBandText(QStringLiteral("--"));
    setModeText(QStringLiteral("--"));
}

void VfoDisplay::setOperatingEnabled(bool enabled)
{
    m_operatingEnabled = enabled;
    m_frequencyEdit->setEnabled(enabled);
    m_bandButton->setEnabled(enabled);
    m_modeButton->setEnabled(enabled);
    for (QPushButton* button : {m_bandButton, m_modeButton})
    {
        button->setProperty("active", enabled);
        button->style()->unpolish(button);
        button->style()->polish(button);
        button->update();
    }
    for (QPushButton* button : std::as_const(m_receiverControlButtons))
    {
        button->setEnabled(enabled);
    }
    update();
}

void VfoDisplay::setBandText(const QString& text)
{
    m_bandButton->setText(text);
}

void VfoDisplay::setModeText(const QString& text)
{
    m_modeButton->setText(text);
}

void VfoDisplay::setSelected(bool selected)
{
    m_identityButton->setProperty("active", selected);
    m_identityButton->style()->unpolish(m_identityButton);
    m_identityButton->style()->polish(m_identityButton);
    m_identityButton->update();
}

void VfoDisplay::setTransmitting(bool transmitting)
{
    if (!m_txBadge)
    {
        return;
    }
    m_txBadge->setProperty("transmitting", transmitting);
    m_txBadge->style()->unpolish(m_txBadge);
    m_txBadge->style()->polish(m_txBadge);
    m_txBadge->update();
}

void VfoDisplay::setReceiverControlState(const QString& control, const QString& value, bool active)
{
    QPushButton* button = m_receiverControlButtons.value(control, nullptr);
    if (!button)
    {
        return;
    }
    button->setText(value.isEmpty() ? control : QStringLiteral("%1 %2").arg(control, value));
    button->setProperty("active", active);
    button->style()->unpolish(button);
    button->style()->polish(button);
    button->update();
}

QPoint VfoDisplay::bandMenuPosition() const
{
    return m_bandButton->mapToGlobal(QPoint(0, m_bandButton->height()));
}

QPoint VfoDisplay::modeMenuPosition() const
{
    return m_modeButton->mapToGlobal(QPoint(0, m_modeButton->height()));
}

QPoint VfoDisplay::receiverControlMenuPosition(const QString& control) const
{
    if (const QPushButton* button = m_receiverControlButtons.value(control, nullptr))
    {
        return button->mapToGlobal(QPoint(0, button->height()));
    }
    return mapToGlobal(rect().center());
}

void VfoDisplay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
}
