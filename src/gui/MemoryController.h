#pragma once

#include <QObject>
#include <QHash>
#include <QString>
#include <QUuid>
#include <QVector>
#include <functional>
#include <memory>

#include "MemoryStore.h"
#include "Types.h"

class MainWindow;
class MemoryCsvController;
class MemoryEditorController;
class MemoryEditorForm;
class MemoryDatabase;
class MemorySyncController;
class MemorySelectionController;
class MemoryViewController;
class MemoryWriteController;
class QWidget;

class MemoryController : public QObject
{
    Q_OBJECT

  public:
    explicit MemoryController(MainWindow* window);
    ~MemoryController() override;

    void buildMemoryWindow();
    void showMemoryWindow();
    void forceRadioMemorySync();
    void setMemoryPollIntervalSeconds(int seconds);
    void setShowSpecialMemories(bool show);
    void setShowSatelliteMemories(bool show);
    QString selectedMemoryId() const;
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
    void setRadioProfileId(const QUuid& profileId);

    bool memoryRefreshInProgress() const;
    bool memoryOperationInProgress() const;
    void showMemoryToast(const QString& message);
    QWidget* popupParent() const;

  signals:
    void initialMemorySyncChanged(bool complete);

  private:
    // These QObject children collaborate over the controller-owned memory map
    // and editor widgets. Keeping that state private to the subsystem avoids a
    // public accessor surface that would let unrelated GUI code mutate it.
    friend class MemoryCsvController;
    friend class MemorySyncController;
    friend class MemorySelectionController;
    friend class MemoryViewController;
    friend class MemoryWriteController;

    // The editor controller owns the extracted editor workflow and operates on
    // the memory state whose lifetime remains managed by this controller.
    friend class MemoryEditorController;
    friend class MemoryEditorForm;

    using MemoryWriteCompletion = std::function<void(bool success)>;

    void requestRadioMemoryRefresh();
    void handleRadioMemoryReceived(MemoryType memory);
    void beginMemoryDatabaseSync();
    void finishMemoryDatabaseSync(int expectedSlotCount);
    void cancelMemoryDatabaseSync();
    void persistRadioMemoryReply(const MemoryType& memory);
    void restoreCommittedMemoryDatabaseSnapshot();
    void scheduleMemoryViewsRebuild();
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

    MainWindow* m_window{nullptr};
    MemoryCsvController* m_memoryCsvController{nullptr};
    MemoryEditorController* m_memoryEditorController{nullptr};
    MemorySyncController* m_memorySyncController{nullptr};
    MemorySelectionController* m_memorySelectionController{nullptr};
    MemoryViewController* m_memoryViewController{nullptr};
    MemoryWriteController* m_memoryWriteController{nullptr};
    std::unique_ptr<MemoryDatabase> m_memoryDatabase;
    QUuid m_radioProfileId;
    QHash<quint32, MemoryType> m_radioMemoriesByKey;
    QHash<quint32, MemoryType> m_pendingDatabaseReplies;
};
