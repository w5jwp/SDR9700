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
    QVector<Funcs> dispatched;
    connect(queue, &CachingQueue::haveCommand, this,
            [&dispatched](Funcs command, const QVariant&, uchar) { dispatched.append(command); });

    queue->add(kPriorityImmediate, funcFreqGet);
    queue->add(kPriorityImmediate, funcModeGet);

    QTRY_COMPARE(dispatched.size(), 2);
    QCOMPARE(dispatched.at(0), funcFreqGet);
    QCOMPARE(dispatched.at(1), funcModeGet);
}

void CachingQueueTest::receivesAndCachesAuthoritativeValues()
{
    CachingQueue* queue = CachingQueue::getInstance();
    QVector<CacheItem> delivered;
    connect(queue, &CachingQueue::sendValues, this,
            [&delivered](const QVector<CacheItem>& values) { delivered += values; });

    queue->receiveValue(funcRfGain, 123, 0);

    QTRY_COMPARE(delivered.size(), 1);
    QCOMPARE(delivered.constFirst().command, funcRfGain);
    QCOMPARE(delivered.constFirst().value.toInt(), 123);
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

QTEST_GUILESS_MAIN(CachingQueueTest)
#include "CachingQueueTest.moc"
