#include "MemoryCsvController.h"

#include "MainWindow.h"
#include "MemoryController.h"
#include "MemoryControllerHelpers.h"
#include "models/RadioModel.h"

#include <QFileDialog>
#include <QMessageBox>

MemoryCsvController::MemoryCsvController(MemoryController* owner) : QObject(owner), m_owner(owner) {}

bool MemoryCsvController::exportRadioMemories()
{
    const QString path =
        QFileDialog::getSaveFileName(m_owner->popupParent(), QStringLiteral("Export Memories"),
                                     QStringLiteral("sdr9700-memories.csv"), QString::fromLatin1(kMemoryFileFilter));
    if (path.isEmpty())
    {
        return false;
    }

    if (!writeMemoriesCsvFile(path, m_owner->currentMemories()))
    {
        QMessageBox::warning(m_owner->popupParent(), QStringLiteral("Export Memories Failed"),
                             QStringLiteral("Memory export failed. Could not save the selected file."));
        return false;
    }

    QMessageBox::information(m_owner->popupParent(), QStringLiteral("Export Memories Successful"),
                             QStringLiteral("Memory export successful."));
    return true;
}

void MemoryCsvController::importRadioMemories()
{
    if (!m_owner->m_window->m_model || !m_owner->m_window->m_model->isConnected())
    {
        QMessageBox::information(m_owner->popupParent(), QStringLiteral("Import Memories"),
                                 QStringLiteral("Connect to the radio before importing memories."));
        return;
    }
    if (m_owner->m_refreshInProgress)
    {
        QMessageBox::information(m_owner->popupParent(), QStringLiteral("Import Memories"),
                                 QStringLiteral("Wait for the current radio memory sync to finish before importing."));
        return;
    }
    if (!m_owner->m_memoryProgressLabel.isEmpty())
    {
        QMessageBox::information(m_owner->popupParent(), QStringLiteral("Import Memories"),
                                 QStringLiteral("Wait for the current memory operation to finish before importing."));
        return;
    }

    const QString path = QFileDialog::getOpenFileName(m_owner->popupParent(), QStringLiteral("Import Memories"),
                                                      QString(), QString::fromLatin1(kMemoryFileFilter));
    if (path.isEmpty())
    {
        return;
    }

    QStringList importErrors;
    QString fileError;
    const QVector<MemoryRecord> records = readMemoriesCsvFile(path, &importErrors, &fileError);
    if (!fileError.isEmpty())
    {
        QMessageBox::warning(m_owner->popupParent(), QStringLiteral("Import Memories Failed"),
                             QStringLiteral("Memory import failed. Could not open the selected file."));
        return;
    }
    if (!importErrors.isEmpty())
    {
        const QString details = importErrors.mid(0, 12).join(QLatin1Char('\n'));
        const QString suffix =
            importErrors.size() > 12 ? QStringLiteral("\n...and %1 more").arg(importErrors.size() - 12) : QString();
        QMessageBox::warning(
            m_owner->popupParent(), QStringLiteral("Import Memories Failed"),
            QStringLiteral("Memory import failed. Fix the CSV file and try again.\n\n%1%2").arg(details, suffix));
        return;
    }
    if (records.isEmpty())
    {
        QMessageBox::warning(
            m_owner->popupParent(), QStringLiteral("Import Memories Failed"),
            QStringLiteral("Memory import failed. The selected file does not contain importable CSV memories."));
        return;
    }

    if (QMessageBox::question(
            m_owner->popupParent(), QStringLiteral("Import Memories"),
            QStringLiteral("Import these memories to the radio?\n\n"
                           "User memory channels 1-99 on 2M, 70CM, and 23CM will be cleared first.")) !=
        QMessageBox::Yes)
    {
        return;
    }

    QVector<MemoryType> backup;
    backup.reserve(m_owner->m_radioMemoriesByKey.size());
    for (const MemoryType& memory : m_owner->m_radioMemoriesByKey)
    {
        backup.append(memory);
    }
    QVector<MemoryType> uploads;
    uploads.reserve(records.size());
    for (const MemoryRecord& record : records)
    {
        uploads.append(radioMemoryFromRecord(record, record.group, record.channel));
    }

    m_owner->queueRadioMemoryWrites(
        deletedUserRadioMemories(), 0, QStringLiteral("Clearing existing memories"),
        [this, uploads, backup](bool cleared)
        {
            if (!cleared)
            {
                restoreRadioMemoriesAfterFailedImport(backup);
                return;
            }
            m_owner->m_radioMemoriesByKey.clear();
            m_owner->queueRadioMemoryWrites(
                uploads, 0, QStringLiteral("Uploading memories"),
                [this, backup, importedCount = uploads.size()](bool uploaded)
                {
                    if (!uploaded)
                    {
                        restoreRadioMemoriesAfterFailedImport(backup);
                        return;
                    }
                    m_owner->m_window->showToast(
                        QStringLiteral("Imported %1 memories successfully.").arg(importedCount));
                    m_owner->requestRadioMemoryRefresh();
                });
        });
}

void MemoryCsvController::restoreRadioMemoriesAfterFailedImport(const QVector<MemoryType>& backup)
{
    if (!m_owner->m_window->m_model || !m_owner->m_window->m_model->isConnected())
    {
        m_owner->m_window->showToast(QStringLiteral("Memory import failed. Reconnect and restore the previous export."),
                                     8000, MainWindow::ToastKind::Error);
        return;
    }

    // An import can fail after only part of its clear/upload batch reached the
    // radio. Clear the user slots again before replaying the in-memory snapshot;
    // otherwise imported rows beyond the failure point could survive beside the
    // restored set. This is operational rollback, not configuration migration.
    m_owner->queueRadioMemoryWrites(
        deletedUserRadioMemories(), 0, QStringLiteral("Rolling back memory import"),
        [this, backup](bool cleared)
        {
            if (!cleared)
            {
                m_owner->m_window->showToast(QStringLiteral("Memory import rollback failed while clearing channels."),
                                             8000, MainWindow::ToastKind::Error);
                m_owner->requestRadioMemoryRefresh();
                return;
            }
            m_owner->m_radioMemoriesByKey.clear();
            m_owner->queueRadioMemoryWrites(
                backup, 0, QStringLiteral("Restoring previous memories"),
                [this](bool restored)
                {
                    m_owner->m_window->showToast(
                        restored ? QStringLiteral("Memory import failed. Previous memories restored.")
                                 : QStringLiteral("Memory import rollback failed while restoring memories."),
                        8000, restored ? MainWindow::ToastKind::Warning : MainWindow::ToastKind::Error);
                    m_owner->requestRadioMemoryRefresh();
                });
        });
}
