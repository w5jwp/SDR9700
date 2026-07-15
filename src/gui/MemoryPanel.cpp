#include "MemoryPanel.h"

#include "UiTheme.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSize>
#include <QTableWidget>
#include <QVBoxLayout>

namespace
{
constexpr int kMemoryPanelWidth = 380;
constexpr int kControlGroupMargin = 5;
constexpr int kMemoryItemHeight = 28;
constexpr int kHeaderHeight = 18;
constexpr int kNumberColumnWidth = 34;
constexpr int kFrequencyColumnWidth = 100;
constexpr int kMemoryColumn = 1;
constexpr int kMemoryColumnCount = 3;
constexpr int kMemoryNumberColumn = 0;
constexpr int kMemoryFrequencyColumn = 2;

QString memoryFrequencyLabel(quint64 hz)
{
    const quint64 mhz = hz / 1000000ULL;
    const quint64 khz = (hz % 1000000ULL) / 1000ULL;
    const quint64 hzPart = hz % 1000ULL;
    return QStringLiteral("%1.%2.%3")
        .arg(mhz, 3, 10, QLatin1Char('0'))
        .arg(khz, 3, 10, QLatin1Char('0'))
        .arg(hzPart, 3, 10, QLatin1Char('0'));
}

QTableWidgetItem* makeCellItem(const QString& text, Qt::Alignment alignment, const QString& memoryId = QString())
{
    auto* item = new QTableWidgetItem(text);
    item->setTextAlignment(alignment);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    if (!memoryId.isEmpty())
    {
        item->setData(Qt::UserRole, memoryId);
    }
    return item;
}
} // namespace

MemoryPanel::MemoryPanel(QWidget* parent) : QGroupBox(parent)
{
    setObjectName(QStringLiteral("MemoryPanel"));
    setTitle(QStringLiteral("Memories"));
    setAccessibleName(QStringLiteral("Memory browser"));
    setFixedWidth(kMemoryPanelWidth);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kControlGroupMargin, kControlGroupMargin + 5, kControlGroupMargin, kControlGroupMargin);
    layout->setSpacing(0);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("memoryBrowserTable"));
    m_table->setAccessibleName(QStringLiteral("Memory browser"));
    m_table->setAccessibleDescription(QStringLiteral("Double-click a memory to tune the active VFO."));
    m_table->setColumnCount(kMemoryColumnCount);
    m_table->setHorizontalHeaderLabels({QStringLiteral("CH"), QStringLiteral("Memory"), QStringLiteral("Frequency")});
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setTextElideMode(Qt::ElideNone);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_table->setShowGrid(true);
    m_table->setGridStyle(Qt::SolidLine);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    m_table->horizontalHeader()->setFixedHeight(kHeaderHeight);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(kMemoryNumberColumn, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(kMemoryColumn, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kMemoryFrequencyColumn, QHeaderView::Fixed);
    m_table->setColumnWidth(kMemoryNumberColumn, kNumberColumnWidth);
    m_table->setColumnWidth(kMemoryFrequencyColumn, kFrequencyColumnWidth);
    m_table->setStyleSheet(
        QStringLiteral("QTableWidget#memoryBrowserTable { background: %1; border: 1px solid %2; "
                       "border-radius: 3px; color: %3; gridline-color: %2; outline: 0; }"
                       "QTableWidget#memoryBrowserTable::item { padding: 0 5px; border: none; }"
                       "QTableWidget#memoryBrowserTable::item:selected { background: %4; color: %5; }"
                       "QHeaderView::section { background: %1; border: 1px solid %2; color: %6; "
                       "font-size: 9px; font-weight: bold; padding: 0 5px; }")
            .arg(UiTheme::Color::Field, UiTheme::Color::Border, UiTheme::Color::TextStatusPrimary,
                 UiTheme::Color::AccentDark, UiTheme::Color::TextBright, UiTheme::Color::TextStatusSecondary));
    layout->addWidget(m_table);

    connect(m_table, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int column)
            {
                Q_UNUSED(column)
                const auto* item = m_table->item(row, kMemoryNumberColumn);
                const QString memoryId = item ? item->data(Qt::UserRole).toString() : QString();
                if (!memoryId.isEmpty())
                {
                    emit memoryActivated(memoryId);
                }
            });
}

void MemoryPanel::setMemories(const QVector<MemoryRecord>& memories, const QString& activeMemoryId)
{
    m_memories = memories;
    m_activeMemoryId = activeMemoryId;
    rebuildList();
}

void MemoryPanel::setActiveMemoryId(const QString& activeMemoryId)
{
    m_activeMemoryId = activeMemoryId;
    applyActiveSelection();
}

void MemoryPanel::rebuildList()
{
    m_table->setRowCount(0);

    if (m_memories.isEmpty())
    {
        m_table->setRowCount(1);
        m_table->setSpan(0, 0, 1, kMemoryColumnCount);
        auto* item = makeCellItem(QStringLiteral("No memories saved"), Qt::AlignCenter);
        item->setFlags(Qt::ItemIsEnabled);
        m_table->setItem(0, 0, item);
        m_table->setRowHeight(0, kMemoryItemHeight);
        return;
    }

    for (const MemoryRecord& memory : m_memories)
    {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        const QString name = memory.name.isEmpty() ? QStringLiteral("(unnamed)") : memory.name;
        auto* numberItem = makeCellItem(memoryNumberLabel(memory.number), Qt::AlignRight | Qt::AlignVCenter, memory.id);
        auto* nameItem = makeCellItem(name, Qt::AlignLeft | Qt::AlignVCenter, memory.id);
        auto* frequencyItem =
            makeCellItem(memoryFrequencyLabel(memory.receiveHz), Qt::AlignRight | Qt::AlignVCenter, memory.id);
        numberItem->setToolTip(QStringLiteral("%1\n%2").arg(name, memoryFrequencyLabel(memory.receiveHz)));
        nameItem->setToolTip(numberItem->toolTip());
        frequencyItem->setToolTip(numberItem->toolTip());
        m_table->setItem(row, kMemoryNumberColumn, numberItem);
        m_table->setItem(row, kMemoryColumn, nameItem);
        m_table->setItem(row, kMemoryFrequencyColumn, frequencyItem);
        m_table->setRowHeight(row, kMemoryItemHeight);
    }

    applyActiveSelection();
}

void MemoryPanel::applyActiveSelection()
{
    if (!m_table)
    {
        return;
    }

    m_table->clearSelection();
    m_table->setCurrentCell(-1, -1);
    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        auto* item = m_table->item(row, kMemoryNumberColumn);
        if (item && item->data(Qt::UserRole).toString() == m_activeMemoryId)
        {
            m_table->selectRow(row);
            m_table->setCurrentCell(row, kMemoryNumberColumn);
            m_table->scrollToItem(item, QAbstractItemView::PositionAtCenter);
            return;
        }
    }
}
