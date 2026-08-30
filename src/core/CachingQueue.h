// cppcheck-suppress-file unusedStructMember
#pragma once
#include <QObject>
#include <QMap>
#include <QMultiMap>
#include <QVariant>
#include <QVector>
#include <QQueue>
#include <QHash>
#include <QRect>
#include <QDateTime>
#include <QRandomGenerator>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

#include "Types.h"
#include "RadioIdentities.h"

enum QueuePriority
{
    // Prime priorities let the scheduler use modulo checks so lower-priority
    // recurring queues are processed at predictable intervals without starving.
    kPriorityNone = 0,
    kPriorityImmediate = 1,
    kPriorityHighest = 2,
    kPriorityHigh = 3,
    kPriorityMediumHigh = 5,
    kPriorityMedium = 7,
    kPriorityMediumLow = 11,
    kPriorityLow = 19,
    kPriorityLowest = 23
};

int priorityValue(const QString& name);

inline qint64 nextQueueItemId()
{
    static std::atomic<qint64> nextId{0};
    return nextId.fetch_add(1, std::memory_order_relaxed);
}

struct QueueItem
{
    QueueItem()
        : command(funcNone),
          param(),
          receiver(0),
          recurring(false),
          id(nextQueueItemId()),
          enqueuedAtMs(QDateTime::currentMSecsSinceEpoch())
    {
    }
    QueueItem(QueueItem const& q)
        : command(q.command),
          param(q.param),
          receiver(q.receiver),
          recurring(q.recurring),
          id(q.id),
          enqueuedAtMs(q.enqueuedAtMs)
    {
    }
    QueueItem(Funcs command, QVariant param, bool recurring, uchar receiver)
        : command(command),
          param(param),
          receiver(receiver),
          recurring(recurring),
          id(nextQueueItemId()),
          enqueuedAtMs(QDateTime::currentMSecsSinceEpoch())
    {
    }
    QueueItem(Funcs command, QVariant param, bool recurring)
        : command(command),
          param(param),
          receiver(0),
          recurring(recurring),
          id(nextQueueItemId()),
          enqueuedAtMs(QDateTime::currentMSecsSinceEpoch())
    {
    }
    QueueItem(Funcs command, QVariant param)
        : command(command),
          param(param),
          receiver(0),
          recurring(false),
          id(nextQueueItemId()),
          enqueuedAtMs(QDateTime::currentMSecsSinceEpoch())
    {
    }
    QueueItem(Funcs command, bool recurring, uchar receiver)
        : command(command),
          param(QVariant()),
          receiver(receiver),
          recurring(recurring),
          id(nextQueueItemId()),
          enqueuedAtMs(QDateTime::currentMSecsSinceEpoch())
    {
    }
    QueueItem(Funcs command, bool recurring)
        : command(command),
          param(QVariant()),
          receiver(0),
          recurring(recurring),
          id(nextQueueItemId()),
          enqueuedAtMs(QDateTime::currentMSecsSinceEpoch())
    {
    }
    explicit QueueItem(Funcs command)
        : command(command),
          param(QVariant()),
          receiver(0),
          recurring(false),
          id(nextQueueItemId()),
          enqueuedAtMs(QDateTime::currentMSecsSinceEpoch())
    {
    }
    QueueItem& operator=(QueueItem const& q)
    {
        command = q.command;
        param = q.param;
        receiver = q.receiver;
        recurring = q.recurring;
        id = q.id;
        enqueuedAtMs = q.enqueuedAtMs;
        return *this;
    }
    Funcs command;
    QVariant param;
    uchar receiver;
    bool recurring;
    qint64 id;
    qint64 enqueuedAtMs;

    bool operator==(const QueueItem& rhs) const
    {
        return (rhs.command == command && rhs.receiver == receiver && rhs.recurring == recurring && rhs.param == param);
    }
};

struct CacheItem
{
    CacheItem() : command(funcNone), req(), reply(), value(), receiver(0) {}
    CacheItem(CacheItem const& c) : command(c.command), req(c.req), reply(c.reply), value(c.value), receiver(c.receiver)
    {
    }
    CacheItem(Funcs command, QVariant value, uchar receiver = 0)
        : command(command), req(QDateTime()), reply(QDateTime()), value(value), receiver(receiver)
    {
    }

    Funcs command;
    QDateTime req;
    QDateTime reply;
    QVariant value;
    uchar receiver;
    CacheItem& operator=(const CacheItem& i)
    {
        this->receiver = i.receiver;
        this->command = i.command;
        this->reply = i.reply;
        this->req = i.req;
        this->value = i.value;
        return *this;
    }
};

struct CachingQueueDiagnostics
{
    qsizetype depth{0};
    qsizetype highWaterMark{0};
    qint64 oldestItemAgeMs{0};
    quint64 dispatched{0};
    quint64 droppedForCapacity{0};
    QMap<QueuePriority, qsizetype> depthByPriority;
};

class CachingQueue : public QObject
{
    Q_OBJECT
    friend class CachingQueueTest;

  signals:
    void haveCommand(Funcs func, QVariant param, uchar receiver);
    // Cache changes cross the worker-thread boundary as one batch per wake.
    // RadioBackend coalesces that batch again before posting it to RadioRouter,
    // which bounds queued Qt events when the GUI or radio-data thread stalls.
    void sendValues(QVector<CacheItem> items);
    void cacheUpdated(CacheItem item);

  public slots:
    void receiveValue(Funcs func, QVariant value, uchar receiver);

  private:
    static CachingQueue* instance;
    static std::mutex instanceMutex;

    std::mutex mutex;

    QMultiMap<QueuePriority, QueueItem> queue;
    QMultiMap<Funcs, CacheItem> cache;
    QQueue<CacheItem> items;
    QHash<quint64, qint64> m_cacheRefreshRequests;
    std::condition_variable waiting;

    void startWorker();
    void stopWorker();
    QueuePriority isRecurring(Funcs func, uchar receiver = 0);
    std::atomic_bool aborted{false};
    std::thread m_worker;
    // Command queue pacing for queued readbacks and cache refreshes. This used
    // to be initialized to -1, which disables CachingQueue::add(); keep this
    // positive unless intentionally backing out queued command scheduling.
    qint64 queueInterval = 50;
    // Set while holding mutex when add() inserts command work. The worker uses
    // this to distinguish a command wake from a cache-value wake so queued
    // readbacks run immediately without accelerating periodic cache traffic.
    bool m_queueWakeRequested{false};
    qsizetype m_queueHighWaterMark{0};
    quint64 m_dispatchedCommands{0};
    quint64 m_droppedForCapacity{0};
    static constexpr qsizetype kMaximumQueueDepth = 512;
    static constexpr quint64 kMaximumImmediateBurst = 8;

    radioCapabilities* radioCaps = nullptr; // Set after IC-9700 capabilities are loaded.

    void run();
    Funcs checkCommandAvailable(Funcs cmd) const;
    void enforceQueueLimit();
    static quint64 cacheRefreshKey(Funcs func, uchar receiver);
    std::optional<CacheItem> updateCache(bool reply, QueueItem item);
    std::optional<CacheItem> updateCache(bool reply, Funcs func, QVariant value = QVariant(), uchar receiver = 0);
    RadioStateType radioState;

  protected:
    explicit CachingQueue(QObject* parent = nullptr) : QObject(parent) {}
    ~CachingQueue();

  public:
    CachingQueue(const CachingQueue& other) = delete;
    void operator=(const CachingQueue&) = delete;

    static CachingQueue* getInstance();
    static void shutdownInstance();
    static bool cacheValuesDiffer(const QVariant& a, const QVariant& b);
    void add(QueuePriority prio, Funcs func, bool recurring = false, uchar receiver = 0);
    void add(QueuePriority prio, QueueItem item, bool unique = false);
    void addUnique(QueuePriority prio, Funcs func, bool recurring = false, uchar receiver = 0);

    QueuePriority del(Funcs func, uchar receiver = 0);
    void clear();
    void resetSessionState();
    CacheItem getCache(Funcs func, uchar receiver = 0);
    CachingQueueDiagnostics diagnostics();

    void setRadioCaps(radioCapabilities* caps);
    void recordLocalRoutingState(Funcs func, QVariant value, uchar receiver);
    VfoCommandType getVfoCommand(vfo_t vfo, uchar rx, bool set = false);
    RadioStateType getState()
    {
        std::lock_guard locker(mutex);
        return radioState;
    }
};

Q_DECLARE_METATYPE(QueueItem)
Q_DECLARE_METATYPE(CacheItem)
Q_DECLARE_METATYPE(QVector<CacheItem>)
