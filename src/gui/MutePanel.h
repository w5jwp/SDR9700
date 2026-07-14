#pragma once

#include <QGroupBox>

class QPushButton;

class MutePanel : public QGroupBox
{
    Q_OBJECT

  public:
    explicit MutePanel(QPushButton* muteButton, QWidget* parent = nullptr);
};
