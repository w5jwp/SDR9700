#include "RadioCommander.h"
#include <QDebug>

#include "RadioIdentities.h"
#include "LogCategories.h"

#include <iterator>

RadioCommander::RadioCommander(QObject* parent) : QObject(parent)
{
    qInfo(logRadio()) << "creating instance of RadioCommander()";
    queue = CachingQueue::getInstance();
}

RadioCommander::RadioCommander(quint8 guid[GUIDLEN], QObject* parent) : QObject(parent)
{

    qInfo(logRadio()) << "creating instance of RadioCommander(guid)";
    memcpy(this->guid, guid, GUIDLEN);
    queue = CachingQueue::getInstance();
}

RadioCommander::~RadioCommander()
{
    qInfo(logRadio()) << "closing instance of RadioCommander()";
}

void RadioCommander::commSetup(quint16 radioCivAddr, UdpConnectionSettings connectionSettings, audioSetup rxAudioSetup,
                               audioSetup txAudioSetup, QString vsp, quint16 tcp)
{
    Q_UNUSED(radioCivAddr)
    Q_UNUSED(connectionSettings)
    Q_UNUSED(rxAudioSetup)
    Q_UNUSED(txAudioSetup)
    Q_UNUSED(vsp)
    Q_UNUSED(tcp)
    qWarning(logRadio()) << "commSetup() (network) not implemented";
}

void RadioCommander::closeComm()
{
    qWarning(logRadio()) << "closeComm() not implemented";
}

void RadioCommander::process()
{
    qWarning(logRadio()) << "process() not implemented";
}

void RadioCommander::setRadioID(quint16 radioID)
{
    Q_UNUSED(radioID)

    qWarning(logRadio()) << "setRadioID() not implemented";
}

void RadioCommander::receiveBaudRate(quint32 baudrate)
{
    Q_UNUSED(baudrate)

    qWarning(logRadio()) << "receiveBaudRate() not implemented";
}

void RadioCommander::setCIVAddr(quint16 civAddr)
{
    Q_UNUSED(civAddr)

    qWarning(logRadio()) << "setCIVAddr() not implemented";
}

void RadioCommander::receiveCommand(Funcs func, QVariant value, uchar receiver)
{
    Q_UNUSED(func)
    Q_UNUSED(value)
    Q_UNUSED(receiver)

    qWarning(logRadio()) << "receiveCommand() not implemented";
}

void RadioCommander::handlePortError(errorType err)
{
    qInfo(logRadio()).noquote().nospace() << "Radio error device=" << err.device << " message=" << err.message;
    emit havePortError(err);
}

void RadioCommander::handleStatusUpdate(const networkStatus& status)
{
    emit haveStatusUpdate(status);
}

void RadioCommander::handleNetworkAudioLevels(const networkAudioLevels& levels)
{
    emit haveNetworkAudioLevels(levels);
}

void RadioCommander::handleNewData(const QByteArray& data)
{
    emit haveDataForServer(data);
}

void RadioCommander::receiveAudioData(const audioPacket& data)
{
    emit haveAudioData(data);
}

void RadioCommander::changeLatency(const quint16 value)
{
    emit haveChangeLatency(value);
}

void RadioCommander::radioSelection(const QList<radio_cap_packet>& radios)
{
    emit requestRadioSelection(radios);
}

void RadioCommander::radioUsage(quint8 radio, bool admin, quint8 busy, const QString& name, const QString& ip)
{
    emit setRadioUsage(radio, admin, busy, name, ip);
}

void RadioCommander::setCurrentRadio(quint8 radio)
{
    emit selectedRadio(radio);
}

double RadioCommander::getMeterCal(meter_t meter, int value)
{
    double ret = static_cast<double>(value);

    if (radioCaps.meters[meter].size())
    {
        auto it = radioCaps.meters[meter].lowerBound(value);
        if (it == radioCaps.meters[meter].end())
        {
            ret = std::prev(it).value();
        }
        else if (value <= it.key())
        {
            if (it == radioCaps.meters[meter].begin())
            {
                ret = it.value();
            }
            else
            {
                int key = it.key();
                double val = it.value();
                double prevVal = (--it).value();
                int prevKey = it.key();
                double interp = ((key - value) * (val - prevVal)) / (key - prevKey);
                ret = val - interp;
            }
        }
    }

    return ret;
}
