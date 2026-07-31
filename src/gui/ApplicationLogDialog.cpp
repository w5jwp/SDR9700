#include "ApplicationLogDialog.h"
#include "ApplicationLog.h"
#include "LogCategories.h"
#include "LoggingConfiguration.h"
#include "UiTheme.h"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
constexpr QSize kApplicationLogWindowSize{1100, 700};
}

ApplicationLogDialog::ApplicationLogDialog(QWidget* parent)
    : sdr9700::ui::UtilityWindow(QStringLiteral("Application Log"), parent)
{
    setObjectName(QStringLiteral("applicationLogWindow"));
    setModal(false);
    setStyleSheet(QStringLiteral("ApplicationLogDialog { background: %1; border: 1px solid %2; }")
                      .arg(QLatin1String(UiTheme::Color::Panel), QLatin1String(UiTheme::Color::Border)));
    setFixedSize(kApplicationLogWindowSize);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* titleBar = new sdr9700::ui::UtilityTitleBar(QStringLiteral("Application Log"), this);
    connect(titleBar->closeButton(), &QPushButton::clicked, this, &QWidget::hide);
    root->addWidget(titleBar);

    auto* content = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(12, 10, 12, 12);
    contentLayout->setSpacing(8);
    root->addWidget(content, 1);

    auto* filterRow = new QHBoxLayout;
    auto* categoryLabel = new QLabel(QStringLiteral("Category:"), content);
    categoryLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(QLatin1String(UiTheme::Color::TextMuted)));
    filterRow->addWidget(categoryLabel);
    m_categoryCombo = new QComboBox(content);
    m_categoryCombo->setObjectName(QStringLiteral("applicationLogCategoryCombo"));
    m_categoryCombo->setAccessibleName(QStringLiteral("Log category filter"));
    m_categoryCombo->setMinimumWidth(190);
    m_categoryCombo->setStyleSheet(
        QStringLiteral("QComboBox { background: %1; border: 1px solid %2; border-radius: 3px;"
                       " color: %3; padding: 4px 28px 4px 8px; }"
                       "QComboBox:hover, QComboBox:focus { border-color: %4; }"
                       "QComboBox QAbstractItemView { background: %1; border: 1px solid %2; color: %3;"
                       " selection-background-color: %5; selection-color: %6; }")
            .arg(QLatin1String(UiTheme::Color::Field), QLatin1String(UiTheme::Color::BorderFocus),
                 QLatin1String(UiTheme::Color::TextField), QLatin1String(UiTheme::Color::AccentBright),
                 QLatin1String(UiTheme::Color::AccentDark), QLatin1String(UiTheme::Color::TextBright)));
    m_categoryCombo->addItem(QStringLiteral("All categories"), QString());
    filterRow->addWidget(m_categoryCombo);
    filterRow->addSpacing(32);
    auto* includeCivCheckBox = new QCheckBox(QStringLiteral("Include CI-V data"), content);
    includeCivCheckBox->setObjectName(QStringLiteral("includeCivLogCheckBox"));
    includeCivCheckBox->setAccessibleDescription(
        QStringLiteral("Include high-volume raw CI-V transmit and receive messages in the application log."));
    includeCivCheckBox->setStyleSheet(
        QStringLiteral("QCheckBox { color: %1; spacing: 8px; }"
                       "QCheckBox:focus { color: %2; }"
                       "QCheckBox::indicator { width: 16px; height: 16px; background: %3;"
                       " border: 2px solid %4; border-radius: 3px; }"
                       "QCheckBox::indicator:hover { border-color: %2; }"
                       "QCheckBox::indicator:checked { background: %5; border-color: %2; }")
            .arg(QLatin1String(UiTheme::Color::TextPrimary), QLatin1String(UiTheme::Color::AccentBright),
                 QLatin1String(UiTheme::Color::Field), QLatin1String(UiTheme::Color::BorderFocus),
                 QLatin1String(UiTheme::Color::Accent)));
    includeCivCheckBox->setChecked(logRadioTraffic().isDebugEnabled() || logRadioTraffic().isInfoEnabled());
    filterRow->addWidget(includeCivCheckBox);
    filterRow->addStretch();
    contentLayout->addLayout(filterRow);

    m_logView = new QPlainTextEdit(content);
    m_logView->setReadOnly(true);
    m_logView->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_logView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_logView->setAccessibleName(QStringLiteral("Application log messages"));
    m_logView->setStyleSheet(QStringLiteral("QPlainTextEdit { background: %1; border: 1px solid %2;"
                                            " border-radius: 3px; color: %3; padding: 6px; }")
                                 .arg(QLatin1String(UiTheme::Color::Field), QLatin1String(UiTheme::Color::Border),
                                      QLatin1String(UiTheme::Color::TextField)));
    contentLayout->addWidget(m_logView, 1);

    auto* footerSeparator = new QWidget(content);
    footerSeparator->setFixedHeight(1);
    footerSeparator->setStyleSheet(QStringLiteral("background: %1;").arg(QLatin1String(UiTheme::Color::BorderMedium)));
    contentLayout->addWidget(footerSeparator);

    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(0, 2, 0, 0);
    m_pauseButton = new QPushButton(QStringLiteral("Pause"), content);
    m_pauseButton->setObjectName(QStringLiteral("applicationLogPauseButton"));
    m_pauseButton->setAccessibleDescription(
        QStringLiteral("Freeze the displayed log while messages continue to be collected."));
    footer->addWidget(m_pauseButton);
    auto* clearButton = new QPushButton(QStringLiteral("Clear"), content);
    clearButton->setObjectName(QStringLiteral("applicationLogClearButton"));
    clearButton->setAccessibleDescription(QStringLiteral("Clear all retained application log messages."));
    footer->addWidget(clearButton);
    footer->addStretch();
    auto* exportButton = new QPushButton(QStringLiteral("Export…"), content);
    exportButton->setAccessibleName(QStringLiteral("Export application log"));
    auto* closeButton = new QPushButton(QStringLiteral("Close"), content);
    footer->addWidget(exportButton);
    footer->addWidget(closeButton);
    contentLayout->addLayout(footer);
    connect(exportButton, &QPushButton::clicked, this, &ApplicationLogDialog::exportLog);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::hide);
    connect(clearButton, &QPushButton::clicked, this,
            [this]()
            {
                ApplicationLog::instance().clear();
                m_logView->clear();
                const QSignalBlocker blocker(m_categoryCombo);
                m_categoryCombo->clear();
                m_categoryCombo->addItem(QStringLiteral("All categories"), QString());
            });
    connect(m_pauseButton, &QPushButton::clicked, this,
            [this]()
            {
                m_paused = !m_paused;
                m_pauseButton->setText(m_paused ? QStringLiteral("Resume") : QStringLiteral("Pause"));
                if (!m_paused)
                {
                    refreshLog();
                }
            });
    connect(includeCivCheckBox, &QCheckBox::toggled, this,
            [](bool enabled) { LoggingConfiguration::setCivDataEnabled(enabled); });

    connect(m_categoryCombo, &QComboBox::currentIndexChanged, this, &ApplicationLogDialog::refreshLog);
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(1000);
    connect(m_refreshTimer, &QTimer::timeout, this, &ApplicationLogDialog::refreshLog);
    m_refreshTimer->start();
    refreshLog();
}

QString ApplicationLogDialog::visibleLogText() const
{
    const QString selectedCategory = m_categoryCombo->currentData().toString();
    QStringList lines;
    for (const ApplicationLog::Entry& entry : ApplicationLog::instance().entries())
    {
        if (selectedCategory.isEmpty() || entry.category == selectedCategory)
        {
            lines.append(entry.text);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

void ApplicationLogDialog::refreshLog()
{
    if (m_paused)
    {
        return;
    }
    const QString selectedCategory = m_categoryCombo->currentData().toString();
    for (const QString& category : ApplicationLog::instance().categories())
    {
        if (m_categoryCombo->findData(category) < 0)
        {
            m_categoryCombo->addItem(category, category);
        }
    }

    const bool wasAtBottom = m_logView->verticalScrollBar()->value() >= m_logView->verticalScrollBar()->maximum();
    const QString text = visibleLogText();
    if (m_logView->toPlainText() != text)
    {
        m_logView->setPlainText(text);
        if (wasAtBottom)
        {
            m_logView->verticalScrollBar()->setValue(m_logView->verticalScrollBar()->maximum());
        }
    }
    const int selectedIndex = m_categoryCombo->findData(selectedCategory);
    if (selectedIndex >= 0 && selectedIndex != m_categoryCombo->currentIndex())
    {
        m_categoryCombo->setCurrentIndex(selectedIndex);
    }
}

void ApplicationLogDialog::exportLog()
{
    const QString defaultName = QStringLiteral("sdr9700-application-log-%1.txt")
                                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QString path =
        QFileDialog::getSaveFileName(this, QStringLiteral("Export Application Log"), QDir::home().filePath(defaultName),
                                     QStringLiteral("Text files (*.txt);;All files (*)"));
    if (path.isEmpty())
    {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        QMessageBox::warning(this, QStringLiteral("Export Application Log"),
                             QStringLiteral("Could not write the log file:\n%1").arg(file.errorString()));
        return;
    }
    QTextStream stream(&file);
    // Export exactly what the operator is viewing. In particular, a paused
    // display must not silently include messages that arrived after Pause.
    stream << m_logView->toPlainText();
    if (stream.status() != QTextStream::Ok)
    {
        QMessageBox::warning(this, QStringLiteral("Export Application Log"),
                             QStringLiteral("The log file could not be written completely."));
    }
}
