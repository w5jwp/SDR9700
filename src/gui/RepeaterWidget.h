#pragma once

#include <QGroupBox>

class QPushButton;

class RepeaterWidget : public QGroupBox
{
    Q_OBJECT

  public:
    struct Buttons
    {
        QPushButton* offset{nullptr};
        QPushButton* tone{nullptr};
    };

    explicit RepeaterWidget(const Buttons& buttons, QWidget* parent = nullptr);
};
