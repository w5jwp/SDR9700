// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QtNumeric>
#include <QString>
#include <QList>
#include <vector>
#include <QHash>
#include <QColor>

#include "PacketTypes.h"
#include "UdpBase.h"

constexpr quint8 kRadioModelId = 0xA2; // IC-9700 CI-V model ID

enum inputTypes
{
    inputMic = 0,
    inputACCA = 1,
    inputACCB = 2,
    inputUSB = 3,
    inputLAN = 4,
    inputMICACCA = 5,
    inputMICACCB = 6,
    inputACCAACCB = 7,
    inputMICACCAACCB = 8,
    inputSPDIF = 9,
    inputMICUSB = 10,
    inputAV = 11,
    inputMICAV = 12,
    inputACCUSB = 13,
    inputLINE = 14,
    inputMICUSBACC = 15,
    inputMICLINEACC = 16,
    inputMICLINE = 17,
    inputNone,
    inputUnknown = 0xff
};

struct radioInput
{
    radioInput() : type(inputUnknown), reg(0), name(""), level(0) {}
    explicit radioInput(inputTypes type) : type(type), reg(0), name(""), level(0) {}
    radioInput(inputTypes type, qint8 reg, QString name) : type(type), reg(reg), name(name), level(0) {}
    inputTypes type;
    qint8 reg;
    QString name;
    uchar level;
};

enum availableBands
{
    band3cm = 0,
    band6cm,    // 1
    band9cm,    // 2
    band13cm,   // 3
    band23cm,   // 4
    band70cm,   // 5
    band2m,     // 6
    bandAir,    // 7
    bandWFM,    // 8
    band4m,     // 9
    band6m,     // 10
    band10m,    // 11
    band12m,    // 12
    band15m,    // 13
    band17m,    // 14
    band20m,    // 15
    band30m,    // 16
    band40m,    // 17
    band60m,    // 18
    band80m,    // 19
    band160m,   // 20
    band630m,   // 21
    band2200m,  // 22
    bandGen,    // 23
    bandUnknown // 24
};

struct centerSpanData
{
    centerSpanData() : reg(0), name(), freq(0) {}
    centerSpanData(centerSpanData const& c) : reg(c.reg), name(c.name), freq(c.freq) {}
    centerSpanData(uchar reg, QString name, unsigned int freq) : reg(reg), name(name), freq(freq) {}
    uchar reg;
    QString name;
    unsigned int freq;

    centerSpanData& operator=(const centerSpanData& i)
    {
        this->reg = i.reg;
        this->name = i.name;
        this->freq = i.freq;
        return *this;
    }
};

struct bandType
{
    bandType()
        : region(),
          band(bandUnknown),
          lowFreq(0),
          highFreq(0),
          defaultMode(),
          range(0.0),
          memGroup(0),
          bytes(0),
          ants(false),
          power(0.0f),
          color(),
          name(),
          offset(0)
    {
    }
    bandType(bandType const& b)
        : region(b.region),
          band(b.band),
          lowFreq(b.lowFreq),
          highFreq(b.highFreq),
          defaultMode(b.defaultMode),
          range(b.range),
          memGroup(b.memGroup),
          bytes(b.bytes),
          ants(b.ants),
          power(b.power),
          color(b.color),
          name(b.name),
          offset(b.offset) {};
    bandType(QString region, availableBands band, quint64 lowFreq, quint64 highFreq, double range, int memGroup,
             qint8 bytes, bool ants, float power, QColor color, QString name, int offset)
        : region(region),
          band(band),
          lowFreq(lowFreq),
          highFreq(highFreq),
          defaultMode(modeFM),
          range(range),
          memGroup(memGroup),
          bytes(bytes),
          ants(ants),
          power(power),
          color(color),
          name(name),
          offset(offset)
    {
    }

    QString region;
    availableBands band;
    quint64 lowFreq;
    quint64 highFreq;
    radioMode_t defaultMode;
    double range;
    int memGroup;
    qint8 bytes;
    bool ants;
    float power;
    QColor color;
    QString name;
    qint64 offset;
    bandType& operator=(const bandType& i)
    {
        this->region = i.region;
        this->band = i.band;
        this->lowFreq = i.lowFreq;
        this->highFreq = i.highFreq;
        this->defaultMode = i.defaultMode;
        this->range = i.range;
        this->memGroup = i.memGroup;
        this->bytes = i.bytes;
        this->ants = i.ants;
        this->power = i.power;
        this->color = i.color;
        this->name = i.name;
        this->offset = i.offset;
        return *this;
    }
};

struct filterType
{
    filterType() : num(0), name(""), modes(0) {}
    filterType(filterType const& f) : num(f.num), name(f.name), modes(f.modes) {}
    filterType(quint8 num, QString name, unsigned int modes) : num(num), name(name), modes(modes) {}

    quint8 num;
    QString name;
    unsigned int modes;
};

struct genericType
{
    genericType() : num(0), name("") {}
    genericType(genericType const& g) : num(g.num), name(g.name) {}
    genericType(quint8 num, const QString& name) : num(num), name(name) {}
    quint8 num;
    QString name;
};

struct widthsType
{
    widthsType() : bands(0), num(0), hz(0) {}
    widthsType(widthsType const& w) : bands(w.bands), num(w.num), hz(w.hz) {}
    widthsType(ushort bands, uchar num, ushort hz) : bands(bands), num(num), hz(hz) {}
    ushort bands;
    uchar num;
    ushort hz;
};

struct bsrRequest
{
    availableBands band;
    int bsrPosition = 1;
};

struct radioCapabilities
{
    quint16 model;
    quint16 modelID = 0; // CIV address
    manufacturersType_t manufacturer = manufIcom;
    QString filename;
    int radioControlModel;
    QString modelName;

    bool hasLan; // OEM ethernet or wifi connection
    bool hasEthernet;
    bool hasWiFi;
    bool hasFDcomms;

    QVector<radioInput> inputs;

    bool hasSpectrum = true;
    quint8 spectSeqMax;
    quint16 spectAmpMax;
    quint16 spectLenMax;
    quint8 numReceiver;
    quint8 numVFO;

    bool hasNB = false;
    QByteArray nbCommand;

    bool hasDD;
    bool hasDV;
    bool hasATU;

    bool hasCTCSS;
    bool hasDTCS;
    bool hasRepeaterModes = false;

    bool hasTransmit;
    bool hasPTTCommand;
    bool hasAttenuator;
    bool hasPreamp;
    bool hasAntennaSel;
    bool hasIFShift;
    bool hasTBPF;

    bool hasRXAntenna;

    bool hasSpecifyMainSubCmd = false;
    bool hasVFOMS = false;
    bool hasVFOAB = true;

    bool hasAdvancedRptrToneCmds = false;
    bool hasQuickSplitCommand = false;

    bool hasCommand29 = false;

    QByteArray quickSplitCommand;
    QHash<Funcs, FuncType> commands;
    QHash<QByteArray, Funcs> commandsReverse;

    std::vector<genericType> attenuators;
    std::vector<genericType> preamps;
    std::vector<genericType> antennas;
    std::vector<filterType> filters;
    std::vector<centerSpanData> scopeCenterSpans;
    std::vector<bandType> bands;
    std::vector<ToneInfo> ctcss;
    std::vector<ToneInfo> dtcs;
    std::vector<genericType> roofing;
    std::vector<genericType> scopeModes;
    std::vector<StepType> steps;
    std::vector<widthsType> widths;

    std::vector<ModeInfo> modes;

    QByteArray transceiveCommand;
    quint8 guid[GUIDLEN] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    quint32 baudRate;
    quint16 memGroups;
    quint16 memories;
    quint16 memStart;
    QString memFormat;
    QVector<MemParserFormat> memParser;
    quint16 satMemories;
    QString satFormat;
    QVector<MemParserFormat> satParser;
    QVector<PeriodicType> periodic;
    QMap<int, double> meters[meterUnknown + 1];
    double meterLines[meterUnknown + 1];
};

Q_DECLARE_METATYPE(manufacturersType_t)
Q_DECLARE_METATYPE(connectionType_t)
Q_DECLARE_METATYPE(udpPreferences)
Q_DECLARE_METATYPE(radioCapabilities)
Q_DECLARE_METATYPE(ModeInfo)
Q_DECLARE_METATYPE(radioInput)
Q_DECLARE_METATYPE(filterType)
Q_DECLARE_METATYPE(inputTypes)
Q_DECLARE_METATYPE(genericType)
Q_DECLARE_METATYPE(bandType)
Q_DECLARE_METATYPE(widthsType)
Q_DECLARE_METATYPE(centerSpanData)
