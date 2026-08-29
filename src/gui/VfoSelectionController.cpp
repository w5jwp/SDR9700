#include "VfoSelectionController.h"

#include "VfoController.h"
#include "VfoSelectionPanel.h"
#include "MainWindowHelpers.h"
#include "backend/IRadioBackend.h"

#include <QAction>
#include <QMenu>

VfoSelectionController::VfoSelectionController(IRadioBackend* backend, VfoController* mainController,
                                               VfoController* subController, QWidget* displayParent, QObject* parent)
    : QObject(parent),
      m_backend(backend),
      m_mainController(mainController),
      m_subController(subController),
      m_panel(new VfoSelectionPanel(displayParent))
{
    auto showBandMenu = [this](Vfo vfo, const QPoint& position)
    {
        VfoController* target = vfo == Vfo::Main ? m_mainController : m_subController;
        const VfoController* other = vfo == Vfo::Main ? m_subController : m_mainController;
        QMenu menu(m_panel);
        sdr9700::ui::main_window::styleCompactMenu(&menu);
        for (const availableBands band : sdr9700::kRadioUiBandOrder)
        {
            if (band == other->band())
            {
                continue;
            }
            QAction* action = menu.addAction(sdr9700::radioBandMenuLabel(band));
            action->setData(static_cast<int>(band));
        }
        if (const QAction* chosen = menu.exec(position))
        {
            target->selectBand(static_cast<availableBands>(chosen->data().toInt()));
        }
    };
    connect(m_mainController, &VfoController::bandMenuRequested, this, showBandMenu);
    connect(m_subController, &VfoController::bandMenuRequested, this, showBandMenu);
    connect(m_panel, &VfoSelectionPanel::vfoRequested, this, &VfoSelectionController::requestSelection);
    connect(m_mainController, &VfoController::selectionRequested, this, &VfoSelectionController::requestSelection);
    connect(m_subController, &VfoController::selectionRequested, this, &VfoSelectionController::requestSelection);
    connect(m_panel, &VfoSelectionPanel::dualWatchRequested, this,
            [this](bool enabled)
            {
                if (m_backend)
                {
                    m_backend->setDualWatchEnabled(enabled);
                }
            });
    connect(m_panel, &VfoSelectionPanel::exchangeRequested, this,
            [this]()
            {
                if (!m_backend || m_exchangePending || m_transmitting)
                {
                    return;
                }
                m_exchangePending = true;
                m_selectionPending = false;
                m_mainController->captureExchangeableControlState();
                m_subController->captureExchangeableControlState();
                m_panel->setExchangePending(true);
                setPttReady(false);
                m_backend->exchangeMainSub();
            });
    if (m_backend)
    {
        connect(m_backend, &IRadioBackend::mainSubExchangeCompleted, this,
                [this]()
                {
                    if (!m_exchangePending)
                    {
                        return;
                    }
                    m_exchangePending = false;
                    m_mainController->applyCapturedControlExchange(m_subController);
                    m_panel->setExchangePending(false);
                    const bool changed = m_selectedVfo != Vfo::Main;
                    m_selectedVfo = Vfo::Main;
                    m_requestedVfo = Vfo::Main;
                    m_panel->setSelectedVfo(Vfo::Main);
                    m_mainController->setSelected(true);
                    m_subController->setSelected(false);
                    updateTransmitIndicators();
                    if (changed)
                    {
                        emit selectedVfoChanged(Vfo::Main);
                    }
                    setPttReady(true);
                });
        connect(m_backend, &IRadioBackend::pttChanged, this,
                [this](bool transmitting)
                {
                    m_transmitting = transmitting;
                    updateTransmitIndicators();
                });
        connect(m_backend, &IRadioBackend::radioValueUpdated, this,
                [this](Funcs func, const QVariant& value, uchar receiver)
                {
                    if (receiver != 0)
                    {
                        return;
                    }
                    if (func == funcVFOBandMS)
                    {
                        const Vfo selected = value.toBool() ? Vfo::Sub : Vfo::Main;
                        if (m_selectionPending && selected != m_requestedVfo)
                        {
                            return;
                        }
                        if (m_selectionPending)
                        {
                            m_selectionPending = false;
                            setPttReady(true);
                        }
                        m_requestedVfo = selected;
                        const bool changed = m_selectedVfo != selected;
                        m_selectedVfo = selected;
                        m_panel->setSelectedVfo(selected);
                        m_mainController->setSelected(selected == Vfo::Main);
                        m_subController->setSelected(selected == Vfo::Sub);
                        updateTransmitIndicators();
                        if (changed)
                        {
                            emit selectedVfoChanged(selected);
                        }
                        if (m_selectedAction && selected == m_requestedVfo)
                        {
                            auto action = std::move(m_selectedAction);
                            action();
                        }
                    }
                    else if (func == funcVFODualWatch)
                    {
                        const bool enabled = value.toBool();
                        m_panel->setDualWatchEnabled(enabled);
                        m_subController->setOperatingEnabled(enabled);
                        if (!enabled)
                        {
                            if (m_selectedVfo != Vfo::Main && m_backend)
                            {
                                requestSelection(Vfo::Main);
                            }
                            m_subController->clearFrequency();
                        }
                    }
                });
        connect(m_backend, &IRadioBackend::readyChanged, this,
                [this](bool ready)
                {
                    if (!ready)
                    {
                        reset();
                    }
                });
    }
    reset();
    setControlsEnabled(false);
}

void VfoSelectionController::requestSelection(Vfo vfo)
{
    m_selectedAction = {};
    if (!m_backend)
    {
        return;
    }
    m_requestedVfo = vfo;
    m_selectionPending = true;
    setPttReady(false);
    m_backend->selectVfo(vfo);
}

void VfoSelectionController::runWhenSelected(Vfo vfo, std::function<void()> action)
{
    if (!action)
    {
        return;
    }
    if (!m_selectionPending && m_selectedVfo == vfo)
    {
        action();
        return;
    }
    requestSelection(vfo);
    m_selectedAction = std::move(action);
}

void VfoSelectionController::setControlsEnabled(bool enabled)
{
    m_panel->setEnabled(enabled);
}

void VfoSelectionController::reset()
{
    m_selectedVfo = Vfo::Main;
    m_requestedVfo = Vfo::Main;
    m_selectionPending = false;
    m_exchangePending = false;
    m_selectedAction = {};
    m_panel->setExchangePending(false);
    setPttReady(true);
    m_transmitting = false;
    m_panel->setSelectedVfo(Vfo::Main);
    m_mainController->setSelected(true);
    m_subController->setSelected(false);
    m_panel->setDualWatchEnabled(false);
    m_mainController->setOperatingEnabled(true);
    m_subController->setOperatingEnabled(false);
    m_subController->clearFrequency();
    updateTransmitIndicators();
}

void VfoSelectionController::setPttReady(bool ready)
{
    if (m_pttReady == ready)
    {
        return;
    }
    m_pttReady = ready;
    emit pttReadyChanged(ready);
}

void VfoSelectionController::updateTransmitIndicators()
{
    m_mainController->setTransmitting(m_transmitting);
    m_subController->setTransmitting(false);
}
