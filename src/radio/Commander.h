#pragma once

#include "RadioCommander.h"
#include "CivRttEstimator.h"

#include <QElapsedTimer>
#include <QTimer>
#include <QVector>

struct CommanderCorrelationDiagnostics
{
    qsizetype pendingReplies{0};
    qsizetype pendingReplyHighWaterMark{0};
    quint64 pendingReplyOverflows{0};
    quint64 pendingReplyExpirations{0};
    quint64 unmatchedReplyFrames{0};
    quint64 ambiguousUnsolicitedFrames{0};
    quint64 acceptedAcknowledgements{0};
    quint64 rejectedAcknowledgements{0};
    quint64 deferredReplyReads{0};
    quint64 coalescedReplyReads{0};
    quint64 droppedReplyReads{0};
    quint64 drainedReplyFrames{0};
    qint64 resolvedReplyDrainMs{0};
    qint64 abandonedReplyDrainMs{0};
    qint64 replyTimeoutMs{0};
    quint64 rttSampleCount{0};
};

struct CommanderSchedulerDiagnostics
{
    qsizetype queuedCommands{0};
    qsizetype highWaterMark{0};
    quint64 coalescedCommands{0};
    quint64 droppedCommands{0};
    quint64 dispatchedCommands{0};
};

class Commander : public RadioCommander
{
    Q_OBJECT
    friend class CommanderCodecTest;

  public:
    explicit Commander(RadioCommander* parent = nullptr);
    explicit Commander(quint8 guid[GUIDLEN], RadioCommander* parent = nullptr);
    ~Commander();
    CommanderCorrelationDiagnostics correlationDiagnostics() const;
    CommanderSchedulerDiagnostics schedulerDiagnostics() const;

  public slots:
    void process() override;
    void commSetup(quint16 radioCivAddr, UdpConnectionSettings settings, audioSetup rxSetup, audioSetup txSetup,
                   QString vsp, quint16 tcp) override;
    void closeComm() override;

    void setRadioID(quint16 radioID) override;
    void setCIVAddr(quint16 newCivAddr) override;

    void handleNewData(const QByteArray& data) override;
    void receiveBaudRate(quint32 baudrate) override;

    void receiveCommand(Funcs func, QVariant value, uchar receiver) override;
    void receiveCommandNoReadback(Funcs func, QVariant value, uchar receiver);
    void scheduleMeterRead(Funcs func, uchar receiver);
    void scheduleStartupRead(Funcs func, uchar receiver);
    void requestMainSubExchange();
    void requestReceiverScopedRead(Funcs func, uchar receiver);
    void discardPendingReplies(Funcs func);
    void readCurrentFrequencyAndMode();
    void setPttActive(bool active);
    void sendDtmfPcm(const QByteArray& pcm);
    void enableAudio();
    void setRxAudioDevice(const QAudioDevice& device);
    void setTxAudioDevice(const QAudioDevice& device);
    bool stopLocalAudio();

  signals:
    void mainSubExchangeDispatched();

  private:
    enum class FrameOrigin
    {
        SolicitedReply,
        UnsolicitedBroadcast
    };

    enum class ReplyParseResult
    {
        NotHandled,
        Parsed,
        Malformed
    };

    void commonSetup();
    void shutdownComm();

    enum class ScheduledCommandClass
    {
        Meter,
        StartupRead
    };

    struct ScheduledCommand
    {
        ScheduledCommandClass commandClass{ScheduledCommandClass::StartupRead};
        Funcs func{funcNone};
        uchar receiver{0};
    };

    void enqueueScheduledRead(ScheduledCommandClass commandClass, Funcs func, uchar receiver);
    void dispatchNextScheduledCommand();
    void resetScheduledCommands();

    void parseData(const QByteArray& dataInput);
    void parseCommand(FrameOrigin origin);
    ReplyParseResult parseFrequencyReply(Funcs& func, QVariant& value, uchar& receiver);
    ReplyParseResult parseModeReply(Funcs& func, QVariant& value, uchar& receiver);
    ReplyParseResult parseLevelMeterReply(Funcs func, QVariant& value);
    ReplyParseResult parseFeatureReply(Funcs func, QVariant& value, uchar receiver);
    ReplyParseResult parseScopeReply(Funcs func, QVariant& value, uchar& receiver);
    bool replyPayloadTooShort(Funcs func, int requiredBytes) const;
    bool appendSetCommandValue(Funcs func, const QVariant& value, uchar receiver, const FuncType& command,
                               QByteArray& payload);
    static quint8 bcdHexToUChar(quint8 in);
    quint8 bcdHexToUChar(quint8 hundreds, quint8 tensunits);
    static unsigned int bcdHexToUInt(quint8 hundreds, quint8 tensunits);
    static unsigned int bcdHexToUInt(quint8 tenthou, quint8 hundreds, quint8 tensunits);

    QByteArray bcdEncodeChar(quint8 num);
    QByteArray bcdEncodeInt(quint16 num);
    QByteArray bcdEncodeInt(unsigned int num);
    QByteArray setMemory(MemoryType mem);
    Frequency parseFrequency();
    Frequency parseFrequency(QByteArray data, quint8 lastPosition);

    Frequency parseFreqData(const QByteArray& data, uchar receiver);
    quint64 parseFreqDataToInt(QByteArray data);
    Frequency parseFrequencyRptOffset(QByteArray data);
    bool parseMemory(QVector<MemParserFormat>* memParser, MemoryType* mem);
    void initializeMemoryForParsing(MemoryType& memory) const;
    void parseMemoryField(const MemParserFormat& format, const QByteArray& data, MemoryType& memory);
    QByteArray makeFreqPayloadRptOffset(Frequency freq);
    QByteArray makeFreqPayload(double frequency);
    QByteArray makeFreqPayload(Frequency freq, uchar numchars = 5);
    QByteArray encodeTone(quint16 tone, bool tinv, bool rinv) const;
    QByteArray encodeTone(quint16 tone) const;

    ToneInfo decodeTone(const QByteArray& eTone);
    uchar makeFilterWidth(ushort width, uchar receiver);

    uchar convertNumberToHex(uchar num);

    ModeInfo parseMode(uchar mode, uchar data, uchar filter, uchar receiver = 0, uchar vfo = 0);
    bool parseSpectrum(ScopeData& d, uchar receiver);
    bool decodeSpectrumSequence(quint8& sequence, quint8& sequenceMax) const;
    FuncType getCommand(Funcs func, QByteArray& payload, int value = INT_MIN, uchar receiver = 0);
    void rememberPendingReply(Funcs func, uchar receiver);
    bool pendingReplyReceiver(Funcs func, uchar* receiver);
    bool takePendingReplyReceiver(Funcs func, uchar* receiver);
    void discardExpiredPendingReplies();
    bool deferReplyReadIfBlocked(Funcs func, uchar receiver);
    void beginReplyFamilyDrain(Funcs func, qint64 durationMs);
    void dispatchDeferredReplyReads();
    void dispatchMainSubExchange();
    bool replyFamilyBlocked(Funcs func) const;
    bool replyFamilyDraining(Funcs func) const;

    QByteArray getLANAddr();
    QByteArray getACCAddr(quint8 ab);
    void sendDataOut();
    void prepDataAndSend(QByteArray data);
    void debugMe();

    centerSpanData createScopeCenter(uchar s, QString name);

    UdpHandler* udp = nullptr;
    QThread* udpHandlerThread = nullptr;

    void determineRadioCaps();
    QByteArray payloadIn;
    QByteArray echoPrefix;
    QByteArray replyPrefix;
    QByteArray genericReplyPrefix;

    QByteArray payloadPrefix;
    QByteArray payloadSuffix;

    QByteArray radioData;

    QByteArray spectrumLine;
    quint16 model = 0;
    quint8 spectSeqMax{0};
    quint16 spectAmpMax{0};
    quint16 spectLenMax{0};
    uchar oldScopeMode{0};

    bool lookingForRadio{false};
    bool foundRadio{false};

    double frequencyMhz{0.0};
    quint16 civAddr{0};
    quint16 incomingCIVAddr{0};

    struct PendingReply
    {
        Funcs func{funcNone};
        uchar receiver{0};
        qint64 createdAtMs{0};
        qint64 expiresAtMs{0};
    };
    QVector<PendingReply> m_pendingReplies;
    struct DeferredReplyRead
    {
        Funcs func{funcNone};
        uchar receiver{0};
    };
    struct ReplyFamilyDrain
    {
        Funcs func{funcNone};
        qint64 untilMs{0};
    };
    QVector<DeferredReplyRead> m_deferredReplyReads;
    bool m_mainSubExchangeQueued{false};
    QVector<ReplyFamilyDrain> m_replyFamilyDrains;
    QTimer* m_replyDrainTimer{nullptr};
    CivRttEstimator m_rttEstimator;
    QElapsedTimer m_pendingCommandClock;
    CommanderCorrelationDiagnostics m_correlationDiagnostics;
    QVector<ScheduledCommand> m_scheduledCommands;
    QTimer* m_scheduledCommandTimer{nullptr};
    CommanderSchedulerDiagnostics m_schedulerDiagnostics;
    int m_consecutiveMeterDispatches{0};
    bool m_suppressReadbackForCurrentCommand{false};
    bool m_shutdownComplete{false};

    ScopeData mainScopeData;
    ScopeData subScopeData;
    QElapsedTimer m_scopeAssemblyClocks[2];
    quint8 m_expectedScopeSequences[2]{0, 0};

    QString ip;
    int cport{0};
    int sport{0};
    int aport{0};
    QString username;

    quint8 localVolume = 0;

    // 12 entries cover 10^0–10^11; BCD frequency data is at most 5 bytes (10 nibbles, indices 0–9).
    static constexpr quint64 kPow10[12] = {1,       10,       100,       1000,       10000,       100000,
                                           1000000, 10000000, 100000000, 1000000000, 10000000000, 100000000000};
    static_assert(std::size(kPow10) == 12);

#ifdef DEBUG_PARSE
    quint64 averageParseTime = 0;
    int numParseSamples = 0;
    int lowParse = 9999;
    int highParse = 0;
    QTime lastParseReport = QTime::currentTime();
#endif
};
