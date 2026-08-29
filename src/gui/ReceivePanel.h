#pragma once

#include <QGroupBox>

class QPushButton;

class ReceivePanel : public QGroupBox
{
    Q_OBJECT

  public:
    struct Buttons
    {
        QPushButton* compressor{nullptr};
        QPushButton* offset{nullptr};
        QPushButton* rit{nullptr};
        QPushButton* tone{nullptr};
        QPushButton* xfc{nullptr};
    };

    explicit ReceivePanel(const Buttons& buttons, QWidget* parent = nullptr);
};
