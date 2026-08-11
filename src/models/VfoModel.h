// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <optional>
#include "Types.h"

class IRadioBackend;

class VfoModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(quint64 frequencyHz READ frequencyHz NOTIFY frequencyChanged)
    Q_PROPERTY(QString mode READ mode NOTIFY modeChanged)
    Q_PROPERTY(int filterLow READ filterLow NOTIFY filterChanged)
    Q_PROPERTY(int filterHigh READ filterHigh NOTIFY filterChanged)
    Q_PROPERTY(bool txActive READ txActive NOTIFY txActiveChanged)

  public:
    explicit VfoModel(IRadioBackend* backend, QObject* parent = nullptr);

    // 145 MHz is the IC-9700's power-on default; replaced by the first radio update.
    quint64 frequencyHz() const { return m_freqHz.value_or(145000000ULL); }
    QString mode() const { return m_mode.value_or(QStringLiteral("FM")); }
    int filterLow() const { return m_filterLow; }
    int filterHigh() const { return m_filterHigh; }
    bool txActive() const { return m_txActive; }
    bool nrOn() const { return m_nrOn; }
    bool nbOn() const { return m_nbOn; }
    bool attenuatorOn() const { return m_attenuatorOn; }
    bool preampOn() const { return m_preampOn; }
    int preampLevel() const { return m_preampLevel; }
    bool autoNotchOn() const { return m_autoNotchOn; }
    bool manualNotchOn() const { return m_manualNotchOn; }
    bool compressorOn() const { return m_compressorOn; }
    bool xfcOn() const { return m_xfcOn; }
    const QString& agcMode() const { return m_agcMode; }
    bool ritOn() const { return m_ritOn; }
    short ritHz() const { return m_ritHz; }

    static QStringList availableModes();

    void setFrequencyHz(quint64 hz);
    void setMode(const QString& modeName);
    void setFilterWidth(int lowHz, int highHz);
    void setRfGain(int level);
    void setAfGain(int level);
    void setSquelch(bool on, int level);
    void setNrEnabled(bool on);
    void setNrLevel(int level);
    void setNbEnabled(bool on);
    void setNbLevel(int level);
    void setPreampLevel(int level);
    void setAttenuatorEnabled(bool on);
    void setTxPower(int level);
    void setAgcMode(const QString& modeName);
    void setAutoNotch(bool on);
    void setManualNotch(bool on);
    void setCompressor(bool on);
    void setXfcEnabled(bool on);
    void setRitEnabled(bool on);
    void setRitOffset(short hz);
    void setDuplexMode(duplexMode_t duplexMode);
    void setRepeaterOffsetHz(quint64 hz);
    void setToneAccessMode(rptAccessTxRx_t toneAccessMode);
    void setToneFrequency(ushort tone);
    void setDtcsCode(ushort code);
    bool setPtt(bool on);
    void sendDtmf(const QString& digits);

    void applyFrequency(quint64 hz);
    void applyMode(const QString& modeName);
    void applyPtt(bool on);
    void applyNrEnabled(bool on);
    void applyNrLevel(int level);
    void applyNbEnabled(bool on);
    void applyNbLevel(int level);
    void applyPreampEnabled(bool on);
    void applyPreampLevel(int level);
    void applyAttenuatorEnabled(bool on);
    void applyRfGain(int level);
    void applySquelch(bool on, int level);
    void applyTxPower(int level);
    void applyAutoNotch(bool on);
    void applyManualNotch(bool on);
    void applyCompressor(bool on);
    void applyXfcEnabled(bool on);
    void applyAgcMode(const QString& modeName);
    void applyRitEnabled(bool on);
    void applyRitOffset(short hz);
    void applyDuplexMode(duplexMode_t duplexMode);
    void applyRepeaterOffsetHz(quint64 hz);
    void applyToneAccessMode(rptAccessTxRx_t toneAccessMode);
    void applyToneFrequency(ushort tone);
    void applyDtcsCode(ushort code);

  signals:
    void frequencyChanged(quint64 hz);
    void modeChanged(const QString& mode);
    void filterChanged(int low, int high);
    void txActiveChanged(bool on);
    void rfGainChanged(int level);
    void afGainChanged(int level);
    void squelchChanged(bool on, int level);
    void nrChanged(bool on, int level);
    void nbChanged(bool on, int level);
    void preampChanged(bool on);
    void preampLevelChanged(int level);
    void attenuatorChanged(bool on);
    void txPowerChanged(int level);
    void agcModeChanged(const QString& mode);
    void autoNotchChanged(bool on);
    void manualNotchChanged(bool on);
    void compressorChanged(bool on);
    void xfcChanged(bool on);
    void ritChanged(bool on, short hz);
    void duplexModeChanged(duplexMode_t mode);
    void repeaterOffsetChanged(quint64 hz);
    void toneAccessModeChanged(rptAccessTxRx_t mode);
    void toneFrequencyChanged(ushort tone);
    void dtcsCodeChanged(ushort code);

  private:
    IRadioBackend* m_backend{nullptr};

    // std::optional members: empty until the first confirmed radio reply.
    // Accessors return a default until the radio value is known.
    std::optional<quint64> m_freqHz;
    std::optional<QString> m_mode;
    std::optional<int> m_rfGain;
    std::optional<int> m_txPower;
    struct SquelchState
    {
        bool on;
        int level;
    };
    std::optional<SquelchState> m_squelch;

    int m_filterLow{-8000};
    int m_filterHigh{8000};
    bool m_txActive{false};
    int m_afGain{128};
    bool m_nrOn{false};
    int m_nrLevel{5};
    bool m_nbOn{false};
    int m_nbLevel{5};
    bool m_preampOn{false};
    int m_preampLevel{0};
    bool m_attenuatorOn{false};
    bool m_autoNotchOn{false};
    bool m_manualNotchOn{false};
    bool m_compressorOn{false};
    bool m_xfcOn{false};
    QString m_agcMode{"mid"};
    bool m_ritOn{false};
    short m_ritHz{0};
    duplexMode_t m_duplexMode{dmSimplex};
    quint64 m_repeaterOffsetHz{600000};
    rptAccessTxRx_t m_toneAccessMode{ratrNN};
    ushort m_toneFrequency{670};
    ushort m_dtcsCode{23};
};
