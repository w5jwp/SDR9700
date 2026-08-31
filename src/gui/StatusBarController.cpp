#include "StatusBarController.h"

#include "AppSettings.h"
#include "MainTitleBar.h"
#include "MainWindowHelpers.h"
#include "MetersDialog.h"
#include "UiTheme.h"
#include "models/RadioModel.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

using namespace sdr9700::ui::main_window;

namespace
{
constexpr int kClockSeparatorGap = 6;

QString normalizedToastMessage(QString message)
{
    message = message.trimmed();
    while (!message.isEmpty())
    {
        const QChar last = message.back();
        if (last == QLatin1Char('.') || last == QLatin1Char('!') || last == QLatin1Char('?') ||
            last == QLatin1Char(';') || last == QLatin1Char(':') || last == QChar(0x2026))
        {
            message.chop(1);
            message = message.trimmed();
            continue;
        }
        break;
    }
    return message;
}
} // namespace

StatusBarController::StatusBarController(MainWindow* window) : QObject(window), m_window(window) {}

void StatusBarController::updateTxIndicator(bool on)
{
    if (!m_window->m_txIndicator)
    {
        return;
    }
    if (m_window->m_txActive == on && !m_window->m_txIndicator->styleSheet().isEmpty())
    {
        if (!on && m_window->m_txDurationTimer && m_window->m_txDurationTimer->isActive())
        {
            m_window->m_txDurationTimer->stop();
            if (m_window->m_titleBar)
            {
                m_window->m_titleBar->setTxDurationActive(false);
            }
        }
        return;
    }
    m_window->m_txActive = on;
    m_window->updateIcomRC28Leds();
    if (on)
    {
        m_window->m_txIndicator->setStyleSheet(statusLabelStyle(UiTheme::Color::Danger, true));
        if (m_window->m_txSwrLabel)
        {
            m_window->m_txSwrLabel->setText(
                QStringLiteral("<span style='color:%1'>SWR --</span>").arg(UiTheme::Color::TextStatusSecondary));
        }
        m_window->m_txElapsed.start();
        updateTxDurationLabel();
        if (m_window->m_txDurationTimer)
        {
            m_window->m_txDurationTimer->start();
        }
    }
    else
    {
        m_window->m_txIndicator->setStyleSheet(statusLabelStyle(UiTheme::Color::TextStatusSecondary, true));
        if (m_window->m_txDurationTimer)
        {
            m_window->m_txDurationTimer->stop();
        }
        if (m_window->m_txSwrLabel)
        {
            m_window->m_txSwrLabel->setText(
                QStringLiteral("<span style='color:%1'>SWR --</span>").arg(UiTheme::Color::TextStatusLabel));
        }
        if (m_window->m_titleBar)
        {
            m_window->m_titleBar->setTxDurationActive(false);
        }
        m_window->m_meterSnapshot.powerWatts = 0.0;
        m_window->m_meterSnapshot.swr = 1.0;
        m_window->m_meterSnapshot.alc = 0.0;
        m_window->m_meterSnapshot.compressionDb = 0.0;
        m_window->m_meterSnapshot.voltageVolts = 0.0;
        m_window->m_meterSnapshot.currentAmps = 0.0;
        m_window->m_meterSnapshot.powerValid = false;
        m_window->m_meterSnapshot.swrValid = false;
        m_window->m_meterSnapshot.alcValid = false;
        m_window->m_meterSnapshot.compressionValid = false;
        m_window->m_meterSnapshot.voltageValid = false;
        m_window->m_meterSnapshot.currentValid = false;
        m_window->m_meterSnapshot.txAudioPeak = 0;
        m_window->m_meterSnapshot.txAudioRms = 0;
        if (m_window->m_metersDialog)
        {
            m_window->m_metersDialog->resetMeters();
            if (m_window->m_model && m_window->m_model->isReady())
            {
                m_window->m_metersDialog->setSMeter(m_window->m_meterSnapshot.sMeter);
            }
        }
    }
}

void StatusBarController::updateTxDurationLabel()
{
    if (!m_window->m_titleBar)
    {
        return;
    }

    const qint64 secs = m_window->m_txElapsed.elapsed() / 1000;
    const int h = int(secs / 3600);
    const int m = int((secs % 3600) / 60);
    const int s = int(secs % 60);
    m_window->m_titleBar->setTxDuration(QStringLiteral("%1:%2:%3")
                                            .arg(h, 2, 10, QLatin1Char('0'))
                                            .arg(m, 2, 10, QLatin1Char('0'))
                                            .arg(s, 2, 10, QLatin1Char('0')),
                                        m_window->m_txActive);
}

void StatusBarController::updateStatusClock()
{
    if (!m_window->m_dateLabel || !m_window->m_timeLabel)
    {
        return;
    }

    const QDateTime now = m_window->m_statusClockUtc ? QDateTime::currentDateTimeUtc() : QDateTime::currentDateTime();
    m_window->m_dateLabel->setText(now.toString("yyyy-MM-dd"));
    m_window->m_timeLabel->setText(m_window->m_statusClockUtc ? now.toString("HH:mm:ss") + "Z"
                                                              : now.toString("HH:mm:ss"));

    const QString tooltip = m_window->m_statusClockUtc ? QStringLiteral("UTC Time Mode\nClick to show local time.")
                                                       : QStringLiteral("Local Time Mode\nClick to show UTC time.");
    m_window->m_dateLabel->setToolTip(tooltip);
    m_window->m_timeLabel->setToolTip(tooltip);
}

void StatusBarController::toggleStatusClockMode()
{
    m_window->m_statusClockUtc = !m_window->m_statusClockUtc;
    AppSettings::instance().setValue("statusClockUTC", m_window->m_statusClockUtc);
    updateStatusClock();
}

void StatusBarController::updateSystemStats()
{
    if (!m_window->m_cpuLabel || !m_window->m_memLabel)
    {
        return;
    }

    auto cpuColor = [](int pct) -> const char*
    {
        if (pct < 50)
        {
            return UiTheme::Color::TextStatusSecondary;
        }
        if (pct < 80)
        {
            return UiTheme::Color::Warning;
        }
        return UiTheme::Color::Danger;
    };

    const SystemStats stats = m_systemStatsProvider.sample();
    if (stats.cpuPercent)
    {
        const int cpuPercent = static_cast<int>(*stats.cpuPercent);
        m_window->m_cpuLabel->setText(
            QStringLiteral("<span style='color:%1'>%2%</span>")
                .arg(QLatin1String(cpuColor(cpuPercent)), QString::number(*stats.cpuPercent, 'f', 1)));
    }
    if (stats.processResidentBytes)
    {
        const double rssMb = static_cast<double>(*stats.processResidentBytes) / (1024.0 * 1024.0);
        const QString rssText = rssMb >= 1024.0 ? QStringLiteral("%1G").arg(rssMb / 1024.0, 0, 'f', 1)
                                                : QStringLiteral("%1M").arg(static_cast<int>(rssMb));
        m_window->m_memLabel->setText(QStringLiteral("<span style='color:%1'>%2</span>")
                                          .arg(QLatin1String(UiTheme::Color::TextStatusSecondary), rssText));
    }
}

void StatusBarController::buildStatusBar()
{
    m_window->statusBar()->setFixedHeight(46);
    m_window->statusBar()->setSizeGripEnabled(false);
    QPalette statusPalette = m_window->statusBar()->palette();
    statusPalette.setColor(QPalette::Window, QColor(QString::fromLatin1(UiTheme::Color::WindowChrome)));
    m_window->statusBar()->setPalette(statusPalette);
    m_window->statusBar()->setAutoFillBackground(true);
    m_window->statusBar()->setStyleSheet(QStringLiteral("QStatusBar { background: %1; border-top: 1px solid %2; }"
                                                        "QStatusBar::item { border: none; }"
                                                        "QLabel { background: transparent; }")
                                             .arg(UiTheme::Color::WindowChrome, UiTheme::Color::StatusBorder));

    auto* container = new QWidget(m_window);
    container->setObjectName(QStringLiteral("statusBarContent"));
    container->setStyleSheet(
        QStringLiteral("QWidget#statusBarContent { background: %1; }").arg(UiTheme::Color::WindowChrome));
    auto* hbox = new QHBoxLayout(container);
    hbox->setContentsMargins(6, 0, 6, 0);
    hbox->setSpacing(0);

    auto makeSep = [this]() -> QLabel*
    {
        auto* s = new QLabel(QStringLiteral("·"), m_window);
        s->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 21px; }").arg("#7a8fa0"));
        s->setAlignment(Qt::AlignCenter);
        s->setFixedWidth(UiTheme::Size::StatusSeparatorWidth);
        return s;
    };

    // Measure all possible label strings at the label font size, then use the
    // widest result (+ padding) as the uniform width for every stack.
    const int uniformStackWidth = [&]()
    {
        QFont regular;
        regular.setPixelSize(12);
        QFont bold = regular;
        bold.setBold(true);
        const QFontMetrics fmR(regular);
        const QFontMetrics fmB(bold);

        int w = 0;
        // Connection-status stack: the regular-weight state text and bold
        // category label share this width so state changes do not move the
        // separators or neighboring status groups.
        for (const char* s : {"Reconnecting", "Connected", "Disconnected"})
        {
            w = qMax(w, fmR.horizontalAdvance(QString::fromLatin1(s)));
        }
        w = qMax(w, fmB.horizontalAdvance(QStringLiteral("Radio")));
        // Network-quality stack. Include every operator-visible quality label
        // because the active page changes as latency and loss measurements
        // cross their thresholds.
        for (const char* s : {"Excellent", "Good", "Fair", "Poor"})
        {
            w = qMax(w, fmR.horizontalAdvance(QString::fromLatin1(s)));
        }
        w = qMax(w, fmB.horizontalAdvance(QStringLiteral("Network")));
        // processor stack
        w = qMax(w, fmR.horizontalAdvance(QStringLiteral("100.0%")));
        w = qMax(w, fmB.horizontalAdvance(QStringLiteral("Processor")));
        // memory stack
        w = qMax(w, fmR.horizontalAdvance(QStringLiteral("999M")));
        w = qMax(w, fmR.horizontalAdvance(QStringLiteral("1.0G")));
        w = qMax(w, fmB.horizontalAdvance(QStringLiteral("Memory")));
        // TX stack
        w = qMax(w, fmR.horizontalAdvance(QStringLiteral("SWR 9.99")));
        w = qMax(w, fmB.horizontalAdvance(QStringLiteral("TX")));
        // time stack
        w = qMax(w, fmR.horizontalAdvance(QStringLiteral("0000-00-00")));
        w = qMax(w, fmR.horizontalAdvance(QStringLiteral("00:00:00Z")));

        return w + 16; // uniform padding buffer
    }();

    const int txStackWidth = [&]()
    {
        QFont regular;
        regular.setPixelSize(12);
        QFont bold = regular;
        bold.setBold(true);
        const QFontMetrics fmR(regular);
        const QFontMetrics fmB(bold);

        int w = fmR.horizontalAdvance(QStringLiteral("SWR 9.99"));
        w = qMax(w, fmB.horizontalAdvance(QStringLiteral("TX")));
        return w + 16;
    }();

    auto applyStatusContainerWidth = [](QWidget* widget, int width)
    {
        widget->setMinimumWidth(width);
        widget->setMaximumWidth(width);
    };

    auto* transmitStatusPanel = new QWidget(m_window);
    applyStatusContainerWidth(transmitStatusPanel, txStackWidth);
    transmitStatusPanel->setAccessibleName("Transmit status");
    transmitStatusPanel->setAccessibleDescription("Shows transmit state and SWR.");
    const QString txTooltip = QStringLiteral("Transmit status and SWR.");
    transmitStatusPanel->setToolTip(txTooltip);
    auto* transmitStatusLayout = new QVBoxLayout(transmitStatusPanel);
    transmitStatusLayout->setContentsMargins(0, 0, 0, 0);
    transmitStatusLayout->setSpacing(0);
    transmitStatusLayout->setAlignment(Qt::AlignVCenter);

    m_window->m_txIndicator = new QLabel(QStringLiteral("TX"), m_window);
    m_window->m_txIndicator->setAlignment(Qt::AlignCenter);
    m_window->m_txIndicator->setToolTip(txTooltip);
    updateTxIndicator(false);

    m_window->m_txSwrLabel = new QLabel(
        QStringLiteral("<span style='color:%1'>SWR --</span>").arg(UiTheme::Color::TextStatusLabel), m_window);
    m_window->m_txSwrLabel->setTextFormat(Qt::RichText);
    m_window->m_txSwrLabel->setStyleSheet(statusLabelStyle(UiTheme::Color::TextStatusSecondary));
    m_window->m_txSwrLabel->setAlignment(Qt::AlignCenter);
    m_window->m_txSwrLabel->setToolTip(QStringLiteral("Transmit SWR from the radio."));

    transmitStatusLayout->addWidget(m_window->m_txIndicator);
    transmitStatusLayout->addWidget(m_window->m_txSwrLabel);
    hbox->addWidget(transmitStatusPanel);
    hbox->addSpacing(16);

    m_window->m_txDurationTimer = new QTimer(m_window);
    m_window->m_txDurationTimer->setInterval(250);
    connect(m_window->m_txDurationTimer, &QTimer::timeout, this, &StatusBarController::updateTxDurationLabel);

    m_window->m_toastLabel = new QLabel(QString(), m_window);
    m_window->m_toastLabel->setObjectName(QStringLiteral("statusToastLabel"));
    m_window->m_toastLabel->setStyleSheet(statusLabelStyle(UiTheme::Color::TextStatusPrimary));
    m_window->m_toastLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    hbox->addWidget(m_window->m_toastLabel);
    m_window->m_statusLabel = m_window->m_toastLabel;

    hbox->addStretch(1);

    // The bridge is opt-in at process startup, and this compact indicator is
    // visible for the lifetime of an automation-enabled session. The orange
    // robot is intentionally distinct from radio/network status: it reports
    // that a local control surface is available, not radio readiness.
    m_window->m_automationIndicatorContainer = new QWidget(m_window);
    auto* automationLayout = new QHBoxLayout(m_window->m_automationIndicatorContainer);
    automationLayout->setContentsMargins(0, 0, 8, 0);
    automationLayout->setSpacing(0);
    m_window->m_automationIndicator = new QLabel(QStringLiteral("\U0001F916"), m_window);
    m_window->m_automationIndicator->setAlignment(Qt::AlignCenter);
    m_window->m_automationIndicator->setFixedSize(28, 28);
    m_window->m_automationIndicator->setAccessibleName(QStringLiteral("Automation client connected"));
    m_window->m_automationIndicator->setStyleSheet(QStringLiteral(
        "QLabel { background: #f0a000; color: #0b0e12; border-radius: 4px; font-size: 16px; font-weight: bold; }"));
    automationLayout->addWidget(m_window->m_automationIndicator);
    m_window->m_automationIndicatorContainer->hide();
    hbox->addWidget(m_window->m_automationIndicatorContainer);

    auto* connectionStatusPanel = new ClickableStatusPanel(m_window);
    applyStatusContainerWidth(connectionStatusPanel, uniformStackWidth);
    auto* connectionStatusLayout = new QVBoxLayout(connectionStatusPanel);
    connectionStatusLayout->setContentsMargins(0, 0, 0, 0);
    connectionStatusLayout->setSpacing(0);
    connectionStatusLayout->setAlignment(Qt::AlignVCenter);

    m_window->m_connDetailLabel = new QLabel(QStringLiteral("Radio"), m_window);
    m_window->m_connDetailLabel->setStyleSheet(statusLabelStyle(UiTheme::Color::TextStatusSecondary, true));
    m_window->m_connDetailLabel->setAlignment(Qt::AlignCenter);

    m_window->m_connStateName = QStringLiteral("Disconnected");
    m_window->m_connStateLabel = new QLabel(
        QStringLiteral("<span style='color:%1'>Disconnected</span>").arg(UiTheme::Color::TextStatusLabel), m_window);
    m_window->m_connStateLabel->setTextFormat(Qt::RichText);
    m_window->m_connStateLabel->setStyleSheet(statusLabelStyle(UiTheme::Color::TextStatusSecondary));
    m_window->m_connStateLabel->setAlignment(Qt::AlignCenter);

    connectionStatusPanel->setCursor(Qt::PointingHandCursor);
    connectionStatusPanel->setAccessibleName("Radio connection");
    connectionStatusPanel->setAccessibleDescription("Click to open the radio chooser.");
    connectionStatusPanel->onClicked = [this]() { m_window->showRadioChooserDialog(); };

    connectionStatusLayout->addWidget(m_window->m_connDetailLabel);
    connectionStatusLayout->addWidget(m_window->m_connStateLabel);
    hbox->addWidget(connectionStatusPanel);
    m_window->updateConnectionTooltip();

    hbox->addWidget(makeSep());

    auto* networkStatusPanel = new QWidget(m_window);
    applyStatusContainerWidth(networkStatusPanel, uniformStackWidth);
    auto* networkStatusLayout = new QVBoxLayout(networkStatusPanel);
    networkStatusLayout->setContentsMargins(0, 0, 0, 0);
    networkStatusLayout->setSpacing(0);
    networkStatusLayout->setAlignment(Qt::AlignVCenter);
    m_window->m_netTitleLabel = new QLabel(QStringLiteral("Network"), m_window);
    m_window->m_netTitleLabel->setStyleSheet(statusLabelStyle(UiTheme::Color::TextStatusSecondary, true));
    m_window->m_netTitleLabel->setAlignment(Qt::AlignCenter);
    m_window->m_netQualLabel = new QLabel("—", m_window);
    m_window->m_netQualLabel->setStyleSheet(statusLabelStyle(UiTheme::Color::TextStatusSecondary));
    m_window->m_netQualLabel->setAlignment(Qt::AlignCenter);
    m_window->m_netQualLabel->setTextFormat(Qt::RichText);
    networkStatusLayout->addWidget(m_window->m_netTitleLabel);
    networkStatusLayout->addWidget(m_window->m_netQualLabel);
    hbox->addWidget(networkStatusPanel);

    hbox->addWidget(makeSep());

    auto makeStatusTitle = [this](const QString& title) -> QLabel*
    {
        auto* label = new QLabel(title, m_window);
        label->setStyleSheet(statusLabelStyle(UiTheme::Color::TextStatusSecondary, true));
        label->setAlignment(Qt::AlignCenter);
        return label;
    };

    auto makeStatusValue = [this]() -> QLabel*
    {
        auto* label = new QLabel(QStringLiteral("—"), m_window);
        label->setAlignment(Qt::AlignCenter);
        label->setTextFormat(Qt::RichText);
        label->setStyleSheet(statusLabelStyle(UiTheme::Color::TextStatusSecondary));
        return label;
    };

    // Processor stack
    {
        auto* cpuStatusPanel = new QWidget(m_window);
        applyStatusContainerWidth(cpuStatusPanel, uniformStackWidth);
        auto* cpuStatusLayout = new QVBoxLayout(cpuStatusPanel);
        cpuStatusLayout->setContentsMargins(0, 0, 0, 0);
        cpuStatusLayout->setSpacing(0);
        cpuStatusLayout->setAlignment(Qt::AlignVCenter);

        auto* const cpuTitleLabel = makeStatusTitle(QStringLiteral("Processor"));
        m_window->m_cpuLabel = makeStatusValue();
        cpuStatusLayout->addWidget(cpuTitleLabel);
        cpuStatusLayout->addWidget(m_window->m_cpuLabel);
        hbox->addWidget(cpuStatusPanel);
    }

    hbox->addWidget(makeSep());

    // Memory stack
    {
        auto* memoryStatusPanel = new QWidget(m_window);
        applyStatusContainerWidth(memoryStatusPanel, uniformStackWidth);
        auto* memoryStatusLayout = new QVBoxLayout(memoryStatusPanel);
        memoryStatusLayout->setContentsMargins(0, 0, 0, 0);
        memoryStatusLayout->setSpacing(0);
        memoryStatusLayout->setAlignment(Qt::AlignVCenter);

        auto* const memoryTitleLabel = makeStatusTitle(QStringLiteral("Memory"));
        m_window->m_memLabel = makeStatusValue();
        memoryStatusLayout->addWidget(memoryTitleLabel);
        memoryStatusLayout->addWidget(m_window->m_memLabel);
        hbox->addWidget(memoryStatusPanel);
    }

    hbox->addWidget(makeSep());
    hbox->addSpacing(kClockSeparatorGap);

    auto* clockStatusPanel = new ClickableStatusPanel(m_window);
    applyStatusContainerWidth(clockStatusPanel, uniformStackWidth);
    clockStatusPanel->setCursor(Qt::PointingHandCursor);
    clockStatusPanel->setAccessibleName("Status bar clock");
    clockStatusPanel->setAccessibleDescription("Click to switch between UTC and local time.");
    clockStatusPanel->onClicked = [this]() { toggleStatusClockMode(); };
    auto* clockStatusLayout = new QVBoxLayout(clockStatusPanel);
    clockStatusLayout->setContentsMargins(0, 0, 0, 0);
    clockStatusLayout->setSpacing(0);
    clockStatusLayout->setAlignment(Qt::AlignVCenter);

    m_window->m_dateLabel = new QLabel(QString(), m_window);
    m_window->m_dateLabel->setObjectName(QStringLiteral("statusDateLabel"));
    m_window->m_dateLabel->setStyleSheet(statusLabelStyle(UiTheme::Color::TextStatusSecondary));
    m_window->m_dateLabel->setAlignment(Qt::AlignCenter);
    m_window->m_timeLabel = new QLabel(QString(), m_window);
    m_window->m_timeLabel->setObjectName(QStringLiteral("statusTimeLabel"));
    m_window->m_timeLabel->setStyleSheet(statusLabelStyle(UiTheme::Color::TextStatusSecondary));
    m_window->m_timeLabel->setAlignment(Qt::AlignCenter);

    clockStatusLayout->addWidget(m_window->m_dateLabel);
    clockStatusLayout->addWidget(m_window->m_timeLabel);
    hbox->addWidget(clockStatusPanel);

    // Never use showMessage(); it hides permanent widgets. All transient
    // messages go through showToast() which overlays m_window->m_statusLabel directly.
    m_window->statusBar()->addWidget(container, 1);

    // Toast timer restores connection status after a toast expires.
    m_window->m_toastTimer = new QTimer(m_window);
    m_window->m_toastTimer->setSingleShot(true);
    connect(m_window->m_toastTimer, &QTimer::timeout, this,
            [this]()
            {
                if (m_window->m_toastLabel)
                {
                    applyToast(m_persistentMessage, m_persistentKind);
                }
            });

    // AppSettings stores booleans as "True"/"False" strings per CONVENTIONS.md.
    m_window->m_statusClockUtc =
        AppSettings::instance().value("statusClockUTC", "True").toString().compare("True", Qt::CaseInsensitive) == 0;
    updateStatusClock();
    updateNetworkQuality(0);
    auto* clockTimer = new QTimer(m_window);
    connect(clockTimer, &QTimer::timeout, this, &StatusBarController::updateStatusClock);
    clockTimer->start(1000);

    auto* sysStatsTimer = new QTimer(m_window);
    connect(sysStatsTimer, &QTimer::timeout, this, &StatusBarController::updateSystemStats);
    sysStatsTimer->start(2000);
    updateSystemStats();
}

void StatusBarController::setAutomationClientCount(int count)
{
    if (!m_window->m_automationIndicator)
    {
        return;
    }
    m_window->m_automationIndicator->setToolTip(
        QStringLiteral("Automation enabled; %1 local client%2 connected.\nTransmit controls are unavailable.")
            .arg(count)
            .arg(count == 1 ? QString() : QStringLiteral("s")));
}

void StatusBarController::setAutomationEnabled(bool enabled)
{
    if (!m_window->m_automationIndicatorContainer)
    {
        return;
    }
    m_window->m_automationIndicatorContainer->setVisible(enabled);
    if (enabled)
    {
        setAutomationClientCount(0);
    }
}

void StatusBarController::showToast(const QString& msg, int durationMs)
{
    showToast(msg, durationMs, MainWindow::ToastKind::Info);
}

void StatusBarController::showToast(const QString& msg, int durationMs, MainWindow::ToastKind kind)
{
    if (!m_window->m_toastLabel || !m_window->m_toastTimer)
    {
        return;
    }

    const QString message = normalizedToastMessage(msg);
    if (durationMs <= 0)
    {
        m_persistentMessage = message;
        m_persistentKind = kind;
    }

    applyToast(message, kind);
    m_window->m_toastTimer->stop();
    if (durationMs > 0)
    {
        m_window->m_toastTimer->start(durationMs);
    }
}

void StatusBarController::clearPersistentToast(const QString& expectedMessage)
{
    if (m_persistentMessage != normalizedToastMessage(expectedMessage))
    {
        return;
    }

    m_persistentMessage.clear();
    m_persistentKind = MainWindow::ToastKind::Info;
    if (!m_window->m_toastTimer || !m_window->m_toastTimer->isActive())
    {
        applyToast(QString(), MainWindow::ToastKind::Info);
    }
}

void StatusBarController::applyToast(const QString& message, MainWindow::ToastKind kind)
{
    const char* color = UiTheme::Color::TextStatusPrimary;
    bool bold = false;
    if (kind == MainWindow::ToastKind::Warning)
    {
        color = UiTheme::Color::Warning;
        bold = true;
    }
    else if (kind == MainWindow::ToastKind::Error)
    {
        color = UiTheme::Color::Danger;
        bold = true;
    }

    m_window->m_toastLabel->setText(message);
    m_window->m_toastLabel->setStyleSheet(statusLabelStyle(color, bold));
}

void StatusBarController::updateNetworkQuality(int rttMs)
{
    if (!m_window->m_netQualLabel)
    {
        return;
    }
    QString label, color;
    if (rttMs <= 0)
    {
        label = "—";
        color = UiTheme::Color::TextStatusLabel;
    }
    else if (rttMs < 20)
    {
        label = "Excellent";
        color = UiTheme::Color::Success;
    }
    else if (rttMs < 50)
    {
        label = "Good";
        color = UiTheme::Color::Accent;
    }
    else if (rttMs < 100)
    {
        label = "Fair";
        color = UiTheme::Color::Warning;
    }
    else
    {
        label = "Poor";
        color = UiTheme::Color::Danger;
    }

    const QString text = QStringLiteral("<span style='color:%1'>%2</span>").arg(color, label);
    m_window->m_netQualLabel->setText(text);
    const QString tooltip = rttMs > 0 ? QStringLiteral("Network Performance\nRTT: %1 ms").arg(rttMs)
                                      : QStringLiteral("Network Performance\nRTT: unavailable");
    if (m_window->m_netTitleLabel)
    {
        m_window->m_netTitleLabel->setToolTip(tooltip);
    }
    m_window->m_netQualLabel->setToolTip(tooltip);
}
