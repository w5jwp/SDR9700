// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QDialog>
#include <QUuid>

class QListWidget;
class QPushButton;
class QCheckBox;

class RadioChooserDialog : public QDialog
{
    Q_OBJECT

  public:
    explicit RadioChooserDialog(QWidget* parent = nullptr);

  signals:
    void connectRequested(const QUuid& profileId);

  private slots:
    void onConnect();
    void onOpenPreferences();
    void onSelectionChanged();
    void onAutoConnectToggled(bool on);

  private:
    void rebuildList();

    QListWidget* m_list{nullptr};
    QPushButton* m_connectBtn{nullptr};
    QCheckBox* m_autoConnectCheck{nullptr};
};
