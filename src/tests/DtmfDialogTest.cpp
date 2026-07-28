#include "DtmfDialog.h"

#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <algorithm>

namespace
{
QPushButton* buttonWithText(QWidget* parent, const QString& text)
{
    const auto buttons = parent->findChildren<QPushButton*>();
    const auto match = std::find_if(buttons.cbegin(), buttons.cend(),
                                    [&text](const QPushButton* button) { return button->text() == text; });
    return match == buttons.cend() ? nullptr : *match;
}
} // namespace

class DtmfDialogTest : public QObject
{
    Q_OBJECT

  private slots:
    void keypadBuildsAndSendsUppercaseSequence();
    void enforcesInputAlphabetAndLength();
    void clearAndBackspaceEditSequence();
    void sendProgressGatesInput();
};

void DtmfDialogTest::keypadBuildsAndSendsUppercaseSequence()
{
    DtmfDialog dialog;
    auto* display = dialog.findChild<QLineEdit*>();
    auto* send = buttonWithText(&dialog, QStringLiteral("Send"));
    QVERIFY(display != nullptr);
    QVERIFY(send != nullptr);
    QVERIFY(!send->isEnabled());

    QTest::mouseClick(buttonWithText(&dialog, QStringLiteral("1")), Qt::LeftButton);
    QTest::mouseClick(buttonWithText(&dialog, QStringLiteral("A")), Qt::LeftButton);
    QTest::mouseClick(buttonWithText(&dialog, QStringLiteral("#")), Qt::LeftButton);
    QCOMPARE(display->text(), QStringLiteral("1A#"));

    QSignalSpy sendSpy(&dialog, &DtmfDialog::sendRequested);
    QTest::mouseClick(send, Qt::LeftButton);
    QCOMPARE(sendSpy.count(), 1);
    QCOMPARE(sendSpy.takeFirst().at(0).toString(), QStringLiteral("1A#"));
}

void DtmfDialogTest::enforcesInputAlphabetAndLength()
{
    DtmfDialog dialog;
    auto* display = dialog.findChild<QLineEdit*>();
    QVERIFY(display != nullptr);
    display->setFocus();
    QTest::keyClicks(display, QStringLiteral("123abcd*#"));
    QCOMPARE(display->text(), QStringLiteral("123abcd*#"));
    QTest::keyClicks(display, QStringLiteral("xyz"));
    QCOMPARE(display->text(), QStringLiteral("123abcd*#"));
    QTest::keyClicks(display, QStringLiteral("0123456789012345"));
    QCOMPARE(display->text().size(), 16);
}

void DtmfDialogTest::clearAndBackspaceEditSequence()
{
    DtmfDialog dialog;
    auto* display = dialog.findChild<QLineEdit*>();
    QVERIFY(display != nullptr);
    display->setText(QStringLiteral("123"));
    QTest::mouseClick(buttonWithText(&dialog, QStringLiteral("⌫")), Qt::LeftButton);
    QCOMPARE(display->text(), QStringLiteral("12"));
    QTest::mouseClick(buttonWithText(&dialog, QStringLiteral("Clear")), Qt::LeftButton);
    QVERIFY(display->text().isEmpty());
    QVERIFY(!buttonWithText(&dialog, QStringLiteral("Send"))->isEnabled());
}

void DtmfDialogTest::sendProgressGatesInput()
{
    DtmfDialog dialog;
    auto* display = dialog.findChild<QLineEdit*>();
    display->setText(QStringLiteral("5"));
    dialog.setSendInProgress(true);
    QVERIFY(display->isReadOnly());
    QVERIFY(!buttonWithText(&dialog, QStringLiteral("5"))->isEnabled());
    QVERIFY(!buttonWithText(&dialog, QStringLiteral("Send"))->isEnabled());

    dialog.setSendInProgress(false);
    QVERIFY(!display->isReadOnly());
    QVERIFY(buttonWithText(&dialog, QStringLiteral("5"))->isEnabled());
    QVERIFY(buttonWithText(&dialog, QStringLiteral("Send"))->isEnabled());
}

QTEST_MAIN(DtmfDialogTest)

#include "DtmfDialogTest.moc"
