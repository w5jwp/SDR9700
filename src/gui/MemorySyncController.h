#pragma once

#include <QObject>
#include <QSet>
#include <QVector>
#include <functional>

class MemoryController;
class QTimer;

class MemorySyncController : public QObject
{
    Q_OBJECT

  public:
    using Completion = std::function<void(bool success)>;

    explicit MemorySyncController(MemoryController* owner);

    void forceRadioMemorySync();
    void setMemoryPollIntervalSeconds(int seconds);
    void handleRadioReadyChanged(bool ready);
    void handleRadioMemoryReceived(quint32 key);
    void requestRadioMemoryRefresh();
    void requestRadioMemoryRefreshForOperation(Completion completion);
    void cancelRadioMemoryRefresh();
    void clearReceivedMemories();

    bool initialMemorySyncComplete() const;
    bool refreshInProgress() const;
    bool hasReceivedMemory(quint32 key) const;
    // These values remain available after a sweep completes so the memory
    // view and deterministic tests can report the final verification result
    // without inferring it from transient progress-label text.
    int lastUnansweredSlotCount() const;
    int missingRetryRound() const;

  private:
    void startScheduledRadioMemoryRefresh();
    void requestNextRadioMemory();
    bool startMissingMemoryRetry();
    void finishRadioMemoryRefresh(bool timedOut = false);
    bool allExpectedRadioMemoriesReceived() const;

    MemoryController* m_owner{nullptr};
    QTimer* m_refreshTimer{nullptr};
    QTimer* m_periodicRefreshTimer{nullptr};
    QTimer* m_syncTimeoutTimer{nullptr};
    QTimer* m_replyGraceTimer{nullptr};
    QSet<quint32> m_receivedMemoryKeys;
    QSet<quint32> m_expectedMemoryKeys;
    // The initial pass contains every radio slot. Retry passes replace this
    // vector with only the still-unanswered keys while preserving the two sets
    // above as the authoritative whole-sweep completion state.
    QVector<quint32> m_pollKeys;
    qsizetype m_pollIndex{0};
    quint16 m_currentGroup{0};
    quint16 m_currentChannel{0};
    bool m_refreshInProgress{false};
    bool m_initialSyncComplete{false};
    bool m_scheduledRefreshInProgress{false};
    int m_operationSyncAttempt{0};
    int m_missingRetryRound{0};
    int m_lastUnansweredSlotCount{0};
    Completion m_operationCompletion;
};
