#pragma once

#include <QObject>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

#include "MemoryStore.h"
#include "Types.h"

class MainWindow;
class QPushButton;
class QTimer;
class QWidget;

class MemoryController : public QObject
{
    Q_OBJECT

  public:
    explicit MemoryController(MainWindow* window);

    void buildMemoryWindow();
    void showMemoryWindow();
    void forceRadioMemorySync();
    QString selectedMemoryId() const;
    void selectCheckedMemory();
    void selectMemoryById(const QString& id, bool showDialogOnFailure);
    void editSelectedMemory();
    void copySelectedMemory();
    void removeSelectedMemory();
    void moveSelectedMemoryUp();
    void moveSelectedMemoryDown();
    void moveSelectedMemory(int direction);
    void storeCurrentMemory();
    void showMemoryEditor(const QString& memoryId);
    void reloadMemoryTable();
    bool backupRadioMemories();
    void restoreRadioMemories();
    bool exportRadioMemories();
    void importRadioMemories();
    bool resetRadioMemories();

  private:
    void requestRadioMemoryRefresh();
    void requestNextRadioMemory();
    void handleRadioMemoryReceived(MemoryType memory);
    void finishRadioMemoryRefresh(bool timedOut = false);
    void updateMemoryTableInteraction();
    void rebuildMemoryViews();
    QVector<MemoryRecord> currentMemories() const;
    MemoryRecord memoryForId(const QString& id, bool* found = nullptr) const;
    MemoryType radioMemoryForId(const QString& id, bool* found = nullptr) const;
    bool parseRadioMemoryId(const QString& id, quint16* group, quint16* channel) const;
    void writeMemoryRecord(const MemoryRecord& memory, quint16 group, quint16 channel);
    void deleteRadioMemory(quint16 group, quint16 channel);
    void queueRadioMemoryWrites(const QVector<MemoryType>& memories, int startDelayMs = 0);
    bool firstOpenChannelForGroup(quint16 group, quint16* channel) const;
    bool targetForMemoryFrequency(quint64 hz, quint16* group, quint16* channel) const;
    int queueRecordsToRadio(const QVector<MemoryRecord>& records, int* skippedCount, int startDelayMs = 0);
    void closeMemoryEditorPane(bool resizeWindow = true);
    QWidget* popupParent() const;

    MainWindow* m_window{nullptr};
    QTimer* m_radioMemoryRefreshTimer{nullptr};
    QTimer* m_radioMemoryPeriodicRefreshTimer{nullptr};
    QTimer* m_radioMemorySyncTimeoutTimer{nullptr};
    QHash<quint32, MemoryType> m_radioMemoriesByKey;
    QSet<quint32> m_receivedRadioMemoryKeys;
    QString m_openMemoryEditorId;
    QPushButton* m_memoryEditButton{nullptr};
    QWidget* m_memoryEditorPane{nullptr};
    QWidget* m_memoryEditorSeparator{nullptr};
    quint16 m_refreshGroup{1};
    quint16 m_refreshChannel{1};
    quint16 m_currentSyncGroup{0};
    quint16 m_currentSyncChannel{0};
    bool m_refreshInProgress{false};
};
