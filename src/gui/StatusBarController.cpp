#include "StatusBarController.h"

#include "AppSettings.h"
#include "MainTitleBar.h"
#include "MainWindowHelpers.h"
#include "MetersDialog.h"
#include "UiTheme.h"
#include "VfoPanel.h"
#include "models/RadioModel.h"

#include <QFile>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <numeric>

using namespace sdr9700::ui::main_window;


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
    if (m_window->m_vfoPanel)
    {
        m_window->m_vfoPanel->setTransmitPowerMode(on);
        if (on)
        {
            m_window->m_vfoPanel->setTransmitPowerMeter(0.0);
        }
        else
        {
            m_window->m_vfoPanel->setSMeterValue(
                qBound(0, static_cast<int>(m_window->m_meterSnapshot.sMeter * 100 / 255), 100));
        }
    }
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

    // CPU usage from /proc/stat — delta between two calls
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

    {
        QFile f(QStringLiteral("/proc/stat"));
        if (f.open(QIODevice::ReadOnly))
        {
            const QByteArray line = f.readLine();
            const QList<QByteArray> parts = line.split(' ');
            // Format: cpu  user nice system idle iowait irq softirq steal ...
            // Leading spaces mean parts[1] may be empty; filter empties
            QList<quint64> vals;
            for (const QByteArray& p : parts)
            {
                if (!p.isEmpty() && p != "cpu")
                {
                    vals.append(p.trimmed().toULongLong());
                }
            }

            if (vals.size() >= 4)
            {
                const quint64 idle = vals[3] + (vals.size() > 4 ? vals[4] : 0); // idle + iowait
                const quint64 total = std::accumulate(vals.cbegin(), vals.cend(), quint64{0});

                double cpuPct = 0.0;
                if (m_window->m_prevCpuTotal > 0 && total > m_window->m_prevCpuTotal)
                {
                    const quint64 dTotal = total - m_window->m_prevCpuTotal;
                    const quint64 dIdle = idle - m_window->m_prevCpuIdle;
                    cpuPct = 100.0 * static_cast<double>(dTotal - dIdle) / static_cast<double>(dTotal);
                    cpuPct = qBound(0.0, cpuPct, 100.0);
                }
                m_window->m_prevCpuTotal = total;
                m_window->m_prevCpuIdle = idle;

                const int cpuPctInt = static_cast<int>(cpuPct);
                m_window->m_cpuLabel->setText(
                    QStringLiteral("<span style='color:%1'>%2%</span>")
                        .arg(QLatin1String(cpuColor(cpuPctInt)), QString::number(cpuPct, 'f', 1)));
            }
        }
    }

    // Process RSS from /proc/self/status (VmRSS field)
    {
        QFile f(QStringLiteral("/proc/self/status"));
        if (f.open(QIODevice::ReadOnly))
        {
            const QString content = QString::fromLatin1(f.readAll());
            const QStringList lines = content.split('\n');
            const auto lineIt = std::find_if(lines.cbegin(), lines.cend(), [](const QString& line)
                                             { return line.startsWith(QLatin1String("VmRSS:")); });
            if (lineIt != lines.cend())
            {
                const QStringList parts = lineIt->simplified().split(' ', Qt::SkipEmptyParts);
                if (parts.size() >= 2)
                {
                    const double rssGb = parts[1].toDouble() / (1024.0 * 1024.0);
                    const QString rssStr = rssGb >= 1.0 ? QStringLiteral("%1G").arg(rssGb, 0, 'f', 1)
                                                        : QStringLiteral("%1M").arg(static_cast<int>(rssGb * 1024));
                    m_window->m_memLabel->setText(QStringLiteral("<span style='color:%1'>%2</span>")
                                                      .arg(QLatin1String(UiTheme::Color::TextStatusSecondary), rssStr));
                }
            }
        }
    }
}

void StatusBarController::buildStatusBar()
{
    m_window->statusBar()->setFixedHeight(46);
    m_window->statusBar()->setSizeGripEnabled(false);
    m_window->statusBar()->setStyleSheet(QStringLiteral("QStatusBar { background: %1; border-top: 1px solid %2; }"
                                                        "QStatusBar::item { border: none; }"
                                                        "QLabel { background: transparent; }")
                                             .arg(UiTheme::Color::MenuBar, UiTheme::Color::StatusBorder));

    auto* container = new QWidget(m_window);
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
        // connection stack
        for (const char* s : {"Reconnecting", "Connected", "Disconnected"})
        {
            w = qMax(w, fmR.horizontalAdvance(QString::fromLatin1(s)));
        }
        w = qMax(w, fmB.horizontalAdvance(QStringLiteral("Radio")));
        // network stack
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
    m_window->m_toastLabel->setStyleSheet(statusLabelStyle(UiTheme::Color::TextStatusPrimary));
    m_window->m_toastLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    hbox->addWidget(m_window->m_toastLabel);
    m_window->m_statusLabel = m_window->m_toastLabel;

    hbox->addStretch(1);

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

        auto* cpuTitleLabel = makeStatusTitle(QStringLiteral("Processor"));
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

        auto* memoryTitleLabel = makeStatusTitle(QStringLiteral("Memory"));
        m_window->m_memLabel = makeStatusValue();
        memoryStatusLayout->addWidget(memoryTitleLabel);
        memoryStatusLayout->addWidget(m_window->m_memLabel);
        hbox->addWidget(memoryStatusPanel);
    }

    hbox->addWidget(makeSep());

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
    m_window->m_dateLabel->setStyleSheet(statusLabelStyle(UiTheme::Color::TextStatusSecondary));
    m_window->m_dateLabel->setAlignment(Qt::AlignCenter);
    m_window->m_timeLabel = new QLabel(QString(), m_window);
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

void StatusBarController::showToast(const QString& msg, int durationMs, MainWindow::ToastKind kind)
{
    if (!m_window->m_toastLabel || !m_window->m_toastTimer)
    {
        return;
    }

    if (durationMs <= 0)
    {
        m_persistentMessage = msg;
        m_persistentKind = kind;
    }

    applyToast(msg, kind);
    m_window->m_toastTimer->stop();
    if (durationMs > 0)
    {
        m_window->m_toastTimer->start(durationMs);
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
