#include "LogCategories.h"
#include "CachingQueue.h"

#include <QMetaType>

namespace
{
constexpr int kCacheLockTimeMs = 100;
bool shutdownHookInstalled = false;

const QMap<QString, int> kPriorityMap = {{"None", 0},        {"Immediate", 1},   {"Highest", 2},
                                         {"High", 3},        {"Medium High", 5}, {"Medium", 7},
                                         {"Medium Low", 11}, {"Low", 19},        {"Lowest", 23}};

} // namespace

int priorityValue(const QString& name)
{
    return kPriorityMap.value(name, 0);
}

CachingQueue* CachingQueue::instance{};
QMutex CachingQueue::instanceMutex;

CachingQueue* CachingQueue::getInstance(QObject* parent)
{
    QMutexLocker locker(&instanceMutex);
    if (instance == nullptr)
    {
        qRegisterMetaType<QVector<CacheItem>>("QVector<CacheItem>");
        instance = new CachingQueue();
        instance->setObjectName(QStringLiteral("CachingQueue()"));
        if (!shutdownHookInstalled)
        {
            shutdownHookInstalled = true;
            QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                             &CachingQueue::shutdownInstance);
        }
        instance->start(QThread::TimeCriticalPriority);
        qDebug(logRadio()) << "Created new CachingQueue() for:"
                           << ((parent != nullptr) ? parent->objectName() : "<unknown>");
    }
    return instance;
}

CachingQueue::~CachingQueue()
{
    stopThread();
    QMutexLocker locker(&instanceMutex);
    if (instance == this)
    {
        instance = nullptr;
    }
    qInfo(logRadio()) << "Destroying caching queue";
}

void CachingQueue::shutdownInstance()
{
    instanceMutex.lock();
    CachingQueue* queue = instance;
    instance = nullptr;
    instanceMutex.unlock();

    if (queue == nullptr)
    {
        return;
    }

    queue->stopThread();
    delete queue;
}

void CachingQueue::stopThread()
{
    aborted.store(true, std::memory_order_relaxed);
    {
        QMutexLocker locker(&mutex);
        waiting.wakeAll();
    }
    if (isRunning() && QThread::currentThread() != this)
    {
        if (!wait(kCacheLockTimeMs))
        {
            qWarning(logRadio()) << "CachingQueue() did not stop after" << kCacheLockTimeMs << "ms";
        }
    }
}

void CachingQueue::run()
{
    qInfo(logRadio()) << "Starting caching queue handler thread (ThreadId:" << QThread::currentThreadId() << ")";

    QMutexLocker locker(&mutex);
    QDeadlineTimer deadline(queueInterval);

    quint64 counter = kPriorityImmediate;

    while (!aborted.load(std::memory_order_relaxed))
    {
        if (!waiting.wait(&mutex, deadline.remainingTime()))
        {
            QueuePriority prio = kPriorityImmediate;
            QueueItem item;
            bool haveCommandToEmit = false;

            // If no immediate commands are queued, rotate through lower priorities.
            if (!queue.contains(prio))
            {
                if (counter % kPriorityHighest == 0)
                {
                    prio = kPriorityHighest;
                }
                else if (counter % kPriorityHigh == 0)
                {
                    prio = kPriorityHigh;
                }
                else if (counter % kPriorityMediumHigh == 0)
                {
                    prio = kPriorityMediumHigh;
                }
                else if (counter % kPriorityMedium == 0)
                {
                    prio = kPriorityMedium;
                }
                else if (counter % kPriorityMediumLow == 0)
                {
                    prio = kPriorityMediumLow;
                }
                else if (counter % kPriorityLow == 0)
                {
                    prio = kPriorityLow;
                }
                else if (counter % kPriorityLowest == 0)
                {
                    prio = kPriorityLowest;
                    counter = kPriorityImmediate;
                }
            }
            counter++;

            auto begin = queue.lowerBound(prio);
            auto end = queue.upperBound(prio);
            auto it = end;
            for (auto candidate = begin; candidate != end; ++candidate)
            {
                if (it == end || candidate.value().id < it.value().id)
                {
                    it = candidate;
                }
            }
            if (it != end)
            {
                item = it.value();
                queue.erase(it);
                // If this is a recurring command, add it back into the queue.
                if (item.recurring && prio != kPriorityImmediate)
                {
                    QueueItem recurringItem = item;
                    recurringItem.id = nextQueueItemId();
                    queue.insert(prio, recurringItem);
                }

                if (!item.recurring)
                {
                    updateCache(false, item.command, item.param, item.receiver);
                }
                haveCommandToEmit = true;
            }

            deadline.setRemainingTime(queueInterval);
            if (haveCommandToEmit)
            {
                locker.unlock();
                emit haveCommand(item.command, item.param, item.receiver);
                locker.relock();
            }
        }
        else if (!aborted.load(std::memory_order_relaxed))
        {
            QQueue<CacheItem> pendingItems;
            QQueue<QString> pendingMessages;
            pendingItems.swap(items);
            pendingMessages.swap(messages);

            locker.unlock();
            if (!pendingItems.isEmpty())
            {
                QVector<CacheItem> batch;
                batch.reserve(pendingItems.size());
                while (!pendingItems.isEmpty())
                {
                    batch.append(pendingItems.dequeue());
                }
                emit sendValues(batch);
                if (receivers(SIGNAL(sendValue(CacheItem))) > 0)
                {
                    for (const CacheItem& item : batch)
                    {
                        emit sendValue(item);
                    }
                }
            }
            while (!pendingMessages.isEmpty())
            {
                emit sendMessage(pendingMessages.dequeue());
            }
            locker.relock();
            if (queueInterval != -1 && deadline.isForever())
            {
                deadline.setRemainingTime(queueInterval);
            }
        }
    }
}

Funcs CachingQueue::checkCommandAvailable(Funcs cmd, bool set) const
{
    Q_UNUSED(set)
    if (radioCaps != nullptr && cmd != funcNone && cmd != funcSelectVFO && !radioCaps->commands.contains(cmd))
    {
        return funcNone;
    }
    return cmd;
}

void CachingQueue::add(QueuePriority prio, Funcs func, bool recurring, uchar receiver)
{
    QueueItem q(func, recurring, receiver);
    add(prio, q, false);
}

void CachingQueue::addUnique(QueuePriority prio, Funcs func, bool recurring, uchar receiver)
{
    QueueItem q(func, recurring, receiver);
    add(prio, q, true);
}

void CachingQueue::addUnique(QueuePriority prio, QueueItem item)
{
    add(prio, item, true);
}

void CachingQueue::add(QueuePriority prio, QueueItem item, bool unique)
{
    QMutexLocker locker(&mutex);

    if (queueInterval == -1)
    {
        return;
    }

    item.command = checkCommandAvailable(item.command, item.param.isValid());
    if (item.command == funcNone)
    {
        return;
    }

    // Do not add a duplicate recurring command of the same priority.
    if (!item.recurring || isRecurring(item.command, item.receiver) != prio)
    {
        if (item.recurring && prio == kPriorityImmediate)
        {
            qWarning(logRadio()) << "CachingQueue::add() Warning, cannot add recurring command with immediate priority!"
                                 << funcString[item.command];
        }
        else
        {
            if (unique)
            {
                int count = queue.remove(prio, item);
                if (count > 0)
                {
                    qDebug(logRadio()) << "CachingQueue::add() deleted" << count << "entries from queue for"
                                       << funcString[item.command] << "on receiver" << item.receiver;
                }
            }

            // Don't immediately request funcTransceiverId; wait for the queue to run.
            if (item.recurring && item.command != funcTransceiverId)
            {
                QueueItem it = item;
                it.recurring = false;
                it.param.clear();
                queue.insert(queue.cend(), kPriorityImmediate, it);
            }
            queue.insert(prio, item);
        }
    }
}

VfoCommandType CachingQueue::getVfoCommand(vfo_t vfo, uchar rx, bool set)
{
    VfoCommandType cmd;
    cmd.receiver = rx;
    cmd.vfo = vfo;
    QMutexLocker locker(&mutex);
    if (radioCaps != nullptr)
    {
        if (set)
        {
            cmd.modeFunc = ((radioCaps->commands.contains(funcMode)) ? funcMode : funcModeSet);
            cmd.freqFunc = ((radioCaps->commands.contains(funcFreq)) ? funcFreq : funcFreqSet);
        }
        else
        {
            cmd.modeFunc = ((radioCaps->commands.contains(funcMode)) ? funcMode : funcModeGet);
            cmd.freqFunc = ((radioCaps->commands.contains(funcFreq)) ? funcFreq : funcFreqGet);
        }

        if (!radioCaps->hasCommand29)
        {
            // When CI-V command 29h is unavailable, selected/unselected commands address VFO A/B.
            if (radioState.vfoMode == vfoModeType_t::vfoModeVfo && (radioState.receiver == 0 && rx == 0))
            {
                if (vfo == vfoA)
                {
                    cmd.modeFunc = ((radioCaps->commands.contains(funcSelectedMode)) ? funcSelectedMode : cmd.modeFunc);
                    cmd.freqFunc = ((radioCaps->commands.contains(funcSelectedFreq)) ? funcSelectedFreq : cmd.freqFunc);
                }
                else if (vfo == vfoB)
                {
                    cmd.modeFunc =
                        ((radioCaps->commands.contains(funcUnselectedMode)) ? funcUnselectedMode : cmd.modeFunc);
                    cmd.freqFunc =
                        ((radioCaps->commands.contains(funcUnselectedFreq)) ? funcUnselectedFreq : cmd.freqFunc);
                }
            }
            else if (rx == radioState.receiver)
            {
                cmd.receiver = 0;
            }
            else
            {
                cmd.modeFunc = funcNone;
                cmd.freqFunc = funcNone;
                cmd.receiver = 0xff;
            }
        }
    }
    return cmd;
}

QueuePriority CachingQueue::del(Funcs func, uchar receiver)
{
    QueuePriority prio = kPriorityNone;
    if (func != funcNone)
    {
        QMutexLocker locker(&mutex);
        auto it = std::find_if(queue.begin(), queue.end(), [func, receiver](const QueueItem& c)
                               { return (c.command == func && c.receiver == receiver && c.recurring == true); });
        if (it != queue.end())
        {
            prio = it.key();
            int count = queue.remove(it.key(), it.value());
            if (count > 0)
            {
                qDebug(logRadio()) << "CachingQueue()::del" << count << "entries from queue for" << funcString[func]
                                   << "on receiver" << receiver;
            }
        }
    }
    return prio;
}

QueuePriority CachingQueue::isRecurring(Funcs func, uchar receiver)
{
    // Caller must already hold mutex when a consistent queue snapshot is required.
    auto rec = std::find_if(queue.begin(), queue.end(), [func, receiver](const QueueItem& c)
                            { return (c.command == func && c.receiver == receiver && c.recurring); });
    if (rec != queue.end())
    {
        return rec.key();
    }
    return kPriorityNone;
}

void CachingQueue::clear()
{
    QMutexLocker locker(&mutex);
    queue.clear();
}

void CachingQueue::resetSessionState()
{
    const radioCapabilities* previousCaps = nullptr;
    {
        QMutexLocker locker(&mutex);
        previousCaps = radioCaps;
        radioCaps = nullptr;
        queue.clear();
        cache.clear();
        items.clear();
        messages.clear();
        radioState = RadioStateType();
    }

    if (previousCaps != nullptr)
    {
        emit radioCapsUpdated(nullptr);
    }
    waiting.wakeAll();
}

void CachingQueue::setRadioCaps(radioCapabilities* caps)
{
    bool changed = false;
    {
        QMutexLocker locker(&mutex);
        if (radioCaps != caps)
        {
            radioCaps = caps;
            changed = true;
        }
    }

    if (changed)
    {
        emit radioCapsUpdated(caps);
    }
}

void CachingQueue::message(QString msg)
{
    {
        QMutexLocker locker(&mutex);
        messages.append(msg);
    }
    qDebug(logRadio()) << "Received:" << msg;
    waiting.wakeOne();
}

void CachingQueue::receiveValue(Funcs func, QVariant value, uchar receiver)
{
    {
        // Parsed CI-V replies are authoritative state. Wait for the queue lock
        // instead of dropping an update during heavy polling or memory sync.
        QMutexLocker locker(&mutex);
        CacheItem c = CacheItem(func, value, receiver);
        items.enqueue(c);
        updateCache(true, func, value, receiver);
    }
    waiting.wakeOne();
}

void CachingQueue::updateCache(bool reply, QueueItem item)
{
    // Caller must hold mutex.

    if (reply)
    {
        // Track radio state changes that affect later command routing.
        if (item.command == funcSatelliteMode && item.param.value<bool>())
        {
            radioState.vfoMode = vfoModeType_t::vfoModeSat;
        }
        if (item.command == funcMemoryMode && (!item.param.isValid() || item.param.value<bool>()))
        {
            radioState.vfoMode = vfoModeType_t::vfoModeMem;
        }
        if (item.command == funcVFOModeSelect || (item.command == funcVFOMode && item.param.value<bool>()))
        {
            radioState.vfoMode = vfoModeType_t::vfoModeVfo;
        }
        if (item.command == funcVFOBandMS)
        {
            radioState.receiver = item.param.toBool() ? 1 : 0;
        }
    }
    else
    {
        // Some VFO selection commands do not produce a useful confirmation.
        if (item.command == funcSelectVFO)
        {
            radioState.vfo = item.param.value<vfo_t>();
            if (radioState.vfo == vfoMain)
            {
                radioState.receiver = 0;
            }
            else if (radioState.vfo == vfoSub && radioCaps != nullptr && radioCaps->numReceiver > 1)
            {
                radioState.receiver = 1;
            }
        }
        else if (item.command == funcVFOASelect)
        {
            radioState.vfo = vfo_t::vfoA;
        }
        else if (item.command == funcVFOBSelect && radioCaps != nullptr && radioCaps->numVFO > 1)
        {
            radioState.vfo = vfo_t::vfoB;
        }
        else if (item.command == funcVFOMainSelect)
        {
            radioState.vfo = vfo_t::vfoMain;
            radioState.receiver = 0;
        }
        else if (item.command == funcVFOSubSelect && radioCaps != nullptr && radioCaps->numReceiver > 1)
        {
            radioState.vfo = vfo_t::vfoSub;
            radioState.receiver = 1;
        }
        else if (item.command == funcVFOBandMS)
        {
            radioState.receiver = item.param.toBool() ? 1 : 0;
        }
    }

    auto cv = cache.find(item.command);
    while (cv != cache.end() && cv->command == item.command)
    {
        if (cv->receiver == item.receiver)
        {
            if (reply)
            {
                cv->reply = QDateTime::currentDateTime();
            }
            else
            {
                cv->req = QDateTime::currentDateTime();
            }
            if (compare(item.param, cv.value().value))
            {
                cv->value.clear();
                cv->value.setValue(item.param);

                emit cacheUpdated(cv.value());
            }
            return;
        }
        ++cv;
    }

    CacheItem c;
    c.command = item.command;
    c.receiver = item.receiver;

    if (reply)
    {
        c.reply = QDateTime::currentDateTime();
    }
    else
    {
        c.req = QDateTime::currentDateTime();
    }
    if (item.param.isValid())
    {
        c.value.setValue(item.param);
    }

    cache.insert(item.command, c);
}

void CachingQueue::updateCache(bool reply, Funcs func, QVariant value, uchar receiver)
{
    QueueItem q(func, value, false, receiver);
    updateCache(reply, q);
}

void CachingQueue::recordLocalRoutingState(Funcs func, QVariant value, uchar receiver)
{
    QMutexLocker locker(&mutex);
    updateCache(false, QueueItem(func, value, false, receiver));
}

CacheItem CachingQueue::getCache(Funcs func, uchar receiver)
{
    CacheItem ret;
    if (func != funcNone)
    {
        QMutexLocker locker(&mutex);
        auto it = cache.find(func);
        while (it != cache.end() && it->command == func)
        {
            if (it->receiver == receiver)
            {
                ret = CacheItem(*it);
                break;
            }
            ++it;
        }
    }
    // Re-request stale values after a small randomized window so periodic
    // refresh traffic does not synchronize into bursts. Keep this below
    // kPriorityHighest; raising it makes the S-meter visibly lag while command
    // intensive workflows are active.
    if (func != funcNone && func != funcPowerControl && func != funcSelectVFO &&
        (!ret.value.isValid() || ret.command == funcSWRMeter || ret.command == funcALCMeter ||
         ret.reply.addSecs(QRandomGenerator::global()->bounded(5, 20)) <= QDateTime::currentDateTime()))
    {
        qDebug(logRadio()) << "No (or expired) cache found for" << funcString[func] << "requesting" << ret.reply;
        add(kPriorityImmediate, func, false, receiver);
    }
    return ret;
}

bool CachingQueue::compare(QVariant a, QVariant b)
{
    bool changed = false;

    if (a.isValid() && b.isValid())
    {
        const int ValueType = a.userType();
        const auto valueHolds = [ValueType](int type) { return ValueType == type; };
        if (ValueType != b.userType())
        {
            return true;
        }

        if (valueHolds(qMetaTypeId<bool>()))
        {
            if (a.value<bool>() != b.value<bool>())
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<QString>()))
        {
            if (a.value<QString>() != b.value<QString>())
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<uchar>()))
        {
            if (a.value<uchar>() != b.value<uchar>())
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<ushort>()))
        {
            if (a.value<ushort>() != b.value<ushort>())
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<short>()))
        {
            if (a.value<short>() != b.value<short>())
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<uint>()))
        {
            if (a.value<uint>() != b.value<uint>())
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<int>()))
        {
            if (a.value<int>() != b.value<int>())
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<double>()))
        {
            if (a.value<double>() != b.value<double>())
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<ModeInfo>()))
        {
            if (a.value<ModeInfo>().mk != b.value<ModeInfo>().mk ||
                a.value<ModeInfo>().reg != b.value<ModeInfo>().reg ||
                a.value<ModeInfo>().filter != b.value<ModeInfo>().filter ||
                a.value<ModeInfo>().data != b.value<ModeInfo>().data)
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<Frequency>()))
        {
            if (a.value<Frequency>().Hz != b.value<Frequency>().Hz)
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<AntennaInfo>()))
        {
            if (a.value<AntennaInfo>().antenna != b.value<AntennaInfo>().antenna ||
                a.value<AntennaInfo>().rx != b.value<AntennaInfo>().rx)
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<radioInput>()))
        {
            if (a.value<radioInput>().type != b.value<radioInput>().type)
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<duplexMode_t>()))
        {
            if (a.value<duplexMode_t>() != b.value<duplexMode_t>())
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<ToneInfo>()))
        {
            if (a.value<ToneInfo>().tone != b.value<ToneInfo>().tone)
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<meter_t>()))
        {
            if (a.value<meter_t>() != b.value<meter_t>())
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<vfo_t>()))
        {
            if (a.value<vfo_t>() != b.value<vfo_t>())
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<LpfHpf>()))
        {
            if (a.value<LpfHpf>().lpf != b.value<LpfHpf>().lpf || a.value<LpfHpf>().hpf != b.value<LpfHpf>().hpf)
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<SpectrumBounds>()))
        {
            if (a.value<SpectrumBounds>().edge != b.value<SpectrumBounds>().edge ||
                a.value<SpectrumBounds>().start != b.value<SpectrumBounds>().start ||
                a.value<SpectrumBounds>().end != b.value<SpectrumBounds>().end)
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<centerSpanData>()))
        {
            if (a.value<centerSpanData>().reg != b.value<centerSpanData>().reg ||
                a.value<centerSpanData>().freq != b.value<centerSpanData>().freq)
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<RptrAccessData>()))
        {
            if (a.value<RptrAccessData>().accessMode != b.value<RptrAccessData>().accessMode ||
                a.value<RptrAccessData>().turnOffTSQL != b.value<RptrAccessData>().turnOffTSQL ||
                a.value<RptrAccessData>().turnOffTone != b.value<RptrAccessData>().turnOffTone)
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<ScopeData>()) || valueHolds(qMetaTypeId<MemoryType>()) ||
                 valueHolds(qMetaTypeId<TimeKind>()) || valueHolds(qMetaTypeId<DateKind>()) ||
                 valueHolds(qMetaTypeId<MeterKind>()) || valueHolds(qMetaTypeId<UdpConnectionSettings>()))
        {
            changed = true;
        }
        else
        {
            qInfo(logRadio()) << "Unsupported cache value:" << a.typeName();
        }
    }
    else if (a.isValid())
    {
        changed = true;
    }

    return changed;
}
