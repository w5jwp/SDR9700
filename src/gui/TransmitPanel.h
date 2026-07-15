#pragma once

#include <QGroupBox>

class QPushButton;

class TransmitPanel : public QGroupBox
{
    Q_OBJECT

  public:
    struct Buttons
    {
        QPushButton* compressor{nullptr};
    };

    explicit TransmitPanel(const Buttons& buttons, QWidget* parent = nullptr);
};
