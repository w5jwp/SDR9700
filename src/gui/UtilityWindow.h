#pragma once

#include "DialogPlacement.h"
#include "UiTheme.h"

#include <QDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QShowEvent>
#include <QTimer>
#include <QWindow>

namespace sdr9700::ui
{
class UtilityWindow : public QDialog
{
  public:
    explicit UtilityWindow(const QString& title, QWidget* parent = nullptr) : QDialog(nullptr), m_centerHost(parent)
    {
        setWindowTitle(title);
        setWindowModality(Qt::NonModal);
        setAttribute(Qt::WA_DeleteOnClose, false);
        setAttribute(Qt::WA_QuitOnClose, false);
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        if (parent)
        {
            connect(parent, &QObject::destroyed, this, [this]() { deleteLater(); });
        }
    }

    void showCentered()
    {
        centerOnHost();
        if (isMinimized())
        {
            showNormal();
        }
        else
        {
            show();
        }

        centerOnHost();
        bringToFront();
        scheduleCenterOnHost();
        scheduleBringToFront();
    }

    void centerOnHost()
    {
        if (!m_centerHost)
        {
            sdr9700::ui::centerWindowOn(this, nullptr);
            return;
        }

        ensurePolished();
        const QSize targetSize = preparedSize();
        if (size() != targetSize)
        {
            resize(targetSize);
        }

        const QWidget* hostWindow = m_centerHost->window();
        const QRect hostGeometry = hostWindow ? hostWindow->frameGeometry() : m_centerHost->frameGeometry();
        const QPoint hostCenter = hostGeometry.isValid() ? hostGeometry.center() : m_centerHost->geometry().center();
        QRect target(QPoint(hostCenter.x() - targetSize.width() / 2, hostCenter.y() - targetSize.height() / 2),
                     targetSize);

        const QRect available = availableGeometryForPoint(hostCenter);
        if (target.width() > available.width())
        {
            target.moveLeft(available.left());
        }
        else if (target.left() < available.left())
        {
            target.moveLeft(available.left());
        }
        else if (target.right() > available.right())
        {
            target.moveRight(available.right());
        }

        if (target.height() > available.height())
        {
            target.moveTop(available.top());
        }
        else if (target.top() < available.top())
        {
            target.moveTop(available.top());
        }
        else if (target.bottom() > available.bottom())
        {
            target.moveBottom(available.bottom());
        }

        move(target.topLeft());
    }

  protected:
    void showEvent(QShowEvent* event) override
    {
        QDialog::showEvent(event);
        centerOnHost();
        scheduleCenterOnHost();
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Escape)
        {
            close();
            event->accept();
            return;
        }

        QDialog::keyPressEvent(event);
    }

  private:
    void scheduleCenterOnHost()
    {
        QTimer::singleShot(0, this, [this]() { centerOnHost(); });
        QTimer::singleShot(50, this, [this]() { centerOnHost(); });
        QTimer::singleShot(150, this, [this]() { centerOnHost(); });
    }

    void bringToFront()
    {
        raise();
        activateWindow();
        if (QWindow* handle = windowHandle())
        {
            handle->requestActivate();
        }
    }

    void scheduleBringToFront()
    {
        QTimer::singleShot(0, this, [this]() { bringToFront(); });
        QTimer::singleShot(50, this, [this]() { bringToFront(); });
    }

    QSize preparedSize() const
    {
        QSize targetSize = size();
        if (const QSize hint = sizeHint(); hint.isValid())
        {
            targetSize = targetSize.expandedTo(hint);
        }
        if (const QSize minimumHint = minimumSizeHint(); minimumHint.isValid())
        {
            targetSize = targetSize.expandedTo(minimumHint);
        }
        return targetSize.expandedTo(minimumSize());
    }

    QPointer<QWidget> m_centerHost;
};

class UtilityTitleBar : public QWidget
{
  public:
    explicit UtilityTitleBar(const QString& title, QWidget* parent = nullptr) : QWidget(parent)
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

        m_closeBtn = new QPushButton(QStringLiteral("X"), this);
        m_closeBtn->setFixedSize(28, 28);
        m_closeBtn->setStyleSheet(
            QStringLiteral("QPushButton { background: transparent; border: none; color: %1; font-size: 13px; }"
                           "QPushButton:hover { background: %2; color: %3; }")
                .arg(QLatin1String(UiTheme::Color::TextMuted), QLatin1String(UiTheme::Color::Danger),
                     QLatin1String(UiTheme::Color::White)));
        layout->addWidget(m_closeBtn);
    }

    QPushButton* closeButton() const { return m_closeBtn; }

  protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            QWidget* panel = parentWidget();
            if (panel)
            {
                m_dragging = true;
                m_dragOffset = event->globalPosition().toPoint() - panel->frameGeometry().topLeft();
                if (panel->isWindow())
                {
                    if (QWindow* win = panel->windowHandle())
                    {
                        if (win->startSystemMove())
                        {
                            m_dragging = false;
                        }
                    }
                }
            }
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!m_dragging)
        {
            QWidget::mouseMoveEvent(event);
            return;
        }

        QWidget* panel = parentWidget();
        if (!panel)
        {
            return;
        }

        panel->move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        m_dragging = false;
        QWidget::mouseReleaseEvent(event);
    }

  private:
    QPushButton* m_closeBtn{nullptr};
    bool m_dragging{false};
    QPoint m_dragOffset;
};
} // namespace sdr9700::ui
