// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QMainWindow>
#include <QCloseEvent>
#include <QElapsedTimer>
#include <QPointer>
#include "RadioProfile.h"
#include "Types.h"
#include "models/MeterController.h"

class RadioModel;
class VfoModel;
class SpectrumScopeModel;
class SpectrumScopeController;
class SpectrumScopeDisplay;
class ControlPanelController;
class VfoPanel;
class MemoryPanel;
class QLabel;
class QToolBar;
class QAction;
class QSlider;
class QPushButton;
class QLineEdit;
class QProgressBar;
class QStatusBar;
class QPixmap;
class QVBoxLayout;
class QGroupBox;
class QWidget;
class QComboBox;
class QDialog;
class QTableWidget;
class QTimer;
class DtmfDialog;
class IcomRC28Controller;
class MainTitleBar;
class MemoryController;
class MemoryCsvController;
class MemoryEditorController;
class MemoryWriteController;
class MetersDialog;
class RadioCommandController;
class RadioChooserDialog;
class SettingsDialog;
class StatusBarController;
#ifdef HAVE_HIDAPI
class IcomRC28Manager;
#endif

class MainWindow : public QMainWindow
{
    Q_OBJECT
    friend class SpectrumScopeController;
    friend class ControlPanelController;
    friend class IcomRC28Controller;
    friend class MemoryController;
    friend class MemoryCsvController;
    friend class MemoryEditorController;
    friend class MemoryWriteController;
    friend class RadioCommandController;
    friend class StatusBarController;

  public:
    explicit MainWindow(RadioModel* model, QWidget* parent = nullptr);

  private slots:
    void onConnectToProfile(const RadioProfile& profile);
    void onConnectionChanged(bool connected);
    void onRadioReadyChanged(bool ready);
    void onFrequencyChanged(quint64 hz);
    void onModeChanged(const QString& mode);
    void onMeterSnapshotChanged(const MeterSnapshot& snapshot);
    void onConnectionStageChanged(ConnectionStage stage, const QString& message);
    void onStatusMessage(const QString& msg, MessageSeverity severity);
    void onError(ErrorCode code, const QString& msg);

    void onAfGainChanged(int value);
    void onRfGainChanged(int value);
    void updateSquelchButton();
    void showRfGainMenu();
    void updateRfGainButton();
    void onTxPowerChanged(int value);
    void updateTxPowerButton();
    void onPttPressed();
    void onPttReleased();
    void onPttChanged(bool on);
    void onDuplexModeChanged(duplexMode_t mode);
    void onRepeaterOffsetChanged(quint64 hz);
    void onToneAccessModeChanged(rptAccessTxRx_t mode);
    void onToneFrequencyChanged(ushort tone);
    void onDtcsCodeChanged(ushort code);
    void onDtmfSendRequested(const QString& digits);

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    void buildToolBar();
    void buildControlPanel(QVBoxLayout* vbox);
    void buildControlPanelContent(QVBoxLayout* vbox);
    void buildStatusBar();
    void centerPopupWindow(QWidget* popup) const;
    void bringDialogToFront(QWidget* dialog) const;
    void updateWindowTitle();
    void showSettingsDialog();
    void showRadioChooserDialog();
    void tryAutoConnect();
    void restoreWindowLayout();
    void saveWindowLayout() const;
    void showAgcMenu();
    void showPreampMenu();
    void updatePreampButton();
    void showNotchMenu();
    void updateNotchButton();
    void showRitMenu();
    void showCustomRitDialog();
    void updateRitButton();
    void toggleMute();
    void cycleMode();
    void toggleRit();
    void handleIcomRC28Button(int button, int action);
    void handleIcomRC28Tune(int steps);
    void dispatchIcomRC28Action(const QString& action);
    void setIcomRC28Ptt(bool on);
    void refreshIcomRC28EncoderSettings();
    void snapIcomRC28FrequencyToKhz();
    void showOffsetMenu();
    void showCustomOffsetDialog();
    void applyOffsetSelection(duplexMode_t mode, quint64 offsetHz);
    void updateOffsetButton();
    void showToneMenu();
    void applyToneSelection(rptAccessTxRx_t mode, ushort value);
    void updateToneButton();
    void updateStepButton();
    void updateIcomRC28Leds();
    void showDtmfDialog();
    void showMetersDialog();
    void buildMemoryWindow();
    void showMemoryWindow();
    void storeCurrentMemory();
    void editSelectedMemory();
    void copySelectedMemory();
    void removeSelectedMemory();
    void moveSelectedMemoryUp();
    void moveSelectedMemoryDown();
    void moveSelectedMemory(int direction);
    void selectCheckedMemory();
    void reloadMemoryTable();
    void selectMemoryById(const QString& id, bool showDialogOnFailure);
    QString selectedMemoryId() const;
    void showMemoryEditor(const QString& memoryId);
    void setRadioControlsEnabled(bool enabled);
    bool radioUiReady() const;
    void resetRadioOwnedControlsForSync();
    void applyActiveVfoFromRadio();
    void updateConnectionTooltip();
    void checkIfMemorySelectionComplete();

    MainTitleBar* m_titleBar{nullptr};
    SpectrumScopeController* m_spectrumScopeController{nullptr};
    ControlPanelController* m_controlPanelController{nullptr};
    IcomRC28Controller* m_icomRC28Controller{nullptr};
    MemoryController* m_memoryController{nullptr};
    RadioCommandController* m_radioCommandController{nullptr};
    StatusBarController* m_statusBarController{nullptr};
    RadioModel* m_model{nullptr};
    QUuid m_pendingProfileId;
    VfoModel* m_vfo{nullptr};
    SpectrumScopeModel* m_spectrumScope{nullptr};

    SpectrumScopeDisplay* m_spectrumScopeDisplay{nullptr};

    VfoPanel* m_vfoPanel{nullptr};
    MemoryPanel* m_memoryPanel{nullptr};
    QPushButton* m_rfGainBtn{nullptr};
    int m_rfGainValue{128};
    QPushButton* m_squelchBtn{nullptr};
    int m_squelchValue{0};
    int m_lanModValue{128};
    QPushButton* m_txPowerBtn{nullptr};
    int m_txPowerValue{128};
    quint64 m_vfoFrequencyHz{0};
    quint64 m_lastBandFrequencyHz[3]{0, 0, 0};
    QPushButton* m_muteBtn{nullptr};
    QPushButton* m_agcBtn{nullptr};
    QPushButton* m_attBtn{nullptr};
    QPushButton* m_compBtn{nullptr};
    QPushButton* m_nbBtn{nullptr};
    QPushButton* m_notchBtn{nullptr};
    QPushButton* m_nrBtn{nullptr};
    QPushButton* m_preBtn{nullptr};
    QPushButton* m_ritBtn{nullptr};
    QPushButton* m_offsetBtn{nullptr};
    QPushButton* m_toneBtn{nullptr};
    QPushButton* m_xfcBtn{nullptr};
    QPushButton* m_pttBtn{nullptr};
    DtmfDialog* m_dtmfDialog{nullptr};
    MetersDialog* m_metersDialog{nullptr};
    QPointer<SettingsDialog> m_settingsDialog;
    QPointer<RadioChooserDialog> m_radioChooserDialog;
    QTimer* m_dtmfPttOffTimer{nullptr};
    bool m_dtmfSendActive{false};

    QDialog* m_memoryWindow{nullptr};
    QComboBox* m_memoryBandFilter{nullptr};
    QTableWidget* m_memoryTable{nullptr};
    QLabel* m_memoryCountLabel{nullptr};
    QProgressBar* m_memoryProgressBar{nullptr};
    int m_currentAfGain{128};
    int m_savedAfGain{128};
    MeterSnapshot m_meterSnapshot;
    bool m_muted{false};
    bool m_applyingRadioSliderUpdate{false};
    duplexMode_t m_duplexMode{dmSimplex};
    quint64 m_repeaterOffsetHz{600000};
    rptAccessTxRx_t m_toneAccessMode{ratrNN};
    ushort m_toneFrequency{670};
    ushort m_dtcsCode{23};
    QString m_activeMemoryId;
    QString m_activeMemoryName;
    quint64 m_activeMemoryFrequencyHz{0};
    duplexMode_t m_activeMemoryDuplexMode{dmSimplex};
    quint64 m_activeMemoryOffsetHz{0};
    rptAccessTxRx_t m_activeMemoryToneMode{ratrNN};
    ushort m_activeMemoryToneValue{0};
    bool m_applyingMemorySelection{false};
    bool m_activeMemorySelectionReleaseScheduled{false};
    int m_memorySelectionGeneration{0};
    bool m_activeMemoryFrequencySettled{false};
    bool m_activeMemoryDuplexSettled{false};
    bool m_activeMemoryOffsetSettled{false};
    bool m_activeMemoryToneModeSettled{false};
    bool m_activeMemoryToneValueSettled{false};

    QWidget* m_lockWidget{nullptr};
    QLabel* m_lockIndicator{nullptr};
    QLabel* m_toastLabel{nullptr};
    QLabel* m_connStateLabel{nullptr};
    QLabel* m_connDetailLabel{nullptr};
    QLabel* m_netTitleLabel{nullptr};
    QLabel* m_netQualLabel{nullptr};
    QLabel* m_txIndicator{nullptr};
    QLabel* m_txSwrLabel{nullptr};
    QLabel* m_dateLabel{nullptr};
    QLabel* m_timeLabel{nullptr};
    bool m_statusClockUtc{true};
    QLabel* m_cpuLabel{nullptr};
    QLabel* m_memLabel{nullptr};
    bool m_controlsLocked{false};

#ifdef HAVE_HIDAPI
    IcomRC28Manager* m_icomRC28Manager{nullptr};
    QTimer* m_icomRC28HoldTimers[2]{nullptr, nullptr};
    bool m_icomRC28HoldConsumed[2]{false, false};
    bool m_icomRC28ButtonDown[2]{false, false};
    bool m_icomRC28PttLatched{false};
    QTimer* m_icomRC28SnapTimer{nullptr};
    int m_icomRC28PulseAccum{0};
    int m_icomRC28Sensitivity{1};
    bool m_icomRC28AutoSnap{false};
#endif

    QLabel* m_statusLabel{nullptr};

    QString m_connStateName;
    QTimer* m_toastTimer{nullptr};

    QTimer* m_reconnectTimer{nullptr};
    bool m_reconnecting{false};
    bool m_userDisconnected{false};
    bool m_lastErrorWasCredential{false};
    bool m_allowChooserOnDisconnect{false};
    bool m_radioUiReadyNotified{false};
    bool m_spectrumScopeStillSyncingAfterReady{false};

    QTimer* m_txDurationTimer{nullptr};
    QElapsedTimer m_txElapsed;
    bool m_txActive{false};
    QTimer* m_spectrumScopeTuneCommitTimer{nullptr};
    QTimer* m_spectrumScopeTuneReleaseTimer{nullptr};
    quint64 m_pendingSpectrumScopeTuneHz{0};
    quint64 m_displaySpectrumScopeTuneHz{0};
    quint64 m_spectrumScopeDisplayCenterHz{0};
    quint64 m_spectrumScopeFixedPanStartHz{0};
    quint64 m_spectrumScopeFixedPanEndHz{0};

    QString m_radioHost;
    quint16 m_radioPort{0};
    QString m_radioUsername;

    void updateSpectrumVfoMarker();
    void updateTxDurationLabel();
    enum class ToastKind
    {
        Info,
        Warning,
        Error,
    };
    void showToast(const QString& msg, int durationMs = 4000, ToastKind kind = ToastKind::Info);
    void updateNetworkQuality(int rttMs);

    void updateTxIndicator(bool on);
    void updateStatusClock();
    void toggleStatusClockMode();
    void updateSystemStats();
    void toggleControlLock();
    void updateControlLockIndicator();
    void updateSpectrumScopeBandLimits(quint64 hz);
    int tuningStepHz() const;
    void applyRadioTuningStep();
    void applySpectrumScopeSettings();
    quint64 roundFrequencyToStep(quint64 hz) const;
    void panSpectrumScopeToCenter(quint64 centerHz);
    quint64 clampSpectrumScopeCenterHz(quint64 hz, double bandwidthMhz) const;
    quint64 clampFrequencyHzToActiveBand(quint64 hz) const;
    void scheduleSpectrumScopeTune(quint64 hz);
    void setActiveMemory(const QString& id, const QString& name, quint64 frequencyHz, int duplexMode, quint64 offsetHz,
                         int toneMode, ushort toneValue);
    void clearActiveMemory();
    void leaveMemoryModeForManualFrequencyChange();
    void updateMemoryNameLabel();
    void commitFrequencyEdit(VfoPanel* panel);
};
