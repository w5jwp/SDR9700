// cppcheck-suppress-file unusedStructMember
#pragma once

#include "RadioProfile.h"
#include <QWidget>

class QListWidget;
class QLineEdit;
class QSpinBox;
class QPushButton;

class RadioProfileWidget : public QWidget
{
    Q_OBJECT

  public:
    explicit RadioProfileWidget(QWidget* parent = nullptr);

  private slots:
    void onSelectionChanged();
    void onAddProfile();
    void onRemoveProfile();
    void onSaveProfile();

  private:
    void rebuildList();
    void loadProfileIntoForm(const RadioProfile& p);
    RadioProfile profileFromForm() const;
    void setFormEnabled(bool enabled);

    QListWidget* m_list{nullptr};
    QPushButton* m_addBtn{nullptr};
    QPushButton* m_removeBtn{nullptr};

    QLineEdit* m_nameEdit{nullptr};
    QLineEdit* m_hostEdit{nullptr};
    QSpinBox* m_portSpin{nullptr};
    QLineEdit* m_userEdit{nullptr};
    QLineEdit* m_passEdit{nullptr};
    QPushButton* m_showPassBtn{nullptr};
    QPushButton* m_saveBtn{nullptr};

    QUuid m_currentId;
    bool m_formDirty{false};
};
