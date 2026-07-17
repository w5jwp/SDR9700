// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QWidget>

class QSpinBox;

class MemoryManagerSettingsPanel : public QWidget
{
    Q_OBJECT

  public:
    explicit MemoryManagerSettingsPanel(QWidget* parent = nullptr);

  signals:
    void pollIntervalSecondsChanged(int seconds);

  private:
    QSpinBox* m_pollIntervalSpin{nullptr};
};
