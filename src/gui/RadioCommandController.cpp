#include "RadioCommandController.h"

#include "AppSettings.h"
#include "LogCategories.h"
#include "MainTitleBar.h"
#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "UiTheme.h"
#include "VfoPanel.h"
#include "backend/IRadioBackend.h"
#include "models/RadioModel.h"
#include "models/VfoModel.h"

#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <algorithm>

using namespace sdr9700::ui::main_window;

#define m_model m_window->m_model
#define m_vfo m_window->m_vfo
#define m_controlsLocked m_window->m_controlsLocked
#define m_muted m_window->m_muted
#define m_savedAfGain m_window->m_savedAfGain
#define m_currentAfGain m_window->m_currentAfGain
#define m_titleBar m_window->m_titleBar
#define m_muteBtn m_window->m_muteBtn
#define m_agcBtn m_window->m_agcBtn
#define m_preBtn m_window->m_preBtn
#define m_notchBtn m_window->m_notchBtn
#define m_ritBtn m_window->m_ritBtn
#define m_offsetBtn m_window->m_offsetBtn
#define m_toneBtn m_window->m_toneBtn
#define m_squelchBtn m_window->m_squelchBtn
#define m_txPowerBtn m_window->m_txPowerBtn
#define m_rfGainBtn m_window->m_rfGainBtn
#define m_rfGainValue m_window->m_rfGainValue
#define m_squelchValue m_window->m_squelchValue
#define m_txPowerValue m_window->m_txPowerValue
#define m_duplexMode m_window->m_duplexMode
#define m_repeaterOffsetHz m_window->m_repeaterOffsetHz
#define m_toneAccessMode m_window->m_toneAccessMode
#define m_dtcsCode m_window->m_dtcsCode
#define m_toneFrequency m_window->m_toneFrequency
#define m_vfoPanel m_window->m_vfoPanel
#define centerPopupWindow m_window->centerPopupWindow
#define clearActiveMemory m_window->clearActiveMemory
#define onAfGainChanged m_window->onAfGainChanged
#define updateIcomRC28Leds m_window->updateIcomRC28Leds

RadioCommandController::RadioCommandController(MainWindow* window) : QObject(window), m_window(window) {}

void RadioCommandController::toggleMute()
{
    m_muted = !m_muted;
    if (m_muted)
    {
        m_savedAfGain = m_currentAfGain;
        m_currentAfGain = 0;
        if (m_titleBar)
        {
            m_titleBar->setVolume(0);
            m_titleBar->setMuted(true);
        }
        onAfGainChanged(0);
    }
    else
    {
        const int restored = qBound(0, m_savedAfGain, 255);
        m_currentAfGain = restored;
        if (m_titleBar)
        {
            m_titleBar->setVolume(restored);
            m_titleBar->setMuted(false);
        }
        onAfGainChanged(restored);
    }
    setCommandButtonActive(m_muteBtn, m_muted);
    updateIcomRC28Leds();
}

void RadioCommandController::cycleMode()
{
    if (!m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    const QStringList modes = m_vfo->availableModes();
    if (modes.isEmpty())
    {
        return;
    }

    const QString current = m_vfo->mode();
    const int index = modes.indexOf(current);
    const int nextIndex = index >= 0 ? (index + 1) % modes.size() : 0;
    m_vfo->setMode(modes.at(nextIndex));
}

void RadioCommandController::toggleRit()
{
    if (!m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    if (m_vfo->ritOn())
    {
        m_vfo->setRitEnabled(false);
    }
    else
    {
        m_vfo->setRitOffset(m_vfo->ritHz());
        m_vfo->setRitEnabled(true);
    }
    updateIcomRC28Leds();
}

void RadioCommandController::showAgcMenu()
{
    if (!m_agcBtn || !m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }
    QMenu menu(m_window);
    styleCompactMenu(&menu);
    static const struct
    {
        const char* mode;
        const char* label;
    } kItems[] = {{"fast", "FAST"}, {"mid", "MID"}, {"slow", "SLOW"}};
    for (const auto& item : kItems)
    {
        auto* act = menu.addAction(QString::fromLatin1(item.label));
        const QString modeStr = QString::fromLatin1(item.mode);
        connect(act, &QAction::triggered, this, [this, modeStr]() { m_vfo->setAgcMode(modeStr); });
    }
    menu.exec(m_agcBtn->mapToGlobal(QPoint(0, m_agcBtn->height())));
}

void RadioCommandController::showPreampMenu()
{
    if (!m_preBtn || !m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    QMenu menu(m_window);
    styleCompactMenu(&menu);
    static const struct
    {
        const char* label;
        int level;
    } kItems[] = {{"OFF", 0}, {"INT", 1}, {"EXT", 2}, {"INT+EXT", 3}};
    for (const auto& item : kItems)
    {
        auto* act = menu.addAction(QString::fromLatin1(item.label));
        connect(act, &QAction::triggered, this, [this, item]() { m_vfo->setPreampLevel(item.level); });
    }
    menu.exec(m_preBtn->mapToGlobal(QPoint(0, m_preBtn->height())));
}

void RadioCommandController::updatePreampButton()
{
    if (!m_preBtn || !m_vfo)
    {
        return;
    }

    const int level = m_vfo->preampLevel();
    setSelectorButtonLines(m_preBtn, QStringLiteral("PRE"), preampLevelLabel(level));
    setCommandButtonActive(m_preBtn, level != 0);
}

void RadioCommandController::showNotchMenu()
{
    if (!m_notchBtn || !m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    QMenu menu(m_window);
    styleCompactMenu(&menu);
    const auto* offAction = menu.addAction(QStringLiteral("OFF"));
    const auto* autoAction = menu.addAction(QStringLiteral("AUTO"));
    const auto* manualAction = menu.addAction(QStringLiteral("MANUAL"));
    const auto* bothAction = menu.addAction(QStringLiteral("AUTO+MANUAL"));

    const QAction* selected = menu.exec(m_notchBtn->mapToGlobal(QPoint(0, m_notchBtn->height())));
    if (!selected)
    {
        return;
    }

    if (selected == offAction)
    {
        m_vfo->setAutoNotch(false);
        m_vfo->setManualNotch(false);
    }
    else if (selected == autoAction)
    {
        m_vfo->setManualNotch(false);
        m_vfo->setAutoNotch(true);
    }
    else if (selected == manualAction)
    {
        m_vfo->setAutoNotch(false);
        m_vfo->setManualNotch(true);
    }
    else if (selected == bothAction)
    {
        m_vfo->setAutoNotch(true);
        m_vfo->setManualNotch(true);
    }
}

void RadioCommandController::updateNotchButton()
{
    if (!m_notchBtn || !m_vfo)
    {
        return;
    }

    const bool autoOn = m_vfo->autoNotchOn();
    const bool manualOn = m_vfo->manualNotchOn();
    const QString secondary = autoOn && manualOn ? QStringLiteral("A/M")
                              : autoOn           ? QStringLiteral("AUTO")
                              : manualOn         ? QStringLiteral("MAN")
                                                 : QStringLiteral("OFF");
    setSelectorButtonLines(m_notchBtn, QStringLiteral("NOTCH"), secondary);
    setCommandButtonActive(m_notchBtn, autoOn || manualOn);
}

void RadioCommandController::updateRitButton()
{
    if (!m_ritBtn || !m_vfo)
    {
        return;
    }
    const bool on = m_vfo->ritOn();
    const short hz = m_vfo->ritHz();
    QString label;
    if (!on)
    {
        label = QStringLiteral("OFF");
    }
    else if (hz >= 0)
    {
        label = QStringLiteral("+%1").arg(hz);
    }
    else
    {
        label = QString::number(hz);
    }
    setSelectorButtonLines(m_ritBtn, QStringLiteral("RIT"), label);
    setCommandButtonActive(m_ritBtn, on);
}

void RadioCommandController::showRitMenu()
{
    if (!m_ritBtn || !m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }
    QMenu menu(m_window);
    styleCompactMenu(&menu);
    const auto* customAction = menu.addAction(QStringLiteral("CUSTOM"));
    menu.addSeparator();
    const auto* offAction = menu.addAction(QStringLiteral("OFF"));
    connect(customAction, &QAction::triggered, this, &RadioCommandController::showCustomRitDialog);
    connect(offAction, &QAction::triggered, this, [this]() { m_vfo->setRitEnabled(false); });
    menu.exec(m_ritBtn->mapToGlobal(QPoint(0, m_ritBtn->height())));
}

void RadioCommandController::showCustomRitDialog()
{
    if (!m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    QDialog dialog(m_window);
    dialog.setWindowTitle(QStringLiteral("Custom RIT"));
    dialog.setModal(true);

    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* rit = new QSpinBox(&dialog);
    rit->setRange(-999, 999);
    rit->setSingleStep(10);
    rit->setSuffix(QStringLiteral(" Hz"));
    rit->setValue(m_vfo->ritOn() ? m_vfo->ritHz() : 0);
    form->addRow(QStringLiteral("RIT"), rit);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    centerPopupWindow(&dialog);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const short hz = static_cast<short>(rit->value());
    if (hz == 0)
    {
        m_vfo->setRitEnabled(false);
        return;
    }

    m_vfo->setRitOffset(hz);
    m_vfo->setRitEnabled(true);
}

void RadioCommandController::showOffsetMenu()
{
    if (!m_offsetBtn || !m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    QMenu menu(m_window);
    styleCompactMenu(&menu);

    const auto* simplexAction = menu.addAction(QStringLiteral("SIMPLEX"));
    menu.addSeparator();
    const QVector<OffsetPreset> presets = offsetPresetsForHz(m_vfo->frequencyHz());
    QVector<QAction*> presetActions;
    presetActions.reserve(presets.size());
    for (const OffsetPreset& preset : presets)
    {
        presetActions.append(menu.addAction(preset.label));
    }
    menu.addSeparator();
    const auto* customAction = menu.addAction(QStringLiteral("CUSTOM"));

    const QAction* selected = menu.exec(m_offsetBtn->mapToGlobal(QPoint(0, m_offsetBtn->height())));
    if (!selected)
    {
        return;
    }

    if (selected == simplexAction)
    {
        applyOffsetSelection(dmSimplex, m_repeaterOffsetHz);
        return;
    }

    for (int i = 0; i < presetActions.size(); ++i)
    {
        if (selected == presetActions.at(i))
        {
            const OffsetPreset& preset = presets.at(i);
            applyOffsetSelection(preset.mode, preset.hz);
            return;
        }
    }

    if (selected == customAction)
    {
        showCustomOffsetDialog();
    }
}

void RadioCommandController::showCustomOffsetDialog()
{
    if (m_controlsLocked)
    {
        return;
    }

    QDialog dialog(m_window);
    dialog.setWindowTitle(QStringLiteral("Custom Offset"));
    dialog.setModal(true);

    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* direction = new QComboBox(&dialog);
    direction->addItem(QStringLiteral("+"), QVariant::fromValue<int>(dmDupPlus));
    direction->addItem(QStringLiteral("-"), QVariant::fromValue<int>(dmDupMinus));
    direction->setCurrentIndex(m_duplexMode == dmDupMinus ? 1 : 0);

    auto* offset = new QDoubleSpinBox(&dialog);
    offset->setDecimals(3);
    offset->setRange(0.001, 99.999);
    offset->setSingleStep(0.005);
    offset->setSuffix(QStringLiteral(" MHz"));
    offset->setValue(qMax<quint64>(1, m_repeaterOffsetHz) / 1000000.0);

    form->addRow(QStringLiteral("Direction"), direction);
    form->addRow(QStringLiteral("Offset"), offset);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    centerPopupWindow(&dialog);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const auto mode = static_cast<duplexMode_t>(direction->currentData().toInt());
    const quint64 offsetHz = static_cast<quint64>(offset->value() * 1000000.0 + 0.5);
    applyOffsetSelection(mode, offsetHz);
}

void RadioCommandController::applyOffsetSelection(duplexMode_t mode, quint64 offsetHz)
{
    if (!m_vfo || m_controlsLocked)
    {
        return;
    }

    clearActiveMemory();
    m_duplexMode = mode;
    if (mode != dmSimplex)
    {
        m_repeaterOffsetHz = offsetHz;
    }
    updateOffsetButton();

    if (mode == dmSimplex)
    {
        m_vfo->setDuplexMode(dmSimplex);
        return;
    }

    m_vfo->setRepeaterOffsetHz(offsetHz);
    m_vfo->setDuplexMode(mode);
}

void RadioCommandController::updateOffsetButton()
{
    if (!m_offsetBtn)
    {
        return;
    }

    const bool active = m_duplexMode == dmDupMinus || m_duplexMode == dmDupPlus;
    setSelectorButtonLines(m_offsetBtn, QStringLiteral("OFFSET"), offsetModeLabel(m_duplexMode, m_repeaterOffsetHz));
    setCommandButtonActive(m_offsetBtn, active);
}

void RadioCommandController::showToneMenu()
{
    if (!m_toneBtn || !m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    QMenu menu(m_window);
    styleCompactMenu(&menu);

    auto styleToneGridButton = [](QPushButton* button)
    {
        button->setFixedSize(54, 24);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QStringLiteral("QPushButton { background: %1; border: 1px solid %2; border-radius: 3px; "
                                             "color: %3; font-size: 11px; }"
                                             "QPushButton:hover { background: %4; border-color: %5; color: %6; }")
                                  .arg(UiTheme::Color::Button, UiTheme::Color::BorderLight, UiTheme::Color::TextPrimary,
                                       UiTheme::Color::AccentDark, UiTheme::Color::Accent, UiTheme::Color::White));
    };

    auto addCtcssMenu = [this, &menu, styleToneGridButton](QMenu* parent, const QString& title, rptAccessTxRx_t mode)
    {
        auto* submenu = parent->addMenu(title);
        styleCompactMenu(submenu);
        auto* panel = new QWidget(submenu);
        auto* grid = new QGridLayout(panel);
        grid->setContentsMargins(6, 6, 6, 6);
        grid->setHorizontalSpacing(4);
        grid->setVerticalSpacing(4);
        static constexpr int kColumns = 4;
        int index = 0;
        for (const TonePreset& preset : kTonePresets)
        {
            auto* button = new QPushButton(QString::fromLatin1(preset.label), panel);
            styleToneGridButton(button);
            const ushort tone = preset.tone;
            connect(button, &QPushButton::clicked, this,
                    [this, &menu, submenu, mode, tone]()
                    {
                        applyToneSelection(mode, tone);
                        submenu->close();
                        menu.close();
                    });
            grid->addWidget(button, index / kColumns, index % kColumns);
            ++index;
        }
        auto* action = new QWidgetAction(submenu);
        action->setDefaultWidget(panel);
        submenu->addAction(action);
    };

    auto addDtcsMenu = [this, &menu, styleToneGridButton](QMenu* parent, const QString& title, rptAccessTxRx_t mode)
    {
        auto* submenu = parent->addMenu(title);
        styleCompactMenu(submenu);
        auto* panel = new QWidget(submenu);
        auto* grid = new QGridLayout(panel);
        grid->setContentsMargins(6, 6, 6, 6);
        grid->setHorizontalSpacing(4);
        grid->setVerticalSpacing(4);
        static constexpr int kColumns = 6;
        int index = 0;
        for (const ushort code : kDtcsCodes)
        {
            auto* button = new QPushButton(dtcsCodeLabel(code), panel);
            styleToneGridButton(button);
            connect(button, &QPushButton::clicked, this,
                    [this, &menu, submenu, mode, code]()
                    {
                        applyToneSelection(mode, code);
                        submenu->close();
                        menu.close();
                    });
            grid->addWidget(button, index / kColumns, index % kColumns);
            ++index;
        }
        auto* action = new QWidgetAction(submenu);
        action->setDefaultWidget(panel);
        submenu->addAction(action);
    };

    addCtcssMenu(&menu, QStringLiteral("TONE"), ratrTN);
    addCtcssMenu(&menu, QStringLiteral("CTCSS"), ratrNT);
    menu.addSeparator();
    addDtcsMenu(&menu, QStringLiteral("DCS"), ratrDN);
    addDtcsMenu(&menu, QStringLiteral("DTCS"), ratrDD);
    menu.addSeparator();
    const auto* offAction = menu.addAction(QStringLiteral("OFF"));

    const QAction* selected = menu.exec(m_toneBtn->mapToGlobal(QPoint(0, m_toneBtn->height())));
    if (!selected)
    {
        return;
    }

    if (selected == offAction)
    {
        applyToneSelection(ratrNN, 0);
    }
}

void RadioCommandController::applyToneSelection(rptAccessTxRx_t mode, ushort value)
{
    if (!m_vfo || m_controlsLocked)
    {
        return;
    }

    const bool dtcs = isDtcsToneMode(mode);
    clearActiveMemory();
    m_toneAccessMode = mode;
    if (dtcs)
    {
        m_dtcsCode = value;
    }
    else if (mode != ratrNN)
    {
        m_toneFrequency = value;
    }
    updateToneButton();

    if (mode == ratrNN)
    {
        m_vfo->setToneAccessMode(mode);
        return;
    }

    if (dtcs)
    {
        m_vfo->setDtcsCode(value);
    }
    else
    {
        m_vfo->setToneFrequency(value);
    }
    m_vfo->setToneAccessMode(mode);
}

void RadioCommandController::updateToneButton()
{
    if (!m_toneBtn)
    {
        return;
    }

    const bool active = m_toneAccessMode != ratrNN;
    const QString primary = active ? toneOptionLabel(m_toneAccessMode) : QStringLiteral("TONE");
    const ushort value = isDtcsToneMode(m_toneAccessMode) ? m_dtcsCode : m_toneFrequency;
    const QString secondary = active ? memoryToneFrequencyLabel(m_toneAccessMode, value) : QStringLiteral("OFF");
    setSelectorButtonLines(m_toneBtn, primary, secondary);
    setCommandButtonActive(m_toneBtn, active);
}

void RadioCommandController::updateSquelchButton()
{
    if (!m_squelchBtn)
    {
        return;
    }
    const bool active = m_squelchValue > 0;
    const QString pct = active ? QStringLiteral("%1%").arg(m_squelchValue * 100 / 255) : QStringLiteral("OFF");
    setSelectorButtonLines(m_squelchBtn, QStringLiteral("SQL"), pct);
    setCommandButtonActive(m_squelchBtn, active);
}

void RadioCommandController::updateTxPowerButton()
{
    if (!m_txPowerBtn)
    {
        return;
    }

    const bool active = m_txPowerValue > 0;
    const int pct = active ? qBound(1, qRound(m_txPowerValue * 100.0 / 255.0), 100) : 0;
    const QString secondary = active ? QStringLiteral("%1%").arg(pct) : QStringLiteral("OFF");
    setSelectorButtonLines(m_txPowerBtn, QStringLiteral("TX PWR"), secondary);
    setCommandButtonActive(m_txPowerBtn, active);
}

void RadioCommandController::updateRfGainButton()
{
    if (!m_rfGainBtn)
    {
        return;
    }

    const bool active = m_rfGainValue > 0;
    const int pct = active ? qBound(1, qRound(m_rfGainValue * 100.0 / 255.0), 100) : 0;
    const QString secondary = active ? QStringLiteral("%1%").arg(pct) : QStringLiteral("OFF");
    setSelectorButtonLines(m_rfGainBtn, QStringLiteral("RF GAIN"), secondary);
    setCommandButtonActive(m_rfGainBtn, active);
}

void RadioCommandController::showRfGainMenu()
{
    if (!m_rfGainBtn || !m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    QMenu menu(m_window);
    styleCompactMenu(&menu);

    auto* panel = new QWidget(&menu);
    panel->setFixedWidth(190);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(8, 6, 8, 6);
    panelLayout->setSpacing(4);

    auto rfGainPercentText = [](int value)
    {
        const int bounded = qBound(0, value, 255);
        if (bounded == 0)
        {
            return QStringLiteral("OFF");
        }
        return QStringLiteral("%1%").arg(qBound(1, qRound(bounded * 100.0 / 255.0), 100));
    };

    auto* valueLabel = new QLabel(rfGainPercentText(m_rfGainValue), panel);
    valueLabel->setAlignment(Qt::AlignCenter);
    valueLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-size: 10px; font-weight: bold; }").arg(UiTheme::Color::TextMuted));

    auto applyRfGain = [this, valueLabel](int v)
    {
        m_rfGainValue = qBound(0, v, 255);
        const QString text = m_rfGainValue == 0
                                 ? QStringLiteral("OFF")
                                 : QStringLiteral("%1%").arg(qBound(1, qRound(m_rfGainValue * 100.0 / 255.0), 100));
        valueLabel->setText(text);
        updateRfGainButton();
        m_vfo->setRfGain(m_rfGainValue);
    };

    auto* slider = new QSlider(Qt::Horizontal, panel);
    slider->setRange(0, 255);
    slider->setValue(m_rfGainValue);
    connect(slider, &QSlider::valueChanged, this, [applyRfGain](int v) { applyRfGain(v); });

    panelLayout->addWidget(valueLabel);
    panelLayout->addWidget(slider);

    auto* panelAction = new QWidgetAction(&menu);
    panelAction->setDefaultWidget(panel);
    menu.addAction(panelAction);

    menu.exec(m_rfGainBtn->mapToGlobal(QPoint(0, m_rfGainBtn->height())));
}

int RadioCommandController::tuningStepHz() const
{
    return qBound(
        1, AppSettings::instance().value(QString::fromLatin1(kTuningStepHZSettingsKey), kDefaultTuningStepHZ).toInt(),
        10000000);
}

void RadioCommandController::applyRadioTuningStep()
{
    if (!m_model || !m_model->isReady())
    {
        return;
    }

    const int step = radioTuningStepForHz(tuningStepHz());
    if (step >= 0)
    {
        m_model->setTuningStep(step);
    }
}

void RadioCommandController::updateStepButton()
{
    if (!m_vfoPanel)
    {
        return;
    }
    const int hz = tuningStepHz();
    const auto presetIt = std::find_if(std::begin(kStepPresets), std::end(kStepPresets),
                                       [hz](const StepPreset& preset) { return preset.hz == hz; });
    if (presetIt != std::end(kStepPresets))
    {
        m_vfoPanel->setStepText(QString::fromLatin1(presetIt->label));
        return;
    }
    // Custom value not in the preset list — format with units
    if (hz >= 1000000)
    {
        m_vfoPanel->setStepText(QStringLiteral("%1 MHz").arg(hz / 1000000));
    }
    else if (hz >= 1000)
    {
        m_vfoPanel->setStepText(QStringLiteral("%1 kHz").arg(hz / 1000.0, 0, 'f', hz % 1000 == 0 ? 0 : 3));
    }
    else
    {
        m_vfoPanel->setStepText(QStringLiteral("%1 Hz").arg(hz));
    }
}
