#pragma once

#include "UtilityWindow.h"

class QComboBox;
class QCheckBox;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class QHideEvent;
class QShowEvent;

class ApplicationLogDialog : public sdr9700::ui::UtilityWindow
{
    Q_OBJECT

  public:
    explicit ApplicationLogDialog(QWidget* parent = nullptr);

  protected:
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;

  private:
    void refreshLog();
    void exportLog();
    void resetLogView();
    void clearCivTrafficReporting();

    QComboBox* m_categoryCombo{nullptr};
    QCheckBox* m_civTrafficCheckBox{nullptr};
    QPlainTextEdit* m_logView{nullptr};
    QPushButton* m_pauseButton{nullptr};
    QTimer* m_refreshTimer{nullptr};
    bool m_paused{false};
    quint64 m_lastSequence{0};
    QString m_activeCategory;
};
