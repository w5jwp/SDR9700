#pragma once

#include "UtilityWindow.h"

#include <QList>

class QLineEdit;
class QPushButton;

class DtmfDialog : public sdr9700::ui::UtilityWindow
{
    Q_OBJECT

  public:
    explicit DtmfDialog(QWidget* parent = nullptr);

    void setSendInProgress(bool inProgress);

  signals:
    void sendRequested(const QString& digits);

  private:
    void appendDigit(const QString& digit);

    QLineEdit* m_display{nullptr};
    QPushButton* m_sendButton{nullptr};
    QList<QPushButton*> m_keyButtons;
};
