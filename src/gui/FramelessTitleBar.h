#pragma once

#include "UiTheme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPushButton>
#include <QShortcut>
#include <QWidget>
#include <QWindow>

class FramelessTitleBar : public QWidget
{
  public:
    explicit FramelessTitleBar(const QString& title, QWidget* parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(28);
        setStyleSheet(QStringLiteral("background: %1;").arg(UiTheme::Color::MenuBar));

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(10, 0, 0, 0);
        layout->setSpacing(0);

        auto* titleLabel = new QLabel(title, this);
        titleLabel->setStyleSheet(
            QStringLiteral("QLabel { color: %1; font-size: 12px; font-weight: bold; background: transparent; }")
                .arg(UiTheme::Color::TextMuted));
        layout->addWidget(titleLabel);
        layout->addStretch();

        m_closeBtn = new QPushButton(QStringLiteral("✕"), this);
        m_closeBtn->setFixedSize(28, 28);
        m_closeBtn->setStyleSheet(
            QStringLiteral("QPushButton { background: transparent; border: none; color: %1; font-size: 13px; }"
                           "QPushButton:hover { background: %2; color: %3; }")
                .arg(QLatin1String(UiTheme::Color::TextMuted), QLatin1String(UiTheme::Color::Danger),
                     QLatin1String(UiTheme::Color::White)));
        m_closeBtn->setAccessibleName(QStringLiteral("Close window"));
        layout->addWidget(m_closeBtn);

        auto* closeShortcut = new QShortcut(QKeySequence::Close, this);
        closeShortcut->setContext(Qt::WindowShortcut);
        connect(closeShortcut, &QShortcut::activated, m_closeBtn, &QPushButton::click);
    }

    QPushButton* closeButton() const { return m_closeBtn; }

  protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            QWidget* titleWindow = parentWidget();
            if (titleWindow && titleWindow->isWindow())
            {
                if (QWindow* win = titleWindow->windowHandle())
                {
                    win->startSystemMove();
                }
            }
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

  private:
    QPushButton* m_closeBtn{nullptr};
};
