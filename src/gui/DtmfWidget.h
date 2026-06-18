#pragma once

#include <QDialog>
#include <QList>

class QLineEdit;
class QPushButton;

class DtmfWidget : public QDialog
{
    Q_OBJECT

  public:
    explicit DtmfWidget(QWidget* parent = nullptr);

    void setSendInProgress(bool inProgress);

  signals:
    void sendRequested(const QString& digits);

  private:
    void appendDigit(const QString& digit);

    QLineEdit* m_display{nullptr};
    QPushButton* m_sendButton{nullptr};
    QList<QPushButton*> m_keyButtons;
};
