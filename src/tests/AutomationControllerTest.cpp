#include "AutomationController.h"
#include "backend/IRadioBackend.h"
#include "MainWindow.h"
#include "models/RadioModel.h"

#include <QCoreApplication>
#include <QEvent>
#include <QJsonArray>
#include <QComboBox>
#include <QMenu>
#include <QPointer>
#include <QTest>
#include <QWidgetAction>

class AutomationControllerTest final : public QObject
{
    Q_OBJECT

  private slots:
    void transmitActionsAreNeverExposedOrAccepted()
    {
        RadioModel model;
        MainWindow window(&model, nullptr, false);
        AutomationController controller(&window);

        const QJsonObject listed =
            controller.execute(QJsonObject{{QStringLiteral("action"), QStringLiteral("list_actions")}});
        QVERIFY(listed.value(QStringLiteral("ok")).toBool());
        const QJsonArray actions = listed.value(QStringLiteral("actions")).toArray();
        QVERIFY(actions.contains(QStringLiteral("ui_dismiss_popup")));
        const QStringList prohibited{QStringLiteral("set_ptt"),      QStringLiteral("ptt"),
                                     QStringLiteral("send_dtmf"),    QStringLiteral("set_tx_power"),
                                     QStringLiteral("set_tx_audio"), QStringLiteral("set_compressor")};
        for (const QString& action : prohibited)
        {
            QVERIFY(!actions.contains(action));
        }

        // Repeat each hostile request enough times to catch an accidental
        // stateful fallback or rate-dependent dispatch path. Every spelling
        // remains an unknown action even while the radio itself is offline.
        for (int iteration = 0; iteration < 1000; ++iteration)
        {
            const QString& action = prohibited.at(iteration % prohibited.size());
            const QJsonObject response = controller.execute(QJsonObject{
                {QStringLiteral("action"), action}, {QStringLiteral("enabled"), true}, {QStringLiteral("value"), 255}});
            QVERIFY(!response.value(QStringLiteral("ok")).toBool());
            QCOMPARE(response.value(QStringLiteral("error")).toString(), QStringLiteral("unknown_action"));
        }
    }

    void stateDeclaresTransmitUnavailable()
    {
        RadioModel model;
        MainWindow window(&model, nullptr, false);
        AutomationController controller(&window);
        const QJsonObject response =
            controller.execute(QJsonObject{{QStringLiteral("action"), QStringLiteral("get_state")}});
        QVERIFY(response.value(QStringLiteral("ok")).toBool());
        const QJsonObject state = response.value(QStringLiteral("state")).toObject();
        QCOMPARE(state.value(QStringLiteral("transmitAllowed")).toBool(true), false);
        QCOMPARE(state.value(QStringLiteral("connected")).toBool(true), false);
        QVERIFY(state.contains(QStringLiteral("radioAfGain")));
        QVERIFY(state.value(QStringLiteral("radioAfGain")).isNull());
        QVERIFY(state.value(QStringLiteral("dialLock")).isNull());
        QVERIFY(state.value(QStringLiteral("txPower")).isNull());
        QVERIFY(state.value(QStringLiteral("lanModLevel")).isNull());
        QVERIFY(state.value(QStringLiteral("compressorEnabled")).isNull());
        QVERIFY(state.value(QStringLiteral("compressorLevel")).isNull());
        QVERIFY(!state.contains(QStringLiteral("afGain")));

        emit model.backend()->radioValueConfirmed(funcDialLock, true, 0);
        emit model.backend()->radioValueConfirmed(funcAfGain, 12, 0xff);
        emit model.backend()->radioValueConfirmed(funcRFPower, 34, 0xff);
        emit model.backend()->radioValueConfirmed(funcLANModLevel, 56, 0xff);
        emit model.backend()->radioValueConfirmed(funcCompressor, true, 0xff);
        emit model.backend()->radioValueConfirmed(funcCompressorLevel, 78, 0xff);
        const QJsonObject populated =
            controller.execute(QJsonObject{{QStringLiteral("action"), QStringLiteral("get_state")}})
                .value(QStringLiteral("state"))
                .toObject();
        QCOMPARE(populated.value(QStringLiteral("dialLock")).toBool(), true);
        QCOMPARE(populated.value(QStringLiteral("radioAfGain")).toInt(), 12);
        QCOMPARE(populated.value(QStringLiteral("txPower")).toInt(), 34);
        QCOMPARE(populated.value(QStringLiteral("lanModLevel")).toInt(), 56);
        QCOMPARE(populated.value(QStringLiteral("compressorEnabled")).toBool(), true);
        QCOMPARE(populated.value(QStringLiteral("compressorLevel")).toInt(), 78);
    }

    void uiInventoryExposesControlsButRejectsPtt()
    {
        RadioModel model;
        MainWindow window(&model, nullptr, false);
        // A production MainWindow schedules profile auto-connect or the modal
        // Radio Chooser for its first event turn. This test exercises only the
        // automation inventory, so remove that startup callback before showing
        // the window. Otherwise a clean CI account with no saved profile blocks
        // forever inside the chooser while processEvents() runs below.
        QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);
        window.show();
        QCoreApplication::processEvents();
        AutomationController controller(&window);

        const QJsonObject inventory =
            controller.execute(QJsonObject{{QStringLiteral("action"), QStringLiteral("ui_list")}});
        QVERIFY(inventory.value(QStringLiteral("ok")).toBool());
        const QJsonArray controls = inventory.value(QStringLiteral("controls")).toArray();
        QVERIFY(controls.size() > 20);

        QString pttControlId;
        for (const QJsonValue& value : controls)
        {
            const QJsonObject control = value.toObject();
            if (control.value(QStringLiteral("pttProhibited")).toBool())
            {
                pttControlId = control.value(QStringLiteral("id")).toString();
                break;
            }
        }
        QVERIFY(!pttControlId.isEmpty());

        // Repeated attempts must remain blocked; no radio-readiness shortcut
        // or queued-click path may turn a denied PTT control into an action.
        for (int attempt = 0; attempt < 1000; ++attempt)
        {
            const QJsonObject response =
                controller.execute(QJsonObject{{QStringLiteral("action"), QStringLiteral("ui_activate")},
                                               {QStringLiteral("controlId"), pttControlId}});
            QVERIFY(!response.value(QStringLiteral("ok")).toBool());
            QCOMPARE(response.value(QStringLiteral("error")).toString(), QStringLiteral("ptt_prohibited"));
        }
    }

    void dynamicallyCreatedDialogControlsAreDiscovered()
    {
        RadioModel model;
        MainWindow window(&model, nullptr, false);
        // Keep the test independent of persisted radio profiles and prevent
        // the first-turn Radio Chooser from entering a nested modal event loop.
        QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);
        window.show();
        QCoreApplication::processEvents();
        AutomationController controller(&window);

        QJsonArray controls = controller.execute(QJsonObject{{QStringLiteral("action"), QStringLiteral("ui_list")}})
                                  .value(QStringLiteral("controls"))
                                  .toArray();
        QString settingsActionId;
        for (const QJsonValue& value : controls)
        {
            const QJsonObject control = value.toObject();
            if (control.value(QStringLiteral("objectName")).toString() == QStringLiteral("settingsAction"))
            {
                settingsActionId = control.value(QStringLiteral("id")).toString();
                break;
            }
        }
        QVERIFY(!settingsActionId.isEmpty());
        QVERIFY(controller
                    .execute(QJsonObject{{QStringLiteral("action"), QStringLiteral("ui_activate")},
                                         {QStringLiteral("controlId"), settingsActionId}})
                    .value(QStringLiteral("ok"))
                    .toBool());
        QWidget* settingsSearch = nullptr;
        QTRY_VERIFY((settingsSearch = window.findChild<QWidget*>(QStringLiteral("settingsSearch"))) != nullptr);

        controls = controller.execute(QJsonObject{{QStringLiteral("action"), QStringLiteral("ui_list")}})
                       .value(QStringLiteral("controls"))
                       .toArray();
        QJsonObject navigation;
        for (const QJsonValue& value : controls)
        {
            const QJsonObject control = value.toObject();
            if (control.value(QStringLiteral("objectName")).toString() == QStringLiteral("settingsNavigation"))
            {
                navigation = control;
                break;
            }
        }
        QVERIFY(!navigation.isEmpty());
        QVERIFY(navigation.value(QStringLiteral("items")).toArray().size() >= 3);

        // The settings window is non-modal and production code schedules its
        // deletion after close. Complete that lifecycle while MainWindow and
        // RadioModel are still alive instead of leaving a visible top-level
        // dialog and its deferred work to overlap parent teardown. Linux's
        // allocator reports that incomplete lifecycle as heap corruption.
        QPointer<QWidget> settingsWindow = settingsSearch->window();
        QVERIFY(settingsWindow);
        QVERIFY(settingsWindow != &window);
        settingsWindow->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(settingsWindow.isNull());
    }


    void settingComboInsideMenuClosesMenu()
    {
        RadioModel model;
        MainWindow window(&model, nullptr, false);
        QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);
        window.show();
        QCoreApplication::processEvents();
        AutomationController controller(&window);

        QMenu menu(&window);
        auto* action = new QWidgetAction(&menu);
        auto* combo = new QComboBox(&menu);
        combo->setObjectName(QStringLiteral("automationMenuCombo"));
        combo->addItems({QStringLiteral("First"), QStringLiteral("Second")});
        action->setDefaultWidget(combo);
        menu.addAction(action);
        menu.popup(window.mapToGlobal(QPoint(20, 20)));
        QTRY_VERIFY(menu.isVisible());

        const QJsonArray controls =
            controller.execute(QJsonObject{{QStringLiteral("action"), QStringLiteral("ui_list")}})
                .value(QStringLiteral("controls"))
                .toArray();
        QString comboId;
        for (const QJsonValue& value : controls)
        {
            const QJsonObject control = value.toObject();
            if (control.value(QStringLiteral("objectName")).toString() == QStringLiteral("automationMenuCombo"))
            {
                comboId = control.value(QStringLiteral("id")).toString();
                break;
            }
        }
        QVERIFY(!comboId.isEmpty());
        QVERIFY(controller
                    .execute(QJsonObject{{QStringLiteral("action"), QStringLiteral("ui_set")},
                                         {QStringLiteral("controlId"), comboId},
                                         {QStringLiteral("value"), 1}})
                    .value(QStringLiteral("ok"))
                    .toBool());
        QCOMPARE(combo->currentIndex(), 1);
        QTRY_VERIFY(!menu.isVisible());
    }
};

QTEST_MAIN(AutomationControllerTest)
#include "AutomationControllerTest.moc"
