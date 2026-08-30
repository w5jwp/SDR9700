#pragma once

#include "Types.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QUuid>
#include <QVector>

struct MemoryDatabaseSyncState
{
    QDateTime completedAt;
    int expectedSlotCount{0};
    int receivedSlotCount{0};
    bool complete{false};
};

// MemoryDatabase is a durable mirror of memory-slot replies received from a
// particular configured radio. Its versioned payload preserves every field in
// MemoryType, which covers the IC-9700 CI-V 1A 00 normal-memory and 1A 07
// satellite-memory layouts: frequency/mode/filter/data mode, duplex and tone
// configuration, digital squelch and DTCS, offsets, D-STAR callsigns, names,
// and both sides of a satellite memory. It is deliberately not an independent
// source of radio configuration: callers may use its records for immediate
// display, but only a current-session radio reply can prove that a slot is
// occupied or empty and only the existing write/readback workflow can confirm
// a mutation.
class MemoryDatabase
{
  public:
    explicit MemoryDatabase(QString path = {});
    ~MemoryDatabase();

    MemoryDatabase(const MemoryDatabase&) = delete;
    MemoryDatabase& operator=(const MemoryDatabase&) = delete;

    bool open(QString* error = nullptr);
    bool isOpen() const;

    QVector<MemoryType> memories(const QUuid& profileId, QString* error = nullptr) const;
    bool store(const QUuid& profileId, const MemoryType& memory, QString* error = nullptr);
    bool remove(const QUuid& profileId, quint16 group, quint16 channel, QString* error = nullptr);
    bool applySyncSnapshot(const QUuid& profileId, const QVector<MemoryType>& replies, int expectedSlotCount,
                           QString* error = nullptr);
    MemoryDatabaseSyncState syncState(const QUuid& profileId, QString* error = nullptr) const;
    bool removeProfile(const QUuid& profileId, QString* error = nullptr);

  private:
    bool ensureSchema(QString* error);

    QString m_path;
    QString m_connectionName;
    QSqlDatabase m_database;
};
