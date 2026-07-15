// cppcheck-suppress-file unusedStructMember
#pragma once
#include <QCoreApplication>
#include <QObject>
#include <QThread>
#include <QMutex>
#include <QMutexLocker>
#include <QMap>
#include <QMultiMap>
#include <QVariant>
#include <QVector>
#include <QQueue>
#include <QRect>
#include <QWaitCondition>
#include <QDateTime>
#include <QRandomGenerator>
#include <atomic>

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
    QueueItem() : command(funcNone), param(), receiver(0), recurring(false), id(nextQueueItemId()) {}
    QueueItem(QueueItem const& q)
        : command(q.command), param(q.param), receiver(q.receiver), recurring(q.recurring), id(q.id) {};
    QueueItem(Funcs command, QVariant param, bool recurring, uchar receiver)
        : command(command), param(param), receiver(receiver), recurring(recurring), id(nextQueueItemId()) {};
    QueueItem(Funcs command, QVariant param, bool recurring)
        : command(command), param(param), receiver(0), recurring(recurring), id(nextQueueItemId()) {};
    QueueItem(Funcs command, QVariant param)
        : command(command), param(param), receiver(0), recurring(false), id(nextQueueItemId()) {};
    QueueItem(Funcs command, bool recurring, uchar receiver)
        : command(command), param(QVariant()), receiver(receiver), recurring(recurring), id(nextQueueItemId()) {};
    QueueItem(Funcs command, bool recurring)
        : command(command), param(QVariant()), receiver(0), recurring(recurring), id(nextQueueItemId()) {};
    explicit QueueItem(Funcs command)
        : command(command), param(QVariant()), receiver(0), recurring(false), id(nextQueueItemId()) {};
    QueueItem& operator=(QueueItem const& q)
    {
        command = q.command;
        param = q.param;
        receiver = q.receiver;
        recurring = q.recurring;
        id = q.id;
        return *this;
    }
    Funcs command;
    QVariant param;
    uchar receiver;
    bool recurring;
    qint64 id = QDateTime::currentMSecsSinceEpoch();

    bool operator==(const QueueItem& rhs) const
    {
        return (rhs.command == command && rhs.receiver == receiver && rhs.recurring == recurring && rhs.param == param);
    }
};

struct CacheItem
{
    CacheItem() : command(funcNone), req(), reply(), value(), receiver(0) {};
    CacheItem(CacheItem const& c)
        : command(c.command), req(c.req), reply(c.reply), value(c.value), receiver(c.receiver) {};
    CacheItem(Funcs command, QVariant value, uchar receiver = 0)
        : command(command), req(QDateTime()), reply(QDateTime()), value(value), receiver(receiver) {};

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

class CachingQueue : public QThread
{
    Q_OBJECT

  signals:
    void haveCommand(Funcs func, QVariant param, uchar receiver);
    void sendValue(CacheItem item);
    void sendValues(QVector<CacheItem> items);
    void sendMessage(QString msg);
    void cacheUpdated(CacheItem item);
    void radioCapsUpdated(radioCapabilities* caps);

  public slots:
    void receiveValue(Funcs func, QVariant value, uchar receiver);

  private:
    static CachingQueue* instance;
    static QMutex instanceMutex;

    QMutex mutex;

    QMultiMap<QueuePriority, QueueItem> queue;
    QMultiMap<Funcs, CacheItem> cache;
    QQueue<CacheItem> items;
    QQueue<QString> messages;
    QWaitCondition waiting;

    void stopThread();
    void setCache(Funcs func, QVariant val, uchar receiver = 0);
    QueuePriority isRecurring(Funcs func, uchar receiver = 0);
    bool compare(QVariant a, QVariant b);

    bool aborted = false;
    qint64 queueInterval = -1;              // Queue worker is stopped while negative.

    radioCapabilities* radioCaps = nullptr; // Set after IC-9700 capabilities are loaded.

    void run();
    Funcs checkCommandAvailable(Funcs cmd, bool set = false) const;
    RadioStateType radioState;

  protected:
    explicit CachingQueue(QObject* parent = nullptr) : QThread(parent) {};
    ~CachingQueue();

  public:
    CachingQueue(const CachingQueue& other) = delete;
    void operator=(const CachingQueue&) = delete;

    static CachingQueue* getInstance(QObject* parent = nullptr);
    static void shutdownInstance();
    void message(QString msg);
    void add(QueuePriority prio, Funcs func, bool recurring = false, uchar receiver = 0);
    void add(QueuePriority prio, QueueItem item, bool unique = false);
    void addUnique(QueuePriority prio, Funcs func, bool recurring = false, uchar receiver = 0);
    void addUnique(QueuePriority prio, QueueItem item);

    QueuePriority del(Funcs func, uchar receiver = 0);
    void clear();
    void resetSessionState();
    // Caller must already hold mutex. This overload updates an existing queued
    // command after a matching radio reply has been processed.
    void updateCache(bool reply, QueueItem item);
    void updateCache(bool reply, Funcs func, QVariant value = QVariant(), uchar receiver = 0);

    CacheItem getCache(Funcs func, uchar receiver = 0);

    void setRadioCaps(radioCapabilities* caps);
    void recordLocalRoutingState(Funcs func, QVariant value, uchar receiver);
    VfoCommandType getVfoCommand(vfo_t vfo, uchar rx, bool set = false);
    RadioStateType getState()
    {
        QMutexLocker locker(&mutex);
        return radioState;
    }
};

Q_DECLARE_METATYPE(QueueItem)
Q_DECLARE_METATYPE(CacheItem)
Q_DECLARE_METATYPE(QVector<CacheItem>)
