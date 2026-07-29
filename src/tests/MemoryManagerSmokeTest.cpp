// QtTest invokes private slots through the generated meta-object.
#include "MainWindow.h"
#include "UtilityWindow.h"
#include "models/RadioModel.h"

#include <QAction>
#include <QComboBox>
#include <QApplication>
#include <QDialog>
#include <QPointer>
#include <QStandardPaths>
#include <QTableWidget>
#include <QWidget>
#include <QtTest>

class MemoryManagerSmokeTest : public QObject
{
    Q_OBJECT

  private slots:
    void initTestCase();
    void constructsMemoryManagerUi();
    void utilityWindowIsDestroyedWithHost();
    void quitActionDefersWindowClose();
};

void MemoryManagerSmokeTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void MemoryManagerSmokeTest::constructsMemoryManagerUi()
{
    RadioModel model;
    MainWindow window(&model);

    QDialog* memoryWindow = nullptr;
    for (QWidget* candidate : QApplication::topLevelWidgets())
    {
        if (candidate->objectName() == QLatin1String("memoryWindow"))
        {
            memoryWindow = qobject_cast<QDialog*>(candidate);
            break;
        }
    }
    QVERIFY(memoryWindow != nullptr);
    auto* table = memoryWindow->findChild<QTableWidget*>(QStringLiteral("memoryManagerTable"));
    auto* editor = memoryWindow->findChild<QWidget*>(QStringLiteral("memoryEditorPane"));
    QVERIFY(table != nullptr);
    QVERIFY(editor != nullptr);
    QCOMPARE(table->columnCount(), 9);
    QVERIFY(!editor->isVisible());
}

void MemoryManagerSmokeTest::utilityWindowIsDestroyedWithHost()
{
    auto* host = new QWidget;
    QPointer<sdr9700::ui::UtilityWindow> utility = new sdr9700::ui::UtilityWindow(QStringLiteral("Test Utility"), host);

    QCOMPARE(utility->parentWidget(), host);
    QVERIFY(utility->isWindow());

    delete host;
    QVERIFY(utility.isNull());
}

void MemoryManagerSmokeTest::quitActionDefersWindowClose()
{
    RadioModel model;
    MainWindow window(&model);
    // MainWindow normally opens the saved profile or chooser on its first event
    // turn. This test only exercises shutdown dispatch, so suppress that startup
    // callback before showing the window.
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);
    window.show();
    QVERIFY(window.isVisible());

    auto* quitAction = window.findChild<QAction*>(QStringLiteral("quitAction"));
    QVERIFY(quitAction != nullptr);
    quitAction->trigger();

    // Closing from inside QAction::triggered would tear down the native window
    // while QMenu is still dispatching its mouse-release event on macOS.
    QVERIFY(window.isVisible());
    QCoreApplication::sendPostedEvents(&window, QEvent::MetaCall);
    QVERIFY(!window.isVisible());
}

QTEST_MAIN(MemoryManagerSmokeTest)

#include "MemoryManagerSmokeTest.moc"
