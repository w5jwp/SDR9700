// QtTest invokes private slots through the generated meta-object.
#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "AppPaths.h"
#include "RadioChooserDialog.h"
#include "RadioProfile.h"
#include "StatusBarController.h"
#include "UiTheme.h"
#include "UtilityWindow.h"
#include "models/RadioModel.h"

#include <QAction>
#include <QComboBox>
#include <QCloseEvent>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFontMetrics>
#include <QLineEdit>
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
    void newInstallationCanAddRadioProfile();
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
    QVERIFY(QDir(sdr9700::configDirectory()).removeRecursively() || !QDir(sdr9700::configDirectory()).exists());
}

void MemoryManagerSmokeTest::newInstallationCanAddRadioProfile()
{
    RadioProfileStore::instance().load();
    QVERIFY(RadioProfileStore::instance().profiles().isEmpty());

    RadioChooserDialog dialog;
    QVERIFY(dialog.windowFlags().testFlag(Qt::FramelessWindowHint));
    QCOMPARE(dialog.minimumSize(), dialog.maximumSize());
    auto* addButton = dialog.findChild<QPushButton*>(QStringLiteral("addRadioProfileButton"));
    auto* saveButton = dialog.findChild<QPushButton*>(QStringLiteral("saveRadioProfileButton"));
    auto* connectButton = dialog.findChild<QPushButton*>(QStringLiteral("connectRadioButton"));
    auto* buttonBox = dialog.findChild<QDialogButtonBox*>(QStringLiteral("radioChooserButtonBox"));
    auto* nameEdit = dialog.findChild<QLineEdit*>(QStringLiteral("radioProfileName"));
    auto* hostEdit = dialog.findChild<QLineEdit*>(QStringLiteral("radioProfileHost"));
    QVERIFY(addButton != nullptr);
    QVERIFY(saveButton != nullptr);
    QVERIFY(connectButton != nullptr);
    QVERIFY(buttonBox != nullptr);
    QVERIFY(nameEdit != nullptr);
    QVERIFY(hostEdit != nullptr);
    QCOMPARE(buttonBox->buttonRole(connectButton), QDialogButtonBox::AcceptRole);
    QVERIFY(buttonBox->button(QDialogButtonBox::Cancel) != nullptr);
    QVERIFY(!nameEdit->isEnabled());
    QVERIFY(!hostEdit->isEnabled());

    addButton->click();

    QVERIFY(nameEdit->isEnabled());
    QVERIFY(hostEdit->isEnabled());
    QVERIFY(RadioProfileStore::instance().profiles().isEmpty());
    nameEdit->setText(QStringLiteral("Test IC-9700"));
    hostEdit->setText(QStringLiteral("192.0.2.1"));
    QVERIFY(saveButton->isEnabled());
    saveButton->click();

    QCOMPARE(RadioProfileStore::instance().profiles().size(), 1);
    const RadioProfile savedProfile = RadioProfileStore::instance().profiles().constFirst();
    QCOMPARE(savedProfile.name, QStringLiteral("Test IC-9700"));
    QCOMPARE(savedProfile.host, QStringLiteral("192.0.2.1"));
    QVERIFY(RadioProfileStore::instance().removeProfile(savedProfile.id));
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
    auto* rfGainButton = window.findChild<QPushButton*>(QStringLiteral("rfGainButton"));
    auto* frequencyEdit = window.findChild<QLineEdit*>(QStringLiteral("vfoFrequencyEdit"));
    QVERIFY(rfGainButton != nullptr);
    QVERIFY(frequencyEdit != nullptr);
    QVERIFY(!rfGainButton->property("levelControl").toBool());
    QCOMPARE(frequencyEdit->alignment(), Qt::AlignCenter);
    const QFontMetrics frequencyMetrics(frequencyEdit->font());
    QCOMPARE(frequencyMetrics.horizontalAdvance(QStringLiteral("000.000.000")),
             frequencyMetrics.horizontalAdvance(QStringLiteral("111.111.111")));
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
    MainWindow window(&model, nullptr, false);
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

    // Shutdown disconnects model-to-UI delivery before the backend publishes
    // its final ready=false state. A late state signal and a repeated close
    // must not re-enter memory-table rebuilding during native teardown.
    model.readyChanged(false);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCloseEvent repeatedClose;
    QCoreApplication::sendEvent(&window, &repeatedClose);
    QVERIFY(repeatedClose.isAccepted());
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
    QCOMPARE(toastLabel->text(), QStringLiteral("Syncing radio memories before import"));

    statusBarController->clearPersistentToast(QStringLiteral("A different operation"));
    QCOMPARE(toastLabel->text(), QStringLiteral("Syncing radio memories before import"));

    statusBarController->clearPersistentToast(importMessage);
    QVERIFY(toastLabel->text().isEmpty());

    const QStringList punctuatedMessages = {
        QStringLiteral("Complete."),         QStringLiteral("Wait..."),   QStringLiteral("Warning!"),
        QStringLiteral("Continue?"),         QStringLiteral("Finished;"), QStringLiteral("Status:"),
        QStringLiteral("Unicode ellipsis…"),
    };
    for (const QString& message : punctuatedMessages)
    {
        statusBarController->showToast(message, 1000);
        QVERIFY2(!toastLabel->text().endsWith(QLatin1Char('.')), qPrintable(toastLabel->text()));
        QVERIFY2(!toastLabel->text().endsWith(QLatin1Char('!')), qPrintable(toastLabel->text()));
        QVERIFY2(!toastLabel->text().endsWith(QLatin1Char('?')), qPrintable(toastLabel->text()));
        QVERIFY2(!toastLabel->text().endsWith(QLatin1Char(';')), qPrintable(toastLabel->text()));
        QVERIFY2(!toastLabel->text().endsWith(QLatin1Char(':')), qPrintable(toastLabel->text()));
        QVERIFY2(!toastLabel->text().endsWith(QChar(0x2026)), qPrintable(toastLabel->text()));
    }

    statusBarController->showToast(QStringLiteral("Radio ready."), 1);
    QCOMPARE(toastLabel->text(), QStringLiteral("Radio ready"));
    QTRY_VERIFY_WITH_TIMEOUT(toastLabel->text().isEmpty(), 100);
}

QTEST_MAIN(MemoryManagerSmokeTest)

#include "MemoryManagerSmokeTest.moc"
