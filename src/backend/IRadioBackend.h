#pragma once

#include <QObject>
#include <QVector>
#include <QString>
#include <QByteArray>
#include <QAudioDevice>
#include <QtGlobal>
#include "Types.h"

class IRadioBackend : public QObject
{
    Q_OBJECT

  public:
    explicit IRadioBackend(QObject* parent = nullptr) : QObject(parent) {}
    ~IRadioBackend() override = default;

  public slots:
    virtual void connectToRadio(const QString& host, quint16 port, const QString& user, const QString& pass) = 0;
    virtual void disconnectFromRadio() = 0;

    virtual void setFrequencyHz(quint64 hz) = 0;
    virtual void setMode(const QString& mode) = 0;
    virtual void setFilterWidth(int lowHz, int highHz) = 0;

    virtual void setNrEnabled(bool on) = 0;
    virtual void setNrLevel(int level) = 0; // 0-15
    virtual void setNbEnabled(bool on) = 0;
    virtual void setNbLevel(int level) = 0; // 0-10
    virtual void setPreampEnabled(bool on) = 0;
    virtual void setPreampLevel(int level) = 0;
    virtual void setAttenuatorEnabled(bool on) = 0;
    virtual void setAfGain(int level) = 0;            // 0-255
    virtual void setRfGain(int level) = 0;            // 0-255
    virtual void setSquelch(bool on, int level) = 0;
    virtual void setAgcMode(const QString& mode) = 0; // "fast","mid","slow"
    virtual void setAutoNotch(bool on) = 0;
    virtual void setManualNotch(bool on) = 0;
    virtual void setCompressor(bool on) = 0;
    virtual void setRitEnabled(bool on) = 0;
    virtual void setRitOffset(short hz) = 0;
    virtual void setDuplexMode(duplexMode_t mode) = 0;
    virtual void setRepeaterOffsetHz(quint64 hz) = 0;
    virtual void setToneAccessMode(rptAccessTxRx_t mode) = 0;
    virtual void setToneFrequency(ushort tone) = 0;
    virtual void setDtcsCode(ushort code) = 0;

    virtual void setScopeEnabled(bool on) = 0;
    virtual void setScopeSpanHz(quint64 hz) = 0;
    virtual void setScopeMode(int mode) = 0; // 0=center, 1=fixed

    virtual void setPtt(bool on) = 0;
    virtual void setTxPower(int level) = 0; // 0-255
    virtual void setTuningStep(int step) = 0;

    virtual void pollFrequency() = 0;

    virtual void setRxAudioDevice(const QAudioDevice& dev) { Q_UNUSED(dev) }
    virtual void setTxAudioDevice(const QAudioDevice& dev) { Q_UNUSED(dev) }
    virtual void setLanModLevel(int level) { Q_UNUSED(level) } // 0-255
    virtual void sendDtmf(const QString& digits) { Q_UNUSED(digits) }

  signals:
    void connected();
    void disconnected();
    void readyChanged(bool ready);
    void errorOccurred(const QString& message);
    void statusMessage(const QString& message);

    void frequencyChanged(quint64 hz);
    void modeChanged(const QString& mode);
    void filterChanged(int lowHz, int highHz);
    void smeterChanged(int smeter);           // 0-9+(OVF) mapped to 0-255
    void swrChanged(double swr);              // calibrated SWR ratio (e.g. 1.0 = perfect)
    void alcChanged(double alc);              // calibrated ALC meter value (0.0-2.0, 1.0 = ALC limit)
    void agcModeChanged(const QString& mode); // "fast","mid","slow"
    void autoNotchChanged(bool on);
    void manualNotchChanged(bool on);
    void compressorChanged(bool on);
    void ritEnabledChanged(bool on);
    void ritOffsetChanged(short hz);
    void pttChanged(bool on);
    void nrChanged(bool on);
    void nbChanged(bool on);
    void preampChanged(bool on);
    void preampLevelChanged(int level);
    void attenuatorChanged(bool on);
    void rfGainChanged(int level);
    void squelchChanged(bool on, int level);
    void txPowerChanged(int level);
    void duplexModeChanged(duplexMode_t mode);
    void repeaterOffsetChanged(quint64 hz);
    void toneAccessModeChanged(rptAccessTxRx_t mode);
    void toneFrequencyChanged(ushort tone);
    void dtcsCodeChanged(ushort code);
    void radioValueUpdated(Funcs func, QVariant value, uchar receiver);

    void spectrumDataReady(const QVector<float>& binsDbm, double startMhz, double endMhz, bool outOfRange);

    void networkQualityChanged(int rttMs);

    void txAudioLevelChanged(int peak, int rms);

    void audioDataReady(const QByteArray& pcm, int sampleRate);
};
