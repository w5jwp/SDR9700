// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QWidget>

class MemoryManagerSettingsPanel : public QWidget
{
    Q_OBJECT

  public:
    explicit MemoryManagerSettingsPanel(QWidget* parent = nullptr);

  signals:
    void pollIntervalSecondsChanged(int seconds);
};
