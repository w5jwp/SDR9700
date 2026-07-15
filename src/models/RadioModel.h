#pragma once

#include <QObject>
#include <QAudioDevice>
#include <QString>
#include <memory>

class IRadioBackend;
class VfoModel;
class PanadapterModel;

class RadioModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool connected READ isConnected NOTIFY connectionChanged)
    Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged)
    Q_PROPERTY(bool transmitting READ isTransmitting NOTIFY transmittingChanged)
    Q_PROPERTY(QString radioName READ radioName NOTIFY infoChanged)
    Q_PROPERTY(int smeter READ smeter NOTIFY smeterChanged)

  public:
    explicit RadioModel(QObject* parent = nullptr);
    ~RadioModel() override;

    bool isConnected() const { return m_connected; }
    bool isReady() const { return m_ready; }
    bool isTransmitting() const { return m_transmitting; }
    const QString& radioName() const { return m_radioName; }
    int smeter() const { return m_smeter; }

    VfoModel* vfo() const { return m_vfo; }
    PanadapterModel* panadapter() const { return m_pan; }
    IRadioBackend* backend() const { return m_backend; }

  public slots:
    void connectToRadio(const QString& host, quint16 port, const QString& user, const QString& pass);
    void disconnectFromRadio();
    void setRxAudioDevice(const QAudioDevice& dev);
    void setTxAudioDevice(const QAudioDevice& dev);
    void setLanModLevel(int level);
    void setTuningStep(int step);

  signals:
    void connectionChanged(bool connected);
    void readyChanged(bool ready);
    void infoChanged();
    void smeterChanged(int value);
    void swrChanged(double swr);
    void alcChanged(double alc);
    void pttChanged(bool on);
    void transmittingChanged(bool on);
    void errorOccurred(const QString& message);
    void statusMessage(const QString& message);
    void networkQualityChanged(int rttMs);
    void txAudioLevelChanged(int peak, int rms);

  private slots:
    void onBackendConnected();
    void onBackendDisconnected();
    void onBackendError(const QString& msg);
    void onBackendReadyChanged(bool ready);
    void onFrequencyChanged(quint64 hz);
    void onModeChanged(const QString& mode);
    void onSmeterChanged(int s);
    void onSwrChanged(double swr);
    void onAlcChanged(double alc);
    void onPttChanged(bool on);
    void onSpectrumDataReady(const QVector<float>& bins, double start, double end);

  private:
    IRadioBackend* m_backend{nullptr};
    VfoModel* m_vfo{nullptr};
    PanadapterModel* m_pan{nullptr};

    bool m_connected{false};
    bool m_ready{false};
    bool m_transmitting{false};
    QString m_radioName{"IC-9700"};
    int m_smeter{0};
};
