// QtTest invokes private slots through the generated meta-object.
#include "SettingsDialog.h"

#include <QLineEdit>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QtTest>

class GuiSmokeTest : public QObject
{
    Q_OBJECT

  private slots:
    void initTestCase();
    void settingsDialogOpensSearchesAndCloses();
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

QTEST_MAIN(GuiSmokeTest)
#include "GuiSmokeTest.moc"
