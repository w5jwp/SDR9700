#pragma once

#include <QObject>
#include <QString>

class MemoryController;
class QTimer;

class MemoryViewController : public QObject
{
    Q_OBJECT

  public:
    explicit MemoryViewController(MemoryController* owner);

    void buildMemoryWindow();
    void showMemoryWindow();
    void scheduleRebuild();
    void rebuild();
    void setProgress(const QString& label, int value, int maximum);
    void clearProgress();
    void setShowSpecialMemories(bool show);
    void setShowSatelliteMemories(bool show);
    void updateTableInteraction();
    void stopScheduledRefresh();
    bool operationInProgress() const;

  private:
    MemoryController* m_owner{nullptr};
    QTimer* m_refreshTimer{nullptr};
    QString m_progressLabel;
    int m_progressValue{0};
    int m_progressMaximum{0};
    bool m_showSpecialMemories{false};
    bool m_showSatelliteMemories{false};
};
