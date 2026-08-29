// QtTest invokes private slots through the generated meta-object.
#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "AppInfo.h"
#include "AppPaths.h"
#include "MemoryEditorPolicy.h"
#include "RadioChooserDialog.h"
#include "RadioCommandController.h"
#include "RadioProfile.h"
#include "StatusBarController.h"
#include "UiTheme.h"
#include "UtilityWindow.h"
#include "models/RadioModel.h"
#include "models/VfoModel.h"

#include <QAction>
#include <QComboBox>
#include <QCloseEvent>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFontMetrics>
#include <QHeaderView>
#include <QLineEdit>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QMenu>
#include <QScrollBar>
#include <QSlider>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
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
    void fileMenuTracksRadioConnection();
    void selectorButtonsAvoidDynamicStyleSheets();
    void compressorMenuReflectsConfirmedLevel();
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
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);

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
    QVERIFY(table != nullptr);
    QVERIFY(memoryWindow->findChild<QWidget*>(QStringLiteral("memoryEditorPane")) == nullptr);
    QCOMPARE(table->columnCount(), 7);
    QCOMPARE(table->horizontalHeaderItem(0)->text(), QStringLiteral("Channel"));
    for (int column = 0; column < 6; ++column)
    {
        const auto expectedMode = column == 1 ? QHeaderView::Stretch : QHeaderView::Fixed;
        QCOMPARE(table->horizontalHeader()->sectionResizeMode(column), expectedMode);
    }
    QCOMPARE(table->horizontalHeader()->height(), 32);
    QCOMPARE(table->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOn);
    QVERIFY(table->styleSheet().contains(QLatin1String(UiTheme::Color::MenuBar)));

    const QList<QPushButton*> memoryButtons = memoryWindow->findChildren<QPushButton*>();
    const auto addButtonIt = std::find_if(memoryButtons.cbegin(), memoryButtons.cend(), [](const QPushButton* button)
                                          { return button->text() == QLatin1String("Add"); });
    QPushButton* addMemoryButton = addButtonIt != memoryButtons.cend() ? *addButtonIt : nullptr;
    QVERIFY(addMemoryButton != nullptr);
    bool foundEditorDialog = false;
    bool editorWasModal = false;
    bool editorWasFrameless = false;
    bool editorHadTitleBar = false;
    bool editorHadScrollArea = false;
    QString editorChannelText;
    QTimer::singleShot(
        100, QCoreApplication::instance(),
        [&]()
        {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog)
            {
                return;
            }
            foundEditorDialog = dialog->objectName() == QLatin1String("memoryEditorDialog");
            editorWasModal = dialog->isModal();
            editorWasFrameless = dialog->windowFlags().testFlag(Qt::FramelessWindowHint);
            editorHadTitleBar = dialog->findChild<QWidget*>(QStringLiteral("memoryEditorTitleBar")) != nullptr;
            editorHadScrollArea = dialog->findChild<QWidget*>(QStringLiteral("memoryEditorScrollArea")) != nullptr;
            if (auto* channelCombo = dialog->findChild<QComboBox*>(QStringLiteral("memoryEditorChannel")))
            {
                editorChannelText = channelCombo->currentText();
            }
            dialog->reject();
        });
    addMemoryButton->click();
    QVERIFY(foundEditorDialog);
    QVERIFY(editorWasModal);
    QVERIFY(editorWasFrameless);
    QVERIFY(editorHadTitleBar);
    QVERIFY(editorHadScrollArea);
    QVERIFY(editorChannelText.contains(QLatin1Char('-')));
    QVERIFY(editorChannelText.endsWith(QStringLiteral("001")));
    QCOMPARE(sdr9700::memory::memoryEditorDialogSize(QSize(1366, 768)), QSize(520, 620));
    QCOMPARE(sdr9700::memory::memoryEditorDialogSize(QSize(1024, 600)), QSize(520, 576));
}

void MemoryManagerSmokeTest::mainWindowRetainsFixedFramelessDesign()
{
    RadioModel model;
    MainWindow window(&model);
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);

    QVERIFY(window.windowFlags().testFlag(Qt::FramelessWindowHint));
    QString expectedTitle = QStringLiteral("SDR9700 v%1").arg(QString::fromLatin1(APP_VERSION));
#if SDR9700_DEBUG_BUILD
    expectedTitle += QStringLiteral(" (DEBUG)");
#endif
    QCOMPARE(window.windowTitle(), expectedTitle);
    QCOMPARE(window.minimumSize(), window.maximumSize());
    QCOMPARE(window.minimumSize(), QSize(UiTheme::Size::MainWindowMinWidth, UiTheme::Size::MainWindowMinHeight));
    QVERIFY(window.findChild<QTableWidget*>(QStringLiteral("memoryBrowserTable")) == nullptr);
    QVERIFY(window.findChild<QWidget*>(QStringLiteral("vfoDisplayStrip")) != nullptr);
    QVERIFY(window.findChild<QSlider*>(QStringLiteral("titleLanModSlider")) != nullptr);
}

void MemoryManagerSmokeTest::fileMenuTracksRadioConnection()
{
    RadioModel model;
    MainWindow window(&model);
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);

    auto* connectionAction = window.findChild<QAction*>(QStringLiteral("radioConnectionAction"));
    QVERIFY(connectionAction != nullptr);
    QCOMPARE(connectionAction->text(), QStringLiteral("Connect to Radio"));

    QSignalSpy connectionSpy(&model, &RadioModel::connectionChanged);
    QSignalSpy stageSpy(&model, &RadioModel::connectionStageChanged);
    QVERIFY(QMetaObject::invokeMethod(&model, "onBackendConnected"));
    QVERIFY(model.isConnected());
    QCOMPARE(connectionAction->text(), QStringLiteral("Disconnect from Radio"));

    connectionAction->trigger();
    QCOMPARE(stageSpy.count(), 1);
    QCOMPARE(stageSpy.constFirst().constFirst().value<ConnectionStage>(), ConnectionStage::Disconnecting);
    QVERIFY(QMetaObject::invokeMethod(&model, "onBackendDisconnected"));
    QVERIFY(!model.isConnected());
    QVERIFY(connectionSpy.count() >= 2);
    QCOMPARE(connectionAction->text(), QStringLiteral("Connect to Radio"));
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
    auto* compressorButton = window.findChild<QPushButton*>(QStringLiteral("vfoCOMPButton"));
    QVERIFY(compressorButton != nullptr);
    QVERIFY(!compressorButton->isCheckable());
    auto* rfGainButton = window.findChild<QPushButton*>(QStringLiteral("rfGainButton"));
    auto* frequencyField = window.findChild<QWidget*>(QStringLiteral("vfoFrequencyField"));
    const auto frequencyEdits = window.findChildren<QLineEdit*>(QStringLiteral("vfoFrequency"));
    auto* statusDateLabel = window.findChild<QLabel*>(QStringLiteral("statusDateLabel"));
    auto* statusTimeLabel = window.findChild<QLabel*>(QStringLiteral("statusTimeLabel"));
    QVERIFY(rfGainButton == nullptr);
    QVERIFY(frequencyField == nullptr);
    QCOMPARE(frequencyEdits.size(), 2);
    QVERIFY(window.findChild<QWidget*>(QStringLiteral("vfoMemoryNameField")) == nullptr);
    QVERIFY(window.findChild<QLineEdit*>(QStringLiteral("vfoMemoryNameLabel")) == nullptr);
    QVERIFY(statusDateLabel != nullptr);
    QVERIFY(statusTimeLabel != nullptr);
    for (const QLineEdit* frequencyEdit : frequencyEdits)
    {
        QCOMPARE(frequencyEdit->alignment(), Qt::AlignRight | Qt::AlignVCenter);
    }
    QCOMPARE(statusDateLabel->alignment(), Qt::AlignCenter);
    QCOMPARE(statusTimeLabel->alignment(), Qt::AlignCenter);
    const QFontMetrics frequencyMetrics(frequencyEdits.constFirst()->font());
    QCOMPARE(frequencyMetrics.horizontalAdvance(QStringLiteral("000.000.000")),
             frequencyMetrics.horizontalAdvance(QStringLiteral("111.111.111")));
}

void MemoryManagerSmokeTest::compressorMenuReflectsConfirmedLevel()
{
    RadioModel model;
    MainWindow window(&model);
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);
    QVERIFY(QMetaObject::invokeMethod(&model, "onBackendReadyChanged", Q_ARG(bool, true)));
    int requestedLevel = -1;
    RadioCommandController controller(&window, [&requestedLevel](int value) { requestedLevel = value; });

    bool inspectedUnknown = false;
    QTimer::singleShot(0, &window,
                       [&]()
                       {
                           auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                           QVERIFY(menu != nullptr);
                           auto* slider = menu->findChild<QSlider*>();
                           QVERIFY(slider != nullptr);
                           QVERIFY(!slider->isEnabled());
                           const QList<QLabel*> labels = menu->findChildren<QLabel*>();
                           QVERIFY(std::any_of(labels.cbegin(), labels.cend(), [](const QLabel* label)
                                               { return label->text() == QStringLiteral("Level --"); }));
                           model.vfo()->applyCompressorLevel(64);
                           QVERIFY(slider->isEnabled());
                           QCOMPARE(slider->value(), 64);
                           model.vfo()->clearCompressorLevel();
                           QVERIFY(!slider->isEnabled());
                           inspectedUnknown = true;
                           menu->close();
                       });
    controller.showCompressorMenu(QPoint(1, 1));
    QVERIFY(inspectedUnknown);

    model.vfo()->applyCompressorLevel(192);
    bool inspectedConfirmed = false;
    QTimer::singleShot(0, &window,
                       [&]()
                       {
                           auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                           QVERIFY(menu != nullptr);
                           auto* slider = menu->findChild<QSlider*>();
                           QVERIFY(slider != nullptr);
                           QVERIFY(slider->isEnabled());
                           QCOMPARE(slider->value(), 192);
                           const QList<QAction*> actions = menu->actions();
                           const auto enabledAction =
                               std::find_if(actions.cbegin(), actions.cend(), [](const QAction* action)
                                            { return action->text() == QStringLiteral("Enabled"); });
                           QVERIFY(enabledAction != actions.cend());
                           QVERIFY(!(*enabledAction)->isChecked());
                           model.vfo()->applyCompressor(true);
                           QVERIFY((*enabledAction)->isChecked());
                           model.vfo()->applyCompressor(false);
                           QVERIFY(!(*enabledAction)->isChecked());
                           slider->setValue(200);
                           inspectedConfirmed = true;
                           menu->close();
                       });
    controller.showCompressorMenu(QPoint(1, 1));
    QVERIFY(inspectedConfirmed);
    QCOMPARE(requestedLevel, 200);

    QVERIFY(QMetaObject::invokeMethod(&model, "onBackendDisconnected"));
    QVERIFY(!model.vfo()->compressorLevelKnown());
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
    const QObjectList children = window.children();
    const auto controller = std::find_if(children.cbegin(), children.cend(), [](const QObject* child)
                                         { return dynamic_cast<const StatusBarController*>(child) != nullptr; });
    QVERIFY(controller != children.cend());
    auto* statusBarController = dynamic_cast<StatusBarController*>(*controller);
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
