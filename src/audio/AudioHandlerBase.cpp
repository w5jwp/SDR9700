#include "AudioHandlerBase.h"

#include <QSemaphore>
#include <memory>

namespace
{
constexpr int kShutdownWaitMs = 500;

qreal localAudioGainFromSlider(quint8 volumeIdx)
{
    const qreal normalized = static_cast<qreal>(volumeIdx) / 255.0;
    return normalized * normalized * normalized;
}
} // namespace

AudioHandlerBase::AudioHandlerBase(QObject* parent) : QObject(parent) {}

AudioHandlerBase::~AudioHandlerBase()
{
    // Safety net: if dispose() was never called, stop the converter thread
    // now so QThread::~QThread() won't abort.
    stopConverterThread(QStringLiteral("Audio"));
}

void AudioHandlerBase::stopConverterThread(const QString& roleName)
{
    if (!converterThread)
    {
        converter = nullptr;
        return;
    }

    qDebug(logAudio()) << "[SHUTDOWN] converterThread->quit(), role=" << roleName;
    converterThread->quit();
    if (!converterThread->wait(kShutdownWaitMs))
    {
        qWarning(logAudio()) << "[SHUTDOWN] converterThread did not stop within" << kShutdownWaitMs
                             << "ms; requesting interruption, role=" << roleName;
        converterThread->requestInterruption();
        converterThread->quit();
        if (!converterThread->wait(kShutdownWaitMs))
        {
            qCritical(logAudio()) << "[SHUTDOWN] converterThread did not stop after bounded shutdown, role="
                                  << roleName;
            converterThread->setParent(nullptr);
            connect(converterThread, &QThread::finished, converterThread, &QObject::deleteLater);
            converterThread = nullptr;
            converter = nullptr;
            return;
        }
    }
    qDebug(logAudio()) << "[SHUTDOWN] converterThread done, role=" << roleName;
    delete converterThread;
    converterThread = nullptr;
    converter = nullptr;
}

void AudioHandlerBase::dispose()
{
    qDebug(logAudio()) << "[SHUTDOWN] dispose() enter, role=" << role()
                       << "onCorrectThread=" << (QThread::currentThread() == thread());
    // Run disposal on this object's thread to avoid races with audio callbacks.
    if (QThread::currentThread() != thread())
    {
        qDebug(logAudio()) << "[SHUTDOWN] dispose() marshaling to audio thread ...";
        auto disposeDone = std::make_shared<QSemaphore>();
        const bool queued = QMetaObject::invokeMethod(
            this,
            [this, disposeDone]()
            {
                dispose();
                disposeDone->release();
            },
            Qt::QueuedConnection);
        if (!queued || !disposeDone->tryAcquire(1, kShutdownWaitMs))
        {
            qWarning(logAudio()) << "[SHUTDOWN] dispose() did not finish within" << kShutdownWaitMs
                                 << "ms, role=" << role();
        }
        return;
    }

    bool expected = false;
    if (!disposed.compare_exchange_strong(expected, true))
    {
        qDebug(logAudio()) << "[SHUTDOWN] dispose() already disposed, returning";
        return;
    }

    // Stop the converter thread before taking devMutex: the converter thread
    // may need devMutex to process its final work, so joining while holding
    // the lock would deadlock.
    if (converterThread)
    {
        disconnect(this, &AudioHandlerBase::setupConverter, converter, &AudioConverter::init);
        disconnect(this, &AudioHandlerBase::sendToConverter, converter, &AudioConverter::convert);
        disconnect(converter, &AudioConverter::converted, nullptr, nullptr);
        stopConverterThread(role());
    }

    qDebug(logAudio()) << "[SHUTDOWN] dispose() locking devMutex, role=" << role();
    QMutexLocker lock(&devMutex);
    qDebug(logAudio()) << "[SHUTDOWN] dispose() calling closeDevice(), role=" << role();
    closeDevice();
    qDebug(logAudio()) << "[SHUTDOWN] dispose() closeDevice() done, role=" << role();

    if (underTimer)
    {
        underTimer->stop();
        underTimer->deleteLater();
        underTimer = nullptr;
    }

    qDebug(logAudio()) << "[SHUTDOWN] dispose() complete, role=" << role();
}

void AudioHandlerBase::reportError(const QString& msg)
{
    qCritical(logAudio()).noquote() << role() << msg;
    emit audioMessage(QString("%1: %2").arg(role(), msg));
}

bool AudioHandlerBase::negotiateFormat(int minSampleRate)
{
    nativeFormat = getNativeFormat();

    if (nativeFormat.channelCount() < 1)
    {
        reportError("Cannot initialize audio device, no channels found");
        return false;
    }

    if (nativeFormat.channelCount() > 2)
    {
        nativeFormat.setChannelCount(2);
    }

    if (nativeFormat.channelCount() == 1 && radioFormat.channelCount() == 2)
    {
        nativeFormat.setChannelCount(2);

        if (!isFormatSupported(nativeFormat))
        {
            nativeFormat.setChannelCount(1);
        }
    }

    if (nativeFormat.sampleRate() < minSampleRate)
    {
        const int prev = nativeFormat.sampleRate();
        nativeFormat.setSampleRate(minSampleRate);
        if (!isFormatSupported(nativeFormat))
        {
            nativeFormat.setSampleRate(prev);
        }
    }

    if (nativeFormat.sampleFormat() == QAudioFormat::UInt8)
    {
        nativeFormat.setSampleFormat(QAudioFormat::Int16);
        if (!isFormatSupported(nativeFormat))
        {
            nativeFormat.setSampleFormat(QAudioFormat::UInt8);
        }
    }
    // ALSA/Qt can return Unknown sample format; default to Int16.
    if (nativeFormat.sampleFormat() == QAudioFormat::Unknown)
    {
        nativeFormat.setSampleFormat(QAudioFormat::Int16);
        if (!isFormatSupported(nativeFormat))
        {
            nativeFormat.setSampleFormat(QAudioFormat::Float);
        }
    }

    qDebug(logAudio()) << role() << "Selected format: ch=" << nativeFormat.channelCount()
                       << " rate=" << nativeFormat.sampleRate() << " fmt=" << nativeFormat.sampleFormat();
    return true;
}

bool AudioHandlerBase::init(const audioSetup& setup)
{
    QMutexLocker lock(&devMutex);
    if (initialized)
    {
        return true;
    }

    setupData = setup;
    radioFormat = toQAudioFormat(setup.codec, setup.sampleRate);

    codec = codecType::LPCM;
    if (setup.codec == 0x01 || setup.codec == 0x20)
    {
        codec = codecType::PCMU;
    }
    else if (setup.codec == 0x40 || setup.codec == 0x41)
    {
        codec = codecType::OPUS;
    }
    if (setup.port.isNull() && setup.port.description().isEmpty() && setup.portInt == -1)
    {
        reportError("Audio device is NULL, check device selection in settings");
        emit initFailed();
        return false;
    }
    else
    {
        deviceInfo = setup.port;
    }

    if (!negotiateFormat(48000))
    {
        emit initFailed();
        return false;
    }

    converter = new AudioConverter();
    converterThread = new QThread(this);
    converterThread->setObjectName(role() == "Input" ? "audioConvIn()" : "audioConvOut()");
    converter->moveToThread(converterThread);

    connect(this, &AudioHandlerBase::setupConverter, converter, &AudioConverter::init);
    connect(converterThread, &QThread::finished, converter, &QObject::deleteLater);
    connect(converter, &AudioConverter::initFailed, this,
            [this](const QString& message)
            {
                reportError(message);
                emit initFailed();
            });

    converterThread->start(QThread::TimeCriticalPriority);

    underTimer = new QTimer(this);
    underTimer->setSingleShot(true);
    connect(underTimer, &QTimer::timeout, this, &AudioHandlerBase::clearUnderrun);

    setVolume(setup.localAFgain);

    if (!openDevice())
    {
        reportError("Failed to open device");
        closeDevice();
        if (underTimer)
        {
            underTimer->stop();
            underTimer->deleteLater();
            underTimer = nullptr;
        }
        if (converterThread)
        {
            stopConverterThread(role());
        }
        emit initFailed();
        return false;
    }

    initialized = true;
    qInfo(logAudio()) << role() << "thread id" << QThread::currentThreadId();
    return true;
}

void AudioHandlerBase::start()
{
    QMutexLocker lock(&devMutex);
}

void AudioHandlerBase::stop()
{
    QMutexLocker lock(&devMutex);
    closeDevice();
}

void AudioHandlerBase::setVolume(quint8 volumeIdx)
{
    volume = localAudioGainFromSlider(volumeIdx);
}

void AudioHandlerBase::changeLatency(quint16 newLatencyMs)
{
    QMutexLocker lock(&devMutex);
    setupData.latency = newLatencyMs;
}

void AudioHandlerBase::stateChanged(QAudio::State state)
{
    switch (state)
    {
    case QAudio::IdleState:
        isUnderrun.store(true, std::memory_order_relaxed);
        if (underTimer->isActive())
        {
            underTimer->stop();
        }
        break;
    case QAudio::ActiveState:
        if (!underTimer->isActive())
        {
            underTimer->start(500);
        }
        break;
    default:
        break;
    }
}

void AudioHandlerBase::clearUnderrun()
{
    isUnderrun.store(false, std::memory_order_relaxed);
}
