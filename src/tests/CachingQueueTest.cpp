// QtTest invokes private slots through the generated meta-object.
#include "CachingQueue.h"

#include <QtTest>

struct UnsupportedCachePayload
{
    int value{0};
};
Q_DECLARE_METATYPE(UnsupportedCachePayload)

class CachingQueueTest : public QObject
{
    Q_OBJECT

  private slots:
    void init();
    void cleanupTestCase();
    void mapsPriorityNames();
    void sharedQueueHasDiagnosticName();
    void queueItemsHaveStableIdentity();
    void dispatchesImmediateCommandsInOrder();
    void receivesAndCachesAuthoritativeValues();
    void comparesRegisteredValueTypes();
    void deliversUnsupportedPayloadTypesConservatively();
    void resetClearsSessionState();
    void vfoBandReadDoesNotChangeRoutingState();
    void emitsCacheChangesWithoutHoldingMutex();
    void restartsAfterExplicitShutdown();
    void reportsQueueDiagnostics();
    void deduplicatesRepeatedCacheRefreshes();
    void boundsCommandQueue();
    void recurringWorkSurvivesImmediatePressure();
};

void CachingQueueTest::init()
{
    CachingQueue::getInstance()->resetSessionState();
}

void CachingQueueTest::cleanupTestCase()
{
    CachingQueue::shutdownInstance();
}

void CachingQueueTest::mapsPriorityNames()
{
    QCOMPARE(priorityValue(QStringLiteral("Immediate")), int(kPriorityImmediate));
    QCOMPARE(priorityValue(QStringLiteral("Medium High")), int(kPriorityMediumHigh));
    QCOMPARE(priorityValue(QStringLiteral("Lowest")), int(kPriorityLowest));
    QCOMPARE(priorityValue(QStringLiteral("invalid")), int(kPriorityNone));
}

void CachingQueueTest::sharedQueueHasDiagnosticName()
{
    QCOMPARE(CachingQueue::getInstance()->objectName(), QStringLiteral("CachingQueue()"));
}

void CachingQueueTest::queueItemsHaveStableIdentity()
{
    const QueueItem first(funcFreqGet, QVariant());
    const QueueItem copy(first);
    const QueueItem second(funcFreqGet, QVariant());

    QCOMPARE(copy.id, first.id);
    QVERIFY(second.id > first.id);
    QCOMPARE(copy, first);
}

void CachingQueueTest::dispatchesImmediateCommandsInOrder()
{
    CachingQueue* queue = CachingQueue::getInstance();
    QSignalSpy dispatched(queue, &CachingQueue::haveCommand);

    queue->add(kPriorityImmediate, funcFreqGet);
    queue->add(kPriorityImmediate, funcModeGet);

    QTRY_COMPARE(dispatched.size(), 2);
    QCOMPARE(dispatched.at(0).at(0).value<Funcs>(), funcFreqGet);
    QCOMPARE(dispatched.at(1).at(0).value<Funcs>(), funcModeGet);
}

void CachingQueueTest::receivesAndCachesAuthoritativeValues()
{
    CachingQueue* queue = CachingQueue::getInstance();
    QSignalSpy delivered(queue, &CachingQueue::sendValues);

    queue->receiveValue(funcRfGain, 123, 0);

    QTRY_COMPARE(delivered.size(), 1);
    const CacheItem cached = queue->getCache(funcRfGain, 0);
    QCOMPARE(cached.command, funcRfGain);
    QCOMPARE(cached.value.toInt(), 123);
    QVERIFY(cached.reply.isValid());
}

void CachingQueueTest::deliversUnsupportedPayloadTypesConservatively()
{
    const QVariant payload = QVariant::fromValue(UnsupportedCachePayload{42});
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("Unsupported cache value.*")));
    QVERIFY(CachingQueue::cacheValuesDiffer(payload, payload));
}

void CachingQueueTest::comparesRegisteredValueTypes()
{
    QVERIFY(!CachingQueue::cacheValuesDiffer(123, 123));
    QVERIFY(CachingQueue::cacheValuesDiffer(123, 124));
    QVERIFY(!CachingQueue::cacheValuesDiffer(QStringLiteral("same"), QStringLiteral("same")));
    QVERIFY(CachingQueue::cacheValuesDiffer(QStringLiteral("before"), QStringLiteral("after")));
    QVERIFY(!CachingQueue::cacheValuesDiffer(QByteArray("same"), QByteArray("same")));
}

void CachingQueueTest::resetClearsSessionState()
{
    CachingQueue* queue = CachingQueue::getInstance();
    queue->recordLocalRoutingState(funcVFOBandMS, true, 0);
    QCOMPARE(queue->getState().receiver, uchar(1));

    queue->resetSessionState();

    QCOMPARE(queue->getState().receiver, uchar(0));
    const CacheItem cached = queue->getCache(funcVFOBandMS, 0);
    QVERIFY(!cached.value.isValid());
}

void CachingQueueTest::vfoBandReadDoesNotChangeRoutingState()
{
    CachingQueue* queue = CachingQueue::getInstance();
    QSignalSpy dispatched(queue, &CachingQueue::haveCommand);
    queue->recordLocalRoutingState(funcVFOBandMS, true, 0);

    queue->add(kPriorityImmediate, funcVFOBandMS);

    QTRY_COMPARE(dispatched.size(), 1);
    QCOMPARE(queue->getState().receiver, uchar(1));
}

void CachingQueueTest::emitsCacheChangesWithoutHoldingMutex()
{
    CachingQueue* queue = CachingQueue::getInstance();
    queue->receiveValue(funcRfGain, 100, 0);

    bool signalObserved = false;
    const QMetaObject::Connection connection = connect(
        queue, &CachingQueue::cacheUpdated, this,
        [queue, &signalObserved](const CacheItem&)
        {
            // A direct callback can safely re-enter the queue only when the
            // signal is emitted after updateCache releases its mutex.
            QCOMPARE(queue->getCache(funcRfGain, 0).value.toInt(), 101);
            signalObserved = true;
        },
        Qt::DirectConnection);

    queue->receiveValue(funcRfGain, 101, 0);
    disconnect(connection);
    QVERIFY(signalObserved);
}

void CachingQueueTest::restartsAfterExplicitShutdown()
{
    CachingQueue::shutdownInstance();
    CachingQueue* queue = CachingQueue::getInstance();
    QSignalSpy dispatched(queue, &CachingQueue::haveCommand);

    queue->add(kPriorityImmediate, funcFreqGet);

    QTRY_COMPARE(dispatched.size(), 1);
}

void CachingQueueTest::reportsQueueDiagnostics()
{
    CachingQueue* queue = CachingQueue::getInstance();
    queue->add(kPriorityLowest, funcFreqGet, true);
    queue->add(kPriorityLowest, funcModeGet, true);

    QTRY_VERIFY(queue->diagnostics().highWaterMark >= 2);
    const CachingQueueDiagnostics diagnostics = queue->diagnostics();
    QVERIFY(diagnostics.depth >= 2);
    QCOMPARE(diagnostics.depthByPriority.value(kPriorityLowest), qsizetype(2));
    QCOMPARE(diagnostics.depthByPriority.value(kPriorityImmediate), diagnostics.depth - 2);
    QVERIFY(diagnostics.oldestItemAgeMs >= 0);
}

void CachingQueueTest::deduplicatesRepeatedCacheRefreshes()
{
    // Keep this as a five-digit request burst so the test exercises sustained
    // duplicate pressure, not only a handful of coincident callers.
    constexpr int kRefreshRequestCount = 10000;
    CachingQueue* queue = CachingQueue::getInstance();
    const quint64 dispatchedBefore = queue->diagnostics().dispatched;

    for (int i = 0; i < kRefreshRequestCount; ++i)
    {
        queue->getCache(funcRfGain, 0);
    }

    QTRY_COMPARE(queue->diagnostics().dispatched, dispatchedBefore + 1);
    QTest::qWait(100);
    QCOMPARE(queue->diagnostics().dispatched, dispatchedBefore + 1);
}

void CachingQueueTest::boundsCommandQueue()
{
    CachingQueue* queue = CachingQueue::getInstance();
    for (int i = 0; i < 700; ++i)
    {
        queue->add(kPriorityLowest, QueueItem(funcRfGain, i, false, 0));
    }

    const CachingQueueDiagnostics diagnostics = queue->diagnostics();
    QVERIFY(diagnostics.depth <= 512);
    QVERIFY(diagnostics.droppedForCapacity > 0);
}

void CachingQueueTest::recurringWorkSurvivesImmediatePressure()
{
    CachingQueue* queue = CachingQueue::getInstance();
    QSignalSpy dispatched(queue, &CachingQueue::haveCommand);

    {
        std::lock_guard locker(queue->mutex);
        queue->queue.insert(kPriorityHighest, QueueItem(funcModeGet, true, 0));
        queue->queue.insert(kPriorityLowest, QueueItem(funcTransceiverId, true, 0));
        for (int i = 0; i < 40; ++i)
        {
            queue->queue.insert(kPriorityImmediate, QueueItem(funcRfGain, i, false, 0));
        }
        queue->m_queueWakeRequested = true;
    }
    queue->waiting.notify_one();

    QTRY_VERIFY(dispatched.size() >= 18);
    int highestIndex = -1;
    int lowestIndex = -1;
    for (int i = 0; i < dispatched.size(); ++i)
    {
        if (dispatched.at(i).at(0).value<Funcs>() == funcModeGet && highestIndex < 0)
        {
            highestIndex = i;
        }
        if (dispatched.at(i).at(0).value<Funcs>() == funcTransceiverId && lowestIndex < 0)
        {
            lowestIndex = i;
        }
    }
    QVERIFY(highestIndex >= 0);
    QVERIFY(lowestIndex >= 0);
    QVERIFY(highestIndex <= int(CachingQueue::kMaximumImmediateBurst));
    QVERIFY(lowestIndex <= int(CachingQueue::kMaximumImmediateBurst * 2 + 1));
}

QTEST_GUILESS_MAIN(CachingQueueTest)
#include "CachingQueueTest.moc"
