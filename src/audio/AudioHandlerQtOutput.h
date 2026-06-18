#pragma once
#include "AudioHandlerBase.h"

class AudioHandlerQtOutput : public AudioHandlerBase
{
    Q_OBJECT

  public:
    explicit AudioHandlerQtOutput(QObject* parent = nullptr) : AudioHandlerBase(parent) {}
    ~AudioHandlerQtOutput() override { dispose(); }
    QString role() const override { return QStringLiteral("Output"); }

  public slots:
    void incomingAudio(audioPacket packet) override;

  protected:
    bool openDevice() noexcept override;
    void closeDevice() noexcept override;
    QAudioFormat getNativeFormat() override;
    bool isFormatSupported(QAudioFormat f) override;

  private:
    void writeToOutputDevice(const QByteArray& data, quint32 seq, float amplitudePeak, float amplitudeRms);

    QAudioSink* audioOutput{nullptr};

    QIODevice* audioDevice{nullptr};

  private slots:
    void onConverted(audioPacket audio);
};
