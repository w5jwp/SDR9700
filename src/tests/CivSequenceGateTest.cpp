// cppcheck-suppress-file unusedFunction
#include "CivSequenceGate.h"

#include <QtTest>

class CivSequenceGateTest : public QObject
{
    Q_OBJECT

  private slots:
    void deliversInOrder();
    void suppressesDuplicates();
    void deliversOutOfOrderWithoutBlocking();
    void handlesRollover();
    void boundsDuplicateHistory();
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

QTEST_GUILESS_MAIN(CivSequenceGateTest)
#include "CivSequenceGateTest.moc"
