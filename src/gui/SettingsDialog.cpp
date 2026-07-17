#include "SettingsDialog.h"
#include "ApplicationConfigurationSettingsPanel.h"
#include "AudioDevicesSettingsPanel.h"
#include "SpectrumScopeSettingsPanel.h"
#include "DialogPlacement.h"
#include "FramelessTitleBar.h"
#include "MemoryManagerSettingsPanel.h"
#ifdef HAVE_HIDAPI
#include "IcomRC28SettingsPanel.h"
#endif

#include <QAbstractItemView>
#include <QAction>
#include <QFont>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QSize>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace
{
constexpr int kSettingsPageScrollbarGutter = 10;

int pageKey(SettingsDialog::Page page)
{
    return static_cast<int>(page);
}

class CurrentPageStackedWidget : public QStackedWidget
{
  public:
    using QStackedWidget::QStackedWidget;

    QSize sizeHint() const override
    {
        return currentWidget() ? currentWidget()->sizeHint() : QStackedWidget::sizeHint();
    }

    QSize minimumSizeHint() const override
    {
        return currentWidget() ? currentWidget()->minimumSizeHint() : QStackedWidget::minimumSizeHint();
    }
};
} // namespace

SettingsDialog::SettingsDialog(QWidget* parent) : SettingsDialog(Page::AudioDevices, parent) {}

#ifdef HAVE_HIDAPI
SettingsDialog::SettingsDialog(Page page, QWidget* parent, IcomRC28Manager* icomRC28Manager)
    : QDialog(parent), m_icomRC28Manager(icomRC28Manager), m_centerHost(parent)
#else
SettingsDialog::SettingsDialog(Page page, QWidget* parent) : QDialog(parent), m_centerHost(parent)
#endif
{
    const QString title = QStringLiteral("Settings");
    setWindowTitle(title);
    setMinimumSize(780, 520);
    setWindowFlags(Qt::FramelessWindowHint);
    setWindowModality(Qt::NonModal);
    setStyleSheet(QStringLiteral("SettingsDialog { background: %1; border: 1px solid %2; }")
                      .arg(QLatin1String(UiTheme::Color::Panel), QLatin1String(UiTheme::Color::Border)));

    auto* titleBar = new FramelessTitleBar(title, this);
    connect(titleBar->closeButton(), &QPushButton::clicked, this, &QDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(titleBar);

    auto* content = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(12, 12, 12, 12);
    contentLayout->setSpacing(10);

    auto* search = new QLineEdit(content);
    search->setObjectName(QStringLiteral("settingsSearch"));
    search->setPlaceholderText(QStringLiteral("Search settings (%1)")
                                   .arg(QKeySequence(QKeySequence::Find).toString(QKeySequence::NativeText)));
    search->setClearButtonEnabled(true);
    search->setAccessibleName(QStringLiteral("Search settings"));
    search->setMinimumHeight(34);
    contentLayout->addWidget(search);

    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(12);

    m_navigation = new QTreeWidget(content);
    m_navigation->setObjectName(QStringLiteral("settingsNavigation"));
    m_navigation->setHeaderHidden(true);
    m_navigation->setRootIsDecorated(true);
    m_navigation->setItemsExpandable(false);
    m_navigation->setIndentation(16);
    m_navigation->setMinimumWidth(190);
    m_navigation->setMaximumWidth(245);
    m_navigation->setUniformRowHeights(false);
    m_navigation->setAccessibleName(QStringLiteral("Settings pages"));
    m_navigation->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_navigation->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    body->addWidget(m_navigation);

    auto* divider = new QWidget(content);
    divider->setFixedWidth(1);
    divider->setStyleSheet(QStringLiteral("background: %1;").arg(QLatin1String(UiTheme::Color::Border)));
    body->addWidget(divider);

    auto* pageHost = new QWidget(content);
    m_pageLayout = new QVBoxLayout(pageHost);
    m_pageLayout->setContentsMargins(0, 0, 0, 0);
    m_pageLayout->setSpacing(8);
    m_pageTitle = new QLabel(pageHost);
    m_pageTitle->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 16px; font-weight: bold; }")
                                   .arg(QLatin1String(UiTheme::Color::TextPrimary)));
    m_pageLayout->addWidget(m_pageTitle);
    m_pages = new CurrentPageStackedWidget(pageHost);
    connect(m_pages, &QStackedWidget::currentChanged, m_pages, &QWidget::updateGeometry);
    connect(m_pages, &QStackedWidget::currentChanged, this,
            [this]()
            {
                updatePageScrollGutter();
                QTimer::singleShot(0, this, &SettingsDialog::updatePageScrollGutter);
            });
    m_pageLayout->addWidget(m_pages, 1);

    m_pageScroll = new QScrollArea(content);
    m_pageScroll->setWidgetResizable(true);
    m_pageScroll->setFrameShape(QFrame::NoFrame);
    m_pageScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_pageScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_pageScroll->verticalScrollBar(), &QScrollBar::rangeChanged, this,
            [this](int, int) { updatePageScrollGutter(); });
    m_pageScroll->setWidget(pageHost);
    body->addWidget(m_pageScroll, 1);
    contentLayout->addLayout(body, 1);

#ifdef HAVE_HIDAPI
    QTreeWidgetItem* accessoriesCategory =
        addCategory(QStringLiteral("ACCESSORIES"), QStringLiteral("accessories hardware controller remote encoder"));
    addPage(accessoriesCategory, Page::IcomRC28, QStringLiteral("Icom RC-28 Remote Encoder"),
            QStringLiteral("hardware icom icomRC28 rc-28 remote encoder controller knob button f1 f2 ptt mapping"),
            [this]()
            {
                auto* panel = new IcomRC28SettingsPanel(m_icomRC28Manager);
                connect(panel, &IcomRC28SettingsPanel::encoderSettingsChanged, this,
                        &SettingsDialog::icomRC28EncoderSettingsChanged);
                return panel;
            });
#endif
    QTreeWidgetItem* applicationCategory =
        addCategory(QStringLiteral("APPLICATION"),
                    QStringLiteral("application configuration backup restore reset memory manager spectrum scope"));
    addPage(applicationCategory, Page::ApplicationConfiguration, QStringLiteral("Configuration"),
            QStringLiteral("application configuration backup restore reset settings"),
            [] { return new ApplicationConfigurationSettingsPanel; });
    addPage(applicationCategory, Page::MemoryManager, QStringLiteral("Memory Manager"),
            QStringLiteral("application memory manager memories radio sync poll polling interval refresh"),
            [this]()
            {
                auto* panel = new MemoryManagerSettingsPanel;
                connect(panel, &MemoryManagerSettingsPanel::pollIntervalSecondsChanged, this,
                        &SettingsDialog::memoryPollIntervalSecondsChanged);
                return panel;
            });
    addPage(applicationCategory, Page::SpectrumScope, QStringLiteral("Spectrum Scope"),
            QStringLiteral("application appearance display visual spectrum scope center line color vfo marker "
                           "background gridlines grid density fewer normal more"),
            [this]()
            {
                auto* panel = new SpectrumScopeSettingsPanel;
                connect(panel, &SpectrumScopeSettingsPanel::centerLineColorChanged, this,
                        &SettingsDialog::spectrumScopeCenterLineColorChanged);
                connect(panel, &SpectrumScopeSettingsPanel::backgroundColorChanged, this,
                        &SettingsDialog::spectrumScopeBackgroundColorChanged);
                connect(panel, &SpectrumScopeSettingsPanel::gridLineColorChanged, this,
                        &SettingsDialog::spectrumScopeGridLineColorChanged);
                connect(panel, &SpectrumScopeSettingsPanel::gridDensityChanged, this,
                        &SettingsDialog::spectrumScopeGridDensityChanged);
                connect(panel, &SpectrumScopeSettingsPanel::reverseMouseWheelTuningChanged, this,
                        &SettingsDialog::reverseMouseWheelTuningChanged);
                return panel;
            });

    QTreeWidgetItem* audioCategory = addCategory(QStringLiteral("RECEIVE & TRANSMIT"),
                                                 QStringLiteral("receive transmit audio input output computer radio"));
    addPage(audioCategory, Page::AudioDevices, QStringLiteral("Audio Devices"),
            QStringLiteral("audio input output device microphone speaker codec channels receive transmit playback"),
            []() { return new AudioDevicesSettingsPanel; });
    for (int i = 0; i < m_navigation->topLevelItemCount(); ++i)
    {
        m_navigation->topLevelItem(i)->sortChildren(0, Qt::AscendingOrder);
    }
    m_navigation->sortItems(0, Qt::AscendingOrder);
    m_navigation->expandAll();

    connect(m_navigation, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current)
            {
                if (!current)
                {
                    return;
                }

                if (!current->data(0, Qt::UserRole).isValid())
                {
                    if (current->childCount() > 0)
                    {
                        m_navigation->setCurrentItem(current->child(0));
                    }
                    return;
                }

                const auto selectedPage = static_cast<Page>(current->data(0, Qt::UserRole).toInt());
                buildDeferredPage(selectedPage);
                m_pages->setCurrentIndex(m_pageIndexes.value(pageKey(selectedPage)));
                m_pageTitle->setText(current->text(0));
                if (m_pageScroll)
                {
                    m_pageScroll->verticalScrollBar()->setValue(0);
                    m_pageScroll->horizontalScrollBar()->setValue(0);
                    QTimer::singleShot(0, this,
                                       [this]()
                                       {
                                           if (m_pageScroll)
                                           {
                                               m_pageScroll->verticalScrollBar()->setValue(0);
                                               m_pageScroll->horizontalScrollBar()->setValue(0);
                                           }
                                       });
                }
            });
    connect(search, &QLineEdit::textChanged, this, &SettingsDialog::filterNavigation);

    auto* findAction = new QAction(this);
    findAction->setShortcut(QKeySequence::Find);
    findAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(findAction, &QAction::triggered, search,
            [search]()
            {
                search->setFocus();
                search->selectAll();
            });
    addAction(findAction);

    selectPage(page);

    auto* closeBtn = new QPushButton(QStringLiteral("Close"), content);
    auto* footerDivider = new QWidget(content);
    footerDivider->setFixedHeight(1);
    footerDivider->setStyleSheet(QStringLiteral("background: %1;").arg(QLatin1String(UiTheme::Color::Border)));
    contentLayout->addWidget(footerDivider);

    auto* btnRow = new QHBoxLayout;
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->addStretch(1);
    btnRow->addWidget(closeBtn);
    contentLayout->addLayout(btnRow);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    root->addWidget(content, 1);
}

QTreeWidgetItem* SettingsDialog::addCategory(const QString& title, const QString& keywords)
{
    auto* item = new QTreeWidgetItem(m_navigation, {title});
    item->setData(0, Qt::UserRole + 1, keywords);
    item->setToolTip(0, keywords);
    QFont font = item->font(0);
    font.setBold(true);
    item->setFont(0, font);
    return item;
}

void SettingsDialog::addPage(QTreeWidgetItem* parent, Page page, const QString& title, const QString& keywords,
                             std::function<QWidget*()> builder)
{
    auto* placeholder = new QWidget;
    const int stackIndex = m_pages->addWidget(placeholder);
    const int key = pageKey(page);

    auto* item = parent ? new QTreeWidgetItem(parent, {title}) : new QTreeWidgetItem(m_navigation, {title});
    item->setData(0, Qt::UserRole, key);
    item->setData(0, Qt::UserRole + 1, keywords);
    item->setToolTip(0, keywords);

    m_pageIndexes.insert(key, stackIndex);
    m_pageItems.insert(key, item);
    m_deferredBuilders.insert(key, std::move(builder));
}

void SettingsDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);

    sdr9700::ui::centerWindowOn(this, m_centerHost);
    QTimer::singleShot(0, this, [this]() { sdr9700::ui::centerWindowOn(this, m_centerHost); });
    QTimer::singleShot(50, this, [this]() { sdr9700::ui::centerWindowOn(this, m_centerHost); });
}

void SettingsDialog::buildDeferredPage(Page page)
{
    const int key = pageKey(page);
    auto it = m_deferredBuilders.find(key);
    if (it == m_deferredBuilders.end())
    {
        return;
    }

    QWidget* placeholder = m_pages->widget(m_pageIndexes.value(key));
    auto* layout = new QVBoxLayout(placeholder);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(it.value()(), 1);
    m_deferredBuilders.erase(it);
}

void SettingsDialog::filterNavigation(const QString& text)
{
    const QString needle = text.trimmed();
    QTreeWidgetItem* firstVisible = nullptr;

    for (int i = 0; i < m_navigation->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* item = m_navigation->topLevelItem(i);
        const bool selfVisible = needle.isEmpty() || itemSearchText(item).contains(needle, Qt::CaseInsensitive);
        bool childVisible = false;
        for (int childIndex = 0; childIndex < item->childCount(); ++childIndex)
        {
            QTreeWidgetItem* child = item->child(childIndex);
            const bool visible =
                selfVisible || needle.isEmpty() || itemSearchText(child).contains(needle, Qt::CaseInsensitive);
            child->setHidden(!visible);
            childVisible = childVisible || visible;
            if (visible && !firstVisible)
            {
                firstVisible = child;
            }
        }

        const bool visible = selfVisible || childVisible;
        item->setHidden(!visible);
        if (item->childCount() == 0 && visible && !firstVisible)
        {
            firstVisible = item;
        }
    }

    if (firstVisible && (!m_navigation->currentItem() || m_navigation->currentItem()->isHidden()))
    {
        m_navigation->setCurrentItem(firstVisible);
    }
}

void SettingsDialog::selectPage(Page page)
{
    QTreeWidgetItem* item = m_pageItems.value(pageKey(page), nullptr);
    if (!item)
    {
        return;
    }

    m_navigation->setCurrentItem(item);
    m_navigation->scrollToItem(item, QAbstractItemView::PositionAtCenter);
}

void SettingsDialog::updatePageScrollGutter()
{
    if (!m_pageScroll || !m_pageLayout)
    {
        return;
    }

    const QScrollBar* verticalScrollBar = m_pageScroll->verticalScrollBar();
    const bool needsVerticalScroll = verticalScrollBar && verticalScrollBar->maximum() > verticalScrollBar->minimum();
    const int rightGutter = needsVerticalScroll ? kSettingsPageScrollbarGutter : 0;
    const QMargins margins = m_pageLayout->contentsMargins();
    if (margins.right() == rightGutter)
    {
        return;
    }

    m_pageLayout->setContentsMargins(margins.left(), margins.top(), rightGutter, margins.bottom());
}

QString SettingsDialog::itemSearchText(QTreeWidgetItem* item)
{
    return item ? item->text(0) + QLatin1Char(' ') + item->data(0, Qt::UserRole + 1).toString() : QString();
}
