#pragma once

#include <QGroupBox>

class QPushButton;

class PttWidget : public QGroupBox
{
    Q_OBJECT

  public:
    explicit PttWidget(QPushButton* pttButton, QPushButton* dtmfButton = nullptr, QWidget* parent = nullptr);
};
