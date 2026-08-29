#include "SpectrumScopeController.h"
#include "SpectrumTuningPolicy.h"

#include "AppSettings.h"
#include "SpectrumScopeDisplay.h"
#include "LogCategories.h"
#include "MainWindow.h"
#include "RadioCommandController.h"
#include "MainWindowHelpers.h"
#include "VfoController.h"
#include "VfoDisplay.h"
#include "VfoSelectionController.h"
#include "VfoSelectionPanel.h"
#include "backend/IRadioBackend.h"
#include "models/SpectrumScopeModel.h"
#include "models/RadioModel.h"
#include "models/VfoModel.h"

#include <QComboBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <cmath>

using namespace sdr9700::ui::main_window;

namespace
{
constexpr int kSpectrumToolbarHeight = 29;
}

SpectrumScopeController::SpectrumScopeController(MainWindow* window) : QObject(window), m_window(window) {}

void SpectrumScopeController::buildSpectrumScope(QVBoxLayout* vbox)
{
    auto* vfoStrip = new QWidget(m_window->centralWidget());
    vfoStrip->setObjectName(QStringLiteral("vfoDisplayStrip"));
    vfoStrip->setStyleSheet(
        QStringLiteral("QWidget#vfoDisplayStrip { background: %1; }").arg(UiTheme::Color::ContentBackground));
    auto* vfoLayout = new QHBoxLayout(vfoStrip);
    vfoLayout->setContentsMargins(kControlStripMargins.left(), 0, kControlStripMargins.right(), 6);
    vfoLayout->setSpacing(0);

    auto* vfoBlock = new QWidget(vfoStrip);
    vfoBlock->setObjectName(QStringLiteral("vfoBlock"));
    vfoBlock->setStyleSheet(
        QStringLiteral("QWidget#vfoBlock { background: black; border: 1px solid %1; }").arg(UiTheme::Color::Border));
    auto* vfoBlockLayout = new QHBoxLayout(vfoBlock);
    vfoBlockLayout->setContentsMargins(1, 1, 1, 1);
    vfoBlockLayout->setSpacing(0);

    m_window->m_mainVfoController = new VfoController(Vfo::Main, m_window->m_model->backend(), vfoStrip, m_window);
    m_window->m_subVfoController = new VfoController(Vfo::Sub, m_window->m_model->backend(), vfoStrip, m_window);
    m_window->m_vfoSelectionController = new VfoSelectionController(
        m_window->m_model->backend(), m_window->m_mainVfoController, m_window->m_subVfoController, vfoStrip, m_window);
    m_window->m_vfoSelectionController->panel()->setPttButton(m_window->m_pttBtn);
    const auto showToneMenu = [this](Vfo vfo, const QPoint& position)
    {
        m_window->m_vfoSelectionController->runWhenSelected(
            vfo, [this, position]() { m_window->m_radioCommandController->showToneMenu(position); });
    };
    const auto showOffsetMenu = [this](Vfo vfo, const QPoint& position)
    {
        m_window->m_vfoSelectionController->runWhenSelected(
            vfo, [this, position]() { m_window->m_radioCommandController->showOffsetMenu(position); });
    };
    connect(m_window->m_mainVfoController, &VfoController::toneMenuRequested, this, showToneMenu);
    connect(m_window->m_subVfoController, &VfoController::toneMenuRequested, this, showToneMenu);
    connect(m_window->m_mainVfoController, &VfoController::offsetMenuRequested, this, showOffsetMenu);
    connect(m_window->m_subVfoController, &VfoController::offsetMenuRequested, this, showOffsetMenu);
    connect(m_window->m_mainVfoController, &VfoController::compressorMenuRequested, this,
            [this](Vfo, const QPoint& position) { m_window->m_radioCommandController->showCompressorMenu(position); });
    connect(m_window->m_vfoSelectionController, &VfoSelectionController::selectedVfoChanged, this,
            [this](Vfo vfo)
            {
                if (auto* backend = m_window->m_model ? m_window->m_model->backend() : nullptr)
                {
                    backend->setScopeVfo(vfo);
                }
                if (m_window->m_spectrumScopeDisplay)
                {
                    m_window->m_spectrumScopeDisplay->clearDisplay();
                }
                recenterActiveVfo(false);
            });
    connect(m_window->m_vfoSelectionController, &VfoSelectionController::pttReadyChanged, this,
            [this](bool ready)
            {
                m_window->m_vfoPttReady = ready;
                if (m_window->m_pttBtn)
                {
                    // Never disable a held PTT button before its release event
                    // can unkey the radio.
                    m_window->m_pttBtn->setEnabled((ready || m_window->m_pttActive || m_window->m_pttBtn->isDown()) &&
                                                   m_window->m_model && m_window->m_model->isReady());
                }
            });
    connect(m_window->m_mainVfoController, &VfoController::frequencyChanged, this,
            [this](quint64 hz) { onActiveVfoFrequencyChanged(Vfo::Main, hz); });
    connect(m_window->m_subVfoController, &VfoController::frequencyChanged, this,
            [this](quint64 hz) { onActiveVfoFrequencyChanged(Vfo::Sub, hz); });
    const auto markRecenterPending = [this](Vfo vfo, quint64 hz)
    {
        if (vfo == Vfo::Main)
        {
            m_pendingMainRecenterHz = hz;
        }
        else
        {
            m_pendingSubRecenterHz = hz;
        }
    };
    connect(m_window->m_mainVfoController, &VfoController::frequencyRecenterRequested, this, markRecenterPending);
    connect(m_window->m_subVfoController, &VfoController::frequencyRecenterRequested, this, markRecenterPending);
    if (auto* backend = m_window->m_model ? m_window->m_model->backend() : nullptr)
    {
        connect(backend, &IRadioBackend::readyChanged, this,
                [this](bool ready)
                {
                    if (!ready)
                    {
                        m_hasCenteredActiveVfo = false;
                        m_pendingMainRecenterHz = 0;
                        m_pendingSubRecenterHz = 0;
                    }
                });
    }
    auto createSeparator = [vfoBlock]()
    {
        auto* separator = new QWidget(vfoBlock);
        separator->setFixedWidth(1);
        separator->setStyleSheet(QStringLiteral("background: %1;").arg(UiTheme::Color::Border));
        return separator;
    };
    vfoBlockLayout->addWidget(m_window->m_mainVfoController->display(), 1);
    vfoBlockLayout->addWidget(createSeparator());
    vfoBlockLayout->addWidget(m_window->m_vfoSelectionController->panel());
    vfoBlockLayout->addWidget(createSeparator());
    vfoBlockLayout->addWidget(m_window->m_subVfoController->display(), 1);
    vfoLayout->addWidget(vfoBlock, 1);
    // Nineteen empty layout pixels plus the first border pixel produce a 20 px
    // visible edge-to-edge separation below the title bar.
    vbox->addSpacing(19);
    vbox->addWidget(vfoStrip);
    // The VFO strip already contributes a 6 px bottom inset. Add 14 px here
    // so the 20 px top gap matches the spectrum frame's side and bottom insets.
    vbox->addSpacing(14);

    m_window->m_spectrumScopeDisplay = new SpectrumScopeDisplay(m_window->centralWidget());
    m_window->m_spectrumScopeDisplay->setInvertMouseWheel(
        AppSettings::instance()
            .value(QString::fromLatin1(kSpectrumScopeInvertMouseWheelSettingsKey), "False")
            .toBool());
    m_window->m_spectrumScopeDisplay->setVfoMarkerColor(
        colorSetting(kSpectrumScopeCenterLineColorSettingsKey, kDefaultSpectrumScopeCenterLineColor));
    m_window->m_spectrumScopeDisplay->setBackgroundColor(
        colorSetting(kSpectrumScopeBackgroundColorSettingsKey, kDefaultSpectrumScopeBackgroundColor));
    m_window->m_spectrumScopeDisplay->setGridLineColor(
        colorSetting(kSpectrumScopeGridLineColorSettingsKey, kDefaultSpectrumScopeGridLineColor));
    m_window->m_spectrumScopeDisplay->setGridDensity(spectrumScopeGridDensitySetting());

    QVector<SpectrumScopeDisplay::SpanChoice> spanChoices;
    spanChoices.reserve(static_cast<int>(std::size(kSpectrumScopeSpanPresets)));
    for (const SpectrumScopeSpanPreset& preset : kSpectrumScopeSpanPresets)
    {
        spanChoices.append({preset.hz, QString::fromLatin1(preset.label)});
    }
    m_window->m_spectrumScopeDisplay->setSpanChoices(spanChoices);

    const quint64 initialSpectrumScopeSpanHz = AppSettings::instance()
                                                   .value(QString::fromLatin1(kSpectrumScopeSpanHZSettingsKey),
                                                          QVariant::fromValue<qulonglong>(kDefaultSpectrumScopeSpanHZ))
                                                   .toULongLong();
    m_window->m_spectrumScopeDisplay->setCurrentSpanHz(initialSpectrumScopeSpanHz);
    const double initialSpectrumScopeCenterMhz = activeVfoFrequencyHz() / 1e6;
    // IC-9700 center-scope span choices are half-spans ("+/-500 kHz"). The
    // display range is full width, so expand the stored/radio half-span here.
    const double initialSpectrumScopeBandwidthMhz = (initialSpectrumScopeSpanHz * 2.0) / 1e6;
    m_window->m_spectrumScopeDisplay->setFrequencyRange(
        initialSpectrumScopeCenterMhz - initialSpectrumScopeBandwidthMhz / 2.0,
        initialSpectrumScopeCenterMhz + initialSpectrumScopeBandwidthMhz / 2.0);
    m_window->m_spectrumScopeDisplay->setDataFrequencyRange(
        initialSpectrumScopeCenterMhz - initialSpectrumScopeBandwidthMhz / 2.0,
        initialSpectrumScopeCenterMhz + initialSpectrumScopeBandwidthMhz / 2.0);
    connect(m_window->m_spectrumScopeDisplay, &SpectrumScopeDisplay::spanSelected, this,
            [this](quint64 hz)
            {
                AppSettings::instance().setValue(QString::fromLatin1(kSpectrumScopeSpanHZSettingsKey),
                                                 QVariant::fromValue<qulonglong>(hz));
                applySpectrumScopeSettings();
            });
    connect(m_window->m_vfo, &VfoModel::filterChanged, this,
            [this](int low, int high) { m_window->m_spectrumScopeDisplay->setFilterWidth(low, high); });
    auto* spectrumInset = new QWidget(m_window->centralWidget());
    spectrumInset->setObjectName(QStringLiteral("spectrumInset"));
    auto* spectrumInsetLayout = new QHBoxLayout(spectrumInset);
    spectrumInsetLayout->setContentsMargins(kControlStripMargins.left(), 0, kControlStripMargins.right(), 0);
    spectrumInsetLayout->setSpacing(0);
    auto* spectrumFrame = new QWidget(spectrumInset);
    spectrumFrame->setObjectName(QStringLiteral("spectrumFrame"));
    spectrumFrame->setStyleSheet(
        QStringLiteral("QWidget#spectrumFrame { border: 1px solid %1; }").arg(UiTheme::Color::Border));
    auto* spectrumFrameLayout = new QVBoxLayout(spectrumFrame);
    spectrumFrameLayout->setContentsMargins(1, 1, 1, 1);
    spectrumFrameLayout->setSpacing(0);

    auto* spectrumToolbar = new QWidget(spectrumFrame);
    spectrumToolbar->setObjectName(QStringLiteral("spectrumToolbar"));
    spectrumToolbar->setFixedHeight(kSpectrumToolbarHeight);
    spectrumToolbar->setAccessibleName(QStringLiteral("Spectrum controls toolbar"));
    QPalette chromePalette = spectrumToolbar->palette();
    chromePalette.setColor(QPalette::Window, QColor(QString::fromLatin1(UiTheme::Color::WindowChrome)));
    spectrumToolbar->setPalette(chromePalette);
    spectrumToolbar->setAutoFillBackground(true);
    spectrumToolbar->setStyleSheet(
        QStringLiteral("QWidget#spectrumToolbar { background: %1; border: 0; border-bottom: 1px solid %2; }")
            .arg(UiTheme::Color::WindowChrome, UiTheme::Color::StatusBorder));
    auto* spectrumToolbarLayout = new QHBoxLayout(spectrumToolbar);
    spectrumToolbarLayout->setContentsMargins(8, 2, 8, 2);
    spectrumToolbarLayout->setSpacing(6);

    m_tuningStepSelector = new QComboBox(spectrumToolbar);
    m_tuningStepSelector->setObjectName(QStringLiteral("spectrumStepSelector"));
    m_tuningStepSelector->setAccessibleName(QStringLiteral("Tuning step"));
    m_tuningStepSelector->hide();
    for (const auto& preset : kStepPresets)
    {
        m_tuningStepSelector->addItem(QString::fromLatin1(preset.label), preset.hz);
    }
    updateTuningStepSelector(m_window->tuningStepHz());
    connect(m_tuningStepSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index)
            {
                if (index < 0 || !m_tuningStepSelector)
                {
                    return;
                }
                AppSettings::instance().setValue(QString::fromLatin1(kTuningStepHZSettingsKey),
                                                 m_tuningStepSelector->itemData(index).toInt());
                m_window->applyRadioTuningStep();
            });

    auto* spanSelector = m_window->m_spectrumScopeDisplay->spanSelector();
    spanSelector->setParent(spectrumToolbar);
    spanSelector->hide();

    const auto makeInlineSelector = [spectrumToolbar](const QString& name, QComboBox* selector)
    {
        auto* control = new QWidget(spectrumToolbar);
        control->setFixedHeight(22);
        auto* layout = new QHBoxLayout(control);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);

        auto* previous = new QToolButton(control);
        auto* label = new QLabel(name, control);
        auto* value = new QLabel(selector->currentText(), control);
        auto* next = new QToolButton(control);
        previous->setText(QStringLiteral("‹"));
        next->setText(QStringLiteral("›"));
        previous->setAccessibleName(QStringLiteral("Previous %1").arg(name.toLower()));
        next->setAccessibleName(QStringLiteral("Next %1").arg(name.toLower()));
        for (QToolButton* button : {previous, next})
        {
            button->setFixedSize(16, 22);
            button->setFocusPolicy(Qt::NoFocus);
            button->setStyleSheet(
                QStringLiteral("QToolButton { background: transparent; border: 0; color: %1; font-size: 14px; "
                               "font-weight: bold; padding: 0; } QToolButton:hover { color: %2; }")
                    .arg(UiTheme::Color::TextStatusSecondary, UiTheme::Color::TextBright));
        }
        label->setStyleSheet(
            QStringLiteral("color: %1; font-size: 10px; font-weight: bold;").arg(UiTheme::Color::TextStatusSecondary));
        label->setAlignment(Qt::AlignCenter);
        value->setMinimumWidth(52);
        value->setAlignment(Qt::AlignCenter);
        value->setStyleSheet(
            QStringLiteral("color: %1; font-size: 10px; font-weight: bold;").arg(UiTheme::Color::TextBright));

        const auto updateControl = [selector, value, previous, next]()
        {
            value->setText(selector->currentText());
            previous->setEnabled(selector->currentIndex() > 0);
            next->setEnabled(selector->currentIndex() + 1 < selector->count());
        };
        QObject::connect(previous, &QToolButton::clicked, selector,
                         [selector]() { selector->setCurrentIndex(selector->currentIndex() - 1); });
        QObject::connect(next, &QToolButton::clicked, selector,
                         [selector]() { selector->setCurrentIndex(selector->currentIndex() + 1); });
        QObject::connect(selector, &QComboBox::currentTextChanged, control,
                         [updateControl](const QString&) { updateControl(); });
        updateControl();

        layout->addWidget(previous);
        layout->addWidget(label);
        layout->addWidget(value);
        layout->addWidget(next);
        return control;
    };

    spectrumToolbarLayout->addWidget(makeInlineSelector(QStringLiteral("STEP"), m_tuningStepSelector), 0,
                                     Qt::AlignVCenter);
    spectrumToolbarLayout->addSpacing(50);
    spectrumToolbarLayout->addWidget(makeInlineSelector(QStringLiteral("SPAN"), spanSelector), 0, Qt::AlignVCenter);
    spectrumToolbarLayout->addStretch();
    spectrumFrameLayout->addWidget(spectrumToolbar);
    spectrumFrameLayout->addWidget(m_window->m_spectrumScopeDisplay);
    spectrumInsetLayout->addWidget(spectrumFrame);
    vbox->addWidget(spectrumInset, 1);
    vbox->addSpacing(20);

    m_window->m_spectrumScopeTuneCommitTimer = new QTimer(m_window);
    m_window->m_spectrumScopeTuneCommitTimer->setSingleShot(true);
    m_window->m_spectrumScopeTuneCommitTimer->setInterval(kSpectrumScopeTuneCommitDelayMs);
    connect(m_window->m_spectrumScopeTuneCommitTimer, &QTimer::timeout, this,
            [this]()
            {
                if (m_window->m_pendingSpectrumScopeTuneHz == 0 || !m_window->m_model->isReady() ||
                    m_window->m_controlsLocked)
                {
                    return;
                }
                m_window->leaveMemoryModeForManualChange();
                tuneActiveVfo(m_window->m_pendingSpectrumScopeTuneHz);
            });

    m_window->m_spectrumScopeTuneReleaseTimer = new QTimer(m_window);
    m_window->m_spectrumScopeTuneReleaseTimer->setSingleShot(true);
    m_window->m_spectrumScopeTuneReleaseTimer->setInterval(kSpectrumScopeTuneReleaseDelayMs);
    connect(m_window->m_spectrumScopeTuneReleaseTimer, &QTimer::timeout, this,
            [this]()
            {
                m_window->m_pendingSpectrumScopeTuneHz = 0;
                m_window->m_spectrumScopeDisplayCenterHz = 0;
                m_window->m_spectrumScopeFixedPanStartHz = 0;
                m_window->m_spectrumScopeFixedPanEndHz = 0;
                if (m_window->m_spectrumScope)
                {
                    m_window->m_spectrumScope->clearDisplayCenterHold();
                }
            });

    m_window->m_spectrumScopeDisplay->setFilterWidth(m_window->m_vfo->filterLow(), m_window->m_vfo->filterHigh());
    updateSpectrumVfoMarker();

    connect(m_window->m_spectrumScope, &SpectrumScopeModel::spectrumReady, this,
            &SpectrumScopeController::onSpectrumReady);
    connect(m_window->m_spectrumScope, &SpectrumScopeModel::rangeChanged, this,
            [this](double center, double bw)
            {
                m_window->m_spectrumScopeDisplayCenterHz = static_cast<quint64>(std::llround(center * 1e6));
                m_window->m_spectrumScopeDisplay->setFrequencyRange(center - bw / 2, center + bw / 2);
                updateSpectrumVfoMarker();
            });

    connect(m_window->m_spectrumScopeDisplay, &SpectrumScopeDisplay::frequencyClicked, this,
            &SpectrumScopeController::onSpectrumClicked);
    connect(m_window->m_spectrumScopeDisplay, &SpectrumScopeDisplay::wheelStepRequested, this,
            &SpectrumScopeController::onWheelStepRequested);
    connect(m_window->m_spectrumScopeDisplay, &SpectrumScopeDisplay::panCenterRequested, this, [this](double centerMhz)
            { panSpectrumScopeToCenter(static_cast<quint64>(std::llround(centerMhz * 1e6))); });
}

void SpectrumScopeController::updateSpectrumVfoMarker()
{
    if (!m_window->m_spectrumScopeDisplay || !m_window->m_vfo)
    {
        return;
    }

    const quint64 displayedHz = activeVfoFrequencyHz();
    m_window->m_spectrumScopeDisplay->setVfoFrequency(displayedHz / 1e6);
}

void SpectrumScopeController::updateTuningStepSelector(int tuningStepHz)
{
    m_tuningStepHz = tuningStepHz;
    if (!m_tuningStepSelector)
    {
        return;
    }
    const int index = m_tuningStepSelector->findData(m_tuningStepHz);
    if (index >= 0)
    {
        const QSignalBlocker blocker(m_tuningStepSelector);
        m_tuningStepSelector->setCurrentIndex(index);
    }
}

void SpectrumScopeController::updateSpectrumScopeBandLimits(quint64 hz)
{
    if (!m_window->m_spectrumScope)
    {
        return;
    }

    quint64 startHz = 0;
    quint64 endHz = 0;
    const availableBands band = sdr9700::radioBandForFrequency(hz);
    if (band == bandUnknown || !sdr9700::radioBandEdges(band, &startHz, &endHz))
    {
        if (!m_hasLastSpectrumScopeLimits)
        {
            return;
        }
        m_hasLastSpectrumScopeLimits = false;
        m_lastSpectrumScopeLimitStartHz = 0;
        m_lastSpectrumScopeLimitEndHz = 0;
        m_window->m_spectrumScope->clearFrequencyLimits();
        if (m_window->m_spectrumScopeDisplay)
        {
            m_window->m_spectrumScopeDisplay->clearFrequencyPanRange();
        }
        return;
    }

    if (m_hasLastSpectrumScopeLimits && m_lastSpectrumScopeLimitStartHz == startHz &&
        m_lastSpectrumScopeLimitEndHz == endHz)
    {
        return;
    }
    m_hasLastSpectrumScopeLimits = true;
    m_lastSpectrumScopeLimitStartHz = startHz;
    m_lastSpectrumScopeLimitEndHz = endHz;
    m_window->m_spectrumScope->setFrequencyLimits(startHz / 1e6, endHz / 1e6);
    if (m_window->m_spectrumScopeDisplay)
    {
        m_window->m_spectrumScopeDisplay->setFrequencyPanRange(startHz / 1e6, endHz / 1e6);
    }
}

void SpectrumScopeController::applySpectrumScopeSettings()
{
    if (!m_window->m_model || !m_window->m_model->isReady() || m_window->m_controlsLocked)
    {
        return;
    }

    auto* backend = m_window->m_model->backend();
    if (!backend)
    {
        return;
    }

    const quint64 spanHz = AppSettings::instance()
                               .value(QString::fromLatin1(kSpectrumScopeSpanHZSettingsKey),
                                      QVariant::fromValue<qulonglong>(kDefaultSpectrumScopeSpanHZ))
                               .toULongLong();

    backend->setScopeMode(0);
    backend->setScopeSpanHz(spanHz);
    m_window->m_spectrumScopeFixedPanStartHz = 0;
    m_window->m_spectrumScopeFixedPanEndHz = 0;
    if (m_window->m_spectrumScopeDisplay)
    {
        m_window->m_spectrumScopeDisplay->setCurrentSpanHz(spanHz);
    }
}

quint64 SpectrumScopeController::roundFrequencyToStep(quint64 hz) const
{
    return sdr9700::roundFrequencyToStep(hz, static_cast<quint64>(m_window->tuningStepHz()));
}

void SpectrumScopeController::panSpectrumScopeToCenter(quint64 centerHz)
{
    if (!m_window->m_spectrumScopeDisplay || !m_window->m_spectrumScope || !m_window->m_model->isReady() ||
        m_window->m_controlsLocked)
    {
        return;
    }

    centerHz = clampSpectrumScopeCenterHz(centerHz, m_window->m_spectrumScope->bandwidthMhz());
    const double bandwidthMhz = m_window->m_spectrumScope->bandwidthMhz();
    const quint64 bandwidthHz = static_cast<quint64>(std::llround(bandwidthMhz * 1e6));
    const quint64 startHz = centerHz - bandwidthHz / 2;
    const quint64 endHz = startHz + bandwidthHz;
    const double centerMhz = centerHz / 1e6;
    m_window->m_spectrumScopeDisplayCenterHz = centerHz;
    m_window->m_spectrumScopeDisplay->setFrequencyRange(centerMhz - bandwidthMhz / 2.0, centerMhz + bandwidthMhz / 2.0);
    if (auto* backend = m_window->m_model ? m_window->m_model->backend() : nullptr)
    {
        const auto changedEnough = [](quint64 current, quint64 previous)
        {
            return current > previous ? current - previous >= kSpectrumScopeFixedPanMinDeltaHz
                                      : previous - current >= kSpectrumScopeFixedPanMinDeltaHz;
        };
        if (m_window->m_spectrumScopeFixedPanStartHz == 0 || m_window->m_spectrumScopeFixedPanEndHz == 0 ||
            changedEnough(startHz, m_window->m_spectrumScopeFixedPanStartHz) ||
            changedEnough(endHz, m_window->m_spectrumScopeFixedPanEndHz))
        {
            backend->setScopeFixedRangeHz(startHz, endHz);
            m_window->m_spectrumScopeFixedPanStartHz = startHz;
            m_window->m_spectrumScopeFixedPanEndHz = endHz;
        }
    }
    updateSpectrumVfoMarker();
}

quint64 SpectrumScopeController::clampSpectrumScopeCenterHz(quint64 hz, double bandwidthMhz) const
{
    const quint64 referenceHz = activeVfoFrequencyHz();
    return sdr9700::clampScopeCenterToBand(hz, referenceHz, bandwidthMhz);
}

quint64 SpectrumScopeController::clampFrequencyHzToActiveBand(quint64 hz) const
{
    const quint64 referenceHz = activeVfoFrequencyHz();
    return sdr9700::clampFrequencyToBand(hz, referenceHz);
}

void SpectrumScopeController::scheduleSpectrumScopeTune(quint64 hz)
{
    scheduleSpectrumScopeTune(hz, true, false, false);
}

void SpectrumScopeController::scheduleSpectrumScopeTune(quint64 hz, bool snapToTuningStep, bool commitImmediately,
                                                        bool clearStaleDisplay)
{
    // Wheel and RC-28 tuning are step-based controls, but a mouse click on the
    // Spectrum Scope is an absolute frequency selection. Snapping clicks to the
    // current VFO step makes off-step signals appear to move away after the
    // display recenters, so callers choose the behavior explicitly. Clicks also
    // clear stale bins and commit immediately; otherwise a near-frequency click
    // can briefly show old scope data under the newly centered frequency scale.
    if (snapToTuningStep)
    {
        hz = roundFrequencyToStep(hz);
    }
    hz = clampFrequencyHzToActiveBand(hz);
    const quint64 displayCenterHz =
        clampSpectrumScopeCenterHz(hz, m_window->m_spectrumScope ? m_window->m_spectrumScope->bandwidthMhz() : 0.0);
    m_window->leaveMemoryModeForManualChange();
    m_window->m_pendingSpectrumScopeTuneHz = hz;
    updateSpectrumScopeBandLimits(hz);
    if (clearStaleDisplay && m_window->m_spectrumScopeDisplay)
    {
        m_window->m_spectrumScopeDisplay->clearDisplay();
    }
    if (m_window->m_spectrumScopeFixedPanStartHz > 0 || m_window->m_spectrumScopeFixedPanEndHz > 0)
    {
        if (auto* backend = m_window->m_model ? m_window->m_model->backend() : nullptr)
        {
            const quint64 spanHz = AppSettings::instance()
                                       .value(QString::fromLatin1(kSpectrumScopeSpanHZSettingsKey),
                                              QVariant::fromValue<qulonglong>(kDefaultSpectrumScopeSpanHZ))
                                       .toULongLong();
            backend->setScopeMode(0);
            backend->setScopeSpanHz(spanHz);
        }
        m_window->m_spectrumScopeFixedPanStartHz = 0;
        m_window->m_spectrumScopeFixedPanEndHz = 0;
    }
    if (m_window->m_spectrumScope)
    {
        m_window->m_spectrumScope->holdDisplayCenter(displayCenterHz / 1e6, hz / 1e6);
    }

    if (m_window->m_spectrumScope)
    {
        m_window->m_spectrumScope->centerOnFrequency(displayCenterHz / 1e6);
    }
    updateSpectrumVfoMarker();

    if (commitImmediately)
    {
        if (m_window->m_spectrumScopeTuneCommitTimer)
        {
            m_window->m_spectrumScopeTuneCommitTimer->stop();
        }
        if (m_window->m_pendingSpectrumScopeTuneHz != 0 && m_window->m_model->isReady() && !m_window->m_controlsLocked)
        {
            m_window->leaveMemoryModeForManualChange();
            tuneActiveVfo(m_window->m_pendingSpectrumScopeTuneHz);
        }
    }
    else if (m_window->m_spectrumScopeTuneCommitTimer)
    {
        m_window->m_spectrumScopeTuneCommitTimer->start();
    }
    if (m_window->m_spectrumScopeTuneReleaseTimer)
    {
        m_window->m_spectrumScopeTuneReleaseTimer->start();
    }
}

void SpectrumScopeController::onSpectrumReady(const QVector<float>& levels, double start, double end, bool outOfRange)
{
    const quint64 referenceHz = activeVfoFrequencyHz();
    if (referenceHz > 0)
    {
        updateSpectrumScopeBandLimits(referenceHz);
    }
    m_window->m_spectrumScopeDisplay->setDataFrequencyRange(start, end);
    updateSpectrumVfoMarker();
    m_window->m_spectrumScopeDisplay->updateSpectrum(levels, outOfRange);
}

void SpectrumScopeController::onSpectrumClicked(double freqMhz)
{
    if (!m_window->m_model->isReady() || m_window->m_controlsLocked)
    {
        return;
    }

    const quint64 targetHz = static_cast<quint64>(std::llround(freqMhz * 1e6));
    scheduleSpectrumScopeTune(targetHz, false, true, true);
}

void SpectrumScopeController::onWheelStepRequested(int steps)
{
    if (steps == 0 || !m_window->m_vfo || !m_window->m_model->isReady() || m_window->m_controlsLocked)
    {
        return;
    }

    const qint64 currentHz = static_cast<qint64>(
        m_window->m_pendingSpectrumScopeTuneHz > 0 ? m_window->m_pendingSpectrumScopeTuneHz : activeVfoFrequencyHz());
    const qint64 targetHz = currentHz + static_cast<qint64>(steps) * m_window->tuningStepHz();
    scheduleSpectrumScopeTune(
        static_cast<quint64>(std::max<qint64>(static_cast<qint64>(kMinimumTuneFrequencyHz), targetHz)));
}

quint64 SpectrumScopeController::activeVfoFrequencyHz() const
{
    if (!m_window->m_vfoSelectionController)
    {
        return m_window->m_vfoFrequencyHz > 0 ? m_window->m_vfoFrequencyHz
                                              : (m_window->m_vfo ? m_window->m_vfo->frequencyHz() : 0);
    }
    const VfoController* controller = m_window->m_vfoSelectionController->selectedVfo() == Vfo::Main
                                          ? m_window->m_mainVfoController
                                          : m_window->m_subVfoController;
    return controller ? controller->frequencyHz() : 0;
}

void SpectrumScopeController::onActiveVfoFrequencyChanged(Vfo vfo, quint64 hz)
{
    if (!m_window->m_vfoSelectionController || m_window->m_vfoSelectionController->selectedVfo() != vfo)
    {
        return;
    }

    quint64& pendingRecenterHz = vfo == Vfo::Main ? m_pendingMainRecenterHz : m_pendingSubRecenterHz;
    const bool requestedRecenter = pendingRecenterHz > 0 && pendingRecenterHz == hz;
    if (requestedRecenter)
    {
        pendingRecenterHz = 0;
    }

    if (!m_hasCenteredActiveVfo || requestedRecenter)
    {
        recenterActiveVfo(true);
        return;
    }

    updateSpectrumScopeBandLimits(hz);
    updateSpectrumVfoMarker();
}

void SpectrumScopeController::recenterActiveVfo(bool clearDisplay)
{
    const quint64 hz = activeVfoFrequencyHz();
    if (hz == 0 || !m_window->m_spectrumScope)
    {
        return;
    }
    m_window->m_pendingSpectrumScopeTuneHz = 0;
    m_window->m_spectrumScope->clearDisplayCenterHold();
    updateSpectrumScopeBandLimits(hz);
    if (clearDisplay && m_window->m_spectrumScopeDisplay)
    {
        m_window->m_spectrumScopeDisplay->clearDisplay();
    }
    m_window->m_spectrumScope->centerOnFrequency(hz / 1e6);
    updateSpectrumVfoMarker();
    m_hasCenteredActiveVfo = true;
}

void SpectrumScopeController::tuneActiveVfo(quint64 hz) const
{
    if (auto* backend = m_window->m_model ? m_window->m_model->backend() : nullptr)
    {
        const Vfo active =
            m_window->m_vfoSelectionController ? m_window->m_vfoSelectionController->selectedVfo() : Vfo::Main;
        backend->setVfoFrequencyHz(active, hz);
    }
}
