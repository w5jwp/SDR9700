#pragma once

#include "Vfo.h"

#include <QObject>

class IRadioBackend;
class VfoController;
class VfoSelectionPanel;
class QWidget;

class VfoSelectionController : public QObject
{
    Q_OBJECT

  public:
    explicit VfoSelectionController(IRadioBackend* backend, VfoController* mainController, VfoController* subController,
                                    QWidget* displayParent, QObject* parent = nullptr);

    VfoSelectionPanel* panel() const { return m_panel; }
    Vfo selectedVfo() const { return m_selectedVfo; }
    void setControlsEnabled(bool enabled);

  signals:
    void selectedVfoChanged(Vfo vfo);
    void pttReadyChanged(bool ready);

  private:
    void reset();
    void setPttReady(bool ready);
    void updateTransmitIndicators();

    IRadioBackend* m_backend{nullptr};
    VfoController* m_mainController{nullptr};
    VfoController* m_subController{nullptr};
    VfoSelectionPanel* m_panel{nullptr};
    Vfo m_selectedVfo{Vfo::Main};
    Vfo m_requestedVfo{Vfo::Main};
    bool m_selectionPending{false};
    bool m_exchangePending{false};
    bool m_pttReady{true};
    bool m_transmitting{false};
};
