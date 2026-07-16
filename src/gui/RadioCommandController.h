#pragma once

#include "Types.h"

#include <QObject>

class MainWindow;

class RadioCommandController : public QObject
{
  public:
    explicit RadioCommandController(MainWindow* window);

    void toggleMute();
    void cycleMode();
    void toggleRit();
    void showAgcMenu();
    void showPreampMenu();
    void updatePreampButton();
    void showNotchMenu();
    void updateNotchButton();
    void updateRitButton();
    void showRitMenu();
    void showCustomRitDialog();
    void showOffsetMenu();
    void showCustomOffsetDialog();
    void applyOffsetSelection(duplexMode_t mode, quint64 offsetHz);
    void updateOffsetButton();
    void showToneMenu();
    void applyToneSelection(rptAccessTxRx_t mode, ushort value);
    void updateToneButton();
    void updateSquelchButton();
    void updateTxPowerButton();
    void updateRfGainButton();
    void showRfGainMenu();
    int tuningStepHz() const;
    void applyRadioTuningStep();
    void updateStepButton();

  private:
    MainWindow* m_window{nullptr};
};
