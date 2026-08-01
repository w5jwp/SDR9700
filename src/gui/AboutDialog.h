#pragma once

#include "UtilityWindow.h"

class AboutDialog : public sdr9700::ui::UtilityWindow
{
    Q_OBJECT

  public:
    explicit AboutDialog(QWidget* parent = nullptr);
};
