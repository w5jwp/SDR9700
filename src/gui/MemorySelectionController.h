#pragma once

#include <QObject>
#include <QString>

#include "MemoryStore.h"

class MemoryController;

class MemorySelectionController : public QObject
{
    Q_OBJECT

  public:
    explicit MemorySelectionController(MemoryController* owner);

    QString selectedMemoryId() const;
    void selectCheckedMemory();
    void selectMemoryById(const QString& id, bool showDialogOnFailure);
    void copySelectedMemory();
    void removeSelectedMemory();
    void moveSelectedMemory(int direction);
    void applyMemoryToVfo(const MemoryRecord& memory);

  private:
    MemoryController* m_owner{nullptr};
};
