#pragma once

#include <QDateTime>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>

#include "RadioIdentities.h"
#include "Types.h"

enum stateTypes
{
    VFOAFREQ,
    VFOBFREQ,
    CURRENTVFO,
    PTT,
    MODE,
    FILTER,
    PASSBAND,
    DUPLEX,
    DATAMODE,
    ANTENNA,
    RXANTENNA,
    CTCSS,
    TSQL,
    DTCS,
    CSQL,
    PREAMP,
    AGC,
    ATTENUATOR,
    MODINPUT,
    AFGAIN,
    RFGAIN,
    SQUELCH,
    RFPOWER,
    MICGAIN,
    COMPLEVEL,
    MONITORLEVEL,
    BAL,
    KEYSPD,
    VOXGAIN,
    ANTIVOXGAIN,
    CWPITCH,
    NOTCHF,
    IF,
    PBTIN,
    PBTOUT,
    APF,
    NR,
    NB,
    NBDEPTH,
    NBWIDTH,
    RADIOINPUT,
    POWERONOFF,
    RITVALUE,
    FAGCFUNC,
    NBFUNC,
    COMPFUNC,
    VOXFUNC,
    TONEFUNC,
    TSQLFUNC,
    SBKINFUNC,
    FBKINFUNC,
    ANFFUNC,
    NRFUNC,
    AIPFUNC,
    APFFUNC,
    MONFUNC,
    MNFUNC,
    RFFUNC,
    AROFUNC,
    MUTEFUNC,
    VSCFUNC,
    REVFUNC,
    SQLFUNC,
    ABMFUNC,
    BCFUNC,
    MBCFUNC,
    RITFUNC,
    AFCFUNC,
    SATMODEFUNC,
    SCOPEFUNC,
    ANN,
    APO,
    BACKLIGHT,
    BEEP,
    TIME,
    BAT,
    KEYLIGHT,
    RESUMEFUNC,
    TBURSTFUNC,
    TUNERFUNC,
    LOCKFUNC,
    SMETER,
    POWERMETER,
    SWRMETER,
    ALCMETER,
    COMPMETER,
    VOLTAGEMETER,
    CURRENTMETER,
};

struct value
{
    quint64 m_value = 0;
    bool m_valid = false;
    bool m_updated = false;
    QDateTime m_dateUpdated;
};

class RadioState
{
  public:
    void invalidate(stateTypes s)
    {
        QMutexLocker locker(&m_mutex);
        m_values[s].m_valid = false;
    }
    bool isValid(stateTypes s)
    {
        QMutexLocker locker(&m_mutex);
        return m_values[s].m_valid;
    }
    bool isUpdated(stateTypes s)
    {
        QMutexLocker locker(&m_mutex);
        return m_values[s].m_updated;
    }
    QDateTime whenUpdated(stateTypes s)
    {
        QMutexLocker locker(&m_mutex);
        return m_values[s].m_dateUpdated;
    }

    void set(stateTypes s, quint64 x, bool u) { setValue(s, x, u); }
    void set(stateTypes s, qint32 x, bool u) { setValue(s, quint64(x), u); }
    void set(stateTypes s, qint16 x, bool u) { setValue(s, quint64(x), u); }
    void set(stateTypes s, quint16 x, bool u) { setValue(s, quint64(x), u); }
    void set(stateTypes s, quint8 x, bool u) { setValue(s, quint64(x), u); }
    void set(stateTypes s, bool x, bool u) { setValue(s, quint64(x), u); }
    void set(stateTypes s, duplexMode_t x, bool u) { setValue(s, quint64(x), u); }
    void set(stateTypes s, inputTypes x, bool u) { setValue(s, quint64(x), u); }

    bool getBool(stateTypes s) { return getValue(s) != 0; }
    quint8 getChar(stateTypes s) { return quint8(getValue(s)); }
    qint16 getInt16(stateTypes s) { return qint16(getValue(s)); }
    quint16 getUInt16(stateTypes s) { return quint16(getValue(s)); }
    qint32 getInt32(stateTypes s) { return qint32(getValue(s)); }
    quint32 getUInt32(stateTypes s) { return quint32(getValue(s)); }
    quint64 getInt64(stateTypes s) { return getValue(s); }
    duplexMode_t getDuplex(stateTypes s) { return duplexMode_t(getValue(s)); }
    inputTypes getInput(stateTypes s) { return inputTypes(getValue(s)); }

  private:
    void setValue(stateTypes s, quint64 x, bool u)
    {
        QMutexLocker locker(&m_mutex);
        const bool valueChanged = x != m_values[s].m_value;
        const bool shouldUpdate = u || !m_values[s].m_updated;
        if (valueChanged && shouldUpdate)
        {
            m_values[s].m_value = x;
            m_values[s].m_valid = true;
            m_values[s].m_updated = u;
            m_values[s].m_dateUpdated = QDateTime::currentDateTime();
        }
    }

    quint64 getValue(stateTypes s)
    {
        QMutexLocker locker(&m_mutex);
        return m_values[s].m_value;
    }

    QMap<stateTypes, value> m_values;
    QMutex m_mutex;
};
