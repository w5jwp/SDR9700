// cppcheck-suppress-file unusedStructMember
#pragma once

#include "RadioProfile.h"

#include <QDialog>
#include <QPointer>
#include <QUuid>

class QListWidget;
class QPushButton;
class QCheckBox;
class QLineEdit;
class QShowEvent;
class QSpinBox;
class QWidget;

class RadioChooserDialog : public QDialog
{
    Q_OBJECT

  public:
    explicit RadioChooserDialog(QWidget* parent = nullptr);

  signals:
    void connectRequested(const QUuid& profileId);

  protected:
    void showEvent(QShowEvent* event) override;

  private slots:
    void onConnect();
    void onSelectionChanged();
    void onAddProfile();
    void onRemoveProfile();
    void onSaveProfile();
    void onAutoConnectToggled(bool on);

  private:
    void rebuildList();
    void loadProfileIntoForm(const RadioProfile& profile);
    RadioProfile profileFromForm() const;
    void setFormEnabled(bool enabled);
    void setFormDirty(bool dirty);

    QListWidget* m_list{nullptr};
    QPushButton* m_addBtn{nullptr};
    QPushButton* m_removeBtn{nullptr};
    QPushButton* m_connectBtn{nullptr};
    QCheckBox* m_autoConnectCheck{nullptr};

    QLineEdit* m_nameEdit{nullptr};
    QLineEdit* m_hostEdit{nullptr};
    QSpinBox* m_portSpin{nullptr};
    QLineEdit* m_userEdit{nullptr};
    QLineEdit* m_passEdit{nullptr};
    QPushButton* m_showPassBtn{nullptr};
    QPushButton* m_saveBtn{nullptr};

    QPointer<QWidget> m_centerHost;
    QUuid m_currentId;
    bool m_formDirty{false};
};
