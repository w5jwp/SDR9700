#include "AudioHandlerQtOutput.h"

bool AudioHandlerQtOutput::openDevice() noexcept
{
    audioOutput = new QAudioSink(deviceInfo, nativeFormat, this);
    connect(audioOutput, &QAudioSink::stateChanged, this, &AudioHandlerQtOutput::stateChanged);

    emit setupConverter(radioFormat, codec, nativeFormat, codecType::LPCM, 7, setupData.resampleQuality);

    connect(this, &AudioHandlerBase::sendToConverter, converter, &AudioConverter::convert);
    connect(converter, &AudioConverter::converted, this, &AudioHandlerQtOutput::onConverted);

    audioOutput->setBufferSize(nativeFormat.bytesForDuration(setupData.latency * 1000));

    audioDevice = audioOutput->start();
    if (!audioDevice)
    {
        audioOutput->deleteLater();
        audioOutput = nullptr;
        return false;
    }

    // Pre-fill half the buffer with silence so ALSA has data to pull
    // before the first real audio packet arrives from the network.
    {
        const int prefillBytes = audioOutput->bufferSize() / 2;
        QByteArray silence(prefillBytes, '\0');
        audioDevice->write(silence.constData(), silence.size());
    }

    connect(audioOutput, &QAudioSink::destroyed, audioDevice, &QObject::deleteLater, Qt::UniqueConnection);
    qInfo(logAudio()) << "Connected to Qt audio output device" << deviceInfo.description();
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
        audioOutput->deleteLater();
        audioOutput = nullptr;
    }
    audioDevice = nullptr;
}

void AudioHandlerQtOutput::incomingAudio(audioPacket packet)
{
    if (!audioDevice || packet.data.isEmpty())
    {
        return;
    }
    packet.volume = volume;
    emit sendToConverter(packet);
}

void AudioHandlerQtOutput::onConverted(audioPacket audio)
{
    if (!audioOutput || !audioDevice || audio.data.isEmpty())
    {
        return;
    }
    const int nowToPktMs = audio.time.msecsTo(QTime::currentTime());
    // msecsTo() wraps at midnight: a negative result means pkt.time is later
    // in the day than currentTime, which happens when the packet was stamped
    // just after midnight and is processed just before. Treat that as recent.
    if (nowToPktMs > 0 && nowToPktMs > setupData.latency * 1.5)
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
            QByteArray silence(silenceBytes, '\0');
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

    qint64 toWrite = data.size();
    const char* p = data.constData();
    while (toWrite > 0)
    {
        qint64 written = audioDevice->write(p, toWrite);
        if (written <= 0)
        {
            break;
        }
        p += written;
        toWrite -= written;
    }

    lastReceived.restart();
    amplitude.store(amplitudePeak, std::memory_order_relaxed);
    emit haveLevels(this->amplitudePeak(), static_cast<quint16>(amplitudeRms * 255.0f), setupData.latency,
                    currentLatency.load(), isUnderrun.load(), isOverrun.load());
}

QAudioFormat AudioHandlerQtOutput::getNativeFormat()
{
    return setupData.port.preferredFormat();
}

bool AudioHandlerQtOutput::isFormatSupported(QAudioFormat f)
{
    return setupData.port.isFormatSupported(f);
}
