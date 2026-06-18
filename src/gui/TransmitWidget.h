#pragma once

#include <QGroupBox>

class QPushButton;

class TransmitWidget : public QGroupBox
{
    Q_OBJECT

  public:
    struct Buttons
    {
        QPushButton* compressor{nullptr};
        QPushButton* micGain{nullptr};
    };

    explicit TransmitWidget(const Buttons& buttons, QWidget* parent = nullptr);
};
