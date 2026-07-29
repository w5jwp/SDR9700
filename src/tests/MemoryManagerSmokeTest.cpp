// QtTest invokes private slots through the generated meta-object.
#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "StatusBarController.h"
#include "UiTheme.h"
#include "UtilityWindow.h"
#include "models/RadioModel.h"

#include <QAction>
#include <QComboBox>
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QWidget>
#include <QtTest>
#include <algorithm>

class MemoryManagerSmokeTest : public QObject
{
    Q_OBJECT

  private slots:
    void initTestCase();
    void constructsMemoryManagerUi();
    void mainWindowRetainsFixedFramelessDesign();
    void selectorButtonsAvoidDynamicStyleSheets();
    void utilityWindowIsDestroyedWithHost();
    void quitActionDefersWindowClose();
    void persistentToastCanBeClearedByOwner();
};

void MemoryManagerSmokeTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void MemoryManagerSmokeTest::constructsMemoryManagerUi()
{
    RadioModel model;
    MainWindow window(&model);

    const QWidgetList topLevelWidgets = QApplication::topLevelWidgets();
    const auto memoryWindowIt =
        std::find_if(topLevelWidgets.cbegin(), topLevelWidgets.cend(),
                     [](const QWidget* candidate) { return candidate->objectName() == QLatin1String("memoryWindow"); });
    QDialog* memoryWindow =
        memoryWindowIt != topLevelWidgets.cend() ? qobject_cast<QDialog*>(*memoryWindowIt) : nullptr;
    QVERIFY(memoryWindow != nullptr);
    QVERIFY(memoryWindow->windowFlags().testFlag(Qt::FramelessWindowHint));
    QCOMPARE(memoryWindow->minimumSize(), memoryWindow->maximumSize());
    auto* table = memoryWindow->findChild<QTableWidget*>(QStringLiteral("memoryManagerTable"));
    auto* editor = memoryWindow->findChild<QWidget*>(QStringLiteral("memoryEditorPane"));
    QVERIFY(table != nullptr);
    QVERIFY(editor != nullptr);
    QCOMPARE(table->columnCount(), 9);
    QVERIFY(!editor->isVisible());
}

void MemoryManagerSmokeTest::mainWindowRetainsFixedFramelessDesign()
{
    RadioModel model;
    MainWindow window(&model);
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);

    QVERIFY(window.windowFlags().testFlag(Qt::FramelessWindowHint));
    QCOMPARE(window.minimumSize(), window.maximumSize());
    QCOMPARE(window.minimumSize(), QSize(UiTheme::Size::MainWindowMinWidth, UiTheme::Size::MainWindowMinHeight));
}

void MemoryManagerSmokeTest::selectorButtonsAvoidDynamicStyleSheets()
{
    RadioModel model;
    MainWindow window(&model);
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);

    int selectorCount = 0;
    for (QPushButton* button : window.findChildren<QPushButton*>())
    {
        if (dynamic_cast<sdr9700::ui::main_window::TwoLineButton*>(button))
        {
            ++selectorCount;
            QVERIFY(button->styleSheet().isEmpty());
            sdr9700::ui::main_window::setCommandButtonActive(button, true);
            sdr9700::ui::main_window::setCommandButtonActive(button, false);
            QVERIFY(button->styleSheet().isEmpty());
        }
    }
    QVERIFY(selectorCount > 0);
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

void MemoryManagerSmokeTest::persistentToastCanBeClearedByOwner()
{
    RadioModel model;
    MainWindow window(&model);
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);

    auto* toastLabel = window.findChild<QLabel*>(QStringLiteral("statusToastLabel"));
    StatusBarController* statusBarController = nullptr;
    for (QObject* child : window.children())
    {
        if (auto* candidate = dynamic_cast<StatusBarController*>(child))
        {
            statusBarController = candidate;
            break;
        }
    }
    QVERIFY(toastLabel != nullptr);
    QVERIFY(statusBarController != nullptr);

    const QString importMessage = QStringLiteral("Syncing radio memories before import...");
    statusBarController->showToast(importMessage, 0);
    QCOMPARE(toastLabel->text(), importMessage);

    statusBarController->clearPersistentToast(QStringLiteral("A different operation"));
    QCOMPARE(toastLabel->text(), importMessage);

    statusBarController->clearPersistentToast(importMessage);
    QVERIFY(toastLabel->text().isEmpty());
}

QTEST_MAIN(MemoryManagerSmokeTest)

#include "MemoryManagerSmokeTest.moc"
