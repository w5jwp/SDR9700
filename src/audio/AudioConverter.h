#pragma once

#include <QObject>
#include <QByteArray>
#include <QTime>
#include <QMap>
#include <QDebug>
#include <QAudioFormat>

#include <QMediaDevices>
#include <QAudioDevice>
#include <QAudioSource>
#include <QAudioSink>

#include "opus/opus.h"
#include <Eigen/Eigen>
#include <Eigen/Dense>

#include "Types.h"

struct SpeexResamplerState_;
using SpeexResamplerState = struct SpeexResamplerState_;

#include "PacketTypes.h"

struct audioPacket
{
    quint32 seq = 0;
    QTime time;
    quint16 sent = 0;
    QByteArray data;
    quint8 guid[GUIDLEN]{};
    float amplitudePeak = 0.0f;
    float amplitudeRMS = 0.0f;
    qreal volume = 1.0;
};

struct audioSetup
{
    audioType type;
    QString name;
    quint16 latency;
    quint8 codec;
    bool ulaw = false;
    bool isinput;
    quint32 sampleRate;
    QAudioDevice port;
    int portInt{0};
    quint8 resampleQuality;
    quint8 localAFgain;
    quint16 blockSize = 20; // milliseconds
    quint8 guid[GUIDLEN];
};

class AudioConverter : public QObject
{
    Q_OBJECT

  public:
    explicit AudioConverter(QObject* parent = nullptr);
    ~AudioConverter();

  public slots:
    bool init(QAudioFormat inFormat, codecType inCodec, QAudioFormat outFormat, codecType outCodec,
              quint8 opusComplexity, quint8 resampleQuality);
    bool convert(audioPacket audio);

  signals:
    void converted(audioPacket audio);
    void floatAudio(Eigen::VectorXf audio);
    void initFailed(QString message);

  protected:
    QAudioFormat inFormat;
    QAudioFormat outFormat;
    OpusEncoder* opusEncoder = nullptr;
    OpusDecoder* opusDecoder = nullptr;
    SpeexResamplerState* resampler = nullptr;
    quint8 opusComplexity{0};
    quint8 resampleQuality = 4;
    double resampleRatio = 1.0;
    quint32 lastAudioSequence{0};
    codecType inCodec;
    codecType outCodec;
    bool initialized = false;
    QByteArray scratchIn;
    QByteArray scratchOut;
    Eigen::VectorXf scratchF;

    void releaseCodecState();
};

using VectorXuint8 = Eigen::Matrix<quint8, Eigen::Dynamic, 1>;
using VectorXint8 = Eigen::Matrix<qint8, Eigen::Dynamic, 1>;
using VectorXint16 = Eigen::Matrix<qint16, Eigen::Dynamic, 1>;
using VectorXint32 = Eigen::Matrix<qint32, Eigen::Dynamic, 1>;

// Eigen vectors cross thread boundaries in queued Qt signal delivery.
Q_DECLARE_METATYPE(Eigen::VectorXf)

static inline QAudioFormat toQAudioFormat(quint8 codec, quint32 sampleRate)
{
    QAudioFormat format;

    // IC-9700 LAN codec ids:
    // 0x01 uLaw 1ch 8bit, 0x02 PCM 1ch 8bit, 0x04 PCM 1ch 16bit
    // 0x08 PCM 2ch 8bit, 0x10 PCM 2ch 16bit, 0x20 uLaw 2ch 8bit
    // 0x40 Opus 1ch, 0x41 Opus 2ch
    format.setSampleRate(sampleRate);

    if (codec == 0x01 || codec == 0x20)
    {
        format.setSampleFormat(QAudioFormat::Float);
    }

    if (codec == 0x02 || codec == 0x08)
    {
        format.setSampleFormat(QAudioFormat::UInt8);
    }
    if (codec == 0x08 || codec == 0x10 || codec == 0x20)
    {
        format.setChannelCount(2);
    }
    else
    {
        format.setChannelCount(1);
    }

    if (codec == 0x04 || codec == 0x10)
    {
        format.setSampleFormat(QAudioFormat::Int16);
    }

    if (codec == 0x40 || codec == 0x41)
    {
        format.setSampleFormat(QAudioFormat::Float);
    }
    return format;
}
