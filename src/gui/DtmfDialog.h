#pragma once

#include <QDialog>
#include <QList>
#include <QPointer>

class QLineEdit;
class QPushButton;
class QShowEvent;

class DtmfDialog : public QDialog
{
    Q_OBJECT

  public:
    explicit DtmfDialog(QWidget* parent = nullptr);

    void setSendInProgress(bool inProgress);

  signals:
    void sendRequested(const QString& digits);

  protected:
    void showEvent(QShowEvent* event) override;

  private:
    void appendDigit(const QString& digit);

    QPointer<QWidget> m_centerHost;
    QLineEdit* m_display{nullptr};
    QPushButton* m_sendButton{nullptr};
    QList<QPushButton*> m_keyButtons;
};
