#pragma once

#include "Types.h"

#include <QObject>
#include <QVector>

class MemoryController;

class MemoryCsvController : public QObject
{
    Q_OBJECT

  public:
    explicit MemoryCsvController(MemoryController* owner);

    bool exportRadioMemories();
    void importRadioMemories();

  private:
    void restoreRadioMemoriesAfterFailedImport(const QVector<MemoryType>& backup);

    MemoryController* m_owner{nullptr};
};
