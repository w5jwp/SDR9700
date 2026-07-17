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
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStringList>
#include <QStyle>
#include <QStyledItemDelegate>
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
constexpr int kRadioMemorySyncTimeoutMs = 30000;
constexpr int kRadioMemorySyncMaxPasses = 3;
constexpr int kRadioMemoryWriteIntervalMs = 100;
constexpr int kRadioMemoryWriteReadbackTimeoutMs = 3000;
constexpr int kRadioMemoryNameMaxChars = 16;
constexpr int kMemoryEditorPaneWidth = 420;
constexpr int kMemoryEditorFieldHeight = 30;
constexpr int kMemoryEditorGutter = 10;
constexpr int kMemoryEditorLabelFieldSpacing = 6;
constexpr int kMemoryFooterTopPadding = 8;
constexpr int kMemoryFooterBottomPadding = 10;
constexpr int kMemoryFooterTextLeftPadding = 6;
constexpr int kMemoryToneCellTextPadding = 8;
constexpr int kMemoryToneTypeSectionWidth = 62;
constexpr int kMemoryToneTypeRole = Qt::UserRole + 1;
constexpr int kMemoryToneRxRole = Qt::UserRole + 2;
constexpr int kMemoryToneTxRole = Qt::UserRole + 3;
constexpr auto kMemoryFileFilter = "SDR9700 Memories (*.csv);;CSV Files (*.csv);;All Files (*)";

enum MemoryToneFamily
{
    MemoryToneOff = 0,
    MemoryToneTone,
    MemoryToneDtcs
};

class ToneCellDelegate : public QStyledItemDelegate
{
  public:
    explicit ToneCellDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const QString type = index.data(kMemoryToneTypeRole).toString();
        if (type.isEmpty() || type == QLatin1String("OFF"))
        {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        QStyleOptionViewItem itemOption(option);
        initStyleOption(&itemOption, index);
        itemOption.text.clear();
        const QWidget* widget = itemOption.widget;
        QStyle* style = widget ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &itemOption, painter, widget);

        const QString rx = index.data(kMemoryToneRxRole).toString();
        const QString tx = index.data(kMemoryToneTxRole).toString();
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const QColor textColor =
            selected ? option.palette.color(QPalette::HighlightedText) : option.palette.color(QPalette::Text);
        QRect rect = option.rect.adjusted(5, 0, -5, 0);
        if (rect.width() < 24 || rect.height() < 8)
        {
            return;
        }

        const int typeWidth = qMin(kMemoryToneTypeSectionWidth, rect.width() / 3);
        const int valueWidth = (rect.width() - typeWidth) / 2;
        const QRect typeRect(rect.left(), rect.top(), typeWidth, rect.height());
        const QRect txRect(typeRect.right() + 1, rect.top(), valueWidth, rect.height());
        const QRect rxRect(txRect.right() + 1, rect.top(), rect.right() - txRect.right(), rect.height());

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setPen(textColor);
        painter->drawText(typeRect.adjusted(kMemoryToneCellTextPadding, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                          type);
        painter->drawText(txRect.adjusted(kMemoryToneCellTextPadding, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("TX: %1").arg(tx.isEmpty() ? QStringLiteral("OFF") : tx));
        painter->drawText(rxRect.adjusted(kMemoryToneCellTextPadding, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("RX: %1").arg(rx.isEmpty() ? QStringLiteral("OFF") : rx));
        painter->restore();
    }
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
    const QVector<OffsetPreset> presets = offsetPresetsForHz(hz);
    const auto preset = std::find_if(presets.cbegin(), presets.cend(),
                                     [mode](const OffsetPreset& option) { return option.mode == mode; });
    if (preset != presets.cend())
    {
        return preset->hz;
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

QString normalizedToneText(QString text)
{
    text = text.trimmed();
    if (text.isEmpty())
    {
        return QString();
    }

    const ushort value = toneValueFromRadioText(text);
    return value > 0 ? toneFrequencyLabel(value) : text;
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
    memory.group = radioMemory.group;
    memory.channel = radioMemory.channel;
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
        radioMemory.tone = normalizedToneText(memory.tone.isEmpty() ? fallbackTone : memory.tone);
        radioMemory.tsql = normalizedToneText(memory.tsql.isEmpty() ? fallbackTone : memory.tsql);
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

QVector<MemoryType> deletedUserRadioMemories()
{
    QVector<MemoryType> deletes;
    deletes.reserve((kRadioMemoryLastGroup - kRadioMemoryFirstGroup + 1) * kRadioMemoryLastChannel);
    for (quint16 group = kRadioMemoryFirstGroup; group <= kRadioMemoryLastGroup; ++group)
    {
        for (quint16 channel = kRadioMemoryFirstChannel; channel <= kRadioMemoryLastChannel; ++channel)
        {
            deletes.append(deletedRadioMemory(group, channel));
        }
    }
    return deletes;
}

QString dtcsMemoryValue(ushort code, int polarity)
{
    return QStringLiteral("%1%2").arg(dtcsCodeLabel(code), polarity == 3 ? QStringLiteral("R") : QStringLiteral("N"));
}

QString memoryToneTypeLabel(const MemoryRecord& memory)
{
    const auto toneMode = static_cast<rptAccessTxRx_t>(memory.toneMode);
    if (toneMode == ratrNN)
    {
        return QStringLiteral("OFF");
    }
    return isDtcsToneMode(toneMode) ? QStringLiteral("DTCS") : QStringLiteral("TONE");
}

QString memoryToneRxLabel(const MemoryRecord& memory)
{
    const auto toneMode = static_cast<rptAccessTxRx_t>(memory.toneMode);
    if (toneMode == ratrNN || toneMode == ratrTN || toneMode == ratrDN)
    {
        return QStringLiteral("OFF");
    }
    if (isDtcsToneMode(toneMode))
    {
        return dtcsMemoryValue(memory.dtcsB, memory.dtcsPolarityB);
    }
    return memory.tsql.isEmpty() ? memory.tone : memory.tsql;
}

QString memoryToneTxLabel(const MemoryRecord& memory)
{
    const auto toneMode = static_cast<rptAccessTxRx_t>(memory.toneMode);
    if (toneMode == ratrNN || toneMode == ratrNT)
    {
        return QStringLiteral("OFF");
    }
    if (isDtcsToneMode(toneMode))
    {
        return dtcsMemoryValue(memory.dtcs, memory.dtcsPolarity);
    }
    return memory.tone.isEmpty() ? memory.tsql : memory.tone;
}

QString memoryToneTableLabel(const MemoryRecord& memory)
{
    const QString type = memoryToneTypeLabel(memory);
    if (type == QLatin1String("OFF"))
    {
        return QStringLiteral("OFF");
    }
    return QStringLiteral("%1: %2/%3").arg(type, memoryToneTxLabel(memory), memoryToneRxLabel(memory));
}

QString memoryFilterLabel(int filter)
{
    if (filter >= 1 && filter <= 3)
    {
        return QStringLiteral("FIL%1").arg(filter);
    }
    return QString::number(filter);
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
    setMemoryPollIntervalSeconds(
        AppSettings::instance()
            .value(QString::fromLatin1(kMemoryPollIntervalSecondsSettingsKey), kDefaultMemoryPollIntervalSeconds)
            .toInt());
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

    m_memoryViewRefreshTimer = new QTimer(this);
    m_memoryViewRefreshTimer->setInterval(250);
    m_memoryViewRefreshTimer->setSingleShot(true);
    connect(m_memoryViewRefreshTimer, &QTimer::timeout, this, &MemoryController::rebuildMemoryViews);

    m_radioMemoryWriteTimeoutTimer = new QTimer(this);
    m_radioMemoryWriteTimeoutTimer->setSingleShot(true);
    m_radioMemoryWriteTimeoutTimer->setInterval(kRadioMemoryWriteReadbackTimeoutMs);
    connect(m_radioMemoryWriteTimeoutTimer, &QTimer::timeout, this,
            [this]()
            {
                if (m_waitingForRadioMemoryWriteReadback)
                {
                    finishQueuedRadioMemoryWrites(true);
                }
            });

    connect(m_window->m_model, &RadioModel::radioMemoryReceived, this, &MemoryController::handleRadioMemoryReceived);
    connect(m_window->m_model, &RadioModel::readyChanged, this,
            [this](bool ready)
            {
                if (ready)
                {
                    if (m_initialMemorySyncComplete)
                    {
                        m_initialMemorySyncComplete = false;
                        emit initialMemorySyncChanged(false);
                    }
                    m_window->showToast(QStringLiteral("Syncing radio memories..."));
                    requestRadioMemoryRefresh();
                    m_radioMemoryPeriodicRefreshTimer->start();
                    return;
                }
                m_radioMemoryRefreshTimer->stop();
                m_radioMemoryPeriodicRefreshTimer->stop();
                m_memoryViewRefreshTimer->stop();
                finishRadioMemoryRefresh(false);
                if (m_initialMemorySyncComplete)
                {
                    m_initialMemorySyncComplete = false;
                    emit initialMemorySyncChanged(false);
                }
                m_radioMemoriesByKey.clear();
                m_receivedRadioMemoryKeys.clear();
                m_expectedRadioMemoryKeys.clear();
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
    m_window->m_memoryBandFilter->setFixedHeight(syncButton->sizeHint().height());
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
        {QStringLiteral("Band"), QStringLiteral("Channel"), QStringLiteral("Name"), QStringLiteral("Frequency"),
         QStringLiteral("Offset"), QStringLiteral("Mode"), QStringLiteral("Tone (TX/RX)"), QStringLiteral("Filter"),
         QStringLiteral("ID")});
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
    m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryBandColumn, QHeaderView::Interactive);
    m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryNumberColumn, QHeaderView::Interactive);
    m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryNameColumn, QHeaderView::Interactive);
    m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryFrequencyColumn, QHeaderView::Interactive);
    m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryDuplexColumn, QHeaderView::Interactive);
    m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryModeColumn, QHeaderView::Interactive);
    m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryToneColumn, QHeaderView::Interactive);
    m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryFilterColumn, QHeaderView::Stretch);
    m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryIdColumn, QHeaderView::Fixed);
    m_window->m_memoryTable->setColumnWidth(kMemoryBandColumn, kMemoryBandColumnWidth);
    m_window->m_memoryTable->setColumnWidth(kMemoryNumberColumn, kMemoryNumberColumnWidth);
    m_window->m_memoryTable->setColumnWidth(kMemoryNameColumn, kMemoryNameColumnWidth);
    m_window->m_memoryTable->setColumnWidth(kMemoryFrequencyColumn, kMemoryFrequencyColumnWidth);
    m_window->m_memoryTable->setColumnWidth(kMemoryDuplexColumn, kMemoryDuplexColumnWidth);
    m_window->m_memoryTable->setColumnWidth(kMemoryModeColumn, kMemoryModeColumnWidth);
    m_window->m_memoryTable->setColumnWidth(kMemoryToneColumn, kMemoryToneColumnWidth);
    m_window->m_memoryTable->setColumnWidth(kMemoryFilterColumn, kMemorySmallColumnWidth);
    m_window->m_memoryTable->setItemDelegateForColumn(kMemoryToneColumn, new ToneCellDelegate(m_window->m_memoryTable));
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

    auto* footerRow = new QWidget(panel);
    auto* footer = new QHBoxLayout(footerRow);
    footer->setContentsMargins(0, kMemoryFooterTopPadding, 0, kMemoryFooterBottomPadding);
    m_window->m_memoryCountLabel = new QLabel(panel);
    m_window->m_memoryCountLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_window->m_memoryCountLabel->setContentsMargins(kMemoryFooterTextLeftPadding, 0, 0, 0);
    m_window->m_memoryCountLabel->setStyleSheet("QLabel { color: palette(mid); }");
    m_window->m_memoryProgressBar = new QProgressBar(panel);
    m_window->m_memoryProgressBar->setFixedWidth(220);
    m_window->m_memoryProgressBar->setTextVisible(false);
    m_window->m_memoryProgressBar->setVisible(false);
    m_window->m_memoryProgressBar->setStyleSheet(
        QStringLiteral("QProgressBar { background: %1; border: 1px solid %2; border-radius: 3px; height: 8px; }"
                       "QProgressBar::chunk { background: %3; border-radius: 2px; }")
            .arg(UiTheme::Color::Field, UiTheme::Color::BorderMedium, UiTheme::Color::Accent));
    auto* closeButton = new QPushButton("Close", panel);
    footer->addWidget(m_window->m_memoryCountLabel, 1);
    footer->addWidget(m_window->m_memoryProgressBar);
    footer->addWidget(closeButton);
    leftRoot->addSpacing(kMemoryEditorGutter);
    auto* leftFooterSeparator = new QWidget(panel);
    leftFooterSeparator->setFixedHeight(1);
    leftFooterSeparator->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    leftFooterSeparator->setStyleSheet(
        QStringLiteral("QWidget { background: %1; }").arg(QLatin1String(UiTheme::Color::BorderMedium)));
    leftRoot->addWidget(leftFooterSeparator);
    leftRoot->addWidget(footerRow);

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
    if (!m_memoryProgressLabel.isEmpty())
    {
        QMessageBox::information(popupParent(), QStringLiteral("Sync Memories"),
                                 QStringLiteral("Wait for the current memory operation to finish before syncing."));
        return;
    }

    requestRadioMemoryRefresh();
    reloadMemoryTable();
    m_window->showToast(QStringLiteral("Radio memory sync started"));
}

void MemoryController::setMemoryPollIntervalSeconds(int seconds)
{
    const int boundedSeconds = qBound(kMemoryPollIntervalMinSeconds, seconds, kMemoryPollIntervalMaxSeconds);
    m_radioMemoryPeriodicRefreshTimer->setInterval(boundedSeconds * 1000);
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
    m_refreshPass = 1;
    m_refreshInProgress = true;
    m_receivedRadioMemoryKeys.clear();
    m_expectedRadioMemoryKeys.clear();
    // Radio Ready depends on this first sync. Track every requested memory slot
    // and complete only after the radio replies for all of them, not merely
    // after all requests have been queued.
    for (quint16 group = kRadioMemoryFirstGroup; group <= kRadioMemoryLastGroup; ++group)
    {
        for (quint16 channel = kRadioMemoryFirstChannel; channel <= kRadioMemoryLastChannel; ++channel)
        {
            m_expectedRadioMemoryKeys.insert(radioMemoryKey(group, channel));
        }
    }
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
        if (allExpectedRadioMemoriesReceived())
        {
            m_radioMemoryRefreshTimer->stop();
            finishRadioMemoryRefresh(false);
            return;
        }

        if (m_refreshPass < kRadioMemorySyncMaxPasses)
        {
            ++m_refreshPass;
            m_refreshGroup = kRadioMemoryFirstGroup;
            m_refreshChannel = kRadioMemoryFirstChannel;
            // Re-run the full memory range instead of trying to schedule only
            // missing keys. The radio is already being polled at a conservative
            // interval, and the simple full-pass retry is easier to back out if
            // later packet captures show a better IC-9700 behavior.
            setMemoryProgress(
                QStringLiteral("Retrying radio memory sync (%1/%2)").arg(m_refreshPass).arg(kRadioMemorySyncMaxPasses),
                m_receivedRadioMemoryKeys.size(), m_expectedRadioMemoryKeys.size());
            return;
        }

        m_radioMemoryRefreshTimer->stop();
        setMemoryProgress(QStringLiteral("Waiting for radio memory replies"), m_receivedRadioMemoryKeys.size(),
                          m_expectedRadioMemoryKeys.size());
        return;
    }

    m_currentSyncGroup = m_refreshGroup;
    m_currentSyncChannel = m_refreshChannel;
    const int syncIndex =
        (m_currentSyncGroup - kRadioMemoryFirstGroup) * (kRadioMemoryLastChannel - kRadioMemoryFirstChannel + 1) +
        (m_currentSyncChannel - kRadioMemoryFirstChannel + 1);
    QString progressLabel = QStringLiteral("Syncing %1 channel %2")
                                .arg(memoryBandLabelForGroup(m_currentSyncGroup))
                                .arg(m_currentSyncChannel, 3, 10, QLatin1Char('0'));
    if (m_refreshPass > 1)
    {
        progressLabel.append(QStringLiteral(" (%1/%2)").arg(m_refreshPass).arg(kRadioMemorySyncMaxPasses));
    }
    setMemoryProgress(progressLabel, syncIndex, kRadioMemorySyncTotal);
    m_window->m_model->requestRadioMemory(m_refreshGroup, m_refreshChannel);
    ++m_refreshChannel;
    if (m_refreshChannel > kRadioMemoryLastChannel)
    {
        m_refreshChannel = kRadioMemoryFirstChannel;
        ++m_refreshGroup;
    }
}

bool MemoryController::allExpectedRadioMemoriesReceived() const
{
    return !m_expectedRadioMemoryKeys.isEmpty() &&
           std::all_of(m_expectedRadioMemoryKeys.cbegin(), m_expectedRadioMemoryKeys.cend(),
                       [this](quint32 key) { return m_receivedRadioMemoryKeys.contains(key); });
}

void MemoryController::finishRadioMemoryRefresh(bool timedOut)
{
    m_radioMemoryRefreshTimer->stop();
    m_radioMemorySyncTimeoutTimer->stop();
    m_memoryViewRefreshTimer->stop();
    const bool wasInProgress = m_refreshInProgress;
    const bool completedFullSync = wasInProgress && !timedOut && allExpectedRadioMemoriesReceived();
    const bool resetAfterSync = m_resetAfterSync;
    m_resetAfterSync = false;
    m_refreshInProgress = false;
    m_currentSyncGroup = 0;
    m_currentSyncChannel = 0;
    m_refreshPass = 0;
    m_expectedRadioMemoryKeys.clear();
    clearMemoryProgress();
    if (timedOut && wasInProgress)
    {
        m_window->showToast(QStringLiteral("Radio memory sync timed out"), 5000, MainWindow::ToastKind::Warning);
        if (resetAfterSync)
        {
            m_window->showToast(QStringLiteral("Memory reset canceled because sync timed out"), 5000,
                                MainWindow::ToastKind::Warning);
        }
    }
    else if (completedFullSync && !m_initialMemorySyncComplete)
    {
        m_initialMemorySyncComplete = true;
        emit initialMemorySyncChanged(true);
    }
    rebuildMemoryViews();
    if (resetAfterSync && !timedOut && wasInProgress)
    {
        QTimer::singleShot(500, this, &MemoryController::resetStoredRadioMemoriesAfterSync);
    }
}

void MemoryController::scheduleMemoryViewsRebuild()
{
    if (!m_refreshInProgress)
    {
        rebuildMemoryViews();
        return;
    }

    if (!m_memoryViewRefreshTimer->isActive())
    {
        m_memoryViewRefreshTimer->start();
    }
}

void MemoryController::resetStoredRadioMemoriesAfterSync()
{
    if (!m_window->m_model || !m_window->m_model->isConnected())
    {
        return;
    }
    if (m_refreshInProgress || !m_memoryProgressLabel.isEmpty())
    {
        return;
    }

    QVector<MemoryType> deletes;
    deletes.reserve(m_radioMemoriesByKey.size());
    for (const MemoryType& memory : m_radioMemoriesByKey)
    {
        deletes.append(deletedRadioMemory(memory.group, memory.channel));
    }
    std::sort(deletes.begin(), deletes.end(),
              [](const MemoryType& left, const MemoryType& right)
              {
                  if (left.group == right.group)
                  {
                      return left.channel < right.channel;
                  }
                  return left.group < right.group;
              });

    if (deletes.isEmpty())
    {
        m_window->showToast(QStringLiteral("No stored memories to reset"));
        rebuildMemoryViews();
        return;
    }

    queueRadioMemoryWrites(deletes, 0, QStringLiteral("Clearing stored memories"),
                           [this]()
                           {
                               m_radioMemoriesByKey.clear();
                               m_receivedRadioMemoryKeys.clear();
                               m_expectedRadioMemoryKeys.clear();
                               rebuildMemoryViews();
                               requestRadioMemoryRefresh();
                           });
}

void MemoryController::updateMemoryTableInteraction()
{
    if (!m_window->m_memoryTable)
    {
        return;
    }

    const bool locked = m_refreshInProgress || !m_memoryProgressLabel.isEmpty();
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
    const bool completedQueuedWrite = m_waitingForRadioMemoryWriteReadback && key == m_expectedRadioMemoryWriteKey;
    auto advanceQueuedWrite = [this, completedQueuedWrite]()
    {
        if (!completedQueuedWrite)
        {
            return;
        }

        m_radioMemoryWriteTimeoutTimer->stop();
        m_waitingForRadioMemoryWriteReadback = false;
        ++m_queuedRadioMemoryWriteIndex;
        setMemoryProgress(m_queuedRadioMemoryWriteLabel, m_queuedRadioMemoryWriteIndex,
                          m_queuedRadioMemoryWrites.size());
        QTimer::singleShot(kRadioMemoryWriteIntervalMs, this, &MemoryController::writeNextQueuedRadioMemory);
    };
    m_receivedRadioMemoryKeys.insert(key);
    if (radioMemoryIsStored(memory))
    {
        m_radioMemoriesByKey.insert(key, memory);
        if (m_window->m_activeMemoryId == radioMemoryId(memory.group, memory.channel))
        {
            const MemoryRecord activeMemory = recordFromRadioMemory(memory);
            m_window->setActiveMemory(activeMemory.id, activeMemory.name, activeMemory.receiveHz,
                                      activeMemory.duplexMode, activeMemory.offsetHz, activeMemory.toneMode,
                                      activeMemory.toneValue);
            applyMemoryToVfo(activeMemory);
        }
    }
    else
    {
        if (m_window->m_activeMemoryId == radioMemoryId(memory.group, memory.channel))
        {
            if (m_window->m_applyingMemorySelection)
            {
                if (m_refreshInProgress)
                {
                    if (m_refreshGroup > kRadioMemoryLastGroup)
                    {
                        setMemoryProgress(QStringLiteral("Waiting for radio memory replies"),
                                          m_receivedRadioMemoryKeys.size(), m_expectedRadioMemoryKeys.size());
                    }
                    if (allExpectedRadioMemoriesReceived())
                    {
                        finishRadioMemoryRefresh(false);
                    }
                }
                advanceQueuedWrite();
                return;
            }
            m_window->clearActiveMemory();
        }
        m_radioMemoriesByKey.remove(key);
    }
    if (m_refreshInProgress)
    {
        if (m_refreshGroup > kRadioMemoryLastGroup)
        {
            setMemoryProgress(QStringLiteral("Waiting for radio memory replies"), m_receivedRadioMemoryKeys.size(),
                              m_expectedRadioMemoryKeys.size());
        }
        if (allExpectedRadioMemoriesReceived())
        {
            finishRadioMemoryRefresh(false);
            return;
        }
    }
    scheduleMemoryViewsRebuild();
    advanceQueuedWrite();
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
                  if (left.group == right.group)
                  {
                      return left.channel < right.channel;
                  }
                  return left.group < right.group;
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

void MemoryController::applyMemoryToVfo(const MemoryRecord& memory)
{
    if (!m_window->m_vfo)
    {
        return;
    }

    m_window->m_vfo->applyFrequency(memory.receiveHz);
    m_window->m_vfo->applyMode(memoryModeLabel(memory.mode));
    m_window->m_vfo->applyRepeaterOffsetHz(memory.offsetHz);
    m_window->m_vfo->applyDuplexMode(static_cast<duplexMode_t>(memory.duplexMode));
    if (isDtcsToneMode(static_cast<rptAccessTxRx_t>(memory.toneMode)))
    {
        m_window->m_vfo->applyDtcsCode(memory.toneValue);
    }
    else if (memory.toneMode != ratrNN)
    {
        m_window->m_vfo->applyToneFrequency(memory.toneValue);
    }
    m_window->m_vfo->applyToneAccessMode(static_cast<rptAccessTxRx_t>(memory.toneMode));
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

void MemoryController::queueRadioMemoryWrites(const QVector<MemoryType>& memories, int startDelayMs,
                                              const QString& progressLabel, std::function<void()> completion)
{
    if (!m_window->m_model || !m_window->m_model->isConnected())
    {
        return;
    }

    QTimer::singleShot(qMax(0, startDelayMs), this,
                       [this, memories, progressLabel, completion = std::move(completion)]() mutable
                       { startQueuedRadioMemoryWrites(memories, progressLabel, std::move(completion)); });
}

void MemoryController::startQueuedRadioMemoryWrites(const QVector<MemoryType>& memories, const QString& progressLabel,
                                                    std::function<void()> completion)
{
    if (!m_window->m_model || !m_window->m_model->isConnected())
    {
        return;
    }
    if (m_waitingForRadioMemoryWriteReadback || !m_queuedRadioMemoryWrites.isEmpty())
    {
        m_window->showToast(QStringLiteral("Memory write already in progress"), 5000, MainWindow::ToastKind::Warning);
        return;
    }

    if (memories.isEmpty())
    {
        if (completion)
        {
            completion();
        }
        return;
    }

    m_queuedRadioMemoryWrites = memories;
    m_queuedRadioMemoryWriteIndex = 0;
    m_queuedRadioMemoryWriteLabel = progressLabel;
    m_queuedRadioMemoryWriteCompletion = std::move(completion);
    setMemoryProgress(m_queuedRadioMemoryWriteLabel, 0, m_queuedRadioMemoryWrites.size());
    writeNextQueuedRadioMemory();
}

void MemoryController::writeNextQueuedRadioMemory()
{
    if (!m_window->m_model || !m_window->m_model->isConnected())
    {
        finishQueuedRadioMemoryWrites(true);
        return;
    }
    if (m_queuedRadioMemoryWriteIndex >= m_queuedRadioMemoryWrites.size())
    {
        finishQueuedRadioMemoryWrites(false);
        return;
    }

    const MemoryType memory = m_queuedRadioMemoryWrites.at(m_queuedRadioMemoryWriteIndex);
    m_expectedRadioMemoryWriteKey = radioMemoryKey(memory.group, memory.channel);
    m_waitingForRadioMemoryWriteReadback = true;
    // RadioBackend requests this memory slot again after writing it. Advance
    // the batch only when that readback arrives so progress reflects radio
    // state, not just elapsed GUI timer time.
    m_window->m_model->writeRadioMemory(memory);
    m_radioMemoryWriteTimeoutTimer->start();
}

void MemoryController::finishQueuedRadioMemoryWrites(bool timedOut)
{
    m_radioMemoryWriteTimeoutTimer->stop();
    const std::function<void()> completion = std::move(m_queuedRadioMemoryWriteCompletion);
    const QString label = m_queuedRadioMemoryWriteLabel;
    m_queuedRadioMemoryWrites.clear();
    m_queuedRadioMemoryWriteIndex = 0;
    m_expectedRadioMemoryWriteKey = 0;
    m_waitingForRadioMemoryWriteReadback = false;
    m_queuedRadioMemoryWriteLabel.clear();
    m_queuedRadioMemoryWriteCompletion = {};
    clearMemoryProgress();
    if (timedOut)
    {
        m_window->showToast(label.isEmpty() ? QStringLiteral("Memory write timed out")
                                            : QStringLiteral("%1 timed out").arg(label),
                            5000, MainWindow::ToastKind::Warning);
        return;
    }

    if (completion)
    {
        completion();
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

int MemoryController::queueRecordsToRadio(const QVector<MemoryRecord>& records, int* skippedCount, int startDelayMs,
                                          const QString& progressLabel, std::function<void()> completion)
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
        if (memory.group < kRadioMemoryFirstGroup || memory.group > kRadioMemoryLastGroup ||
            memory.channel < kRadioMemoryFirstChannel || memory.channel > kRadioMemoryLastChannel)
        {
            ++skipped;
            continue;
        }
        const quint32 key = radioMemoryKey(memory.group, memory.channel);
        if (occupied.contains(key))
        {
            ++skipped;
            continue;
        }

        occupied.insert(key);
        writes.append(radioMemoryFromRecord(memory, memory.group, memory.channel));
        ++queuedCount;
    }

    queueRadioMemoryWrites(writes, startDelayMs, progressLabel, std::move(completion));

    if (skippedCount)
    {
        *skippedCount = skipped;
    }
    return queuedCount;
}

void MemoryController::setMemoryProgress(const QString& label, int value, int maximum)
{
    m_memoryProgressLabel = label;
    m_memoryProgressValue = qBound(0, value, maximum);
    m_memoryProgressMaximum = qMax(0, maximum);
    if (m_window->m_memoryCountLabel)
    {
        m_window->m_memoryCountLabel->setText(QStringLiteral("%1 (%2/%3)")
                                                  .arg(m_memoryProgressLabel)
                                                  .arg(m_memoryProgressValue)
                                                  .arg(m_memoryProgressMaximum));
    }
    if (m_window->m_memoryProgressBar)
    {
        m_window->m_memoryProgressBar->setRange(0, m_memoryProgressMaximum);
        m_window->m_memoryProgressBar->setValue(m_memoryProgressValue);
        m_window->m_memoryProgressBar->setVisible(m_memoryProgressMaximum > 0);
    }
    updateMemoryTableInteraction();
}

void MemoryController::clearMemoryProgress()
{
    m_memoryProgressLabel.clear();
    m_memoryProgressValue = 0;
    m_memoryProgressMaximum = 0;
    if (m_window->m_memoryProgressBar)
    {
        m_window->m_memoryProgressBar->setVisible(false);
        m_window->m_memoryProgressBar->setValue(0);
    }
    updateMemoryTableInteraction();
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
                             QStringLiteral("Memory export successful."));
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
    if (!m_memoryProgressLabel.isEmpty())
    {
        QMessageBox::information(popupParent(), QStringLiteral("Import Memories"),
                                 QStringLiteral("Wait for the current memory operation to finish before importing."));
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

    if (QMessageBox::question(
            popupParent(), QStringLiteral("Import Memories"),
            QStringLiteral("Import these memories to the radio?\n\n"
                           "User memory channels 1-99 on 2M, 70CM, and 23CM will be cleared first.")) !=
        QMessageBox::Yes)
    {
        return;
    }

    const QVector<MemoryType> deletes = deletedUserRadioMemories();
    queueRadioMemoryWrites(
        deletes, 0, QStringLiteral("Clearing existing memories"),
        [this, records]()
        {
            m_radioMemoriesByKey.clear();

            int skippedCount = 0;
            const int importedCount =
                queueRecordsToRadio(records, &skippedCount, 0, QStringLiteral("Uploading memories"),
                                    [this]() { requestRadioMemoryRefresh(); });

            m_window->showToast(
                skippedCount > 0
                    ? QStringLiteral("Queued %1 memories for import, skipped %2").arg(importedCount).arg(skippedCount)
                    : QStringLiteral("Queued %1 memories for import").arg(importedCount));
        });
}

void MemoryController::rebuildMemoryViews()
{
    const QVector<MemoryRecord> memories = currentMemories();
    if (m_window->m_memoryPanel)
    {
        m_window->m_memoryPanel->setSyncInProgress(m_refreshInProgress, QStringLiteral("Syncing radio memories..."));
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

        auto* bandItem = setItem(kMemoryBandColumn, memoryBandLabelForGroup(memory.group));
        bandItem->setTextAlignment(Qt::AlignCenter);
        auto* numberItem =
            setItem(kMemoryNumberColumn, QString::number(memory.channel).rightJustified(3, QLatin1Char('0')));
        numberItem->setData(Qt::UserRole, memory.channel);
        numberItem->setTextAlignment(Qt::AlignCenter);
        setItem(kMemoryNameColumn, memory.name);
        auto* frequencyItem = setItem(kMemoryFrequencyColumn, memoryFrequencyLabel(memory.receiveHz));
        frequencyItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(memory.receiveHz));
        frequencyItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        setItem(kMemoryDuplexColumn, memory.shift)->setTextAlignment(Qt::AlignCenter);
        setItem(kMemoryModeColumn, memoryModeLabel(memory.mode))->setTextAlignment(Qt::AlignCenter);
        auto* toneItem = setItem(kMemoryToneColumn, memoryToneTableLabel(memory));
        toneItem->setData(kMemoryToneTypeRole, memoryToneTypeLabel(memory));
        toneItem->setData(kMemoryToneRxRole, memoryToneRxLabel(memory));
        toneItem->setData(kMemoryToneTxRole, memoryToneTxLabel(memory));
        toneItem->setToolTip(toneItem->text());
        toneItem->setTextAlignment(Qt::AlignCenter);
        setItem(kMemoryFilterColumn, memoryFilterLabel(memory.filter))->setTextAlignment(Qt::AlignCenter);
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
            setMemoryProgress(QStringLiteral("Syncing %1 channel %2")
                                  .arg(memoryBandLabelForGroup(m_currentSyncGroup))
                                  .arg(m_currentSyncChannel, 3, 10, QLatin1Char('0')),
                              syncIndex, kRadioMemorySyncTotal);
            return;
        }
        if (m_refreshInProgress)
        {
            setMemoryProgress(QStringLiteral("Syncing memories"), 0, kRadioMemorySyncTotal);
            return;
        }
        if (!m_memoryProgressLabel.isEmpty())
        {
            setMemoryProgress(m_memoryProgressLabel, m_memoryProgressValue, m_memoryProgressMaximum);
            return;
        }
        clearMemoryProgress();
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

    quint16 group = 0;
    quint16 channel = 0;
    if (!parseRadioMemoryId(memory.id, &group, &channel))
    {
        return;
    }

    m_window->m_applyingMemorySelection = true;
    m_window->m_activeMemorySelectionReleaseScheduled = false;
    const int generation = ++m_window->m_memorySelectionGeneration;
    m_window->setActiveMemory(memory.id, memory.name, memory.receiveHz, memory.duplexMode, memory.offsetHz,
                              memory.toneMode, memory.toneValue);
    m_window->m_model->selectRadioMemory(group, channel);
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
                           m_window->m_activeMemorySelectionReleaseScheduled = false;
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

    quint16 group = kRadioMemoryFirstGroup;
    quint16 channel = 0;
    if (!parseRadioMemoryId(id, &group, nullptr) || !firstOpenChannelForGroup(group, &channel))
    {
        QMessageBox::warning(popupParent(), "Copy Memory", "No empty user memory channel is available on this band.");
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

    quint16 group = 0;
    quint16 channel = 0;
    if (!parseRadioMemoryId(id, &group, &channel))
    {
        return;
    }
    if (QMessageBox::question(popupParent(), "Remove Memory",
                              QStringLiteral("Remove memory \"%1\"?").arg(memory.name)) != QMessageBox::Yes)
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
    root->setContentsMargins(kMemoryEditorGutter, 8, kMemoryEditorGutter, 0);

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
    struct EditorSection
    {
        QGroupBox* group{nullptr};
        QFormLayout* form{nullptr};
    };
    auto makeSection = [editor, configureSectionForm](const QString& title)
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
        auto* form = new QFormLayout(group);
        configureSectionForm(form);
        return EditorSection{group, form};
    };
    auto* channelCombo = new QComboBox(editor);
    for (quint16 channel = kRadioMemoryFirstChannel; channel <= kRadioMemoryLastChannel; ++channel)
    {
        channelCombo->addItem(QString::number(channel).rightJustified(3, QLatin1Char('0')), channel);
    }
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
    modeOffsetLayout->setSpacing(10);
    auto* modeColumn = new QWidget(modeOffsetRow);
    auto* modeColumnLayout = new QVBoxLayout(modeColumn);
    modeColumnLayout->setContentsMargins(0, 0, 0, 0);
    modeColumnLayout->setSpacing(kMemoryEditorLabelFieldSpacing);
    auto* modeLabel = new QLabel(QStringLiteral("Mode"), modeColumn);
    modeLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    modeColumnLayout->addWidget(modeLabel);
    modeColumnLayout->addWidget(modeCombo);
    auto* offsetColumn = new QWidget(modeOffsetRow);
    auto* offsetColumnLayout = new QVBoxLayout(offsetColumn);
    offsetColumnLayout->setContentsMargins(0, 0, 0, 0);
    offsetColumnLayout->setSpacing(kMemoryEditorLabelFieldSpacing);
    auto* offsetLabel = new QLabel(QStringLiteral("Offset"), offsetColumn);
    offsetLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    offsetColumnLayout->addWidget(offsetLabel);
    offsetColumnLayout->addWidget(offsetCombo);
    modeOffsetLayout->addWidget(modeColumn, 1);
    modeOffsetLayout->addWidget(offsetColumn, 1);
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
    auto* customOffsetField = new QWidget(editor);
    auto* customOffsetFieldLayout = new QVBoxLayout(customOffsetField);
    customOffsetFieldLayout->setContentsMargins(0, 0, 0, 0);
    customOffsetFieldLayout->setSpacing(kMemoryEditorLabelFieldSpacing);
    auto* customOffsetLabel = new QLabel(QStringLiteral("Custom Offset"), customOffsetField);
    customOffsetLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    customOffsetFieldLayout->addWidget(customOffsetLabel);
    customOffsetFieldLayout->addWidget(customOffsetRow);
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
             channelCombo,   nameEdit,    frequencyEdit,         modeCombo,           filterCombo,     dataModeCombo,
             scanGroupCombo, offsetCombo, customOffsetModeCombo, customOffsetSpin,    toneOptionCombo, dsqlCombo,
             dtcsSpin,       dtcsRxSpin,  dtcsPolarityCombo,     dtcsRxPolarityCombo, dvSqlSpin,       urEdit,
             r1Edit,         r2Edit,      tonePresetBtn,         ctcssPresetBtn,      dtcsPresetBtn,   dtcsRxPresetBtn})
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
    dtcsCodeColumnLayout->setSpacing(kMemoryEditorLabelFieldSpacing);
    auto* dtcsCodeTextLabel = new QLabel(QStringLiteral("TX Code"), dtcsCodeColumn);
    dtcsCodeTextLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    dtcsCodeColumnLayout->addWidget(dtcsCodeTextLabel);
    dtcsCodeColumnLayout->addWidget(dtcsPresetBtn);
    auto* dtcsPolarityColumn = new QWidget(dtcsRow);
    auto* dtcsPolarityColumnLayout = new QVBoxLayout(dtcsPolarityColumn);
    dtcsPolarityColumnLayout->setContentsMargins(0, 0, 0, 0);
    dtcsPolarityColumnLayout->setSpacing(kMemoryEditorLabelFieldSpacing);
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
    dtcsRxCodeColumnLayout->setSpacing(kMemoryEditorLabelFieldSpacing);
    auto* dtcsRxCodeTextLabel = new QLabel(QStringLiteral("RX Code"), dtcsRxCodeColumn);
    dtcsRxCodeTextLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    dtcsRxCodeColumnLayout->addWidget(dtcsRxCodeTextLabel);
    dtcsRxCodeColumnLayout->addWidget(dtcsRxPresetBtn);
    auto* dtcsRxPolarityColumn = new QWidget(dtcsRxRow);
    auto* dtcsRxPolarityColumnLayout = new QVBoxLayout(dtcsRxPolarityColumn);
    dtcsRxPolarityColumnLayout->setContentsMargins(0, 0, 0, 0);
    dtcsRxPolarityColumnLayout->setSpacing(kMemoryEditorLabelFieldSpacing);
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
        columnLayout->setSpacing(kMemoryEditorLabelFieldSpacing);
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
    const EditorSection memorySection = makeSection(QStringLiteral("Memory"));
    const EditorSection optionsSection = makeSection(QStringLiteral("Options"));
    const EditorSection toneSection = makeSection(QStringLiteral("Tone"));
    const EditorSection dstarSection = makeSection(QStringLiteral("D-STAR / DV"));
    auto* memoryGroup = memorySection.group;
    auto* optionsGroup = optionsSection.group;
    auto* toneGroup = toneSection.group;
    auto* dstarGroup = dstarSection.group;

    auto* optionsRow = new QWidget(optionsGroup);
    auto* optionsRowLayout = new QHBoxLayout(optionsRow);
    optionsRowLayout->setContentsMargins(0, 0, 0, 0);
    optionsRowLayout->setSpacing(8);
    auto addOptionColumn = [optionsRow, optionsRowLayout](const QString& labelText, QWidget* field, int stretch)
    {
        auto* column = new QWidget(optionsRow);
        auto* columnLayout = new QVBoxLayout(column);
        columnLayout->setContentsMargins(0, 0, 0, 0);
        columnLayout->setSpacing(kMemoryEditorLabelFieldSpacing);
        auto* label = new QLabel(labelText, column);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        columnLayout->addWidget(label);
        columnLayout->addWidget(field);
        optionsRowLayout->addWidget(column, stretch);
    };
    addOptionColumn(QStringLiteral("Data Mode"), dataModeCombo, 1);
    addOptionColumn(QStringLiteral("Scan Group"), scanGroupCombo, 1);
    addOptionColumn(QStringLiteral("Filter"), filterCombo, 1);

    auto* memoryFields = new QWidget(memoryGroup);
    auto* memoryGrid = new QGridLayout(memoryFields);
    memoryGrid->setContentsMargins(0, 0, 0, 0);
    memoryGrid->setHorizontalSpacing(10);
    memoryGrid->setVerticalSpacing(6);
    memoryGrid->setColumnStretch(0, 1);
    memoryGrid->setColumnStretch(1, 1);
    auto addMemoryField =
        [memoryFields, memoryGrid](int row, int column, const QString& labelText, QWidget* field, int columnSpan = 1)
    {
        auto* fieldContainer = new QWidget(memoryFields);
        auto* fieldLayout = new QVBoxLayout(fieldContainer);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(kMemoryEditorLabelFieldSpacing);
        auto* label = new QLabel(labelText, fieldContainer);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        fieldLayout->addWidget(label);
        fieldLayout->addWidget(field);
        memoryGrid->addWidget(fieldContainer, row, column, 1, columnSpan);
    };
    addMemoryField(0, 0, QStringLiteral("Channel"), channelCombo);
    addMemoryField(0, 1, QStringLiteral("Frequency"), frequencyEdit);
    addMemoryField(1, 0, QStringLiteral("Name"), nameEdit, 2);
    memoryGrid->addWidget(modeOffsetRow, 2, 0, 1, 2);
    memoryGrid->addWidget(customOffsetField, 3, 0, 1, 2);
    memorySection.form->addRow(memoryFields);
    optionsSection.form->addRow(optionsRow);
    toneSection.form->addRow(toneOptionCombo);
    toneSection.form->addRow(toneValueRow);
    toneSection.form->addRow(dtcsValueRow);
    dstarSection.form->addRow("Digital SQL (DSQL):", dsqlCombo);
    dstarSection.form->addRow("DV SQL:", dvSqlSpin);
    dstarSection.form->addRow("Your Call (UR):", urEdit);
    dstarSection.form->addRow("Repeater 1 Callsign (R1):", r1Edit);
    dstarSection.form->addRow("Repeater 2 Callsign (R2):", r2Edit);

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
        [modeCombo, offsetColumn, offsetCombo, customOffsetField, resizeEditorToContents]()
    {
        const bool showOffset = modeSupportsMemoryOffset(modeCombo->currentData().toInt());
        const bool customSelected = offsetCombo->currentData(Qt::UserRole).toInt() == kMemoryOffsetCustom;
        offsetColumn->setVisible(showOffset);
        customOffsetField->setVisible(showOffset && customSelected);
        resizeEditorToContents();
    };

    populateOffsetOptions();
    updateCustomOffsetVisibility();

    auto populateToneValues = [clearTonePick]() { clearTonePick(); };

    auto updateConditionalSections = [modeCombo, toneOptionCombo, toneForm = toneSection.form, toneValueRow,
                                      dtcsValueRow, dstarGroup, resizeEditorToContents]()
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

        const quint64 currentFrequencyHz = m_window->m_vfo->frequencyHz();
        frequencyEdit->setText(memoryFrequencyLabel(currentFrequencyHz));
        if (nameEdit->text().trimmed().isEmpty())
        {
            nameEdit->setText(memoryFrequencyLabel(currentFrequencyHz));
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

    auto applyMemoryToForm = [this, channelCombo, nameEdit, frequencyEdit, modeCombo, filterCombo, dataModeCombo,
                              scanGroupCombo, populateOffsetOptions, setOffsetSelection, updateCustomOffsetVisibility,
                              toneOptionCombo, toneEdit, tsqlEdit, dsqlCombo, dtcsSpin, dtcsRxSpin, dtcsPolarityCombo,
                              dtcsRxPolarityCombo, dvSqlSpin, urEdit, r1Edit, r2Edit, setTonePick, setCtcssPick,
                              setDtcsPick, setDtcsRxPick, populateToneValues,
                              updateConditionalSections](const MemoryRecord& memory)
    {
        quint16 group = kRadioMemoryFirstGroup;
        quint16 channel = kRadioMemoryFirstChannel;
        parseRadioMemoryId(memory.id, &group, &channel);
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
        const quint16 defaultGroup =
            m_window->m_vfo ? radioMemoryGroupForHz(m_window->m_vfo->frequencyHz()) : kRadioMemoryFirstGroup;
        quint16 firstOpenChannel = kRadioMemoryFirstChannel;
        if (firstOpenChannelForGroup(defaultGroup, &firstOpenChannel))
        {
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
    auto* footerContainer = new QWidget(editor);
    auto* footerLayout = new QVBoxLayout(footerContainer);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(0);
    auto* footerSeparator = new QWidget(footerContainer);
    footerSeparator->setFixedHeight(1);
    footerSeparator->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    footerSeparator->setStyleSheet(
        QStringLiteral("QWidget { background: %1; }").arg(QLatin1String(UiTheme::Color::BorderMedium)));
    footerLayout->addWidget(footerSeparator);
    auto* buttonRow = new QWidget(footerContainer);
    auto* buttonRowLayout = new QHBoxLayout(buttonRow);
    buttonRowLayout->setContentsMargins(0, kMemoryPanelSpacing + kMemoryFooterTopPadding, 0,
                                        kMemoryFooterBottomPadding);
    auto* copyButton = new QPushButton("Copy Current", buttonRow);
    copyButton->setMinimumWidth(copyButton->sizeHint().width() + 20);
    auto* saveButton = new QPushButton("Save", buttonRow);
    auto* cancelButton = new QPushButton("Cancel", buttonRow);
    buttonRowLayout->addWidget(copyButton, 0, Qt::AlignLeft);
    buttonRowLayout->addStretch(1);
    buttonRowLayout->addWidget(saveButton, 0, Qt::AlignRight);
    buttonRowLayout->addWidget(cancelButton, 0, Qt::AlignRight);
    footerLayout->addWidget(buttonRow);
    root->addWidget(footerContainer);
    resizeEditorToContents();
    connect(copyButton, &QPushButton::clicked, editor, copyCurrentSettings);
    connect(cancelButton, &QPushButton::clicked, this,
            [this]() { QTimer::singleShot(0, this, [this]() { closeMemoryEditorPane(); }); });

    connect(
        saveButton, &QPushButton::clicked, editor,
        [this, editor, frequencyEdit, toneOptionCombo, toneEdit, tsqlEdit, dtcsSpin, dtcsRxSpin, nameEdit, modeCombo,
         filterCombo, dataModeCombo, scanGroupCombo, offsetCombo, customOffsetModeCombo, customOffsetSpin, dsqlCombo,
         dtcsPolarityCombo, dtcsRxPolarityCombo, dvSqlSpin, urEdit, r1Edit, r2Edit, channelCombo, editing, memoryId,
         parent]()
        {
            quint64 receiveHz = 0;
            if (!parseFrequencyText(frequencyEdit->text(), &receiveHz))
            {
                QMessageBox::warning(editor, "Add/Edit Memory", "Enter a valid receive frequency.");
                frequencyEdit->setFocus();
                frequencyEdit->selectAll();
                return;
            }
            const availableBands inferredBand = sdr9700::radioBandForFrequency(receiveHz);
            const sdr9700::RadioBandDef* bandDefinition = sdr9700::radioBandDefinition(inferredBand);
            if (!bandDefinition || bandDefinition->memGroup < kRadioMemoryFirstGroup ||
                bandDefinition->memGroup > kRadioMemoryLastGroup)
            {
                QMessageBox::warning(editor, "Add/Edit Memory", "Enter a frequency in the 2M, 70CM, or 23CM range.");
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

            const quint16 group = static_cast<quint16>(bandDefinition->memGroup);
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
                    if (m_radioMemoriesByKey.contains(radioMemoryKey(group, channel)))
                    {
                        if (QMessageBox::question(parent, "Add/Edit Memory",
                                                  QStringLiteral("Overwrite radio memory %1 channel %2?")
                                                      .arg(memoryBandLabelForGroup(group))
                                                      .arg(channel, 3, 10, QLatin1Char('0'))) != QMessageBox::Yes)
                        {
                            return;
                        }
                    }
                    deleteRadioMemory(oldGroup, oldChannel);
                }
            }
            else if (m_radioMemoriesByKey.contains(radioMemoryKey(group, channel)))
            {
                if (QMessageBox::question(parent, "Add/Edit Memory",
                                          QStringLiteral("Overwrite radio memory %1 channel %2?")
                                              .arg(memoryBandLabelForGroup(group))
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
