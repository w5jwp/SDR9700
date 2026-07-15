#include "RadioModel.h"
#include "VfoModel.h"
#include "PanadapterModel.h"
#include "backend/RadioBackend.h"

#include <QDebug>

namespace
{
constexpr double kPanCenterMarginFraction = 0.25;
}

RadioModel::RadioModel(QObject* parent) : QObject(parent)
{
    m_backend = new RadioBackend(this);
    m_vfo = new VfoModel(m_backend, this);
    m_pan = new PanadapterModel(this);

    connect(m_backend, &IRadioBackend::connected, this, &RadioModel::onBackendConnected);
    connect(m_backend, &IRadioBackend::disconnected, this, &RadioModel::onBackendDisconnected);
    connect(m_backend, &IRadioBackend::readyChanged, this, &RadioModel::onBackendReadyChanged);
    connect(m_backend, &IRadioBackend::errorOccurred, this, &RadioModel::onBackendError);
    connect(m_backend, &IRadioBackend::statusMessage, this, &RadioModel::statusMessage);
    connect(m_backend, &IRadioBackend::frequencyChanged, this, &RadioModel::onFrequencyChanged);
    connect(m_backend, &IRadioBackend::modeChanged, this, &RadioModel::onModeChanged);
    connect(m_backend, &IRadioBackend::smeterChanged, this, &RadioModel::onSmeterChanged);
    connect(m_backend, &IRadioBackend::swrChanged, this, &RadioModel::onSwrChanged);
    connect(m_backend, &IRadioBackend::alcChanged, this, &RadioModel::onAlcChanged);
    connect(m_backend, &IRadioBackend::agcModeChanged, m_vfo, &VfoModel::applyAgcMode);
    connect(m_backend, &IRadioBackend::autoNotchChanged, m_vfo, &VfoModel::applyAutoNotch);
    connect(m_backend, &IRadioBackend::manualNotchChanged, m_vfo, &VfoModel::applyManualNotch);
    connect(m_backend, &IRadioBackend::compressorChanged, m_vfo, &VfoModel::applyCompressor);
    connect(m_backend, &IRadioBackend::ritEnabledChanged, m_vfo, &VfoModel::applyRitEnabled);
    connect(m_backend, &IRadioBackend::ritOffsetChanged, m_vfo, &VfoModel::applyRitOffset);
    connect(m_backend, &IRadioBackend::nrChanged, m_vfo, &VfoModel::applyNrEnabled);
    connect(m_backend, &IRadioBackend::nbChanged, m_vfo, &VfoModel::applyNbEnabled);
    connect(m_backend, &IRadioBackend::preampChanged, m_vfo, &VfoModel::applyPreampEnabled);
    connect(m_backend, &IRadioBackend::preampLevelChanged, m_vfo, &VfoModel::applyPreampLevel);
    connect(m_backend, &IRadioBackend::attenuatorChanged, m_vfo, &VfoModel::applyAttenuatorEnabled);
    connect(m_backend, &IRadioBackend::rfGainChanged, m_vfo, &VfoModel::applyRfGain);
    connect(m_backend, &IRadioBackend::squelchChanged, m_vfo, &VfoModel::applySquelch);
    connect(m_backend, &IRadioBackend::txPowerChanged, m_vfo, &VfoModel::applyTxPower);
    connect(m_backend, &IRadioBackend::duplexModeChanged, m_vfo, &VfoModel::applyDuplexMode);
    connect(m_backend, &IRadioBackend::repeaterOffsetChanged, m_vfo, &VfoModel::applyRepeaterOffsetHz);
    connect(m_backend, &IRadioBackend::toneAccessModeChanged, m_vfo, &VfoModel::applyToneAccessMode);
    connect(m_backend, &IRadioBackend::toneFrequencyChanged, m_vfo, &VfoModel::applyToneFrequency);
    connect(m_backend, &IRadioBackend::dtcsCodeChanged, m_vfo, &VfoModel::applyDtcsCode);
    connect(m_backend, &IRadioBackend::spectrumDataReady, this, &RadioModel::onSpectrumDataReady);
    connect(m_backend, &IRadioBackend::pttChanged, this, &RadioModel::onPttChanged);
    connect(m_backend, &IRadioBackend::networkQualityChanged, this, &RadioModel::networkQualityChanged);
    connect(m_backend, &IRadioBackend::txAudioLevelChanged, this, &RadioModel::txAudioLevelChanged);
}

RadioModel::~RadioModel() = default;

void RadioModel::connectToRadio(const QString& host, quint16 port, const QString& user, const QString& pass)
{
    m_backend->connectToRadio(host, port, user, pass);
}

void RadioModel::disconnectFromRadio()
{
    m_backend->disconnectFromRadio();
}

void RadioModel::setRxAudioDevice(const QAudioDevice& dev)
{
    m_backend->setRxAudioDevice(dev);
}

void RadioModel::setTxAudioDevice(const QAudioDevice& dev)
{
    m_backend->setTxAudioDevice(dev);
}

void RadioModel::setLanModLevel(int level)
{
    m_backend->setLanModLevel(level);
}

void RadioModel::setTuningStep(int step)
{
    m_backend->setTuningStep(step);
}

void RadioModel::onBackendConnected()
{
    m_connected = true;
    m_ready = false;
    emit connectionChanged(true);
    emit readyChanged(false);
    // Status message is already emitted by RadioBackend::onLanReady(); no duplicate here.
}

void RadioModel::onBackendDisconnected()
{
    m_connected = false;
    m_ready = false;
    emit connectionChanged(false);
    emit readyChanged(false);
    onPttChanged(false);
}

void RadioModel::onBackendError(const QString& msg)
{
    emit errorOccurred(msg);
    if (m_connected)
    {
        m_connected = false;
        m_ready = false;
        emit connectionChanged(false);
        emit readyChanged(false);
    }
}

void RadioModel::onBackendReadyChanged(bool ready)
{
    if (m_ready == ready)
    {
        return;
    }

    m_ready = ready;
    emit readyChanged(ready);
}

void RadioModel::onFrequencyChanged(quint64 hz)
{
    m_vfo->applyFrequency(hz);
    if (m_pan->isUserZoomed())
    {
        return;
    }
    double freqMhz = hz / 1e6;
    double start = m_pan->startMhz();
    double end = m_pan->endMhz();
    double margin = m_pan->bandwidthMhz() * kPanCenterMarginFraction;
    if (freqMhz < start + margin || freqMhz > end - margin)
    {
        m_pan->centerOnFrequency(freqMhz);
    }
}

void RadioModel::onModeChanged(const QString& mode)
{
    m_vfo->applyMode(mode);
}

void RadioModel::onSmeterChanged(int s)
{
    m_smeter = s;
    emit smeterChanged(s);
}

void RadioModel::onSwrChanged(double swr)
{
    emit swrChanged(swr);
}

void RadioModel::onAlcChanged(double alc)
{
    emit alcChanged(alc);
}

void RadioModel::onPttChanged(bool on)
{
    m_vfo->applyPtt(on);
    if (m_transmitting == on)
    {
        return;
    }

    m_transmitting = on;
    emit transmittingChanged(on);
    emit pttChanged(on);
}

void RadioModel::onSpectrumDataReady(const QVector<float>& bins, double start, double end)
{
    if (!m_ready)
    {
        return;
    }
    m_pan->ingestSpectrum(bins, start, end);
}
