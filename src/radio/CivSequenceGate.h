#pragma once

#include <QByteArray>
#include <QQueue>
#include <QSet>
#include <QVector>

struct CivSequenceGateDiagnostics
{
    quint64 delivered{0};
    quint64 duplicatesSuppressed{0};
    quint64 reordered{0};
    quint64 discontinuities{0};
    quint64 missingSequences{0};
    qsizetype buffered{0};
    qsizetype highWaterMark{0};
};

struct CivSequenceGateResult
{
    QVector<QByteArray> payloads;
    bool discontinuity{false};
    quint16 firstMissing{0};
    quint16 lastMissing{0};
};

class CivSequenceGate
{
  public:
    static constexpr qsizetype kRecentSequenceWindow = 512;

    CivSequenceGateResult accept(quint16 sequence, const QByteArray& payload, qint64 receivedAtMs);
    void reset();
    const CivSequenceGateDiagnostics& diagnostics() const;

  private:
    static qint16 distance(quint16 from, quint16 to);

    bool m_started{false};
    quint16 m_expected{0};
    QQueue<quint16> m_recentSequenceOrder;
    QSet<quint16> m_recentSequences;
    CivSequenceGateDiagnostics m_diagnostics;
};
