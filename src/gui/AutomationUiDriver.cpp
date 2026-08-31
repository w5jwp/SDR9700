#include "AutomationUiDriver.h"

#include "DtmfDialog.h"
#include "MainWindow.h"

#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QJsonValue>
#include <QLineEdit>
#include <QMetaObject>
#include <QSpinBox>
#include <QTimer>
#include <QWidget>

namespace
{
QJsonArray modelItems(const QAbstractItemModel* model, const QModelIndex& parent = {},
                      const QJsonArray& parentPath = {})
{
    QJsonArray items;
    if (!model)
    {
        return items;
    }
    for (int row = 0; row < model->rowCount(parent) && items.size() < 500; ++row)
    {
        const QModelIndex index = model->index(row, 0, parent);
        QJsonArray path = parentPath;
        path.append(row);
        items.append(QJsonObject{{QStringLiteral("path"), path},
                                 {QStringLiteral("text"), model->data(index, Qt::DisplayRole).toString()}});
        const QJsonArray children = modelItems(model, index, path);
        for (const QJsonValue& child : children)
        {
            if (items.size() >= 500)
            {
                break;
            }
            items.append(child);
        }
    }
    return items;
}

QModelIndex requestedModelIndex(QAbstractItemView* view, const QJsonObject& request)
{
    if (!view->model())
    {
        return {};
    }
    const QJsonArray path = request.value(QStringLiteral("indexPath")).toArray();
    QModelIndex parent;
    if (!path.isEmpty())
    {
        for (const QJsonValue& component : path)
        {
            parent = view->model()->index(component.toInt(-1), 0, parent);
            if (!parent.isValid())
            {
                return {};
            }
        }
        const int column = request.value(QStringLiteral("column")).toInt(0);
        return column == 0 ? parent : parent.siblingAtColumn(column);
    }
    return view->model()->index(request.value(QStringLiteral("row")).toInt(-1),
                                request.value(QStringLiteral("column")).toInt(0));
}
} // namespace

AutomationUiDriver::AutomationUiDriver(MainWindow* window) : QObject(window), m_window(window) {}

QJsonObject AutomationUiDriver::reject(const QString& code, const QString& message)
{
    return QJsonObject{
        {QStringLiteral("ok"), false}, {QStringLiteral("error"), code}, {QStringLiteral("message"), message}};
}

bool AutomationUiDriver::isInteractive(QObject* object) const
{
    return qobject_cast<QAbstractButton*>(object) || qobject_cast<QAction*>(object) ||
           qobject_cast<QComboBox*>(object) || qobject_cast<QAbstractSlider*>(object) ||
           qobject_cast<QSpinBox*>(object) || qobject_cast<QDoubleSpinBox*>(object) ||
           qobject_cast<QLineEdit*>(object) || qobject_cast<QAbstractItemView*>(object);
}

bool AutomationUiDriver::isPttProducing(QObject* object) const
{
    if (object == m_window->m_pttBtn)
    {
        return true;
    }
    const auto* button = qobject_cast<QAbstractButton*>(object);
    if (!button || button->text().trimmed().compare(QStringLiteral("Send"), Qt::CaseInsensitive) != 0)
    {
        return false;
    }
    for (QObject* parent = object->parent(); parent; parent = parent->parent())
    {
        if (qobject_cast<DtmfDialog*>(parent))
        {
            return true;
        }
    }
    return false;
}

QString AutomationUiDriver::registerObject(QObject* object)
{
    const QVariant existing = object->property("sdr9700AutomationId");
    QString id = existing.toString();
    if (id.isEmpty())
    {
        id = QStringLiteral("control-%1").arg(m_nextId++);
        object->setProperty("sdr9700AutomationId", id);
    }
    m_objects.insert(id, object);
    return id;
}

void AutomationUiDriver::rebuildRegistry()
{
    m_objects.clear();
    for (QWidget* topLevel : QApplication::topLevelWidgets())
    {
        if (!topLevel || (!topLevel->isVisible() && topLevel != m_window))
        {
            continue;
        }
        if (isInteractive(topLevel))
        {
            registerObject(topLevel);
        }
        const auto descendants = topLevel->findChildren<QObject*>();
        for (QObject* object : descendants)
        {
            if (isInteractive(object))
            {
                registerObject(object);
            }
        }
    }
    const auto actions = m_window->findChildren<QAction*>();
    for (QAction* action : actions)
    {
        registerObject(action);
    }
}

QJsonObject AutomationUiDriver::describe(QObject* object, const QString& id) const
{
    QJsonObject description{{QStringLiteral("id"), id},
                            {QStringLiteral("type"), QString::fromLatin1(object->metaObject()->className())},
                            {QStringLiteral("objectName"), object->objectName()},
                            {QStringLiteral("pttProhibited"), isPttProducing(object)}};
    if (const auto* widget = qobject_cast<QWidget*>(object))
    {
        description.insert(QStringLiteral("visible"), widget->isVisible());
        description.insert(QStringLiteral("enabled"), widget->isEnabled());
        description.insert(QStringLiteral("window"), widget->window()->windowTitle());
        description.insert(QStringLiteral("accessibleName"), widget->accessibleName());
    }
    if (const auto* button = qobject_cast<QAbstractButton*>(object))
    {
        description.insert(QStringLiteral("text"), button->text());
        description.insert(QStringLiteral("checked"),
                           button->isCheckable() ? QJsonValue(button->isChecked()) : QJsonValue(QJsonValue::Null));
    }
    else if (const auto* action = qobject_cast<QAction*>(object))
    {
        description.insert(QStringLiteral("text"), action->text());
        description.insert(QStringLiteral("enabled"), action->isEnabled());
        description.insert(QStringLiteral("visible"), action->isVisible());
    }
    else if (const auto* combo = qobject_cast<QComboBox*>(object))
    {
        description.insert(QStringLiteral("value"), combo->currentText());
        QJsonArray options;
        for (int index = 0; index < combo->count(); ++index)
        {
            options.append(combo->itemText(index));
        }
        description.insert(QStringLiteral("options"), options);
    }
    else if (const auto* slider = qobject_cast<QAbstractSlider*>(object))
    {
        description.insert(QStringLiteral("value"), slider->value());
        description.insert(QStringLiteral("minimum"), slider->minimum());
        description.insert(QStringLiteral("maximum"), slider->maximum());
    }
    else if (const auto* spin = qobject_cast<QSpinBox*>(object))
    {
        description.insert(QStringLiteral("value"), spin->value());
        description.insert(QStringLiteral("minimum"), spin->minimum());
        description.insert(QStringLiteral("maximum"), spin->maximum());
    }
    else if (const auto* spin = qobject_cast<QDoubleSpinBox*>(object))
    {
        description.insert(QStringLiteral("value"), spin->value());
        description.insert(QStringLiteral("minimum"), spin->minimum());
        description.insert(QStringLiteral("maximum"), spin->maximum());
    }
    else if (const auto* view = qobject_cast<QAbstractItemView*>(object))
    {
        description.insert(QStringLiteral("items"), modelItems(view->model()));
    }
    return description;
}

QJsonObject AutomationUiDriver::listControls()
{
    rebuildRegistry();
    QJsonArray controls;
    QStringList ids = m_objects.keys();
    ids.sort();
    for (const QString& id : ids)
    {
        if (m_objects.value(id))
        {
            controls.append(describe(m_objects.value(id), id));
        }
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("controls"), controls}};
}

QObject* AutomationUiDriver::resolve(const QJsonObject& request)
{
    rebuildRegistry();
    return m_objects.value(request.value(QStringLiteral("controlId")).toString());
}

QJsonObject AutomationUiDriver::activate(const QJsonObject& request)
{
    QObject* object = resolve(request);
    if (!object)
    {
        return reject(QStringLiteral("control_not_found"),
                      QStringLiteral("Call ui_list and provide a current controlId"));
    }
    if (isPttProducing(object))
    {
        return reject(QStringLiteral("ptt_prohibited"),
                      QStringLiteral("Automation may not invoke a PTT-producing control"));
    }
    if (const auto* widget = qobject_cast<QWidget*>(object); widget && (!widget->isVisible() || !widget->isEnabled()))
    {
        return reject(QStringLiteral("control_unavailable"),
                      QStringLiteral("Control must be visible and enabled before it can be activated"));
    }
    if (const auto* action = qobject_cast<QAction*>(object); action && (!action->isVisible() || !action->isEnabled()))
    {
        return reject(QStringLiteral("control_unavailable"),
                      QStringLiteral("Action must be visible and enabled before it can be activated"));
    }
    if (auto* button = qobject_cast<QAbstractButton*>(object))
    {
        QPointer<QAbstractButton> guarded(button);
        QTimer::singleShot(0, this,
                           [guarded]()
                           {
                               if (guarded)
                                   guarded->click();
                           });
    }
    else if (auto* action = qobject_cast<QAction*>(object))
    {
        QPointer<QAction> guarded(action);
        QTimer::singleShot(0, this,
                           [guarded]()
                           {
                               if (guarded)
                                   guarded->trigger();
                           });
    }
    else if (auto* view = qobject_cast<QAbstractItemView*>(object))
    {
        const QModelIndex index = requestedModelIndex(view, request);
        if (!index.isValid())
        {
            return reject(QStringLiteral("invalid_index"), QStringLiteral("row and column must identify a model item"));
        }
        view->setCurrentIndex(index);
        if (request.value(QStringLiteral("doubleClick")).toBool())
        {
            QPointer<QAbstractItemView> guarded(view);
            QTimer::singleShot(0, this,
                               [guarded, index]()
                               {
                                   if (guarded)
                                       QMetaObject::invokeMethod(guarded, "doubleClicked", Q_ARG(QModelIndex, index));
                               });
        }
    }
    else
    {
        return reject(QStringLiteral("unsupported_operation"),
                      QStringLiteral("This control accepts ui_set, not ui_activate"));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("status"), QStringLiteral("accepted")}};
}

QJsonObject AutomationUiDriver::setValue(const QJsonObject& request)
{
    QObject* object = resolve(request);
    if (!object)
    {
        return reject(QStringLiteral("control_not_found"),
                      QStringLiteral("Call ui_list and provide a current controlId"));
    }
    if (isPttProducing(object))
    {
        return reject(QStringLiteral("ptt_prohibited"),
                      QStringLiteral("Automation may not invoke a PTT-producing control"));
    }
    if (const auto* widget = qobject_cast<QWidget*>(object); widget && (!widget->isVisible() || !widget->isEnabled()))
    {
        return reject(QStringLiteral("control_unavailable"),
                      QStringLiteral("Control must be visible and enabled before its value can be changed"));
    }
    const QJsonValue value = request.value(QStringLiteral("value"));
    if (auto* check = qobject_cast<QCheckBox*>(object))
    {
        if (!value.isBool())
            return reject(QStringLiteral("invalid_value"), QStringLiteral("Checkbox value must be boolean"));
        check->setChecked(value.toBool());
    }
    else if (auto* combo = qobject_cast<QComboBox*>(object))
    {
        const int index = value.isDouble() ? value.toInt(-1) : combo->findText(value.toString(), Qt::MatchFixedString);
        if (index < 0 || index >= combo->count())
            return reject(QStringLiteral("invalid_value"), QStringLiteral("Unknown combo-box option"));
        combo->setCurrentIndex(index);
    }
    else if (auto* slider = qobject_cast<QAbstractSlider*>(object))
    {
        const int integer = value.toInt(slider->minimum() - 1);
        if (integer < slider->minimum() || integer > slider->maximum())
            return reject(QStringLiteral("invalid_value"), QStringLiteral("Value is outside the slider range"));
        slider->setValue(integer);
        QMetaObject::invokeMethod(slider, "sliderReleased", Qt::QueuedConnection);
    }
    else if (auto* spin = qobject_cast<QSpinBox*>(object))
    {
        spin->setValue(value.toInt());
    }
    else if (auto* spin = qobject_cast<QDoubleSpinBox*>(object))
    {
        spin->setValue(value.toDouble());
    }
    else if (auto* lineEdit = qobject_cast<QLineEdit*>(object))
    {
        lineEdit->setText(value.toString());
        QMetaObject::invokeMethod(lineEdit, "editingFinished", Qt::QueuedConnection);
    }
    else if (auto* button = qobject_cast<QAbstractButton*>(object); button && button->isCheckable())
    {
        if (!value.isBool())
            return reject(QStringLiteral("invalid_value"), QStringLiteral("Checkable-button value must be boolean"));
        button->setChecked(value.toBool());
    }
    else
    {
        return reject(QStringLiteral("unsupported_operation"), QStringLiteral("Control does not accept ui_set"));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("status"), QStringLiteral("accepted")}};
}
