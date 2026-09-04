#include "ApplicationLogDialog.h"
#include "ApplicationLog.h"
#include "DialogFooter.h"
#include "LogCategories.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
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
    auto* buttonBox = dialog.findChild<QDialogButtonBox*>(QStringLiteral("dialogButtonBox"));
    auto* footerSeparator = dialog.findChild<QWidget*>(QStringLiteral("dialogFooterSeparator"));
    auto* footerRow = dialog.findChild<QWidget*>(QStringLiteral("dialogFooterRow"));
    QVERIFY(pauseButton != nullptr);
    QVERIFY(clearButton != nullptr);
    QVERIFY(civCheckBox != nullptr);
    QCOMPARE(civCheckBox->text(), QStringLiteral("Report CI-V Traffic"));
    QCOMPARE(civCheckBox->accessibleDescription(), QStringLiteral("Include raw CI-V traffic in the application log."));
    QVERIFY(categoryCombo != nullptr);
    QCOMPARE(categoryCombo->currentText(), QStringLiteral("All categories"));
    QVERIFY(categoryCombo->minimumWidth() >= 190);
    QVERIFY(logView != nullptr);
    QVERIFY(buttonBox != nullptr);
    QVERIFY(buttonBox->button(QDialogButtonBox::Close) == nullptr);
    QVERIFY(footerSeparator != nullptr);
    QVERIFY(footerSeparator->isHidden());
    QVERIFY(footerRow != nullptr);
    QCOMPARE(footerRow->parentWidget()->sizePolicy().verticalPolicy(), QSizePolicy::Fixed);
    auto* footerRowLayout = qobject_cast<QHBoxLayout*>(footerRow->layout());
    QVERIFY(footerRowLayout != nullptr);
    QCOMPARE(footerRowLayout->contentsMargins().top(), 0);
    QCOMPARE(footerRowLayout->contentsMargins().bottom(), sdr9700::ui::kDialogFooterSpacing);
    QVERIFY(logView->document()->maximumBlockCount() > 0);
    auto* refreshTimer = dialog.findChild<QTimer*>(QStringLiteral("applicationLogRefreshTimer"));
    QVERIFY(refreshTimer != nullptr);
    QVERIFY(!refreshTimer->isActive());
    dialog.showCentered();
    QTRY_VERIFY(refreshTimer->isActive());
    QVERIFY(!civCheckBox->isChecked());
    QCOMPARE(logRadioTraffic().isDebugEnabled(), debugWasEnabled);
    QCOMPARE(logRadioTraffic().isInfoEnabled(), infoWasEnabled);
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

    const QMessageLogContext udpContext("test.cpp", 1, "test", "udp");
    const QMessageLogContext audioContext("test.cpp", 1, "test", "audio");
    const QMessageLogContext radioContext("test.cpp", 1, "test", "radio");
    const QMessageLogContext guiContext("test.cpp", 1, "test", "gui");
    ApplicationLog::instance().append(QtInfoMsg, udpContext, QStringLiteral("udp message"));
    ApplicationLog::instance().append(QtInfoMsg, audioContext, QStringLiteral("audio message"));
    ApplicationLog::instance().append(QtInfoMsg, radioContext, QStringLiteral("radio message"));
    ApplicationLog::instance().append(QtInfoMsg, guiContext, QStringLiteral("gui message"));
    QTRY_COMPARE(categoryCombo->count(), 5);
    QCOMPARE(categoryCombo->itemText(0), QStringLiteral("All categories"));
    QCOMPARE(categoryCombo->itemText(1), QStringLiteral("audio"));
    QCOMPARE(categoryCombo->itemText(2), QStringLiteral("gui"));
    QCOMPARE(categoryCombo->itemText(3), QStringLiteral("radio"));
    QCOMPARE(categoryCombo->itemText(4), QStringLiteral("udp"));

    clearButton->click();
    pauseButton->click();
    ApplicationLog::instance().append(QtInfoMsg, radioContext, QStringLiteral("second message while paused"));
    QTest::qWait(1100);
    QVERIFY(!logView->toPlainText().contains(QStringLiteral("second message while paused")));
    pauseButton->click();
    QVERIFY(logView->toPlainText().contains(QStringLiteral("second message while paused")));

    civCheckBox->setChecked(true);
    QVERIFY(logRadioTraffic().isDebugEnabled());
    QVERIFY(logRadioTraffic().isInfoEnabled());
    const QMessageLogContext civContext("test.cpp", 1, "test", "ci-v");
    ApplicationLog::instance().append(QtInfoMsg, civContext, QStringLiteral("retained CI-V message"));
    QTRY_VERIFY(logView->toPlainText().contains(QStringLiteral("retained CI-V message")));
    civCheckBox->setChecked(false);
    QCOMPARE(logRadioTraffic().isDebugEnabled(), debugWasEnabled);
    QCOMPARE(logRadioTraffic().isInfoEnabled(), infoWasEnabled);
    ApplicationLog::instance().append(QtInfoMsg, civContext, QStringLiteral("suppressed CI-V message"));
    QTest::qWait(1100);
    QVERIFY(!logView->toPlainText().contains(QStringLiteral("suppressed CI-V message")));
    dialog.hide();
    QVERIFY(!civCheckBox->isChecked());
    QCOMPARE(logRadioTraffic().isDebugEnabled(), debugWasEnabled);
    QCOMPARE(logRadioTraffic().isInfoEnabled(), infoWasEnabled);
    civCheckBox->setChecked(true);
    dialog.show();
    QVERIFY(!civCheckBox->isChecked());
    QCOMPARE(logRadioTraffic().isDebugEnabled(), debugWasEnabled);
    QCOMPARE(logRadioTraffic().isInfoEnabled(), infoWasEnabled);
    civCheckBox->setChecked(false);
}

QTEST_MAIN(ApplicationLogDialogTest)
#include "ApplicationLogDialogTest.moc"
