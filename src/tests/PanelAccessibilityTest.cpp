#include "DtmfDialog.h"
#include "MutePanel.h"
#include "PttPanel.h"
#include "ReceivePanel.h"
#include "TransmitPanel.h"

#include <QLineEdit>
#include <QPushButton>
#include <QTest>

class PanelAccessibilityTest : public QObject
{
    Q_OBJECT

  private slots:
    void panelsExposeAccessibleNames();
    void panelsAdoptProvidedButtons();
    void optionalPanelButtonsAreNullSafe();
    void dtmfControlsHaveUsableInitialState();
};

void PanelAccessibilityTest::panelsExposeAccessibleNames()
{
    PttPanel pttPanel(new QPushButton);
    MutePanel mutePanel(new QPushButton);
    TransmitPanel transmitPanel({new QPushButton});
    QCOMPARE(pttPanel.accessibleName(), QStringLiteral("Transmit"));
    QCOMPARE(mutePanel.accessibleName(), QStringLiteral("Mute"));
    QCOMPARE(transmitPanel.accessibleName(), QStringLiteral("Transmit"));
}

void PanelAccessibilityTest::panelsAdoptProvidedButtons()
{
    QWidget owner;
    QList<QPushButton*> buttons;
    for (int i = 0; i < 12; ++i)
    {
        buttons.append(new QPushButton(QString::number(i), &owner));
    }
    ReceivePanel::Buttons receiveButtons{
        buttons[0], buttons[1], buttons[2], buttons[3], buttons[4],  buttons[5],
        buttons[6], buttons[7], buttons[8], buttons[9], buttons[10], buttons[11],
    };
    ReceivePanel panel(receiveButtons);
    QCOMPARE(panel.accessibleName(), QStringLiteral("Control"));
    for (QPushButton* button : buttons)
    {
        QCOMPARE(button->parentWidget()->parentWidget(), &panel);
    }
}

void PanelAccessibilityTest::optionalPanelButtonsAreNullSafe()
{
    PttPanel pttPanel(nullptr);
    MutePanel mutePanel(nullptr);
    QVERIFY(pttPanel.findChildren<QPushButton*>().isEmpty());
    QVERIFY(mutePanel.findChildren<QPushButton*>().isEmpty());
}

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

QTEST_MAIN(PanelAccessibilityTest)

#include "PanelAccessibilityTest.moc"
