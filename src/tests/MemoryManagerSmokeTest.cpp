// QtTest invokes private slots through the generated meta-object.
#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "MainTitleBar.h"
#include "AppInfo.h"
#include "AppPaths.h"
#include "AppSettings.h"
#include "MemoryEditorPolicy.h"
#include "MemoryController.h"
#include "MemoryConstants.h"
#include "MemoryDatabase.h"
#include "MemorySyncController.h"
#include "RadioChooserDialog.h"
#include "RadioCommandController.h"
#include "RadioProfile.h"
#include "StatusBarController.h"
#include "UiTheme.h"
#include "UtilityWindow.h"
#include "VfoSelectionController.h"
#include "backend/IRadioBackend.h"
#include "models/RadioModel.h"
#include "models/VfoModel.h"

#include <QAction>
#include <QAbstractItemDelegate>
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
#include <QProgressBar>
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
    void memoryManagerShowsCachedVerificationAndLiveSyncProgress();
    void unnamedRadioMemoryIsPersistedWithFrequencyName();
    void memoryVisibilitySettingsHideOptionalCategoriesByDefault();
    void mainWindowRetainsFixedFramelessDesign();
    void fileMenuTracksRadioConnection();
    void selectorButtonsAvoidDynamicStyleSheets();
    void compressorMenuReflectsConfirmedLevel();
    void utilityWindowIsDestroyedWithHost();
    void quitActionDefersWindowClose();
    void persistentStatusMessageCanBeClearedByOwner();
    void automationIndicatorReflectsClientCount();
    void titleBarSpeakerTogglesMute();
};

void MemoryManagerSmokeTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QVERIFY(QDir(sdr9700::configDirectory()).removeRecursively() || !QDir(sdr9700::configDirectory()).exists());
    QVERIFY(QDir(sdr9700::dataDirectory()).removeRecursively() || !QDir(sdr9700::dataDirectory()).exists());
}

void MemoryManagerSmokeTest::memoryManagerShowsCachedVerificationAndLiveSyncProgress()
{
    const QUuid profileId = QUuid::createUuid();
    MemoryType storedMemory;
    storedMemory.group = 1;
    storedMemory.channel = 1;
    storedMemory.frequency.Hz = 145000000;
    std::copy_n("DATABASE TEST", 13, storedMemory.name);

    MemoryDatabase database;
    QString databaseError;
    QVERIFY2(database.open(&databaseError), qPrintable(databaseError));
    QVERIFY2(database.store(profileId, storedMemory, &databaseError), qPrintable(databaseError));

    RadioModel model;
    MainWindow window(&model);
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);
    auto* controller = window.findChild<MemoryController*>();
    auto* statusLabel = window.findChild<QLabel*>(QStringLiteral("memoryManagerStatusLabel"));
    auto* progressBar = window.findChild<QProgressBar*>(QStringLiteral("memoryManagerProgressBar"));
    auto* memoryTable = window.findChild<QTableWidget*>(QStringLiteral("memoryManagerTable"));
    QVERIFY(controller != nullptr);
    QVERIFY(statusLabel != nullptr);
    QVERIFY(progressBar != nullptr);
    QVERIFY(memoryTable != nullptr);

    controller->setRadioProfileId(profileId);
    QCOMPARE(statusLabel->text(), QStringLiteral("Waiting to verify 1 cached memory with the radio (0/420)"));
    QVERIFY(!progressBar->isHidden());
    QCOMPARE(progressBar->value(), 0);
    QCOMPARE(progressBar->maximum(), 420);
    QCOMPARE(memoryTable->rowCount(), 1);
    QVERIFY(!memoryTable->item(0, 0)->data(sdr9700::memory::kMemoryVerifiedThisSessionRole).toBool());
    QVERIFY(!memoryTable->item(0, 0)->text().isEmpty());
    QCOMPARE(memoryTable->item(0, 0)->foreground().color(), QColor(QLatin1String(UiTheme::Color::TextMuted)));
    QVERIFY(memoryTable->item(0, 0)->toolTip().contains(QStringLiteral("local cache")));

    QVERIFY(QMetaObject::invokeMethod(&model, "onBackendConnected"));
    QVERIFY(QMetaObject::invokeMethod(&model, "onBackendReadyChanged", Q_ARG(bool, true)));
    QVERIFY(!statusLabel->text().startsWith(QStringLiteral("Syncing 2M channel")));
    model.spectrumActivity();
    model.spectrumActivity();
    model.spectrumActivity();
    QTRY_VERIFY_WITH_TIMEOUT(statusLabel->text().startsWith(QStringLiteral("Syncing 2M channel 001")), 1500);
    MemoryType liveMemory = storedMemory;
    liveMemory.frequency.Hz = 145500000;
    liveMemory.frequency.MHzDouble = 145.5;

    // Exercise the complete 420-slot response volume rather than short-cutting
    // controller state. The first stored response preserves the cached row;
    // every other response authoritatively confirms an empty radio slot.
    for (quint16 group = 1; group <= 3; ++group)
    {
        for (quint16 channel = 1; channel <= 107; ++channel)
        {
            MemoryType reply;
            reply.group = group;
            reply.channel = channel;
            reply.del = true;
            if (group == storedMemory.group && channel == storedMemory.channel)
            {
                reply = liveMemory;
            }
            model.radioMemoryReceived(reply);
            QCoreApplication::sendPostedEvents(controller, QEvent::MetaCall);
            if (group == storedMemory.group && channel == storedMemory.channel)
            {
                // The table may reflect live replies immediately, but the
                // durable generation remains unchanged until finalization.
                QCOMPARE(database.memories(profileId, &databaseError).constFirst().frequency.Hz,
                         storedMemory.frequency.Hz);
            }
        }
    }
    for (quint16 channel = 1; channel <= 99; ++channel)
    {
        if (channel == 99)
        {
            continue;
        }
        MemoryType reply;
        reply.group = 0;
        reply.channel = channel;
        reply.sat = true;
        reply.del = true;
        model.radioMemoryReceived(reply);
        QCoreApplication::sendPostedEvents(controller, QEvent::MetaCall);
    }
    QTRY_VERIFY_WITH_TIMEOUT(statusLabel->text().startsWith(QStringLiteral("Finalizing radio memory sync")), 500);
    auto* syncController = controller->findChild<MemorySyncController*>();
    QVERIFY(syncController != nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(syncController->missingRetryRound(), 1, 1500);

    MemoryType recoveredReply;
    recoveredReply.group = 0;
    recoveredReply.channel = 99;
    recoveredReply.sat = true;
    recoveredReply.del = true;
    model.radioMemoryReceived(recoveredReply);
    QCoreApplication::sendPostedEvents(controller, QEvent::MetaCall);
    QTRY_COMPARE_WITH_TIMEOUT(statusLabel->text(), QStringLiteral("1 memory total"), 1000);
    QVERIFY(progressBar->isHidden());
    QCOMPARE(memoryTable->rowCount(), 1);
    QVERIFY(memoryTable->item(0, 0)->data(sdr9700::memory::kMemoryVerifiedThisSessionRole).toBool());
    QCOMPARE(database.memories(profileId, &databaseError).constFirst().frequency.Hz, liveMemory.frequency.Hz);
    QVERIFY(database.syncState(profileId, &databaseError).complete);

    // A later sweep that never receives the occupied slot must retry only the
    // missing key, finish after the bounded retry budget, and downgrade the
    // retained database row to explicitly cached provenance. Startup and the
    // memory UI must not remain locked forever around an unresponsive slot.
    controller->forceRadioMemorySync();
    for (quint16 group = 1; group <= 3; ++group)
    {
        for (quint16 channel = 1; channel <= 107; ++channel)
        {
            if (group == storedMemory.group && channel == storedMemory.channel)
            {
                continue;
            }
            MemoryType reply;
            reply.group = group;
            reply.channel = channel;
            reply.del = true;
            model.radioMemoryReceived(reply);
            QCoreApplication::sendPostedEvents(controller, QEvent::MetaCall);
        }
    }
    for (quint16 channel = 1; channel <= 99; ++channel)
    {
        MemoryType reply;
        reply.group = 0;
        reply.channel = channel;
        reply.sat = true;
        reply.del = true;
        model.radioMemoryReceived(reply);
        QCoreApplication::sendPostedEvents(controller, QEvent::MetaCall);
    }
    QTRY_COMPARE_WITH_TIMEOUT(statusLabel->text(), QStringLiteral("1 total (0 verified, 1 cached; 1 slot unanswered)"),
                              3500);
    QVERIFY(!memoryTable->item(0, 0)->data(sdr9700::memory::kMemoryVerifiedThisSessionRole).toBool());
    QCOMPARE(database.memories(profileId, &databaseError).constFirst().frequency.Hz, liveMemory.frequency.Hz);
    const MemoryDatabaseSyncState partialState = database.syncState(profileId, &databaseError);
    QCOMPARE(partialState.receivedSlotCount, 419);
    QVERIFY(!partialState.complete);

    // A row double-click must activate the memory on the VFO that the radio
    // currently reports as selected. MAIN is the initial confirmed selection.
    // Invoke the table signal so this test covers the complete UI connection
    // rather than calling the memory controller directly.
    QVERIFY(QMetaObject::invokeMethod(memoryTable, "cellDoubleClicked", Q_ARG(int, 0), Q_ARG(int, 0)));
    auto* statusMessageLabel = window.findChild<QLabel*>(QStringLiteral("statusMessageLabel"));
    QVERIFY(statusMessageLabel != nullptr);
    QCOMPARE(statusMessageLabel->text(), QStringLiteral("Selected memory on MAIN: DATABASE TEST"));

    // Physical CI-V receiver routing is deliberately independent of the
    // operator's selected UI side. Drive the selection controller used by the
    // main form and prove that the identical row action follows it to SUB,
    // even though background polling may temporarily route through either
    // physical receiver.
    auto* vfoSelection = window.findChild<VfoSelectionController*>();
    QVERIFY(vfoSelection != nullptr);
    vfoSelection->selectVfo(Vfo::Sub);
    model.backend()->radioValueConfirmed(funcVFOBandMS, QVariant::fromValue<bool>(true), 0);
    QCoreApplication::processEvents();
    QVERIFY(QMetaObject::invokeMethod(memoryTable, "cellDoubleClicked", Q_ARG(int, 0), Q_ARG(int, 0)));
    QCOMPARE(statusMessageLabel->text(), QStringLiteral("Selected memory on SUB: DATABASE TEST"));
}

void MemoryManagerSmokeTest::unnamedRadioMemoryIsPersistedWithFrequencyName()
{
    const QUuid profileId = QUuid::createUuid();
    RadioModel model;
    MainWindow window(&model);
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);
    auto* controller = window.findChild<MemoryController*>();
    QVERIFY(controller != nullptr);
    controller->setRadioProfileId(profileId);

    MemoryType unnamed;
    unnamed.group = 1;
    unnamed.channel = 25;
    unnamed.frequency.Hz = 145500000;
    model.radioMemoryReceived(unnamed);
    QCoreApplication::sendPostedEvents(controller, QEvent::MetaCall);

    MemoryDatabase database;
    QString error;
    QVERIFY2(database.open(&error), qPrintable(error));
    const QVector<MemoryType> stored = database.memories(profileId, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.constFirst().group, quint16(1));
    QCOMPARE(stored.constFirst().channel, quint16(25));
    QCOMPARE(sdr9700::memory::radioMemoryName(stored.constFirst()), QStringLiteral("145.500.000"));
}

void MemoryManagerSmokeTest::memoryVisibilitySettingsHideOptionalCategoriesByDefault()
{
    AppSettings::instance().remove(QStringLiteral("memoryShowSpecialMemories"));
    AppSettings::instance().remove(QStringLiteral("memoryShowSatelliteMemories"));

    const QUuid profileId = QUuid::createUuid();
    MemoryType ordinary;
    ordinary.group = 1;
    ordinary.channel = 1;
    ordinary.frequency.Hz = 145000000;
    std::copy_n("ORDINARY", 8, ordinary.name);
    MemoryType special = ordinary;
    special.channel = 100;
    std::copy_n("SCAN EDGE", 9, special.name);
    MemoryType satellite = ordinary;
    satellite.group = 0;
    satellite.channel = 1;
    satellite.sat = true;
    std::copy_n("SATELLITE", 9, satellite.name);

    MemoryDatabase database;
    QString error;
    QVERIFY2(database.open(&error), qPrintable(error));
    QVERIFY2(database.store(profileId, ordinary, &error), qPrintable(error));
    QVERIFY2(database.store(profileId, special, &error), qPrintable(error));
    QVERIFY2(database.store(profileId, satellite, &error), qPrintable(error));

    RadioModel model;
    MainWindow window(&model);
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);
    auto* controller = window.findChild<MemoryController*>();
    auto* table = window.findChild<QTableWidget*>(QStringLiteral("memoryManagerTable"));
    auto* filter = window.findChild<QComboBox*>(QStringLiteral("memoryManagerBandFilter"));
    QVERIFY(controller != nullptr);
    QVERIFY(table != nullptr);
    QVERIFY(filter != nullptr);
    controller->setRadioProfileId(profileId);

    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(table->item(0, 0)->text(), QStringLiteral("2M"));
    QCOMPARE(table->item(0, 1)->text(), QStringLiteral("001 [ORDINARY]"));
    QCOMPARE(filter->findData(QStringLiteral("special")), -1);
    QCOMPARE(filter->findData(QStringLiteral("satellite")), -1);

    table->selectRow(0);
    const QList<QPushButton*> buttons = window.findChildren<QPushButton*>();
    const auto editButtonIt = std::find_if(buttons.cbegin(), buttons.cend(), [](const QPushButton* button)
                                           { return button->text() == QLatin1String("Edit"); });
    QVERIFY(editButtonIt != buttons.cend());
    QString editBand;
    QString editChannel;
    QTimer::singleShot(100, QCoreApplication::instance(),
                       [&]()
                       {
                           auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                           if (!dialog)
                           {
                               return;
                           }
                           if (auto* band = dialog->findChild<QLineEdit*>(QStringLiteral("memoryEditorBand")))
                           {
                               editBand = band->text();
                               QVERIFY(band->isReadOnly());
                           }
                           if (auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("memoryEditorChannel")))
                           {
                               editChannel = combo->currentText();
                           }
                           dialog->reject();
                       });
    (*editButtonIt)->click();
    QCOMPARE(editBand, QStringLiteral("2M"));
    QCOMPARE(editChannel, QStringLiteral("001 [ORDINARY]"));

    const auto addButtonIt = std::find_if(buttons.cbegin(), buttons.cend(), [](const QPushButton* button)
                                          { return button->text() == QLatin1String("Add"); });
    QVERIFY(addButtonIt != buttons.cend());
    QString firstEmptyChannel;
    QTimer::singleShot(100, QCoreApplication::instance(),
                       [&]()
                       {
                           auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                           if (!dialog)
                           {
                               return;
                           }
                           auto* frequency = dialog->findChild<QLineEdit*>(QStringLiteral("memoryEditorFrequency"));
                           auto* channel = dialog->findChild<QComboBox*>(QStringLiteral("memoryEditorChannel"));
                           if (frequency && channel)
                           {
                               QVERIFY(channel->findChild<QAbstractItemDelegate*>(
                                           QStringLiteral("memoryEditorChannelDelegate")) != nullptr);
                               frequency->setText(QStringLiteral("145.500000"));
                               firstEmptyChannel = channel->currentText();
                           }
                           dialog->reject();
                       });
    (*addButtonIt)->click();
    QCOMPARE(firstEmptyChannel, QStringLiteral("002 [EMPTY]"));

    controller->setShowSpecialMemories(true);
    QCOMPARE(table->rowCount(), 2);
    QVERIFY(filter->findData(QStringLiteral("special")) >= 0);

    controller->setShowSatelliteMemories(true);
    QCOMPARE(table->rowCount(), 3);
    QVERIFY(filter->findData(QStringLiteral("satellite")) >= 0);

    controller->setShowSpecialMemories(false);
    controller->setShowSatelliteMemories(false);
    QCOMPARE(table->rowCount(), 1);
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
    auto* windowMenu = window.findChild<QMenu*>(QStringLiteral("windowMenu"));
    QVERIFY(table != nullptr);
    QVERIFY(windowMenu != nullptr);
    QVERIFY(memoryWindow->findChild<QWidget*>(QStringLiteral("memoryEditorPane")) == nullptr);
    QCOMPARE(table->columnCount(), 7);
    QCOMPARE(table->horizontalHeaderItem(0)->text(), QStringLiteral("Band"));
    QCOMPARE(table->horizontalHeaderItem(1)->text(), QStringLiteral("Channel"));
    for (int column = 0; column < 6; ++column)
    {
        const auto expectedMode = column == 1 ? QHeaderView::Stretch : QHeaderView::Fixed;
        QCOMPARE(table->horizontalHeader()->sectionResizeMode(column), expectedMode);
    }
    QCOMPARE(table->horizontalHeader()->height(), 32);

    memoryWindow->show();
    QVERIFY(QMetaObject::invokeMethod(windowMenu, "aboutToShow"));
    const QList<QAction*> windowActions = windowMenu->actions();
    const auto memoryWindowAction = std::find_if(windowActions.cbegin(), windowActions.cend(), [](const QAction* action)
                                                 { return action->text() == QLatin1String("Memory Manager"); });
    QVERIFY(memoryWindowAction != windowActions.cend());
    memoryWindow->hide();
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
    QString initialEditorBandText;
    QString initialEditorChannelText;
    QString derivedEditorBandText;
    QString derivedEditorChannelText;
    QTimer::singleShot(100, QCoreApplication::instance(),
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
                           editorHadTitleBar =
                               dialog->findChild<QWidget*>(QStringLiteral("memoryEditorTitleBar")) != nullptr;
                           editorHadScrollArea =
                               dialog->findChild<QWidget*>(QStringLiteral("memoryEditorScrollArea")) != nullptr;
                           auto* bandEdit = dialog->findChild<QLineEdit*>(QStringLiteral("memoryEditorBand"));
                           auto* channelCombo = dialog->findChild<QComboBox*>(QStringLiteral("memoryEditorChannel"));
                           auto* frequencyEdit = dialog->findChild<QLineEdit*>(QStringLiteral("memoryEditorFrequency"));
                           if (bandEdit && channelCombo && frequencyEdit)
                           {
                               QVERIFY(bandEdit->isReadOnly());
                               QCOMPARE(bandEdit->alignment(), Qt::AlignCenter);
                               initialEditorBandText = bandEdit->text();
                               initialEditorChannelText = channelCombo->currentText();
                               frequencyEdit->setText(QStringLiteral("443.050000"));
                               derivedEditorBandText = bandEdit->text();
                               derivedEditorChannelText = channelCombo->currentText();
                           }
                           dialog->reject();
                       });
    addMemoryButton->click();
    QVERIFY(foundEditorDialog);
    QVERIFY(editorWasModal);
    QVERIFY(editorWasFrameless);
    QVERIFY(editorHadTitleBar);
    QVERIFY(editorHadScrollArea);
    QVERIFY(initialEditorBandText.isEmpty());
    QVERIFY(initialEditorChannelText.isEmpty());
    QCOMPARE(derivedEditorBandText, QStringLiteral("70CM"));
    QCOMPARE(derivedEditorChannelText, QStringLiteral("001 [EMPTY]"));
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
    QVERIFY(window.findChild<QSlider*>(QStringLiteral("titleLanModSlider")) == nullptr);
    QVERIFY(window.findChild<QPushButton*>(QStringLiteral("vfoMODButton")) != nullptr);
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
                           QVERIFY(slider->isEnabled());
                           QCOMPARE(slider->value(), 0);
                           const QList<QLabel*> labels = menu->findChildren<QLabel*>();
                           QVERIFY(std::any_of(labels.cbegin(), labels.cend(), [](const QLabel* label)
                                               { return label->text() == QStringLiteral("0%"); }));
                           model.vfo()->applyCompressorLevel(64);
                           QVERIFY(slider->isEnabled());
                           QCOMPARE(slider->value(), 0);
                           model.vfo()->clearCompressorLevel();
                           QVERIFY(slider->isEnabled());
                           inspectedUnknown = true;
                           menu->close();
                       });
    controller.showCompressorMenu(QPoint());
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
                           QCOMPARE(slider->value(), 0);
                           const QList<QAction*> actions = menu->actions();
                           const auto enabledAction =
                               std::find_if(actions.cbegin(), actions.cend(), [](const QAction* action)
                                            { return action->text() == QStringLiteral("Enabled"); });
                           QVERIFY(enabledAction == actions.cend());
                           model.vfo()->applyCompressor(true);
                           QCOMPARE(slider->value(), 192);
                           model.vfo()->applyCompressor(false);
                           QCOMPARE(slider->value(), 0);
                           slider->setValue(200);
                           inspectedConfirmed = true;
                           menu->close();
                       });
    controller.showCompressorMenu(QPoint());
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

void MemoryManagerSmokeTest::persistentStatusMessageCanBeClearedByOwner()
{
    RadioModel model;
    MainWindow window(&model);
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);

    auto* statusMessageLabel = window.findChild<QLabel*>(QStringLiteral("statusMessageLabel"));
    const QObjectList children = window.children();
    const auto controller = std::find_if(children.cbegin(), children.cend(), [](const QObject* child)
                                         { return dynamic_cast<const StatusBarController*>(child) != nullptr; });
    QVERIFY(controller != children.cend());
    auto* statusBarController = dynamic_cast<StatusBarController*>(*controller);
    QVERIFY(statusMessageLabel != nullptr);
    QVERIFY(statusBarController != nullptr);

    const QString importMessage = QStringLiteral("Syncing radio memories before import...");
    statusBarController->showStatusMessage(importMessage, 0);
    QCOMPARE(statusMessageLabel->text(), QStringLiteral("Syncing radio memories before import"));

    statusBarController->clearPersistentStatusMessage(QStringLiteral("A different operation"));
    QCOMPARE(statusMessageLabel->text(), QStringLiteral("Syncing radio memories before import"));

    statusBarController->clearPersistentStatusMessage(importMessage);
    QVERIFY(statusMessageLabel->text().isEmpty());

    const QString recommendedLength(StatusBarController::kRecommendedStatusMessageCharacters, QLatin1Char('x'));
    statusBarController->showStatusMessage(recommendedLength, 0);
    QCOMPARE(statusMessageLabel->text(), recommendedLength);

    const QString warningLength(StatusBarController::kRecommendedStatusMessageCharacters + 1, QLatin1Char('x'));
    QTest::ignoreMessage(QtWarningMsg,
                         "Status message exceeds the recommended length characters=65 recommended=64 maximum=72");
    statusBarController->showStatusMessage(warningLength, 0);
    QCOMPARE(statusMessageLabel->text(), warningLength);

    const QString maximumLength(StatusBarController::kMaximumStatusMessageCharacters, QLatin1Char('x'));
    QTest::ignoreMessage(QtWarningMsg,
                         "Status message exceeds the recommended length characters=72 recommended=64 maximum=72");
    statusBarController->showStatusMessage(maximumLength, 0);
    QCOMPARE(statusMessageLabel->text(), maximumLength);

    const QString elidedLength(StatusBarController::kMaximumStatusMessageCharacters + 1, QLatin1Char('x'));
    QTest::ignoreMessage(
        QtWarningMsg,
        "Status message elided because it exceeds the maximum length characters=73 recommended=64 maximum=72");
    statusBarController->showStatusMessage(elidedLength, 0);
    QCOMPARE(statusMessageLabel->text().size(), StatusBarController::kMaximumStatusMessageCharacters);
    QVERIFY(statusMessageLabel->text().endsWith(QChar(0x2026)));
    QCOMPARE(statusMessageLabel->toolTip(), elidedLength);
    statusBarController->clearPersistentStatusMessage(maximumLength);
    QVERIFY(!statusMessageLabel->text().isEmpty());
    statusBarController->clearPersistentStatusMessage(elidedLength);
    QVERIFY(statusMessageLabel->text().isEmpty());
    QVERIFY(statusMessageLabel->toolTip().isEmpty());

    const QStringList punctuatedMessages = {
        QStringLiteral("Complete."),         QStringLiteral("Wait..."),   QStringLiteral("Warning!"),
        QStringLiteral("Continue?"),         QStringLiteral("Finished;"), QStringLiteral("Status:"),
        QStringLiteral("Unicode ellipsis…"),
    };
    for (const QString& message : punctuatedMessages)
    {
        statusBarController->showStatusMessage(message, 1000);
        QVERIFY2(!statusMessageLabel->text().endsWith(QLatin1Char('.')), qPrintable(statusMessageLabel->text()));
        QVERIFY2(!statusMessageLabel->text().endsWith(QLatin1Char('!')), qPrintable(statusMessageLabel->text()));
        QVERIFY2(!statusMessageLabel->text().endsWith(QLatin1Char('?')), qPrintable(statusMessageLabel->text()));
        QVERIFY2(!statusMessageLabel->text().endsWith(QLatin1Char(';')), qPrintable(statusMessageLabel->text()));
        QVERIFY2(!statusMessageLabel->text().endsWith(QLatin1Char(':')), qPrintable(statusMessageLabel->text()));
        QVERIFY2(!statusMessageLabel->text().endsWith(QChar(0x2026)), qPrintable(statusMessageLabel->text()));
    }

    statusBarController->showStatusMessage(QStringLiteral("Radio ready."), 1);
    QCOMPARE(statusMessageLabel->text(), QStringLiteral("Radio ready"));
    QTRY_VERIFY_WITH_TIMEOUT(statusMessageLabel->text().isEmpty(), 100);
}

void MemoryManagerSmokeTest::automationIndicatorReflectsClientCount()
{
    RadioModel model;
    MainWindow window(&model);
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);

    auto* indicator = window.findChild<QLabel*>(QStringLiteral("automationIndicator"));
    const QObjectList children = window.children();
    const auto controller = std::find_if(children.cbegin(), children.cend(), [](const QObject* child)
                                         { return dynamic_cast<const StatusBarController*>(child) != nullptr; });
    QVERIFY(controller != children.cend());
    auto* statusBarController = dynamic_cast<StatusBarController*>(*controller);
    QVERIFY(indicator != nullptr);
    QVERIFY(statusBarController != nullptr);

    statusBarController->setAutomationEnabled(true);
    QVERIFY(indicator->styleSheet().contains(QStringLiteral("border: none")));
    QVERIFY(indicator->styleSheet().contains(QStringLiteral("background: #f0a000")));
    QCOMPARE(indicator->toolTip(),
             QStringLiteral("Automation enabled.\n0 local clients connected.\nTransmit controls are unavailable."));

    statusBarController->setAutomationClientCount(1);
    QVERIFY(indicator->styleSheet().contains(QStringLiteral("border: none")));
    QVERIFY(indicator->styleSheet().contains(QStringLiteral("background: %1").arg(UiTheme::Color::Danger)));
    QCOMPARE(indicator->toolTip(),
             QStringLiteral("Automation enabled.\n1 local client connected.\nTransmit controls are unavailable."));

    statusBarController->setAutomationClientCount(0);
    QVERIFY(indicator->styleSheet().contains(QStringLiteral("border: none")));
    QVERIFY(indicator->styleSheet().contains(QStringLiteral("background: #f0a000")));
}

void MemoryManagerSmokeTest::titleBarSpeakerTogglesMute()
{
    MainTitleBar titleBar;
    auto* speakerButton = titleBar.findChild<QPushButton*>(QStringLiteral("titleSpeakerMuteButton"));
    QVERIFY(speakerButton != nullptr);
    QVERIFY(!speakerButton->icon().isNull());
    QVERIFY(speakerButton->text().isEmpty());
    QCOMPARE(speakerButton->toolTip(), QStringLiteral("Mute audio"));

    const auto buttons = titleBar.findChildren<QPushButton*>();
    QVERIFY(std::none_of(buttons.cbegin(), buttons.cend(),
                         [](const QPushButton* button) { return button->text() == QStringLiteral("MUTE"); }));

    QSignalSpy muteSpy(&titleBar, &MainTitleBar::muteToggled);
    QTest::mouseClick(speakerButton, Qt::LeftButton);
    QCOMPARE(muteSpy.count(), 1);

    titleBar.setMuted(true);
    QVERIFY(speakerButton->text().isEmpty());
    QVERIFY(speakerButton->styleSheet().contains(QStringLiteral("border: 1px solid %1").arg(UiTheme::Color::Danger)));
    QCOMPARE(speakerButton->toolTip(), QStringLiteral("Unmute audio"));

    QTest::mouseClick(speakerButton, Qt::LeftButton);
    QCOMPARE(muteSpy.count(), 2);
    titleBar.setMuted(false);
    QVERIFY(speakerButton->text().isEmpty());
    QVERIFY(!speakerButton->icon().isNull());
    QCOMPARE(speakerButton->toolTip(), QStringLiteral("Mute audio"));
}

QTEST_MAIN(MemoryManagerSmokeTest)

#include "MemoryManagerSmokeTest.moc"
