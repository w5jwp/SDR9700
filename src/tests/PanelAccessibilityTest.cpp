#include "DtmfDialog.h"
#include "DialogFooter.h"
#include "MetersDialog.h"

#include <QLineEdit>
#include <QPushButton>
#include <QTest>
#include <memory>

class PanelAccessibilityTest : public QObject
{
    Q_OBJECT

  private slots:
    void dtmfControlsHaveUsableInitialState();
    void utilityDialogsAreFixedAndFrameless();
    void metersSurviveRepeatedUpdatesAndDestruction();
};

void PanelAccessibilityTest::dtmfControlsHaveUsableInitialState()
{
    DtmfDialog dialog;
    auto* display = dialog.findChild<QLineEdit*>();
    QVERIFY(display != nullptr);
    QCOMPARE(display->maxLength(), 16);
    QVERIFY(display->validator() != nullptr);
    QVERIFY(display->isEnabled());
    const auto buttons = dialog.findChildren<QPushButton*>();
    QVERIFY(buttons.size() >= 19);
}

void PanelAccessibilityTest::utilityDialogsAreFixedAndFrameless()
{
    DtmfDialog dtmf;
    MetersDialog meters;
    for (QDialog* dialog : {static_cast<QDialog*>(&dtmf), static_cast<QDialog*>(&meters)})
    {
        QVERIFY(dialog->windowFlags().testFlag(Qt::FramelessWindowHint));
        QCOMPARE(dialog->minimumSize(), dialog->maximumSize());
        auto* closeButton = dialog->findChild<QPushButton*>(QString(), Qt::FindChildrenRecursively);
        QVERIFY(closeButton != nullptr);
    }

    auto* metersFooterSeparator = meters.findChild<QWidget*>(QStringLiteral("dialogFooterSeparator"));
    auto* metersFooterRow = meters.findChild<QWidget*>(QStringLiteral("dialogFooterRow"));
    auto* metersCloseButton = meters.findChild<QPushButton*>(QStringLiteral("metersCloseButton"));
    QVERIFY(metersFooterSeparator != nullptr);
    QCOMPARE(metersFooterSeparator->height(), 1);
    QVERIFY(metersFooterRow != nullptr);
    QVERIFY(metersFooterRow->layout() != nullptr);
    QCOMPARE(metersFooterRow->layout()->contentsMargins(),
             QMargins(0, sdr9700::ui::kDialogFooterSpacing, 0, sdr9700::ui::kDialogFooterSpacing));
    QVERIFY(metersCloseButton != nullptr);
    QCOMPARE(metersCloseButton->text(), QStringLiteral("Close"));
}

void PanelAccessibilityTest::metersSurviveRepeatedUpdatesAndDestruction()
{
    for (int iteration = 0; iteration < 20; ++iteration)
    {
        auto meters = std::make_unique<MetersDialog>();
        for (int level = 0; level < 100; ++level)
        {
            // Exercise both repeated values, which must not rebuild the style
            // sheet, and threshold transitions that legitimately change it.
            const int audioLevel = level < 25 ? 0 : (level < 50 ? 64 : (level < 75 ? 220 : 250));
            meters->setTransmitAudioLevel(audioLevel, audioLevel);
            meters->setTransmitAudioLevel(audioLevel, audioLevel);
        }
    }
}

QTEST_MAIN(PanelAccessibilityTest)

#include "PanelAccessibilityTest.moc"
