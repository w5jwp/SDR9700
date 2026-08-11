#ifdef HAVE_HIDAPI
#include "IcomRC28Manager.h"

#include "AppSettings.h"
#include "LogCategories.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QStringList>

namespace
{
constexpr auto kICOMRC28ButtonMappingKey = "ICOMRC28ButtonMapping";
// RC-28 HID report byte 5 values observed for the three physical buttons.
constexpr uint8_t kIcomRC28ButtonF1 = 0x05;
constexpr uint8_t kIcomRC28ButtonF2 = 0x03;
constexpr uint8_t kIcomRC28ButtonPtt = 0x06;

QString readIcomRC28SettingsJson()
{
    const auto& settings = AppSettings::instance();
    return settings.value(QString::fromLatin1(kICOMRC28ButtonMappingKey), QStringLiteral("{}")).toString();
}
} // namespace

IcomRC28Manager::IcomRC28Manager(QObject* parent) : QObject(parent)
{
    if (hid_init() != 0)
    {
        qWarning(logIcomRC28()).noquote() << "hidapi initialization failed; RC-28 support will remain unavailable";
    }

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &IcomRC28Manager::poll);

    m_hotplugTimer = new QTimer(this);
    m_hotplugTimer->setInterval(kHotplugIntervalMs);
    connect(m_hotplugTimer, &QTimer::timeout, this, &IcomRC28Manager::hotplugCheck);
}

IcomRC28Manager::~IcomRC28Manager()
{
    close();
    hid_exit();
}

QString IcomRC28Manager::settingsField(const QString& field, const QString& defaultValue)
{
    const QByteArray raw = readIcomRC28SettingsJson().toUtf8();
    const QJsonObject obj = QJsonDocument::fromJson(raw).object();
    const QJsonValue value = obj.value(field);
    return value.isString() ? value.toString() : defaultValue;
}

void IcomRC28Manager::setSettingsField(const QString& field, const QString& value)
{
    auto& settings = AppSettings::instance();
    QJsonObject obj = QJsonDocument::fromJson(readIcomRC28SettingsJson().toUtf8()).object();
    obj.insert(field, value);
    settings.setValue(QString::fromLatin1(kICOMRC28ButtonMappingKey),
                      QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

QString IcomRC28Manager::detectDevice()
{
    if (auto* info = hid_enumerate(kIcomRC28Vid, kIcomRC28Pid))
    {
        hid_free_enumeration(info);
        return QStringLiteral("Icom RC-28 Remote Encoder");
    }
    return {};
}

bool IcomRC28Manager::parseIcomRC28Report(const uint8_t* buf, size_t len, int* steps, int* button, int* action)
{
    if (len < 6 || buf[0] != kIcomRC28ReportGuard)
    {
        return false;
    }

    const uint8_t speed = buf[1];
    const uint8_t dir = buf[3];
    const uint8_t btns = buf[5];

    if (btns != m_prevButtons)
    {
        const uint8_t prev = m_prevButtons;
        m_prevButtons = btns;

        if (btns == kIcomRC28ButtonsIdle)
        {
            if (prev == kIcomRC28ButtonF1)
            {
                *button = 1;
                *action = 1;
                return true;
            }
            if (prev == kIcomRC28ButtonF2)
            {
                *button = 2;
                *action = 1;
                return true;
            }
            if (prev == kIcomRC28ButtonPtt)
            {
                *button = 3;
                *action = 1;
                return true;
            }
        }
        else
        {
            if (btns == kIcomRC28ButtonF1)
            {
                *button = 1;
                *action = 0;
                return true;
            }
            if (btns == kIcomRC28ButtonF2)
            {
                *button = 2;
                *action = 0;
                return true;
            }
            if (btns == kIcomRC28ButtonPtt)
            {
                *button = 3;
                *action = 0;
                return true;
            }
        }
    }

    if (btns == kIcomRC28ButtonsIdle && speed > 0 && (dir == 0x01 || dir == 0x02))
    {
        *steps = (dir == 0x01) ? static_cast<int>(speed) : -static_cast<int>(speed);
        return true;
    }

    return false;
}

bool IcomRC28Manager::open()
{
    if (m_device.load(std::memory_order_relaxed))
    {
        close();
    }

    // Block when more than one physical RC-28 is present. Compare serial first,
    // then fall back to hidapi path because the RC-28 exposes no serial on Linux.
    bool multiplePhysical = false;
    if (auto* info = hid_enumerate(kIcomRC28Vid, kIcomRC28Pid))
    {
        QString firstKey;
        bool firstSeen = false;
        for (auto* cur = info; cur; cur = cur->next)
        {
            const QString key = (cur->serial_number && cur->serial_number[0] != L'\0')
                                    ? QStringLiteral("sn:") + QString::fromWCharArray(cur->serial_number)
                                    : QStringLiteral("path:") + QString::fromLatin1(cur->path ? cur->path : "");
            if (!firstSeen)
            {
                firstKey = key;
                firstSeen = true;
            }
            else if (key != firstKey)
            {
                multiplePhysical = true;
                break;
            }
        }
        hid_free_enumeration(info);
    }

    if (multiplePhysical)
    {
        if (!m_multipleDetected.load(std::memory_order_acquire))
        {
            m_blockedDeviceName = QStringLiteral("Icom RC-28 Remote Encoder");
            m_multipleDetected.store(true, std::memory_order_release);
            emit multipleDevicesDetected(m_blockedDeviceName);
        }
        return false;
    }

    m_blockedDeviceName.clear();
    m_multipleDetected.store(false, std::memory_order_release);

    hid_device* device = hid_open(kIcomRC28Vid, kIcomRC28Pid, nullptr);
    if (!device)
    {
        return false;
    }

    if (hid_set_nonblocking(device, 1) != 0)
    {
        qWarning(logIcomRC28()).noquote() << "Could not configure the RC-28 for nonblocking reads";
        hid_close(device);
        return false;
    }
    m_device.store(device, std::memory_order_release);
    m_deviceName = QStringLiteral("Icom RC-28 Remote Encoder");
    m_devicePath.clear();
    m_serialNumber.clear();
    if (auto* info = hid_enumerate(kIcomRC28Vid, kIcomRC28Pid))
    {
        m_devicePath = QString::fromLatin1(info->path ? info->path : "");
        m_serialNumber = info->serial_number ? QString::fromWCharArray(info->serial_number) : QString{};
        hid_free_enumeration(info);
    }

    m_pollTimer->start();
    m_hotplugTimer->stop();
    m_prevButtons = kIcomRC28ButtonsIdle;
    emit connectionChanged(true, m_deviceName);
    qInfo(logIcomRC28()).noquote() << "RC-28 opened";
    return true;
}

void IcomRC28Manager::close()
{
    m_pollTimer->stop();
    m_hotplugTimer->stop();
    m_prevButtons = kIcomRC28ButtonsIdle;

    hid_device* device = m_device.exchange(nullptr);
    if (device)
    {
        if (!writeLeds(device, kIcomRC28LedsOff))
        {
            qWarning(logIcomRC28()).noquote() << "Could not turn off RC-28 LEDs before closing the device";
        }
        hid_close(device);
    }

    if (!m_deviceName.isEmpty())
    {
        m_deviceName.clear();
        m_devicePath.clear();
        m_serialNumber.clear();
        emit connectionChanged(false, {});
    }
}

QByteArray IcomRC28Manager::ledReport(uint8_t ledByte)
{
    QByteArray report(33, '\0');
    report[1] = '\x01';
    report[2] = static_cast<char>(ledByte);
    return report;
}

bool IcomRC28Manager::writeLeds(hid_device* device, uint8_t ledByte)
{
    if (!device)
    {
        return false;
    }
    const QByteArray report = ledReport(ledByte);
    return hid_write(device, reinterpret_cast<const unsigned char*>(report.constData()), report.size()) ==
           report.size();
}

bool IcomRC28Manager::sendLeds(uint8_t ledByte)
{
    hid_device* device = m_device.load(std::memory_order_relaxed);
    if (!device)
    {
        return false;
    }
    return writeLeds(device, ledByte);
}

void IcomRC28Manager::setIcomRC28Leds(uint8_t ledByte)
{
    if (!sendLeds(ledByte) && isOpen())
    {
        qWarning(logIcomRC28()).noquote() << "Could not update RC-28 LEDs";
    }
}

void IcomRC28Manager::poll()
{
    hid_device* device = m_device.load(std::memory_order_relaxed);
    if (!device)
    {
        return;
    }

    while (true)
    {
        const int res = hid_read(device, m_buf, sizeof(m_buf));
        if (res < 0)
        {
            close();
            m_hotplugTimer->start();
            return;
        }
        if (res == 0)
        {
            break;
        }

        int steps = 0;
        int button = 0;
        int action = 0;
        if (parseIcomRC28Report(m_buf, static_cast<size_t>(res), &steps, &button, &action))
        {
            if (steps != 0)
            {
                emit tuneSteps(steps);
            }
            else if (button != 0)
            {
                emit buttonPressed(button, action);
            }
        }
    }
}

void IcomRC28Manager::hotplugCheck()
{
    if (!m_device.load(std::memory_order_relaxed))
    {
        if (open())
        {
            m_hotplugTimer->stop();
        }
    }
}

void IcomRC28Manager::loadSettings()
{
    if (isOpen())
    {
        return;
    }

    if (!open())
    {
        m_hotplugTimer->start();
    }
}

#endif
