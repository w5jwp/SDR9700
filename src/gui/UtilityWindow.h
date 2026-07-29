#pragma once

#include "DialogPlacement.h"
#include <QDialog>
#include <QKeyEvent>
#include <QPointer>
#include <QShowEvent>
#include <QTimer>
#include <QWindow>

namespace sdr9700::ui
{
class UtilityWindow : public QDialog
{
  public:
    explicit UtilityWindow(const QString& title, QWidget* parent = nullptr)
        : QDialog(parent, Qt::Window), m_centerHost(parent)
    {
        setWindowTitle(title);
        setWindowModality(Qt::NonModal);
        setAttribute(Qt::WA_DeleteOnClose, false);
        setAttribute(Qt::WA_QuitOnClose, false);
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
} // namespace sdr9700::ui
