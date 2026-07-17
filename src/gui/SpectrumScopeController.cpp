#include "SpectrumScopeController.h"

#include "AppSettings.h"
#include "SpectrumScopeDisplay.h"
#include "LogCategories.h"
#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "VfoPanel.h"
#include "backend/IRadioBackend.h"
#include "models/SpectrumScopeModel.h"
#include "models/RadioModel.h"
#include "models/VfoModel.h"

#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <cmath>

using namespace sdr9700::ui::main_window;

SpectrumScopeController::SpectrumScopeController(MainWindow* window) : QObject(window), m_window(window) {}

void SpectrumScopeController::buildSpectrumScope(QVBoxLayout* vbox)
{
    auto* spectrumScopeDivider = new QWidget(m_window->centralWidget());
    spectrumScopeDivider->setObjectName(QStringLiteral("spectrumScopeDivider"));
    spectrumScopeDivider->setFixedHeight(6);
    spectrumScopeDivider->setStyleSheet(
        QStringLiteral("QWidget#spectrumScopeDivider { background: %1; }").arg(UiTheme::Color::PanelDark));
    vbox->addWidget(spectrumScopeDivider);

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
    const double initialSpectrumScopeCenterMhz = m_window->m_vfo->frequencyHz() / 1e6;
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
    vbox->addWidget(m_window->m_spectrumScopeDisplay, 1);

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
                m_window->leaveMemoryModeForManualFrequencyChange();
                m_window->m_vfo->setFrequencyHz(m_window->m_pendingSpectrumScopeTuneHz);
            });

    m_window->m_spectrumScopeTuneReleaseTimer = new QTimer(m_window);
    m_window->m_spectrumScopeTuneReleaseTimer->setSingleShot(true);
    m_window->m_spectrumScopeTuneReleaseTimer->setInterval(kSpectrumScopeTuneReleaseDelayMs);
    connect(m_window->m_spectrumScopeTuneReleaseTimer, &QTimer::timeout, this,
            [this]()
            {
                m_window->m_pendingSpectrumScopeTuneHz = 0;
                m_window->m_displaySpectrumScopeTuneHz = 0;
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

    const quint64 displayedHz =
        m_window->m_displaySpectrumScopeTuneHz > 0
            ? m_window->m_displaySpectrumScopeTuneHz
            : (m_window->m_vfoFrequencyHz > 0 ? m_window->m_vfoFrequencyHz : m_window->m_vfo->frequencyHz());
    m_window->m_spectrumScopeDisplay->setVfoFrequency(displayedHz / 1e6);
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
    const quint64 stepHz = static_cast<quint64>(m_window->tuningStepHz());
    if (stepHz <= 1)
    {
        return hz;
    }
    return ((hz + stepHz / 2) / stepHz) * stepHz;
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
    const quint64 referenceHz = m_window->m_vfoFrequencyHz > 0 ? m_window->m_vfoFrequencyHz
                                                               : (m_window->m_vfo ? m_window->m_vfo->frequencyHz() : 0);
    availableBands band = sdr9700::radioBandForFrequency(referenceHz);
    if (band == bandUnknown)
    {
        band = sdr9700::radioBandForFrequency(hz);
    }

    quint64 startHz = 0;
    quint64 endHz = 0;
    if (band == bandUnknown || !sdr9700::radioBandEdges(band, &startHz, &endHz))
    {
        return hz;
    }

    const double startMhz = startHz / 1e6;
    const double endMhz = endHz / 1e6;
    const double halfBandwidthMhz = qMax(0.0, bandwidthMhz) / 2.0;
    const double minCenterMhz = startMhz + halfBandwidthMhz;
    const double maxCenterMhz = endMhz - halfBandwidthMhz;
    const double requestedMhz = hz / 1e6;
    const double clampedMhz =
        maxCenterMhz >= minCenterMhz ? qBound(minCenterMhz, requestedMhz, maxCenterMhz) : (startMhz + endMhz) / 2.0;
    return static_cast<quint64>(std::llround(clampedMhz * 1e6));
}

quint64 SpectrumScopeController::clampFrequencyHzToActiveBand(quint64 hz) const
{
    const quint64 referenceHz = m_window->m_vfoFrequencyHz > 0 ? m_window->m_vfoFrequencyHz
                                                               : (m_window->m_vfo ? m_window->m_vfo->frequencyHz() : 0);
    availableBands band = sdr9700::radioBandForFrequency(referenceHz);
    if (band == bandUnknown)
    {
        band = sdr9700::radioBandForFrequency(hz);
    }

    quint64 startHz = 0;
    quint64 endHz = 0;
    if (band == bandUnknown || !sdr9700::radioBandEdges(band, &startHz, &endHz))
    {
        return hz;
    }

    return std::clamp(hz, startHz, endHz);
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
    m_window->leaveMemoryModeForManualFrequencyChange();
    m_window->m_pendingSpectrumScopeTuneHz = hz;
    m_window->m_displaySpectrumScopeTuneHz = hz;
    m_window->m_vfoFrequencyHz = hz;
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

    if (m_window->m_vfoPanel && !m_window->m_vfoPanel->frequencyHasFocus())
    {
        m_window->m_vfoPanel->setFrequencyText(formatFrequency(hz));
        m_window->m_vfoPanel->setBandText(bandLabelForHz(hz));
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
            m_window->leaveMemoryModeForManualFrequencyChange();
            m_window->m_vfo->setFrequencyHz(m_window->m_pendingSpectrumScopeTuneHz);
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
    const quint64 referenceHz = m_window->m_vfoFrequencyHz > 0 ? m_window->m_vfoFrequencyHz
                                                               : (m_window->m_vfo ? m_window->m_vfo->frequencyHz() : 0);
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

    const qint64 currentHz =
        static_cast<qint64>(m_window->m_displaySpectrumScopeTuneHz > 0 ? m_window->m_displaySpectrumScopeTuneHz
                                                                       : m_window->m_vfo->frequencyHz());
    const qint64 targetHz = currentHz + static_cast<qint64>(steps) * m_window->tuningStepHz();
    scheduleSpectrumScopeTune(
        static_cast<quint64>(std::max<qint64>(static_cast<qint64>(kMinimumTuneFrequencyHz), targetHz)));
}
