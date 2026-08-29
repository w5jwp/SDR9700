#include "RadioCommandController.h"
#include "DialogFooter.h"

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
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <algorithm>
#include <utility>

using namespace sdr9700::ui::main_window;


RadioCommandController::RadioCommandController(MainWindow* window, CompressorLevelSetter compressorLevelSetter)
    : QObject(window), m_window(window), m_compressorLevelSetter(std::move(compressorLevelSetter))
{
    if (!m_compressorLevelSetter)
    {
        m_compressorLevelSetter = [window](int value) { window->m_vfo->setCompressorLevel(value); };
    }
}

void RadioCommandController::toggleMute()
{
    m_window->m_muted = !m_window->m_muted;
    if (m_window->m_muted)
    {
        m_window->m_savedAfGain = m_window->m_currentAfGain;
        m_window->m_currentAfGain = 0;
        if (m_window->m_titleBar)
        {
            m_window->m_titleBar->setVolume(0);
            m_window->m_titleBar->setMuted(true);
        }
        m_window->onAfGainChanged(0);
    }
    else
    {
        const int restored = qBound(0, m_window->m_savedAfGain, 255);
        m_window->m_currentAfGain = restored;
        if (m_window->m_titleBar)
        {
            m_window->m_titleBar->setVolume(restored);
            m_window->m_titleBar->setMuted(false);
        }
        m_window->onAfGainChanged(restored);
    }
    setCommandButtonActive(m_window->m_muteBtn, m_window->m_muted);
    m_window->updateIcomRC28Leds();
}

void RadioCommandController::cycleMode()
{
    if (!m_window->m_vfo || !m_window->m_model->isReady() || m_window->m_controlsLocked)
    {
        return;
    }

    const QStringList modes = m_window->m_vfo->availableModes();
    if (modes.isEmpty())
    {
        return;
    }

    const QString current = m_window->m_vfo->mode();
    const int index = modes.indexOf(current);
    const int nextIndex = index >= 0 ? (index + 1) % modes.size() : 0;
    m_window->leaveMemoryModeForManualChange();
    m_window->m_vfo->setMode(modes.at(nextIndex));
}

void RadioCommandController::toggleRit()
{
    if (!m_window->m_vfo || !m_window->m_model->isReady() || m_window->m_controlsLocked)
    {
        return;
    }

    if (m_window->m_vfo->ritOn())
    {
        m_window->m_vfo->setRitEnabled(false);
    }
    else
    {
        m_window->m_vfo->setRitOffset(m_window->m_vfo->ritHz());
        m_window->m_vfo->setRitEnabled(true);
    }
    m_window->updateIcomRC28Leds();
}

void RadioCommandController::updateRitButton()
{
    if (!m_window->m_ritBtn || !m_window->m_vfo)
    {
        return;
    }
    const bool on = m_window->m_vfo->ritOn();
    const short hz = m_window->m_vfo->ritHz();
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
    setSelectorButtonLines(m_window->m_ritBtn, QStringLiteral("RIT"), label);
    setCommandButtonActive(m_window->m_ritBtn, on);
}

void RadioCommandController::showRitMenu()
{
    if (!m_window->m_ritBtn || !m_window->m_vfo || !m_window->m_model->isReady() || m_window->m_controlsLocked)
    {
        return;
    }
    QMenu menu(m_window);
    styleCompactMenu(&menu);
    const auto* customAction = menu.addAction(QStringLiteral("CUSTOM"));
    menu.addSeparator();
    const auto* offAction = menu.addAction(QStringLiteral("OFF"));
    connect(customAction, &QAction::triggered, this, &RadioCommandController::showCustomRitDialog);
    connect(offAction, &QAction::triggered, this, [this]() { m_window->m_vfo->setRitEnabled(false); });
    menu.exec(m_window->m_ritBtn->mapToGlobal(QPoint(0, m_window->m_ritBtn->height())));
}

void RadioCommandController::showCustomRitDialog()
{
    if (!m_window->m_vfo || !m_window->m_model->isReady() || m_window->m_controlsLocked)
    {
        return;
    }

    QDialog dialog(m_window);
    dialog.setWindowTitle(QStringLiteral("Custom RIT"));
    dialog.setModal(true);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(UiTheme::Size::DialogContentMargin, 10, UiTheme::Size::DialogContentMargin, 0);
    layout->setSpacing(sdr9700::ui::kDialogFooterSpacing);
    auto* form = new QFormLayout;
    auto* rit = new QSpinBox(&dialog);
    rit->setRange(-999, 999);
    rit->setSingleStep(10);
    rit->setSuffix(QStringLiteral(" Hz"));
    rit->setValue(m_window->m_vfo->ritOn() ? m_window->m_vfo->ritHz() : 0);
    form->addRow(QStringLiteral("RIT"), rit);
    layout->addLayout(form);

    const sdr9700::ui::DialogFooter footer = sdr9700::ui::createDialogFooter(&dialog);
    footer.buttonBox->addButton(QDialogButtonBox::Cancel);
    footer.buttonBox->addButton(QStringLiteral("Set"), QDialogButtonBox::AcceptRole);
    layout->addWidget(footer.widget);
    connect(footer.buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(footer.buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    m_window->centerPopupWindow(&dialog);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const short hz = static_cast<short>(rit->value());
    if (hz == 0)
    {
        m_window->m_vfo->setRitEnabled(false);
        return;
    }

    m_window->m_vfo->setRitOffset(hz);
    m_window->m_vfo->setRitEnabled(true);
}

void RadioCommandController::showOffsetMenu(const QPoint& position)
{
    if ((position.isNull() && !m_window->m_offsetBtn) || !m_window->m_vfo || !m_window->m_model->isReady() ||
        m_window->m_controlsLocked)
    {
        return;
    }

    QMenu menu(m_window);
    styleCompactMenu(&menu);

    const auto* simplexAction = menu.addAction(QStringLiteral("SIMPLEX"));
    menu.addSeparator();
    const QVector<OffsetPreset> presets = offsetPresetsForHz(m_window->m_vfo->frequencyHz());
    QVector<QAction*> presetActions;
    presetActions.reserve(presets.size());
    for (const OffsetPreset& preset : presets)
    {
        presetActions.append(menu.addAction(preset.label));
    }
    menu.addSeparator();
    const auto* customAction = menu.addAction(QStringLiteral("CUSTOM"));

    const QPoint menuPosition =
        position.isNull() ? m_window->m_offsetBtn->mapToGlobal(QPoint(0, m_window->m_offsetBtn->height())) : position;
    const QAction* selected = menu.exec(menuPosition);
    if (!selected)
    {
        return;
    }

    if (selected == simplexAction)
    {
        applyOffsetSelection(dmSimplex, m_window->m_repeaterOffsetHz);
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
    if (m_window->m_controlsLocked)
    {
        return;
    }

    QDialog dialog(m_window);
    dialog.setWindowTitle(QStringLiteral("Custom Offset"));
    dialog.setModal(true);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(UiTheme::Size::DialogContentMargin, 10, UiTheme::Size::DialogContentMargin, 0);
    layout->setSpacing(sdr9700::ui::kDialogFooterSpacing);
    auto* form = new QFormLayout;
    auto* direction = new QComboBox(&dialog);
    direction->addItem(QStringLiteral("+"), QVariant::fromValue<int>(dmDupPlus));
    direction->addItem(QStringLiteral("-"), QVariant::fromValue<int>(dmDupMinus));
    direction->setCurrentIndex(m_window->m_duplexMode == dmDupMinus ? 1 : 0);

    auto* offset = new QDoubleSpinBox(&dialog);
    offset->setDecimals(3);
    offset->setRange(0.001, 99.999);
    offset->setSingleStep(0.005);
    offset->setSuffix(QStringLiteral(" MHz"));
    offset->setValue(qMax<quint64>(1, m_window->m_repeaterOffsetHz) / 1000000.0);

    form->addRow(QStringLiteral("Direction"), direction);
    form->addRow(QStringLiteral("Offset"), offset);
    layout->addLayout(form);

    const sdr9700::ui::DialogFooter footer = sdr9700::ui::createDialogFooter(&dialog);
    footer.buttonBox->addButton(QDialogButtonBox::Cancel);
    footer.buttonBox->addButton(QStringLiteral("Set"), QDialogButtonBox::AcceptRole);
    layout->addWidget(footer.widget);
    connect(footer.buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(footer.buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    m_window->centerPopupWindow(&dialog);
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
    if (!m_window->m_vfo || m_window->m_controlsLocked)
    {
        return;
    }

    m_window->leaveMemoryModeForManualChange();

    if (mode == dmSimplex)
    {
        m_window->m_vfo->setDuplexMode(dmSimplex);
        return;
    }

    m_window->m_vfo->setRepeaterOffsetHz(offsetHz);
    m_window->m_vfo->setDuplexMode(mode);
}

void RadioCommandController::updateOffsetButton()
{
    if (!m_window->m_offsetBtn)
    {
        return;
    }

    const bool active = m_window->m_duplexMode == dmDupMinus || m_window->m_duplexMode == dmDupPlus;
    setSelectorButtonLines(m_window->m_offsetBtn, QStringLiteral("OFFSET"),
                           offsetModeLabel(m_window->m_duplexMode, m_window->m_repeaterOffsetHz));
    setCommandButtonActive(m_window->m_offsetBtn, active);
}

void RadioCommandController::showToneMenu(const QPoint& position)
{
    if ((position.isNull() && !m_window->m_toneBtn) || !m_window->m_vfo || !m_window->m_model->isReady() ||
        m_window->m_controlsLocked)
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
    addCtcssMenu(&menu, QStringLiteral("TSQL"), ratrTT);
    menu.addSeparator();
    addDtcsMenu(&menu, QStringLiteral("DTCS"), ratrDD);
    menu.addSeparator();
    const auto* offAction = menu.addAction(QStringLiteral("OFF"));

    const QPoint menuPosition =
        position.isNull() ? m_window->m_toneBtn->mapToGlobal(QPoint(0, m_window->m_toneBtn->height())) : position;
    const QAction* selected = menu.exec(menuPosition);
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
    if (!m_window->m_vfo || m_window->m_controlsLocked)
    {
        return;
    }

    const bool dtcs = isDtcsToneMode(mode);
    m_window->leaveMemoryModeForManualChange();

    if (mode == ratrNN)
    {
        m_window->m_vfo->setToneAccessMode(mode);
        return;
    }

    if (dtcs)
    {
        m_window->m_vfo->setDtcsCode(value);
    }
    else
    {
        m_window->m_vfo->setToneFrequency(value);
    }
    m_window->m_vfo->setToneAccessMode(mode);
}

void RadioCommandController::updateToneButton()
{
    if (!m_window->m_toneBtn)
    {
        return;
    }

    const bool active = m_window->m_toneAccessMode != ratrNN;
    const QString primary = active ? toneOptionLabel(m_window->m_toneAccessMode) : QStringLiteral("TONE");
    const ushort value = isDtcsToneMode(m_window->m_toneAccessMode) ? m_window->m_dtcsCode : m_window->m_toneFrequency;
    const QString secondary =
        active ? memoryToneFrequencyLabel(m_window->m_toneAccessMode, value) : QStringLiteral("OFF");
    setSelectorButtonLines(m_window->m_toneBtn, primary, secondary);
    setCommandButtonActive(m_window->m_toneBtn, active);
}

void RadioCommandController::showCompressorMenu(const QPoint& position)
{
    if (!m_window->m_compBtn || !m_window->m_vfo || !m_window->m_model->isReady() || m_window->m_controlsLocked)
    {
        return;
    }

    QMenu menu(m_window);
    styleCompactMenu(&menu);

    auto* enabledAction = menu.addAction(QStringLiteral("Enabled"));
    enabledAction->setCheckable(true);
    enabledAction->setChecked(m_window->m_vfo->compressorOn());
    connect(enabledAction, &QAction::toggled, this, [this](bool enabled) { m_window->m_vfo->setCompressor(enabled); });
    connect(m_window->m_vfo, &VfoModel::compressorChanged, enabledAction,
            [enabledAction](bool enabled)
            {
                const QSignalBlocker block(enabledAction);
                enabledAction->setChecked(enabled);
            });

    auto* panel = new QWidget(&menu);
    panel->setFixedWidth(190);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(8, 6, 8, 6);
    panelLayout->setSpacing(4);

    auto levelText = [](int value)
    { return QStringLiteral("Level %1%").arg(qRound(qBound(0, value, 255) * 100.0 / 255.0)); };

    const bool levelKnown = m_window->m_vfo->compressorLevelKnown();
    auto* valueLabel =
        new QLabel(levelKnown ? levelText(m_window->m_vfo->compressorLevel()) : QStringLiteral("Level --"), panel);
    valueLabel->setAlignment(Qt::AlignCenter);
    valueLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-size: 10px; font-weight: bold; }").arg(UiTheme::Color::TextMuted));

    auto* slider = new QSlider(Qt::Horizontal, panel);
    slider->setRange(0, 255);
    slider->setValue(m_window->m_vfo->compressorLevel());
    slider->setEnabled(levelKnown);
    slider->setAccessibleName(QStringLiteral("Speech compressor level"));
    slider->setAccessibleDescription(QStringLiteral("Adjusts the IC-9700 speech compression level."));
    connect(slider, &QSlider::valueChanged, this,
            [this, valueLabel, levelText](int value)
            {
                valueLabel->setText(levelText(value));
                m_compressorLevelSetter(value);
            });
    connect(m_window->m_vfo, &VfoModel::compressorLevelChanged, slider,
            [slider, valueLabel, levelText](int value)
            {
                const QSignalBlocker block(slider);
                slider->setValue(value);
                valueLabel->setText(levelText(value));
            });
    connect(m_window->m_vfo, &VfoModel::compressorLevelKnownChanged, slider,
            [slider, valueLabel](bool known)
            {
                slider->setEnabled(known);
                if (!known)
                {
                    valueLabel->setText(QStringLiteral("Level --"));
                }
            });

    panelLayout->addWidget(valueLabel);
    panelLayout->addWidget(slider);

    auto* panelAction = new QWidgetAction(&menu);
    panelAction->setDefaultWidget(panel);
    menu.addAction(panelAction);
    menu.exec(position.isNull() ? m_window->m_compBtn->mapToGlobal(QPoint(0, m_window->m_compBtn->height()))
                                : position);
}

int RadioCommandController::tuningStepHz() const
{
    return qBound(
        1, AppSettings::instance().value(QString::fromLatin1(kTuningStepHZSettingsKey), kDefaultTuningStepHZ).toInt(),
        10000000);
}

void RadioCommandController::applyRadioTuningStep()
{
    if (!m_window->m_model || !m_window->m_model->isReady())
    {
        return;
    }

    const int step = radioTuningStepForHz(tuningStepHz());
    if (step >= 0)
    {
        m_window->m_model->setTuningStep(step);
    }
}

void RadioCommandController::updateStepButton()
{
    if (!m_window->m_vfoPanel)
    {
        return;
    }
    const int hz = tuningStepHz();
    const auto presetIt = std::find_if(std::begin(kStepPresets), std::end(kStepPresets),
                                       [hz](const StepPreset& preset) { return preset.hz == hz; });
    if (presetIt != std::end(kStepPresets))
    {
        m_window->m_vfoPanel->setStepText(QString::fromLatin1(presetIt->label));
        return;
    }
    // Custom value not in the preset list — format with units
    if (hz >= 1000000)
    {
        m_window->m_vfoPanel->setStepText(QStringLiteral("%1 MHz").arg(hz / 1000000));
    }
    else if (hz >= 1000)
    {
        m_window->m_vfoPanel->setStepText(QStringLiteral("%1 kHz").arg(hz / 1000.0, 0, 'f', hz % 1000 == 0 ? 0 : 3));
    }
    else
    {
        m_window->m_vfoPanel->setStepText(QStringLiteral("%1 Hz").arg(hz));
    }
}
