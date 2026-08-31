#include "AutomationController.h"
#include "MainWindow.h"
#include "models/RadioModel.h"

#include <QJsonArray>
#include <QTest>

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
    }

    void uiInventoryExposesControlsButRejectsPtt()
    {
        RadioModel model;
        MainWindow window(&model, nullptr, false);
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
            if (control.value(QStringLiteral("text")).toString() == QStringLiteral("Settings…"))
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
        QTRY_VERIFY(window.findChild<QWidget*>(QStringLiteral("settingsSearch")) != nullptr);

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
    }
};

QTEST_MAIN(AutomationControllerTest)
#include "AutomationControllerTest.moc"
