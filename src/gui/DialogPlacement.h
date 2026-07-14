#pragma once

#include <QGuiApplication>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QScreen>
#include <QSize>
#include <QWidget>
#include <algorithm>

namespace sdr9700::ui
{
inline QRect availableGeometryForPoint(const QPoint& point)
{
    const QList<QScreen*> screens = QGuiApplication::screens();
    const auto screenIt = std::find_if(screens.cbegin(), screens.cend(), [&point](const QScreen* screen)
                                       { return screen && screen->availableGeometry().contains(point); });
    if (screenIt != screens.cend())
    {
        return (*screenIt)->availableGeometry();
    }

    if (const QScreen* primary = QGuiApplication::primaryScreen())
    {
        return primary->availableGeometry();
    }

    return QRect(point, QSize(1, 1));
}

inline QSize preparedWindowSize(QWidget* window)
{
    window->ensurePolished();
    if (!window->isVisible())
    {
        window->adjustSize();
    }

    QSize size = window->size();
    if (const QSize hint = window->sizeHint(); hint.isValid())
    {
        size = size.expandedTo(hint);
    }
    if (const QSize minimumHint = window->minimumSizeHint(); minimumHint.isValid())
    {
        size = size.expandedTo(minimumHint);
    }
    return size.expandedTo(window->minimumSize());
}

inline void centerWindowOn(QWidget* window, const QWidget* host)
{
    if (!window)
    {
        return;
    }

    if (!window->isWindow())
    {
        const QSize size = preparedWindowSize(window);
        window->resize(size);

        QWidget* parent = window->parentWidget();
        const QWidget* hostWidget = host ? host : parent;
        QRect hostGeometry(QPoint(0, 0), parent ? parent->size() : size);
        if (parent && hostWidget)
        {
            hostGeometry = QRect(hostWidget == parent ? QPoint(0, 0) : hostWidget->mapTo(parent, QPoint(0, 0)),
                                 hostWidget->size());
        }

        QRect target(
            QPoint(hostGeometry.center().x() - size.width() / 2, hostGeometry.center().y() - size.height() / 2), size);
        if (parent)
        {
            const QRect parentGeometry(QPoint(0, 0), parent->size());
            if (target.width() > parentGeometry.width())
            {
                target.moveLeft(parentGeometry.left());
            }
            else if (target.left() < parentGeometry.left())
            {
                target.moveLeft(parentGeometry.left());
            }
            else if (target.right() > parentGeometry.right())
            {
                target.moveRight(parentGeometry.right());
            }

            if (target.height() > parentGeometry.height())
            {
                target.moveTop(parentGeometry.top());
            }
            else if (target.top() < parentGeometry.top())
            {
                target.moveTop(parentGeometry.top());
            }
            else if (target.bottom() > parentGeometry.bottom())
            {
                target.moveBottom(parentGeometry.bottom());
            }
        }

        window->move(target.topLeft());
        window->raise();
        return;
    }

    const QWidget* hostWindow = host ? host->window() : nullptr;
    const QRect hostGeometry = hostWindow ? hostWindow->frameGeometry() : window->frameGeometry();
    const QPoint hostCenter = hostGeometry.isValid() ? hostGeometry.center() : window->geometry().center();
    const QSize size = preparedWindowSize(window);
    window->resize(size);

    const QRect frameGeometry = window->frameGeometry();
    const QSize frameSize = frameGeometry.isValid() ? frameGeometry.size() : size;
    QRect target(QPoint(hostCenter.x() - frameSize.width() / 2, hostCenter.y() - frameSize.height() / 2), frameSize);

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

    const QPoint frameOffset = window->geometry().topLeft() - window->frameGeometry().topLeft();
    window->move(target.topLeft() + frameOffset);
}
} // namespace sdr9700::ui
