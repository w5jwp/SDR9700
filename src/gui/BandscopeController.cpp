#include "BandscopeController.h"

#include "AppSettings.h"
#include "BandscopeDisplay.h"
#include "LogCategories.h"
#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "VfoPanel.h"
#include "backend/IRadioBackend.h"
#include "models/BandscopeModel.h"
#include "models/RadioModel.h"
#include "models/VfoModel.h"

#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <cmath>

using namespace sdr9700::ui::main_window;

#define m_bandscopeDisplay m_window->m_bandscopeDisplay
#define m_vfo m_window->m_vfo
#define m_vfoFrequencyHz m_window->m_vfoFrequencyHz
#define m_displayBandscopeTuneHz m_window->m_displayBandscopeTuneHz
#define m_bandscope m_window->m_bandscope
#define m_model m_window->m_model
#define m_controlsLocked m_window->m_controlsLocked
#define m_bandscopeDisplayCenterHz m_window->m_bandscopeDisplayCenterHz
#define m_bandscopeFixedPanStartHz m_window->m_bandscopeFixedPanStartHz
#define m_bandscopeFixedPanEndHz m_window->m_bandscopeFixedPanEndHz
#define m_bandscopeTuneCommitTimer m_window->m_bandscopeTuneCommitTimer
#define m_bandscopeTuneReleaseTimer m_window->m_bandscopeTuneReleaseTimer
#define m_pendingBandscopeTuneHz m_window->m_pendingBandscopeTuneHz
#define m_vfoPanel m_window->m_vfoPanel
#define clearActiveMemory m_window->clearActiveMemory
#define tuningStepHz m_window->tuningStepHz

BandscopeController::BandscopeController(MainWindow* window) : QObject(window), m_window(window) {}

void BandscopeController::buildBandscope(QVBoxLayout* vbox)
{
    auto* bandscopeDivider = new QWidget(m_window->centralWidget());
    bandscopeDivider->setObjectName(QStringLiteral("bandscopeDivider"));
    bandscopeDivider->setFixedHeight(6);
    bandscopeDivider->setStyleSheet(
        QStringLiteral("QWidget#bandscopeDivider { background: %1; }").arg(UiTheme::Color::PanelDark));
    vbox->addWidget(bandscopeDivider);

    m_bandscopeDisplay = new BandscopeDisplay(m_window->centralWidget());
    m_bandscopeDisplay->setInvertMouseWheel(
        AppSettings::instance().value(QString::fromLatin1(kBandScopeInvertMouseWheelSettingsKey), "False").toBool());
    m_bandscopeDisplay->setVfoMarkerColor(
        colorSetting(kBandScopeCenterLineColorSettingsKey, kDefaultBandscopeCenterLineColor));
    m_bandscopeDisplay->setBackgroundColor(
        colorSetting(kBandScopeBackgroundColorSettingsKey, kDefaultBandscopeBackgroundColor));
    m_bandscopeDisplay->setGridLineColor(
        colorSetting(kBandScopeGridLineColorSettingsKey, kDefaultBandscopeGridLineColor));
    m_bandscopeDisplay->setGridDensity(bandscopeGridDensitySetting());

    QVector<BandscopeDisplay::SpanChoice> spanChoices;
    spanChoices.reserve(static_cast<int>(std::size(kBandscopeSpanPresets)));
    for (const BandscopeSpanPreset& preset : kBandscopeSpanPresets)
    {
        spanChoices.append({preset.hz, QString::fromLatin1(preset.label)});
    }
    m_bandscopeDisplay->setSpanChoices(spanChoices);

    const quint64 initialBandscopeSpanHz = AppSettings::instance()
                                               .value(QString::fromLatin1(kBandScopeSpanHZSettingsKey),
                                                      QVariant::fromValue<qulonglong>(kDefaultBandScopeSpanHZ))
                                               .toULongLong();
    m_bandscopeDisplay->setCurrentSpanHz(initialBandscopeSpanHz);
    const double initialBandscopeCenterMhz = m_vfo->frequencyHz() / 1e6;
    const double initialBandscopeBandwidthMhz = initialBandscopeSpanHz / 1e6;
    m_bandscopeDisplay->setFrequencyRange(initialBandscopeCenterMhz - initialBandscopeBandwidthMhz / 2.0,
                                          initialBandscopeCenterMhz + initialBandscopeBandwidthMhz / 2.0);
    m_bandscopeDisplay->setDataFrequencyRange(initialBandscopeCenterMhz - initialBandscopeBandwidthMhz / 2.0,
                                              initialBandscopeCenterMhz + initialBandscopeBandwidthMhz / 2.0);
    connect(m_bandscopeDisplay, &BandscopeDisplay::spanSelected, this,
            [this](quint64 hz)
            {
                AppSettings::instance().setValue(QString::fromLatin1(kBandScopeSpanHZSettingsKey),
                                                 QVariant::fromValue<qulonglong>(hz));
                applyBandscopeSettings();
            });
    connect(m_vfo, &VfoModel::filterChanged, this,
            [this](int low, int high) { m_bandscopeDisplay->setFilterWidth(low, high); });
    vbox->addWidget(m_bandscopeDisplay, 1);

    m_bandscopeTuneCommitTimer = new QTimer(m_window);
    m_bandscopeTuneCommitTimer->setSingleShot(true);
    m_bandscopeTuneCommitTimer->setInterval(kBandscopeTuneCommitDelayMs);
    connect(m_bandscopeTuneCommitTimer, &QTimer::timeout, this,
            [this]()
            {
                if (m_pendingBandscopeTuneHz == 0 || !m_model->isReady() || m_controlsLocked)
                {
                    return;
                }
                m_vfo->setFrequencyHz(m_pendingBandscopeTuneHz);
            });

    m_bandscopeTuneReleaseTimer = new QTimer(m_window);
    m_bandscopeTuneReleaseTimer->setSingleShot(true);
    m_bandscopeTuneReleaseTimer->setInterval(kBandscopeTuneReleaseDelayMs);
    connect(m_bandscopeTuneReleaseTimer, &QTimer::timeout, this,
            [this]()
            {
                m_pendingBandscopeTuneHz = 0;
                m_displayBandscopeTuneHz = 0;
                m_bandscopeDisplayCenterHz = 0;
                m_bandscopeFixedPanStartHz = 0;
                m_bandscopeFixedPanEndHz = 0;
                if (m_bandscope)
                {
                    m_bandscope->clearDisplayCenterHold();
                }
            });

    m_bandscopeDisplay->setFilterWidth(m_vfo->filterLow(), m_vfo->filterHigh());
    updateSpectrumVfoMarker();

    connect(m_bandscope, &BandscopeModel::spectrumReady, m_window, &MainWindow::onSpectrumReady);
    connect(m_bandscope, &BandscopeModel::rangeChanged, this,
            [this](double center, double bw)
            {
                m_bandscopeDisplayCenterHz = static_cast<quint64>(std::llround(center * 1e6));
                m_bandscopeDisplay->setFrequencyRange(center - bw / 2, center + bw / 2);
                updateSpectrumVfoMarker();
            });

    connect(m_bandscopeDisplay, &BandscopeDisplay::frequencyClicked, m_window, &MainWindow::onSpectrumClicked);
    connect(m_bandscopeDisplay, &BandscopeDisplay::wheelStepRequested, this,
            &BandscopeController::onWheelStepRequested);
    connect(m_bandscopeDisplay, &BandscopeDisplay::panCenterRequested, this,
            [this](double centerMhz) { panBandscopeToCenter(static_cast<quint64>(std::llround(centerMhz * 1e6))); });
}

void BandscopeController::updateSpectrumVfoMarker()
{
    if (!m_bandscopeDisplay || !m_vfo)
    {
        return;
    }

    const quint64 displayedHz = m_displayBandscopeTuneHz > 0
                                    ? m_displayBandscopeTuneHz
                                    : (m_vfoFrequencyHz > 0 ? m_vfoFrequencyHz : m_vfo->frequencyHz());
    m_bandscopeDisplay->setVfoFrequency(displayedHz / 1e6);
}

void BandscopeController::updateBandscopeBandLimits(quint64 hz)
{
    if (!m_bandscope)
    {
        return;
    }

    quint64 startHz = 0;
    quint64 endHz = 0;
    const availableBands band = sdr9700::radioBandForFrequency(hz);
    if (band == bandUnknown || !sdr9700::radioBandEdges(band, &startHz, &endHz))
    {
        m_bandscope->clearFrequencyLimits();
        if (m_bandscopeDisplay)
        {
            m_bandscopeDisplay->clearFrequencyPanRange();
        }
        return;
    }

    m_bandscope->setFrequencyLimits(startHz / 1e6, endHz / 1e6);
    if (m_bandscopeDisplay)
    {
        m_bandscopeDisplay->setFrequencyPanRange(startHz / 1e6, endHz / 1e6);
    }
}

void BandscopeController::applyBandscopeSettings()
{
    if (!m_model || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    auto* backend = m_model->backend();
    if (!backend)
    {
        return;
    }

    const quint64 spanHz = AppSettings::instance()
                               .value(QString::fromLatin1(kBandScopeSpanHZSettingsKey),
                                      QVariant::fromValue<qulonglong>(kDefaultBandScopeSpanHZ))
                               .toULongLong();

    backend->setScopeMode(0);
    backend->setScopeSpanHz(spanHz);
    m_bandscopeFixedPanStartHz = 0;
    m_bandscopeFixedPanEndHz = 0;
    if (m_bandscopeDisplay)
    {
        m_bandscopeDisplay->setCurrentSpanHz(spanHz);
    }
}

quint64 BandscopeController::roundFrequencyToStep(quint64 hz) const
{
    const quint64 stepHz = static_cast<quint64>(tuningStepHz());
    if (stepHz <= 1)
    {
        return hz;
    }
    return ((hz + stepHz / 2) / stepHz) * stepHz;
}

void BandscopeController::panBandscopeToCenter(quint64 centerHz)
{
    if (!m_bandscopeDisplay || !m_bandscope || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    centerHz = clampBandscopeCenterHz(centerHz, m_bandscope->bandwidthMhz());
    const double bandwidthMhz = m_bandscope->bandwidthMhz();
    const quint64 bandwidthHz = static_cast<quint64>(std::llround(bandwidthMhz * 1e6));
    const quint64 startHz = centerHz - bandwidthHz / 2;
    const quint64 endHz = startHz + bandwidthHz;
    const double centerMhz = centerHz / 1e6;
    m_bandscopeDisplayCenterHz = centerHz;
    m_bandscopeDisplay->setFrequencyRange(centerMhz - bandwidthMhz / 2.0, centerMhz + bandwidthMhz / 2.0);
    if (auto* backend = m_model ? m_model->backend() : nullptr)
    {
        const auto changedEnough = [](quint64 current, quint64 previous)
        {
            return current > previous ? current - previous >= kBandscopeFixedPanMinDeltaHz
                                      : previous - current >= kBandscopeFixedPanMinDeltaHz;
        };
        if (m_bandscopeFixedPanStartHz == 0 || m_bandscopeFixedPanEndHz == 0 ||
            changedEnough(startHz, m_bandscopeFixedPanStartHz) || changedEnough(endHz, m_bandscopeFixedPanEndHz))
        {
            backend->setScopeFixedRangeHz(startHz, endHz);
            m_bandscopeFixedPanStartHz = startHz;
            m_bandscopeFixedPanEndHz = endHz;
        }
    }
    updateSpectrumVfoMarker();
}

quint64 BandscopeController::clampBandscopeCenterHz(quint64 hz, double bandwidthMhz) const
{
    const quint64 referenceHz = m_vfoFrequencyHz > 0 ? m_vfoFrequencyHz : (m_vfo ? m_vfo->frequencyHz() : 0);
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

quint64 BandscopeController::clampFrequencyHzToActiveBand(quint64 hz) const
{
    const quint64 referenceHz = m_vfoFrequencyHz > 0 ? m_vfoFrequencyHz : (m_vfo ? m_vfo->frequencyHz() : 0);
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

void BandscopeController::scheduleBandscopeTune(quint64 hz)
{
    hz = clampFrequencyHzToActiveBand(roundFrequencyToStep(hz));
    const quint64 displayCenterHz = clampBandscopeCenterHz(hz, m_bandscope ? m_bandscope->bandwidthMhz() : 0.0);
    clearActiveMemory();
    m_pendingBandscopeTuneHz = hz;
    m_displayBandscopeTuneHz = hz;
    m_vfoFrequencyHz = hz;
    updateBandscopeBandLimits(hz);
    if (m_bandscopeFixedPanStartHz > 0 || m_bandscopeFixedPanEndHz > 0)
    {
        if (auto* backend = m_model ? m_model->backend() : nullptr)
        {
            const quint64 spanHz = AppSettings::instance()
                                       .value(QString::fromLatin1(kBandScopeSpanHZSettingsKey),
                                              QVariant::fromValue<qulonglong>(kDefaultBandScopeSpanHZ))
                                       .toULongLong();
            backend->setScopeMode(0);
            backend->setScopeSpanHz(spanHz);
        }
        m_bandscopeFixedPanStartHz = 0;
        m_bandscopeFixedPanEndHz = 0;
    }
    if (m_bandscope)
    {
        m_bandscope->holdDisplayCenter(displayCenterHz / 1e6);
    }

    if (m_vfoPanel && !m_vfoPanel->frequencyHasFocus())
    {
        m_vfoPanel->setFrequencyText(formatFrequency(hz));
        m_vfoPanel->setBandText(bandLabelForHz(hz));
    }
    if (m_bandscope)
    {
        m_bandscope->centerOnFrequency(displayCenterHz / 1e6);
    }
    updateSpectrumVfoMarker();

    if (m_bandscopeTuneCommitTimer)
    {
        m_bandscopeTuneCommitTimer->start();
    }
    if (m_bandscopeTuneReleaseTimer)
    {
        m_bandscopeTuneReleaseTimer->start();
    }
}

void BandscopeController::onSpectrumReady(const QVector<float>& levels, double start, double end, bool outOfRange)
{
    const quint64 referenceHz = m_vfoFrequencyHz > 0 ? m_vfoFrequencyHz : (m_vfo ? m_vfo->frequencyHz() : 0);
    if (referenceHz > 0)
    {
        updateBandscopeBandLimits(referenceHz);
    }
    m_bandscopeDisplay->setDataFrequencyRange(start, end);
    updateSpectrumVfoMarker();
    m_bandscopeDisplay->updateSpectrum(levels, outOfRange);
}

void BandscopeController::onSpectrumClicked(double freqMhz)
{
    if (!m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    clearActiveMemory();
    scheduleBandscopeTune(clampFrequencyHzToActiveBand(static_cast<quint64>(std::llround(freqMhz * 1e6))));
}

void BandscopeController::onWheelStepRequested(int steps)
{
    if (steps == 0 || !m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    const qint64 currentHz =
        static_cast<qint64>(m_displayBandscopeTuneHz > 0 ? m_displayBandscopeTuneHz : m_vfo->frequencyHz());
    const qint64 targetHz = currentHz + static_cast<qint64>(steps) * tuningStepHz();
    scheduleBandscopeTune(
        static_cast<quint64>(std::max<qint64>(static_cast<qint64>(kMinimumTuneFrequencyHz), targetHz)));
}
