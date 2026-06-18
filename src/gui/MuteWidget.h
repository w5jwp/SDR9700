#pragma once

#include <QGroupBox>

class QPushButton;

class MuteWidget : public QGroupBox
{
    Q_OBJECT

  public:
    explicit MuteWidget(QPushButton* muteButton, QWidget* parent = nullptr);
};
