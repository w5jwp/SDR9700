#pragma once

#include <QObject>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QDebug>

#include "Types.h"
#include "UdpHandler.h"
#include "RadioIdentities.h"
#include "CachingQueue.h"

// CI-V address used by SDR9700 as the controller when talking to the IC-9700
// over LAN. Radio replies addressed to this value are treated as responses to
// SDR9700-originated commands.
inline constexpr quint8 compCivAddr = 0xE1;

class RadioCommander : public QObject
{
    Q_OBJECT
    friend class CommanderCodecTest;

  public:
    explicit RadioCommander(QObject* parent = nullptr);
    explicit RadioCommander(quint8 guid[GUIDLEN], QObject* parent = nullptr);
    ~RadioCommander();

  public slots:
    void receiveAudioData(const audioPacket& data);
    void handlePortError(errorType err);
    void handleStatusUpdate(const networkStatus& status);
    void handleNetworkAudioLevels(const networkAudioLevels& levels);
    void changeLatency(const quint16 value);
    void radioSelection(const QList<radio_cap_packet>& radios);
    void radioUsage(quint8 radio, bool admin, quint8 busy, const QString& name, const QString& ip);
    void setCurrentRadio(quint8 radio);

    virtual void process();
    virtual void commSetup(quint16 radioCivAddr, UdpConnectionSettings connectionSettings, audioSetup rxAudioSetup,
                           audioSetup txAudioSetup, QString vsp, quint16 tcp);
    virtual void closeComm();

    virtual void setRadioID(quint16 radioID);
    virtual void setCIVAddr(quint16 civAddr);

    virtual void handleNewData(const QByteArray& data);
    virtual void receiveBaudRate(quint32 baudrate);

    virtual void receiveCommand(Funcs func, QVariant value, uchar receiver);

  signals:
    void commReady();
    void lanReady();

    void havePortError(errorType err);
    void haveStatusUpdate(const networkStatus& status);
    void haveSessionHeartbeat();

    void haveNetworkAudioLevels(const networkAudioLevels l);
    void dataForComm(const QByteArray& outData);
    void haveDataFromRadio(const QByteArray& outData);

    void setHalfDuplex(bool en);

    void haveChangeLatency(quint16 value);
    void haveDataForServer(QByteArray outData);
    void haveAudioData(audioPacket data);
    void initUdpHandler();
    void requestEnableAudio();
    void haveSetVolume(quint8 level);
    void haveBaudRate(quint32 baudrate);

    void haveSpectrumData(QByteArray spectrum, double startFreq, double endFreq);
    void haveSpectrumBounds();
    void haveScopeSpan(Frequency span, bool isSub);
    void haveSpectrumMode(uchar spectmode);
    void haveScopeEdge(char edge);

    void haveRadioID(radioCapabilities radioCaps);
    void discoveredRadioID(radioCapabilities radioCaps);

    void haveDuplexMode(duplexMode_t);
    void haveTone(quint16 tone);
    void haveTSQL(quint16 tsql);
    void haveDTCS(quint16 dcscode, bool tinv, bool rinv);
    void haveRptOffsetFrequency(Frequency f);
    void haveMemory(MemoryType mem);

    void requestRadioSelection(QList<radio_cap_packet> radios);
    void setRadioUsage(quint8 radio, bool admin, quint8 busy, QString user, QString ip);
    void selectedRadio(quint8 radio);
    void finished();
    void haveReceivedValue(Funcs func, QVariant value);
    void sidetone(QString text);
    void stopsidetone();

  protected:
    CachingQueue* queue;
    UdpConnectionSettings settings;
    audioSetup rxSetup;
    audioSetup txSetup;

    double getMeterCal(meter_t meter, int value);

    quint8 guid[GUIDLEN] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    bool radioPoweredOn = false; // Updated after a valid controller-addressed CI-V reply confirms the radio is
                                 // responding.

    struct radioCapabilities radioCaps{};
    bool haveRadioCaps = false;
    bool isRadioAdmin = true; // true = request admin-level radio access on connect

  private:
};
