#ifdef HAVE_HIDAPI
#include "RC28MappingDialog.h"

#include "core/Rc28Manager.h"
#include "FramelessTitleBar.h"

#include <algorithm>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <iterator>

namespace
{
struct ActionItem
{
    const char* id;
    const char* label;
};

constexpr ActionItem kActions[] = {
    {"None", "None"},
    {"CycleStep", "Cycle step"},
    {"ToggleMute", "Toggle mute"},
    {"ToggleLock", "Toggle lock"},
    {"ToggleRit", "Toggle RIT"},
    {"CycleMode", "Cycle mode"},
};

void populateActionCombo(QComboBox* combo, const QString& current)
{
    if (combo->count() == 0)
    {
        for (const auto& item : kActions)
        {
            combo->addItem(QString::fromLatin1(item.label), QString::fromLatin1(item.id));
        }
    }

    const int index = combo->findData(current);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

QString actionLabel(const QString& actionId)
{
    const auto it = std::find_if(std::begin(kActions), std::end(kActions),
                                 [&actionId](const ActionItem& item) { return actionId == QLatin1String(item.id); });
    return it != std::end(kActions) ? QString::fromLatin1(it->label) : actionId;
}
} // namespace

RC28MappingDialog::RC28MappingDialog(Rc28Manager* manager, QWidget* parent) : QDialog(parent), m_manager(manager)
{
    setWindowTitle(QStringLiteral("Icom RC-28 Remote Encoder"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setStyleSheet(QStringLiteral("RC28MappingDialog { background: %1; border: 1px solid %2; }")
                      .arg(QLatin1String(UiTheme::Color::Panel), QLatin1String(UiTheme::Color::Border)));
    setMinimumWidth(640);
    buildUi();
    loadSettings();
    refreshDeviceInfo();

    if (m_manager)
    {
        connect(m_manager, &Rc28Manager::connectionChanged, this,
                [this](bool connected, const QString& deviceName)
                {
                    Q_UNUSED(deviceName)
                    refreshDeviceInfo();
                    appendLog(connected ? QStringLiteral("Connected") : QStringLiteral("Disconnected"));
                });
        connect(m_manager, &Rc28Manager::multipleDevicesDetected, this,
                [this](const QString& deviceName)
                {
                    appendLog(QStringLiteral("Multiple devices detected: %1").arg(deviceName));
                    refreshDeviceInfo();
                });
        connect(m_manager, &Rc28Manager::tuneSteps, this,
                [this](int steps) { appendLog(QStringLiteral("Tune steps: %1").arg(steps)); });
        connect(m_manager, &Rc28Manager::buttonPressed, this,
                [this](int button, int action)
                {
                    const QString actionName = action == 0 ? QStringLiteral("press") : QStringLiteral("release");
                    appendLog(QStringLiteral("Button %1 %2").arg(button).arg(actionName));
                });
    }
}

void RC28MappingDialog::buildUi()
{
    auto* titleBar = new FramelessTitleBar(windowTitle(), this);
    connect(titleBar->closeButton(), &QPushButton::clicked, this, &QDialog::reject);

    auto* content = new QWidget(this);
    auto* root = new QVBoxLayout(content);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);
    outerLayout->addWidget(titleBar);
    outerLayout->addWidget(content, 1);

    auto* deviceGroup = new QGroupBox(QStringLiteral("Device"), content);
    auto* deviceForm = new QFormLayout(deviceGroup);
    deviceForm->setLabelAlignment(Qt::AlignRight);

    m_statusLabel = new QLabel(QStringLiteral("Not connected"), deviceGroup);
    m_deviceNameLabel = new QLabel(QStringLiteral("-"), deviceGroup);
    m_devicePathLabel = new QLabel(QStringLiteral("-"), deviceGroup);
    m_serialLabel = new QLabel(QStringLiteral("-"), deviceGroup);
    m_releaseLabel = new QLabel(QStringLiteral("-"), deviceGroup);
    for (auto* label : {m_statusLabel, m_deviceNameLabel, m_devicePathLabel, m_serialLabel, m_releaseLabel})
    {
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }

    deviceForm->addRow(QStringLiteral("Status:\t"), m_statusLabel);
    deviceForm->addRow(QStringLiteral("Name:\t"), m_deviceNameLabel);
    deviceForm->addRow(QStringLiteral("Path:\t"), m_devicePathLabel);
    deviceForm->addRow(QStringLiteral("Serial:\t"), m_serialLabel);
    deviceForm->addRow(QStringLiteral("Release:\t"), m_releaseLabel);
    root->addWidget(deviceGroup);

    auto* mapGroup = new QGroupBox(QStringLiteral("Button Mapping"), content);
    auto* mapForm = new QFormLayout(mapGroup);
    m_f1PressCombo = new QComboBox(mapGroup);
    m_f1HoldCombo = new QComboBox(mapGroup);
    m_f2PressCombo = new QComboBox(mapGroup);
    m_f2HoldCombo = new QComboBox(mapGroup);
    m_pttModeCombo = new QComboBox(mapGroup);
    for (auto* combo : {m_f1PressCombo, m_f1HoldCombo, m_f2PressCombo, m_f2HoldCombo, m_pttModeCombo})
    {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    }
    populateActionCombo(m_f1PressCombo, QStringLiteral("None"));
    populateActionCombo(m_f1HoldCombo, QStringLiteral("None"));
    populateActionCombo(m_f2PressCombo, QStringLiteral("None"));
    populateActionCombo(m_f2HoldCombo, QStringLiteral("None"));

    m_pttModeCombo->addItem(QStringLiteral("Disabled"), QStringLiteral("Disabled"));
    m_pttModeCombo->addItem(QStringLiteral("Momentary"), QStringLiteral("Momentary"));
    m_pttModeCombo->addItem(QStringLiteral("Latched"), QStringLiteral("Latched"));

    mapForm->addRow(QStringLiteral("F-1 (Short Press):\t"), m_f1PressCombo);
    mapForm->addRow(QStringLiteral("F-1 (Long Press):\t"), m_f1HoldCombo);
    mapForm->addRow(QStringLiteral("F-2 (Short Press):\t"), m_f2PressCombo);
    mapForm->addRow(QStringLiteral("F-2 (Long Press):\t"), m_f2HoldCombo);
    mapForm->addRow(QStringLiteral("Transmit:\t"), m_pttModeCombo);
    root->addWidget(mapGroup);

    auto* logGroup = new QGroupBox(QStringLiteral("Activity Log"), content);
    auto* logLayout = new QVBoxLayout(logGroup);
    m_logView = new QPlainTextEdit(logGroup);
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(200);
    logLayout->addWidget(m_logView);
    root->addWidget(logGroup, 1);

    connect(m_f1PressCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { saveActionField(QStringLiteral("f1Press"), m_f1PressCombo->currentData().toString()); });
    connect(m_f1HoldCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { saveActionField(QStringLiteral("f1Hold"), m_f1HoldCombo->currentData().toString()); });
    connect(m_f2PressCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { saveActionField(QStringLiteral("f2Press"), m_f2PressCombo->currentData().toString()); });
    connect(m_f2HoldCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { saveActionField(QStringLiteral("f2Hold"), m_f2HoldCombo->currentData().toString()); });
    connect(m_pttModeCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { savePttMode(m_pttModeCombo->currentData().toString()); });
}

void RC28MappingDialog::refreshDeviceInfo()
{
    if (!m_manager)
    {
        return;
    }

    const bool open = m_manager->isOpen();
    const bool blocked = m_manager->isBlockedByMultiple();
    const bool detected = !open && !blocked && !Rc28Manager::detectDevice().isEmpty();
    if (m_statusLabel)
    {
        m_statusLabel->setText(blocked ? QStringLiteral("Multiple devices detected")
                               : open  ? QStringLiteral("Connected")
                                       : (detected ? QStringLiteral("Detected") : QStringLiteral("Not connected")));
    }
    if (m_deviceNameLabel)
    {
        m_deviceNameLabel->setText(
            blocked ? m_manager->blockedDeviceName()
                    : (m_manager->deviceName().isEmpty() ? QStringLiteral("-") : m_manager->deviceName()));
    }
    if (m_devicePathLabel)
    {
        m_devicePathLabel->setText(m_manager->devicePath().isEmpty() ? QStringLiteral("-") : m_manager->devicePath());
    }
    if (m_serialLabel)
    {
        m_serialLabel->setText(m_manager->serialNumber().isEmpty() ? QStringLiteral("-") : m_manager->serialNumber());
    }
    if (m_releaseLabel)
    {
        m_releaseLabel->setText(m_manager->releaseNumber() > 0
                                    ? QStringLiteral("0x%1").arg(m_manager->releaseNumber(), 4, 16, QLatin1Char('0'))
                                    : QStringLiteral("-"));
    }
}

void RC28MappingDialog::appendLog(const QString& text)
{
    if (!m_logView)
    {
        return;
    }

    const QString line =
        QStringLiteral("[%1] %2").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), text);
    m_logView->appendPlainText(line);
}

void RC28MappingDialog::loadSettings()
{
    const QSignalBlocker blockF1Press(m_f1PressCombo);
    const QSignalBlocker blockF1Hold(m_f1HoldCombo);
    const QSignalBlocker blockF2Press(m_f2PressCombo);
    const QSignalBlocker blockF2Hold(m_f2HoldCombo);
    const QSignalBlocker blockPtt(m_pttModeCombo);

    populateActionCombo(m_f1PressCombo, Rc28Manager::settingsField(QStringLiteral("f1Press"), QStringLiteral("None")));
    populateActionCombo(m_f1HoldCombo, Rc28Manager::settingsField(QStringLiteral("f1Hold"), QStringLiteral("None")));
    populateActionCombo(m_f2PressCombo, Rc28Manager::settingsField(QStringLiteral("f2Press"), QStringLiteral("None")));
    populateActionCombo(m_f2HoldCombo, Rc28Manager::settingsField(QStringLiteral("f2Hold"), QStringLiteral("None")));

    const QString pttMode = Rc28Manager::settingsField(QStringLiteral("pttMode"), QStringLiteral("Disabled"));
    const int pttIndex = m_pttModeCombo->findData(pttMode);
    m_pttModeCombo->setCurrentIndex(pttIndex >= 0 ? pttIndex : 0);
}

void RC28MappingDialog::saveActionField(const QString& field, const QString& value)
{
    Rc28Manager::setSettingsField(field, value);
    appendLog(QStringLiteral("%1 -> %2").arg(field, actionLabel(value)));
}

void RC28MappingDialog::savePttMode(const QString& mode)
{
    Rc28Manager::setSettingsField(QStringLiteral("pttMode"), mode);
    appendLog(QStringLiteral("pttMode -> %1").arg(mode));
}

#endif
