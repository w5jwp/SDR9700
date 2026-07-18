#pragma once
#include "AudioHandlerBase.h"

class AudioHandlerQtInput : public AudioHandlerBase
{
    Q_OBJECT

  public:
    explicit AudioHandlerQtInput(QObject* parent = nullptr) : AudioHandlerBase(parent) {}
    ~AudioHandlerQtInput() override { dispose(); }
    QString role() const override { return QStringLiteral("Input"); }

  protected:
    bool openDevice() noexcept override;
    void closeDevice() noexcept override;
    QAudioFormat getNativeFormat() override;
    bool isFormatSupported(QAudioFormat f) override;

  private:
    QAudioSource* audioInput{nullptr};

    QIODevice* audioDevice{nullptr};
    qsizetype m_bufferReadOffset{0};

  private slots:
    void onReadyRead();
    void onConverted(const audioPacket& audio);
};
