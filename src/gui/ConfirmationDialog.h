#pragma once

#include <QMessageBox>
#include <QPushButton>

namespace sdr9700::ui
{
inline QPushButton* configureConfirmationButtons(QMessageBox& dialog, const QString& actionLabel, bool destructive)
{
    QPushButton* cancelButton = dialog.addButton(QMessageBox::Cancel);
    QPushButton* actionButton =
        dialog.addButton(actionLabel, destructive ? QMessageBox::DestructiveRole : QMessageBox::AcceptRole);
    dialog.setDefaultButton(cancelButton);
    dialog.setEscapeButton(cancelButton);
    return actionButton;
}

inline bool confirmAction(QWidget* parent, const QString& title, const QString& message, const QString& actionLabel,
                          bool destructive)
{
    QMessageBox dialog(QMessageBox::Question, title, message, QMessageBox::NoButton, parent);
    QPushButton* actionButton = configureConfirmationButtons(dialog, actionLabel, destructive);
    dialog.exec();
    return dialog.clickedButton() == actionButton;
}
} // namespace sdr9700::ui
