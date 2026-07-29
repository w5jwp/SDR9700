#pragma once

#include "Types.h"

#include <QObject>
#include <QString>
#include <QVector>
#include <functional>

class MemoryController;
class QTimer;

class MemoryWriteController : public QObject
{
    Q_OBJECT

  public:
    using Completion = std::function<void(bool success)>;

    explicit MemoryWriteController(MemoryController* owner);

    void queueWrites(const QVector<MemoryType>& memories, int startDelayMs, const QString& progressLabel,
                     Completion completion);
    void handleReadback(quint32 key, const MemoryType& memory);
    bool active() const;

  private:
    void startWrites(const QVector<MemoryType>& memories, const QString& progressLabel, Completion completion);
    void writeNext();
    void requestExpectedReadback();
    void finish(bool failed);

    MemoryController* m_owner{nullptr};
    QTimer* m_timeoutTimer{nullptr};
    QTimer* m_readbackRetryTimer{nullptr};
    QVector<MemoryType> m_memories;
    int m_index{0};
    quint32 m_expectedKey{0};
    bool m_waitingForReadback{false};
    QString m_progressLabel;
    Completion m_completion;
};
