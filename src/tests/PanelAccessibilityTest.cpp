#include "DtmfDialog.h"
#include "MetersDialog.h"

#include <QGroupBox>
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

    QVERIFY(meters.findChild<QWidget*>(QStringLiteral("dialogFooterSeparator")) == nullptr);
    QVERIFY(meters.findChild<QPushButton*>(QStringLiteral("metersCloseButton")) == nullptr);
    QVERIFY(meters.findChild<QGroupBox*>(QStringLiteral("receiveMeters")) != nullptr);
    QVERIFY(meters.findChild<QGroupBox*>(QStringLiteral("transmitMeters")) != nullptr);
    QVERIFY(meters.findChild<QGroupBox*>(QStringLiteral("audioMeters")) != nullptr);
    QVERIFY(meters.findChild<QGroupBox*>(QStringLiteral("radioMeters")) != nullptr);
    QStringList groupTitles;
    for (const auto* group : meters.findChildren<QGroupBox*>())
    {
        groupTitles.append(group->title());
    }
    QCOMPARE(groupTitles, QStringList({QStringLiteral("AUDIO"), QStringLiteral("RADIO"), QStringLiteral("RECEIVE"),
                                       QStringLiteral("TRANSMIT")}));
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
