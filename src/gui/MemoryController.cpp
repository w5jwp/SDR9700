#include "MemoryController.h"

#include "ConfigurationManager.h"
#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "MemoryPanel.h"
#include "VfoPanel.h"
#include "UtilityWindow.h"
#include "models/RadioModel.h"
#include "models/VfoModel.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QUuid>
#include <algorithm>
#include <iterator>

using namespace sdr9700::ui::main_window;


MemoryController::MemoryController(MainWindow* window) : QObject(window), m_window(window) {}

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
    auto* root = new QVBoxLayout(panel);
    root->setContentsMargins(kMemoryPanelMargins);
    root->setSpacing(kMemoryPanelSpacing);

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

    auto* selectGroup = new QGroupBox(panel);
    auto* selectLayout = new QHBoxLayout(selectGroup);
    selectLayout->setContentsMargins(kMemoryToolbarGroupMargins);
    selectLayout->setSpacing(kMemoryToolbarGroupSpacing);
    auto* selectButton = new QPushButton("Select", panel);
    selectLayout->addWidget(selectButton);
    toolbar->addWidget(selectGroup);

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
    root->addLayout(toolbar);

    m_window->m_memoryTable = new QTableWidget(panel);
    m_window->m_memoryTable->setColumnCount(kMemoryTableColumnCount);
    m_window->m_memoryTable->setHorizontalHeaderLabels(
        {QStringLiteral("#"), QStringLiteral("Name"), QStringLiteral("Frequency (RX)"), QStringLiteral("Shift"),
         QStringLiteral("Tone"), QStringLiteral("Notes"), QStringLiteral("Band Key"), QStringLiteral("ID")});
    m_window->m_memoryTable->setColumnHidden(kMemoryBandKeyColumn, true);
    m_window->m_memoryTable->setColumnHidden(kMemoryIdColumn, true);
    m_window->m_memoryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_window->m_memoryTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_window->m_memoryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_window->m_memoryTable->setSortingEnabled(false);
    m_window->m_memoryTable->setDragDropMode(QAbstractItemView::NoDragDrop);
    m_window->m_memoryTable->setShowGrid(true);
    m_window->m_memoryTable->setGridStyle(Qt::SolidLine);
    m_window->m_memoryTable->setStyleSheet(
        QStringLiteral("QTableWidget { gridline-color: %1; }").arg(UiTheme::Color::Border));
    m_window->m_memoryTable->verticalHeader()->setVisible(false);
    m_window->m_memoryTable->horizontalHeader()->setStretchLastSection(false);
    m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryNumberColumn, QHeaderView::Interactive);
    m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryNameColumn, QHeaderView::Interactive);
    m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryFrequencyColumn, QHeaderView::Interactive);
    m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryShiftColumn, QHeaderView::Interactive);
    m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryToneColumn, QHeaderView::Interactive);
    m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryNotesColumn, QHeaderView::Stretch);
    m_window->m_memoryTable->setColumnWidth(kMemoryNumberColumn, kMemoryNumberColumnWidth);
    m_window->m_memoryTable->setColumnWidth(kMemoryNameColumn, kMemoryNameColumnWidth);
    m_window->m_memoryTable->setColumnWidth(kMemoryFrequencyColumn, kMemoryFrequencyColumnWidth);
    m_window->m_memoryTable->setColumnWidth(kMemoryShiftColumn, kMemoryShiftColumnWidth);
    m_window->m_memoryTable->setColumnWidth(kMemoryToneColumn, kMemoryToneColumnWidth);
    root->addWidget(m_window->m_memoryTable, 1);

    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(kNoMargins);
    auto* localNote = new QLabel("These memories are local to SDR9700 and are not saved in the radio.", panel);
    localNote->setStyleSheet("QLabel { color: palette(mid); }");
    m_window->m_closeMemoryWindowOnSelectCheck = new QCheckBox("Close after selection", panel);
    m_window->m_closeMemoryWindowOnSelectCheck->setChecked(
        AppSettings::instance().value(QString::fromLatin1(kMemoryWindowCloseOnSelectSettingsKey), "True").toBool());
    m_window->m_closeMemoryWindowOnSelectCheck->setToolTip("Close the Memories window after selecting a memory.");
    m_window->m_closeMemoryWindowOnSelectCheck->setStyleSheet("QCheckBox { color: palette(mid); }");
    m_window->m_memoryCountLabel = new QLabel(panel);
    m_window->m_memoryCountLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_window->m_memoryCountLabel->setStyleSheet("QLabel { color: palette(mid); }");
    auto* closeButton = new QPushButton("Close", panel);
    footer->addWidget(localNote, 1);
    footer->addWidget(m_window->m_closeMemoryWindowOnSelectCheck);
    footer->addWidget(m_window->m_memoryCountLabel);
    footer->addWidget(closeButton);
    root->addLayout(footer);

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
    connect(m_window->m_closeMemoryWindowOnSelectCheck, &QCheckBox::toggled, this, [](bool checked)
            { AppSettings::instance().setValue(QString::fromLatin1(kMemoryWindowCloseOnSelectSettingsKey), checked); });
    connect(selectButton, &QPushButton::clicked, this, &MemoryController::selectCheckedMemory);
    connect(upButton, &QPushButton::clicked, this, &MemoryController::moveSelectedMemoryUp);
    connect(downButton, &QPushButton::clicked, this, &MemoryController::moveSelectedMemoryDown);
    connect(addButton, &QPushButton::clicked, this, &MemoryController::storeCurrentMemory);
    connect(editButton, &QPushButton::clicked, this, &MemoryController::editSelectedMemory);
    connect(copyButton, &QPushButton::clicked, this, &MemoryController::copySelectedMemory);
    connect(removeButton, &QPushButton::clicked, this, &MemoryController::removeSelectedMemory);
    connect(importButton, &QPushButton::clicked, this,
            [this]()
            {
                const MemoryImportResult result = ConfigurationManager::importMemories(m_window);
                if (result.success)
                {
                    reloadMemoryTable();
                    m_window->showToast(QStringLiteral("Imported %1 memories").arg(result.importedCount));
                }
            });
    connect(exportButton, &QPushButton::clicked, this,
            [this]()
            {
                if (ConfigurationManager::exportMemories(m_window))
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
        QMessageBox::information(m_window, "Select Memory", "Choose one memory first.");
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
            QMessageBox::information(m_window, "Select Memory", "Unlock controls before selecting a memory.");
        }
        else
        {
            m_window->showToast(QStringLiteral("Controls are locked"), 4000, MainWindow::ToastKind::Warning);
        }
        return;
    }

    const QVector<MemoryRecord> memories = loadMemories();
    auto it =
        std::find_if(memories.cbegin(), memories.cend(), [&id](const MemoryRecord& memory) { return memory.id == id; });
    if (it == memories.cend())
    {
        return;
    }
    if (!m_window->m_model->isReady() || !m_window->m_vfo)
    {
        if (showDialogOnFailure)
        {
            QMessageBox::information(m_window, "Select Memory",
                                     "Connect to the radio and wait for sync before selecting a memory.");
        }
        else
        {
            m_window->showToast(QStringLiteral("Connect to the radio before selecting a memory"), 4000,
                                MainWindow::ToastKind::Warning);
        }
        return;
    }

    const MemoryRecord& memory = *it;
    m_window->setActiveMemory(memory.id, memory.name, memory.receiveHz, memory.duplexMode, memory.offsetHz,
                              memory.toneMode, memory.toneValue);
    m_window->m_applyingMemorySelection = true;
    const int generation = ++m_window->m_memorySelectionGeneration;
    m_window->m_vfo->setFrequencyHz(memory.receiveHz);
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
    if (m_window->m_memoryWindow && m_window->m_closeMemoryWindowOnSelectCheck &&
        m_window->m_closeMemoryWindowOnSelectCheck->isChecked())
    {
        m_window->m_memoryWindow->hide();
    }
}

void MemoryController::editSelectedMemory()
{
    const QString id = selectedMemoryId();
    if (id.isEmpty())
    {
        QMessageBox::information(m_window, "Edit Memory", "Choose one memory first.");
        return;
    }
    showMemoryEditor(id);
}

void MemoryController::copySelectedMemory()
{
    const QString id = selectedMemoryId();
    if (id.isEmpty())
    {
        QMessageBox::information(m_window, "Copy Memory", "Choose one memory first.");
        return;
    }

    QVector<MemoryRecord> memories = loadMemories();
    auto it =
        std::find_if(memories.begin(), memories.end(), [&id](const MemoryRecord& memory) { return memory.id == id; });
    if (it == memories.end())
    {
        return;
    }

    MemoryRecord copy = *it;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.name = (copy.name.isEmpty() ? QStringLiteral("Copy") : QStringLiteral("%1 Copy").arg(copy.name))
                    .left(kMemoryNameMaxChars);

    const int insertIndex = int(std::distance(memories.begin(), it)) + 1;
    memories.insert(insertIndex, copy);
    if (!saveMemories(memories))
    {
        QMessageBox::warning(m_window, "Copy Memory", "Could not save the copied memory.");
        return;
    }
    reloadMemoryTable();

    for (int row = 0; row < m_window->m_memoryTable->rowCount(); ++row)
    {
        const auto* idItem = m_window->m_memoryTable->item(row, kMemoryIdColumn);
        if (idItem && idItem->text() == copy.id)
        {
            m_window->m_memoryTable->selectRow(row);
            break;
        }
    }
    m_window->showToast(QStringLiteral("Copied memory: %1").arg(copy.name));
}

void MemoryController::removeSelectedMemory()
{
    const QString id = selectedMemoryId();
    if (id.isEmpty())
    {
        QMessageBox::information(m_window, "Remove Memory", "Choose one memory first.");
        return;
    }

    QVector<MemoryRecord> memories = loadMemories();
    auto it =
        std::find_if(memories.begin(), memories.end(), [&id](const MemoryRecord& memory) { return memory.id == id; });
    if (it == memories.end())
    {
        return;
    }

    if (QMessageBox::question(m_window, "Remove Memory", QStringLiteral("Remove memory \"%1\"?").arg(it->name)) !=
        QMessageBox::Yes)
    {
        return;
    }

    memories.erase(it);
    if (!saveMemories(memories))
    {
        QMessageBox::warning(m_window, "Remove Memory", "Could not remove the selected memory.");
        return;
    }
    reloadMemoryTable();
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
        QMessageBox::information(m_window, "Move Memory", "Choose one memory first.");
        return;
    }

    QVector<MemoryRecord> memories = loadMemories();
    const QString bandFilter =
        m_window->m_memoryBandFilter ? m_window->m_memoryBandFilter->currentData().toString() : QString();
    if (!bandFilter.isEmpty())
    {
        QMessageBox::information(m_window, "Move Memory", "Switch to All memories before reordering.");
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

    std::swap(memories[visibleIndexes.at(visiblePosition)].number, memories[visibleIndexes.at(targetPosition)].number);
    if (!saveMemories(memories))
    {
        QMessageBox::warning(m_window, "Move Memory", "Could not save the memory order.");
        return;
    }
    reloadMemoryTable();

    for (int row = 0; row < m_window->m_memoryTable->rowCount(); ++row)
    {
        const auto* idItem = m_window->m_memoryTable->item(row, kMemoryIdColumn);
        if (idItem && idItem->text() == id)
        {
            m_window->m_memoryTable->selectRow(row);
            break;
        }
    }
}

void MemoryController::storeCurrentMemory()
{
    showMemoryEditor(QString());
}

void MemoryController::showMemoryEditor(const QString& memoryId)
{
    QDialog dialog(m_window);
    static constexpr int kMemoryEditorWidth = 460;
    const bool editing = !memoryId.isEmpty();
    dialog.setWindowTitle("Add/Edit Memory");
    applyTitleCloseOnlyFlags(&dialog);
    auto* root = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    form->setHorizontalSpacing(14);
    auto* nameEdit = new QLineEdit(&dialog);
    nameEdit->setMaxLength(kMemoryNameMaxChars);
    nameEdit->setPlaceholderText(QStringLiteral("Maximum %1 characters").arg(kMemoryNameMaxChars));
    auto* frequencyEdit = new QLineEdit(&dialog);
    frequencyEdit->setPlaceholderText("145.000000");
    auto* offsetCombo = new QComboBox(&dialog);
    auto* customOffsetRow = new QWidget(&dialog);
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
    auto* toneOptionCombo = new QComboBox(&dialog);
    toneOptionCombo->addItem("Off", ratrNN);
    toneOptionCombo->addItem("TONE", ratrTN);
    toneOptionCombo->addItem("CTCSS", ratrNT);
    toneOptionCombo->addItem("DCS", ratrDN);
    toneOptionCombo->addItem("DTCS", ratrDD);
    ushort tonePickerValue{0};
    bool tonePickerValid{false};
    auto* toneValueBtn = new QPushButton(QStringLiteral("-- Select --"), &dialog);
    auto* toneValueLabel = new QLabel("Tone Frequency:", &dialog);
    auto setTonePick = [&tonePickerValue, &tonePickerValid, toneValueBtn](ushort v, const QString& label)
    {
        tonePickerValue = v;
        tonePickerValid = true;
        toneValueBtn->setText(label);
    };
    auto clearTonePick = [&tonePickerValue, &tonePickerValid, toneValueBtn]()
    {
        tonePickerValue = 0;
        tonePickerValid = false;
        toneValueBtn->setText(QStringLiteral("-- Select --"));
    };
    auto* notesEdit = new QPlainTextEdit(&dialog);
    notesEdit->setFixedHeight(90);

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
        for (int i = 0; i < offsetCombo->count(); ++i)
        {
            if (offsetCombo->itemData(i, Qt::UserRole).toInt() == mode &&
                offsetCombo->itemData(i, Qt::UserRole + 1).toULongLong() == offsetHz)
            {
                offsetCombo->setCurrentIndex(i);
                return;
            }
        }
        customOffsetModeCombo->setCurrentIndex(qMax(0, customOffsetModeCombo->findData(mode)));
        customOffsetSpin->setValue(offsetHz / 1000000.0);
        offsetCombo->setCurrentIndex(qMax(0, offsetCombo->findData(kMemoryOffsetCustom)));
    };

    auto updateCustomOffsetVisibility = [&dialog, form, offsetCombo, customOffsetRow]()
    {
        const bool customSelected = offsetCombo->currentData(Qt::UserRole).toInt() == kMemoryOffsetCustom;
        customOffsetRow->setVisible(customSelected);
        if (QWidget* label = form->labelForField(customOffsetRow))
        {
            label->setVisible(customSelected);
        }
        dialog.adjustSize();
        dialog.setFixedWidth(kMemoryEditorWidth);
    };

    populateOffsetOptions();
    updateCustomOffsetVisibility();

    auto populateToneValues = [clearTonePick]() { clearTonePick(); };

    auto updateToneValueVisibility = [&dialog, form, toneOptionCombo, toneValueBtn, toneValueLabel]()
    {
        const auto mode = static_cast<rptAccessTxRx_t>(toneOptionCombo->currentData().toInt());
        const bool toneSelected = mode != ratrNN;
        toneValueLabel->setText(isDtcsToneMode(mode) ? QStringLiteral("Digital Code:")
                                                     : QStringLiteral("Tone Frequency:"));
        toneValueBtn->setVisible(toneSelected);
        toneValueLabel->setVisible(toneSelected);
        dialog.adjustSize();
        dialog.setFixedWidth(kMemoryEditorWidth);
    };

    auto copyCurrentSettings = [this, nameEdit, frequencyEdit, offsetCombo, customOffsetModeCombo, customOffsetSpin,
                                toneOptionCombo, setTonePick, populateToneValues, populateOffsetOptions,
                                setOffsetSelection, updateCustomOffsetVisibility, updateToneValueVisibility]()
    {
        if (!m_window->m_model->isReady() || !m_window->m_vfo)
        {
            QMessageBox::information(m_window, "Copy Current Settings",
                                     "Connect to the radio and wait for sync before copying current settings.");
            return;
        }

        frequencyEdit->setText(memoryFrequencyLabel(m_window->m_vfo->frequencyHz()));
        if (nameEdit->text().trimmed().isEmpty())
        {
            nameEdit->setText(memoryFrequencyLabel(m_window->m_vfo->frequencyHz()));
        }
        populateOffsetOptions();
        setOffsetSelection(m_window->m_duplexMode, m_window->m_repeaterOffsetHz);
        updateCustomOffsetVisibility();
        toneOptionCombo->setCurrentIndex(qMax(0, toneOptionCombo->findData(m_window->m_toneAccessMode)));
        populateToneValues();
        updateToneValueVisibility();
        const bool isDtcs = isDtcsToneMode(m_window->m_toneAccessMode);
        const ushort toneValue = isDtcs ? m_window->m_dtcsCode : m_window->m_toneFrequency;
        const QString toneText = isDtcs ? dtcsCodeLabel(toneValue) : toneFrequencyLabel(toneValue);
        setTonePick(toneValue, toneText);
    };

    populateToneValues();
    connect(toneOptionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog,
            [populateToneValues, updateToneValueVisibility]()
            {
                populateToneValues();
                updateToneValueVisibility();
            });
    connect(toneValueBtn, &QPushButton::clicked, toneValueBtn,
            [toneValueBtn, toneOptionCombo, setTonePick]()
            {
                const auto mode = static_cast<rptAccessTxRx_t>(toneOptionCombo->currentData().toInt());
                if (mode == ratrNN)
                {
                    return;
                }
                QMenu menu(toneValueBtn);
                styleCompactMenu(&menu);
                auto styleToneGridButton = [](QPushButton* button)
                {
                    button->setFixedSize(54, 24);
                    button->setCursor(Qt::PointingHandCursor);
                    button->setStyleSheet(
                        QStringLiteral("QPushButton { background: %1; border: 1px solid %2; border-radius: 3px; "
                                       "color: %3; font-size: 11px; }"
                                       "QPushButton:hover { background: %4; border-color: %5; color: %6; }")
                            .arg(UiTheme::Color::Button, UiTheme::Color::BorderLight, UiTheme::Color::TextPrimary,
                                 UiTheme::Color::AccentDark, UiTheme::Color::Accent, UiTheme::Color::White));
                };
                auto* panel = new QWidget(&menu);
                auto* grid = new QGridLayout(panel);
                grid->setContentsMargins(6, 6, 6, 6);
                grid->setHorizontalSpacing(4);
                grid->setVerticalSpacing(4);
                if (isDtcsToneMode(mode))
                {
                    static constexpr int kCols = 6;
                    int idx = 0;
                    for (const ushort code : kDtcsCodes)
                    {
                        const QString label = dtcsCodeLabel(code);
                        auto* btn = new QPushButton(label, panel);
                        styleToneGridButton(btn);
                        connect(btn, &QPushButton::clicked, &menu,
                                [setTonePick, label, code, menuPtr = &menu]()
                                {
                                    setTonePick(code, label);
                                    menuPtr->close();
                                });
                        grid->addWidget(btn, idx / kCols, idx % kCols);
                        ++idx;
                    }
                }
                else
                {
                    static constexpr int kCols = 4;
                    int idx = 0;
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
                }
                auto* action = new QWidgetAction(&menu);
                action->setDefaultWidget(panel);
                menu.addAction(action);
                menu.exec(toneValueBtn->mapToGlobal(QPoint(0, toneValueBtn->height())));
            });
    connect(frequencyEdit, &QLineEdit::editingFinished, &dialog,
            [populateOffsetOptions, updateCustomOffsetVisibility]()
            {
                populateOffsetOptions();
                updateCustomOffsetVisibility();
            });
    connect(offsetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, updateCustomOffsetVisibility);

    auto applyMemoryToForm = [nameEdit, frequencyEdit, populateOffsetOptions, setOffsetSelection,
                              updateCustomOffsetVisibility, toneOptionCombo, setTonePick, notesEdit, populateToneValues,
                              updateToneValueVisibility](const MemoryRecord& memory)
    {
        nameEdit->setText(memory.name);
        frequencyEdit->setText(memoryFrequencyLabel(memory.receiveHz));
        populateOffsetOptions();
        setOffsetSelection(static_cast<duplexMode_t>(memory.duplexMode), memory.offsetHz);
        updateCustomOffsetVisibility();
        toneOptionCombo->setCurrentIndex(qMax(0, toneOptionCombo->findData(memory.toneMode)));
        populateToneValues();
        updateToneValueVisibility();
        if (memory.toneMode != ratrNN && memory.toneValue > 0)
        {
            const bool isDtcs = isDtcsToneMode(static_cast<rptAccessTxRx_t>(memory.toneMode));
            const QString toneText = isDtcs ? dtcsCodeLabel(memory.toneValue) : toneFrequencyLabel(memory.toneValue);
            setTonePick(memory.toneValue, toneText);
        }
        notesEdit->setPlainText(memory.notes);
    };

    if (editing)
    {
        const QVector<MemoryRecord> memories = loadMemories();
        auto it = std::find_if(memories.cbegin(), memories.cend(),
                               [&memoryId](const MemoryRecord& memory) { return memory.id == memoryId; });
        if (it == memories.cend())
        {
            QMessageBox::information(m_window, "Edit Memory", "Select a memory to edit.");
            return;
        }
        applyMemoryToForm(*it);
    }

    form->addRow("Name:", nameEdit);
    form->addRow("Frequency (RX):", frequencyEdit);
    form->addRow("Offset:", offsetCombo);
    form->addRow("Custom Offset:", customOffsetRow);
    form->addRow("Tone Option:", toneOptionCombo);
    form->addRow(toneValueLabel, toneValueBtn);
    form->addRow("Notes:", notesEdit);
    root->addLayout(form);
    updateCustomOffsetVisibility();
    updateToneValueVisibility();

    root->addSpacing(12);
    auto* buttonRow = new QWidget(&dialog);
    auto* buttonRowLayout = new QHBoxLayout(buttonRow);
    buttonRowLayout->setContentsMargins(0, 0, 0, 0);
    auto* copyButton = new QPushButton("Copy Current", buttonRow);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, buttonRow);
    buttonRowLayout->addWidget(copyButton, 0, Qt::AlignLeft);
    buttonRowLayout->addStretch(1);
    buttonRowLayout->addWidget(buttons, 0, Qt::AlignRight);
    root->addWidget(buttonRow);
    dialog.setFixedWidth(kMemoryEditorWidth);
    connect(copyButton, &QPushButton::clicked, &dialog, copyCurrentSettings);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    MemoryRecord memory;
    bool submitted = false;
    connect(buttons->button(QDialogButtonBox::Ok), &QPushButton::clicked, &dialog,
            [&]()
            {
                quint64 receiveHz = 0;
                if (!parseFrequencyText(frequencyEdit->text(), &receiveHz))
                {
                    QMessageBox::warning(&dialog, "Add/Edit Memory", "Enter a valid receive frequency.");
                    frequencyEdit->setFocus();
                    frequencyEdit->selectAll();
                    return;
                }

                const auto toneMode = static_cast<rptAccessTxRx_t>(toneOptionCombo->currentData().toInt());
                bool toneOk = true;
                ushort toneValue = 0;
                if (toneMode != ratrNN)
                {
                    toneOk = tonePickerValid;
                    toneValue = tonePickerValue;
                }
                if (!toneOk)
                {
                    QMessageBox::warning(&dialog, "Add/Edit Memory", "Select a valid tone frequency or DCS/DTCS code.");
                    toneValueBtn->setFocus();
                    return;
                }

                memory.id = editing ? memoryId : QUuid::createUuid().toString(QUuid::WithoutBraces);
                memory.name = nameEdit->text().trimmed();
                if (memory.name.length() > kMemoryNameMaxChars)
                {
                    QMessageBox::warning(
                        &dialog, "Add/Edit Memory",
                        QStringLiteral("Memory names are limited to %1 characters.").arg(kMemoryNameMaxChars));
                    nameEdit->setFocus();
                    nameEdit->selectAll();
                    return;
                }
                if (memory.name.isEmpty())
                {
                    memory.name = memoryFrequencyLabel(receiveHz);
                }
                if (editing)
                {
                    const QVector<MemoryRecord> existingMemories = loadMemories();
                    auto existingIt =
                        std::find_if(existingMemories.cbegin(), existingMemories.cend(),
                                     [&memoryId](const MemoryRecord& existing) { return existing.id == memoryId; });
                    if (existingIt != existingMemories.cend())
                    {
                        memory.number = existingIt->number;
                    }
                }
                memory.receiveHz = receiveHz;
                memory.band = bandLabelForHz(memory.receiveHz);
                memory.bandKey = memoryBandKeyForHz(memory.receiveHz);
                if (offsetCombo->currentData(Qt::UserRole).toInt() == kMemoryOffsetCustom)
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
                memory.notes = notesEdit->toPlainText().trimmed();
                submitted = true;
                dialog.accept();
            });

    m_window->centerPopupWindow(&dialog);
    if (dialog.exec() != QDialog::Accepted || !submitted)
    {
        return;
    }

    QVector<MemoryRecord> memories = loadMemories();
    if (editing)
    {
        auto current = std::find_if(memories.begin(), memories.end(),
                                    [&memory](const MemoryRecord& record) { return record.id == memory.id; });
        if (current != memories.end())
        {
            *current = memory;
        }
    }
    else
    {
        memories.append(memory);
    }
    if (!saveMemories(memories))
    {
        QMessageBox::warning(m_window, "Add/Edit Memory", "Could not save the memory.");
        return;
    }
    reloadMemoryTable();
    showMemoryWindow();
    m_window->showToast(editing ? QStringLiteral("Memory updated") : QStringLiteral("Memory stored"));
}

void MemoryController::reloadMemoryTable()
{
    const QString bandFilter =
        m_window->m_memoryBandFilter ? m_window->m_memoryBandFilter->currentData().toString() : QString();
    const QVector<MemoryRecord> memories = loadMemories();
    if (m_window->m_memoryPanel)
    {
        m_window->m_memoryPanel->setMemories(memories, m_window->m_activeMemoryId);
    }

    if (!m_window->m_memoryTable)
    {
        return;
    }

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

        auto* numberItem = setItem(kMemoryNumberColumn, memoryNumberLabel(memory.number));
        numberItem->setData(Qt::UserRole, memory.number);
        numberItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        setItem(kMemoryNameColumn, memory.name);
        auto* frequencyItem = setItem(kMemoryFrequencyColumn, memoryFrequencyLabel(memory.receiveHz));
        frequencyItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(memory.receiveHz));
        setItem(kMemoryShiftColumn, memory.shift);
        auto* toneItem = setItem(kMemoryToneColumn, memoryToneDisplayLabel(memory));
        toneItem->setToolTip(toneItem->text());
        auto* notesItem = setItem(kMemoryNotesColumn, memory.notes);
        notesItem->setToolTip(memory.notes);
        auto* bandKeyItem =
            setItem(kMemoryBandKeyColumn, QStringLiteral("%1").arg(memory.bandKey, 4, 10, QLatin1Char('0')));
        bandKeyItem->setData(Qt::UserRole, memory.bandKey);
        setItem(kMemoryIdColumn, memory.id);
        ++visibleCount;
    }
    if (m_window->m_memoryCountLabel)
    {
        const int totalCount = memories.size();
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
