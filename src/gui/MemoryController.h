#pragma once

#include <QObject>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>
#include <functional>

#include "MemoryStore.h"
#include "Types.h"

class MainWindow;
class MemoryCsvController;
class MemoryEditorController;
class MemorySyncController;
class MemoryWriteController;
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
    void setMemoryPollIntervalSeconds(int seconds);
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
    bool exportRadioMemories();
    void importRadioMemories();
    bool initialMemorySyncComplete() const;

    // Narrow synchronization-controller-facing hooks.
    bool radioConnected() const;
    bool memoryRefreshInProgress() const;
    bool memoryOperationInProgress() const;
    bool memoryEditorVisible() const;
    void cancelMemoryRefresh();
    void requestRadioMemoryRefreshFromController();
    void setMemoryPollTimerIntervalSeconds(int seconds);
    void clearMemoryEditButtonChecked();
    void closeMemoryEditorFromController();
    void showMemoryToast(const QString& message);
    QWidget* popupParent() const;

  signals:
    void initialMemorySyncChanged(bool complete);

  private:
    friend class MemoryCsvController;
    friend class MemoryWriteController;

    // The editor controller owns the extracted editor workflow and operates on
    // the memory state whose lifetime remains managed by this controller.
    friend class MemoryEditorController;

    using MemoryWriteCompletion = std::function<void(bool success)>;

    void requestRadioMemoryRefresh();
    void requestNextRadioMemory();
    void handleRadioMemoryReceived(MemoryType memory);
    void finishRadioMemoryRefresh(bool timedOut = false);
    bool allExpectedRadioMemoriesReceived() const;
    void scheduleMemoryViewsRebuild();
    void resetStoredRadioMemoriesAfterSync();
    void updateMemoryTableInteraction();
    void rebuildMemoryViews();
    QVector<MemoryRecord> currentMemories() const;
    MemoryRecord memoryForId(const QString& id, bool* found = nullptr) const;
    MemoryType radioMemoryForId(const QString& id, bool* found = nullptr) const;
    bool parseRadioMemoryId(const QString& id, quint16* group, quint16* channel) const;
    void applyMemoryToVfo(const MemoryRecord& memory);
    void writeMemoryRecord(const MemoryRecord& memory, quint16 group, quint16 channel,
                           MemoryWriteCompletion completion = {});
    void deleteRadioMemory(quint16 group, quint16 channel, MemoryWriteCompletion completion = {});
    void queueRadioMemoryWrites(const QVector<MemoryType>& memories, int startDelayMs = 0,
                                const QString& progressLabel = QString(), MemoryWriteCompletion completion = {});
    bool firstOpenChannelForGroup(quint16 group, quint16* channel) const;
    void setMemoryProgress(const QString& label, int value, int maximum);
    void clearMemoryProgress();
    void closeMemoryEditorPane(bool resizeWindow = true);

    MainWindow* m_window{nullptr};
    MemoryCsvController* m_memoryCsvController{nullptr};
    MemoryEditorController* m_memoryEditorController{nullptr};
    MemorySyncController* m_memorySyncController{nullptr};
    MemoryWriteController* m_memoryWriteController{nullptr};
    QTimer* m_radioMemoryRefreshTimer{nullptr};
    QTimer* m_radioMemoryPeriodicRefreshTimer{nullptr};
    QTimer* m_radioMemorySyncTimeoutTimer{nullptr};
    QTimer* m_radioMemoryReplyGraceTimer{nullptr};
    QTimer* m_memoryViewRefreshTimer{nullptr};
    QHash<quint32, MemoryType> m_radioMemoriesByKey;
    QSet<quint32> m_receivedRadioMemoryKeys;
    QSet<quint32> m_expectedRadioMemoryKeys;
    QString m_openMemoryEditorId;
    QPushButton* m_memoryEditButton{nullptr};
    QWidget* m_memoryEditorPane{nullptr};
    QWidget* m_memoryEditorSeparator{nullptr};
    quint16 m_refreshGroup{1};
    quint16 m_refreshChannel{1};
    quint16 m_currentSyncGroup{0};
    quint16 m_currentSyncChannel{0};
    bool m_refreshInProgress{false};
    bool m_resetAfterSync{false};
    QString m_memoryProgressLabel;
    int m_memoryProgressValue{0};
    int m_memoryProgressMaximum{0};
    bool m_initialMemorySyncComplete{false};
};
