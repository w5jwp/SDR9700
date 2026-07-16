#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "BandscopeDisplay.h"
#include "RadioChooserDialog.h"
#include "SettingsDialog.h"
#include "AboutDialog.h"
#include "DialogPlacement.h"
#include "DtmfDialog.h"
#include "BandscopeController.h"
#include "IcomRC28Controller.h"
#include "MainTitleBar.h"
#include "MemoryController.h"
#include "MemoryPanel.h"
#include "MetersDialog.h"
#include "PttPanel.h"
#include "RadioCommandController.h"
#include "ReceivePanel.h"
#include "StatusBarController.h"
#include "VfoPanel.h"
#include "UiTheme.h"
#include "UtilityWindow.h"
#include "ConfigurationManager.h"
#include "MemoryStore.h"
#include "AppBuildConfig.h"
#include "AppInfo.h"
#include "AppSettings.h"
#include "LogCategories.h"
#include "RadioCapabilities.h"
#include "backend/IRadioBackend.h"
#ifdef HAVE_HIDAPI
#include "core/IcomRC28Manager.h"
#endif
#include "models/RadioModel.h"
#include "models/VfoModel.h"
#include "models/BandscopeModel.h"

#include <QToolBar>
#include <QAction>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSlider>
#include <QSpinBox>
#include <QPushButton>
#include <QStatusBar>
#include <QGuiApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QColor>
#include <QFormLayout>
#include <QFile>
#include <QHeaderView>
#include <QMessageBox>
#include <QCloseEvent>
#include <QEvent>
#include <QFont>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QScreen>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTableWidget>
#include <QStringList>
#include <QTimer>
#include <QDateTime>
#include <QVector>
#include <QWidgetAction>
#include <QUuid>
#include <algorithm>
#include <functional>
#include <iterator>
#include <numeric>

using namespace sdr9700::ui::main_window;

MainWindow::MainWindow(RadioModel* model, QWidget* parent)
    : QMainWindow(parent), m_model(model), m_vfo(model->vfo()), m_bandscope(model->bandscope())
{
    m_bandscopeController = new BandscopeController(this);
    m_icomRC28Controller = new IcomRC28Controller(this);
    m_memoryController = new MemoryController(this);
    m_radioCommandController = new RadioCommandController(this);
    m_statusBarController = new StatusBarController(this);

    setWindowFlag(Qt::FramelessWindowHint);
    updateWindowTitle();
    setFixedSize(UiTheme::Size::MainWindowMinWidth, UiTheme::Size::MainWindowMinHeight);

    auto* central = new QWidget(this);
    auto* vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);
    setCentralWidget(central);

    buildToolBar();
    buildControlPanel(vbox);
    m_bandscopeController->buildBandscope(vbox);
    buildMemoryWindow();
    restoreWindowLayout();
    buildStatusBar();

    connect(m_model, &RadioModel::connectionChanged, this, &MainWindow::onConnectionChanged);
    connect(m_model, &RadioModel::readyChanged, this, &MainWindow::onRadioReadyChanged);
    connect(m_model, &RadioModel::meterSnapshotChanged, this, &MainWindow::onMeterSnapshotChanged);
    connect(m_model, &RadioModel::pttChanged, this, &MainWindow::onPttChanged);
    connect(m_model, &RadioModel::statusMessage, this, &MainWindow::onStatusMessage);
    connect(m_model, &RadioModel::errorOccurred, this, &MainWindow::onError);
    connect(m_model, &RadioModel::networkQualityChanged, this, &MainWindow::updateNetworkQuality);

    connect(m_vfo, &VfoModel::frequencyChanged, this, &MainWindow::onFrequencyChanged);
    connect(m_vfo, &VfoModel::modeChanged, this, &MainWindow::onModeChanged);
    connect(m_vfo, &VfoModel::duplexModeChanged, this, &MainWindow::onDuplexModeChanged);
    connect(m_vfo, &VfoModel::repeaterOffsetChanged, this, &MainWindow::onRepeaterOffsetChanged);
    connect(m_vfo, &VfoModel::toneAccessModeChanged, this, &MainWindow::onToneAccessModeChanged);
    connect(m_vfo, &VfoModel::toneFrequencyChanged, this, &MainWindow::onToneFrequencyChanged);
    connect(m_vfo, &VfoModel::dtcsCodeChanged, this, &MainWindow::onDtcsCodeChanged);
    connect(m_vfo, &VfoModel::nrChanged, this, [this](bool on, int) { setCommandButtonActive(m_nrBtn, on); });
    connect(m_vfo, &VfoModel::nbChanged, this, [this](bool on, int) { setCommandButtonActive(m_nbBtn, on); });
    connect(m_vfo, &VfoModel::preampChanged, this, [this](bool) { updatePreampButton(); });
    connect(m_vfo, &VfoModel::preampLevelChanged, this, [this](int) { updatePreampButton(); });
    connect(m_vfo, &VfoModel::attenuatorChanged, this, [this](bool on) { setCommandButtonActive(m_attBtn, on); });
    connect(m_vfo, &VfoModel::autoNotchChanged, this, [this](bool) { updateNotchButton(); });
    connect(m_vfo, &VfoModel::manualNotchChanged, this, [this](bool) { updateNotchButton(); });
    connect(m_vfo, &VfoModel::compressorChanged, this, [this](bool on) { setCommandButtonActive(m_compBtn, on); });
    connect(m_vfo, &VfoModel::xfcChanged, this, [this](bool on) { setCommandButtonActive(m_xfcBtn, on); });
    connect(m_vfo, &VfoModel::ritChanged, this, [this](bool, short) { updateRitButton(); });
    connect(m_vfo, &VfoModel::agcModeChanged, this,
            [this](const QString& mode) { setSelectorButtonLines(m_agcBtn, QStringLiteral("AGC"), mode.toUpper()); });
    connect(m_vfo, &VfoModel::rfGainChanged, this, &MainWindow::onRfGainChanged);
    connect(m_vfo, &VfoModel::squelchChanged, this,
            [this](bool, int level)
            {
                m_squelchValue = level;
                if (m_vfoPanel)
                {
                    m_vfoPanel->setSquelch(level);
                }
                updateSquelchButton();
            });
    connect(m_vfo, &VfoModel::txPowerChanged, this, &MainWindow::onTxPowerChanged);
    if (auto* backend = m_model->backend())
    {
        connect(backend, &IRadioBackend::radioValueUpdated, this,
                [this](Funcs func, const QVariant& value, uchar receiver)
                {
                    if (receiver != 0)
                    {
                        return;
                    }
                    auto* receiverPanel = m_vfoPanel;
                    switch (func)
                    {
                    case funcVFOBandMS:
                        applyActiveVfoFromRadio();
                        break;
                    case funcFreqGet:
                    case funcFreqSet:
                    case funcSelectedFreq:
                    {
                        if (!receiverPanel)
                        {
                            break;
                        }
                        const auto f = value.value<Frequency>();
                        if (f.Hz > 0)
                        {
                            m_vfoFrequencyHz = f.Hz;
                            qInfo(logGui()) << "VFO route: MAIN frequency to VFO" << f.Hz;
                            receiverPanel->setFrequencyText(formatFrequency(f.Hz));
                            receiverPanel->setBandText(bandLabelForHz(f.Hz));
                        }
                        else
                        {
                            receiverPanel->setFrequencyText(QStringLiteral("---.---.---"));
                        }
                        break;
                    }
                    case funcModeGet:
                    case funcModeSet:
                    case funcSelectedMode:
                    {
                        if (!receiverPanel)
                        {
                            break;
                        }
                        const auto mi = value.value<ModeInfo>();
                        qInfo(logGui()) << "VFO route: MAIN mode to VFO" << mi.name.toUpper();
                        receiverPanel->setModeText(mi.name.toUpper());
                        break;
                    }
                    case funcUnselectedFreq:
                    case funcUnselectedMode:
                        // Command 25/26 unselected data is the inactive VFO inside the MAIN band,
                        // not the SUB band. Do not paint the right-hand SUB VFO from it.
                        break;
                    case funcRFPower:
                    {
                        const int level = qBound(0, value.toInt(), 255);
                        if (receiverPanel)
                        {
                            receiverPanel->setTxPower(level);
                        }
                        m_txPowerValue = level;
                        updateTxPowerButton();
                        break;
                    }
                    case funcSquelch:
                    {
                        const int level = qBound(0, value.toInt(), 255);
                        if (receiverPanel)
                        {
                            receiverPanel->setSquelch(level);
                        }
                        m_squelchValue = level;
                        updateSquelchButton();
                        break;
                    }
                    default:
                        break;
                    }
                });
    }

    resetRadioOwnedControlsForSync();

    onConnectionChanged(false);

    m_icomRC28Controller->initialize();

    QTimer::singleShot(0, this, &MainWindow::tryAutoConnect);
}

void MainWindow::buildToolBar()
{
    const QString menuStyle =
        QStringLiteral("QMenu { background: %1; border: 1px solid %2; color: %3; }"
                       "QMenu::item { padding: 5px 18px 5px 10px; }"
                       "QMenu::item:selected { background: %4; color: %5; }"
                       "QMenu::separator { height: 1px; background: %6; margin: 3px 8px; }")
            .arg(UiTheme::Color::MenuPanel, UiTheme::Color::BorderMedium, UiTheme::Color::TextPrimary,
                 UiTheme::Color::AccentDark, UiTheme::Color::White, UiTheme::Color::Border);

    m_titleBar = new MainTitleBar(this);
    m_titleBar->setTitle(
        QStringLiteral("<span style='color:#2a82da; font-size:13px; font-weight:bold;'>%1 v%2</span>")
            .arg(QString::fromLatin1(APP_NAME).toHtmlEscaped(), QString::fromLatin1(APP_VERSION).toHtmlEscaped()));

    auto* fileMenu = new QMenu(this);
    fileMenu->setStyleSheet(menuStyle);
    fileMenu->addAction("Connect to Radio", this, [this]() { showRadioChooserDialog(); });
    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction("Quit", this, &QWidget::close);
    quitAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
    m_titleBar->addMenu(QStringLiteral("&File"), fileMenu);

    m_titleBar->addAction(QStringLiteral("&Settings"), this, [this]() { showSettingsDialog(); });

    auto* viewMenu = new QMenu(this);
    viewMenu->setStyleSheet(menuStyle);
    viewMenu->addAction("DTMF", this, &MainWindow::showDtmfDialog);
    viewMenu->addAction("Memory Manager", this, &MainWindow::showMemoryWindow);
    viewMenu->addAction("Meters", this, &MainWindow::showMetersDialog);
    m_titleBar->addMenu(QStringLiteral("&View"), viewMenu);

    auto* helpMenu = new QMenu(this);
    helpMenu->setStyleSheet(menuStyle);
    helpMenu->addAction("About", this,
                        [this]()
                        {
                            AboutDialog dlg(this);
                            centerPopupWindow(&dlg);
                            dlg.exec();
                        });
    m_titleBar->addMenu(QStringLiteral("&Help"), helpMenu);

    connect(m_titleBar, &MainTitleBar::minimizeRequested, this, &QWidget::showMinimized);
    connect(m_titleBar, &MainTitleBar::closeRequested, this, &QWidget::close);
    connect(m_titleBar, &MainTitleBar::muteToggled, this, &MainWindow::toggleMute);
    connect(m_titleBar, &MainTitleBar::lockToggled, this, &MainWindow::toggleControlLock);
    connect(m_titleBar, &MainTitleBar::txDurationResetRequested, this,
            [this]()
            {
                if (m_txActive)
                {
                    m_txElapsed.restart();
                }
                m_titleBar->setTxDuration(QStringLiteral("00:00:00"), m_txActive);
            });
    connect(m_titleBar, &MainTitleBar::volumeChanged, this,
            [this](int value)
            {
                if (!m_model || !m_model->isReady() || m_controlsLocked)
                {
                    return;
                }
                const int bounded = qBound(0, value, 255);
                m_currentAfGain = bounded;
                AppSettings::instance().setValue(QStringLiteral("volumeLevel"), bounded);
                if (auto* backend = m_model->backend())
                {
                    backend->setAfGain(bounded);
                }
            });

    menuBar()->setVisible(false);
    setMenuWidget(m_titleBar);
}

void MainWindow::buildMemoryWindow()
{
    m_memoryController->buildMemoryWindow();
}

void MainWindow::centerPopupWindow(QWidget* popup) const
{
    sdr9700::ui::centerWindowOn(popup, this);
}

void MainWindow::bringDialogToFront(QWidget* dialog) const
{
    if (!dialog)
    {
        return;
    }

    if (dialog->isMinimized())
    {
        dialog->showNormal();
    }
    else
    {
        dialog->show();
    }

    centerPopupWindow(dialog);
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::showSettingsDialog()
{
    if (m_settingsDialog)
    {
        bringDialogToFront(m_settingsDialog);
        return;
    }

#ifdef HAVE_HIDAPI
    auto* dlg = new SettingsDialog(SettingsDialog::Page::AudioDevices, this, m_icomRC28Manager);
#else
    auto* dlg = new SettingsDialog(this);
#endif
    m_settingsDialog = dlg;
    connect(dlg, &QObject::destroyed, this,
            [this, dlg]()
            {
                if (m_settingsDialog == dlg)
                {
                    m_settingsDialog = nullptr;
                }
            });
    connect(dlg, &SettingsDialog::reverseMouseWheelTuningChanged, m_bandscopeDisplay,
            &BandscopeDisplay::setInvertMouseWheel);
    connect(dlg, &SettingsDialog::bandscopeCenterLineColorChanged, m_bandscopeDisplay,
            &BandscopeDisplay::setVfoMarkerColor);
    connect(dlg, &SettingsDialog::bandscopeBackgroundColorChanged, m_bandscopeDisplay,
            &BandscopeDisplay::setBackgroundColor);
    connect(dlg, &SettingsDialog::bandscopeGridLineColorChanged, m_bandscopeDisplay,
            &BandscopeDisplay::setGridLineColor);
    connect(dlg, &SettingsDialog::bandscopeGridDensityChanged, m_bandscopeDisplay, &BandscopeDisplay::setGridDensity);
#ifdef HAVE_HIDAPI
    connect(dlg, &SettingsDialog::icomRC28EncoderSettingsChanged, this,
            [this](const QString&, const QString&)
            {
                refreshIcomRC28EncoderSettings();
                if (!m_icomRC28AutoSnap && m_icomRC28SnapTimer)
                {
                    m_icomRC28SnapTimer->stop();
                }
            });
#endif
    centerPopupWindow(dlg);
    QTimer::singleShot(0, dlg, [this, dlg]() { centerPopupWindow(dlg); });
    QPointer<SettingsDialog> dlgGuard = dlg;
    connect(dlg, &QDialog::finished, this,
            [this, dlgGuard]()
            {
                if (m_settingsDialog == dlgGuard)
                {
                    m_settingsDialog = nullptr;
                }

                m_lanModValue = qBound(0, AppSettings::instance().value("LANModLevel", 128).toInt(), 255);
                if (m_vfoPanel)
                {
                    m_vfoPanel->setLanMod(m_lanModValue);
                }
                m_model->setLanModLevel(m_lanModValue);
                m_bandscopeDisplay->setInvertMouseWheel(
                    AppSettings::instance()
                        .value(QString::fromLatin1(kBandScopeInvertMouseWheelSettingsKey), "False")
                        .toBool());
#ifdef HAVE_HIDAPI
                refreshIcomRC28EncoderSettings();
#endif
                if (dlgGuard)
                {
                    dlgGuard->deleteLater();
                }
            });
    dlg->setWindowModality(Qt::NonModal);
    dlg->show();
    bringDialogToFront(dlg);
}

void MainWindow::showMemoryWindow()
{
    m_memoryController->showMemoryWindow();
}

QString MainWindow::selectedMemoryId() const
{
    return m_memoryController->selectedMemoryId();
}

void MainWindow::selectCheckedMemory()
{
    m_memoryController->selectCheckedMemory();
}

void MainWindow::selectMemoryById(const QString& id, bool showDialogOnFailure)
{
    m_memoryController->selectMemoryById(id, showDialogOnFailure);
}

void MainWindow::editSelectedMemory()
{
    m_memoryController->editSelectedMemory();
}

void MainWindow::copySelectedMemory()
{
    m_memoryController->copySelectedMemory();
}

void MainWindow::removeSelectedMemory()
{
    m_memoryController->removeSelectedMemory();
}

void MainWindow::moveSelectedMemoryUp()
{
    m_memoryController->moveSelectedMemoryUp();
}

void MainWindow::moveSelectedMemoryDown()
{
    m_memoryController->moveSelectedMemoryDown();
}

void MainWindow::moveSelectedMemory(int direction)
{
    m_memoryController->moveSelectedMemory(direction);
}

void MainWindow::updateWindowTitle()
{
    QString title = QStringLiteral("%1 %2").arg(QString::fromLatin1(APP_NAME), QString::fromLatin1(APP_VERSION));
#if SDR9700_DEBUG_BUILD
    title += QStringLiteral(" (DEBUG)");
#endif
    setWindowTitle(title);
}

void MainWindow::storeCurrentMemory()
{
    m_memoryController->storeCurrentMemory();
}

void MainWindow::showMemoryEditor(const QString& memoryId)
{
    m_memoryController->showMemoryEditor(memoryId);
}

void MainWindow::reloadMemoryTable()
{
    m_memoryController->reloadMemoryTable();
}

void MainWindow::buildControlPanel(QVBoxLayout* vbox)
{
    auto* strip = new QWidget(centralWidget());
    strip->setObjectName("controlStrip");
    strip->setMinimumWidth(UiTheme::Size::MainWindowMinWidth);
    strip->setFocusPolicy(Qt::StrongFocus);
    strip->setStyleSheet(QStringLiteral("QWidget#controlStrip { background: %1; }"
                                        "QGroupBox { color: %2; border: 1px solid %3; border-radius: 3px; "
                                        "margin-top: 8px; padding-top: 4px; font-size: 10px; font-weight: bold; }"
                                        "QGroupBox::title { subcontrol-origin: border; subcontrol-position: top left; "
                                        "left: 8px; top: -6px; padding: 0 4px; color: %4; background: %1; }"
                                        "QLabel { color: %5; }"
                                        "QLineEdit { background: %6; border: 1px solid %7; border-radius: 3px; "
                                        "color: %8; padding: 0 8px; selection-background-color: %9; }")
                             .arg(UiTheme::Color::Panel, UiTheme::Color::TextStatusPrimary,
                                  UiTheme::Color::BorderMedium, UiTheme::Color::TextStatusSecondary,
                                  UiTheme::Color::TextStatusPrimary, UiTheme::Color::Field, UiTheme::Color::BorderFocus,
                                  UiTheme::Color::TextField, UiTheme::Color::AccentDark));
    auto* root = new QVBoxLayout(strip);
    root->setContentsMargins(kControlStripMargins);
    root->setSpacing(kNoSpacing);

    auto* controlRow = new QHBoxLayout;
    controlRow->setSpacing(kControlRowSpacing);

    auto makeSelectorButton = [strip](const QString& primary, const QString& secondary, const QString& name,
                                      const QString& description) -> QPushButton*
    {
        auto* button = new TwoLineButton(strip);
        button->setCheckable(false);
        button->setFixedSize(kSelectorButtonSize);
        button->setAccessibleDescription(description);
        button->setStyleSheet(commandButtonStyle(false));
        setSelectorButtonLines(button, primary, secondary);
        button->setAccessibleName(name);
        return button;
    };

    m_txPowerValue = 0;
    m_squelchValue = 0;
    m_rfGainValue = 0;
    m_lanModValue = qBound(0, AppSettings::instance().value("LANModLevel", 128).toInt(), 255);
    m_vfoPanel = new VfoPanel(QStringLiteral("VFO"), strip);
    m_vfoPanel->setFrequencyReadOnly(false);
    m_vfoPanel->setFrequencyText(QStringLiteral("---.---.---"));
    m_vfoPanel->setBandText(QStringLiteral("--"));
    m_vfoPanel->setModeText(QStringLiteral("--"));
    m_vfoPanel->setMemoryName(QString::fromLatin1(kNoActiveMemoryLabel), QStringLiteral("No active memory"));
    m_vfoPanel->setTxPower(0);
    m_vfoPanel->setLanMod(m_lanModValue);
    const int appVolume = appVolumeSettingValue();
    m_currentAfGain = appVolume;
    if (m_titleBar)
    {
        m_titleBar->setVolume(appVolume);
    }
    m_vfoPanel->setSquelch(0);
    m_memoryPanel = new MemoryPanel(strip);

    m_agcBtn = makeSelectorButton("AGC", QStringLiteral("MID"), "AGC mode", "Select AGC time constant.");
    m_attBtn = makeSelectorButton("ATT", QStringLiteral("OFF"), "Attenuator", "Toggle receiver attenuator.");
    m_attBtn->setCheckable(true);
    m_attBtn->setProperty("toggleLabel", "ATT");
    m_nbBtn = makeSelectorButton("NB", QStringLiteral("OFF"), "Noise blanker", "Toggle noise blanker.");
    m_nbBtn->setCheckable(true);
    m_nbBtn->setProperty("toggleLabel", "NB");
    m_notchBtn = makeSelectorButton("NOTCH", QStringLiteral("OFF"), "Notch", "Select notch filter mode.");
    m_nrBtn = makeSelectorButton("NR", QStringLiteral("OFF"), "Noise reduction", "Toggle noise reduction.");
    m_nrBtn->setCheckable(true);
    m_nrBtn->setProperty("toggleLabel", "NR");
    m_preBtn = makeSelectorButton("PRE", QStringLiteral("OFF"), "Preamp", "Select receiver preamp.");
    m_ritBtn = makeSelectorButton("RIT", QStringLiteral("OFF"), "RIT", "Set receiver incremental tuning offset.");
    m_rfGainBtn = makeSelectorButton("RF GAIN", QStringLiteral("OFF"), "RF gain", "Set receiver RF gain.");
    m_rfGainBtn->setProperty("levelControl", true);
    m_agcBtn->setToolTip(QStringLiteral("Automatic Gain Control (AGC)\n"
                                        "Controls receiver gain to produce a constant audio output level."));
    m_attBtn->setToolTip(QStringLiteral("Attenuator (ATT)\n"
                                        "Prevents a desired signal from becoming distorted in the presence of a very "
                                        "strong signal."));
    m_nbBtn->setToolTip(
        QStringLiteral("Noise Blanker (NB)\nEliminate pulse-type noise such as the noise from car ignitions."));
    m_notchBtn->setToolTip(QStringLiteral("Notch Filter\n"
                                          "Attenuates beat tones, tuning signals, and so on in the SSB, CW, RTTY, and "
                                          "AM modes."));
    m_nrBtn->setToolTip(
        QStringLiteral("Noise Reduction (NR)\nReduces random noise components and enhances signal audio."));
    m_preBtn->setToolTip(QStringLiteral("Preamplifier (PRE)\nAmplifies received signals in the receiver front end."));
    m_rfGainBtn->setToolTip(
        QStringLiteral("RF Gain\nIncrease/decrease the noise received from a nearby strong station."));
    m_ritBtn->setToolTip(
        QStringLiteral("Receive Increment Tuning (RIT)\nCompensate for differences in frequencies of other stations."));

    m_pttBtn = makeSelectorButton("PTT", QStringLiteral("OFF"), "PTT", "Hold to transmit.");
    m_pttBtn->setCheckable(false);

    m_compBtn = makeSelectorButton("COMP", QStringLiteral("OFF"), "Compressor", "Toggle speech compressor.");
    m_compBtn->setCheckable(true);
    m_compBtn->setProperty("toggleLabel", "COMP");
    m_offsetBtn =
        makeSelectorButton("OFFSET", QStringLiteral("SIMPLEX"), "Repeater offset", "Select repeater duplex offset.");
    m_offsetBtn->setToolTip("Select repeater duplex offset.");
    m_toneBtn = makeSelectorButton("TONE", QStringLiteral("OFF"), "Tone settings", "Select tone or DTCS.");
    m_toneBtn->setToolTip("Select tone or DTCS.");
    m_xfcBtn = makeSelectorButton("XFC", QStringLiteral("OFF"), "XFC", "Toggle transmit frequency check.");
    m_xfcBtn->setCheckable(true);
    m_xfcBtn->setProperty("toggleLabel", "XFC");
    m_xfcBtn->setToolTip("Transmit Frequency Check (XFC)\nMonitor the transmit frequency while split is active.");

    const ReceivePanel::Buttons receiveButtons{
        m_agcBtn,    m_attBtn, m_compBtn,   m_nbBtn,  m_notchBtn, m_nrBtn,
        m_offsetBtn, m_preBtn, m_rfGainBtn, m_ritBtn, m_toneBtn,  m_xfcBtn,
    };
    auto* receiveGroup = new ReceivePanel(receiveButtons, strip);
    auto* pttGroup = new PttPanel(m_pttBtn, nullptr, strip);
    auto* receiveStack = new QWidget(strip);
    receiveStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* receiveStackLayout = new QVBoxLayout(receiveStack);
    receiveStackLayout->setContentsMargins(0, 0, 0, 0);
    receiveStackLayout->setSpacing(kControlGroupSpacing);

    auto* receiveTopRow = new QHBoxLayout;
    receiveTopRow->setContentsMargins(0, 0, 0, 0);
    receiveTopRow->setSpacing(kControlRowSpacing);
    receiveTopRow->addWidget(receiveGroup, 1);
    receiveTopRow->addWidget(pttGroup);
    receiveStackLayout->addLayout(receiveTopRow);

    connect(m_agcBtn, &QPushButton::clicked, this, &MainWindow::showAgcMenu);
    connect(m_compBtn, &QPushButton::clicked, this,
            [this]()
            {
                if (m_vfo)
                {
                    m_vfo->setCompressor(!m_vfo->compressorOn());
                    setCommandButtonActive(m_compBtn, m_vfo->compressorOn());
                }
            });
    connect(m_notchBtn, &QPushButton::clicked, this, &MainWindow::showNotchMenu);
    connect(m_ritBtn, &QPushButton::clicked, this, &MainWindow::showRitMenu);
    connect(m_offsetBtn, &QPushButton::clicked, this, &MainWindow::showOffsetMenu);
    connect(m_toneBtn, &QPushButton::clicked, this, &MainWindow::showToneMenu);
    connect(m_xfcBtn, &QPushButton::clicked, this,
            [this]()
            {
                if (m_vfo)
                {
                    m_vfo->setXfcEnabled(!m_vfo->xfcOn());
                    setCommandButtonActive(m_xfcBtn, m_vfo->xfcOn());
                }
            });
    connect(m_memoryPanel, &MemoryPanel::memoryActivated, this,
            [this](const QString& memoryId) { selectMemoryById(memoryId, false); });

    controlRow->addWidget(m_vfoPanel);
    controlRow->addWidget(m_memoryPanel);
    controlRow->addWidget(receiveStack, 1);

    m_dtmfDialog = new DtmfDialog(this);
    m_metersDialog = new MetersDialog(this);
    m_dtmfDialog->hide();
    m_metersDialog->hide();

    m_dtmfPttOffTimer = new QTimer(this);
    m_dtmfPttOffTimer->setSingleShot(true);
    connect(m_dtmfPttOffTimer, &QTimer::timeout, this,
            [this]()
            {
                m_vfo->setPtt(false);
                m_dtmfSendActive = false;
                if (m_dtmfDialog)
                {
                    m_dtmfDialog->setSendInProgress(false);
                }
                // CI-V echo timing can cause the radio's TX-active acknowledgement to
                // arrive after the unkey command is queued, leaving pttChanged(true)
                // as the last state RadioModel sees. Reset the UI immediately; the
                // eventual pttChanged(false) from the radio is a harmless duplicate.
                onPttChanged(false);
            });

    root->addLayout(controlRow);
    vbox->addWidget(strip);
    strip->setFocus();

    connect(m_vfoPanel, &VfoPanel::frequencyReturnPressed, this, [this]() { commitFrequencyEdit(m_vfoPanel); });
    connect(m_pttBtn, &QPushButton::pressed, this, &MainWindow::onPttPressed);
    connect(m_pttBtn, &QPushButton::released, this, &MainWindow::onPttReleased);
    connect(m_dtmfDialog, &DtmfDialog::sendRequested, this, &MainWindow::onDtmfSendRequested);
    auto connectTxPowerSlider = [this](VfoPanel* widget)
    {
        connect(widget, &VfoPanel::txPowerChanged, this,
                [this](int value)
                {
                    if (m_applyingRadioSliderUpdate || !m_model->isReady() || m_controlsLocked)
                    {
                        return;
                    }
                    m_txPowerValue = qBound(0, value, 255);
                    if (auto* backend = m_model ? m_model->backend() : nullptr)
                    {
                        backend->setTxPower(m_txPowerValue);
                    }
                });
    };
    connectTxPowerSlider(m_vfoPanel);
    auto connectLanModSlider = [this](VfoPanel* widget)
    {
        connect(widget, &VfoPanel::lanModChanged, this,
                [this](int value)
                {
                    if (m_applyingRadioSliderUpdate || !m_model->isReady() || m_controlsLocked)
                    {
                        return;
                    }
                    m_lanModValue = qBound(0, value, 255);
                    AppSettings::instance().setValue(QStringLiteral("LANModLevel"), m_lanModValue);
                    m_model->setLanModLevel(m_lanModValue);
                });
    };
    connectLanModSlider(m_vfoPanel);
    auto connectSquelchSlider = [this](VfoPanel* widget)
    {
        connect(widget, &VfoPanel::squelchChanged, this,
                [this](int value)
                {
                    if (m_applyingRadioSliderUpdate || !m_model->isReady() || m_controlsLocked)
                    {
                        return;
                    }
                    if (auto* backend = m_model ? m_model->backend() : nullptr)
                    {
                        backend->setSquelch(value > 0, value);
                    }
                });
    };
    connectSquelchSlider(m_vfoPanel);
    connect(m_rfGainBtn, &QPushButton::clicked, this, &MainWindow::showRfGainMenu);
    auto showModeMenuFor = [this](const VfoPanel* widget)
    {
        if (!widget || !m_vfo)
        {
            return;
        }
        QMenu menu(this);
        styleCompactMenu(&menu);
        for (const QString& mode : m_vfo->availableModes())
        {
            menu.addAction(mode);
        }
        const QAction* chosen = menu.exec(widget->modeMenuPosition());
        if (chosen)
        {
            m_vfo->setMode(chosen->text());
        }
    };
    connect(m_vfoPanel, &VfoPanel::modeClicked, this, [showModeMenuFor, this]() { showModeMenuFor(m_vfoPanel); });
    auto showBandMenuFor = [this](const VfoPanel* widget)
    {
        if (!widget || !m_vfo)
        {
            return;
        }
        QMenu menu(this);
        styleCompactMenu(&menu);
        for (const availableBands band : sdr9700::kRadioUiBandOrder)
        {
            auto* action = menu.addAction(sdr9700::radioBandMenuLabel(band));
            action->setData(static_cast<int>(band));
        }
        const QAction* chosen = menu.exec(widget->bandMenuPosition());
        if (chosen)
        {
            const auto band = static_cast<availableBands>(chosen->data().toInt());
            const int bandIndex = sdr9700::radioBandUiIndex(band);
            if (bandIndex < 0)
            {
                return;
            }
            const quint64 defaultFrequency = sdr9700::radioBandDefaultFrequency(band);
            const quint64 hz =
                m_lastBandFrequencyHz[bandIndex] > 0 ? m_lastBandFrequencyHz[bandIndex] : defaultFrequency;
            if (hz == 0)
            {
                return;
            }
            qInfo(logGui()) << "VFO action: band selected" << sdr9700::radioBandShortLabel(band) << hz;
            m_vfo->setFrequencyHz(hz);
        }
    };
    connect(m_vfoPanel, &VfoPanel::bandClicked, this, [showBandMenuFor, this]() { showBandMenuFor(m_vfoPanel); });
    connect(m_vfoPanel, &VfoPanel::stepClicked, this,
            [this]()
            {
                if (!m_vfoPanel)
                {
                    return;
                }
                const int currentStep = tuningStepHz();
                QMenu menu(this);
                styleCompactMenu(&menu);
                for (const auto& preset : kStepPresets)
                {
                    auto* action = menu.addAction(QString::fromLatin1(preset.label));
                    action->setData(preset.hz);
                    action->setCheckable(true);
                    action->setChecked(preset.hz == currentStep);
                }
                const QAction* chosen = menu.exec(m_vfoPanel->stepMenuPosition());
                if (chosen)
                {
                    AppSettings::instance().setValue(QString::fromLatin1(kTuningStepHZSettingsKey),
                                                     chosen->data().toInt());
                    updateStepButton();
                    applyRadioTuningStep();
                }
            });
    updateStepButton();
    connect(m_nrBtn, &QPushButton::clicked, this,
            [this]()
            {
                if (m_vfo)
                {
                    m_vfo->setNrEnabled(!m_vfo->nrOn());
                    setCommandButtonActive(m_nrBtn, m_vfo->nrOn());
                }
            });
    connect(m_nbBtn, &QPushButton::clicked, this,
            [this]()
            {
                if (m_vfo)
                {
                    m_vfo->setNbEnabled(!m_vfo->nbOn());
                    setCommandButtonActive(m_nbBtn, m_vfo->nbOn());
                }
            });
    connect(m_preBtn, &QPushButton::clicked, this, &MainWindow::showPreampMenu);
    connect(m_attBtn, &QPushButton::clicked, this,
            [this]()
            {
                if (m_vfo)
                {
                    m_vfo->setAttenuatorEnabled(!m_vfo->attenuatorOn());
                    setCommandButtonActive(m_attBtn, m_vfo->attenuatorOn());
                }
            });

    m_currentAfGain = appVolumeSettingValue();
    m_savedAfGain = m_currentAfGain;
    resetRadioOwnedControlsForSync();
}

void MainWindow::updateTxIndicator(bool on)
{
    m_statusBarController->updateTxIndicator(on);
}

void MainWindow::updateTxDurationLabel()
{
    m_statusBarController->updateTxDurationLabel();
}

void MainWindow::updateStatusClock()
{
    m_statusBarController->updateStatusClock();
}

void MainWindow::toggleStatusClockMode()
{
    m_statusBarController->toggleStatusClockMode();
}

void MainWindow::updateSystemStats()
{
    m_statusBarController->updateSystemStats();
}

void MainWindow::buildStatusBar()
{
    m_statusBarController->buildStatusBar();
}

void MainWindow::showRadioChooserDialog()
{
    if (m_radioChooserDialog)
    {
        bringDialogToFront(m_radioChooserDialog);
        return;
    }

    auto* dlg = new RadioChooserDialog(this);
    m_radioChooserDialog = dlg;
    connect(dlg, &QObject::destroyed, this,
            [this, dlg]()
            {
                if (m_radioChooserDialog == dlg)
                {
                    m_radioChooserDialog = nullptr;
                }
            });
    connect(dlg, &RadioChooserDialog::connectRequested, this,
            [this](const QUuid& id)
            {
                const RadioProfile* p = RadioProfileStore::instance().profileById(id);
                if (p)
                {
                    onConnectToProfile(*p);
                }
            });
    QPointer<RadioChooserDialog> dlgGuard = dlg;
    centerPopupWindow(dlg);
    if (dlgGuard)
    {
        dlgGuard->exec();
    }
    if (m_radioChooserDialog == dlgGuard)
    {
        m_radioChooserDialog = nullptr;
    }
    if (dlgGuard)
    {
        dlgGuard->deleteLater();
    }
}

void MainWindow::onConnectToProfile(const RadioProfile& profile)
{
    m_pendingProfileId = profile.id;
    m_radioHost = profile.host;
    m_radioPort = profile.port;
    m_radioUsername = profile.username;
    m_userDisconnected = false;
    m_model->connectToRadio(profile.host, profile.port, profile.username, profile.password);
    if (m_connStateLabel)
    {
        m_connStateName = QStringLiteral("Connecting");
        m_connStateLabel->setText(
            QStringLiteral("<span style='color:%1'>Connecting</span>").arg(UiTheme::Color::Accent));
    }
    updateConnectionTooltip();
}

void MainWindow::tryAutoConnect()
{
    RadioProfileStore& store = RadioProfileStore::instance();
    store.load();
    m_allowChooserOnDisconnect = false;

    const bool autoConnect = AppSettings::instance().value("autoConnect", "True").toBool();
    if (autoConnect)
    {
        const QUuid lastId = store.lastProfileId();
        if (!lastId.isNull())
        {
            const RadioProfile* p = store.profileById(lastId);
            if (p)
            {
                onConnectToProfile(*p);
                return;
            }
        }
    }

    showRadioChooserDialog();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveWindowLayout();
    if (m_icomRC28Controller)
    {
        m_icomRC28Controller->close();
    }
    m_userDisconnected = true;
    m_model->disconnectFromRadio();
    QMainWindow::closeEvent(event);
}

void MainWindow::restoreWindowLayout()
{
    const QSize fixedSize(UiTheme::Size::MainWindowMinWidth, UiTheme::Size::MainWindowMinHeight);
    const bool hasSavedPos = AppSettings::instance().contains(QStringLiteral("mainWindowPositionX")) &&
                             AppSettings::instance().contains(QStringLiteral("mainWindowPositionY"));
    if (hasSavedPos)
    {
        const QPoint savedTopLeft(AppSettings::instance().value(QStringLiteral("mainWindowPositionX")).toInt(),
                                  AppSettings::instance().value(QStringLiteral("mainWindowPositionY")).toInt());
        const QRect savedRect(savedTopLeft, fixedSize);
        const QPoint pos = availableScreenContains(savedRect)
                               ? savedTopLeft
                               : centeredRectInAvailableGeometry(fixedSize, availableGeometryFor(savedRect)).topLeft();
        move(pos);
    }
    if (m_bandscopeDisplay)
    {
        const int spectrumHeight = AppSettings::instance().value("bandScopeSpectrumHeight", -1).toInt();
        if (spectrumHeight > 0)
        {
            m_bandscopeDisplay->setSpectrumPaneHeight(spectrumHeight);
        }
    }
}

void MainWindow::saveWindowLayout() const
{
    AppSettings::instance().setValue("mainWindowPositionX", normalGeometry().x());
    AppSettings::instance().setValue("mainWindowPositionY", normalGeometry().y());
    if (m_bandscopeDisplay)
    {
        AppSettings::instance().setValue("bandScopeSpectrumHeight", m_bandscopeDisplay->spectrumPaneHeight());
    }
}

void MainWindow::updateSpectrumVfoMarker()
{
    m_bandscopeController->updateSpectrumVfoMarker();
}

void MainWindow::setRadioControlsEnabled(bool enabled)
{
    const bool controlsEnabled = enabled && !m_controlsLocked;
    if (m_vfoPanel)
    {
        m_vfoPanel->setEnabled(controlsEnabled);
        m_vfoPanel->setControlsEnabled(controlsEnabled);
    }
    if (m_memoryPanel)
    {
        m_memoryPanel->setEnabled(controlsEnabled);
    }
    if (m_titleBar)
    {
        m_titleBar->setVolumeEnabled(enabled);
    }

    for (auto* button : {m_agcBtn, m_nrBtn, m_nbBtn, m_notchBtn, m_preBtn, m_attBtn, m_ritBtn, m_compBtn, m_offsetBtn,
                         m_toneBtn, m_xfcBtn, m_squelchBtn})
    {
        if (button)
        {
            button->setEnabled(controlsEnabled);
        }
    }
    if (m_muteBtn)
    {
        m_muteBtn->setEnabled(enabled);
    }
    if (m_pttBtn)
    {
        m_pttBtn->setEnabled(enabled);
    }
    if (m_rfGainBtn)
    {
        m_rfGainBtn->setEnabled(controlsEnabled);
    }

    if (m_bandscopeDisplay)
    {
        m_bandscopeDisplay->setInteractionLocked(m_controlsLocked);
    }
}

void MainWindow::resetRadioOwnedControlsForSync()
{
    m_vfoFrequencyHz = 0;
    m_meterSnapshot = {};
    m_txPowerValue = 0;
    m_rfGainValue = 0;
    m_squelchValue = 0;
    m_lanModValue = qBound(0, AppSettings::instance().value("LANModLevel", 128).toInt(), 255);
    m_duplexMode = dmSimplex;
    m_toneAccessMode = ratrNN;
    m_toneFrequency = 670;
    m_dtcsCode = 23;

    if (m_vfoPanel)
    {
        if (!m_vfoPanel->frequencyHasFocus())
        {
            m_vfoPanel->setFrequencyText(QStringLiteral("---.---.---"));
        }
        m_vfoPanel->setBandText(QStringLiteral("--"));
        m_vfoPanel->setModeText(QStringLiteral("--"));
        m_vfoPanel->setMeterEnabled(false);
        m_vfoPanel->setTransmitPowerMode(false);
        m_vfoPanel->setSMeterValue(0);
        m_vfoPanel->setTxPower(0);
        m_vfoPanel->setLanMod(m_lanModValue);
        m_vfoPanel->setSquelch(0);
    }

    setCommandButtonActive(m_nrBtn, false);
    setCommandButtonActive(m_nbBtn, false);
    setSelectorButtonLines(m_notchBtn, QStringLiteral("NOTCH"), QStringLiteral("OFF"));
    setCommandButtonActive(m_notchBtn, false);
    setSelectorButtonLines(m_preBtn, QStringLiteral("PRE"), QStringLiteral("OFF"));
    setCommandButtonActive(m_preBtn, false);
    setCommandButtonActive(m_attBtn, false);
    setCommandButtonActive(m_compBtn, false);
    setCommandButtonActive(m_xfcBtn, false);
    setSelectorButtonLines(m_agcBtn, QStringLiteral("AGC"), QStringLiteral("MID"));
    setSelectorButtonLines(m_ritBtn, QStringLiteral("RIT"), QStringLiteral("OFF"));
    setCommandButtonActive(m_ritBtn, false);
    updateOffsetButton();
    updateToneButton();
    updateSquelchButton();
    updateTxPowerButton();
    updateRfGainButton();
    updateTxIndicator(false);
    if (m_metersDialog)
    {
        m_metersDialog->resetMeters();
    }
}

void MainWindow::applyActiveVfoFromRadio()
{
    updateSpectrumVfoMarker();
}

void MainWindow::toggleControlLock()
{
    m_controlsLocked = !m_controlsLocked;
    updateControlLockIndicator();
    setRadioControlsEnabled(m_model && m_model->isConnected() && m_model->isReady());
    updateIcomRC28Leds();
}

void MainWindow::toggleMute()
{
    m_radioCommandController->toggleMute();
}

void MainWindow::cycleMode()
{
    m_radioCommandController->cycleMode();
}

void MainWindow::toggleRit()
{
    m_radioCommandController->toggleRit();
}

#ifdef HAVE_HIDAPI
void MainWindow::dispatchIcomRC28Action(const QString& action)
{
    m_icomRC28Controller->dispatchIcomRC28Action(action);
}

void MainWindow::setIcomRC28Ptt(bool on)
{
    m_icomRC28Controller->setIcomRC28Ptt(on);
}

void MainWindow::updateIcomRC28Leds()
{
    m_icomRC28Controller->updateIcomRC28Leds();
}

void MainWindow::handleIcomRC28Tune(int steps)
{
    m_icomRC28Controller->handleIcomRC28Tune(steps);
}

void MainWindow::refreshIcomRC28EncoderSettings()
{
    m_icomRC28Controller->refreshIcomRC28EncoderSettings();
}

void MainWindow::snapIcomRC28FrequencyToKhz()
{
    m_icomRC28Controller->snapIcomRC28FrequencyToKhz();
}

void MainWindow::handleIcomRC28Button(int button, int action)
{
    m_icomRC28Controller->handleIcomRC28Button(button, action);
}

#endif

void MainWindow::updateControlLockIndicator()
{
    if (m_titleBar)
    {
        m_titleBar->setLocked(m_controlsLocked);
    }

    if (!m_lockIndicator)
    {
        return;
    }

    m_lockIndicator->setText(QStringLiteral("LOCK"));
    m_lockIndicator->setStyleSheet(
        m_controlsLocked ? QStringLiteral("QLabel { color: %1; background: %2; font-weight: bold; font-size: 21px; "
                                          "border-radius: 4px; padding: 0px 1px; }")
                               .arg(UiTheme::Color::PanelDark, UiTheme::Color::Warning)
                         : QStringLiteral("QLabel { color: %1; font-weight: bold; font-size: 21px; }")
                               .arg(UiTheme::Color::TextStatusSecondary));
    const QString tooltip = m_controlsLocked ? QStringLiteral("Controls Locked\nClick to unlock.")
                                             : QStringLiteral("Controls Unlocked\nClick to lock.");
    m_lockIndicator->setToolTip(tooltip);
    if (m_lockWidget)
    {
        m_lockWidget->setToolTip(tooltip);
        m_lockWidget->setAccessibleName(m_controlsLocked ? QStringLiteral("Controls locked")
                                                         : QStringLiteral("Controls unlocked"));
    }
}

void MainWindow::updateBandscopeBandLimits(quint64 hz)
{
    m_bandscopeController->updateBandscopeBandLimits(hz);
}

int MainWindow::tuningStepHz() const
{
    return m_radioCommandController->tuningStepHz();
}

void MainWindow::applyRadioTuningStep()
{
    m_radioCommandController->applyRadioTuningStep();
}

void MainWindow::applyBandscopeSettings()
{
    m_bandscopeController->applyBandscopeSettings();
}

void MainWindow::updateStepButton()
{
    m_radioCommandController->updateStepButton();
}

quint64 MainWindow::roundFrequencyToStep(quint64 hz) const
{
    return m_bandscopeController->roundFrequencyToStep(hz);
}

void MainWindow::panBandscopeToCenter(quint64 centerHz)
{
    m_bandscopeController->panBandscopeToCenter(centerHz);
}

quint64 MainWindow::clampBandscopeCenterHz(quint64 hz, double bandwidthMhz) const
{
    return m_bandscopeController->clampBandscopeCenterHz(hz, bandwidthMhz);
}

quint64 MainWindow::clampFrequencyHzToActiveBand(quint64 hz) const
{
    return m_bandscopeController->clampFrequencyHzToActiveBand(hz);
}

void MainWindow::scheduleBandscopeTune(quint64 hz)
{
    m_bandscopeController->scheduleBandscopeTune(hz);
}

void MainWindow::setActiveMemory(const QString& id, const QString& name, quint64 frequencyHz, int duplexMode,
                                 quint64 offsetHz, int toneMode, ushort toneValue)
{
    m_activeMemoryId = id;
    m_activeMemoryName = name.left(kMemoryNameMaxChars);
    m_activeMemoryFrequencyHz = frequencyHz;
    m_activeMemoryDuplexMode = static_cast<duplexMode_t>(duplexMode);
    m_activeMemoryOffsetHz = offsetHz;
    m_activeMemoryToneMode = static_cast<rptAccessTxRx_t>(toneMode);
    m_activeMemoryToneValue = toneValue;
    m_activeMemoryFrequencySettled = m_vfo && m_vfo->frequencyHz() == frequencyHz;
    m_activeMemoryDuplexSettled = m_duplexMode == m_activeMemoryDuplexMode;
    m_activeMemoryOffsetSettled = m_activeMemoryDuplexMode == dmSimplex || m_repeaterOffsetHz == offsetHz;
    m_activeMemoryToneModeSettled = m_toneAccessMode == m_activeMemoryToneMode;
    const bool isDtcs = isDtcsToneMode(m_activeMemoryToneMode);
    m_activeMemoryToneValueSettled = toneMode == ratrNN || (isDtcs && m_dtcsCode == toneValue) ||
                                     (!isDtcs && toneMode != ratrNN && m_toneFrequency == toneValue);
    updateMemoryNameLabel();
    if (m_memoryPanel)
    {
        m_memoryPanel->setActiveMemoryId(m_activeMemoryId);
    }
}

void MainWindow::clearActiveMemory()
{
    m_applyingMemorySelection = false;
    m_activeMemoryFrequencySettled = false;
    m_activeMemoryDuplexSettled = false;
    m_activeMemoryOffsetSettled = false;
    m_activeMemoryToneModeSettled = false;
    m_activeMemoryToneValueSettled = false;
    if (m_activeMemoryId.isEmpty())
    {
        return;
    }

    m_activeMemoryId.clear();
    m_activeMemoryName.clear();
    m_activeMemoryFrequencyHz = 0;
    m_activeMemoryDuplexMode = dmSimplex;
    m_activeMemoryOffsetHz = 0;
    m_activeMemoryToneMode = ratrNN;
    m_activeMemoryToneValue = 0;
    updateMemoryNameLabel();
    if (m_memoryPanel)
    {
        m_memoryPanel->setActiveMemoryId(QString());
    }
}

void MainWindow::checkIfMemorySelectionComplete()
{
    if (!m_applyingMemorySelection)
    {
        return;
    }
    if (m_activeMemoryFrequencySettled && m_activeMemoryDuplexSettled && m_activeMemoryOffsetSettled &&
        m_activeMemoryToneModeSettled && m_activeMemoryToneValueSettled)
    {
        m_applyingMemorySelection = false;
    }
}

void MainWindow::updateMemoryNameLabel()
{
    if (!m_vfoPanel)
    {
        return;
    }

    const QString text = m_activeMemoryId.isEmpty() ? QString::fromLatin1(kNoActiveMemoryLabel) : m_activeMemoryName;
    m_vfoPanel->setMemoryName(text, m_activeMemoryId.isEmpty() ? QStringLiteral("No active memory")
                                                               : QStringLiteral("Active memory: %1").arg(text));
}

void MainWindow::updateConnectionTooltip()
{
    if (!m_connStateLabel || !m_connDetailLabel)
    {
        return;
    }

    QStringList lines;
    lines << QStringLiteral("Radio Connection Status");
    lines << QStringLiteral("Radio ID: %1").arg(m_model ? m_model->radioName() : QStringLiteral("IC-9700"));
    lines << QStringLiteral("State: %1").arg(m_connStateName);
    if (!m_radioHost.isEmpty())
    {
        lines << QStringLiteral("Host: %1").arg(m_radioHost);
    }
    if (m_radioPort > 0)
    {
        lines << QStringLiteral("Port: %1").arg(m_radioPort);
    }
    if (!m_radioUsername.isEmpty())
    {
        lines << QStringLiteral("Username: %1").arg(m_radioUsername);
    }

    const QString tooltip = lines.join(QLatin1Char('\n'));
    m_connStateLabel->setToolTip(tooltip);
    m_connDetailLabel->setToolTip(tooltip);
}

void MainWindow::onConnectionChanged(bool connected)
{
    setRadioControlsEnabled(connected && m_model->isReady());
    resetRadioOwnedControlsForSync();

    if (connected)
    {
        m_reconnecting = false;
        m_lastErrorWasCredential = false;
        m_allowChooserOnDisconnect = true;
        if (m_reconnectTimer)
        {
            m_reconnectTimer->stop();
        }

        const int appVolume = appVolumeSettingValue();
        m_currentAfGain = appVolume;
        if (m_titleBar)
        {
            m_titleBar->setVolume(appVolume);
        }
        if (m_vfo)
        {
            m_vfo->setAfGain(appVolume);
        }

        if (m_connStateLabel)
        {
            m_connStateName = QStringLiteral("Syncing");
            m_connStateLabel->setText(
                QStringLiteral("<span style='color:%1'>Syncing</span>").arg(UiTheme::Color::Warning));
        }
        updateConnectionTooltip();
        if (!m_pendingProfileId.isNull())
        {
            if (!RadioProfileStore::instance().setLastProfileId(m_pendingProfileId))
            {
                showToast("Could not save last selected radio profile", 8000, ToastKind::Warning);
            }
        }
    }
    else
    {
        if (m_bandscopeTuneCommitTimer)
        {
            m_bandscopeTuneCommitTimer->stop();
        }
        if (m_bandscopeTuneReleaseTimer)
        {
            m_bandscopeTuneReleaseTimer->stop();
        }
        m_pendingBandscopeTuneHz = 0;
        m_displayBandscopeTuneHz = 0;
        m_bandscopeDisplayCenterHz = 0;
        m_bandscopeFixedPanStartHz = 0;
        m_bandscopeFixedPanEndHz = 0;
        if (m_bandscope)
        {
            m_bandscope->clearDisplayCenterHold();
        }
        clearActiveMemory();
        m_bandscopeDisplay->clearDisplay();
        m_bandscopeDisplay->clearFrequencyPanRange();
#ifdef HAVE_HIDAPI
        m_icomRC28PttLatched = false;
#endif
        updateNetworkQuality(0);

        const bool canReconnect = !m_userDisconnected && !m_lastErrorWasCredential && !m_pendingProfileId.isNull() &&
                                  RadioProfileStore::instance().profileById(m_pendingProfileId);

        if (canReconnect)
        {
            m_reconnecting = true;
            if (m_connStateLabel)
            {
                m_connStateName = QStringLiteral("Reconnecting");
                m_connStateLabel->setText(
                    QStringLiteral("<span style='color:%1'>Reconnecting</span>").arg(UiTheme::Color::Danger));
            }
            // Keep the IP in the detail label so the user knows which radio is reconnecting.
            updateConnectionTooltip();

            if (!m_reconnectTimer)
            {
                m_reconnectTimer = new QTimer(this);
                m_reconnectTimer->setSingleShot(true);
                connect(m_reconnectTimer, &QTimer::timeout, this,
                        [this]()
                        {
                            if (!m_reconnecting)
                            {
                                return;
                            }
                            const RadioProfile* p = RadioProfileStore::instance().profileById(m_pendingProfileId);
                            if (p)
                            {
                                onConnectToProfile(*p);
                            }
                            else
                            {
                                m_reconnecting = false;
                            }
                        });
            }
            m_reconnectTimer->start(5000);
        }
        else
        {
            const bool wasUserDisconnected = m_userDisconnected;
            m_reconnecting = false;
            m_userDisconnected = false;
            m_lastErrorWasCredential = false;
            if (m_connStateLabel)
            {
                m_connStateName = QStringLiteral("Disconnected");
                m_connStateLabel->setText(
                    QStringLiteral("<span style='color:%1'>Disconnected</span>").arg(UiTheme::Color::TextStatusLabel));
            }
            updateConnectionTooltip();
            if (!wasUserDisconnected && m_allowChooserOnDisconnect)
            {
                QTimer::singleShot(0, this, [this]() { showRadioChooserDialog(); });
            }
        }
    }
}

void MainWindow::onRadioReadyChanged(bool ready)
{
    const bool connected = m_model->isConnected();
    setRadioControlsEnabled(connected && ready);
    if (m_vfoPanel)
    {
        m_vfoPanel->setMeterEnabled(ready);
        if (!ready)
        {
            m_vfoPanel->setTransmitPowerMode(false);
            m_vfoPanel->setSMeterValue(0);
        }
    }
    if (!m_connStateLabel || !connected)
    {
        return;
    }

    if (ready)
    {
        applyRadioTuningStep();
        applyBandscopeSettings();
        m_connStateName = QStringLiteral("Connected");
        m_connStateLabel->setText(
            QStringLiteral("<span style='color:%1'>Connected</span>").arg(UiTheme::Color::Success));
    }
    else
    {
        m_connStateName = QStringLiteral("Syncing");
        m_connStateLabel->setText(QStringLiteral("<span style='color:%1'>Syncing</span>").arg(UiTheme::Color::Warning));
    }
    updateConnectionTooltip();
}

void MainWindow::onFrequencyChanged(quint64 hz)
{
    if (!m_activeMemoryId.isEmpty())
    {
        if (hz == m_activeMemoryFrequencyHz)
        {
            m_activeMemoryFrequencySettled = true;
            checkIfMemorySelectionComplete();
        }
        else if (m_activeMemoryFrequencySettled && !m_applyingMemorySelection)
        {
            clearActiveMemory();
        }
    }

    if (m_displayBandscopeTuneHz > 0 && m_bandscopeTuneReleaseTimer && m_bandscopeTuneReleaseTimer->isActive())
    {
        if (hz != m_displayBandscopeTuneHz)
        {
            qDebug(logBandscope()) << "Ignoring transient bandscope tune confirmation"
                                   << "confirmedHz=" << hz << "displayHz=" << m_displayBandscopeTuneHz;
            return;
        }
        m_pendingBandscopeTuneHz = 0;
        m_displayBandscopeTuneHz = 0;
        m_bandscopeDisplayCenterHz = 0;
        m_bandscopeFixedPanStartHz = 0;
        m_bandscopeFixedPanEndHz = 0;
        m_bandscopeTuneReleaseTimer->stop();
        if (m_bandscope)
        {
            m_bandscope->clearDisplayCenterHold();
        }
    }

    m_vfoFrequencyHz = hz;
    updateBandscopeBandLimits(hz);
    if (const int bandIndex = vfoBandIndexForHz(hz); bandIndex >= 0)
    {
        m_lastBandFrequencyHz[bandIndex] = hz;
    }
    qInfo(logGui()) << "VFO route: selected MAIN frequency" << hz;
    if (m_vfoPanel && !m_vfoPanel->frequencyHasFocus())
    {
        m_vfoPanel->setFrequencyText(formatFrequency(hz));
    }
    if (m_vfoPanel)
    {
        m_vfoPanel->setBandText(bandLabelForHz(hz));
    }
    updateSpectrumVfoMarker();
}

void MainWindow::onModeChanged(const QString& mode)
{
    if (m_vfoPanel)
    {
        m_vfoPanel->setModeText(mode);
    }
}

void MainWindow::onMeterSnapshotChanged(const MeterSnapshot& snapshot)
{
    if (!m_model->isReady())
    {
        return;
    }

    m_meterSnapshot = snapshot;
    if (m_metersDialog)
    {
        const bool hasAnyMeterValue =
            m_meterSnapshot.sMeterValid || m_meterSnapshot.powerValid || m_meterSnapshot.swrValid ||
            m_meterSnapshot.alcValid || m_meterSnapshot.compressionValid || m_meterSnapshot.voltageValid ||
            m_meterSnapshot.currentValid || m_meterSnapshot.txAudioPeak > 0 || m_meterSnapshot.txAudioRms > 0;
        if (!hasAnyMeterValue)
        {
            m_metersDialog->resetMeters();
        }
        else
        {
            if (m_meterSnapshot.sMeterValid)
            {
                m_metersDialog->setSMeter(m_meterSnapshot.sMeter);
            }
            if (m_meterSnapshot.powerValid)
            {
                m_metersDialog->setPowerMeter(m_meterSnapshot.powerWatts);
            }
            if (m_meterSnapshot.swrValid)
            {
                m_metersDialog->setSwr(m_meterSnapshot.swr);
            }
            if (m_meterSnapshot.alcValid)
            {
                m_metersDialog->setAlc(m_meterSnapshot.alc);
            }
            if (m_meterSnapshot.compressionValid)
            {
                m_metersDialog->setCompressionMeter(m_meterSnapshot.compressionDb);
            }
            if (m_meterSnapshot.voltageValid)
            {
                m_metersDialog->setVoltageMeter(m_meterSnapshot.voltageVolts);
            }
            if (m_meterSnapshot.currentValid)
            {
                m_metersDialog->setCurrentMeter(m_meterSnapshot.currentAmps);
            }
            m_metersDialog->setTransmitAudioLevel(m_meterSnapshot.txAudioPeak, m_meterSnapshot.txAudioRms);
        }
    }
    if (m_vfoPanel)
    {
        if (m_txActive)
        {
            if (m_meterSnapshot.powerValid)
            {
                m_vfoPanel->setTransmitPowerMeter(m_meterSnapshot.powerWatts);
            }
        }
        else if (m_meterSnapshot.sMeterValid)
        {
            m_vfoPanel->setSMeterValue(qBound(0, static_cast<int>(m_meterSnapshot.sMeter * 100 / 255), 100));
        }
    }

    if (!m_meterSnapshot.swrValid || !m_txActive || !m_txSwrLabel)
    {
        return;
    }

    const double swr = m_meterSnapshot.swr;
    const char* swrColor = swr <= 1.7   ? UiTheme::Color::Success
                           : swr <= 2.7 ? UiTheme::Color::Warning
                                        : UiTheme::Color::Danger;
    m_txSwrLabel->setText(
        QStringLiteral("<span style='color:%1'>SWR %2</span>").arg(QString::fromLatin1(swrColor)).arg(swr, 0, 'f', 2));
}

void MainWindow::showToast(const QString& msg, int durationMs, ToastKind kind)
{
    m_statusBarController->showToast(msg, durationMs, kind);
}

void MainWindow::updateNetworkQuality(int rttMs)
{
    m_statusBarController->updateNetworkQuality(rttMs);
}

void MainWindow::onStatusMessage(const QString& msg)
{
    ToastKind kind = ToastKind::Info;
    const QString lower = msg.toLower();
    if (lower.contains(QStringLiteral("timed out")) || lower.contains(QStringLiteral("stopped")) ||
        lower.contains(QStringLiteral("blocked")) || lower.contains(QStringLiteral("locked")) ||
        lower.contains(QStringLiteral("disconnected")) || lower.contains(QStringLiteral("could not")) ||
        lower.contains(QStringLiteral("failed")))
    {
        kind = ToastKind::Warning;
    }
    showToast(msg, 5000, kind);
}

void MainWindow::onError(const QString& msg)
{
    showToast(errorStatusMessage(msg), 8000, ToastKind::Error);
    m_lastErrorWasCredential = msg.startsWith(QStringLiteral("Login denied"), Qt::CaseInsensitive);
}

void MainWindow::onAfGainChanged(int value)
{
    m_vfo->setAfGain(value);
}

void MainWindow::onRfGainChanged(int value)
{
    m_rfGainValue = qBound(0, value, 255);
    updateRfGainButton();
}

void MainWindow::onTxPowerChanged(int value)
{
    m_txPowerValue = qBound(0, value, 255);
    if (m_vfoPanel)
    {
        m_vfoPanel->setTxPower(m_txPowerValue);
    }
    updateTxPowerButton();
}

void MainWindow::showDtmfDialog()
{
    if (!m_dtmfDialog)
    {
        return;
    }

    m_dtmfDialog->showCentered();
}

void MainWindow::showMetersDialog()
{
    if (!m_metersDialog)
    {
        return;
    }

    if (m_model && m_model->isReady())
    {
        m_metersDialog->resetMeters();
        if (m_meterSnapshot.sMeterValid)
        {
            m_metersDialog->setSMeter(m_meterSnapshot.sMeter);
        }
        if (m_meterSnapshot.powerValid)
        {
            m_metersDialog->setPowerMeter(m_meterSnapshot.powerWatts);
        }
        if (m_meterSnapshot.swrValid)
        {
            m_metersDialog->setSwr(m_meterSnapshot.swr);
        }
        if (m_meterSnapshot.alcValid)
        {
            m_metersDialog->setAlc(m_meterSnapshot.alc);
        }
        if (m_meterSnapshot.compressionValid)
        {
            m_metersDialog->setCompressionMeter(m_meterSnapshot.compressionDb);
        }
        if (m_meterSnapshot.voltageValid)
        {
            m_metersDialog->setVoltageMeter(m_meterSnapshot.voltageVolts);
        }
        if (m_meterSnapshot.currentValid)
        {
            m_metersDialog->setCurrentMeter(m_meterSnapshot.currentAmps);
        }
        m_metersDialog->setTransmitAudioLevel(m_meterSnapshot.txAudioPeak, m_meterSnapshot.txAudioRms);
    }
    else
    {
        m_metersDialog->resetMeters();
    }
    m_metersDialog->showCentered();
}

void MainWindow::onDtmfSendRequested(const QString& digits)
{
    if (m_dtmfSendActive || !m_vfo || digits.isEmpty())
    {
        return;
    }

    m_dtmfSendActive = true;
    if (m_dtmfDialog)
    {
        m_dtmfDialog->setSendInProgress(true);
    }

    m_vfo->setPtt(true);

    // The DTMF PCM buffer queued to UdpAudio is consumed only after the 1000 ms
    // TX gate expires. kTrailMs must cover the remaining gate window after the
    // lead-in (600 ms) plus some silence after the last tone.
    constexpr int kLeadInMs = 400;
    constexpr int kPerDigitMs = 400;
    constexpr int kTrailMs = 900;

    QTimer::singleShot(kLeadInMs, this,
                       [this, digits]()
                       {
                           if (m_dtmfSendActive && m_vfo)
                           {
                               m_vfo->sendDtmf(digits);
                           }
                       });

    m_dtmfPttOffTimer->start(kLeadInMs + digits.length() * kPerDigitMs + kTrailMs);
}

void MainWindow::onPttPressed()
{
    if (!m_vfo || !m_model->isReady())
    {
        return;
    }

    m_vfo->setPtt(true);
}

void MainWindow::onPttReleased()
{
    if (!m_vfo)
    {
        return;
    }
    m_vfo->setPtt(false);
}

void MainWindow::onPttChanged(bool on)
{
#ifdef HAVE_HIDAPI
    m_icomRC28PttLatched = on;
#endif
    if (!on && m_dtmfSendActive)
    {
        m_dtmfSendActive = false;
        if (m_dtmfPttOffTimer)
        {
            m_dtmfPttOffTimer->stop();
        }
        if (m_dtmfDialog)
        {
            m_dtmfDialog->setSendInProgress(false);
        }
    }
    updateTxIndicator(on);
    m_pttBtn->setProperty("pttActive", on);
    m_pttBtn->style()->unpolish(m_pttBtn);
    m_pttBtn->style()->polish(m_pttBtn);
    m_pttBtn->update();
    setSelectorButtonLines(m_pttBtn, QStringLiteral("PTT"), on ? QStringLiteral("ON") : QStringLiteral("OFF"));
}

void MainWindow::onDuplexModeChanged(duplexMode_t mode)
{
    m_duplexMode = mode;
    if (!m_activeMemoryId.isEmpty())
    {
        if (mode == m_activeMemoryDuplexMode)
        {
            m_activeMemoryDuplexSettled = true;
            checkIfMemorySelectionComplete();
        }
        else if (m_activeMemoryDuplexSettled && !m_applyingMemorySelection)
        {
            clearActiveMemory();
        }
    }
    updateOffsetButton();
}

void MainWindow::onRepeaterOffsetChanged(quint64 hz)
{
    m_repeaterOffsetHz = hz;
    if (!m_activeMemoryId.isEmpty() && m_activeMemoryDuplexMode != dmSimplex)
    {
        if (hz == m_activeMemoryOffsetHz)
        {
            m_activeMemoryOffsetSettled = true;
            checkIfMemorySelectionComplete();
        }
        else if (m_activeMemoryOffsetSettled && !m_applyingMemorySelection)
        {
            clearActiveMemory();
        }
    }
    updateOffsetButton();
}

void MainWindow::onToneAccessModeChanged(rptAccessTxRx_t mode)
{
    m_toneAccessMode = mode;
    if (!m_activeMemoryId.isEmpty())
    {
        if (mode == m_activeMemoryToneMode)
        {
            m_activeMemoryToneModeSettled = true;
            checkIfMemorySelectionComplete();
        }
        else if (m_activeMemoryToneModeSettled && !m_applyingMemorySelection)
        {
            clearActiveMemory();
        }
    }
    updateToneButton();
}

void MainWindow::onToneFrequencyChanged(ushort tone)
{
    m_toneFrequency = tone;
    if (!m_activeMemoryId.isEmpty() && m_activeMemoryToneMode != ratrNN && !isDtcsToneMode(m_activeMemoryToneMode))
    {
        if (tone == m_activeMemoryToneValue)
        {
            m_activeMemoryToneValueSettled = true;
            checkIfMemorySelectionComplete();
        }
        else if (m_activeMemoryToneValueSettled && !m_applyingMemorySelection)
        {
            clearActiveMemory();
        }
    }
    updateToneButton();
}

void MainWindow::onDtcsCodeChanged(ushort code)
{
    m_dtcsCode = code;
    if (!m_activeMemoryId.isEmpty() && isDtcsToneMode(m_activeMemoryToneMode))
    {
        if (code == m_activeMemoryToneValue)
        {
            m_activeMemoryToneValueSettled = true;
            checkIfMemorySelectionComplete();
        }
        else if (m_activeMemoryToneValueSettled && !m_applyingMemorySelection)
        {
            clearActiveMemory();
        }
    }
    updateToneButton();
}

void MainWindow::commitFrequencyEdit(VfoPanel* panel)
{
    auto* backend = m_model ? m_model->backend() : nullptr;
    if (!panel || !m_vfo || !backend || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }
    if (panel != m_vfoPanel)
    {
        panel->setFrequencyText(QStringLiteral("---.---.---"));
        panel->clearFrequencyFocus();
        return;
    }

    const quint64 currentHz = m_vfoFrequencyHz;
    const auto restoreFrequencyText = [this, panel, currentHz]()
    {
        if (currentHz > 0)
        {
            panel->setFrequencyText(formatFrequency(currentHz));
        }
        else
        {
            panel->setFrequencyText(QStringLiteral("---.---.---"));
        }
    };

    quint64 hz = 0;
    if (!parseFrequencyText(panel->frequencyText(), &hz))
    {
        restoreFrequencyText();
        return;
    }

    clearActiveMemory();
    backend->setFrequencyHz(hz);
    panel->clearFrequencyFocus();
}

void MainWindow::showAgcMenu()
{
    m_radioCommandController->showAgcMenu();
}

void MainWindow::showPreampMenu()
{
    m_radioCommandController->showPreampMenu();
}

void MainWindow::updatePreampButton()
{
    m_radioCommandController->updatePreampButton();
}

void MainWindow::showNotchMenu()
{
    m_radioCommandController->showNotchMenu();
}

void MainWindow::updateNotchButton()
{
    m_radioCommandController->updateNotchButton();
}

void MainWindow::updateRitButton()
{
    m_radioCommandController->updateRitButton();
}

void MainWindow::showRitMenu()
{
    m_radioCommandController->showRitMenu();
}

void MainWindow::showCustomRitDialog()
{
    m_radioCommandController->showCustomRitDialog();
}

void MainWindow::showOffsetMenu()
{
    m_radioCommandController->showOffsetMenu();
}

void MainWindow::showCustomOffsetDialog()
{
    m_radioCommandController->showCustomOffsetDialog();
}

void MainWindow::applyOffsetSelection(duplexMode_t mode, quint64 offsetHz)
{
    m_radioCommandController->applyOffsetSelection(mode, offsetHz);
}

void MainWindow::updateOffsetButton()
{
    m_radioCommandController->updateOffsetButton();
}

void MainWindow::showToneMenu()
{
    m_radioCommandController->showToneMenu();
}

void MainWindow::applyToneSelection(rptAccessTxRx_t mode, ushort value)
{
    m_radioCommandController->applyToneSelection(mode, value);
}

void MainWindow::updateToneButton()
{
    m_radioCommandController->updateToneButton();
}

void MainWindow::updateSquelchButton()
{
    m_radioCommandController->updateSquelchButton();
}

void MainWindow::updateTxPowerButton()
{
    m_radioCommandController->updateTxPowerButton();
}

void MainWindow::updateRfGainButton()
{
    m_radioCommandController->updateRfGainButton();
}

void MainWindow::showRfGainMenu()
{
    m_radioCommandController->showRfGainMenu();
}
