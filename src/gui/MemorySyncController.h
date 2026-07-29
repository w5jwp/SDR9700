#pragma once

#include <QObject>
#include <QSet>
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
    quint16 currentGroup() const;
    quint16 currentChannel() const;

  private:
    void requestNextRadioMemory();
    void finishRadioMemoryRefresh(bool timedOut = false);
    bool allExpectedRadioMemoriesReceived() const;

    MemoryController* m_owner{nullptr};
    QTimer* m_refreshTimer{nullptr};
    QTimer* m_periodicRefreshTimer{nullptr};
    QTimer* m_syncTimeoutTimer{nullptr};
    QTimer* m_replyGraceTimer{nullptr};
    QSet<quint32> m_receivedMemoryKeys;
    QSet<quint32> m_expectedMemoryKeys;
    quint16 m_refreshGroup{1};
    quint16 m_refreshChannel{1};
    quint16 m_currentGroup{0};
    quint16 m_currentChannel{0};
    bool m_refreshInProgress{false};
    bool m_initialSyncComplete{false};
    Completion m_operationCompletion;
};
