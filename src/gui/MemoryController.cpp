#include "MemoryController.h"

#include "AppSettings.h"
#include "DialogPlacement.h"
#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "MemoryPanel.h"
#include "RadioCapabilities.h"
#include "VfoPanel.h"
#include "UtilityWindow.h"
#include "models/RadioModel.h"
#include "models/VfoModel.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <algorithm>
#include <cstring>
#include <initializer_list>

using namespace sdr9700::ui::main_window;

namespace
{
constexpr quint16 kRadioMemoryFirstGroup = 1;
constexpr quint16 kRadioMemoryLastGroup = 3;
constexpr quint16 kRadioMemoryFirstChannel = 1;
constexpr quint16 kRadioMemoryLastChannel = 99;
constexpr int kRadioMemorySyncTotal =
    (kRadioMemoryLastGroup - kRadioMemoryFirstGroup + 1) * (kRadioMemoryLastChannel - kRadioMemoryFirstChannel + 1);
constexpr int kRadioMemoryRefreshIntervalMs = 25;
constexpr int kRadioMemoryPeriodicRefreshMs = 120000;
constexpr int kRadioMemorySyncTimeoutMs = 30000;
constexpr int kRadioMemoryWriteIntervalMs = 100;
constexpr int kRadioMemoryNameMaxChars = 16;
constexpr int kMemoryEditorPaneWidth = 420;
constexpr int kMemoryEditorFieldHeight = 30;
constexpr int kMemoryEditorGutter = 10;
constexpr auto kMemoryFileFilter = "SDR9700 Memories (*.csv);;CSV Files (*.csv);;All Files (*)";
constexpr auto kMemoryBackupFilter = "SDR9700 memory backups (sdr9700-memories-backup-*.json);;JSON Files (*.json)";

enum MemoryToneFamily
{
    MemoryToneOff = 0,
    MemoryToneTone,
    MemoryToneDtcs
};

QSize memoryManagerWindowSize()
{
    return QSize(kMemoryWindowSize.width() + kMemoryEditorPaneWidth + kMemoryEditorGutter + (kMemoryPanelSpacing * 2) +
                     1,
                 kMemoryWindowSize.height());
}

MemoryToneFamily memoryToneFamilyForMode(rptAccessTxRx_t mode)
{
    if (isDtcsToneMode(mode))
    {
        return MemoryToneDtcs;
    }
    if (mode == ratrTN || mode == ratrNT || mode == ratrTT || mode == ratrTD)
    {
        return MemoryToneTone;
    }
    return MemoryToneOff;
}

quint32 radioMemoryKey(quint16 group, quint16 channel)
{
    return (static_cast<quint32>(group) << 16) | static_cast<quint32>(channel);
}

QString radioMemoryId(quint16 group, quint16 channel)
{
    return QStringLiteral("radio:%1:%2").arg(group).arg(channel, 3, 10, QLatin1Char('0'));
}

QString radioMemoryName(const MemoryType& memory)
{
    int length = 0;
    while (length < static_cast<int>(sizeof(memory.name)) && memory.name[length] != '\0')
    {
        ++length;
    }
    return QString::fromLatin1(memory.name, length).trimmed();
}

QString memoryCharField(const char* field, int size)
{
    int length = 0;
    while (length < size && field[length] != '\0')
    {
        ++length;
    }
    return QString::fromLatin1(field, length).trimmed();
}

bool radioMemoryIsStored(const MemoryType& memory)
{
    return !memory.del && memory.frequency.Hz > 0;
}

quint16 radioMemoryGroupForHz(quint64 hz)
{
    const availableBands band = sdr9700::radioBandForFrequency(hz);
    if (const sdr9700::RadioBandDef* def = sdr9700::radioBandDefinition(band))
    {
        if (def->memGroup >= kRadioMemoryFirstGroup && def->memGroup <= kRadioMemoryLastGroup)
        {
            return static_cast<quint16>(def->memGroup);
        }
    }
    return kRadioMemoryFirstGroup;
}

int flattenedRadioMemoryNumber(quint16 group, quint16 channel)
{
    return static_cast<int>(group) * 100 + static_cast<int>(channel);
}

int recordDuplexModeFromRadio(quint8 duplex)
{
    switch (duplex)
    {
    case 1:
        return dmDupMinus;
    case 2:
        return dmDupPlus;
    default:
        return dmSimplex;
    }
}

quint64 defaultOffsetForModeAndHz(duplexMode_t mode, quint64 hz)
{
    for (const OffsetPreset& preset : offsetPresetsForHz(hz))
    {
        if (preset.mode == mode)
        {
            return preset.hz;
        }
    }
    return 0;
}

quint64 normalizedOffsetForModeAndHz(duplexMode_t mode, quint64 rawOffsetHz, quint64 receiveHz)
{
    if (mode != dmDupMinus && mode != dmDupPlus)
    {
        return 0;
    }

    for (const OffsetPreset& preset : offsetPresetsForHz(receiveHz))
    {
        if (preset.mode != mode)
        {
            continue;
        }
        if (rawOffsetHz == preset.hz || rawOffsetHz * 100ULL == preset.hz)
        {
            return preset.hz;
        }
    }
    if (rawOffsetHz == 0)
    {
        return defaultOffsetForModeAndHz(mode, receiveHz);
    }
    return rawOffsetHz;
}

quint8 radioDuplexFromRecord(int duplexMode)
{
    switch (static_cast<duplexMode_t>(duplexMode))
    {
    case dmDupMinus:
        return 1;
    case dmDupPlus:
        return 2;
    default:
        return 0;
    }
}

ushort toneValueFromRadioText(const QString& text)
{
    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok)
    {
        return 0;
    }
    return static_cast<ushort>(value * 10.0 + 0.5);
}

QString modeLabelFromRegister(int mode)
{
    switch (static_cast<radioMode_t>(mode))
    {
    case modeLSB:
        return QStringLiteral("LSB");
    case modeUSB:
        return QStringLiteral("USB");
    case modeAM:
        return QStringLiteral("AM");
    case modeCW:
        return QStringLiteral("CW");
    case modeFM:
        return QStringLiteral("FM");
    case modeDV:
        return QStringLiteral("DV");
    default:
        return QStringLiteral("FM");
    }
}

int modeRegisterFromLabel(const QString& mode)
{
    if (mode == QLatin1String("LSB"))
    {
        return modeLSB;
    }
    if (mode == QLatin1String("USB"))
    {
        return modeUSB;
    }
    if (mode == QLatin1String("AM"))
    {
        return modeAM;
    }
    if (mode == QLatin1String("CW"))
    {
        return modeCW;
    }
    if (mode == QLatin1String("DV"))
    {
        return modeDV;
    }
    return modeFM;
}

MemoryRecord recordFromRadioMemory(const MemoryType& radioMemory)
{
    MemoryRecord memory;
    memory.id = radioMemoryId(radioMemory.group, radioMemory.channel);
    memory.number = flattenedRadioMemoryNumber(radioMemory.group, radioMemory.channel);
    memory.name = radioMemoryName(radioMemory);
    if (memory.name.isEmpty())
    {
        memory.name = memoryFrequencyLabel(radioMemory.frequency.Hz);
    }
    memory.receiveHz = radioMemory.frequency.Hz;
    memory.mode = radioMemory.mode;
    memory.filter = radioMemory.filter;
    memory.dataMode = radioMemory.datamode;
    memory.scan = radioMemory.scan;
    memory.band = sdr9700::radioBandShortLabel(sdr9700::radioBandForFrequency(memory.receiveHz));
    memory.bandKey = memoryBandKeyForHz(memory.receiveHz);
    memory.duplexMode = recordDuplexModeFromRadio(radioMemory.duplex);
    memory.offsetHz = normalizedOffsetForModeAndHz(static_cast<duplexMode_t>(memory.duplexMode),
                                                   radioMemory.duplexOffset.Hz, memory.receiveHz);
    memory.shift = offsetModeLabel(static_cast<duplexMode_t>(memory.duplexMode), memory.offsetHz);
    memory.toneMode = radioMemory.tonemode;
    memory.toneOption = toneOptionLabel(static_cast<rptAccessTxRx_t>(memory.toneMode));
    memory.tone = radioMemory.tone;
    memory.tsql = radioMemory.tsql;
    memory.dsql = radioMemory.dsql;
    memory.dtcs = radioMemory.dtcs;
    memory.dtcsPolarity = radioMemory.dtcsp;
    memory.dtcsB = radioMemory.dtcsB;
    memory.dtcsPolarityB = radioMemory.dtcspB;
    memory.dvSql = radioMemory.dvsql;
    memory.urCall = memoryCharField(radioMemory.UR, sizeof radioMemory.UR);
    memory.r1Call = memoryCharField(radioMemory.R1, sizeof radioMemory.R1);
    memory.r2Call = memoryCharField(radioMemory.R2, sizeof radioMemory.R2);
    if (isDtcsToneMode(static_cast<rptAccessTxRx_t>(memory.toneMode)))
    {
        memory.toneValue = memory.dtcs;
    }
    else if (memory.toneMode != ratrNN)
    {
        memory.toneValue = toneValueFromRadioText(!memory.tone.isEmpty() ? memory.tone : memory.tsql);
    }
    memory.toneFrequency = memoryToneFrequencyLabel(static_cast<rptAccessTxRx_t>(memory.toneMode), memory.toneValue);
    return memory;
}

MemoryType radioMemoryFromRecord(const MemoryRecord& memory, quint16 group, quint16 channel)
{
    MemoryType radioMemory;
    radioMemory.group = group;
    radioMemory.channel = channel;
    radioMemory.scan = static_cast<quint8>(qBound(0, memory.scan, 3));
    radioMemory.frequency.Hz = memory.receiveHz;
    radioMemory.frequency.VFO = activeVFO;
    radioMemory.mode = static_cast<quint8>(memory.mode);
    radioMemory.filter = static_cast<quint8>(qBound(1, memory.filter, 3));
    radioMemory.datamode = static_cast<quint8>(qBound(0, memory.dataMode, 1));
    radioMemory.duplex = radioDuplexFromRecord(memory.duplexMode);
    radioMemory.tonemode = static_cast<quint8>(memory.toneMode);
    radioMemory.dsql = static_cast<quint8>(qBound(0, memory.dsql, 2));
    radioMemory.dtcsp = static_cast<quint8>(qBound(0, memory.dtcsPolarity, 3));
    radioMemory.dtcspB = static_cast<quint8>(qBound(0, memory.dtcsPolarityB, 3));
    radioMemory.dvsql = static_cast<quint8>(qBound(0, memory.dvSql, 99));
    radioMemory.duplexOffset.Hz = memory.offsetHz;
    radioMemory.duplexOffset.VFO = activeVFO;
    const auto toneMode = static_cast<rptAccessTxRx_t>(memory.toneMode);
    if (isDtcsToneMode(toneMode))
    {
        radioMemory.dtcs = memory.dtcs;
        radioMemory.dtcsB = memory.dtcsB;
    }
    else if (toneMode != ratrNN)
    {
        const QString fallbackTone = toneFrequencyLabel(memory.toneValue);
        radioMemory.tone = memory.tone.isEmpty() ? fallbackTone : memory.tone;
        radioMemory.tsql = memory.tsql.isEmpty() ? fallbackTone : memory.tsql;
    }
    const QByteArray name = memory.name.toLatin1().left(kRadioMemoryNameMaxChars);
    std::copy(name.cbegin(), name.cend(), radioMemory.name);
    const QByteArray ur = memory.urCall.toLatin1().left(sizeof radioMemory.UR);
    const QByteArray r1 = memory.r1Call.toLatin1().left(sizeof radioMemory.R1);
    const QByteArray r2 = memory.r2Call.toLatin1().left(sizeof radioMemory.R2);
    std::copy(ur.cbegin(), ur.cend(), radioMemory.UR);
    std::copy(r1.cbegin(), r1.cend(), radioMemory.R1);
    std::copy(r2.cbegin(), r2.cend(), radioMemory.R2);
    return radioMemory;
}

MemoryType deletedRadioMemory(quint16 group, quint16 channel)
{
    MemoryType memory;
    memory.group = group;
    memory.channel = channel;
    memory.del = true;
    return memory;
}

QString backupDirectoryPath()
{
    return QDir(QFileInfo(AppSettings::configPath()).absolutePath()).filePath(QStringLiteral("backups"));
}

QString memoryBackupPath()
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    return QDir(backupDirectoryPath()).filePath(QStringLiteral("sdr9700-memories-backup-%1.json").arg(timestamp));
}

QString memoryToneTableLabel(const MemoryRecord& memory)
{
    const auto toneMode = static_cast<rptAccessTxRx_t>(memory.toneMode);
    if (toneMode == ratrNN)
    {
        return QStringLiteral("OFF");
    }

    return isDtcsToneMode(toneMode) ? QStringLiteral("DTCS") : toneOptionLabel(toneMode).toUpper();
}

QString memoryToneFrequencyTableValue(const MemoryRecord& memory)
{
    const auto toneMode = static_cast<rptAccessTxRx_t>(memory.toneMode);
    if (toneMode == ratrNN)
    {
        return QStringLiteral("-");
    }

    if (isDtcsToneMode(toneMode))
    {
        auto polarityLabel = [](int polarityValue)
        { return polarityValue == 3 ? QStringLiteral("R") : QStringLiteral("N"); };
        if (toneMode == ratrDD || toneMode == ratrDT)
        {
            return QStringLiteral("%1 / %2").arg(
                QStringLiteral("%1%2").arg(dtcsCodeLabel(memory.dtcsB), polarityLabel(memory.dtcsPolarityB)),
                QStringLiteral("%1%2").arg(dtcsCodeLabel(memory.dtcs), polarityLabel(memory.dtcsPolarity)));
        }
        return QStringLiteral("- / %1%2").arg(dtcsCodeLabel(memory.dtcs), polarityLabel(memory.dtcsPolarity));
    }
    if (toneMode == ratrNT)
    {
        return QStringLiteral("%1 / -").arg(memory.tsql.isEmpty() ? memory.tone : memory.tsql);
    }
    if (toneMode == ratrTT)
    {
        return QStringLiteral("%1 / %2").arg(memory.tsql.isEmpty() ? memory.tone : memory.tsql,
                                             memory.tone.isEmpty() ? memory.tsql : memory.tone);
    }
    if (toneMode == ratrTN)
    {
        return QStringLiteral("- / %1").arg(memory.tone.isEmpty() ? memory.tsql : memory.tone);
    }

    const QString value = memoryToneFrequencyLabel(toneMode, memory.toneValue);
    return value.isEmpty() ? QStringLiteral("-") : value;
}

QString memoryFilterLabel(int filter)
{
    if (filter >= 1 && filter <= 3)
    {
        return QStringLiteral("FIL%1").arg(filter);
    }
    return QString::number(filter);
}

QString memoryScanGroupLabel(int scan)
{
    switch (scan)
    {
    case 1:
        return QStringLiteral("Group 1");
    case 2:
        return QStringLiteral("Group 2");
    case 3:
        return QStringLiteral("Group 3");
    default:
        return QStringLiteral("OFF");
    }
}

bool modeSupportsMemoryOffset(int mode)
{
    return mode == modeFM || mode == modeDV || mode == modeDD;
}
} // namespace

MemoryController::MemoryController(MainWindow* window) : QObject(window), m_window(window)
{
    m_radioMemoryRefreshTimer = new QTimer(this);
    m_radioMemoryRefreshTimer->setInterval(kRadioMemoryRefreshIntervalMs);
    connect(m_radioMemoryRefreshTimer, &QTimer::timeout, this, &MemoryController::requestNextRadioMemory);

    m_radioMemoryPeriodicRefreshTimer = new QTimer(this);
    m_radioMemoryPeriodicRefreshTimer->setInterval(kRadioMemoryPeriodicRefreshMs);
    connect(m_radioMemoryPeriodicRefreshTimer, &QTimer::timeout, this, &MemoryController::requestRadioMemoryRefresh);

    m_radioMemorySyncTimeoutTimer = new QTimer(this);
    m_radioMemorySyncTimeoutTimer->setSingleShot(true);
    connect(m_radioMemorySyncTimeoutTimer, &QTimer::timeout, this,
            [this]()
            {
                if (m_refreshInProgress)
                {
                    finishRadioMemoryRefresh(true);
                }
            });

    connect(m_window->m_model, &RadioModel::radioMemoryReceived, this, &MemoryController::handleRadioMemoryReceived);
    connect(m_window->m_model, &RadioModel::readyChanged, this,
            [this](bool ready)
            {
                if (ready)
                {
                    requestRadioMemoryRefresh();
                    m_radioMemoryPeriodicRefreshTimer->start();
                    return;
                }
                m_radioMemoryRefreshTimer->stop();
                m_radioMemoryPeriodicRefreshTimer->stop();
                finishRadioMemoryRefresh(false);
                m_radioMemoriesByKey.clear();
                m_receivedRadioMemoryKeys.clear();
                rebuildMemoryViews();
            });
}

void MemoryController::buildMemoryWindow()
{
    m_window->m_memoryWindow = new sdr9700::ui::UtilityWindow(QStringLiteral("Memory Manager"), m_window);
    m_window->m_memoryWindow->setStyleSheet(
        QStringLiteral("QDialog { background: %1; border: 1px solid %2; }")
            .arg(QLatin1String(UiTheme::Color::Panel), QLatin1String(UiTheme::Color::Border)));
    m_window->m_memoryWindow->setObjectName("memoryWindow");
    m_window->m_memoryWindow->setAttribute(Qt::WA_DeleteOnClose, false);
    m_window->m_memoryWindow->resize(kMemoryWindowSize);
    m_window->m_memoryWindow->setFixedSize(kMemoryWindowSize);

    auto* panel = new QWidget(m_window->m_memoryWindow);
    auto* root = new QHBoxLayout(panel);
    root->setContentsMargins(kMemoryPanelMargins);
    root->setSpacing(kMemoryPanelSpacing);

    auto* leftPane = new QWidget(panel);
    leftPane->setFixedWidth(kMemoryWindowSize.width() - kMemoryPanelMargins.left() - kMemoryPanelMargins.right());
    auto* leftRoot = new QVBoxLayout(leftPane);
    leftRoot->setContentsMargins(0, 0, kMemoryEditorGutter, 0);
    leftRoot->setSpacing(kMemoryPanelSpacing);

    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(kNoMargins);
    toolbar->setSpacing(kMemoryToolbarSpacing);

    auto* filterGroup = new QGroupBox(panel);
    auto* filterLayout = new QHBoxLayout(filterGroup);
    filterLayout->setContentsMargins(kMemoryToolbarGroupMargins);
    filterLayout->setSpacing(kMemoryToolbarGroupSpacing);
    m_window->m_memoryBandFilter = new QComboBox(panel);
    m_window->m_memoryBandFilter->addItem(QStringLiteral("All"), QString());
    for (const availableBands band : sdr9700::kRadioUiBandOrder)
    {
        const QString label = sdr9700::radioBandShortLabel(band);
        m_window->m_memoryBandFilter->addItem(label, label);
    }
    filterLayout->addWidget(m_window->m_memoryBandFilter);
    toolbar->addWidget(filterGroup);
    toolbar->addStretch(1);

    auto* syncGroup = new QGroupBox(panel);
    auto* syncLayout = new QHBoxLayout(syncGroup);
    syncLayout->setContentsMargins(kMemoryToolbarGroupMargins);
    syncLayout->setSpacing(kMemoryToolbarGroupSpacing);
    auto* syncButton = new QPushButton("Sync", panel);
    syncButton->setToolTip("Immediately read radio memories into SDR9700.");
    syncLayout->addWidget(syncButton);
    toolbar->addWidget(syncGroup);
    toolbar->addStretch(1);

    auto* reorderGroup = new QGroupBox(panel);
    auto* reorderLayout = new QHBoxLayout(reorderGroup);
    reorderLayout->setContentsMargins(kMemoryToolbarGroupMargins);
    reorderLayout->setSpacing(kMemoryToolbarGroupSpacing);
    auto* upButton = new QPushButton("Up", panel);
    auto* downButton = new QPushButton("Down", panel);
    reorderLayout->addWidget(upButton);
    reorderLayout->addWidget(downButton);
    toolbar->addWidget(reorderGroup);
    toolbar->addStretch(1);

    auto* memoryGroup = new QGroupBox(panel);
    auto* memoryLayout = new QHBoxLayout(memoryGroup);
    memoryLayout->setContentsMargins(kMemoryToolbarGroupMargins);
    memoryLayout->setSpacing(kMemoryToolbarGroupSpacing);
    auto* addButton = new QPushButton("Add", panel);
    auto* editButton = new QPushButton("Edit", panel);
    editButton->setCheckable(true);
    editButton->setStyleSheet(QStringLiteral("QPushButton:checked { background: %1; border: 1px solid %2; "
                                             "border-radius: 3px; color: %3; }")
                                  .arg(UiTheme::Color::AccentDark, UiTheme::Color::Accent, UiTheme::Color::TextBright));
    m_memoryEditButton = editButton;
    auto* copyButton = new QPushButton("Copy", panel);
    auto* removeButton = new QPushButton("Remove", panel);
    memoryLayout->addWidget(addButton);
    memoryLayout->addWidget(editButton);
    memoryLayout->addWidget(copyButton);
    memoryLayout->addWidget(removeButton);
    toolbar->addWidget(memoryGroup);
    toolbar->addStretch(1);

    auto* transferGroup = new QGroupBox(panel);
    auto* transferLayout = new QHBoxLayout(transferGroup);
    transferLayout->setContentsMargins(kMemoryToolbarGroupMargins);
    transferLayout->setSpacing(kMemoryToolbarGroupSpacing);
    auto* importButton = new QPushButton("Import", panel);
    auto* exportButton = new QPushButton("Export", panel);
    transferLayout->addWidget(importButton);
    transferLayout->addWidget(exportButton);
    toolbar->addWidget(transferGroup);
    leftRoot->addLayout(toolbar);

    m_window->m_memoryTable = new QTableWidget(panel);
    m_window->m_memoryTable->setObjectName(QStringLiteral("memoryManagerTable"));
    m_window->m_memoryTable->setColumnCount(kMemoryTableColumnCount);
    m_window->m_memoryTable->setHorizontalHeaderLabels(
        {QStringLiteral("Channel"), QStringLiteral("Name"), QStringLiteral("Frequency"), QStringLiteral("Offset"),
         QStringLiteral("Mode"), QStringLiteral("Tone"), QStringLiteral("RX / TX"), QStringLiteral("Filter"),
         QStringLiteral("Scan Group"), QStringLiteral("ID")});
    m_window->m_memoryTable->setColumnHidden(kMemoryIdColumn, true);
    m_window->m_memoryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_window->m_memoryTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_window->m_memoryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_window->m_memoryTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_window->m_memoryTable->setSortingEnabled(false);
    m_window->m_memoryTable->setDragDropMode(QAbstractItemView::NoDragDrop);
    m_window->m_memoryTable->setShowGrid(true);
    m_window->m_memoryTable->setGridStyle(Qt::SolidLine);
    m_window->m_memoryTable->setStyleSheet(QStringLiteral("QTableWidget#memoryManagerTable { gridline-color: %1; }"
                                                          "QTableWidget#memoryManagerTable::item { padding: 0 10px; "
                                                          "border: none; }")
                                               .arg(UiTheme::Color::Border));
    m_window->m_memoryTable->verticalHeader()->setVisible(false);
    m_window->m_memoryTable->horizontalHeader()->setStretchLastSection(false);
    m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryNumberColumn, QHeaderView::Interactive);
    m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryNameColumn, QHeaderView::Interactive);
    m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryFrequencyColumn, QHeaderView::Interactive);
    m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryDuplexColumn, QHeaderView::Interactive);
    m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryModeColumn, QHeaderView::Interactive);
    m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryToneColumn, QHeaderView::Interactive);
    m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryToneFrequencyColumn,
                                                                      QHeaderView::Interactive);
    m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryFilterColumn, QHeaderView::Interactive);
    m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryScanColumn, QHeaderView::Stretch);
    m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryIdColumn, QHeaderView::Fixed);
    m_window->m_memoryTable->setColumnWidth(kMemoryNumberColumn, kMemoryNumberColumnWidth);
    m_window->m_memoryTable->setColumnWidth(kMemoryNameColumn, kMemoryNameColumnWidth);
    m_window->m_memoryTable->setColumnWidth(kMemoryFrequencyColumn, kMemoryFrequencyColumnWidth);
    m_window->m_memoryTable->setColumnWidth(kMemoryDuplexColumn, kMemoryDuplexColumnWidth);
    m_window->m_memoryTable->setColumnWidth(kMemoryModeColumn, kMemoryModeColumnWidth);
    m_window->m_memoryTable->setColumnWidth(kMemoryToneColumn, kMemoryToneColumnWidth);
    m_window->m_memoryTable->setColumnWidth(kMemoryToneFrequencyColumn, kMemoryToneFrequencyColumnWidth);
    m_window->m_memoryTable->setColumnWidth(kMemoryFilterColumn, kMemorySmallColumnWidth);
    m_window->m_memoryTable->setColumnWidth(kMemoryScanColumn, kMemoryScanColumnWidth);
    leftRoot->addWidget(m_window->m_memoryTable, 1);

    m_memoryEditorSeparator = new QWidget(panel);
    m_memoryEditorSeparator->setFixedWidth(1);
    m_memoryEditorSeparator->setStyleSheet(
        QStringLiteral("QWidget { background: %1; }").arg(QLatin1String(UiTheme::Color::BorderMedium)));
    m_memoryEditorSeparator->hide();

    m_memoryEditorPane = new QWidget(panel);
    m_memoryEditorPane->setObjectName(QStringLiteral("memoryEditorPane"));
    m_memoryEditorPane->setFixedWidth(kMemoryEditorPaneWidth);
    m_memoryEditorPane->hide();

    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(kNoMargins);
    m_window->m_memoryCountLabel = new QLabel(panel);
    m_window->m_memoryCountLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_window->m_memoryCountLabel->setStyleSheet("QLabel { color: palette(mid); }");
    auto* closeButton = new QPushButton("Close", panel);
    footer->addWidget(m_window->m_memoryCountLabel, 1);
    footer->addWidget(closeButton);
    leftRoot->addSpacing(kMemoryEditorGutter);
    auto* leftFooterSeparator = new QWidget(panel);
    leftFooterSeparator->setFixedHeight(1);
    leftFooterSeparator->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    leftFooterSeparator->setStyleSheet(
        QStringLiteral("QWidget { background: %1; }").arg(QLatin1String(UiTheme::Color::BorderMedium)));
    leftRoot->addWidget(leftFooterSeparator);
    leftRoot->addSpacing(kMemoryEditorGutter);
    leftRoot->addLayout(footer);

    root->addWidget(leftPane, 1);
    root->addWidget(m_memoryEditorSeparator);
    root->addWidget(m_memoryEditorPane);

    auto* memTitleBar = new sdr9700::ui::UtilityTitleBar(QStringLiteral("Memory Manager"), m_window->m_memoryWindow);
    connect(memTitleBar->closeButton(), &QPushButton::clicked, m_window->m_memoryWindow, &QWidget::hide);
    connect(closeButton, &QPushButton::clicked, m_window->m_memoryWindow, &QWidget::hide);

    auto* windowLayout = new QVBoxLayout(m_window->m_memoryWindow);
    windowLayout->setContentsMargins(kNoMargins);
    windowLayout->setSpacing(0);
    windowLayout->addWidget(memTitleBar);
    windowLayout->addWidget(panel, 1);

    connect(m_window->m_memoryBandFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MemoryController::reloadMemoryTable);
    connect(syncButton, &QPushButton::clicked, this, &MemoryController::forceRadioMemorySync);
    connect(m_window->m_memoryTable, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int column)
            {
                Q_UNUSED(column)
                if (m_refreshInProgress)
                {
                    return;
                }
                const auto* idItem = m_window->m_memoryTable->item(row, kMemoryIdColumn);
                const QString memoryId = idItem ? idItem->text() : QString();
                if (!memoryId.isEmpty())
                {
                    selectMemoryById(memoryId, true);
                }
            });
    connect(m_window->m_memoryTable, &QTableWidget::cellClicked, this,
            [this](int row, int column)
            {
                Q_UNUSED(column)
                if (!m_memoryEditorPane || !m_memoryEditorPane->isVisible())
                {
                    return;
                }

                const auto* idItem = m_window->m_memoryTable->item(row, kMemoryIdColumn);
                const QString memoryId = idItem ? idItem->text() : QString();
                if (memoryId != m_openMemoryEditorId)
                {
                    closeMemoryEditorPane();
                }
            });
    connect(upButton, &QPushButton::clicked, this, &MemoryController::moveSelectedMemoryUp);
    connect(downButton, &QPushButton::clicked, this, &MemoryController::moveSelectedMemoryDown);
    connect(addButton, &QPushButton::clicked, this, &MemoryController::storeCurrentMemory);
    connect(editButton, &QPushButton::clicked, this, &MemoryController::editSelectedMemory);
    connect(copyButton, &QPushButton::clicked, this, &MemoryController::copySelectedMemory);
    connect(removeButton, &QPushButton::clicked, this, &MemoryController::removeSelectedMemory);
    connect(importButton, &QPushButton::clicked, this, &MemoryController::importRadioMemories);
    connect(exportButton, &QPushButton::clicked, this,
            [this]()
            {
                if (exportRadioMemories())
                {
                    m_window->showToast(QStringLiteral("Memories exported"));
                }
            });

    reloadMemoryTable();
}

void MemoryController::showMemoryWindow()
{
    if (!m_window->m_memoryWindow)
    {
        return;
    }
    reloadMemoryTable();
    static_cast<sdr9700::ui::UtilityWindow*>(m_window->m_memoryWindow)->showCentered();
}

QWidget* MemoryController::popupParent() const
{
    if (m_window && m_window->m_memoryWindow && m_window->m_memoryWindow->isVisible())
    {
        return m_window->m_memoryWindow;
    }
    return m_window;
}

void MemoryController::closeMemoryEditorPane(bool resizeWindow)
{
    if (!m_memoryEditorPane || !m_window || !m_window->m_memoryWindow)
    {
        return;
    }

    if (QLayout* layout = m_memoryEditorPane->layout())
    {
        while (QLayoutItem* item = layout->takeAt(0))
        {
            if (QWidget* widget = item->widget())
            {
                delete widget;
            }
            delete item;
        }
        delete layout;
    }

    m_memoryEditorPane->hide();
    if (m_memoryEditorSeparator)
    {
        m_memoryEditorSeparator->hide();
    }
    m_openMemoryEditorId.clear();
    if (m_memoryEditButton)
    {
        m_memoryEditButton->setChecked(false);
    }
    if (!resizeWindow)
    {
        return;
    }

    m_window->m_memoryWindow->setFixedSize(kMemoryWindowSize);
    if (m_window->m_memoryWindow->isVisible())
    {
        static_cast<sdr9700::ui::UtilityWindow*>(m_window->m_memoryWindow)->centerOnHost();
    }
}

void MemoryController::forceRadioMemorySync()
{
    if (!m_window->m_model || !m_window->m_model->isConnected())
    {
        QMessageBox::information(popupParent(), QStringLiteral("Sync Memories"),
                                 QStringLiteral("Connect to the radio before syncing memories."));
        return;
    }

    if (m_refreshInProgress)
    {
        finishRadioMemoryRefresh(false);
    }

    requestRadioMemoryRefresh();
    reloadMemoryTable();
    m_window->showToast(QStringLiteral("Radio memory sync started"));
}

void MemoryController::requestRadioMemoryRefresh()
{
    if (!m_window->m_model || !m_window->m_model->isConnected() || m_refreshInProgress)
    {
        return;
    }

    m_refreshGroup = kRadioMemoryFirstGroup;
    m_refreshChannel = kRadioMemoryFirstChannel;
    m_currentSyncGroup = 0;
    m_currentSyncChannel = 0;
    m_refreshInProgress = true;
    m_receivedRadioMemoryKeys.clear();
    m_radioMemorySyncTimeoutTimer->start(kRadioMemorySyncTimeoutMs);
    requestNextRadioMemory();
    if (!m_refreshInProgress)
    {
        return;
    }
    m_radioMemoryRefreshTimer->start();
    rebuildMemoryViews();
}

void MemoryController::requestNextRadioMemory()
{
    if (!m_window->m_model || !m_window->m_model->isConnected())
    {
        finishRadioMemoryRefresh(false);
        return;
    }

    if (m_refreshGroup > kRadioMemoryLastGroup)
    {
        finishRadioMemoryRefresh(false);
        return;
    }

    m_currentSyncGroup = m_refreshGroup;
    m_currentSyncChannel = m_refreshChannel;
    m_window->m_model->requestRadioMemory(m_refreshGroup, m_refreshChannel);
    ++m_refreshChannel;
    if (m_refreshChannel > kRadioMemoryLastChannel)
    {
        m_refreshChannel = kRadioMemoryFirstChannel;
        ++m_refreshGroup;
    }
}

void MemoryController::finishRadioMemoryRefresh(bool timedOut)
{
    m_radioMemoryRefreshTimer->stop();
    m_radioMemorySyncTimeoutTimer->stop();
    const bool wasInProgress = m_refreshInProgress;
    m_refreshInProgress = false;
    m_currentSyncGroup = 0;
    m_currentSyncChannel = 0;
    if (timedOut && wasInProgress)
    {
        m_window->showToast(QStringLiteral("Radio memory sync timed out"), 5000, MainWindow::ToastKind::Warning);
    }
    rebuildMemoryViews();
}

void MemoryController::updateMemoryTableInteraction()
{
    if (!m_window->m_memoryTable)
    {
        return;
    }

    const bool locked = m_refreshInProgress;
    m_window->m_memoryTable->setSelectionMode(locked ? QAbstractItemView::NoSelection
                                                     : QAbstractItemView::SingleSelection);
    m_window->m_memoryTable->setFocusPolicy(locked ? Qt::NoFocus : Qt::StrongFocus);
    m_window->m_memoryTable->setAttribute(Qt::WA_TransparentForMouseEvents, locked);
    if (QWidget* viewport = m_window->m_memoryTable->viewport())
    {
        viewport->setAttribute(Qt::WA_TransparentForMouseEvents, locked);
    }
}

void MemoryController::handleRadioMemoryReceived(MemoryType memory)
{
    if (memory.group < kRadioMemoryFirstGroup || memory.group > kRadioMemoryLastGroup ||
        memory.channel < kRadioMemoryFirstChannel || memory.channel > kRadioMemoryLastChannel)
    {
        return;
    }

    const quint32 key = radioMemoryKey(memory.group, memory.channel);
    m_receivedRadioMemoryKeys.insert(key);
    if (radioMemoryIsStored(memory))
    {
        m_radioMemoriesByKey.insert(key, memory);
    }
    else
    {
        m_radioMemoriesByKey.remove(key);
        if (m_window->m_activeMemoryId == radioMemoryId(memory.group, memory.channel))
        {
            m_window->clearActiveMemory();
        }
    }
    rebuildMemoryViews();
}

QVector<MemoryRecord> MemoryController::currentMemories() const
{
    QVector<MemoryRecord> memories;
    memories.reserve(m_radioMemoriesByKey.size());
    for (const MemoryType& radioMemory : m_radioMemoriesByKey)
    {
        memories.append(recordFromRadioMemory(radioMemory));
    }
    std::sort(memories.begin(), memories.end(),
              [](const MemoryRecord& left, const MemoryRecord& right)
              {
                  const auto parse = [](const QString& id)
                  {
                      quint16 group = 0;
                      quint16 channel = 0;
                      const QStringList parts = id.split(QLatin1Char(':'));
                      if (parts.size() == 3)
                      {
                          group = static_cast<quint16>(parts.at(1).toUInt());
                          channel = static_cast<quint16>(parts.at(2).toUInt());
                      }
                      return std::pair<quint16, quint16>(group, channel);
                  };
                  const auto [leftGroup, leftChannel] = parse(left.id);
                  const auto [rightGroup, rightChannel] = parse(right.id);
                  if (leftGroup == rightGroup)
                  {
                      return leftChannel < rightChannel;
                  }
                  return leftGroup < rightGroup;
              });
    return memories;
}

MemoryRecord MemoryController::memoryForId(const QString& id, bool* found) const
{
    if (found)
    {
        *found = false;
    }
    bool haveRadioMemory = false;
    const MemoryType radioMemory = radioMemoryForId(id, &haveRadioMemory);
    if (!haveRadioMemory)
    {
        return {};
    }
    if (found)
    {
        *found = true;
    }
    return recordFromRadioMemory(radioMemory);
}

MemoryType MemoryController::radioMemoryForId(const QString& id, bool* found) const
{
    if (found)
    {
        *found = false;
    }

    quint16 group = 0;
    quint16 channel = 0;
    if (!parseRadioMemoryId(id, &group, &channel))
    {
        return {};
    }

    const quint32 key = radioMemoryKey(group, channel);
    auto it = m_radioMemoriesByKey.constFind(key);
    if (it == m_radioMemoriesByKey.cend())
    {
        return {};
    }

    if (found)
    {
        *found = true;
    }
    return it.value();
}

bool MemoryController::parseRadioMemoryId(const QString& id, quint16* group, quint16* channel) const
{
    const QStringList parts = id.split(QLatin1Char(':'));
    if (parts.size() != 3 || parts.at(0) != QLatin1String("radio"))
    {
        return false;
    }

    bool groupOk = false;
    bool channelOk = false;
    const uint parsedGroup = parts.at(1).toUInt(&groupOk);
    const uint parsedChannel = parts.at(2).toUInt(&channelOk);
    if (!groupOk || !channelOk || parsedGroup < kRadioMemoryFirstGroup || parsedGroup > kRadioMemoryLastGroup ||
        parsedChannel < kRadioMemoryFirstChannel || parsedChannel > kRadioMemoryLastChannel)
    {
        return false;
    }

    if (group)
    {
        *group = static_cast<quint16>(parsedGroup);
    }
    if (channel)
    {
        *channel = static_cast<quint16>(parsedChannel);
    }
    return true;
}

void MemoryController::writeMemoryRecord(const MemoryRecord& memory, quint16 group, quint16 channel)
{
    if (!m_window->m_model || !m_window->m_model->isConnected())
    {
        return;
    }
    m_window->m_model->writeRadioMemory(radioMemoryFromRecord(memory, group, channel));
}

void MemoryController::deleteRadioMemory(quint16 group, quint16 channel)
{
    if (!m_window->m_model || !m_window->m_model->isConnected())
    {
        return;
    }
    m_window->m_model->writeRadioMemory(deletedRadioMemory(group, channel));
}

void MemoryController::queueRadioMemoryWrites(const QVector<MemoryType>& memories, int startDelayMs)
{
    if (!m_window->m_model || !m_window->m_model->isConnected())
    {
        return;
    }

    int delayMs = qMax(0, startDelayMs);
    for (const MemoryType& memory : memories)
    {
        QTimer::singleShot(delayMs, this,
                           [this, memory]()
                           {
                               if (m_window->m_model && m_window->m_model->isConnected())
                               {
                                   m_window->m_model->writeRadioMemory(memory);
                               }
                           });
        delayMs += kRadioMemoryWriteIntervalMs;
    }
}

bool MemoryController::firstOpenChannelForGroup(quint16 group, quint16* channel) const
{
    if (group < kRadioMemoryFirstGroup || group > kRadioMemoryLastGroup)
    {
        return false;
    }

    for (quint16 candidate = kRadioMemoryFirstChannel; candidate <= kRadioMemoryLastChannel; ++candidate)
    {
        const quint32 key = radioMemoryKey(group, candidate);
        if (!m_receivedRadioMemoryKeys.contains(key))
        {
            return false;
        }
        if (!m_radioMemoriesByKey.contains(key))
        {
            if (channel)
            {
                *channel = candidate;
            }
            return true;
        }
    }
    return false;
}

bool MemoryController::targetForMemoryFrequency(quint64 hz, quint16* group, quint16* channel) const
{
    const quint16 targetGroup = radioMemoryGroupForHz(hz);
    quint16 targetChannel = 0;
    if (!firstOpenChannelForGroup(targetGroup, &targetChannel))
    {
        return false;
    }
    if (group)
    {
        *group = targetGroup;
    }
    if (channel)
    {
        *channel = targetChannel;
    }
    return true;
}

int MemoryController::queueRecordsToRadio(const QVector<MemoryRecord>& records, int* skippedCount, int startDelayMs)
{
    QSet<quint32> occupied;
    for (auto it = m_radioMemoriesByKey.cbegin(); it != m_radioMemoriesByKey.cend(); ++it)
    {
        occupied.insert(it.key());
    }

    int queuedCount = 0;
    int skipped = 0;
    QVector<MemoryType> writes;
    writes.reserve(records.size());
    for (const MemoryRecord& memory : records)
    {
        const quint16 group = radioMemoryGroupForHz(memory.receiveHz);
        quint16 targetChannel = 0;
        bool foundChannel = false;
        for (quint16 channel = kRadioMemoryFirstChannel; channel <= kRadioMemoryLastChannel; ++channel)
        {
            const quint32 key = radioMemoryKey(group, channel);
            if (!m_receivedRadioMemoryKeys.contains(key))
            {
                break;
            }
            if (!occupied.contains(key))
            {
                targetChannel = channel;
                occupied.insert(key);
                foundChannel = true;
                break;
            }
        }

        if (!foundChannel)
        {
            ++skipped;
            continue;
        }

        writes.append(radioMemoryFromRecord(memory, group, targetChannel));
        ++queuedCount;
    }

    queueRadioMemoryWrites(writes, startDelayMs);

    if (skippedCount)
    {
        *skippedCount = skipped;
    }
    return queuedCount;
}

bool MemoryController::backupRadioMemories()
{
    const QString path = memoryBackupPath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(popupParent(), QStringLiteral("Backup Memories Failed"),
                             QStringLiteral("Memory backup failed. Could not create the backup file."));
        return false;
    }

    const QByteArray data = memoriesExportDocument(currentMemories()).toJson(QJsonDocument::Indented);
    if (file.write(data) != static_cast<qint64>(data.size()) || !file.commit())
    {
        QMessageBox::warning(popupParent(), QStringLiteral("Backup Memories Failed"),
                             QStringLiteral("Memory backup failed. Could not create the backup file."));
        return false;
    }

    QMessageBox::information(popupParent(), QStringLiteral("Backup Memories Successful"),
                             QStringLiteral("Memory backup successful.\n\nSaved to:\n%1").arg(path));
    return true;
}

void MemoryController::restoreRadioMemories()
{
    if (!m_window->m_model || !m_window->m_model->isConnected())
    {
        QMessageBox::information(popupParent(), QStringLiteral("Restore Memories"),
                                 QStringLiteral("Connect to the radio before restoring memories."));
        return;
    }
    if (m_refreshInProgress)
    {
        QMessageBox::information(popupParent(), QStringLiteral("Restore Memories"),
                                 QStringLiteral("Wait for the current radio memory sync to finish before restoring."));
        return;
    }

    const QString path = QFileDialog::getOpenFileName(popupParent(), QStringLiteral("Restore Memories"),
                                                      backupDirectoryPath(), QString::fromLatin1(kMemoryBackupFilter));
    if (path.isEmpty())
    {
        return;
    }

    if (QMessageBox::question(popupParent(), QStringLiteral("Restore Memories"),
                              QStringLiteral("Restore this memory backup to the radio?\n\n"
                                             "Current user memories in groups 1-3 will be cleared first.\n"
                                             "Factory scan-edge and call memories are left untouched.")) !=
        QMessageBox::Yes)
    {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::warning(popupParent(), QStringLiteral("Restore Memories Failed"),
                             QStringLiteral("Memory restore failed. Could not open the selected file."));
        return;
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError)
    {
        QMessageBox::warning(popupParent(), QStringLiteral("Restore Memories Failed"),
                             QStringLiteral("Memory restore failed. The selected file is not valid JSON."));
        return;
    }

    QVector<MemoryType> deletes;
    deletes.reserve((kRadioMemoryLastGroup - kRadioMemoryFirstGroup + 1) * kRadioMemoryLastChannel);
    for (quint16 group = kRadioMemoryFirstGroup; group <= kRadioMemoryLastGroup; ++group)
    {
        for (quint16 channel = kRadioMemoryFirstChannel; channel <= kRadioMemoryLastChannel; ++channel)
        {
            deletes.append(deletedRadioMemory(group, channel));
        }
    }
    queueRadioMemoryWrites(deletes);
    m_radioMemoriesByKey.clear();

    int skipped = 0;
    const int restoreStartDelayMs = deletes.size() * kRadioMemoryWriteIntervalMs + 500;
    const int queued = queueRecordsToRadio(memoriesFromDocument(doc), &skipped, restoreStartDelayMs);
    QMessageBox::information(
        popupParent(), QStringLiteral("Restore Memories Successful"),
        QStringLiteral("Queued %1 memories for radio restore.%2")
            .arg(queued)
            .arg(skipped > 0 ? QStringLiteral("\n\nSkipped %1 memories because the target group is full or not synced.")
                                   .arg(skipped)
                             : QString()));
    QTimer::singleShot(restoreStartDelayMs + queued * kRadioMemoryWriteIntervalMs + 1000, this,
                       &MemoryController::requestRadioMemoryRefresh);
}

bool MemoryController::exportRadioMemories()
{
    const QString path =
        QFileDialog::getSaveFileName(popupParent(), QStringLiteral("Export Memories"),
                                     QStringLiteral("sdr9700-memories.csv"), QString::fromLatin1(kMemoryFileFilter));
    if (path.isEmpty())
    {
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(popupParent(), QStringLiteral("Export Memories Failed"),
                             QStringLiteral("Memory export failed. Could not save the selected file."));
        return false;
    }

    const QByteArray data = memoriesExportCsv(currentMemories());
    if (file.write(data) != static_cast<qint64>(data.size()) || !file.commit())
    {
        QMessageBox::warning(popupParent(), QStringLiteral("Export Memories Failed"),
                             QStringLiteral("Memory export failed. Could not save the selected file."));
        return false;
    }

    QMessageBox::information(popupParent(), QStringLiteral("Export Memories Successful"),
                             QStringLiteral("Memory export successful.\n\nSaved to:\n%1").arg(path));
    return true;
}

void MemoryController::importRadioMemories()
{
    if (!m_window->m_model || !m_window->m_model->isConnected())
    {
        QMessageBox::information(popupParent(), QStringLiteral("Import Memories"),
                                 QStringLiteral("Connect to the radio before importing memories."));
        return;
    }
    if (m_refreshInProgress)
    {
        QMessageBox::information(popupParent(), QStringLiteral("Import Memories"),
                                 QStringLiteral("Wait for the current radio memory sync to finish before importing."));
        return;
    }

    const QString path = QFileDialog::getOpenFileName(popupParent(), QStringLiteral("Import Memories"), QString(),
                                                      QString::fromLatin1(kMemoryFileFilter));
    if (path.isEmpty())
    {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::warning(popupParent(), QStringLiteral("Import Memories Failed"),
                             QStringLiteral("Memory import failed. Could not open the selected file."));
        return;
    }

    const QVector<MemoryRecord> records = memoriesFromCsv(file.readAll());
    if (records.isEmpty())
    {
        QMessageBox::warning(
            popupParent(), QStringLiteral("Import Memories Failed"),
            QStringLiteral("Memory import failed. The selected file does not contain importable CSV memories."));
        return;
    }

    int skippedCount = 0;
    const int importedCount = queueRecordsToRadio(records, &skippedCount);

    QMessageBox::information(
        popupParent(), QStringLiteral("Import Memories Successful"),
        QStringLiteral("Queued %1 memories for radio import.%2")
            .arg(importedCount)
            .arg(skippedCount > 0
                     ? QStringLiteral("\n\nSkipped %1 memories because the target group is full or not synced.")
                           .arg(skippedCount)
                     : QString()));
    m_window->showToast(QStringLiteral("Queued %1 memories for radio import").arg(importedCount));
    QTimer::singleShot(importedCount * kRadioMemoryWriteIntervalMs + 1000, this,
                       &MemoryController::requestRadioMemoryRefresh);
}

bool MemoryController::resetRadioMemories()
{
    if (!m_window->m_model || !m_window->m_model->isConnected())
    {
        QMessageBox::information(popupParent(), QStringLiteral("Reset Memories"),
                                 QStringLiteral("Connect to the radio before resetting memories."));
        return false;
    }

    if (QMessageBox::question(popupParent(), QStringLiteral("Reset Memories"),
                              QStringLiteral("Clear user radio memories in groups 1-3?\n\n"
                                             "Factory scan-edge and call memories are left untouched.")) !=
        QMessageBox::Yes)
    {
        return false;
    }

    QVector<MemoryType> deletes;
    deletes.reserve((kRadioMemoryLastGroup - kRadioMemoryFirstGroup + 1) * kRadioMemoryLastChannel);
    for (quint16 group = kRadioMemoryFirstGroup; group <= kRadioMemoryLastGroup; ++group)
    {
        for (quint16 channel = kRadioMemoryFirstChannel; channel <= kRadioMemoryLastChannel; ++channel)
        {
            deletes.append(deletedRadioMemory(group, channel));
        }
    }
    queueRadioMemoryWrites(deletes);
    m_radioMemoriesByKey.clear();
    m_receivedRadioMemoryKeys.clear();
    rebuildMemoryViews();
    QTimer::singleShot(deletes.size() * kRadioMemoryWriteIntervalMs + 1000, this,
                       &MemoryController::requestRadioMemoryRefresh);
    return true;
}

void MemoryController::rebuildMemoryViews()
{
    const QVector<MemoryRecord> memories = currentMemories();
    if (m_window->m_memoryPanel)
    {
        m_window->m_memoryPanel->setMemories(memories, m_window->m_activeMemoryId);
    }

    if (!m_window->m_memoryTable)
    {
        return;
    }
    updateMemoryTableInteraction();

    const QString bandFilter =
        m_window->m_memoryBandFilter ? m_window->m_memoryBandFilter->currentData().toString() : QString();
    m_window->m_memoryTable->setSortingEnabled(false);
    m_window->m_memoryTable->setRowCount(0);
    int visibleCount = 0;
    for (const MemoryRecord& memory : memories)
    {
        if (!bandFilter.isEmpty() && memory.band != bandFilter)
        {
            continue;
        }

        const int row = m_window->m_memoryTable->rowCount();
        m_window->m_memoryTable->insertRow(row);

        auto setItem = [this, row](int column, const QString& text)
        {
            auto* item = new QTableWidgetItem(text);
            m_window->m_memoryTable->setItem(row, column, item);
            return item;
        };

        quint16 group = 0;
        quint16 channel = 0;
        parseRadioMemoryId(memory.id, &group, &channel);
        auto* numberItem =
            setItem(kMemoryNumberColumn, QStringLiteral("%1-%2").arg(group).arg(channel, 3, 10, QLatin1Char('0')));
        numberItem->setData(Qt::UserRole, channel);
        numberItem->setTextAlignment(Qt::AlignCenter);
        setItem(kMemoryNameColumn, memory.name);
        auto* frequencyItem = setItem(kMemoryFrequencyColumn, memoryFrequencyLabel(memory.receiveHz));
        frequencyItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(memory.receiveHz));
        frequencyItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        setItem(kMemoryDuplexColumn, memory.shift)->setTextAlignment(Qt::AlignCenter);
        setItem(kMemoryModeColumn, memoryModeLabel(memory.mode))->setTextAlignment(Qt::AlignCenter);
        auto* toneItem = setItem(kMemoryToneColumn, memoryToneTableLabel(memory));
        toneItem->setToolTip(toneItem->text());
        toneItem->setTextAlignment(Qt::AlignCenter);
        auto* toneFrequencyItem = setItem(kMemoryToneFrequencyColumn, memoryToneFrequencyTableValue(memory));
        toneFrequencyItem->setToolTip(toneFrequencyItem->text());
        toneFrequencyItem->setTextAlignment(Qt::AlignCenter);
        setItem(kMemoryFilterColumn, memoryFilterLabel(memory.filter))->setTextAlignment(Qt::AlignCenter);
        setItem(kMemoryScanColumn, memoryScanGroupLabel(memory.scan))->setTextAlignment(Qt::AlignCenter);
        setItem(kMemoryIdColumn, memory.id);
        ++visibleCount;
    }

    if (m_window->m_memoryCountLabel)
    {
        const int totalCount = memories.size();
        if (m_refreshInProgress && m_currentSyncGroup >= kRadioMemoryFirstGroup &&
            m_currentSyncChannel >= kRadioMemoryFirstChannel)
        {
            const int syncIndex = (m_currentSyncGroup - kRadioMemoryFirstGroup) *
                                      (kRadioMemoryLastChannel - kRadioMemoryFirstChannel + 1) +
                                  (m_currentSyncChannel - kRadioMemoryFirstChannel + 1);
            m_window->m_memoryCountLabel->setText(QStringLiteral("Syncing group %1 channel %2 (%3/%4)")
                                                      .arg(m_currentSyncGroup)
                                                      .arg(m_currentSyncChannel, 3, 10, QLatin1Char('0'))
                                                      .arg(syncIndex)
                                                      .arg(kRadioMemorySyncTotal));
            return;
        }
        if (m_refreshInProgress)
        {
            m_window->m_memoryCountLabel->setText(QStringLiteral("Syncing memories"));
            return;
        }
        if (bandFilter.isEmpty())
        {
            m_window->m_memoryCountLabel->setText(
                QStringLiteral("%1 %2 total")
                    .arg(totalCount)
                    .arg(totalCount == 1 ? QStringLiteral("memory") : QStringLiteral("memories")));
        }
        else
        {
            m_window->m_memoryCountLabel->setText(
                QStringLiteral("%1 filtered / %2 total").arg(visibleCount).arg(totalCount));
        }
    }
}

QString MemoryController::selectedMemoryId() const
{
    if (!m_window->m_memoryTable)
    {
        return QString();
    }

    const int row = m_window->m_memoryTable->currentRow();
    if (row < 0)
    {
        return QString();
    }

    const auto* idItem = m_window->m_memoryTable->item(row, kMemoryIdColumn);
    return idItem ? idItem->text() : QString();
}

void MemoryController::selectCheckedMemory()
{
    const QString id = selectedMemoryId();
    if (id.isEmpty())
    {
        QMessageBox::information(popupParent(), "Select Memory", "Choose one memory first.");
        return;
    }

    selectMemoryById(id, true);
}

void MemoryController::selectMemoryById(const QString& id, bool showDialogOnFailure)
{
    if (id.isEmpty())
    {
        return;
    }
    if (m_window->m_controlsLocked)
    {
        if (showDialogOnFailure)
        {
            QMessageBox::information(popupParent(), "Select Memory", "Unlock controls before selecting a memory.");
        }
        else
        {
            m_window->showToast(QStringLiteral("Controls are locked"), 4000, MainWindow::ToastKind::Warning);
        }
        return;
    }

    bool found = false;
    const MemoryRecord memory = memoryForId(id, &found);
    if (!found)
    {
        return;
    }
    if (!m_window->m_model->isReady() || !m_window->m_vfo)
    {
        if (showDialogOnFailure)
        {
            QMessageBox::information(popupParent(), "Select Memory",
                                     "Connect to the radio and wait for sync before selecting a memory.");
        }
        else
        {
            m_window->showToast(QStringLiteral("Connect to the radio before selecting a memory"), 4000,
                                MainWindow::ToastKind::Warning);
        }
        return;
    }

    m_window->setActiveMemory(memory.id, memory.name, memory.receiveHz, memory.duplexMode, memory.offsetHz,
                              memory.toneMode, memory.toneValue);
    m_window->m_applyingMemorySelection = true;
    const int generation = ++m_window->m_memorySelectionGeneration;
    m_window->m_vfo->setFrequencyHz(memory.receiveHz);
    m_window->m_vfo->setMode(modeLabelFromRegister(memory.mode));
    m_window->m_vfo->setRepeaterOffsetHz(memory.offsetHz);
    m_window->m_vfo->setDuplexMode(static_cast<duplexMode_t>(memory.duplexMode));
    if (isDtcsToneMode(static_cast<rptAccessTxRx_t>(memory.toneMode)))
    {
        m_window->m_vfo->setDtcsCode(memory.toneValue);
    }
    else if (memory.toneMode != ratrNN)
    {
        m_window->m_vfo->setToneFrequency(memory.toneValue);
    }
    m_window->m_vfo->setToneAccessMode(static_cast<rptAccessTxRx_t>(memory.toneMode));
    // Release immediately if the radio was already at every correct setting (no callbacks will fire).
    m_window->checkIfMemorySelectionComplete();
    // Fallback: release the guard after 3 s in case the radio never confirms.
    QTimer::singleShot(3000, this,
                       [this, generation]()
                       {
                           if (m_window->m_memorySelectionGeneration != generation)
                           {
                               return;
                           }
                           m_window->m_applyingMemorySelection = false;
                       });
    m_window->showToast(QStringLiteral("Selected memory: %1").arg(memory.name));
}

void MemoryController::editSelectedMemory()
{
    if (m_memoryEditorPane && m_memoryEditorPane->isVisible())
    {
        closeMemoryEditorPane();
        return;
    }

    const QString id = selectedMemoryId();
    if (id.isEmpty())
    {
        if (m_memoryEditButton)
        {
            m_memoryEditButton->setChecked(false);
        }
        QMessageBox::information(popupParent(), "Edit Memory", "Choose one memory first.");
        return;
    }
    showMemoryEditor(id);
}

void MemoryController::copySelectedMemory()
{
    const QString id = selectedMemoryId();
    if (id.isEmpty())
    {
        QMessageBox::information(popupParent(), "Copy Memory", "Choose one memory first.");
        return;
    }

    bool found = false;
    MemoryRecord copy = memoryForId(id, &found);
    if (!found)
    {
        return;
    }

    copy.name = (copy.name.isEmpty() ? QStringLiteral("Copy") : QStringLiteral("%1 Copy").arg(copy.name))
                    .left(kRadioMemoryNameMaxChars);

    quint16 group = 0;
    quint16 channel = 0;
    if (!targetForMemoryFrequency(copy.receiveHz, &group, &channel))
    {
        QMessageBox::warning(popupParent(), "Copy Memory",
                             "No empty radio memory channel is available in the target group.");
        return;
    }

    writeMemoryRecord(copy, group, channel);
    m_window->showToast(QStringLiteral("Copied memory: %1").arg(copy.name));
}

void MemoryController::removeSelectedMemory()
{
    const QString id = selectedMemoryId();
    if (id.isEmpty())
    {
        QMessageBox::information(popupParent(), "Remove Memory", "Choose one memory first.");
        return;
    }

    bool found = false;
    const MemoryRecord memory = memoryForId(id, &found);
    if (!found)
    {
        return;
    }

    if (QMessageBox::question(popupParent(), "Remove Memory",
                              QStringLiteral("Remove memory \"%1\"?").arg(memory.name)) != QMessageBox::Yes)
    {
        return;
    }

    quint16 group = 0;
    quint16 channel = 0;
    if (!parseRadioMemoryId(id, &group, &channel))
    {
        return;
    }
    deleteRadioMemory(group, channel);
    m_window->showToast(QStringLiteral("Memory removed"));
}

void MemoryController::moveSelectedMemoryUp()
{
    moveSelectedMemory(-1);
}

void MemoryController::moveSelectedMemoryDown()
{
    moveSelectedMemory(1);
}

void MemoryController::moveSelectedMemory(int direction)
{
    const QString id = selectedMemoryId();
    if (id.isEmpty())
    {
        QMessageBox::information(popupParent(), "Move Memory", "Choose one memory first.");
        return;
    }

    QVector<MemoryRecord> memories = currentMemories();
    const QString bandFilter =
        m_window->m_memoryBandFilter ? m_window->m_memoryBandFilter->currentData().toString() : QString();
    if (!bandFilter.isEmpty())
    {
        QMessageBox::information(popupParent(), "Move Memory", "Switch to All memories before reordering.");
        return;
    }

    QVector<int> visibleIndexes;
    for (int i = 0; i < memories.size(); ++i)
    {
        visibleIndexes.append(i);
    }

    int visiblePosition = -1;
    for (int i = 0; i < visibleIndexes.size(); ++i)
    {
        if (memories.at(visibleIndexes.at(i)).id == id)
        {
            visiblePosition = i;
            break;
        }
    }

    const int targetPosition = visiblePosition + direction;
    if (visiblePosition < 0 || targetPosition < 0 || targetPosition >= visibleIndexes.size())
    {
        return;
    }

    const MemoryRecord source = memories.at(visibleIndexes.at(visiblePosition));
    const MemoryRecord target = memories.at(visibleIndexes.at(targetPosition));
    quint16 sourceGroup = 0;
    quint16 sourceChannel = 0;
    quint16 targetGroup = 0;
    quint16 targetChannel = 0;
    if (!parseRadioMemoryId(source.id, &sourceGroup, &sourceChannel) ||
        !parseRadioMemoryId(target.id, &targetGroup, &targetChannel))
    {
        return;
    }
    if (sourceGroup != targetGroup)
    {
        QMessageBox::information(popupParent(), "Move Memory",
                                 "Radio memories can only move within the same band group.");
        return;
    }
    writeMemoryRecord(source, targetGroup, targetChannel);
    writeMemoryRecord(target, sourceGroup, sourceChannel);
}

void MemoryController::storeCurrentMemory()
{
    showMemoryEditor(QString());
}

void MemoryController::showMemoryEditor(const QString& memoryId)
{
    QWidget* parent = popupParent();
    const bool editing = !memoryId.isEmpty();
    if (!m_memoryEditorPane || !m_window || !m_window->m_memoryWindow)
    {
        return;
    }

    closeMemoryEditorPane(false);
    auto* editor = m_memoryEditorPane;
    editor->show();
    if (m_memoryEditorSeparator)
    {
        m_memoryEditorSeparator->show();
    }
    m_window->m_memoryWindow->setFixedSize(memoryManagerWindowSize());
    static_cast<sdr9700::ui::UtilityWindow*>(m_window->m_memoryWindow)->centerOnHost();

    auto* root = new QVBoxLayout(editor);
    root->setSpacing(8);
    root->setContentsMargins(10, 8, 0, 0);

    auto* editorTitle = new QLabel(editing ? QStringLiteral("Edit Memory") : QStringLiteral("Add Memory"), editor);
    editorTitle->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 12px; font-weight: bold; }")
                                   .arg(QLatin1String(UiTheme::Color::TextStatusPrimary)));
    root->addWidget(editorTitle);

    auto configureSectionForm = [](QFormLayout* form)
    {
        form->setHorizontalSpacing(14);
        form->setVerticalSpacing(6);
        form->setContentsMargins(10, 12, 10, 10);
    };
    auto makeSection = [editor, configureSectionForm](const QString& title, QFormLayout** form)
    {
        auto* group = new QGroupBox(title, editor);
        group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        group->setStyleSheet(QStringLiteral("QGroupBox { color: %1; border: 1px solid %2; border-radius: 3px; "
                                            "margin-top: 8px; padding-top: 4px; font-size: 10px; font-weight: bold; }"
                                            "QGroupBox::title { subcontrol-origin: border; "
                                            "subcontrol-position: top left; left: 8px; top: -6px; padding: 0 4px; "
                                            "color: %3; background: %4; }")
                                 .arg(UiTheme::Color::TextStatusPrimary, UiTheme::Color::BorderMedium,
                                      UiTheme::Color::TextStatusSecondary, UiTheme::Color::Panel));
        *form = new QFormLayout(group);
        configureSectionForm(*form);
        return group;
    };
    auto* groupCombo = new QComboBox(editor);
    for (quint16 group = kRadioMemoryFirstGroup; group <= kRadioMemoryLastGroup; ++group)
    {
        groupCombo->addItem(QString::number(group), group);
    }
    auto* channelCombo = new QComboBox(editor);
    for (quint16 channel = kRadioMemoryFirstChannel; channel <= kRadioMemoryLastChannel; ++channel)
    {
        channelCombo->addItem(QString::number(channel).rightJustified(3, QLatin1Char('0')), channel);
    }
    auto* channelRow = new QWidget(editor);
    auto* channelRowLayout = new QHBoxLayout(channelRow);
    channelRowLayout->setContentsMargins(0, 0, 0, 0);
    channelRowLayout->setSpacing(6);
    channelRowLayout->addWidget(groupCombo, 1);
    auto* channelSeparator = new QLabel(QStringLiteral("-"), channelRow);
    channelSeparator->setAlignment(Qt::AlignCenter);
    channelRowLayout->addWidget(channelSeparator);
    channelRowLayout->addWidget(channelCombo, 2);
    auto* nameEdit = new QLineEdit(editor);
    nameEdit->setMaxLength(kRadioMemoryNameMaxChars);
    nameEdit->setPlaceholderText(QStringLiteral("Maximum %1 characters").arg(kRadioMemoryNameMaxChars));
    auto* frequencyEdit = new QLineEdit(editor);
    frequencyEdit->setPlaceholderText("145.000000");
    auto* modeCombo = new QComboBox(editor);
    modeCombo->addItem(QStringLiteral("FM"), modeFM);
    modeCombo->addItem(QStringLiteral("DV"), modeDV);
    modeCombo->addItem(QStringLiteral("USB"), modeUSB);
    modeCombo->addItem(QStringLiteral("LSB"), modeLSB);
    modeCombo->addItem(QStringLiteral("AM"), modeAM);
    modeCombo->addItem(QStringLiteral("CW"), modeCW);
    modeCombo->addItem(QStringLiteral("CW-R"), modeCW_R);
    modeCombo->addItem(QStringLiteral("RTTY"), modeRTTY);
    modeCombo->addItem(QStringLiteral("RTTY-R"), modeRTTY_R);
    modeCombo->addItem(QStringLiteral("DD"), modeDD);
    auto* filterCombo = new QComboBox(editor);
    filterCombo->addItem(QStringLiteral("FIL1"), 1);
    filterCombo->addItem(QStringLiteral("FIL2"), 2);
    filterCombo->addItem(QStringLiteral("FIL3"), 3);
    auto* dataModeCombo = new QComboBox(editor);
    dataModeCombo->addItem(QStringLiteral("OFF"), 0);
    dataModeCombo->addItem(QStringLiteral("DATA1"), 1);
    auto* scanGroupCombo = new QComboBox(editor);
    scanGroupCombo->addItem(QStringLiteral("OFF"), 0);
    scanGroupCombo->addItem(QStringLiteral("Group 1"), 1);
    scanGroupCombo->addItem(QStringLiteral("Group 2"), 2);
    scanGroupCombo->addItem(QStringLiteral("Group 3"), 3);
    auto* offsetCombo = new QComboBox(editor);
    auto* modeOffsetRow = new QWidget(editor);
    auto* modeOffsetLayout = new QHBoxLayout(modeOffsetRow);
    modeOffsetLayout->setContentsMargins(0, 0, 0, 0);
    modeOffsetLayout->setSpacing(8);
    auto* offsetLabel = new QLabel(QStringLiteral("Offset:"), modeOffsetRow);
    modeOffsetLayout->addWidget(modeCombo, 1);
    modeOffsetLayout->addWidget(offsetLabel);
    modeOffsetLayout->addWidget(offsetCombo, 1);
    auto* customOffsetRow = new QWidget(editor);
    auto* customOffsetLayout = new QHBoxLayout(customOffsetRow);
    customOffsetLayout->setContentsMargins(0, 0, 0, 0);
    customOffsetLayout->setSpacing(6);
    auto* customOffsetModeCombo = new QComboBox(customOffsetRow);
    customOffsetModeCombo->addItem("+", dmDupPlus);
    customOffsetModeCombo->addItem("-", dmDupMinus);
    auto* customOffsetSpin = new QDoubleSpinBox(customOffsetRow);
    customOffsetSpin->setRange(0.0, 99.999);
    customOffsetSpin->setDecimals(3);
    customOffsetSpin->setSuffix(" MHz");
    customOffsetLayout->addWidget(customOffsetModeCombo);
    customOffsetLayout->addWidget(customOffsetSpin, 1);
    auto* toneOptionCombo = new QComboBox(editor);
    toneOptionCombo->addItem("OFF", MemoryToneOff);
    toneOptionCombo->addItem("TONE", MemoryToneTone);
    toneOptionCombo->addItem("DTCS", MemoryToneDtcs);
    auto* toneEdit = new QLineEdit(editor);
    toneEdit->setPlaceholderText(QStringLiteral("103.5"));
    toneEdit->hide();
    auto* tsqlEdit = new QLineEdit(editor);
    tsqlEdit->setPlaceholderText(QStringLiteral("103.5"));
    tsqlEdit->hide();
    auto* dsqlCombo = new QComboBox(editor);
    dsqlCombo->addItem(QStringLiteral("OFF"), 0);
    dsqlCombo->addItem(QStringLiteral("DSQL"), 1);
    dsqlCombo->addItem(QStringLiteral("CSQL"), 2);
    auto* dtcsSpin = new QSpinBox(editor);
    dtcsSpin->setRange(0, 999);
    dtcsSpin->setValue(23);
    dtcsSpin->setDisplayIntegerBase(10);
    dtcsSpin->hide();
    auto* dtcsRxSpin = new QSpinBox(editor);
    dtcsRxSpin->setRange(0, 999);
    dtcsRxSpin->setValue(23);
    dtcsRxSpin->setDisplayIntegerBase(10);
    dtcsRxSpin->hide();
    auto* dtcsPolarityCombo = new QComboBox(editor);
    dtcsPolarityCombo->addItem(QStringLiteral("N"), 0);
    dtcsPolarityCombo->addItem(QStringLiteral("R"), 3);
    auto* dtcsRxPolarityCombo = new QComboBox(editor);
    dtcsRxPolarityCombo->addItem(QStringLiteral("N"), 0);
    dtcsRxPolarityCombo->addItem(QStringLiteral("R"), 3);
    auto* dvSqlSpin = new QSpinBox(editor);
    dvSqlSpin->setRange(0, 99);
    auto* urEdit = new QLineEdit(editor);
    urEdit->setMaxLength(8);
    auto* r1Edit = new QLineEdit(editor);
    r1Edit->setMaxLength(8);
    auto* r2Edit = new QLineEdit(editor);
    r2Edit->setMaxLength(8);
    auto* tonePresetBtn = new QPushButton(QStringLiteral("NONE"), editor);
    auto* ctcssPresetBtn = new QPushButton(QStringLiteral("NONE"), editor);
    auto* dtcsPresetBtn = new QPushButton(QStringLiteral("NONE"), editor);
    auto* dtcsRxPresetBtn = new QPushButton(QStringLiteral("NONE"), editor);
    auto setEditorFieldHeight = [](QWidget* widget) { widget->setMinimumHeight(kMemoryEditorFieldHeight); };
    for (QWidget* widget : std::initializer_list<QWidget*>{
             groupCombo,     channelCombo,   nameEdit,    frequencyEdit,         modeCombo,           filterCombo,
             dataModeCombo,  scanGroupCombo, offsetCombo, customOffsetModeCombo, customOffsetSpin,    toneOptionCombo,
             dsqlCombo,      dtcsSpin,       dtcsRxSpin,  dtcsPolarityCombo,     dtcsRxPolarityCombo, dvSqlSpin,
             urEdit,         r1Edit,         r2Edit,      tonePresetBtn,         ctcssPresetBtn,      dtcsPresetBtn,
             dtcsRxPresetBtn})
    {
        setEditorFieldHeight(widget);
    }
    tonePresetBtn->setFixedHeight(kMemoryEditorFieldHeight);
    ctcssPresetBtn->setFixedHeight(kMemoryEditorFieldHeight);
    dtcsPresetBtn->setFixedHeight(kMemoryEditorFieldHeight);
    dtcsRxPresetBtn->setFixedHeight(kMemoryEditorFieldHeight);
    auto* dtcsRow = new QWidget(editor);
    auto* dtcsRowLayout = new QHBoxLayout(dtcsRow);
    dtcsRowLayout->setContentsMargins(0, 0, 0, 0);
    dtcsRowLayout->setSpacing(8);
    auto* dtcsCodeColumn = new QWidget(dtcsRow);
    auto* dtcsCodeColumnLayout = new QVBoxLayout(dtcsCodeColumn);
    dtcsCodeColumnLayout->setContentsMargins(0, 0, 0, 0);
    dtcsCodeColumnLayout->setSpacing(3);
    auto* dtcsCodeTextLabel = new QLabel(QStringLiteral("TX Code"), dtcsCodeColumn);
    dtcsCodeTextLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    dtcsCodeColumnLayout->addWidget(dtcsCodeTextLabel);
    dtcsCodeColumnLayout->addWidget(dtcsPresetBtn);
    auto* dtcsPolarityColumn = new QWidget(dtcsRow);
    auto* dtcsPolarityColumnLayout = new QVBoxLayout(dtcsPolarityColumn);
    dtcsPolarityColumnLayout->setContentsMargins(0, 0, 0, 0);
    dtcsPolarityColumnLayout->setSpacing(3);
    auto* dtcsPolarityLabel = new QLabel(QStringLiteral("Polarity"), dtcsPolarityColumn);
    dtcsPolarityLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    dtcsPolarityColumnLayout->addWidget(dtcsPolarityLabel);
    dtcsPolarityColumnLayout->addWidget(dtcsPolarityCombo);
    dtcsRowLayout->addWidget(dtcsCodeColumn, 1);
    dtcsRowLayout->addWidget(dtcsPolarityColumn, 0);

    auto* dtcsRxRow = new QWidget(editor);
    auto* dtcsRxRowLayout = new QHBoxLayout(dtcsRxRow);
    dtcsRxRowLayout->setContentsMargins(0, 0, 0, 0);
    dtcsRxRowLayout->setSpacing(8);
    auto* dtcsRxCodeColumn = new QWidget(dtcsRxRow);
    auto* dtcsRxCodeColumnLayout = new QVBoxLayout(dtcsRxCodeColumn);
    dtcsRxCodeColumnLayout->setContentsMargins(0, 0, 0, 0);
    dtcsRxCodeColumnLayout->setSpacing(3);
    auto* dtcsRxCodeTextLabel = new QLabel(QStringLiteral("RX Code"), dtcsRxCodeColumn);
    dtcsRxCodeTextLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    dtcsRxCodeColumnLayout->addWidget(dtcsRxCodeTextLabel);
    dtcsRxCodeColumnLayout->addWidget(dtcsRxPresetBtn);
    auto* dtcsRxPolarityColumn = new QWidget(dtcsRxRow);
    auto* dtcsRxPolarityColumnLayout = new QVBoxLayout(dtcsRxPolarityColumn);
    dtcsRxPolarityColumnLayout->setContentsMargins(0, 0, 0, 0);
    dtcsRxPolarityColumnLayout->setSpacing(3);
    auto* dtcsRxPolarityLabel = new QLabel(QStringLiteral("Polarity"), dtcsRxPolarityColumn);
    dtcsRxPolarityLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    dtcsRxPolarityColumnLayout->addWidget(dtcsRxPolarityLabel);
    dtcsRxPolarityColumnLayout->addWidget(dtcsRxPolarityCombo);
    dtcsRxRowLayout->addWidget(dtcsRxCodeColumn, 1);
    dtcsRxRowLayout->addWidget(dtcsRxPolarityColumn, 0);
    auto* toneValueRow = new QWidget(editor);
    auto* toneValueRowLayout = new QHBoxLayout(toneValueRow);
    toneValueRowLayout->setContentsMargins(0, 0, 0, 0);
    toneValueRowLayout->setSpacing(8);
    auto* dtcsValueRow = new QWidget(editor);
    auto* dtcsValueRowLayout = new QHBoxLayout(dtcsValueRow);
    dtcsValueRowLayout->setContentsMargins(0, 0, 0, 0);
    dtcsValueRowLayout->setSpacing(8);
    auto addToneColumn = [](QHBoxLayout* layout, QWidget* parent, const QString& labelText, QWidget* field)
    {
        auto* column = new QWidget(parent);
        auto* columnLayout = new QVBoxLayout(column);
        columnLayout->setContentsMargins(0, 0, 0, 0);
        columnLayout->setSpacing(3);
        auto* label = new QLabel(labelText, column);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        columnLayout->addWidget(label);
        columnLayout->addWidget(field);
        layout->addWidget(column, 1);
    };
    auto addPairDivider = [](QHBoxLayout* layout, QWidget* parent)
    {
        auto* divider = new QWidget(parent);
        divider->setFixedWidth(1);
        divider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        divider->setStyleSheet(
            QStringLiteral("QWidget { background: %1; }").arg(QLatin1String(UiTheme::Color::BorderMedium)));
        layout->addWidget(divider);
    };
    addToneColumn(toneValueRowLayout, toneValueRow, QStringLiteral("RX Tone"), ctcssPresetBtn);
    addPairDivider(toneValueRowLayout, toneValueRow);
    addToneColumn(toneValueRowLayout, toneValueRow, QStringLiteral("TX Tone"), tonePresetBtn);
    dtcsValueRowLayout->addWidget(dtcsRxRow, 1);
    addPairDivider(dtcsValueRowLayout, dtcsValueRow);
    dtcsValueRowLayout->addWidget(dtcsRow, 1);
    auto setTonePick = [tonePresetBtn, toneEdit](ushort value, const QString& label)
    {
        tonePresetBtn->setText(value == 0 ? QStringLiteral("NONE") : label);
        toneEdit->setText(value == 0 ? QString() : label);
    };
    auto setCtcssPick = [ctcssPresetBtn, tsqlEdit](ushort value, const QString& label)
    {
        ctcssPresetBtn->setText(value == 0 ? QStringLiteral("NONE") : label);
        tsqlEdit->setText(value == 0 ? QString() : label);
    };
    auto setDtcsPick =
        [dtcsPresetBtn, dtcsSpin, dtcsRxPresetBtn, dtcsRxPolarityCombo, dtcsRxSpin](ushort v, const QString& label)
    {
        dtcsPresetBtn->setText(v == 0 ? QStringLiteral("NONE") : label);
        dtcsSpin->setValue(v);
        // IC-9700 memory tone modes currently expose DTCS TX-only and DTCS TX/RX, but not RX-only DTCS.
        // Keep the RX selector inaccessible until TX is set so the editor cannot create an unsupported mode.
        const bool txDtcsEnabled = v != 0;
        dtcsRxPresetBtn->setEnabled(txDtcsEnabled);
        dtcsRxPolarityCombo->setEnabled(txDtcsEnabled);
        if (!txDtcsEnabled)
        {
            dtcsRxPresetBtn->setText(QStringLiteral("NONE"));
            dtcsRxSpin->setValue(0);
            dtcsRxPolarityCombo->setCurrentIndex(qMax(0, dtcsRxPolarityCombo->findData(0)));
        }
    };
    auto setDtcsRxPick = [dtcsRxPresetBtn, dtcsRxSpin](ushort v, const QString& label)
    {
        dtcsRxPresetBtn->setText(v == 0 ? QStringLiteral("NONE") : label);
        dtcsRxSpin->setValue(v);
    };
    auto clearTonePick = [tonePresetBtn, ctcssPresetBtn, dtcsPresetBtn, dtcsRxPresetBtn, dtcsRxPolarityCombo, toneEdit,
                          tsqlEdit, dtcsSpin, dtcsRxSpin]()
    {
        tonePresetBtn->setText(QStringLiteral("NONE"));
        ctcssPresetBtn->setText(QStringLiteral("NONE"));
        dtcsPresetBtn->setText(QStringLiteral("NONE"));
        dtcsRxPresetBtn->setText(QStringLiteral("NONE"));
        dtcsRxPresetBtn->setEnabled(false);
        dtcsRxPolarityCombo->setEnabled(false);
        toneEdit->clear();
        tsqlEdit->clear();
        dtcsSpin->setValue(0);
        dtcsRxSpin->setValue(0);
    };
    QFormLayout* memoryForm = nullptr;
    QFormLayout* optionsForm = nullptr;
    QFormLayout* toneForm = nullptr;
    QFormLayout* dstarForm = nullptr;
    auto* memoryGroup = makeSection(QStringLiteral("Memory"), &memoryForm);
    auto* optionsGroup = makeSection(QStringLiteral("Options"), &optionsForm);
    auto* toneGroup = makeSection(QStringLiteral("Tone"), &toneForm);
    auto* dstarGroup = makeSection(QStringLiteral("D-STAR / DV"), &dstarForm);

    auto* optionsRow = new QWidget(optionsGroup);
    auto* optionsRowLayout = new QHBoxLayout(optionsRow);
    optionsRowLayout->setContentsMargins(0, 0, 0, 0);
    optionsRowLayout->setSpacing(8);
    auto addOptionColumn = [optionsRow, optionsRowLayout](const QString& labelText, QWidget* field, int stretch)
    {
        auto* column = new QWidget(optionsRow);
        auto* columnLayout = new QVBoxLayout(column);
        columnLayout->setContentsMargins(0, 0, 0, 0);
        columnLayout->setSpacing(3);
        auto* label = new QLabel(labelText, column);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        columnLayout->addWidget(label);
        columnLayout->addWidget(field);
        optionsRowLayout->addWidget(column, stretch);
    };
    addOptionColumn(QStringLiteral("Data Mode"), dataModeCombo, 1);
    addOptionColumn(QStringLiteral("Scan Group"), scanGroupCombo, 1);
    addOptionColumn(QStringLiteral("Filter"), filterCombo, 1);

    memoryForm->addRow("Channel:", channelRow);
    memoryForm->addRow("Frequency:", frequencyEdit);
    memoryForm->addRow("Name:", nameEdit);
    memoryForm->addRow("Mode:", modeOffsetRow);
    memoryForm->addRow("Custom Offset:", customOffsetRow);
    optionsForm->addRow(optionsRow);
    toneForm->addRow("Tone Mode:", toneOptionCombo);
    toneForm->addRow(QString(), toneValueRow);
    toneForm->addRow(QString(), dtcsValueRow);
    dstarForm->addRow("Digital SQL (DSQL):", dsqlCombo);
    dstarForm->addRow("DV SQL:", dvSqlSpin);
    dstarForm->addRow("Your Call (UR):", urEdit);
    dstarForm->addRow("Repeater 1 Callsign (R1):", r1Edit);
    dstarForm->addRow("Repeater 2 Callsign (R2):", r2Edit);

    root->addWidget(memoryGroup);
    root->addWidget(toneGroup);
    root->addWidget(optionsGroup);
    root->addWidget(dstarGroup);

    auto resizeEditorToContents = [editor]()
    {
        if (QLayout* layout = editor->layout())
        {
            layout->invalidate();
            layout->activate();
        }
        editor->updateGeometry();
    };

    auto populateOffsetOptions = [frequencyEdit, offsetCombo]()
    {
        const QVariant currentModeData = offsetCombo->currentData(Qt::UserRole);
        const int currentMode = currentModeData.isValid() ? currentModeData.toInt() : dmSimplex;
        const quint64 currentOffset = offsetCombo->currentData(Qt::UserRole + 1).toULongLong();
        offsetCombo->clear();
        offsetCombo->addItem(QStringLiteral("Simplex"), dmSimplex);
        offsetCombo->setItemData(offsetCombo->count() - 1, 0ULL, Qt::UserRole + 1);

        quint64 hz = 0;
        if (parseFrequencyText(frequencyEdit->text(), &hz))
        {
            for (const OffsetPreset& preset : offsetPresetsForHz(hz))
            {
                offsetCombo->addItem(preset.label, preset.mode);
                offsetCombo->setItemData(offsetCombo->count() - 1, QVariant::fromValue<qulonglong>(preset.hz),
                                         Qt::UserRole + 1);
            }
        }

        offsetCombo->addItem(QStringLiteral("Custom"), kMemoryOffsetCustom);
        offsetCombo->setItemData(offsetCombo->count() - 1, QVariant::fromValue<qulonglong>(currentOffset),
                                 Qt::UserRole + 1);

        for (int i = 0; i < offsetCombo->count(); ++i)
        {
            if (offsetCombo->itemData(i, Qt::UserRole).toInt() == currentMode &&
                offsetCombo->itemData(i, Qt::UserRole + 1).toULongLong() == currentOffset)
            {
                offsetCombo->setCurrentIndex(i);
                return;
            }
        }
        offsetCombo->setCurrentIndex(0);
    };

    auto setOffsetSelection =
        [offsetCombo, customOffsetModeCombo, customOffsetSpin](duplexMode_t mode, quint64 offsetHz)
    {
        const quint64 scaledOffsetHz = offsetHz * 100ULL;
        for (int i = 0; i < offsetCombo->count(); ++i)
        {
            const quint64 presetOffsetHz = offsetCombo->itemData(i, Qt::UserRole + 1).toULongLong();
            if (offsetCombo->itemData(i, Qt::UserRole).toInt() == mode &&
                (presetOffsetHz == offsetHz || presetOffsetHz == scaledOffsetHz))
            {
                offsetCombo->setCurrentIndex(i);
                return;
            }
        }
        customOffsetModeCombo->setCurrentIndex(qMax(0, customOffsetModeCombo->findData(mode)));
        customOffsetSpin->setValue(offsetHz / 1000000.0);
        offsetCombo->setCurrentIndex(qMax(0, offsetCombo->findData(kMemoryOffsetCustom)));
    };

    auto updateCustomOffsetVisibility =
        [memoryForm, modeCombo, offsetLabel, offsetCombo, customOffsetRow, resizeEditorToContents]()
    {
        const bool showOffset = modeSupportsMemoryOffset(modeCombo->currentData().toInt());
        const bool customSelected = offsetCombo->currentData(Qt::UserRole).toInt() == kMemoryOffsetCustom;
        offsetLabel->setVisible(showOffset);
        offsetCombo->setVisible(showOffset);
        memoryForm->setRowVisible(customOffsetRow, showOffset && customSelected);
        resizeEditorToContents();
    };

    populateOffsetOptions();
    updateCustomOffsetVisibility();

    auto populateToneValues = [clearTonePick]() { clearTonePick(); };

    auto updateConditionalSections =
        [modeCombo, toneOptionCombo, toneForm, toneValueRow, dtcsValueRow, dstarGroup, resizeEditorToContents]()
    {
        const int mode = modeCombo->currentData().toInt();
        const auto toneFamily = static_cast<MemoryToneFamily>(toneOptionCombo->currentData().toInt());
        const bool showDstar = mode == modeDV || mode == modeDD;
        auto setToneRowVisible = [toneForm](QWidget* field, bool visible) { toneForm->setRowVisible(field, visible); };
        setToneRowVisible(toneValueRow, toneFamily == MemoryToneTone);
        setToneRowVisible(dtcsValueRow, toneFamily == MemoryToneDtcs);
        dstarGroup->setVisible(showDstar);
        resizeEditorToContents();
    };

    auto copyCurrentSettings = [this, editor, nameEdit, frequencyEdit, modeCombo, offsetCombo, customOffsetModeCombo,
                                customOffsetSpin, toneOptionCombo, setTonePick, setCtcssPick, setDtcsPick,
                                populateToneValues, populateOffsetOptions, setOffsetSelection,
                                updateCustomOffsetVisibility, updateConditionalSections]()
    {
        if (!m_window->m_model->isReady() || !m_window->m_vfo)
        {
            QMessageBox::information(editor, "Copy Current Settings",
                                     "Connect to the radio and wait for sync before copying current settings.");
            return;
        }

        frequencyEdit->setText(memoryFrequencyLabel(m_window->m_vfo->frequencyHz()));
        if (nameEdit->text().trimmed().isEmpty())
        {
            nameEdit->setText(memoryFrequencyLabel(m_window->m_vfo->frequencyHz()));
        }
        modeCombo->setCurrentIndex(qMax(0, modeCombo->findData(modeRegisterFromLabel(m_window->m_vfo->mode()))));
        populateOffsetOptions();
        setOffsetSelection(m_window->m_duplexMode, m_window->m_repeaterOffsetHz);
        updateCustomOffsetVisibility();
        const MemoryToneFamily toneFamily = memoryToneFamilyForMode(m_window->m_toneAccessMode);
        toneOptionCombo->setCurrentIndex(qMax(0, toneOptionCombo->findData(toneFamily)));
        populateToneValues();
        updateConditionalSections();
        if (m_window->m_toneAccessMode != ratrNN)
        {
            const bool isDtcs = isDtcsToneMode(m_window->m_toneAccessMode);
            const ushort toneValue = isDtcs ? m_window->m_dtcsCode : m_window->m_toneFrequency;
            const QString toneText = isDtcs ? dtcsCodeLabel(toneValue) : toneFrequencyLabel(toneValue);
            if (isDtcs)
            {
                setDtcsPick(toneValue, toneText);
            }
            else
            {
                setTonePick(toneValue, toneText);
            }
        }
    };

    populateToneValues();
    connect(toneOptionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), editor,
            [populateToneValues, updateConditionalSections]()
            {
                populateToneValues();
                updateConditionalSections();
            });
    connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), editor,
            [updateCustomOffsetVisibility, updateConditionalSections]()
            {
                updateCustomOffsetVisibility();
                updateConditionalSections();
            });
    auto styleToneGridButton = [](QPushButton* button)
    {
        button->setFixedSize(54, 24);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QStringLiteral("QPushButton { background: %1; border: 1px solid %2; border-radius: 3px; "
                                             "color: %3; font-size: 11px; }"
                                             "QPushButton:hover { background: %4; border-color: %5; color: %6; }")
                                  .arg(UiTheme::Color::Button, UiTheme::Color::BorderLight, UiTheme::Color::TextPrimary,
                                       UiTheme::Color::AccentDark, UiTheme::Color::Accent, UiTheme::Color::White));
    };
    connect(tonePresetBtn, &QPushButton::clicked, tonePresetBtn,
            [tonePresetBtn, setTonePick, styleToneGridButton]()
            {
                QMenu menu(tonePresetBtn);
                styleCompactMenu(&menu);
                auto* panel = new QWidget(&menu);
                auto* grid = new QGridLayout(panel);
                grid->setContentsMargins(6, 6, 6, 6);
                grid->setHorizontalSpacing(4);
                grid->setVerticalSpacing(4);
                static constexpr int kCols = 4;
                int idx = 0;
                auto* noneBtn = new QPushButton(QStringLiteral("NONE"), panel);
                styleToneGridButton(noneBtn);
                connect(noneBtn, &QPushButton::clicked, &menu,
                        [setTonePick, menuPtr = &menu]()
                        {
                            setTonePick(0, QStringLiteral("NONE"));
                            menuPtr->close();
                        });
                grid->addWidget(noneBtn, idx / kCols, idx % kCols);
                ++idx;
                for (const TonePreset& preset : kTonePresets)
                {
                    const QString label = QString::fromLatin1(preset.label);
                    const ushort tone = preset.tone;
                    auto* btn = new QPushButton(label, panel);
                    styleToneGridButton(btn);
                    connect(btn, &QPushButton::clicked, &menu,
                            [setTonePick, label, tone, menuPtr = &menu]()
                            {
                                setTonePick(tone, label);
                                menuPtr->close();
                            });
                    grid->addWidget(btn, idx / kCols, idx % kCols);
                    ++idx;
                }
                auto* action = new QWidgetAction(&menu);
                action->setDefaultWidget(panel);
                menu.addAction(action);
                menu.exec(tonePresetBtn->mapToGlobal(QPoint(0, tonePresetBtn->height())));
            });
    connect(ctcssPresetBtn, &QPushButton::clicked, ctcssPresetBtn,
            [ctcssPresetBtn, setCtcssPick, styleToneGridButton]()
            {
                QMenu menu(ctcssPresetBtn);
                styleCompactMenu(&menu);
                auto* panel = new QWidget(&menu);
                auto* grid = new QGridLayout(panel);
                grid->setContentsMargins(6, 6, 6, 6);
                grid->setHorizontalSpacing(4);
                grid->setVerticalSpacing(4);
                static constexpr int kCols = 4;
                int idx = 0;
                auto* noneBtn = new QPushButton(QStringLiteral("NONE"), panel);
                styleToneGridButton(noneBtn);
                connect(noneBtn, &QPushButton::clicked, &menu,
                        [setCtcssPick, menuPtr = &menu]()
                        {
                            setCtcssPick(0, QStringLiteral("NONE"));
                            menuPtr->close();
                        });
                grid->addWidget(noneBtn, idx / kCols, idx % kCols);
                ++idx;
                for (const TonePreset& preset : kTonePresets)
                {
                    const QString label = QString::fromLatin1(preset.label);
                    const ushort tone = preset.tone;
                    auto* btn = new QPushButton(label, panel);
                    styleToneGridButton(btn);
                    connect(btn, &QPushButton::clicked, &menu,
                            [setCtcssPick, label, tone, menuPtr = &menu]()
                            {
                                setCtcssPick(tone, label);
                                menuPtr->close();
                            });
                    grid->addWidget(btn, idx / kCols, idx % kCols);
                    ++idx;
                }
                auto* action = new QWidgetAction(&menu);
                action->setDefaultWidget(panel);
                menu.addAction(action);
                menu.exec(ctcssPresetBtn->mapToGlobal(QPoint(0, ctcssPresetBtn->height())));
            });
    connect(dtcsPresetBtn, &QPushButton::clicked, dtcsPresetBtn,
            [dtcsPresetBtn, setDtcsPick, styleToneGridButton]()
            {
                QMenu menu(dtcsPresetBtn);
                styleCompactMenu(&menu);
                auto* panel = new QWidget(&menu);
                auto* grid = new QGridLayout(panel);
                grid->setContentsMargins(6, 6, 6, 6);
                grid->setHorizontalSpacing(4);
                grid->setVerticalSpacing(4);
                static constexpr int kCols = 6;
                int idx = 0;
                auto* noneBtn = new QPushButton(QStringLiteral("NONE"), panel);
                styleToneGridButton(noneBtn);
                connect(noneBtn, &QPushButton::clicked, &menu,
                        [setDtcsPick, menuPtr = &menu]()
                        {
                            setDtcsPick(0, QStringLiteral("NONE"));
                            menuPtr->close();
                        });
                grid->addWidget(noneBtn, idx / kCols, idx % kCols);
                ++idx;
                for (const ushort code : kDtcsCodes)
                {
                    const QString label = dtcsCodeLabel(code);
                    auto* btn = new QPushButton(label, panel);
                    styleToneGridButton(btn);
                    connect(btn, &QPushButton::clicked, &menu,
                            [setDtcsPick, label, code, menuPtr = &menu]()
                            {
                                setDtcsPick(code, label);
                                menuPtr->close();
                            });
                    grid->addWidget(btn, idx / kCols, idx % kCols);
                    ++idx;
                }
                auto* action = new QWidgetAction(&menu);
                action->setDefaultWidget(panel);
                menu.addAction(action);
                menu.exec(dtcsPresetBtn->mapToGlobal(QPoint(0, dtcsPresetBtn->height())));
            });
    connect(dtcsRxPresetBtn, &QPushButton::clicked, dtcsRxPresetBtn,
            [dtcsRxPresetBtn, setDtcsRxPick, styleToneGridButton]()
            {
                QMenu menu(dtcsRxPresetBtn);
                styleCompactMenu(&menu);
                auto* panel = new QWidget(&menu);
                auto* grid = new QGridLayout(panel);
                grid->setContentsMargins(6, 6, 6, 6);
                grid->setHorizontalSpacing(4);
                grid->setVerticalSpacing(4);
                static constexpr int kCols = 6;
                int idx = 0;
                auto* noneBtn = new QPushButton(QStringLiteral("NONE"), panel);
                styleToneGridButton(noneBtn);
                connect(noneBtn, &QPushButton::clicked, &menu,
                        [setDtcsRxPick, menuPtr = &menu]()
                        {
                            setDtcsRxPick(0, QStringLiteral("NONE"));
                            menuPtr->close();
                        });
                grid->addWidget(noneBtn, idx / kCols, idx % kCols);
                ++idx;
                for (const ushort code : kDtcsCodes)
                {
                    const QString label = dtcsCodeLabel(code);
                    auto* btn = new QPushButton(label, panel);
                    styleToneGridButton(btn);
                    connect(btn, &QPushButton::clicked, &menu,
                            [setDtcsRxPick, label, code, menuPtr = &menu]()
                            {
                                setDtcsRxPick(code, label);
                                menuPtr->close();
                            });
                    grid->addWidget(btn, idx / kCols, idx % kCols);
                    ++idx;
                }
                auto* action = new QWidgetAction(&menu);
                action->setDefaultWidget(panel);
                menu.addAction(action);
                menu.exec(dtcsRxPresetBtn->mapToGlobal(QPoint(0, dtcsRxPresetBtn->height())));
            });
    connect(frequencyEdit, &QLineEdit::editingFinished, editor,
            [populateOffsetOptions, updateCustomOffsetVisibility]()
            {
                populateOffsetOptions();
                updateCustomOffsetVisibility();
            });
    connect(offsetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), editor, updateCustomOffsetVisibility);

    auto applyMemoryToForm = [this, groupCombo, channelCombo, nameEdit, frequencyEdit, modeCombo, filterCombo,
                              dataModeCombo, scanGroupCombo, populateOffsetOptions, setOffsetSelection,
                              updateCustomOffsetVisibility, toneOptionCombo, toneEdit, tsqlEdit, dsqlCombo, dtcsSpin,
                              dtcsRxSpin, dtcsPolarityCombo, dtcsRxPolarityCombo, dvSqlSpin, urEdit, r1Edit, r2Edit,
                              setTonePick, setCtcssPick, setDtcsPick, setDtcsRxPick, populateToneValues,
                              updateConditionalSections](const MemoryRecord& memory)
    {
        quint16 group = kRadioMemoryFirstGroup;
        quint16 channel = kRadioMemoryFirstChannel;
        parseRadioMemoryId(memory.id, &group, &channel);
        groupCombo->setCurrentIndex(qMax(0, groupCombo->findData(group)));
        channelCombo->setCurrentIndex(qMax(0, channelCombo->findData(channel)));
        nameEdit->setText(memory.name);
        frequencyEdit->setText(memoryFrequencyLabel(memory.receiveHz));
        modeCombo->setCurrentIndex(qMax(0, modeCombo->findData(memory.mode)));
        filterCombo->setCurrentIndex(qMax(0, filterCombo->findData(memory.filter)));
        dataModeCombo->setCurrentIndex(qMax(0, dataModeCombo->findData(memory.dataMode)));
        scanGroupCombo->setCurrentIndex(qMax(0, scanGroupCombo->findData(memory.scan)));
        populateOffsetOptions();
        setOffsetSelection(static_cast<duplexMode_t>(memory.duplexMode), memory.offsetHz);
        updateCustomOffsetVisibility();
        const auto toneModeForEditor = static_cast<rptAccessTxRx_t>(memory.toneMode);
        toneOptionCombo->setCurrentIndex(
            qMax(0, toneOptionCombo->findData(memoryToneFamilyForMode(toneModeForEditor))));
        toneEdit->setText(memory.tone);
        tsqlEdit->setText(memory.tsql);
        dsqlCombo->setCurrentIndex(qMax(0, dsqlCombo->findData(memory.dsql)));
        dtcsSpin->setValue(memory.dtcs);
        dtcsRxSpin->setValue(memory.dtcsB);
        dtcsPolarityCombo->setCurrentIndex(qMax(0, dtcsPolarityCombo->findData(memory.dtcsPolarity)));
        dtcsRxPolarityCombo->setCurrentIndex(qMax(0, dtcsRxPolarityCombo->findData(memory.dtcsPolarityB)));
        dvSqlSpin->setValue(memory.dvSql);
        urEdit->setText(memory.urCall);
        r1Edit->setText(memory.r1Call);
        r2Edit->setText(memory.r2Call);
        populateToneValues();
        updateConditionalSections();
        if (toneModeForEditor != ratrNN)
        {
            const bool isDtcs = isDtcsToneMode(toneModeForEditor);
            if (isDtcs)
            {
                setDtcsPick(memory.dtcs, dtcsCodeLabel(memory.dtcs));
                if (toneModeForEditor == ratrDD || toneModeForEditor == ratrDT)
                {
                    setDtcsRxPick(memory.dtcsB, dtcsCodeLabel(memory.dtcsB));
                }
            }
            else
            {
                if (toneModeForEditor == ratrTN || toneModeForEditor == ratrTT || toneModeForEditor == ratrTD)
                {
                    const ushort txTone = toneValueFromRadioText(
                        !memory.tone.isEmpty() ? memory.tone : toneFrequencyLabel(memory.toneValue));
                    if (txTone > 0)
                    {
                        setTonePick(txTone, toneFrequencyLabel(txTone));
                    }
                }
                if (toneModeForEditor == ratrNT || toneModeForEditor == ratrTT || toneModeForEditor == ratrDT)
                {
                    const ushort rxTone = toneValueFromRadioText(
                        !memory.tsql.isEmpty() ? memory.tsql : toneFrequencyLabel(memory.toneValue));
                    if (rxTone > 0)
                    {
                        setCtcssPick(rxTone, toneFrequencyLabel(rxTone));
                    }
                }
            }
        }
    };

    if (editing)
    {
        bool found = false;
        const MemoryRecord memory = memoryForId(memoryId, &found);
        if (!found)
        {
            QMessageBox::information(parent, "Edit Memory", "Select a memory to edit.");
            closeMemoryEditorPane();
            return;
        }
        applyMemoryToForm(memory);
    }
    else
    {
        quint16 firstOpenChannel = kRadioMemoryFirstChannel;
        if (firstOpenChannelForGroup(kRadioMemoryFirstGroup, &firstOpenChannel))
        {
            groupCombo->setCurrentIndex(qMax(0, groupCombo->findData(kRadioMemoryFirstGroup)));
            channelCombo->setCurrentIndex(qMax(0, channelCombo->findData(firstOpenChannel)));
        }
    }

    updateCustomOffsetVisibility();
    updateConditionalSections();

    m_openMemoryEditorId = memoryId;
    if (m_memoryEditButton)
    {
        m_memoryEditButton->setChecked(editing);
    }

    root->addStretch(1);
    root->addSpacing(kMemoryEditorGutter);
    auto* footerSeparator = new QWidget(editor);
    footerSeparator->setFixedHeight(1);
    footerSeparator->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    footerSeparator->setStyleSheet(
        QStringLiteral("QWidget { background: %1; }").arg(QLatin1String(UiTheme::Color::BorderMedium)));
    root->addWidget(footerSeparator);
    root->addSpacing(kMemoryEditorGutter);
    auto* buttonRow = new QWidget(editor);
    auto* buttonRowLayout = new QHBoxLayout(buttonRow);
    buttonRowLayout->setContentsMargins(0, 0, 0, 0);
    auto* copyButton = new QPushButton("Copy Current", buttonRow);
    copyButton->setMinimumWidth(copyButton->sizeHint().width() + 20);
    auto* saveButton = new QPushButton("Save", buttonRow);
    auto* cancelButton = new QPushButton("Cancel", buttonRow);
    buttonRowLayout->addWidget(copyButton, 0, Qt::AlignLeft);
    buttonRowLayout->addStretch(1);
    buttonRowLayout->addWidget(saveButton, 0, Qt::AlignRight);
    buttonRowLayout->addWidget(cancelButton, 0, Qt::AlignRight);
    root->addWidget(buttonRow);
    resizeEditorToContents();
    connect(copyButton, &QPushButton::clicked, editor, copyCurrentSettings);
    connect(cancelButton, &QPushButton::clicked, this,
            [this]() { QTimer::singleShot(0, this, [this]() { closeMemoryEditorPane(); }); });

    connect(
        saveButton, &QPushButton::clicked, editor,
        [this, editor, frequencyEdit, toneOptionCombo, toneEdit, tsqlEdit, dtcsSpin, dtcsRxSpin, nameEdit, modeCombo,
         filterCombo, dataModeCombo, scanGroupCombo, offsetCombo, customOffsetModeCombo, customOffsetSpin, dsqlCombo,
         dtcsPolarityCombo, dtcsRxPolarityCombo, dvSqlSpin, urEdit, r1Edit, r2Edit, groupCombo, channelCombo, editing,
         memoryId, parent]()
        {
            quint64 receiveHz = 0;
            if (!parseFrequencyText(frequencyEdit->text(), &receiveHz))
            {
                QMessageBox::warning(editor, "Add/Edit Memory", "Enter a valid receive frequency.");
                frequencyEdit->setFocus();
                frequencyEdit->selectAll();
                return;
            }

            auto toneMode = ratrNN;
            ushort toneValue = 0;
            const QString toneText = toneEdit->text().trimmed();
            const QString tsqlText = tsqlEdit->text().trimmed();
            const ushort txTone = toneValueFromRadioText(toneText);
            const ushort rxTone = toneValueFromRadioText(tsqlText);
            const ushort txDtcs = static_cast<ushort>(dtcsSpin->value());
            const ushort rxDtcs = static_cast<ushort>(dtcsRxSpin->value());
            const auto toneFamily = static_cast<MemoryToneFamily>(toneOptionCombo->currentData().toInt());
            if (toneFamily == MemoryToneTone)
            {
                if (rxTone > 0 && txTone > 0)
                {
                    toneMode = ratrTT;
                    toneValue = txTone;
                }
                else if (rxTone > 0)
                {
                    toneMode = ratrNT;
                    toneValue = rxTone;
                }
                else if (txTone > 0)
                {
                    toneMode = ratrTN;
                    toneValue = txTone;
                }
            }
            else if (toneFamily == MemoryToneDtcs)
            {
                if (txDtcs > 0 && rxDtcs > 0)
                {
                    toneMode = ratrDD;
                    toneValue = txDtcs;
                }
                else if (txDtcs > 0)
                {
                    toneMode = ratrDN;
                    toneValue = txDtcs;
                }
            }

            MemoryRecord memory;
            memory.id = editing ? memoryId : QString();
            memory.name = nameEdit->text().trimmed();
            if (memory.name.length() > kRadioMemoryNameMaxChars)
            {
                QMessageBox::warning(
                    editor, "Add/Edit Memory",
                    QStringLiteral("Radio memory names are limited to %1 characters.").arg(kRadioMemoryNameMaxChars));
                nameEdit->setFocus();
                nameEdit->selectAll();
                return;
            }
            if (memory.name.isEmpty())
            {
                memory.name = memoryFrequencyLabel(receiveHz);
            }
            memory.receiveHz = receiveHz;
            memory.mode = modeCombo->currentData().toInt();
            memory.filter = filterCombo->currentData().toInt();
            memory.dataMode = dataModeCombo->currentData().toInt();
            memory.scan = scanGroupCombo->currentData().toInt();
            memory.band = bandLabelForHz(memory.receiveHz);
            memory.bandKey = memoryBandKeyForHz(memory.receiveHz);
            if (!modeSupportsMemoryOffset(memory.mode))
            {
                memory.duplexMode = dmSimplex;
                memory.offsetHz = 0;
            }
            else if (offsetCombo->currentData(Qt::UserRole).toInt() == kMemoryOffsetCustom)
            {
                memory.duplexMode = customOffsetModeCombo->currentData().toInt();
                memory.offsetHz = static_cast<quint64>(customOffsetSpin->value() * 1000000.0 + 0.5);
            }
            else
            {
                memory.duplexMode = offsetCombo->currentData(Qt::UserRole).toInt();
                memory.offsetHz = offsetCombo->currentData(Qt::UserRole + 1).toULongLong();
            }
            memory.shift = offsetModeLabel(static_cast<duplexMode_t>(memory.duplexMode), memory.offsetHz);
            memory.toneMode = static_cast<int>(toneMode);
            memory.toneValue = toneValue;
            memory.toneOption = toneOptionLabel(toneMode);
            memory.toneFrequency = memoryToneFrequencyLabel(toneMode, toneValue);
            memory.tone = toneText;
            memory.tsql = tsqlText;
            memory.dsql = dsqlCombo->currentData().toInt();
            memory.dtcs = static_cast<ushort>(dtcsSpin->value());
            memory.dtcsPolarity = dtcsPolarityCombo->currentData().toInt();
            memory.dtcsB = static_cast<ushort>(dtcsRxSpin->value());
            memory.dtcsPolarityB = dtcsRxPolarityCombo->currentData().toInt();
            memory.dvSql = dvSqlSpin->value();
            memory.urCall = urEdit->text().trimmed().toUpper();
            memory.r1Call = r1Edit->text().trimmed().toUpper();
            memory.r2Call = r2Edit->text().trimmed().toUpper();

            const quint16 group = static_cast<quint16>(groupCombo->currentData().toUInt());
            const quint16 channel = static_cast<quint16>(channelCombo->currentData().toUInt());
            if (editing)
            {
                quint16 oldGroup = 0;
                quint16 oldChannel = 0;
                if (!parseRadioMemoryId(memoryId, &oldGroup, &oldChannel))
                {
                    QMessageBox::warning(parent, "Add/Edit Memory", "Could not identify the selected radio memory.");
                    return;
                }
                if (group != oldGroup || channel != oldChannel)
                {
                    deleteRadioMemory(oldGroup, oldChannel);
                }
            }
            else if (m_radioMemoriesByKey.contains(radioMemoryKey(group, channel)))
            {
                if (QMessageBox::question(parent, "Add/Edit Memory",
                                          QStringLiteral("Overwrite radio memory group %1 channel %2?")
                                              .arg(group)
                                              .arg(channel, 3, 10, QLatin1Char('0'))) != QMessageBox::Yes)
                {
                    return;
                }
            }

            writeMemoryRecord(memory, group, channel);
            reloadMemoryTable();
            QTimer::singleShot(0, this, [this]() { closeMemoryEditorPane(); });
            m_window->showToast(editing ? QStringLiteral("Memory updated") : QStringLiteral("Memory stored"));
        });
}

void MemoryController::reloadMemoryTable()
{
    rebuildMemoryViews();
}
