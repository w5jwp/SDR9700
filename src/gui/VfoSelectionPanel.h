#pragma once

#include "Vfo.h"

#include <QWidget>

class QPushButton;

class VfoSelectionPanel : public QWidget
{
    Q_OBJECT

  public:
    explicit VfoSelectionPanel(QWidget* parent = nullptr);

    void setSelectedVfo(Vfo vfo);
    void setDualWatchEnabled(bool enabled);
    void setDualWatchPending(bool pending);
    void setExchangePending(bool pending);
    void setReceiverContextReady(bool ready);
    void setRadioReady(bool ready);
    void setControlsEnabled(bool enabled);
    void setPttButton(QPushButton* button);
    Vfo selectedVfo() const { return m_selectedVfo; }
    bool dualWatchEnabled() const { return m_dualWatchEnabled; }

  signals:
    void vfoRequested(Vfo vfo);
    void dualWatchRequested(bool enabled);
    void exchangeRequested();

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    void updateControlsEnabled();
    void updateButtonStyles();

    QPushButton* m_mainButton{nullptr};
    QPushButton* m_subButton{nullptr};
    QPushButton* m_dualWatchButton{nullptr};
    QPushButton* m_exchangeButton{nullptr};
    QPushButton* m_pttButton{nullptr};
    Vfo m_selectedVfo{Vfo::Main};
    bool m_dualWatchEnabled{false};
    bool m_dualWatchPending{false};
    bool m_exchangePending{false};
    bool m_receiverContextReady{true};
    bool m_radioReady{false};
    bool m_controlsEnabled{true};
};
