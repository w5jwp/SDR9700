#include "VfoController.h"
#include "VfoDisplay.h"
#include "VfoSelectionPanel.h"
#include "VfoSMeter.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QtTest>

class VfoDisplayTest : public QObject
{
    Q_OBJECT

  private slots:
    void controllersKeepIndependentIdentityAndFrequency();
    void selectionPanelPublishesRequestsAndAppliesConfirmedState();
};

void VfoDisplayTest::controllersKeepIndependentIdentityAndFrequency()
{
    QWidget parent;
    VfoController mainController(Vfo::Main, nullptr, &parent);
    VfoController subController(Vfo::Sub, nullptr, &parent);

    QCOMPARE(mainController.vfo(), Vfo::Main);
    QCOMPARE(subController.vfo(), Vfo::Sub);
    QCOMPARE(mainController.display()->frequencyText(), QStringLiteral("---.---.---"));
    QCOMPARE(subController.display()->frequencyText(), QStringLiteral("---.---.---"));

    QSignalSpy mainStatePublishedSpy(&mainController, &VfoController::statePublished);
    mainController.setFrequencyHz(145500000ULL);
    subController.setFrequencyHz(433920000ULL);
    QVERIFY(mainController.hasPublishedState());
    QCOMPARE(mainStatePublishedSpy.count(), 1);
    QCOMPARE(mainStatePublishedSpy.takeFirst().at(0).value<Vfo>(), Vfo::Main);

    QCOMPARE(mainController.display()->frequencyText(), QStringLiteral("145.500.000"));
    QCOMPARE(subController.display()->frequencyText(), QStringLiteral("433.920.000"));
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoIdentityButton"))->text(),
             QStringLiteral("MAIN"));
    QCOMPARE(subController.display()->findChild<QPushButton*>(QStringLiteral("vfoIdentityButton"))->text(),
             QStringLiteral("SUB"));
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoBandButton"))->text(),
             QStringLiteral("2M"));
    QCOMPARE(subController.display()->findChild<QPushButton*>(QStringLiteral("vfoBandButton"))->text(),
             QStringLiteral("70CM"));
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoModeButton"))->text(),
             QStringLiteral("--"));
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoBandButton"))->property("active"),
             QVariant(true));
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoModeButton"))->property("active"),
             QVariant(true));
    QCOMPARE(mainController.display()->findChild<QLabel*>(QStringLiteral("vfoTxBadge"))->text(), QStringLiteral("TX"));
    QVERIFY(subController.display()->findChild<QLabel*>(QStringLiteral("vfoTxBadge")) == nullptr);
    auto* mainSMeter = mainController.display()->findChild<VfoSMeter*>();
    auto* subSMeter = subController.display()->findChild<VfoSMeter*>();
    QVERIFY(mainSMeter != nullptr);
    QVERIFY(subSMeter != nullptr);
    mainController.display()->setSMeterValue(120);
    QCOMPARE(mainSMeter->accessibleDescription(), QStringLiteral("Signal strength S9"));
    mainController.display()->setSMeterValue(130);
    QCOMPARE(mainSMeter->accessibleDescription(), QStringLiteral("Signal strength S9+05"));
    mainController.display()->setSMeterValue(255);
    QCOMPARE(mainSMeter->accessibleDescription(), QStringLiteral("Signal strength S9+60"));
    QVERIFY(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoSQLButton")) != nullptr);
    QVERIFY(subController.display()->findChild<QPushButton*>(QStringLiteral("vfoSQLButton")) != nullptr);
    QVERIFY(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoTXPWRButton")) != nullptr);
    QVERIFY(subController.display()->findChild<QPushButton*>(QStringLiteral("vfoTXPWRButton")) == nullptr);
    QVERIFY(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoXFCButton")) != nullptr);
    QVERIFY(subController.display()->findChild<QPushButton*>(QStringLiteral("vfoXFCButton")) == nullptr);
    QVERIFY(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoCOMPButton")) != nullptr);
    QVERIFY(subController.display()->findChild<QPushButton*>(QStringLiteral("vfoCOMPButton")) == nullptr);
    auto* toneStateButton = mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoTONEButton"));
    auto* offsetStateButton = mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoOFFSETButton"));
    mainController.display()->setReceiverControlState(QStringLiteral("TONE"), QStringLiteral("TSQL 67.0"), true);
    mainController.display()->setReceiverControlState(QStringLiteral("OFFSET"), QStringLiteral("SIMPLEX"), false);
    mainController.display()->setReceiverControlState(QStringLiteral("PRE"), QString(), true);
    QCOMPARE(toneStateButton->text(), QStringLiteral("TSQL 67.0"));
    QCOMPARE(offsetStateButton->text(), QStringLiteral("SIMPLEX"));
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoPREButton"))->text(),
             QStringLiteral("P.AMP"));
    for (const QString& control :
         {QStringLiteral("AGC"), QStringLiteral("ATT"), QStringLiteral("NB"), QStringLiteral("NOTCH"),
          QStringLiteral("NR"), QStringLiteral("PRE"), QStringLiteral("RFG"), QStringLiteral("TONE"),
          QStringLiteral("OFFSET"), QStringLiteral("SQL"), QStringLiteral("TXPWR")})
    {
        QVERIFY(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfo%1Button").arg(control)) !=
                nullptr);
    }

    mainController.clearFrequency();
    QCOMPARE(mainController.display()->frequencyText(), QStringLiteral("---.---.---"));
    QCOMPARE(subController.display()->frequencyText(), QStringLiteral("433.920.000"));

    auto* mainFrequency = mainController.display()->findChild<QLineEdit*>(QStringLiteral("vfoFrequency"));
    QVERIFY(mainFrequency != nullptr);
    QVERIFY(!mainFrequency->isReadOnly());
    QSignalSpy submissionSpy(mainController.display(), &VfoDisplay::frequencySubmitted);
    mainFrequency->setText(QStringLiteral("not a frequency"));
    QVERIFY(QMetaObject::invokeMethod(mainFrequency, "returnPressed", Qt::DirectConnection));
    QCOMPARE(submissionSpy.count(), 1);
    QCOMPARE(mainController.display()->frequencyText(), QStringLiteral("---.---.---"));

    QSignalSpy vfoClickSpy(mainController.display(), &VfoDisplay::vfoClicked);
    mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoIdentityButton"))->click();
    QCOMPARE(vfoClickSpy.count(), 1);

    mainController.setSelected(true);
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoIdentityButton"))->property("active"),
             QVariant(true));
    mainController.setOperatingEnabled(false);
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoBandButton"))->property("active"),
             QVariant(false));
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoModeButton"))->property("active"),
             QVariant(false));
    for (const QString& control :
         {QStringLiteral("AGC"), QStringLiteral("ATT"), QStringLiteral("NB"), QStringLiteral("NOTCH"),
          QStringLiteral("NR"), QStringLiteral("PRE"), QStringLiteral("RFG"), QStringLiteral("TONE"),
          QStringLiteral("OFFSET"), QStringLiteral("SQL"), QStringLiteral("TXPWR")})
    {
        QVERIFY(!mainController.display()
                     ->findChild<QPushButton*>(QStringLiteral("vfo%1Button").arg(control))
                     ->isEnabled());
    }
    mainController.setOperatingEnabled(true);
    mainController.display()->setTransmitPowerWatts(42.0);
    mainController.setTransmitting(true);
    QCOMPARE(mainController.display()->findChild<QLabel*>(QStringLiteral("vfoTxBadge"))->property("transmitting"),
             QVariant(true));
    QCOMPARE(mainSMeter->accessibleName(), QStringLiteral("RF power meter"));
    QCOMPARE(mainSMeter->accessibleDescription(), QStringLiteral("RF power 0.0 watts"));
    mainController.display()->setTransmitPowerWatts(42.0);
    QCOMPARE(mainSMeter->accessibleDescription(), QStringLiteral("RF power 42.0 watts"));
    mainController.setTransmitting(false);
    QCOMPARE(mainSMeter->accessibleName(), QStringLiteral("Signal strength meter"));
    mainController.setTransmitting(true);
    QCOMPARE(mainSMeter->accessibleDescription(), QStringLiteral("RF power 0.0 watts"));
    mainController.setTransmitting(false);

    parent.resize(640, 240);
    mainController.display()->setGeometry(0, 0, 600, mainController.display()->height());
    mainController.display()->show();
    parent.show();
    QApplication::processEvents();
    const auto* txBadge = mainController.display()->findChild<QLabel*>(QStringLiteral("vfoTxBadge"));
    const auto* receiverButton = mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoAGCButton"));
    const auto* toneButton = mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoTONEButton"));
    QCOMPARE(toneButton->width(), 80);
    const auto* rightReceiverButton = mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoRFGButton"));
    const auto* sqlButton = mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoSQLButton"));
    const auto* bandButton = mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoBandButton"));
    const auto* modeButton = mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoModeButton"));
    const auto* identityButton = mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoIdentityButton"));
    const int renderedInset = identityButton->y();
    QCOMPARE(mainController.display()->height() - receiverButton->geometry().bottom() - 1, renderedInset);
    const int toneBottom = toneButton->mapTo(mainController.display(), QPoint(0, toneButton->height() - 1)).y();
    QCOMPARE(receiverButton->y() - toneBottom - 1, 40);
    QCOMPARE(identityButton->x(), renderedInset);
    QCOMPARE(mainController.display()->width() - rightReceiverButton->geometry().right() - 1, renderedInset);
    QCOMPARE(bandButton->x() - sqlButton->geometry().right() - 1, txBadge->x() - modeButton->geometry().right() - 1);
}

void VfoDisplayTest::selectionPanelPublishesRequestsAndAppliesConfirmedState()
{
    VfoSelectionPanel panel;
    QSignalSpy vfoSpy(&panel, &VfoSelectionPanel::vfoRequested);
    QSignalSpy dualWatchSpy(&panel, &VfoSelectionPanel::dualWatchRequested);
    QSignalSpy exchangeSpy(&panel, &VfoSelectionPanel::exchangeRequested);

    QCOMPARE(panel.selectedVfo(), Vfo::Main);
    QVERIFY(!panel.dualWatchEnabled());

    const auto buttons = panel.findChildren<QPushButton*>();
    QCOMPARE(buttons.size(), 4);
    for (QPushButton* button : buttons)
    {
        if (button->text() == QStringLiteral("SUB"))
        {
            button->click();
        }
        else if (button->text() == QStringLiteral("DUAL WATCH"))
        {
            button->click();
        }
        else if (button->text() == QStringLiteral("MAIN ↔ SUB"))
        {
            button->click();
        }
    }

    QCOMPARE(vfoSpy.count(), 1);
    QCOMPARE(vfoSpy.takeFirst().at(0).value<Vfo>(), Vfo::Sub);
    QCOMPARE(dualWatchSpy.count(), 1);
    QCOMPARE(exchangeSpy.count(), 1);
    QCOMPARE(dualWatchSpy.takeFirst().at(0).toBool(), true);
    QCOMPARE(panel.selectedVfo(), Vfo::Main);
    QVERIFY(!panel.dualWatchEnabled());

    panel.setSelectedVfo(Vfo::Sub);
    panel.setDualWatchEnabled(true);
    QCOMPARE(panel.selectedVfo(), Vfo::Sub);
    QVERIFY(panel.dualWatchEnabled());

    QPushButton* mainButton = nullptr;
    QPushButton* subButton = nullptr;
    QPushButton* dualWatchButton = nullptr;
    QPushButton* exchangeButton = nullptr;
    for (QPushButton* button : buttons)
    {
        if (button->text() == QStringLiteral("MAIN"))
        {
            mainButton = button;
        }
        else if (button->text() == QStringLiteral("SUB"))
        {
            subButton = button;
        }
        else if (button->text() == QStringLiteral("DUAL WATCH"))
        {
            dualWatchButton = button;
        }
        else if (button->text() == QStringLiteral("MAIN ↔ SUB"))
        {
            exchangeButton = button;
        }
    }
    QVERIFY(mainButton != nullptr);
    QVERIFY(subButton != nullptr);
    QVERIFY(dualWatchButton != nullptr);
    QVERIFY(exchangeButton != nullptr);
    panel.show();
    QApplication::processEvents();
    QWidget* exchangeDivider = panel.findChild<QWidget*>(QStringLiteral("exchangeDivider"));
    QVERIFY(exchangeDivider != nullptr);
    const QPoint mainPosition = mainButton->mapTo(&panel, QPoint(0, 0));
    const QPoint subPosition = subButton->mapTo(&panel, QPoint(0, 0));
    QCOMPARE(mainPosition.y(), subPosition.y());
    QVERIFY(mainPosition.x() < subPosition.x());
    QVERIFY(mainPosition.y() < exchangeButton->y());
    QVERIFY(exchangeButton->y() < dualWatchButton->y());
    QCOMPARE(subPosition.x() - mainPosition.x() - mainButton->width(), 4);
    QCOMPARE(exchangeButton->y() - mainPosition.y() - mainButton->height(), 10);
    QCOMPARE(exchangeDivider->y() - exchangeButton->geometry().bottom() - 1, 10);
    QCOMPARE(dualWatchButton->y() - exchangeDivider->geometry().bottom() - 1, 10);
    QCOMPARE(mainButton->height(), subButton->height());
    QCOMPARE(subButton->height(), dualWatchButton->height());
    QCOMPARE(exchangeButton->height(), dualWatchButton->height());
    QCOMPARE(mainButton->width(), 52);
    QCOMPARE(subButton->width(), 52);
    QCOMPARE(exchangeDivider->width(), 108);
    QCOMPARE(exchangeButton->x(), dualWatchButton->x());
    QCOMPARE(exchangeDivider->x(), dualWatchButton->x());
}

QTEST_MAIN(VfoDisplayTest)
#include "VfoDisplayTest.moc"
