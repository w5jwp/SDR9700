// QtTest invokes private slots through the generated meta-object.
#include "ConfirmationDialog.h"
#include "SettingsDialog.h"

#include <QLineEdit>
#include <QComboBox>
#include <QMessageBox>
#include <QPushButton>
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
    void audioSettingsChangesAreForwarded();
    void confirmationDialogsUseSafeSemanticButtons();
};

void GuiSmokeTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void GuiSmokeTest::settingsDialogOpensSearchesAndCloses()
{
    SettingsDialog dialog(SettingsDialog::Page::MemoryManager);
    QVERIFY(dialog.windowFlags().testFlag(Qt::FramelessWindowHint));
    QCOMPARE(dialog.minimumSize(), dialog.maximumSize());
    dialog.show();

    QTRY_VERIFY(dialog.isVisible());
    auto* search = dialog.findChild<QLineEdit*>(QStringLiteral("settingsSearch"));
    auto* navigation = dialog.findChild<QTreeWidget*>(QStringLiteral("settingsNavigation"));
    QVERIFY(search != nullptr);
    QVERIFY(navigation != nullptr);
    QVERIFY(navigation->topLevelItemCount() > 0);
    QVERIFY(navigation->itemsExpandable());
    QTreeWidgetItem* category = navigation->topLevelItem(0);
    QVERIFY(category != nullptr);
    QVERIFY(category->childCount() > 0);
    category->setExpanded(false);
    QVERIFY(!category->isExpanded());
    category->setExpanded(true);
    QVERIFY(category->isExpanded());
    QTreeWidgetItemIterator itemIterator(navigation);
    while (*itemIterator)
    {
        QTreeWidgetItem* item = *itemIterator;
        QVERIFY(!item->toolTip(0).isEmpty());
        QVERIFY(item->toolTip(0) != item->data(0, Qt::UserRole + 1).toString());
        ++itemIterator;
    }

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

void GuiSmokeTest::audioSettingsChangesAreForwarded()
{
    SettingsDialog dialog(SettingsDialog::Page::AudioDevices);
    QSignalSpy changedSpy(&dialog, &SettingsDialog::audioSettingsChanged);
    dialog.show();

    auto* channels = dialog.findChild<QComboBox*>(QStringLiteral("audioOutputChannels"));
    QVERIFY(channels != nullptr);
    QCOMPARE(channels->count(), 2);
    channels->setCurrentIndex(channels->currentIndex() == 0 ? 1 : 0);
    QCOMPARE(changedSpy.count(), 1);
}

void GuiSmokeTest::confirmationDialogsUseSafeSemanticButtons()
{
    QMessageBox dialog;
    QPushButton* action = sdr9700::ui::configureConfirmationButtons(dialog, QStringLiteral("Import"), true);
    auto* cancel = qobject_cast<QPushButton*>(dialog.button(QMessageBox::Cancel));

    QVERIFY(action != nullptr);
    QVERIFY(cancel != nullptr);
    QCOMPARE(action->text(), QStringLiteral("Import"));
    QCOMPARE(dialog.buttonRole(action), QMessageBox::DestructiveRole);
    QCOMPARE(dialog.defaultButton(), cancel);
    QCOMPARE(dialog.escapeButton(), cancel);
    sdr9700::ui::configureMessageBoxWindow(dialog);
    QVERIFY(dialog.windowFlags().testFlag(Qt::FramelessWindowHint));
    QCOMPARE(dialog.minimumSize(), dialog.maximumSize());
}

QTEST_MAIN(GuiSmokeTest)
#include "GuiSmokeTest.moc"
