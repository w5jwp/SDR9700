#include "DataDecoderDialog.h"
#include "DialogFooter.h"
#include "UiTheme.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>
#include <utility>

namespace
{
constexpr QSize kWindowSize{1180, 620};
constexpr int kMaximumRows = 1000;

QLabel* metricValue(QWidget* parent)
{
    auto* label = new QLabel(QStringLiteral("0"), parent);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(
        QStringLiteral("color: %1; font-weight: bold;").arg(QLatin1String(UiTheme::Color::TextBright)));
    return label;
}

void addMetric(QHBoxLayout* layout, const QString& name, QLabel* value, QWidget* parent)
{
    auto* metric = new QWidget(parent);
    auto* metricLayout = new QVBoxLayout(metric);
    metricLayout->setContentsMargins(0, 0, 0, 0);
    metricLayout->setSpacing(2);
    auto* label = new QLabel(name, metric);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QStringLiteral("color: %1;").arg(QLatin1String(UiTheme::Color::TextMuted)));
    value->setParent(metric);
    metricLayout->addWidget(label);
    metricLayout->addWidget(value);
    layout->addWidget(metric, 1, Qt::AlignVCenter);
}

QString linkedAddress(const QString& address, bool* containsLink)
{
    static const QRegularExpression callsignPattern(QStringLiteral("^[A-Z0-9]{1,2}[0-9][A-Z]{1,4}$"));
    QStringList linkedParts;
    for (const QString& part : address.split(QLatin1Char(','), Qt::KeepEmptyParts))
    {
        QString base = part;
        if (base.endsWith(QLatin1Char('*')))
        {
            base.chop(1);
        }
        const qsizetype ssidSeparator = base.lastIndexOf(QLatin1Char('-'));
        if (ssidSeparator > 0)
        {
            base.truncate(ssidSeparator);
        }
        if (callsignPattern.match(base).hasMatch())
        {
            *containsLink = true;
            linkedParts.append(
                QStringLiteral("<a style=\"color:%1;\" href=\"https://qrz.com/db/%2\">%3</a>")
                    .arg(QLatin1String(UiTheme::Color::AccentBright), base.toHtmlEscaped(), part.toHtmlEscaped()));
        }
        else
        {
            linkedParts.append(part.toHtmlEscaped());
        }
    }
    return linkedParts.join(QLatin1Char(','));
}

void addAddressLinks(QTableWidget* table, int row, int column, const QString& address, bool elide = false)
{
    bool containsLink = false;
    const QString displayedAddress =
        elide ? table->fontMetrics().elidedText(address, Qt::ElideRight, qMax(20, table->columnWidth(column) - 16))
              : address;
    const QString html = linkedAddress(displayedAddress, &containsLink);
    if (!containsLink)
    {
        return;
    }
    auto* label = new QLabel(table);
    label->setTextFormat(Qt::RichText);
    label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    label->setOpenExternalLinks(true);
    label->setText(html);
    label->setFont(table->font());
    label->setContentsMargins(8, 0, 8, 0);
    label->setAccessibleName(QStringLiteral("%1; open callsign on QRZ.com").arg(address));
    label->setStyleSheet(QStringLiteral("background: transparent;"));
    if (QTableWidgetItem* item = table->item(row, column))
    {
        item->setData(Qt::DisplayRole, QString());
    }
    table->setCellWidget(row, column, label);
}
} // namespace

void Ax25DecoderWorker::processAudio(const QByteArray& pcm, int sampleRate, int channelCount)
{
    const QVector<Ax25Frame> frames = m_decoder.processPcm16(pcm, sampleRate, channelCount);
    if (!frames.isEmpty())
    {
        emit framesDecoded(frames);
    }
    emit statsChanged(m_decoder.stats());
}

void Ax25DecoderWorker::reset()
{
    m_decoder.reset();
    emit statsChanged(m_decoder.stats());
}

DataDecoderDialog::DataDecoderDialog(QWidget* parent)
    : sdr9700::ui::UtilityWindow(QStringLiteral("Data Decoder"), parent)
{
    qRegisterMetaType<Ax25Frame>();
    qRegisterMetaType<QVector<Ax25Frame>>();
    qRegisterMetaType<Ax25DecoderStats>();
    setObjectName(QStringLiteral("dataDecoderWindow"));
    setStyleSheet(QStringLiteral("DataDecoderDialog { background: %1; border: 1px solid %2; }")
                      .arg(QLatin1String(UiTheme::Color::Panel), QLatin1String(UiTheme::Color::Border)));
    setFixedSize(kWindowSize);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* titleBar = new sdr9700::ui::UtilityTitleBar(QStringLiteral("Data Decoder"), this);
    connect(titleBar->closeButton(), &QPushButton::clicked, this, &QWidget::hide);
    root->addWidget(titleBar);

    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(UiTheme::Size::DialogContentMargin, 10, UiTheme::Size::DialogContentMargin, 0);
    layout->setSpacing(sdr9700::ui::kDialogFooterSpacing);
    root->addWidget(content, 1);

    const QString summaryStyle =
        QStringLiteral("QGroupBox { border: 1px solid %1; border-radius: 3px; color: %2; margin-top: 8px; }"
                       "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }")
            .arg(QLatin1String(UiTheme::Color::BorderMedium), QLatin1String(UiTheme::Color::TextMuted));
    auto* summaryRow = new QHBoxLayout;
    summaryRow->setSpacing(10);

    auto* statusGroup = new QGroupBox(QStringLiteral("Decoder Status"), content);
    statusGroup->setStyleSheet(summaryStyle);
    auto* statusLayout = new QHBoxLayout(statusGroup);
    statusLayout->setContentsMargins(16, 5, 10, 17);
    statusLayout->setSpacing(10);
    statusLayout->setAlignment(Qt::AlignVCenter);
    m_statusIndicator = new QLabel(statusGroup);
    m_statusIndicator->setFixedSize(14, 14);
    m_statusIndicator->setAccessibleName(QStringLiteral("Decoder status indicator"));
    m_statusText = new QLabel(QStringLiteral("Waiting…"), statusGroup);
    m_statusText->setStyleSheet(QStringLiteral("color: %1;").arg(QLatin1String(UiTheme::Color::TextPrimary)));
    statusLayout->addWidget(m_statusIndicator, 0, Qt::AlignVCenter);
    statusLayout->addWidget(m_statusText, 0, Qt::AlignVCenter);
    statusLayout->addStretch(1);
    summaryRow->addWidget(statusGroup, 2);

    auto* performanceGroup = new QGroupBox(QStringLiteral("Decode Performance"), content);
    performanceGroup->setStyleSheet(summaryStyle);
    auto* performanceLayout = new QHBoxLayout(performanceGroup);
    performanceLayout->setContentsMargins(10, 5, 10, 17);
    performanceLayout->setSpacing(16);
    performanceLayout->setAlignment(Qt::AlignVCenter);
    m_candidatesValue = metricValue(performanceGroup);
    m_decodedValue = metricValue(performanceGroup);
    m_rejectedValue = metricValue(performanceGroup);
    m_successValue = metricValue(performanceGroup);
    addMetric(performanceLayout, QStringLiteral("Candidates"), m_candidatesValue, performanceGroup);
    addMetric(performanceLayout, QStringLiteral("Decoded"), m_decodedValue, performanceGroup);
    addMetric(performanceLayout, QStringLiteral("Rejected"), m_rejectedValue, performanceGroup);
    addMetric(performanceLayout, QStringLiteral("Decode Rate"), m_successValue, performanceGroup);
    summaryRow->addWidget(performanceGroup, 2);
    layout->addLayout(summaryRow);

    m_packetTable = new QTableWidget(content);
    m_packetTable->setObjectName(QStringLiteral("ax25PacketTable"));
    m_packetTable->setColumnCount(6);
    m_packetTable->setHorizontalHeaderLabels({QStringLiteral("Timestamp"), QStringLiteral("Protocol"),
                                              QStringLiteral("Source"), QStringLiteral("Destination"),
                                              QStringLiteral("Path"), QStringLiteral("Payload")});
    m_packetTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_packetTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_packetTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_packetTable->setAlternatingRowColors(true);
    m_packetTable->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_packetTable->setShowGrid(true);
    m_packetTable->setGridStyle(Qt::SolidLine);
    m_packetTable->verticalHeader()->hide();
    m_packetTable->verticalHeader()->setDefaultSectionSize(28);
    m_packetTable->setStyleSheet(
        QStringLiteral("QTableWidget#ax25PacketTable { background: %1; border: 1px solid %2; color: %3; "
                       "gridline-color: %2; outline: 0; }"
                       "QTableWidget#ax25PacketTable::item { padding: 4px 8px; border: none; }"
                       "QTableWidget#ax25PacketTable::item:alternate { background: %4; }"
                       "QTableWidget#ax25PacketTable::item:selected { background: %5; color: %6; }"
                       "QHeaderView::section { background: %4; border: 1px solid %2; color: %7; "
                       "font-weight: bold; padding: 5px 10px; }")
            .arg(QLatin1String(UiTheme::Color::Field), QLatin1String(UiTheme::Color::Border),
                 QLatin1String(UiTheme::Color::TextField), QLatin1String(UiTheme::Color::PanelDark),
                 QLatin1String(UiTheme::Color::AccentDark), QLatin1String(UiTheme::Color::TextBright),
                 QLatin1String(UiTheme::Color::TextStatusSecondary)));
    m_packetTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_packetTable->horizontalHeader()->setFixedHeight(32);
    m_packetTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    for (int column = 0; column < 5; ++column)
    {
        m_packetTable->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Interactive);
    }
    m_packetTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_packetTable->setColumnWidth(0, 110);
    m_packetTable->setColumnWidth(1, 115);
    m_packetTable->setColumnWidth(2, 105);
    m_packetTable->setColumnWidth(3, 105);
    m_packetTable->setColumnWidth(4, 230);
    m_packetTable->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(m_packetTable, 1);

    auto* detailsLabel = new QLabel(QStringLiteral("Raw Packet"), content);
    detailsLabel->setStyleSheet(QStringLiteral("color: %1;").arg(QLatin1String(UiTheme::Color::TextMuted)));
    layout->addWidget(detailsLabel);
    m_packetDetails = new QPlainTextEdit(content);
    m_packetDetails->setObjectName(QStringLiteral("dataDecoderPacketDetails"));
    m_packetDetails->setReadOnly(true);
    m_packetDetails->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_packetDetails->setPlaceholderText(QStringLiteral("Select a packet to view the raw packet."));
    m_packetDetails->setAccessibleName(QStringLiteral("Raw packet"));
    m_packetDetails->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_packetDetails->setFixedHeight(76);
    m_packetDetails->setStyleSheet(
        QStringLiteral("QPlainTextEdit { background: %1; border: 1px solid %2; border-radius: 3px; "
                       "color: %3; padding: 5px; }")
            .arg(QLatin1String(UiTheme::Color::Field), QLatin1String(UiTheme::Color::Border),
                 QLatin1String(UiTheme::Color::TextField)));
    layout->addWidget(m_packetDetails);

    const sdr9700::ui::DialogFooter footer = sdr9700::ui::createDialogFooter(content);
    m_pauseButton = footer.buttonBox->addButton(QStringLiteral("Pause"), QDialogButtonBox::ActionRole);
    auto* clearButton = footer.buttonBox->addButton(QStringLiteral("Clear"), QDialogButtonBox::ResetRole);
    auto* exportButton = footer.buttonBox->addButton(QStringLiteral("Export…"), QDialogButtonBox::ActionRole);
    footer.buttonBox->addButton(QDialogButtonBox::Close);
    layout->addWidget(footer.widget);

    m_worker = new Ax25DecoderWorker;
    m_worker->moveToThread(&m_decoderThread);
    connect(&m_decoderThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &DataDecoderDialog::audioReceived, m_worker, &Ax25DecoderWorker::processAudio, Qt::QueuedConnection);
    connect(this, &DataDecoderDialog::resetDecoder, m_worker, &Ax25DecoderWorker::reset, Qt::QueuedConnection);
    connect(m_worker, &Ax25DecoderWorker::framesDecoded, this, &DataDecoderDialog::appendFrames);
    connect(m_worker, &Ax25DecoderWorker::statsChanged, this, &DataDecoderDialog::updateStats);
    m_decoderThread.setObjectName(QStringLiteral("AX25 decoder"));
    m_decoderThread.start();

    connect(m_pauseButton, &QPushButton::clicked, this,
            [this]()
            {
                m_paused = !m_paused;
                m_pauseButton->setText(m_paused ? QStringLiteral("Resume") : QStringLiteral("Pause"));
                if (!m_paused && !m_pausedFrames.isEmpty())
                {
                    const QVector<Ax25Frame> frames = std::exchange(m_pausedFrames, {});
                    appendFrames(frames);
                }
            });
    connect(clearButton, &QPushButton::clicked, this,
            [this]()
            {
                m_packetTable->setRowCount(0);
                m_packetDetails->clear();
                emit resetDecoder();
            });
    connect(exportButton, &QPushButton::clicked, this, &DataDecoderDialog::exportPackets);
    connect(footer.buttonBox, &QDialogButtonBox::rejected, this, &QWidget::hide);
    connect(m_packetTable, &QTableWidget::currentCellChanged, this,
            [this](int currentRow)
            {
                if (currentRow < 0)
                {
                    m_packetDetails->clear();
                    return;
                }
                QStringList values;
                for (int column = 0; column < m_packetTable->columnCount(); ++column)
                {
                    const QTableWidgetItem* item = m_packetTable->item(currentRow, column);
                    values.append(item ? item->data(Qt::UserRole).toString() : QString());
                }
                const QString route = values.at(2) + QLatin1Char('>') + values.at(3) +
                                      (values.at(4).isEmpty() ? QString() : QLatin1Char(',') + values.at(4));
                m_packetDetails->setPlainText(route + QLatin1Char(':') + values.at(5));
            });
    updateStats({});
}

DataDecoderDialog::~DataDecoderDialog()
{
    m_decoderThread.quit();
    m_decoderThread.wait();
}

void DataDecoderDialog::processAudio(const QByteArray& pcm, int sampleRate, int channelCount)
{
    emit audioReceived(pcm, sampleRate, channelCount);
}

void DataDecoderDialog::appendFrames(const QVector<Ax25Frame>& frames)
{
    if (m_paused)
    {
        m_pausedFrames.append(frames);
        if (m_pausedFrames.size() > kMaximumRows)
        {
            m_pausedFrames.remove(0, m_pausedFrames.size() - kMaximumRows);
        }
        return;
    }
    for (const Ax25Frame& frame : frames)
    {
        while (m_packetTable->rowCount() >= kMaximumRows)
        {
            m_packetTable->removeRow(0);
        }
        const int row = m_packetTable->rowCount();
        m_packetTable->insertRow(row);
        const QStringList values{frame.receivedAt.toString(QStringLiteral("HH:mm:ss.zzz")),
                                 frame.protocol,
                                 frame.source,
                                 frame.destination,
                                 frame.path,
                                 frame.payload};
        for (int column = 0; column < values.size(); ++column)
        {
            auto* item = new QTableWidgetItem(values.at(column));
            item->setData(Qt::UserRole, values.at(column));
            m_packetTable->setItem(row, column, item);
        }
        addAddressLinks(m_packetTable, row, 2, frame.source);
        addAddressLinks(m_packetTable, row, 3, frame.destination);
        addAddressLinks(m_packetTable, row, 4, frame.path, true);
        m_packetTable->scrollToBottom();
    }
}

void DataDecoderDialog::updateStats(const Ax25DecoderStats& stats)
{
    const quint64 failures = stats.fcsFailures + stats.malformed;
    const quint64 completed = stats.decoded + failures;
    m_candidatesValue->setText(QString::number(stats.candidates));
    m_decodedValue->setText(QString::number(stats.decoded));
    m_rejectedValue->setText(QString::number(failures));
    m_successValue->setText(completed == 0 ? QStringLiteral("--")
                                           : QStringLiteral("%1%").arg(qRound(100.0 * stats.decoded / completed)));

    if (stats.decoded < m_lastDecodedCount)
    {
        m_lastDecode.invalidate();
    }
    else if (stats.decoded > m_lastDecodedCount)
    {
        m_lastDecode.restart();
    }
    m_lastDecodedCount = stats.decoded;

    const char* color = UiTheme::Color::TextStatusLabel;
    QString status = QStringLiteral("Waiting…");
    if (m_lastDecode.isValid() && m_lastDecode.elapsed() < 2000)
    {
        color = UiTheme::Color::Success;
        status = QStringLiteral("Decoding…");
    }
    else if (stats.audioLevel > 3)
    {
        color = UiTheme::Color::Accent;
        status = QStringLiteral("Receiving…");
    }
    m_statusIndicator->setStyleSheet(QStringLiteral("background: %1; border-radius: 7px;").arg(QLatin1String(color)));
    m_statusText->setText(status);
}

QString DataDecoderDialog::exportText() const
{
    QStringList lines{QStringLiteral("Timestamp\tProtocol\tSource\tDestination\tPath\tPayload")};
    for (int row = 0; row < m_packetTable->rowCount(); ++row)
    {
        QStringList fields;
        for (int column = 0; column < m_packetTable->columnCount(); ++column)
        {
            const QTableWidgetItem* item = m_packetTable->item(row, column);
            const QVariant retainedValue = item->data(Qt::UserRole);
            fields.append(retainedValue.isValid() ? retainedValue.toString() : item->text());
        }
        lines.append(fields.join(QLatin1Char('\t')));
    }
    return lines.join(QLatin1Char('\n'));
}

void DataDecoderDialog::exportPackets()
{
    const QString name = QStringLiteral("sdr9700-ax25-%1.txt")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QString path =
        QFileDialog::getSaveFileName(this, QStringLiteral("Export AX.25 Packets"), QDir::home().filePath(name),
                                     QStringLiteral("Text files (*.txt)"));
    if (path.isEmpty())
    {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        QMessageBox::warning(this, QStringLiteral("Export AX.25 Packets"), file.errorString());
        return;
    }
    QTextStream(&file) << exportText();
}
