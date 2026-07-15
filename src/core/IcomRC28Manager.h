#pragma once
#ifdef HAVE_HIDAPI

#include <QObject>
#include <QTimer>
#include <QByteArray>
#include <QString>
#include <atomic>
#include <hidapi/hidapi.h>

class IcomRC28Manager : public QObject
{
    Q_OBJECT

  public:
    explicit IcomRC28Manager(QObject* parent = nullptr);
    ~IcomRC28Manager() override;

    static QString detectDevice();
    void close();

    bool isOpen() const { return m_device.load(std::memory_order_relaxed) != nullptr; }
    bool isBlockedByMultiple() const { return m_multipleDetected.load(std::memory_order_acquire); }
    const QString& devicePath() const { return m_devicePath; }
    const QString& serialNumber() const { return m_serialNumber; }

    static QString settingsField(const QString& field, const QString& defaultValue);
    static void setSettingsField(const QString& field, const QString& value);

    // LED byte is active-low; clearing a bit turns the LED on.
    static constexpr uint8_t kLedBitTx = 0x01u;
    static constexpr uint8_t kLedBitF1 = 0x02u;
    static constexpr uint8_t kLedBitF2 = 0x04u;
    static constexpr uint8_t kLedBitLink = 0x08u;
    static constexpr uint8_t kLedsAllOff = 0x0Fu;

  public slots:
    void loadSettings();
    void setIcomRC28Leds(uint8_t ledByte);

  signals:
    void tuneSteps(int steps);
    void buttonPressed(int button, int action);
    void connectionChanged(bool connected, const QString& deviceName);
    void multipleDevicesDetected(const QString& deviceName);

  private slots:
    void poll();
    void hotplugCheck();

  private:
    bool open();
    void sendLeds(uint8_t ledByte);

    bool parseIcomRC28Report(const uint8_t* buf, size_t len, int* steps, int* button, int* action);

    std::atomic<hid_device*> m_device{nullptr};
    QString m_deviceName;
    QString m_devicePath;
    QString m_serialNumber;
    std::atomic<bool> m_multipleDetected{false};
    QString m_blockedDeviceName;
    uint8_t m_prevButtons{kIcomRC28ButtonsIdle};
    QTimer* m_pollTimer{nullptr};
    QTimer* m_hotplugTimer{nullptr};
    uint8_t m_buf[64]{};

    static constexpr uint16_t kIcomRC28Vid = 0x0C26;
    static constexpr uint16_t kIcomRC28Pid = 0x001E;
    static constexpr uint8_t kIcomRC28ReportGuard = 0x01;
    static constexpr uint8_t kIcomRC28ButtonsIdle = 0x07;
    static constexpr uint8_t kIcomRC28LedsOff = 0x0F;
    static constexpr int kPollIntervalMs = 5;
    static constexpr int kHotplugIntervalMs = 3000;
};

#endif
