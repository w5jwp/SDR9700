#include "MemoryController.h"
#include "MemoryControllerHelpers.h"

#include "AppSettings.h"
#include "DialogPlacement.h"
#include "LogCategories.h"
#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "MemoryEditorController.h"
#include "MemorySyncPolicy.h"
#include "MemoryPanel.h"
#include "MemorySyncController.h"
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
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <algorithm>
#include <cstring>
#include <initializer_list>

using namespace sdr9700::ui::main_window;


MemoryController::MemoryController(MainWindow* window) : QObject(window), m_window(window)
{
    m_memoryEditorController = new MemoryEditorController(this);
    m_memorySyncController = new MemorySyncController(this);

    m_radioMemoryRefreshTimer = new QTimer(this);
    m_radioMemoryRefreshTimer->setSingleShot(true);
    m_radioMemoryRefreshTimer->setInterval(kRadioMemoryRefreshIntervalMs);
    connect(m_radioMemoryRefreshTimer, &QTimer::timeout, this, &MemoryController::requestNextRadioMemory);

    m_radioMemoryPeriodicRefreshTimer = new QTimer(this);
    m_memorySyncController->setMemoryPollIntervalSeconds(
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

    m_radioMemoryReplyGraceTimer = new QTimer(this);
    m_radioMemoryReplyGraceTimer->setSingleShot(true);
    connect(m_radioMemoryReplyGraceTimer, &QTimer::timeout, this,
            [this]()
            {
                if (m_refreshInProgress)
                {
                    finishRadioMemoryRefresh(false);
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

    connect(m_window->m_model, &RadioModel::radioMemoryReceived, this, &MemoryController::handleRadioMemoryReceived,
            Qt::QueuedConnection);
    connect(m_window->m_model, &RadioModel::readyChanged, this,
            [this](bool ready)
            {
                qInfo(logGui()) << "MemoryController observed radio readyChanged:" << ready;
                if (ready)
                {
                    if (m_initialMemorySyncComplete)
                    {
                        m_initialMemorySyncComplete = false;
                        emit initialMemorySyncChanged(false);
                    }
                    // Keep startup progress visible until MainWindow replaces it
                    // with the final Radio Ready message after this first poll.
                    m_window->showToast(QStringLiteral("Syncing radio memories..."), 0);
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

void MemoryController::forceRadioMemorySync()
{
    m_memorySyncController->forceRadioMemorySync();
}

void MemoryController::setMemoryPollIntervalSeconds(int seconds)
{
    m_memorySyncController->setMemoryPollIntervalSeconds(seconds);
}

bool MemoryController::initialMemorySyncComplete() const
{
    return m_initialMemorySyncComplete;
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

    connect(closeButton, &QPushButton::clicked, m_window->m_memoryWindow, &QWidget::hide);

    auto* windowLayout = new QVBoxLayout(m_window->m_memoryWindow);
    windowLayout->setContentsMargins(kNoMargins);
    windowLayout->setSpacing(0);
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

// Radio memory sync state machine:
// - requestRadioMemoryRefresh() polls every user channel in every IC-9700 band
//   group and records each key in m_expectedRadioMemoryKeys.
// - handleRadioMemoryReceived() marks replies as received and stores only
//   populated memories in m_radioMemoriesByKey.
// - requestNextRadioMemory() completes after every memory slot has been polled
//   and a short late-reply grace period has elapsed. Field logs showed some
//   empty/default slots do not produce a useful memory-content reply, so Radio
//   Ready must not require all 297 possible keys to answer. Backout point:
//   restore allExpectedRadioMemoriesReceived() as the completion gate if later
//   captures prove every IC-9700 slot reliably responds.
// - finishRadioMemoryRefresh() is the only place that clears progress and marks
//   the first sync complete. MainWindow keeps radio controls disabled until that
//   first full sync completes, so timeout recovery must schedule a new initial
//   sync instead of leaving the operator parked at "Syncing".
void MemoryController::requestRadioMemoryRefresh()
{
    if (QThread::currentThread() != thread())
    {
        // Radio readiness can be emitted from the backend/radio worker path.
        // The first memory request would still run from that thread, but Qt
        // refuses to start this controller's GUI-owned timers there. Always
        // repost the sync state machine to MemoryController's owning thread.
        QMetaObject::invokeMethod(this, &MemoryController::requestRadioMemoryRefresh, Qt::QueuedConnection);
        return;
    }

    if (!m_window->m_model)
    {
        qInfo(logGui()) << "Radio memory sync skipped; radio model is not available";
        return;
    }
    if (!m_window->m_model->isConnected())
    {
        qInfo(logGui()) << "Radio memory sync skipped; radio model is not connected";
        return;
    }
    if (m_refreshInProgress)
    {
        qInfo(logGui()) << "Radio memory sync skipped; refresh is already in progress";
        return;
    }

    m_refreshGroup = kRadioMemoryFirstGroup;
    m_refreshChannel = kRadioMemoryFirstChannel;
    m_currentSyncGroup = 0;
    m_currentSyncChannel = 0;
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
    // Keep this timeout tied to the amount of CI-V work being scheduled. A
    // fixed timeout looked fine on a fast link but could expire while slower
    // radios were still sending late memory replies.
    m_radioMemorySyncTimeoutTimer->start(radioMemorySyncTimeoutMs());
    m_radioMemoryReplyGraceTimer->stop();
    qInfo(logGui()) << "Radio memory sync started; polling" << kRadioMemorySyncTotal << "slots";
    requestNextRadioMemory();
    rebuildMemoryViews();
}

void MemoryController::requestNextRadioMemory()
{
    if (!m_refreshInProgress)
    {
        return;
    }

    if (!m_window->m_model || !m_window->m_model->isConnected())
    {
        finishRadioMemoryRefresh(false);
        return;
    }

    if (m_refreshGroup > kRadioMemoryLastGroup)
    {
        m_radioMemoryRefreshTimer->stop();
        if (allExpectedRadioMemoriesReceived())
        {
            qInfo(logGui()) << "Radio memory sync received all expected replies";
            finishRadioMemoryRefresh(false);
            return;
        }

        qInfo(logGui()) << "Radio memory sync poll pass complete; received" << m_receivedRadioMemoryKeys.size() << "of"
                        << m_expectedRadioMemoryKeys.size() << "possible replies, waiting briefly for late replies";
        setMemoryProgress(QStringLiteral("Finalizing radio memory sync"), m_receivedRadioMemoryKeys.size(),
                          m_expectedRadioMemoryKeys.size());
        if (!m_radioMemoryReplyGraceTimer->isActive())
        {
            m_radioMemoryReplyGraceTimer->start(kRadioMemorySyncReplyGraceMs);
        }
        return;
    }

    m_currentSyncGroup = m_refreshGroup;
    m_currentSyncChannel = m_refreshChannel;
    if (m_currentSyncChannel == kRadioMemoryFirstChannel)
    {
        qInfo(logGui()) << "Radio memory sync polling" << memoryBandLabelForGroup(m_currentSyncGroup);
    }
    else if (m_currentSyncChannel <= 5 || (m_currentSyncChannel % 25) == 0)
    {
        qInfo(logGui()) << "Radio memory sync polling" << memoryBandLabelForGroup(m_currentSyncGroup) << "channel"
                        << m_currentSyncChannel;
    }
    const int syncIndex =
        (m_currentSyncGroup - kRadioMemoryFirstGroup) * (kRadioMemoryLastChannel - kRadioMemoryFirstChannel + 1) +
        (m_currentSyncChannel - kRadioMemoryFirstChannel + 1);
    QString progressLabel = QStringLiteral("Syncing %1 channel %2")
                                .arg(memoryBandLabelForGroup(m_currentSyncGroup))
                                .arg(m_currentSyncChannel, 3, 10, QLatin1Char('0'));
    setMemoryProgress(progressLabel, syncIndex, kRadioMemorySyncTotal);
    m_window->m_model->requestRadioMemory(m_refreshGroup, m_refreshChannel);
    ++m_refreshChannel;
    if (m_refreshChannel > kRadioMemoryLastChannel)
    {
        m_refreshChannel = kRadioMemoryFirstChannel;
        ++m_refreshGroup;
    }
    // The radio normally answers a memory poll quickly; handleRadioMemoryReceived()
    // advances immediately when that happens. Keep this single-shot as the
    // fallback for empty/default channels that do not produce a useful memory
    // contents reply, so one missing response cannot strand startup at
    // "Syncing radio state".
    m_radioMemoryRefreshTimer->start();
}

bool MemoryController::allExpectedRadioMemoriesReceived() const
{
    return sdr9700::memorySyncComplete(m_expectedRadioMemoryKeys, m_receivedRadioMemoryKeys);
}

bool MemoryController::radioConnected() const
{
    return m_window->m_model && m_window->m_model->isConnected();
}

bool MemoryController::memoryRefreshInProgress() const
{
    return m_refreshInProgress;
}

bool MemoryController::memoryOperationInProgress() const
{
    return !m_memoryProgressLabel.isEmpty();
}

bool MemoryController::memoryEditorVisible() const
{
    return m_memoryEditorPane && m_memoryEditorPane->isVisible();
}

void MemoryController::cancelMemoryRefresh()
{
    finishRadioMemoryRefresh(false);
}

void MemoryController::requestRadioMemoryRefreshFromController()
{
    requestRadioMemoryRefresh();
}

void MemoryController::setMemoryPollTimerIntervalSeconds(int seconds)
{
    const int boundedSeconds = qBound(kMemoryPollIntervalMinSeconds, seconds, kMemoryPollIntervalMaxSeconds);
    m_radioMemoryPeriodicRefreshTimer->setInterval(boundedSeconds * 1000);
}

void MemoryController::clearMemoryEditButtonChecked()
{
    if (m_memoryEditButton)
    {
        m_memoryEditButton->setChecked(false);
    }
}

void MemoryController::closeMemoryEditorFromController()
{
    closeMemoryEditorPane();
}

void MemoryController::showMemoryToast(const QString& message)
{
    m_window->showToast(message);
}

void MemoryController::finishRadioMemoryRefresh(bool timedOut)
{
    m_radioMemoryRefreshTimer->stop();
    m_radioMemorySyncTimeoutTimer->stop();
    m_radioMemoryReplyGraceTimer->stop();
    m_memoryViewRefreshTimer->stop();
    const bool wasInProgress = m_refreshInProgress;
    const bool completedPollPass = wasInProgress && !timedOut && m_window->m_model &&
                                   m_window->m_model->isConnected() && m_refreshGroup > kRadioMemoryLastGroup;
    // Reaching the end of the request list proves only that GUI timers fired.
    // At least one parsed memory/empty-slot reply is required as evidence that
    // CI-V memory traffic is actually flowing before startup can become Ready.
    const bool noRadioReplies = completedPollPass && m_receivedRadioMemoryKeys.isEmpty();
    timedOut = timedOut || noRadioReplies;
    const bool resetAfterSync = m_resetAfterSync;
    m_resetAfterSync = false;
    m_refreshInProgress = false;
    m_currentSyncGroup = 0;
    m_currentSyncChannel = 0;
    m_expectedRadioMemoryKeys.clear();
    clearMemoryProgress();
    if (timedOut && wasInProgress)
    {
        qWarning(logGui()) << "Radio memory sync failed after receiving" << m_receivedRadioMemoryKeys.size()
                           << "memory replies" << (noRadioReplies ? "(no CI-V memory replies)" : "(timeout)");
        m_window->showToast(m_initialMemorySyncComplete ? QStringLiteral("Radio memory sync timed out")
                                                        : QStringLiteral("Radio memory sync timed out; retrying"),
                            5000, MainWindow::ToastKind::Warning);
        if (resetAfterSync)
        {
            m_window->showToast(QStringLiteral("Memory reset canceled because sync timed out"), 5000,
                                MainWindow::ToastKind::Warning);
        }
        if (!m_initialMemorySyncComplete && m_window->m_model && m_window->m_model->isConnected())
        {
            // Radio Ready is intentionally gated on the first memory sync. A
            // lost CI-V memory reply should not strand the UI until the normal
            // periodic poll interval elapses, so retry the initial sync quickly.
            // Backout point: remove this singleShot if field testing shows the
            // IC-9700 needs a longer quiet period after a memory poll timeout.
            QTimer::singleShot(kRadioMemoryInitialSyncRetryDelayMs, this,
                               [this]()
                               {
                                   if (!m_initialMemorySyncComplete)
                                   {
                                       requestRadioMemoryRefresh();
                                   }
                               });
        }
    }
    else if (completedPollPass && !m_initialMemorySyncComplete)
    {
        m_initialMemorySyncComplete = true;
        qInfo(logGui()) << "Initial radio memory sync complete with" << m_radioMemoriesByKey.size()
                        << "stored memories";
        emit initialMemorySyncChanged(true);
    }
    else if (completedPollPass)
    {
        qInfo(logGui()) << "Radio memory sync complete with" << m_radioMemoriesByKey.size() << "stored memories";
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
                           [this](bool success)
                           {
                               if (!success)
                               {
                                   return;
                               }
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
    if (m_refreshInProgress && (memory.channel <= 5 || (memory.channel % 25) == 0))
    {
        qInfo(logGui()) << "Radio memory sync received" << memoryBandLabelForGroup(memory.group) << "channel"
                        << memory.channel;
    }
    const bool expectedQueuedWriteReply =
        sdr9700::memoryReadbackExpected(m_waitingForRadioMemoryWriteReadback, m_expectedRadioMemoryWriteKey, key);
    const bool completedQueuedWrite =
        expectedQueuedWriteReply && m_queuedRadioMemoryWriteIndex < m_queuedRadioMemoryWrites.size() &&
        radioMemoryReadbackMatches(m_queuedRadioMemoryWrites.at(m_queuedRadioMemoryWriteIndex), memory);
    if (expectedQueuedWriteReply && !completedQueuedWrite)
    {
        // Keep waiting until the operation timeout. Treating any response for
        // the slot as success can report an edit/import complete even when the
        // radio returned the old contents or rejected one field.
        qWarning(logGui()) << "Radio memory write readback did not match requested contents for"
                           << memoryBandLabelForGroup(memory.group) << "channel" << memory.channel;
    }
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
                        setMemoryProgress(QStringLiteral("Finalizing radio memory sync"),
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
        m_radioMemoryRefreshTimer->stop();
        if (m_refreshGroup > kRadioMemoryLastGroup)
        {
            setMemoryProgress(QStringLiteral("Finalizing radio memory sync"), m_receivedRadioMemoryKeys.size(),
                              m_expectedRadioMemoryKeys.size());
        }
        if (allExpectedRadioMemoriesReceived())
        {
            finishRadioMemoryRefresh(false);
            return;
        }
        // This slot already runs on MemoryController's GUI thread. Advance the
        // memory poll immediately instead of posting another queued event; field
        // logs showed startup could strand at "Syncing radio state" after the
        // first memory reply if the GUI event queue was also processing the
        // post-ready CI-V status burst. The single-shot timer started in
        // requestNextRadioMemory() remains the backstop for empty slots that do
        // not return memory contents.
        requestNextRadioMemory();
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

void MemoryController::writeMemoryRecord(const MemoryRecord& memory, quint16 group, quint16 channel,
                                         MemoryWriteCompletion completion)
{
    if (!m_window->m_model || !m_window->m_model->isConnected())
    {
        if (completion)
        {
            completion(false);
        }
        return;
    }
    queueRadioMemoryWrites({radioMemoryFromRecord(memory, group, channel)}, 0, QStringLiteral("Writing memory"),
                           std::move(completion));
}

void MemoryController::deleteRadioMemory(quint16 group, quint16 channel, MemoryWriteCompletion completion)
{
    if (!m_window->m_model || !m_window->m_model->isConnected())
    {
        if (completion)
        {
            completion(false);
        }
        return;
    }
    queueRadioMemoryWrites({deletedRadioMemory(group, channel)}, 0, QStringLiteral("Removing memory"),
                           std::move(completion));
}

void MemoryController::queueRadioMemoryWrites(const QVector<MemoryType>& memories, int startDelayMs,
                                              const QString& progressLabel, MemoryWriteCompletion completion)
{
    if (!m_window->m_model || !m_window->m_model->isConnected())
    {
        if (completion)
        {
            completion(false);
        }
        return;
    }

    QTimer::singleShot(qMax(0, startDelayMs), this,
                       [this, memories, progressLabel, completion = std::move(completion)]() mutable
                       { startQueuedRadioMemoryWrites(memories, progressLabel, std::move(completion)); });
}

void MemoryController::startQueuedRadioMemoryWrites(const QVector<MemoryType>& memories, const QString& progressLabel,
                                                    MemoryWriteCompletion completion)
{
    if (!m_window->m_model || !m_window->m_model->isConnected())
    {
        if (completion)
        {
            completion(false);
        }
        return;
    }
    if (m_waitingForRadioMemoryWriteReadback || !m_queuedRadioMemoryWrites.isEmpty())
    {
        m_window->showToast(QStringLiteral("Memory write already in progress"), 5000, MainWindow::ToastKind::Warning);
        if (completion)
        {
            completion(false);
        }
        return;
    }

    if (memories.isEmpty())
    {
        if (completion)
        {
            completion(true);
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
    const MemoryWriteCompletion completion = std::move(m_queuedRadioMemoryWriteCompletion);
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
    }
    if (completion)
    {
        completion(!timedOut);
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

void MemoryController::restoreRadioMemoriesAfterFailedImport(const QVector<MemoryType>& backup)
{
    if (!m_window->m_model || !m_window->m_model->isConnected())
    {
        m_window->showToast(QStringLiteral("Memory import failed. Reconnect and restore the previous export."), 8000,
                            MainWindow::ToastKind::Error);
        return;
    }

    // An import can fail after only part of its clear/upload batch reached the
    // radio. Clear the user slots again before replaying the in-memory snapshot;
    // otherwise imported rows beyond the failure point could survive beside the
    // restored set. This is operational rollback, not configuration migration.
    queueRadioMemoryWrites(
        deletedUserRadioMemories(), 0, QStringLiteral("Rolling back memory import"),
        [this, backup](bool cleared)
        {
            if (!cleared)
            {
                m_window->showToast(QStringLiteral("Memory import rollback failed while clearing channels."), 8000,
                                    MainWindow::ToastKind::Error);
                requestRadioMemoryRefresh();
                return;
            }
            m_radioMemoriesByKey.clear();
            queueRadioMemoryWrites(
                backup, 0, QStringLiteral("Restoring previous memories"),
                [this](bool restored)
                {
                    m_window->showToast(restored
                                            ? QStringLiteral("Memory import failed. Previous memories restored.")
                                            : QStringLiteral("Memory import rollback failed while restoring memories."),
                                        8000, restored ? MainWindow::ToastKind::Warning : MainWindow::ToastKind::Error);
                    requestRadioMemoryRefresh();
                });
        });
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

    QStringList importErrors;
    const QVector<MemoryRecord> records = memoriesFromCsv(file.readAll(), &importErrors);
    if (!importErrors.isEmpty())
    {
        const QString details = importErrors.mid(0, 12).join(QLatin1Char('\n'));
        const QString suffix =
            importErrors.size() > 12 ? QStringLiteral("\n...and %1 more").arg(importErrors.size() - 12) : QString();
        QMessageBox::warning(
            popupParent(), QStringLiteral("Import Memories Failed"),
            QStringLiteral("Memory import failed. Fix the CSV file and try again.\n\n%1%2").arg(details, suffix));
        return;
    }
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

    QVector<MemoryType> backup;
    backup.reserve(m_radioMemoriesByKey.size());
    for (const MemoryType& memory : m_radioMemoriesByKey)
    {
        backup.append(memory);
    }
    QVector<MemoryType> uploads;
    uploads.reserve(records.size());
    for (const MemoryRecord& record : records)
    {
        uploads.append(radioMemoryFromRecord(record, record.group, record.channel));
    }

    const QVector<MemoryType> deletes = deletedUserRadioMemories();
    queueRadioMemoryWrites(deletes, 0, QStringLiteral("Clearing existing memories"),
                           [this, uploads, backup](bool cleared)
                           {
                               if (!cleared)
                               {
                                   restoreRadioMemoriesAfterFailedImport(backup);
                                   return;
                               }
                               m_radioMemoriesByKey.clear();
                               queueRadioMemoryWrites(
                                   uploads, 0, QStringLiteral("Uploading memories"),
                                   [this, backup, importedCount = uploads.size()](bool uploaded)
                                   {
                                       if (!uploaded)
                                       {
                                           restoreRadioMemoriesAfterFailedImport(backup);
                                           return;
                                       }
                                       m_window->showToast(
                                           QStringLiteral("Imported %1 memories successfully.").arg(importedCount));
                                       requestRadioMemoryRefresh();
                                   });
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
    // Timeout guard: release the memory-selection protection after 3 s in case
    // the radio never confirms the selected channel.
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
    m_memoryEditorController->editSelectedMemory();
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

    writeMemoryRecord(copy, group, channel,
                      [this, name = copy.name](bool success)
                      {
                          if (success)
                          {
                              m_window->showToast(QStringLiteral("Copied memory: %1").arg(name));
                          }
                      });
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

    deleteRadioMemory(group, channel,
                      [this](bool success)
                      {
                          if (success)
                          {
                              m_window->showToast(QStringLiteral("Memory removed"));
                          }
                      });
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
    queueRadioMemoryWrites({radioMemoryFromRecord(source, targetGroup, targetChannel),
                            radioMemoryFromRecord(target, sourceGroup, sourceChannel)},
                           0, QStringLiteral("Reordering memories"),
                           [this](bool success)
                           {
                               if (success)
                               {
                                   m_window->showToast(QStringLiteral("Memories reordered"));
                               }
                           });
}

void MemoryController::storeCurrentMemory()
{
    m_memoryEditorController->storeCurrentMemory();
}

void MemoryController::showMemoryEditor(const QString& memoryId)
{
    m_memoryEditorController->showMemoryEditor(memoryId);
}

void MemoryController::reloadMemoryTable()
{
    rebuildMemoryViews();
}
