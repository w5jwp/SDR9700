#include "AudioHandlerQtInput.h"

bool AudioHandlerQtInput::openDevice() noexcept
{
    audioInput = new QAudioSource(deviceInfo, nativeFormat, this);
    connect(audioInput, &QAudioSource::stateChanged, this, &AudioHandlerQtInput::stateChanged);

    emit setupConverter(nativeFormat, codecType::LPCM, radioFormat, codec, 7, setupData.resampleQuality);

    connect(this, &AudioHandlerBase::sendToConverter, converter, &AudioConverter::convert);
    connect(converter, &AudioConverter::converted, this, &AudioHandlerQtInput::onConverted);

    audioInput->setBufferSize(nativeFormat.bytesForDuration(setupData.latency * 1000));

    audioDevice = audioInput->start();
    if (!audioDevice)
    {
        audioInput->deleteLater();
        audioInput = nullptr;
        return false;
    }

    connect(audioInput, &QAudioSource::destroyed, audioDevice, &QObject::deleteLater, Qt::UniqueConnection);
    connect(audioDevice, &QIODevice::readyRead, this, &AudioHandlerQtInput::onReadyRead, Qt::UniqueConnection);
    qInfo(logAudio()) << "Connected to Qt audio input device" << deviceInfo.description();
    return true;
}

void AudioHandlerQtInput::closeDevice() noexcept
{
    if (audioInput)
    {
        if (audioInput->state() != QAudio::StoppedState)
        {
            audioInput->stop();
        }
        audioInput->deleteLater();
        audioInput = nullptr;
    }
    audioDevice = nullptr;
}

void AudioHandlerQtInput::onReadyRead()
{
    if (!audioDevice)
    {
        return;
    }
    tempBuf.data.append(audioDevice->readAll());

    const int bytesPerBlock = nativeFormat.bytesForDuration(setupData.blockSize * 1000);
    while (tempBuf.data.size() >= bytesPerBlock)
    {
        audioPacket pkt;
        pkt.time = QTime::currentTime();
        pkt.sent = 0;
        pkt.volume = volume;
        memcpy(&pkt.guid, setupData.guid, GUIDLEN);
        pkt.data = tempBuf.data.left(bytesPerBlock);
        tempBuf.data.remove(0, bytesPerBlock);
        emit sendToConverter(pkt);
    }
}

void AudioHandlerQtInput::onConverted(audioPacket audio)
{
    if (audio.data.isEmpty())
    {
        return;
    }

    emit haveAudioData(audio);

    if (lastReceived.isValid() && lastReceived.elapsed() > 100)
    {
        qDebug(logAudio()) << role() << "Time since last audio packet" << lastReceived.elapsed() << "Expected around"
                           << setupData.blockSize;
    }
    if (!lastReceived.isValid())
    {
        lastReceived.start();
    }
    else
    {
        lastReceived.restart();
    }

    amplitude.store(audio.amplitudePeak, std::memory_order_relaxed);
    emit haveLevels(amplitudePeak(), static_cast<quint16>(audio.amplitudeRMS * 255.0f), setupData.latency,
                    static_cast<quint16>(latencyMs()), isUnderrun.load(), isOverrun.load());
}

QAudioFormat AudioHandlerQtInput::getNativeFormat()
{
    return setupData.port.preferredFormat();
}

bool AudioHandlerQtInput::isFormatSupported(QAudioFormat f)
{
    return setupData.port.isFormatSupported(f);
}
