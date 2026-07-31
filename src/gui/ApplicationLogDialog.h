#pragma once

#include "UtilityWindow.h"

class QComboBox;
class QPlainTextEdit;
class QPushButton;
class QTimer;

class ApplicationLogDialog : public sdr9700::ui::UtilityWindow
{
    Q_OBJECT

  public:
    explicit ApplicationLogDialog(QWidget* parent = nullptr);

  private:
    void refreshLog();
    void exportLog();
    QString visibleLogText() const;

    QComboBox* m_categoryCombo{nullptr};
    QPlainTextEdit* m_logView{nullptr};
    QPushButton* m_pauseButton{nullptr};
    QTimer* m_refreshTimer{nullptr};
    bool m_paused{false};
};
