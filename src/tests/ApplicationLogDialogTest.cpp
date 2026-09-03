#include "ApplicationLogDialog.h"
#include "ApplicationLog.h"
#include "LogCategories.h"
#include "LoggingConfiguration.h"

#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QTimer>
#include <QSignalSpy>
#include <QVBoxLayout>
#include <QWidget>
#include <QtTest>

class ApplicationLogDialogTest : public QObject
{
    Q_OBJECT

  private slots:
    void remainsNonModalWhileVisible();
    void providesLiveLogControls();
};

void ApplicationLogDialogTest::remainsNonModalWhileVisible()
{
    QWidget host;
    auto* layout = new QVBoxLayout(&host);
    auto* hostButton = new QPushButton(QStringLiteral("Radio control"), &host);
    layout->addWidget(hostButton);
    host.show();

    ApplicationLogDialog dialog(&host);
    QCOMPARE(dialog.windowModality(), Qt::NonModal);
    QVERIFY(!dialog.isModal());
    dialog.showCentered();

    QTRY_VERIFY(dialog.isVisible());
    QVERIFY(host.isEnabled());
    QVERIFY(hostButton->isEnabled());
    QSignalSpy clickedSpy(hostButton, &QPushButton::clicked);
    hostButton->click();
    QCOMPARE(clickedSpy.count(), 1);
}

void ApplicationLogDialogTest::providesLiveLogControls()
{
    const bool debugWasEnabled = logRadioTraffic().isDebugEnabled();
    const bool infoWasEnabled = logRadioTraffic().isInfoEnabled();
    ApplicationLogDialog dialog;

    auto* pauseButton = dialog.findChild<QPushButton*>(QStringLiteral("applicationLogPauseButton"));
    auto* clearButton = dialog.findChild<QPushButton*>(QStringLiteral("applicationLogClearButton"));
    auto* civCheckBox = dialog.findChild<QCheckBox*>(QStringLiteral("includeCivLogCheckBox"));
    auto* categoryCombo = dialog.findChild<QComboBox*>(QStringLiteral("applicationLogCategoryCombo"));
    auto* logView = dialog.findChild<QPlainTextEdit*>();
    QVERIFY(pauseButton != nullptr);
    QVERIFY(clearButton != nullptr);
    QVERIFY(civCheckBox != nullptr);
    QVERIFY(categoryCombo != nullptr);
    QCOMPARE(categoryCombo->currentText(), QStringLiteral("All categories"));
    QVERIFY(categoryCombo->minimumWidth() >= 190);
    QVERIFY(logView != nullptr);
    QVERIFY(logView->document()->maximumBlockCount() > 0);
    auto* refreshTimer = dialog.findChild<QTimer*>(QStringLiteral("applicationLogRefreshTimer"));
    QVERIFY(refreshTimer != nullptr);
    QVERIFY(!refreshTimer->isActive());
    dialog.showCentered();
    QTRY_VERIFY(refreshTimer->isActive());
    QCOMPARE(pauseButton->text(), QStringLiteral("Pause"));
    pauseButton->click();
    QCOMPARE(pauseButton->text(), QStringLiteral("Resume"));
    QVERIFY(!refreshTimer->isActive());
    const QMessageLogContext context("test.cpp", 1, "test", "radio");
    ApplicationLog::instance().append(QtInfoMsg, context, QStringLiteral("message received while paused"));
    QTest::qWait(1100);
    QVERIFY(!logView->toPlainText().contains(QStringLiteral("message received while paused")));
    pauseButton->click();
    QCOMPARE(pauseButton->text(), QStringLiteral("Pause"));
    QVERIFY(refreshTimer->isActive());
    QVERIFY(logView->toPlainText().contains(QStringLiteral("message received while paused")));
    clearButton->click();
    QVERIFY(ApplicationLog::instance().entries().isEmpty());
    QVERIFY(logView->toPlainText().isEmpty());
    pauseButton->click();
    pauseButton->click();
    QVERIFY(!logView->toPlainText().contains(QStringLiteral("message received while paused")));

    civCheckBox->setChecked(true);
    QVERIFY(logRadioTraffic().isDebugEnabled());
    QVERIFY(logRadioTraffic().isInfoEnabled());
    civCheckBox->setChecked(false);
    QVERIFY(!logRadioTraffic().isDebugEnabled());
    QVERIFY(!logRadioTraffic().isInfoEnabled());

    LoggingConfiguration::setCivDataEnabled(debugWasEnabled || infoWasEnabled);
}

QTEST_MAIN(ApplicationLogDialogTest)
#include "ApplicationLogDialogTest.moc"
