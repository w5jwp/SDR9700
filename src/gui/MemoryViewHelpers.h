#pragma once

#include "MemoryConstants.h"
#include "MemoryRecordHelpers.h"
#include "UiTheme.h"

#include <QApplication>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QStyle>
#include <algorithm>

namespace sdr9700::memory
{
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

        constexpr int kFieldGap = 10;
        const QFontMetrics metrics(option.font);
        const int typeWidth = std::max({metrics.horizontalAdvance(QStringLiteral("CTCSS")),
                                        metrics.horizontalAdvance(QStringLiteral("DTCS")),
                                        metrics.horizontalAdvance(QStringLiteral("DCS"))});
        const int txWidth = qMax(metrics.horizontalAdvance(QStringLiteral("TX: 000.0")),
                                 metrics.horizontalAdvance(QStringLiteral("TX: 000N")));
        const int typeX = rect.left() + kMemoryToneCellTextPadding;
        const int txX = typeX + typeWidth + kFieldGap;
        const int rxX = txX + txWidth + kFieldGap;
        const QRect typeRect(typeX, rect.top(), typeWidth, rect.height());
        const QRect txRect(txX, rect.top(), txWidth, rect.height());
        const QRect rxRect(rxX, rect.top(), qMax(0, rect.right() - rxX + 1), rect.height());

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setPen(textColor);
        painter->drawText(typeRect, Qt::AlignLeft | Qt::AlignVCenter, type);
        painter->drawText(txRect, Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("TX: %1").arg(tx.isEmpty() ? QStringLiteral("OFF") : tx));
        painter->drawText(rxRect, Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("RX: %1").arg(rx.isEmpty() ? QStringLiteral("OFF") : rx));
        painter->restore();
    }
};


inline QString dtcsMemoryValue(ushort code, int polarity)
{
    return QStringLiteral("%1%2").arg(dtcsCodeLabel(code), polarity == 3 ? QStringLiteral("R") : QStringLiteral("N"));
}

inline QString memoryToneTypeLabel(const MemoryRecord& memory)
{
    const auto toneMode = static_cast<rptAccessTxRx_t>(memory.toneMode);
    if (toneMode == ratrNN)
    {
        return QStringLiteral("OFF");
    }
    return isDtcsToneMode(toneMode) ? QStringLiteral("DTCS") : QStringLiteral("TONE");
}

inline QString memoryToneRxLabel(const MemoryRecord& memory)
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

inline QString memoryToneTxLabel(const MemoryRecord& memory)
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

inline QString memoryToneTableLabel(const MemoryRecord& memory)
{
    const QString type = memoryToneTypeLabel(memory);
    if (type == QLatin1String("OFF"))
    {
        return QStringLiteral("OFF");
    }
    return QStringLiteral("%1: %2/%3").arg(type, memoryToneTxLabel(memory), memoryToneRxLabel(memory));
}


} // namespace sdr9700::memory
