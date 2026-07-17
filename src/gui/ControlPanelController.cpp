#include "ControlPanelController.h"

#include "MainWindow.h"

ControlPanelController::ControlPanelController(MainWindow* window) : QObject(window), m_window(window) {}

void ControlPanelController::buildControlPanel(QVBoxLayout* vbox)
{
    // MainWindow still owns the actual widgets because many radio and memory
    // flows reference them directly. This controller is the responsibility
    // boundary for future control-strip work; keep behavior identical while
    // the high-risk wiring is moved out in smaller follow-up slices.
    m_window->buildControlPanelContent(vbox);
}
