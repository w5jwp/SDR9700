#pragma once

#include <QGroupBox>

class QPushButton;

class ReceivePanel : public QGroupBox
{
    Q_OBJECT

  public:
    struct Buttons
    {
        QPushButton* agc{nullptr};
        QPushButton* attenuator{nullptr};
        QPushButton* compressor{nullptr};
        QPushButton* noiseBlanker{nullptr};
        QPushButton* notch{nullptr};
        QPushButton* noiseReduction{nullptr};
        QPushButton* offset{nullptr};
        QPushButton* preamp{nullptr};
        QPushButton* rfGain{nullptr};
        QPushButton* rit{nullptr};
        QPushButton* tone{nullptr};
        QPushButton* xfc{nullptr};
    };

    explicit ReceivePanel(const Buttons& buttons, QWidget* parent = nullptr);
};
