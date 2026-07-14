// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QHash>
#include <QDialog>
#include <QPointer>

#include <functional>

class QWidget;
class QLabel;
class QShowEvent;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
#ifdef HAVE_HIDAPI
class IcomRC28Manager;
#endif

class SettingsDialog : public QDialog
{
    Q_OBJECT

  public:
    enum class Page
    {
        RadioSetup,
        Audio,
        Application,
#ifdef HAVE_HIDAPI
        IcomRC28,
#endif
    };

    explicit SettingsDialog(QWidget* parent = nullptr);
#ifdef HAVE_HIDAPI
    explicit SettingsDialog(Page page, QWidget* parent = nullptr, IcomRC28Manager* icomRC28Manager = nullptr);
#else
    explicit SettingsDialog(Page page, QWidget* parent = nullptr);
#endif

  signals:
    void lanModLevelChanged(int level);
    void reverseMouseWheelTuningChanged(bool reversed);
#ifdef HAVE_HIDAPI
    void icomRC28EncoderSettingsChanged(const QString& field, const QString& value);
#endif

  protected:
    void showEvent(QShowEvent* event) override;

  private:
    void addPage(Page page, const QString& title, const QString& keywords, std::function<QWidget*()> builder);
    void buildDeferredPage(Page page);
    void filterNavigation(const QString& text);
    void selectPage(Page page);

#ifdef HAVE_HIDAPI
    IcomRC28Manager* m_icomRC28Manager{nullptr};
#endif
    QPointer<QWidget> m_centerHost;
    QTreeWidget* m_navigation{nullptr};
    QStackedWidget* m_pages{nullptr};
    QLabel* m_pageTitle{nullptr};
    QHash<int, std::function<QWidget*()>> m_deferredBuilders;
    QHash<int, int> m_pageIndexes;
    QHash<int, QTreeWidgetItem*> m_pageItems;
};
