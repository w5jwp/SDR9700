#include "SettingsDialog.h"
#include "AudioSettingsPanel.h"
#include "DialogPlacement.h"
#include "FramelessTitleBar.h"
#include "MouseSettingsPanel.h"
#include "RadioSetupSettingsPanel.h"
#ifdef HAVE_HIDAPI
#include "IcomRC28SettingsPanel.h"
#endif

#include <QAbstractItemView>
#include <QAction>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace
{
int pageKey(SettingsDialog::Page page)
{
    return static_cast<int>(page);
}
} // namespace

SettingsDialog::SettingsDialog(QWidget* parent) : SettingsDialog(Page::RadioSetup, parent) {}

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
    m_navigation->setRootIsDecorated(false);
    m_navigation->setIndentation(0);
    m_navigation->setMinimumWidth(190);
    m_navigation->setMaximumWidth(245);
    m_navigation->setUniformRowHeights(false);
    m_navigation->setAccessibleName(QStringLiteral("Settings pages"));
    body->addWidget(m_navigation);

    auto* divider = new QWidget(content);
    divider->setFixedWidth(1);
    divider->setStyleSheet(QStringLiteral("background: %1;").arg(QLatin1String(UiTheme::Color::Border)));
    body->addWidget(divider);

    auto* pageHost = new QWidget(content);
    auto* pageLayout = new QVBoxLayout(pageHost);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(8);
    m_pageTitle = new QLabel(pageHost);
    m_pageTitle->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 16px; font-weight: bold; }")
                                   .arg(QLatin1String(UiTheme::Color::TextPrimary)));
    pageLayout->addWidget(m_pageTitle);
    m_pages = new QStackedWidget(pageHost);
    pageLayout->addWidget(m_pages, 1);
    body->addWidget(pageHost, 1);
    contentLayout->addLayout(body, 1);

    addPage(Page::RadioSetup, QStringLiteral("Radio Setup"),
            QStringLiteral("profile connection host port username password auto connect radio lan"),
            []() { return new RadioSetupSettingsPanel; });
    addPage(Page::Audio, QStringLiteral("Audio"),
            QStringLiteral("audio input output device speaker microphone codec channels lan input mod level transmit"),
            [this]()
            {
                auto* panel = new AudioSettingsPanel;
                m_audioPanel = panel;
                panel->setTransmitAudioLevel(m_txAudioPeak, m_txAudioRms);
                connect(panel, &AudioSettingsPanel::lanModLevelChanged, this, &SettingsDialog::lanModLevelChanged);
                return panel;
            });
    addPage(Page::Application, QStringLiteral("Mouse"),
            QStringLiteral("application local mouse wheel reverse tuning direction"),
            [this]()
            {
                auto* panel = new MouseSettingsPanel;
                connect(panel, &MouseSettingsPanel::reverseMouseWheelTuningChanged, this,
                        &SettingsDialog::reverseMouseWheelTuningChanged);
                return panel;
            });
#ifdef HAVE_HIDAPI
    addPage(Page::IcomRC28, QStringLiteral("Icom RC-28 Remote Encoder"),
            QStringLiteral("hardware icom icomRC28 rc-28 remote encoder controller knob button f1 f2 ptt mapping"),
            [this]()
            {
                auto* panel = new IcomRC28SettingsPanel(m_icomRC28Manager);
                connect(panel, &IcomRC28SettingsPanel::encoderSettingsChanged, this,
                        &SettingsDialog::icomRC28EncoderSettingsChanged);
                return panel;
            });
#endif
    m_navigation->sortItems(0, Qt::AscendingOrder);

    connect(m_navigation, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current)
            {
                if (!current)
                {
                    return;
                }

                const auto selectedPage = static_cast<Page>(current->data(0, Qt::UserRole).toInt());
                buildDeferredPage(selectedPage);
                m_pages->setCurrentIndex(m_pageIndexes.value(pageKey(selectedPage)));
                m_pageTitle->setText(current->text(0));
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

void SettingsDialog::setTransmitAudioLevel(int peak, int rms)
{
    m_txAudioPeak = qBound(0, peak, 255);
    m_txAudioRms = qBound(0, rms, 255);
    if (m_audioPanel)
    {
        m_audioPanel->setTransmitAudioLevel(m_txAudioPeak, m_txAudioRms);
    }
}

void SettingsDialog::addPage(Page page, const QString& title, const QString& keywords,
                             std::function<QWidget*()> builder)
{
    auto* placeholder = new QWidget;
    const int stackIndex = m_pages->addWidget(placeholder);
    const int key = pageKey(page);

    auto* item = new QTreeWidgetItem(m_navigation, {title});
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
        const QString haystack = item->text(0) + QLatin1Char(' ') + item->data(0, Qt::UserRole + 1).toString();
        const bool visible = needle.isEmpty() || haystack.contains(needle, Qt::CaseInsensitive);
        item->setHidden(!visible);
        if (visible && !firstVisible)
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
