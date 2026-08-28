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
    void setExchangePending(bool pending);
    Vfo selectedVfo() const { return m_selectedVfo; }
    bool dualWatchEnabled() const { return m_dualWatchEnabled; }

  signals:
    void vfoRequested(Vfo vfo);
    void dualWatchRequested(bool enabled);
    void exchangeRequested();

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    void updateButtonStyles();

    QPushButton* m_mainButton{nullptr};
    QPushButton* m_subButton{nullptr};
    QPushButton* m_dualWatchButton{nullptr};
    QPushButton* m_exchangeButton{nullptr};
    Vfo m_selectedVfo{Vfo::Main};
    bool m_dualWatchEnabled{false};
};
