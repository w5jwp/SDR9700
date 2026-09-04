#include "AutomationController.h"

#include "AutomationServer.h"
#include "AutomationUiDriver.h"
#include "MainWindow.h"
#include "StatusBarController.h"
#include "VfoController.h"
#include "VfoSelectionController.h"
#include "models/RadioModel.h"
#include "models/RadioState.h"
#include "radio/RadioCapabilities.h"

#include <QJsonArray>
#include <QJsonValue>

namespace
{
constexpr int kProtocolVersion = 1;

QString vfoName(Vfo vfo)
{
    return vfo == Vfo::Main ? QStringLiteral("MAIN") : QStringLiteral("SUB");
}

std::optional<Vfo> parseVfo(const QJsonValue& value)
{
    const QString name = value.toString().trimmed().toUpper();
    if (name == QLatin1String("MAIN"))
    {
        return Vfo::Main;
    }
    if (name == QLatin1String("SUB"))
    {
        return Vfo::Sub;
    }
    return std::nullopt;
}

std::optional<availableBands> parseBand(const QJsonValue& value)
{
    const QString name = value.toString().trimmed().toLower();
    if (name == QLatin1String("2m"))
    {
        return band2m;
    }
    if (name == QLatin1String("70cm"))
    {
        return band70cm;
    }
    if (name == QLatin1String("23cm"))
    {
        return band23cm;
    }
    return std::nullopt;
}

QJsonValue optionalInteger(const std::optional<quint64>& value)
{
    return value ? QJsonValue(static_cast<qint64>(*value)) : QJsonValue(QJsonValue::Null);
}

template <typename T> QJsonValue optionalJsonValue(const std::optional<T>& value)
{
    return value ? QJsonValue(*value) : QJsonValue(QJsonValue::Null);
}

QJsonObject receiverSnapshot(const sdr9700::RadioState::Receiver& receiver)
{
    return QJsonObject{
        {QStringLiteral("frequencyHz"), optionalInteger(receiver.frequencyHz)},
        {QStringLiteral("band"), receiver.band == bandUnknown
                                     ? QJsonValue(QJsonValue::Null)
                                     : QJsonValue(sdr9700::radioBandShortLabel(receiver.band))},
        {QStringLiteral("mode"), receiver.mode ? QJsonValue(*receiver.mode) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("filter"), receiver.filter ? QJsonValue(*receiver.filter) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("repeaterOffsetHz"), optionalInteger(receiver.repeaterOffsetHz)},
        {QStringLiteral("agcMode"), optionalJsonValue(receiver.agcMode)},
        {QStringLiteral("attenuatorEnabled"), optionalJsonValue(receiver.attenuatorEnabled)},
        {QStringLiteral("nbEnabled"), optionalJsonValue(receiver.nbEnabled)},
        {QStringLiteral("nbLevel"), optionalJsonValue(receiver.nbLevel)},
        {QStringLiteral("autoNotchEnabled"), optionalJsonValue(receiver.autoNotchEnabled)},
        {QStringLiteral("manualNotchEnabled"), optionalJsonValue(receiver.manualNotchEnabled)},
        {QStringLiteral("nrEnabled"), optionalJsonValue(receiver.nrEnabled)},
        {QStringLiteral("nrLevel"), optionalJsonValue(receiver.nrLevel)},
        {QStringLiteral("preampLevel"), optionalJsonValue(receiver.preampLevel)},
        {QStringLiteral("rfGain"), optionalJsonValue(receiver.rfGain)},
        {QStringLiteral("squelch"), optionalJsonValue(receiver.squelch)}};
}
} // namespace

AutomationController::AutomationController(MainWindow* window)
    : QObject(window),
      m_window(window),
      m_server(new AutomationServer(this)),
      m_uiDriver(new AutomationUiDriver(window))
{
    connect(m_server, &AutomationServer::clientCountChanged, this,
            [this](int count) { m_window->m_statusBarController->setAutomationClientCount(count); });
}

bool AutomationController::start()
{
    const bool started = m_server->start([this](const QJsonObject& request) { return execute(request); });
    if (started)
    {
        m_window->m_statusBarController->setAutomationEnabled(true);
    }
    return started;
}

QJsonObject AutomationController::reject(const QString& code, const QString& message) const
{
    return QJsonObject{
        {QStringLiteral("ok"), false}, {QStringLiteral("error"), code}, {QStringLiteral("message"), message}};
}

bool AutomationController::receiveControlReady() const
{
    return m_window && m_window->m_model && m_window->m_model->isConnected() && m_window->m_model->isReady() &&
           !m_window->m_model->isTransmitting() && !m_window->m_controlsLocked;
}

QJsonObject AutomationController::stateSnapshot() const
{
    const RadioModel* model = m_window->m_model;
    const sdr9700::RadioState* state = model->radioState();
    const auto& shared = state->shared();
    return QJsonObject{{QStringLiteral("protocol"), kProtocolVersion},
                       {QStringLiteral("transmitAllowed"), false},
                       {QStringLiteral("connected"), model->isConnected()},
                       {QStringLiteral("ready"), model->isReady()},
                       {QStringLiteral("transmitting"), model->isTransmitting()},
                       {QStringLiteral("controlsLocked"), m_window->m_controlsLocked},
                       {QStringLiteral("selectedVfo"), vfoName(m_window->m_vfoSelectionController->selectedVfo())},
                       {QStringLiteral("dialLock"),
                        shared.dialLockEnabled ? QJsonValue(*shared.dialLockEnabled) : QJsonValue(QJsonValue::Null)},
                       {QStringLiteral("afGain"), optionalJsonValue(shared.afGain)},
                       {QStringLiteral("txPower"), optionalJsonValue(shared.txPower)},
                       {QStringLiteral("lanModLevel"), optionalJsonValue(shared.lanModLevel)},
                       {QStringLiteral("compressorEnabled"), optionalJsonValue(shared.compressorEnabled)},
                       {QStringLiteral("compressorLevel"), optionalJsonValue(shared.compressorLevel)},
                       {QStringLiteral("dualWatch"),
                        shared.dualWatchEnabled ? QJsonValue(*shared.dualWatchEnabled) : QJsonValue(QJsonValue::Null)},
                       {QStringLiteral("receivers"),
                        QJsonObject{{QStringLiteral("MAIN"), receiverSnapshot(state->receiver(Vfo::Main))},
                                    {QStringLiteral("SUB"), receiverSnapshot(state->receiver(Vfo::Sub))}}}};
}

QJsonObject AutomationController::execute(const QJsonObject& request)
{
    const QString action = request.value(QStringLiteral("action")).toString();
    if (action == QLatin1String("ping"))
    {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("protocol"), kProtocolVersion}};
    }
    if (action == QLatin1String("list_actions"))
    {
        // This list is also a security declaration: transmit operations are
        // intentionally absent and unknown action names are rejected below.
        return QJsonObject{
            {QStringLiteral("ok"), true},
            {QStringLiteral("actions"),
             QJsonArray{QStringLiteral("ping"), QStringLiteral("list_actions"), QStringLiteral("get_state"),
                        QStringLiteral("select_vfo"), QStringLiteral("set_frequency"), QStringLiteral("select_band"),
                        QStringLiteral("set_dual_watch"), QStringLiteral("exchange_main_sub"),
                        QStringLiteral("ui_list"), QStringLiteral("ui_activate"), QStringLiteral("ui_set")}}};
    }
    if (action == QLatin1String("get_state"))
    {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("state"), stateSnapshot()}};
    }
    if (action == QLatin1String("ui_list"))
    {
        return m_uiDriver->listControls();
    }
    if (action == QLatin1String("ui_activate"))
    {
        return m_uiDriver->activate(request);
    }
    if (action == QLatin1String("ui_set"))
    {
        return m_uiDriver->setValue(request);
    }

    const bool allowlistedMutation =
        action == QLatin1String("select_vfo") || action == QLatin1String("set_frequency") ||
        action == QLatin1String("select_band") || action == QLatin1String("set_dual_watch") ||
        action == QLatin1String("exchange_main_sub");
    if (!allowlistedMutation)
    {
        return reject(QStringLiteral("unknown_action"),
                      QStringLiteral("Action is not in the receive-control automation allowlist"));
    }

    if (!receiveControlReady())
    {
        return reject(QStringLiteral("radio_not_ready"),
                      QStringLiteral("Receive controls require a ready, unlocked, non-transmitting radio"));
    }

    if (action == QLatin1String("select_vfo"))
    {
        const auto vfo = parseVfo(request.value(QStringLiteral("vfo")));
        if (!vfo)
        {
            return reject(QStringLiteral("invalid_vfo"), QStringLiteral("vfo must be MAIN or SUB"));
        }
        if (!m_window->m_vfoSelectionController->selectVfo(*vfo))
        {
            return reject(QStringLiteral("request_rejected"),
                          QStringLiteral("VFO selection requires ready dual-watch receivers"));
        }
    }
    else if (action == QLatin1String("set_frequency"))
    {
        const auto vfo = parseVfo(request.value(QStringLiteral("vfo")));
        const qint64 frequencyHz = request.value(QStringLiteral("frequencyHz")).toInteger(-1);
        if (!vfo)
        {
            return reject(QStringLiteral("invalid_vfo"), QStringLiteral("vfo must be MAIN or SUB"));
        }
        if (frequencyHz < 0 || sdr9700::radioBandForFrequency(static_cast<quint64>(frequencyHz)) == bandUnknown)
        {
            return reject(QStringLiteral("invalid_frequency"),
                          QStringLiteral("frequencyHz must be inside an IC-9700 amateur band"));
        }
        (*vfo == Vfo::Main ? m_window->m_mainVfoController : m_window->m_subVfoController)
            ->requestFrequencyHz(static_cast<quint64>(frequencyHz));
    }
    else if (action == QLatin1String("select_band"))
    {
        const auto vfo = parseVfo(request.value(QStringLiteral("vfo")));
        const auto band = parseBand(request.value(QStringLiteral("band")));
        if (!vfo || !band)
        {
            return reject(QStringLiteral("invalid_argument"),
                          QStringLiteral("select_band requires vfo MAIN/SUB and band 2m/70cm/23cm"));
        }
        const VfoController* other = *vfo == Vfo::Main ? m_window->m_subVfoController : m_window->m_mainVfoController;
        if (other->band() == *band)
        {
            return reject(QStringLiteral("request_rejected"),
                          QStringLiteral("The requested band is already assigned to the other VFO"));
        }
        if (!(*vfo == Vfo::Main ? m_window->m_mainVfoController : m_window->m_subVfoController)->selectBand(*band))
        {
            return reject(QStringLiteral("request_rejected"),
                          QStringLiteral("A band change is already awaiting radio confirmation"));
        }
    }
    else if (action == QLatin1String("set_dual_watch"))
    {
        const QJsonValue enabled = request.value(QStringLiteral("enabled"));
        if (!enabled.isBool() || !m_window->m_vfoSelectionController->requestDualWatch(enabled.toBool()))
        {
            return reject(QStringLiteral("request_rejected"), QStringLiteral("Dual-watch change was not accepted"));
        }
    }
    else if (action == QLatin1String("exchange_main_sub"))
    {
        if (!m_window->m_vfoSelectionController->requestMainSubExchange())
        {
            return reject(QStringLiteral("request_rejected"), QStringLiteral("MAIN/SUB exchange is already busy"));
        }
    }
    // Accepted means the existing radio controller accepted an asynchronous
    // request. It never claims the radio has confirmed the new state; clients
    // use get_state to observe the same confirmed projection used by the UI.
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("status"), QStringLiteral("accepted")},
                       {QStringLiteral("state"), stateSnapshot()}};
}
