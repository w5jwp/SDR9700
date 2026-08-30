// cppcheck-suppress-file unusedFunction
#include "CivSequenceGate.h"

#include <QtTest>
#include <algorithm>
#include <random>

class CivSequenceGateTest : public QObject
{
    Q_OBJECT

  private slots:
    void deliversInOrder();
    void suppressesDuplicates();
    void deliversOutOfOrderWithoutBlocking();
    void handlesRollover();
    void boundsDuplicateHistory();
    void survivesSeededLossDuplicateAndReorderSoak();
};

void CivSequenceGateTest::deliversInOrder()
{
    CivSequenceGate gate;
    QCOMPARE(gate.accept(10, QByteArrayLiteral("a"), 0).payloads, QVector<QByteArray>({QByteArrayLiteral("a")}));
    QCOMPARE(gate.accept(11, QByteArrayLiteral("b"), 1).payloads, QVector<QByteArray>({QByteArrayLiteral("b")}));
}

void CivSequenceGateTest::suppressesDuplicates()
{
    CivSequenceGate gate;
    QCOMPARE(gate.accept(10, QByteArrayLiteral("a"), 0).payloads.size(), 1);
    QVERIFY(gate.accept(10, QByteArrayLiteral("a"), 1).payloads.isEmpty());
    QCOMPARE(gate.diagnostics().duplicatesSuppressed, quint64(1));
}

void CivSequenceGateTest::deliversOutOfOrderWithoutBlocking()
{
    CivSequenceGate gate;
    QCOMPARE(gate.accept(10, QByteArrayLiteral("a"), 0).payloads.size(), 1);
    QCOMPARE(gate.accept(12, QByteArrayLiteral("c"), 1).payloads, QVector<QByteArray>({QByteArrayLiteral("c")}));
    QCOMPARE(gate.accept(11, QByteArrayLiteral("b"), 2).payloads, QVector<QByteArray>({QByteArrayLiteral("b")}));
    QCOMPARE(gate.diagnostics().reordered, quint64(1));
}

void CivSequenceGateTest::handlesRollover()
{
    CivSequenceGate gate;
    QCOMPARE(gate.accept(0xffff, QByteArrayLiteral("a"), 0).payloads.size(), 1);
    QCOMPARE(gate.accept(0, QByteArrayLiteral("b"), 1).payloads.size(), 1);
    QCOMPARE(gate.accept(1, QByteArrayLiteral("c"), 2).payloads.size(), 1);
}

void CivSequenceGateTest::boundsDuplicateHistory()
{
    CivSequenceGate gate;
    for (qsizetype sequence = 0; sequence <= CivSequenceGate::kRecentSequenceWindow; ++sequence)
    {
        QCOMPARE(gate.accept(static_cast<quint16>(sequence), QByteArrayLiteral("data"), sequence).payloads.size(), 1);
    }
    QCOMPARE(gate.diagnostics().highWaterMark, CivSequenceGate::kRecentSequenceWindow);
    QCOMPARE(gate.accept(0, QByteArrayLiteral("new rollover"), 1000).payloads.size(), 1);
}

void CivSequenceGateTest::survivesSeededLossDuplicateAndReorderSoak()
{
    CivSequenceGate gate;
    std::mt19937 random(0x9700);
    QSet<quint16> expectedSequences;
    QSet<quint16> deliveredSequences;
    quint64 injectedDuplicates = 0;

    struct Datagram
    {
        quint16 sequence{0};
        QByteArray payload;
    };

    constexpr int kDatagramCount = 20000;
    constexpr int kBatchSize = 8;
    for (int batchStart = 0; batchStart < kDatagramCount; batchStart += kBatchSize)
    {
        QVector<Datagram> arrivals;
        for (int offset = 0; offset < kBatchSize && batchStart + offset < kDatagramCount; ++offset)
        {
            const int logicalIndex = batchStart + offset;
            const quint16 sequence = static_cast<quint16>(logicalIndex);
            if (logicalIndex % 23 == 0)
            {
                continue;
            }

            const QByteArray payload = QByteArray::number(sequence);
            expectedSequences.insert(sequence);
            arrivals.append({sequence, payload});
            if (logicalIndex % 7 == 0)
            {
                arrivals.append({sequence, payload});
                ++injectedDuplicates;
            }
        }

        std::shuffle(arrivals.begin(), arrivals.end(), random);
        for (const Datagram& datagram : arrivals)
        {
            const CivSequenceGateResult result = gate.accept(datagram.sequence, datagram.payload, batchStart);
            for (const QByteArray& payload : result.payloads)
            {
                const quint16 delivered = payload.toUShort();
                QVERIFY2(!deliveredSequences.contains(delivered), "CI-V sequence was delivered more than once");
                deliveredSequences.insert(delivered);
            }
        }
    }

    QCOMPARE(deliveredSequences, expectedSequences);
    QCOMPARE(gate.diagnostics().delivered, quint64(expectedSequences.size()));
    QCOMPARE(gate.diagnostics().duplicatesSuppressed, injectedDuplicates);
    QVERIFY(gate.diagnostics().reordered > 0);
    QVERIFY(gate.diagnostics().highWaterMark <= CivSequenceGate::kRecentSequenceWindow);
}

QTEST_GUILESS_MAIN(CivSequenceGateTest)
#include "CivSequenceGateTest.moc"
