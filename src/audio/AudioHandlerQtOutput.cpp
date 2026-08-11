#include "AudioHandlerQtOutput.h"

bool AudioHandlerQtOutput::openDevice() noexcept
{
    audioOutput = new QAudioSink(deviceInfo, nativeFormat, this);
    connect(audioOutput, &QAudioSink::stateChanged, this, &AudioHandlerQtOutput::stateChanged);

    connect(converter, &AudioConverter::converted, this, &AudioHandlerQtOutput::onConverted);

    audioOutput->setBufferSize(nativeFormat.bytesForDuration(setupData.latency * 1000));

    audioDevice = audioOutput->start();
    if (!audioDevice)
    {
        delete audioOutput;
        audioOutput = nullptr;
        return false;
    }

    // Pre-fill half the buffer with silence so ALSA has data to pull
    // before the first real audio packet arrives from the network.
    {
        const int prefillBytes = audioOutput->bufferSize() / 2;
        // Unsigned 8-bit PCM is centered at 0x80; all other supported Qt
        // formats represent silence with zero bits.
        const char silenceByte = nativeFormat.sampleFormat() == QAudioFormat::UInt8 ? char(0x80) : '\0';
        QByteArray silence(prefillBytes, silenceByte);
        audioDevice->write(silence.constData(), silence.size());
    }

    qInfo(logAudio()).noquote() << "Connected to Qt audio output device" << deviceInfo.description();
    return true;
}

void AudioHandlerQtOutput::closeDevice() noexcept
{
    if (audioOutput)
    {
        if (audioOutput->state() != QAudio::StoppedState)
        {
            audioOutput->stop();
        }
        // dispose() marshals closeDevice() to this object's audio thread.
        // Destroy the native sink there before UdpAudio stops that thread;
        // deleteLater() can otherwise strand CoreAudio teardown on a stopped
        // event loop during a live device change.
        delete audioOutput;
        audioOutput = nullptr;
    }
    audioDevice = nullptr;
    m_pendingOutput.clear();
    m_pendingOutputOffset = 0;
}

void AudioHandlerQtOutput::incomingAudio(audioPacket packet)
{
    if (!audioDevice || packet.data.isEmpty())
    {
        return;
    }
    packet.volume = volume;
    queueForConversion(std::move(packet));
}

void AudioHandlerQtOutput::onConverted(const audioPacket& audio)
{
    if (!audioOutput || !audioDevice || audio.data.isEmpty())
    {
        return;
    }
    const qint64 packetAgeMs = audio.createdAtMs > 0 ? audioMonotonicTimestampMs() - audio.createdAtMs : 0;
    if (packetAgeMs > setupData.latency * 1.5)
    {
        return;
    }
    writeToOutputDevice(audio.data, audio.seq, audio.amplitudePeak, audio.amplitudeRMS);
}

void AudioHandlerQtOutput::writeToOutputDevice(const QByteArray& data, quint32 seq, float amplitudePeak,
                                               float amplitudeRms)
{
    Q_UNUSED(seq);
    if (!audioOutput || !audioDevice)
    {
        return;
    }

    // Recover from underrun: re-prime the buffer with silence so the device
    // has a cushion before real audio resumes, preventing click cascades.
    if (isUnderrun.load(std::memory_order_relaxed))
    {
        const int prefillBytes = audioOutput->bufferSize() / 2;
        const int freeBytes = static_cast<int>(audioOutput->bytesFree());
        const int silenceBytes = qMin(prefillBytes, freeBytes);
        if (silenceBytes > 0)
        {
            const char silenceByte = nativeFormat.sampleFormat() == QAudioFormat::UInt8 ? char(0x80) : '\0';
            QByteArray silence(silenceBytes, silenceByte);
            audioDevice->write(silence.constData(), silence.size());
        }
        isUnderrun.store(false, std::memory_order_relaxed);
        if (underTimer && underTimer->isActive())
        {
            underTimer->stop();
        }
    }

    qint64 buffered = audioOutput->bufferSize() - audioOutput->bytesFree();
    int devLatencyMs = static_cast<int>(nativeFormat.durationForBytes(buffered) / 1000);
    int pipelineMs = lastReceived.isValid() ? static_cast<int>(lastReceived.elapsed()) : 0;
    int newLatency = pipelineMs + devLatencyMs;
    int prev = currentLatency.load(std::memory_order_relaxed);
    currentLatency.store(static_cast<int>(prev * 0.8 + newLatency * 0.2), std::memory_order_relaxed);

    if (m_pendingOutputOffset > 0)
    {
        m_pendingOutput.remove(0, m_pendingOutputOffset);
        m_pendingOutputOffset = 0;
    }
    m_pendingOutput.append(data);

    // QAudioSink's push device may accept only part of a frame. Retain that
    // tail for the next 20 ms callback instead of discarding it. Cap the local
    // backlog at two sink buffers so a blocked device cannot grow memory or
    // turn a transient stall into seconds of delayed playback.
    const qsizetype maxPendingBytes = qMax<qsizetype>(audioOutput->bufferSize() * 2, data.size());
    if (m_pendingOutput.size() > maxPendingBytes)
    {
        qsizetype removeBytes = m_pendingOutput.size() - maxPendingBytes;
        const int bytesPerFrame = qMax(1, nativeFormat.bytesPerFrame());
        removeBytes = ((removeBytes + bytesPerFrame - 1) / bytesPerFrame) * bytesPerFrame;
        m_pendingOutput.remove(0, qMin(removeBytes, m_pendingOutput.size()));
        isOverrun.store(true, std::memory_order_relaxed);
    }
    drainPendingOutput();

    lastReceived.restart();
    amplitude.store(amplitudePeak, std::memory_order_relaxed);
    emit haveLevels(this->amplitudePeak(), static_cast<quint16>(amplitudeRms * 255.0f), setupData.latency,
                    currentLatency.load(), isUnderrun.load(), isOverrun.load());
}

void AudioHandlerQtOutput::drainPendingOutput()
{
    if (!audioOutput || !audioDevice)
    {
        return;
    }

    while (m_pendingOutputOffset < m_pendingOutput.size())
    {
        const qint64 freeBytes = audioOutput->bytesFree();
        if (freeBytes <= 0)
        {
            return;
        }
        const qint64 remaining = m_pendingOutput.size() - m_pendingOutputOffset;
        const qint64 requested = qMin(freeBytes, remaining);
        const qint64 written = audioDevice->write(m_pendingOutput.constData() + m_pendingOutputOffset, requested);
        if (written <= 0)
        {
            return;
        }
        m_pendingOutputOffset += written;
    }

    m_pendingOutput.clear();
    m_pendingOutputOffset = 0;
    isOverrun.store(false, std::memory_order_relaxed);
}

QAudioFormat AudioHandlerQtOutput::getNativeFormat()
{
    return setupData.port.preferredFormat();
}

bool AudioHandlerQtOutput::isFormatSupported(QAudioFormat f)
{
    return setupData.port.isFormatSupported(f);
}
