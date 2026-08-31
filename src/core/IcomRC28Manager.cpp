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
// RC-28 HID report byte 5 is an active-low button mask. These bit positions
// are established by the observed single-button values: idle=0x07, F1=0x05,
// F2=0x03, and PTT=0x06. Parsing changed bits rather than comparing the whole
// byte preserves releases when an operator rolls directly between buttons or
// holds PTT while using F1/F2.
constexpr uint8_t kIcomRC28ButtonBitPtt = 0x01;
constexpr uint8_t kIcomRC28ButtonBitF1 = 0x02;
constexpr uint8_t kIcomRC28ButtonBitF2 = 0x04;

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

QString IcomRC28Manager::normalizedSerialNumber(const QString& descriptorSerial)
{
    QString serial = descriptorSerial.trimmed();

    // The RC-28 HID serial descriptor on macOS prefixes the unit's unique
    // serial with the product name, for example "RC-28 01234567". Device name
    // is already reported separately, so exposing that descriptor verbatim in
    // the Settings panel makes the Serial field repeat product information.
    // Normalize only the known RC-28 descriptor form; an unfamiliar descriptor
    // remains intact so future firmware or platform formats are not truncated.
    constexpr QLatin1StringView productName("RC-28");
    if (serial == productName)
    {
        return {};
    }
    constexpr QLatin1StringView productPrefix("RC-28 ");
    if (serial.startsWith(productPrefix))
    {
        serial = serial.sliced(productPrefix.size()).trimmed();
    }
    return serial;
}

QVector<IcomRC28Manager::ButtonTransition> IcomRC28Manager::buttonTransitions(uint8_t previous, uint8_t current)
{
    QVector<ButtonTransition> transitions;
    const auto appendIfChanged = [&transitions, previous, current](uint8_t bit, int button)
    {
        const bool wasPressed = (previous & bit) == 0;
        const bool isPressed = (current & bit) == 0;
        if (wasPressed != isPressed)
        {
            transitions.append(ButtonTransition{button, isPressed ? 0 : 1});
        }
    };

    // Process PTT first so a release cannot be delayed behind a simultaneous
    // function-button transition.
    appendIfChanged(kIcomRC28ButtonBitPtt, 3);
    appendIfChanged(kIcomRC28ButtonBitF1, 1);
    appendIfChanged(kIcomRC28ButtonBitF2, 2);
    return transitions;
}

bool IcomRC28Manager::parseIcomRC28TuningReport(const uint8_t* buf, size_t len, int* steps) const
{
    if (len < 6 || buf[0] != kIcomRC28ReportGuard)
    {
        return false;
    }

    const uint8_t speed = buf[1];
    const uint8_t dir = buf[3];
    if (buf[5] == kIcomRC28ButtonsIdle && speed > 0 && (dir == 0x01 || dir == 0x02))
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
        m_serialNumber =
            normalizedSerialNumber(info->serial_number ? QString::fromWCharArray(info->serial_number) : QString{});
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

    // Publish the closed state before release handlers run. A release can
    // update LED state through the controller; isOpen() must already be false
    // so that path never writes to a HID handle whose preceding read failed.
    hid_device* device = m_device.exchange(nullptr);

    // A USB removal can make hid_read fail before the device reports its
    // final active-low release state. Synthesize every outstanding release
    // before forgetting the mask; PTT is deliberately first in the transition
    // list so loss of the controller cannot leave the radio keyed.
    const QVector<ButtonTransition> releases = buttonTransitions(m_prevButtons, kIcomRC28ButtonsIdle);
    m_prevButtons = kIcomRC28ButtonsIdle;
    for (const ButtonTransition& release : releases)
    {
        emit buttonPressed(release.button, release.action);
    }

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
        if (static_cast<size_t>(res) >= 6 && m_buf[0] == kIcomRC28ReportGuard)
        {
            const uint8_t buttons = m_buf[5];
            const QVector<ButtonTransition> transitions = buttonTransitions(m_prevButtons, buttons);
            m_prevButtons = buttons;
            for (const ButtonTransition& transition : transitions)
            {
                emit buttonPressed(transition.button, transition.action);
            }
        }
        if (parseIcomRC28TuningReport(m_buf, static_cast<size_t>(res), &steps))
        {
            emit tuneSteps(steps);
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
