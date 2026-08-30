#include "MemoryViewController.h"

#include "MainWindow.h"
#include "DialogFooter.h"
#include "MemoryController.h"
#include "MemoryConstants.h"
#include "MemoryRecordHelpers.h"
#include "MemoryViewHelpers.h"
#include "MemorySyncController.h"
#include "UtilityWindow.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

using namespace sdr9700::memory;

MemoryViewController::MemoryViewController(MemoryController* owner) : QObject(owner), m_owner(owner)
{
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(250);
    m_refreshTimer->setSingleShot(true);
    connect(m_refreshTimer, &QTimer::timeout, this, &MemoryViewController::rebuild);
}

void MemoryViewController::stopScheduledRefresh()
{
    m_refreshTimer->stop();
}

bool MemoryViewController::operationInProgress() const
{
    return !m_progressLabel.isEmpty();
}

void MemoryViewController::buildMemoryWindow()
{
    m_owner->m_window->m_memoryWindow =
        new sdr9700::ui::UtilityWindow(QStringLiteral("Memory Manager"), m_owner->m_window);
    m_owner->m_window->m_memoryWindow->setStyleSheet(
        QStringLiteral("QDialog { background: %1; border: 1px solid %2; }")
            .arg(QLatin1String(UiTheme::Color::Panel), QLatin1String(UiTheme::Color::Border)));
    m_owner->m_window->m_memoryWindow->setObjectName("memoryWindow");
    m_owner->m_window->m_memoryWindow->setAttribute(Qt::WA_DeleteOnClose, false);
    m_owner->m_window->m_memoryWindow->resize(kMemoryWindowSize);
    m_owner->m_window->m_memoryWindow->setFixedSize(kMemoryWindowSize);

    auto* panel = new QWidget(m_owner->m_window->m_memoryWindow);
    auto* root = new QHBoxLayout(panel);
    root->setContentsMargins(kMemoryWindowMargins.left(), kMemoryWindowMargins.top(), kMemoryWindowMargins.right(), 0);
    root->setSpacing(kMemoryWindowSpacing);

    auto* leftPane = new QWidget(panel);
    leftPane->setFixedWidth(kMemoryWindowSize.width() - kMemoryWindowMargins.left() - kMemoryWindowMargins.right());
    auto* leftRoot = new QVBoxLayout(leftPane);
    leftRoot->setContentsMargins(0, 0, 0, 0);
    leftRoot->setSpacing(sdr9700::ui::kDialogFooterSpacing);

    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(kNoMargins);
    toolbar->setSpacing(kMemoryToolbarSpacing);

    auto* filterGroup = new QGroupBox(panel);
    auto* filterLayout = new QHBoxLayout(filterGroup);
    filterLayout->setContentsMargins(kMemoryToolbarGroupMargins);
    filterLayout->setSpacing(kMemoryToolbarGroupSpacing);
    m_owner->m_window->m_memoryBandFilter = new QComboBox(panel);
    m_owner->m_window->m_memoryBandFilter->addItem(QStringLiteral("All"), QString());
    for (const availableBands band : sdr9700::kRadioUiBandOrder)
    {
        const QString label = sdr9700::radioBandShortLabel(band);
        m_owner->m_window->m_memoryBandFilter->addItem(label, label);
    }
    filterLayout->addWidget(m_owner->m_window->m_memoryBandFilter);
    toolbar->addWidget(filterGroup);
    toolbar->addStretch(1);

    auto* syncGroup = new QGroupBox(panel);
    auto* syncLayout = new QHBoxLayout(syncGroup);
    syncLayout->setContentsMargins(kMemoryToolbarGroupMargins);
    syncLayout->setSpacing(kMemoryToolbarGroupSpacing);
    auto* syncButton = new QPushButton("Sync", panel);
    syncButton->setToolTip("Immediately read radio memories into SDR9700.");
    m_owner->m_window->m_memoryBandFilter->setFixedHeight(syncButton->sizeHint().height());
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

    m_owner->m_window->m_memoryTable = new QTableWidget(panel);
    m_owner->m_window->m_memoryTable->setObjectName(QStringLiteral("memoryManagerTable"));
    m_owner->m_window->m_memoryTable->setColumnCount(kMemoryTableColumnCount);
    m_owner->m_window->m_memoryTable->setHorizontalHeaderLabels(
        {QStringLiteral("Channel"), QStringLiteral("Name"), QStringLiteral("Frequency"), QStringLiteral("Offset"),
         QStringLiteral("Mode"), QStringLiteral("Tone (TX/RX)"), QStringLiteral("ID")});
    m_owner->m_window->m_memoryTable->setColumnHidden(kMemoryIdColumn, true);
    m_owner->m_window->m_memoryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_owner->m_window->m_memoryTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_owner->m_window->m_memoryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_owner->m_window->m_memoryTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_owner->m_window->m_memoryTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_owner->m_window->m_memoryTable->setSortingEnabled(false);
    m_owner->m_window->m_memoryTable->setDragDropMode(QAbstractItemView::NoDragDrop);
    m_owner->m_window->m_memoryTable->setShowGrid(true);
    m_owner->m_window->m_memoryTable->setGridStyle(Qt::SolidLine);
    m_owner->m_window->m_memoryTable->setAlternatingRowColors(true);
    m_owner->m_window->m_memoryTable->setStyleSheet(UiTheme::tableStyle(QStringLiteral("memoryManagerTable")));
    m_owner->m_window->m_memoryTable->verticalHeader()->setVisible(false);
    m_owner->m_window->m_memoryTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_owner->m_window->m_memoryTable->horizontalHeader()->setFixedHeight(32);
    m_owner->m_window->m_memoryTable->horizontalHeader()->setSectionsClickable(false);
    m_owner->m_window->m_memoryTable->horizontalHeader()->setStretchLastSection(false);
    for (int column = kMemoryChannelColumn; column <= kMemoryToneColumn; ++column)
    {
        m_owner->m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Fixed);
    }
    m_owner->m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryNameColumn, QHeaderView::Stretch);
    m_owner->m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryIdColumn, QHeaderView::Fixed);
    m_owner->m_window->m_memoryTable->setColumnWidth(kMemoryChannelColumn, kMemoryChannelColumnWidth);
    m_owner->m_window->m_memoryTable->setColumnWidth(kMemoryNameColumn, kMemoryNameColumnWidth);
    m_owner->m_window->m_memoryTable->setColumnWidth(kMemoryFrequencyColumn, kMemoryFrequencyColumnWidth);
    m_owner->m_window->m_memoryTable->setColumnWidth(kMemoryDuplexColumn, kMemoryDuplexColumnWidth);
    m_owner->m_window->m_memoryTable->setColumnWidth(kMemoryModeColumn, kMemoryModeColumnWidth);
    m_owner->m_window->m_memoryTable->setColumnWidth(kMemoryToneColumn, kMemoryToneColumnWidth);
    m_owner->m_window->m_memoryTable->setItemDelegateForColumn(kMemoryToneColumn,
                                                               new ToneCellDelegate(m_owner->m_window->m_memoryTable));
    leftRoot->addWidget(m_owner->m_window->m_memoryTable, 1);

    const sdr9700::ui::DialogFooter footer = sdr9700::ui::createDialogFooter(panel);
    m_owner->m_window->m_memoryCountLabel = new QLabel(panel);
    m_owner->m_window->m_memoryCountLabel->setObjectName(QStringLiteral("memoryManagerStatusLabel"));
    m_owner->m_window->m_memoryCountLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_owner->m_window->m_memoryCountLabel->setContentsMargins(kMemoryFooterTextLeftPadding, 0, 0, 0);
    m_owner->m_window->m_memoryCountLabel->setStyleSheet("QLabel { color: palette(mid); }");
    m_owner->m_window->m_memoryProgressBar = new QProgressBar(panel);
    m_owner->m_window->m_memoryProgressBar->setObjectName(QStringLiteral("memoryManagerProgressBar"));
    m_owner->m_window->m_memoryProgressBar->setFixedWidth(220);
    m_owner->m_window->m_memoryProgressBar->setTextVisible(false);
    m_owner->m_window->m_memoryProgressBar->setVisible(false);
    m_owner->m_window->m_memoryProgressBar->setStyleSheet(
        QStringLiteral("QProgressBar { background: %1; border: 1px solid %2; border-radius: 3px; height: 8px; }"
                       "QProgressBar::chunk { background: %3; border-radius: 2px; }")
            .arg(UiTheme::Color::Field, UiTheme::Color::BorderMedium, UiTheme::Color::Accent));
    footer.rowLayout->insertWidget(0, m_owner->m_window->m_memoryCountLabel, 1);
    footer.rowLayout->insertWidget(1, m_owner->m_window->m_memoryProgressBar);
    footer.buttonBox->addButton(QDialogButtonBox::Close);
    leftRoot->addWidget(footer.widget);

    root->addWidget(leftPane, 1);

    connect(footer.buttonBox, &QDialogButtonBox::rejected, m_owner->m_window->m_memoryWindow, &QWidget::hide);

    auto* windowLayout = new QVBoxLayout(m_owner->m_window->m_memoryWindow);
    windowLayout->setContentsMargins(kNoMargins);
    windowLayout->setSpacing(0);
    auto* titleBar =
        new sdr9700::ui::UtilityTitleBar(QStringLiteral("Memory Manager"), m_owner->m_window->m_memoryWindow);
    connect(titleBar->closeButton(), &QPushButton::clicked, m_owner->m_window->m_memoryWindow, &QWidget::hide);
    windowLayout->addWidget(titleBar);
    windowLayout->addWidget(panel, 1);

    connect(m_owner->m_window->m_memoryBandFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), m_owner,
            &MemoryController::reloadMemoryTable);
    connect(syncButton, &QPushButton::clicked, m_owner, &MemoryController::forceRadioMemorySync);
    connect(upButton, &QPushButton::clicked, m_owner, &MemoryController::moveSelectedMemoryUp);
    connect(downButton, &QPushButton::clicked, m_owner, &MemoryController::moveSelectedMemoryDown);
    connect(addButton, &QPushButton::clicked, m_owner, &MemoryController::storeCurrentMemory);
    connect(editButton, &QPushButton::clicked, m_owner, &MemoryController::editSelectedMemory);
    connect(copyButton, &QPushButton::clicked, m_owner, &MemoryController::copySelectedMemory);
    connect(removeButton, &QPushButton::clicked, m_owner, &MemoryController::removeSelectedMemory);
    connect(importButton, &QPushButton::clicked, m_owner, &MemoryController::importRadioMemories);
    connect(exportButton, &QPushButton::clicked, this,
            [this]()
            {
                if (m_owner->exportRadioMemories())
                {
                    m_owner->m_window->showToast(QStringLiteral("Memories exported"));
                }
            });

    m_owner->reloadMemoryTable();
}


void MemoryViewController::showMemoryWindow()
{
    if (!m_owner->m_window->m_memoryWindow)
    {
        return;
    }
    m_owner->reloadMemoryTable();
    static_cast<sdr9700::ui::UtilityWindow*>(m_owner->m_window->m_memoryWindow)->showCentered();
}


void MemoryViewController::scheduleRebuild()
{
    if (!m_owner->m_memorySyncController->refreshInProgress())
    {
        rebuild();
        return;
    }

    if (!m_refreshTimer->isActive())
    {
        m_refreshTimer->start();
    }
}


void MemoryViewController::updateTableInteraction()
{
    if (!m_owner->m_window->m_memoryTable)
    {
        return;
    }

    const bool locked = m_owner->m_memorySyncController->refreshInProgress() || !m_progressLabel.isEmpty();
    m_owner->m_window->m_memoryTable->setSelectionMode(locked ? QAbstractItemView::NoSelection
                                                              : QAbstractItemView::SingleSelection);
    m_owner->m_window->m_memoryTable->setFocusPolicy(locked ? Qt::NoFocus : Qt::StrongFocus);
    m_owner->m_window->m_memoryTable->setAttribute(Qt::WA_TransparentForMouseEvents, locked);
    if (QWidget* viewport = m_owner->m_window->m_memoryTable->viewport())
    {
        viewport->setAttribute(Qt::WA_TransparentForMouseEvents, locked);
    }
}


void MemoryViewController::setProgress(const QString& label, int value, int maximum)
{
    m_progressLabel = label;
    m_progressValue = qBound(0, value, maximum);
    m_progressMaximum = qMax(0, maximum);
    if (m_owner->m_window->m_memoryCountLabel)
    {
        m_owner->m_window->m_memoryCountLabel->setText(
            QStringLiteral("%1 (%2/%3)").arg(m_progressLabel).arg(m_progressValue).arg(m_progressMaximum));
    }
    if (m_owner->m_window->m_memoryProgressBar)
    {
        m_owner->m_window->m_memoryProgressBar->setRange(0, m_progressMaximum);
        m_owner->m_window->m_memoryProgressBar->setValue(m_progressValue);
        m_owner->m_window->m_memoryProgressBar->setVisible(m_progressMaximum > 0);
    }
    updateTableInteraction();
}


void MemoryViewController::clearProgress()
{
    m_progressLabel.clear();
    m_progressValue = 0;
    m_progressMaximum = 0;
    if (m_owner->m_window->m_memoryProgressBar)
    {
        m_owner->m_window->m_memoryProgressBar->setVisible(false);
        m_owner->m_window->m_memoryProgressBar->setValue(0);
    }
    updateTableInteraction();
}


void MemoryViewController::rebuild()
{
    const QVector<MemoryRecord> memories = m_owner->currentMemories();
    if (!m_owner->m_window->m_memoryTable)
    {
        return;
    }
    updateTableInteraction();

    const QString bandFilter = m_owner->m_window->m_memoryBandFilter
                                   ? m_owner->m_window->m_memoryBandFilter->currentData().toString()
                                   : QString();
    m_owner->m_window->m_memoryTable->setSortingEnabled(false);
    m_owner->m_window->m_memoryTable->setRowCount(0);
    int visibleCount = 0;
    for (const MemoryRecord& memory : memories)
    {
        if (!bandFilter.isEmpty() && memory.band != bandFilter)
        {
            continue;
        }

        const int row = m_owner->m_window->m_memoryTable->rowCount();
        m_owner->m_window->m_memoryTable->insertRow(row);

        auto setItem = [this, row](int column, const QString& text)
        {
            auto* item = new QTableWidgetItem(text);
            m_owner->m_window->m_memoryTable->setItem(row, column, item);
            return item;
        };

        auto* channelItem = setItem(kMemoryChannelColumn, QStringLiteral("%1-%2")
                                                              .arg(memoryBandLabelForGroup(memory.group))
                                                              .arg(memory.channel, 3, 10, QLatin1Char('0')));
        channelItem->setData(Qt::UserRole, memory.channel);
        channelItem->setTextAlignment(Qt::AlignCenter);
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
        setItem(kMemoryIdColumn, memory.id);
        ++visibleCount;
    }

    if (m_owner->m_window->m_memoryCountLabel)
    {
        m_owner->m_window->m_memoryCountLabel->setStyleSheet("QLabel { color: palette(mid); }");
        const int totalCount = memories.size();
        if (m_owner->m_memorySyncController->refreshInProgress())
        {
            if (m_progressLabel.isEmpty())
            {
                setProgress(QStringLiteral("Syncing memories"), 0, kRadioMemorySyncTotal);
            }
            else
            {
                setProgress(m_progressLabel, m_progressValue, m_progressMaximum);
            }
            return;
        }
        if (!m_progressLabel.isEmpty())
        {
            setProgress(m_progressLabel, m_progressValue, m_progressMaximum);
            return;
        }
        clearProgress();
        if (bandFilter.isEmpty())
        {
            m_owner->m_window->m_memoryCountLabel->setText(
                QStringLiteral("%1 %2 total")
                    .arg(totalCount)
                    .arg(totalCount == 1 ? QStringLiteral("memory") : QStringLiteral("memories")));
        }
        else
        {
            m_owner->m_window->m_memoryCountLabel->setText(
                QStringLiteral("%1 filtered / %2 total").arg(visibleCount).arg(totalCount));
        }
    }
}
