#include "MainTitleBar.h"
#include "UiTheme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QKeySequence>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyleOption>
#include <QToolButton>
#include <QWindow>
#include <memory>

namespace
{
// QToolButton that always renders the mnemonic underline (& prefix in text),
// independent of the platform style hint SH_UnderlineShortcut.
class TitleMenuButton : public QToolButton
{
  public:
    explicit TitleMenuButton(QWidget* parent = nullptr) : QToolButton(parent) {}

  protected:
    void paintEvent(QPaintEvent*) override
    {
        QStyleOptionToolButton opt;
        initStyleOption(&opt);
        QPainter p(this);
        // Draw frame/background with text cleared so the style doesn't draw
        // it (we re-draw below with the mnemonic flag forced on).
        const QString labelText = opt.text;
        opt.text.clear();
        style()->drawComplexControl(QStyle::CC_ToolButton, &opt, &p, this);
        const QRect cr = style()->subControlRect(QStyle::CC_ToolButton, &opt, QStyle::SC_ToolButton, this);
        style()->drawItemText(&p, cr, Qt::AlignCenter | Qt::TextShowMnemonic, opt.palette, isEnabled(), labelText,
                              foregroundRole());
    }
};

QChar mnemonicChar(const QString& label)
{
    const int pos = label.indexOf(QLatin1Char('&'));
    if (pos >= 0 && pos + 1 < label.size() && label[pos + 1] != QLatin1Char('&'))
    {
        return label[pos + 1].toUpper();
    }
    return {};
}

constexpr int kTitleBarHeight = 32;
constexpr int kWindowButtonSize = 32;
constexpr int kVolumeSliderWidth = 110;
constexpr int kVolumeLabelWidth = 30;
constexpr int kTxDurationWidth = 62;
constexpr int kTitleControlSpacing = 8;
constexpr int kVolumeValueSpacing = 2;
constexpr QMargins kNoMargins(0, 0, 0, 0);

QString menuButtonStyle()
{
    return QStringLiteral("QToolButton { background: transparent; color: %1; padding: 0 9px;"
                          " border: none; font-size: 13px; }"
                          "QToolButton:hover { background: %2; }"
                          "QToolButton:pressed { background: %3; }"
                          "QToolButton::menu-indicator { image: none; width: 0; }")
        .arg(UiTheme::Color::TextPrimary, UiTheme::Color::ButtonHover, UiTheme::Color::AccentDark);
}

QString muteButtonStyle(bool active)
{
    return active ? QStringLiteral("QPushButton { background: %1; border: 1px solid %2; border-radius: 3px;"
                                   " color: %3; font-size: 10px; font-weight: bold; padding: 0 6px; }"
                                   "QPushButton:hover { background: %4; }")
                        .arg(UiTheme::Color::AccentDark, UiTheme::Color::Accent, UiTheme::Color::TextBright,
                             UiTheme::Color::AccentHover)
                  : QStringLiteral("QPushButton { background: transparent; border: 1px solid %1; border-radius: 3px;"
                                   " color: %2; font-size: 10px; font-weight: bold; padding: 0 6px; }"
                                   "QPushButton:hover { background: %3; border-color: %4; }")
                        .arg(UiTheme::Color::BorderLight, UiTheme::Color::TextMuted, UiTheme::Color::ButtonHover,
                             UiTheme::Color::ButtonHoverBorder);
}

QString txDurationStyle(bool active)
{
    return active ? QStringLiteral("QPushButton { background: %1; border: 1px solid %2; border-radius: 3px;"
                                   " color: %3; font-size: 10px; font-weight: bold; padding: 0 3px; }"
                                   "QPushButton:hover { background: %4; }")
                        .arg(UiTheme::Color::PttActive, UiTheme::Color::PttActiveBorder, UiTheme::Color::TextBright,
                             UiTheme::Color::PttHover)
                  : QStringLiteral("QPushButton { background: transparent; border: 1px solid %1; border-radius: 3px;"
                                   " color: %2; font-size: 10px; font-weight: bold; padding: 0 3px; }"
                                   "QPushButton:hover { background: %3; border-color: %4; }")
                        .arg(UiTheme::Color::BorderLight, UiTheme::Color::TextMuted, UiTheme::Color::ButtonHover,
                             UiTheme::Color::ButtonHoverBorder);
}

QString windowButtonStyle(const char* hoverBg)
{
    return QStringLiteral("QPushButton { background: transparent; border: none; color: %1; font-size: 14px; }"
                          "QPushButton:hover { background: %2; }")
        .arg(UiTheme::Color::TextMuted, QString::fromLatin1(hoverBg));
}

} // namespace

MainTitleBar::MainTitleBar(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(kTitleBarHeight);
    setAutoFillBackground(false);
    setStyleSheet(QStringLiteral("MainTitleBar { background: %1; }").arg(UiTheme::Color::MenuBar));

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(kNoMargins);
    root->setSpacing(0);

    // Menu buttons sit in a sub-layout so addMenu() can append to it
    auto* menuContainer = new QWidget(this);
    menuContainer->setObjectName(QStringLiteral("titleMenuContainer"));
    menuContainer->setStyleSheet(QStringLiteral("QWidget#titleMenuContainer { background: transparent; }"));
    m_menuLayout = new QHBoxLayout(menuContainer);
    m_menuLayout->setContentsMargins(10, 0, 0, 0);
    m_menuLayout->setSpacing(0);
    root->addWidget(menuContainer);

    root->addStretch(1);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setTextFormat(Qt::RichText);
    m_titleLabel->setStyleSheet(QStringLiteral("QLabel { background: transparent; }"));
    root->addWidget(m_titleLabel);

    root->addStretch(1);

    // Volume controls
    auto* volSep = new QWidget(this);
    volSep->setFixedSize(1, 18);
    volSep->setStyleSheet(QStringLiteral("background: %1;").arg(UiTheme::Color::Border));
    root->addWidget(volSep);
    root->addSpacing(kTitleControlSpacing);

    m_txDurationButton = new QPushButton(QStringLiteral("00:00:00"), this);
    m_txDurationButton->setFixedSize(kTxDurationWidth, 22);
    m_txDurationButton->setCheckable(false);
    m_txDurationButton->setStyleSheet(txDurationStyle(false));
    m_txDurationButton->setToolTip(QStringLiteral("Reset transmit duration"));
    root->addWidget(m_txDurationButton);
    root->addSpacing(kTitleControlSpacing);

    auto* txSep = new QWidget(this);
    txSep->setFixedSize(1, 18);
    txSep->setStyleSheet(QStringLiteral("background: %1;").arg(UiTheme::Color::Border));
    root->addWidget(txSep);
    root->addSpacing(kTitleControlSpacing);

    m_lockBtn = new QPushButton(QStringLiteral("LOCK"), this);
    m_lockBtn->setFixedHeight(22);
    m_lockBtn->setCheckable(false);
    m_lockBtn->setStyleSheet(muteButtonStyle(false));
    m_lockBtn->setToolTip(QStringLiteral("Toggle control lock"));
    root->addWidget(m_lockBtn);
    root->addSpacing(kTitleControlSpacing);

    auto* btnSep = new QWidget(this);
    btnSep->setFixedSize(1, 18);
    btnSep->setStyleSheet(QStringLiteral("background: %1;").arg(UiTheme::Color::Border));
    root->addWidget(btnSep);
    root->addSpacing(kTitleControlSpacing);

    m_muteBtn = new QPushButton(QStringLiteral("MUTE"), this);
    m_muteBtn->setFixedHeight(22);
    m_muteBtn->setCheckable(false);
    m_muteBtn->setStyleSheet(muteButtonStyle(false));
    m_muteBtn->setToolTip(QStringLiteral("Toggle mute"));
    root->addWidget(m_muteBtn);
    root->addSpacing(kTitleControlSpacing);

    auto* muteSep = new QWidget(this);
    muteSep->setFixedSize(1, 18);
    muteSep->setStyleSheet(QStringLiteral("background: %1;").arg(UiTheme::Color::Border));
    root->addWidget(muteSep);
    root->addSpacing(kTitleControlSpacing);

    auto* speakerLabel = new QLabel(QStringLiteral("🔊"), this);
    speakerLabel->setFixedHeight(20);
    speakerLabel->setAlignment(Qt::AlignVCenter | Qt::AlignHCenter);
    speakerLabel->setStyleSheet(
        QStringLiteral("QLabel { background: transparent; font-size: 14px; padding-bottom: 2px; }"));
    speakerLabel->setToolTip(QStringLiteral("Volume"));
    root->addWidget(speakerLabel, 0, Qt::AlignVCenter);
    root->addSpacing(kTitleControlSpacing);

    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setRange(0, 255);
    m_volumeSlider->setValue(128);
    m_volumeSlider->setFixedWidth(kVolumeSliderWidth);
    m_volumeSlider->setFixedHeight(20);
    m_volumeSlider->setToolTip(QStringLiteral("Volume"));
    root->addWidget(m_volumeSlider);
    root->addSpacing(kVolumeValueSpacing);

    m_volumeLabel = new QLabel(QStringLiteral("50%"), this);
    m_volumeLabel->setFixedWidth(kVolumeLabelWidth);
    m_volumeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_volumeLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-size: 10px; font-weight: bold; background: transparent; }")
            .arg(UiTheme::Color::TextMuted));
    root->addWidget(m_volumeLabel);
    root->addSpacing(kTitleControlSpacing);

    auto* chromeSep = new QWidget(this);
    chromeSep->setFixedSize(1, 18);
    chromeSep->setStyleSheet(QStringLiteral("background: %1;").arg(UiTheme::Color::Border));
    root->addWidget(chromeSep);
    root->addSpacing(kTitleControlSpacing);

    m_minimizeBtn = new QPushButton(QStringLiteral("−"), this);
    m_minimizeBtn->setFixedSize(kWindowButtonSize, kTitleBarHeight);
    m_minimizeBtn->setStyleSheet(windowButtonStyle(UiTheme::Color::ButtonHover));
    m_minimizeBtn->setToolTip(QStringLiteral("Minimize"));
    m_minimizeBtn->setAccessibleName(QStringLiteral("Minimize window"));
    root->addWidget(m_minimizeBtn);

    m_closeBtn = new QPushButton(QStringLiteral("✕"), this);
    m_closeBtn->setFixedSize(kWindowButtonSize, kTitleBarHeight);
    m_closeBtn->setStyleSheet(windowButtonStyle(UiTheme::Color::Danger));
    m_closeBtn->setToolTip(QStringLiteral("Close"));
    m_closeBtn->setAccessibleName(QStringLiteral("Close window"));
    root->addWidget(m_closeBtn);

    connect(m_volumeSlider, &QSlider::valueChanged, this,
            [this](int v)
            {
                const int pct = v * 100 / 255;
                if (m_volumeLabel)
                {
                    m_volumeLabel->setText(QStringLiteral("%1%").arg(pct));
                }
                emit volumeChanged(v);
            });

    connect(m_muteBtn, &QPushButton::clicked, this, &MainTitleBar::muteToggled);
    connect(m_lockBtn, &QPushButton::clicked, this, &MainTitleBar::lockToggled);
    connect(m_txDurationButton, &QPushButton::clicked, this, &MainTitleBar::txDurationResetRequested);
    connect(m_minimizeBtn, &QPushButton::clicked, this, &MainTitleBar::minimizeRequested);
    connect(m_closeBtn, &QPushButton::clicked, this, &MainTitleBar::closeRequested);
    auto* closeShortcut = new QShortcut(QKeySequence::Close, this);
    closeShortcut->setContext(Qt::WindowShortcut);
    connect(closeShortcut, &QShortcut::activated, this, &MainTitleBar::closeRequested);
}

void MainTitleBar::addMenu(const QString& label, QMenu* menu)
{
    auto* btn = new TitleMenuButton(this);
    btn->setText(label);
    btn->setMenu(menu);
    btn->setPopupMode(QToolButton::InstantPopup);
    btn->setStyleSheet(menuButtonStyle());
    btn->setFixedHeight(kTitleBarHeight);
    btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_menuLayout->addWidget(btn);

    const QChar mnemonic = mnemonicChar(label);
    if (!mnemonic.isNull())
    {
        auto* sc = new QShortcut(QKeySequence(Qt::ALT | mnemonic.unicode()), this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated, btn, [btn]() { btn->showMenu(); });
    }
}

void MainTitleBar::addAction(const QString& label, QObject* context, std::function<void()> callback)
{
    auto* btn = new TitleMenuButton(this);
    btn->setText(label);
    btn->setStyleSheet(menuButtonStyle());
    btn->setFixedHeight(kTitleBarHeight);
    btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_menuLayout->addWidget(btn);

    auto callbackPtr = std::make_shared<std::function<void()>>(std::move(callback));
    connect(btn, &QToolButton::clicked, context,
            [callbackPtr]()
            {
                if (*callbackPtr)
                {
                    (*callbackPtr)();
                }
            });

    const QChar mnemonic = mnemonicChar(label);
    if (!mnemonic.isNull())
    {
        auto* sc = new QShortcut(QKeySequence(Qt::ALT | mnemonic.unicode()), this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated, context,
                [callbackPtr]()
                {
                    if (*callbackPtr)
                    {
                        (*callbackPtr)();
                    }
                });
    }
}

void MainTitleBar::setTitle(const QString& title)
{
    if (m_titleLabel)
    {
        m_titleLabel->setText(title);
    }
}

void MainTitleBar::setVolume(int value)
{
    if (!m_volumeSlider)
    {
        return;
    }
    const int bounded = qBound(0, value, 255);
    const QSignalBlocker blocker(m_volumeSlider);
    m_volumeSlider->setValue(bounded);
    if (m_volumeLabel)
    {
        m_volumeLabel->setText(QStringLiteral("%1%").arg(bounded * 100 / 255));
    }
}

void MainTitleBar::setMuted(bool muted)
{
    if (m_muteBtn)
    {
        m_muteBtn->setStyleSheet(muteButtonStyle(muted));
    }
}

void MainTitleBar::setLocked(bool locked)
{
    if (m_lockBtn)
    {
        m_lockBtn->setStyleSheet(muteButtonStyle(locked));
    }
}

void MainTitleBar::setTxDuration(const QString& duration, bool transmitting)
{
    if (!m_txDurationButton)
    {
        return;
    }
    m_txDurationButton->setText(duration);
    m_txDurationButton->setStyleSheet(txDurationStyle(transmitting));
}

void MainTitleBar::setTxDurationActive(bool transmitting)
{
    if (m_txDurationButton)
    {
        m_txDurationButton->setStyleSheet(txDurationStyle(transmitting));
    }
}

void MainTitleBar::setVolumeEnabled(bool enabled)
{
    if (m_volumeSlider)
    {
        m_volumeSlider->setEnabled(enabled);
    }
    if (m_muteBtn)
    {
        m_muteBtn->setEnabled(enabled);
    }
    if (m_lockBtn)
    {
        m_lockBtn->setEnabled(true); // lock is always available regardless of radio state
    }
}

void MainTitleBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        if (QWindow* win = window()->windowHandle())
        {
            win->startSystemMove();
        }
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}
