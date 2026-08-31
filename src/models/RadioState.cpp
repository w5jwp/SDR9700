#include "RadioState.h"

#include "backend/IRadioBackend.h"

namespace sdr9700
{

namespace
{
Vfo vfoForReceiver(uchar receiver)
{
    return receiver == 1 ? Vfo::Sub : Vfo::Main;
}
} // namespace

RadioState::RadioState(IRadioBackend* backend, QObject* parent) : QObject(parent)
{
    if (!backend)
    {
        return;
    }

    connect(backend, &IRadioBackend::radioValueUpdated, this, &RadioState::applyRadioValue);
    connect(backend, &IRadioBackend::readyChanged, this, &RadioState::setReady);
    connect(backend, &IRadioBackend::pttChanged, this, &RadioState::setTransmitting);
    connect(backend, &IRadioBackend::disconnected, this, &RadioState::invalidateSession);
}

const RadioState::Receiver& RadioState::receiver(Vfo vfo) const
{
    return m_receivers.at(receiverIndex(vfo));
}

const RadioState::BandRecall* RadioState::bandRecall(Vfo vfo, availableBands band) const
{
    const int bandIndex = radioBandUiIndex(band);
    if (bandIndex < 0)
    {
        return nullptr;
    }
    return &m_bandRecall.at(receiverIndex(vfo)).at(static_cast<std::size_t>(bandIndex));
}

std::size_t RadioState::receiverIndex(Vfo vfo)
{
    return vfo == Vfo::Sub ? 1U : 0U;
}

RadioState::BandRecall* RadioState::mutableBandRecall(Vfo vfo, availableBands band)
{
    const int bandIndex = radioBandUiIndex(band);
    if (bandIndex < 0)
    {
        return nullptr;
    }
    return &m_bandRecall.at(receiverIndex(vfo)).at(static_cast<std::size_t>(bandIndex));
}

void RadioState::applyRadioValue(Funcs func, const QVariant& value, uchar receiverId)
{
    if (receiverId > 1)
    {
        return;
    }

    const Vfo vfo = vfoForReceiver(receiverId);
    Receiver& state = m_receivers.at(receiverIndex(vfo));
    BandRecall* recall = mutableBandRecall(vfo, state.band);
    bool receiverChanged = false;
    bool recallChanged = false;

    switch (func)
    {
    case funcFreqGet:
    case funcFreqSet:
    case funcSelectedFreq:
    case funcUnselectedFreq:
    {
        const quint64 hz = value.value<Frequency>().Hz;
        const availableBands reportedBand = radioBandForFrequency(hz);
        if (hz == 0 || reportedBand == bandUnknown)
        {
            return;
        }
        if (state.band != reportedBand)
        {
            // A frequency crossing is the only source-confirmed indication
            // that this logical receiver entered another amateur band. Clear
            // the receiver-specific operating values so replies from the old
            // band cannot be copied into the new band's recall record.
            state = Receiver{};
            state.band = reportedBand;
        }
        state.frequencyHz = hz;
        recall = mutableBandRecall(vfo, reportedBand);
        recall->frequencyHz = hz;
        receiverChanged = true;
        recallChanged = true;
        break;
    }
    case funcModeGet:
    case funcModeSet:
    case funcSelectedMode:
    case funcUnselectedMode:
    {
        const ModeInfo info = value.value<ModeInfo>();
        const QString mode = info.name.trimmed().toUpper();
        if (mode.isEmpty())
        {
            return;
        }
        state.mode = mode;
        state.filter = info.filter;
        if (recall)
        {
            recall->mode = mode;
            recall->filter = info.filter;
            recallChanged = true;
        }
        receiverChanged = true;
        break;
    }
    case funcSplitStatus:
        state.duplexMode = value.value<duplexMode_t>();
        if (recall)
        {
            recall->duplexMode = *state.duplexMode;
            recallChanged = true;
        }
        receiverChanged = true;
        break;
    case funcReadFreqOffset:
        state.repeaterOffsetHz = value.value<Frequency>().Hz;
        if (recall)
        {
            recall->repeaterOffsetHz = *state.repeaterOffsetHz;
            recallChanged = true;
        }
        receiverChanged = true;
        break;
    case funcToneSquelchType:
        state.toneAccessMode = value.value<RptrAccessData>().accessMode;
        if (recall)
        {
            recall->toneAccessMode = *state.toneAccessMode;
            recallChanged = true;
        }
        receiverChanged = true;
        break;
    case funcToneFreq:
        state.toneFrequency = value.value<ToneInfo>().tone;
        if (recall)
        {
            recall->toneFrequency = *state.toneFrequency;
            recallChanged = true;
        }
        receiverChanged = true;
        break;
    case funcTSQLFreq:
        state.toneSquelchFrequency = value.value<ToneInfo>().tone;
        if (recall)
        {
            recall->toneSquelchFrequency = *state.toneSquelchFrequency;
            recallChanged = true;
        }
        receiverChanged = true;
        break;
    case funcDTCSCode:
        state.dtcsCode = value.value<ToneInfo>().tone;
        if (recall)
        {
            recall->dtcsCode = *state.dtcsCode;
            recallChanged = true;
        }
        receiverChanged = true;
        break;
    case funcVFOBandMS:
        m_shared.selectedVfo = value.toBool() ? Vfo::Sub : Vfo::Main;
        emit sharedStateChanged();
        return;
    case funcVFODualWatch:
        m_shared.dualWatchEnabled = value.toBool();
        emit sharedStateChanged();
        return;
    default:
        return;
    }

    if (receiverChanged)
    {
        emit receiverStateChanged(vfo);
    }
    if (recallChanged)
    {
        emit bandRecallChanged(vfo, state.band);
    }
}

void RadioState::setReady(bool ready)
{
    if (m_shared.ready == ready)
    {
        return;
    }
    m_shared.ready = ready;
    if (!ready)
    {
        invalidateReceiver(Vfo::Main);
        invalidateReceiver(Vfo::Sub);
        m_shared.selectedVfo.reset();
        m_shared.dualWatchEnabled.reset();
    }
    emit sharedStateChanged();
}

void RadioState::setTransmitting(bool transmitting)
{
    if (m_shared.transmitting == transmitting)
    {
        return;
    }
    m_shared.transmitting = transmitting;
    emit sharedStateChanged();
}

void RadioState::invalidateReceiver(Vfo vfo)
{
    m_receivers.at(receiverIndex(vfo)) = Receiver{};
    emit receiverStateChanged(vfo);
}

void RadioState::invalidateSession()
{
    m_shared = Shared{};
    m_bandRecall = {};
    invalidateReceiver(Vfo::Main);
    invalidateReceiver(Vfo::Sub);
    emit sharedStateChanged();
}

} // namespace sdr9700
