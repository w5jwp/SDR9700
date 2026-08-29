#pragma once

#include "Types.h"

#include <QObject>
#include <QPoint>
#include <functional>

class MainWindow;

class RadioCommandController : public QObject
{
  public:
    using CompressorLevelSetter = std::function<void(int)>;

    explicit RadioCommandController(MainWindow* window, CompressorLevelSetter compressorLevelSetter = {});

    void toggleMute();
    void cycleMode();
    void toggleRit();
    void updateRitButton();
    void showRitMenu();
    void showCustomRitDialog();
    void showOffsetMenu(const QPoint& position = {});
    void showCustomOffsetDialog();
    void applyOffsetSelection(duplexMode_t mode, quint64 offsetHz);
    void updateOffsetButton();
    void showToneMenu(const QPoint& position = {});
    void applyToneSelection(rptAccessTxRx_t mode, ushort value);
    void updateToneButton();
    void showCompressorMenu(const QPoint& position = {});
    int tuningStepHz() const;
    void applyRadioTuningStep();
    void updateStepButton();

  private:
    MainWindow* m_window{nullptr};
    CompressorLevelSetter m_compressorLevelSetter;
};
