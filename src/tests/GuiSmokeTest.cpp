// QtTest invokes private slots through the generated meta-object.
#include "SettingsDialog.h"

#include <QLineEdit>
#include <QShortcut>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QtTest>

class GuiSmokeTest : public QObject
{
    Q_OBJECT

  private slots:
    void initTestCase();
    void settingsDialogOpensSearchesAndCloses();
    void settingsFindShortcutFocusesSearch();
};

void GuiSmokeTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void GuiSmokeTest::settingsDialogOpensSearchesAndCloses()
{
    SettingsDialog dialog(SettingsDialog::Page::MemoryManager);
    dialog.show();

    QTRY_VERIFY(dialog.isVisible());
    auto* search = dialog.findChild<QLineEdit*>(QStringLiteral("settingsSearch"));
    auto* navigation = dialog.findChild<QTreeWidget*>(QStringLiteral("settingsNavigation"));
    QVERIFY(search != nullptr);
    QVERIFY(navigation != nullptr);
    QVERIFY(navigation->topLevelItemCount() > 0);

    search->setText(QStringLiteral("spectrum"));
    QCoreApplication::processEvents();
    QVERIFY(!search->text().isEmpty());

    dialog.close();
    QTRY_VERIFY(!dialog.isVisible());
}

void GuiSmokeTest::settingsFindShortcutFocusesSearch()
{
    SettingsDialog dialog(SettingsDialog::Page::MemoryManager);
    dialog.show();
    QTRY_VERIFY(dialog.isVisible());
    auto* search = dialog.findChild<QLineEdit*>(QStringLiteral("settingsSearch"));
    auto* navigation = dialog.findChild<QTreeWidget*>(QStringLiteral("settingsNavigation"));
    QVERIFY(search != nullptr);
    QVERIFY(navigation != nullptr);
    Q_UNUSED(navigation)
    auto* shortcut = dialog.findChild<QShortcut*>();
    QVERIFY(shortcut != nullptr);
    QCOMPARE(shortcut->key(), QKeySequence(QKeySequence::Find));
    search->setText(QStringLiteral("spectrum"));
    QVERIFY(QMetaObject::invokeMethod(shortcut, "activated", Qt::DirectConnection));
    QCOMPARE(search->selectedText(), QStringLiteral("spectrum"));
}

QTEST_MAIN(GuiSmokeTest)
#include "GuiSmokeTest.moc"
