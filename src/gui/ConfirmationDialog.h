#pragma once

#include "UiTheme.h"

#include <QMessageBox>
#include <QPushButton>

namespace sdr9700::ui
{
inline void configureMessageBoxWindow(QMessageBox& dialog)
{
    dialog.setWindowFlag(Qt::FramelessWindowHint, true);
    dialog.setSizeGripEnabled(false);
    dialog.setStyleSheet(QStringLiteral("QMessageBox { background: %1; border: 1px solid %2; }"
                                        "QMessageBox QLabel { color: %3; }")
                             .arg(QLatin1String(UiTheme::Color::Panel), QLatin1String(UiTheme::Color::Border),
                                  QLatin1String(UiTheme::Color::TextPrimary)));
    dialog.ensurePolished();
    dialog.adjustSize();
    dialog.setFixedSize(dialog.sizeHint());
}

inline QPushButton* configureConfirmationButtons(QMessageBox& dialog, const QString& actionLabel, bool destructive)
{
    QPushButton* const cancelButton = dialog.addButton(QMessageBox::Cancel);
    QPushButton* const actionButton =
        dialog.addButton(actionLabel, destructive ? QMessageBox::DestructiveRole : QMessageBox::AcceptRole);
    dialog.setDefaultButton(cancelButton);
    dialog.setEscapeButton(cancelButton);
    return actionButton;
}

inline bool confirmAction(QWidget* parent, const QString& title, const QString& message, const QString& actionLabel,
                          bool destructive)
{
    QMessageBox dialog(QMessageBox::Question, title, message, QMessageBox::NoButton, parent);
    const QPushButton* const actionButton = configureConfirmationButtons(dialog, actionLabel, destructive);
    configureMessageBoxWindow(dialog);
    dialog.exec();
    return dialog.clickedButton() == actionButton;
}

inline void showWarning(QWidget* parent, const QString& title, const QString& message)
{
    QMessageBox dialog(QMessageBox::Warning, title, message, QMessageBox::Ok, parent);
    configureMessageBoxWindow(dialog);
    dialog.exec();
}
} // namespace sdr9700::ui
