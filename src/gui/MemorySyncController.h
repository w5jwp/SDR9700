#pragma once

#include <QObject>

class MemoryController;

class MemorySyncController : public QObject
{
    Q_OBJECT

  public:
    explicit MemorySyncController(MemoryController* owner);

    void forceRadioMemorySync();
    void setMemoryPollIntervalSeconds(int seconds);

  private:
    MemoryController* m_owner{nullptr};
};
