#pragma once

#include <QGroupBox>

class QPushButton;

class RepeaterPanel : public QGroupBox
{
    Q_OBJECT

  public:
    struct Buttons
    {
        QPushButton* offset{nullptr};
        QPushButton* tone{nullptr};
    };

    explicit RepeaterPanel(const Buttons& buttons, QWidget* parent = nullptr);
};
