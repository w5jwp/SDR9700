#pragma once

#include <QGroupBox>

class QPushButton;

class PttPanel : public QGroupBox
{
    Q_OBJECT

  public:
    explicit PttPanel(QPushButton* pttButton, QPushButton* dtmfButton = nullptr, QWidget* parent = nullptr);
};
