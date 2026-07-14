#pragma once

#include <QWidget>

class QCheckBox;

class MouseSettingsPanel : public QWidget
{
    Q_OBJECT

  public:
    explicit MouseSettingsPanel(QWidget* parent = nullptr);

  signals:
    void reverseMouseWheelTuningChanged(bool reversed);

  private:
    QCheckBox* m_invertPanadapterMouseWheelCheck{nullptr};
};
