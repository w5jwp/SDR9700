// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QColor>
#include <QHash>
#include <QDialog>
#include <QPointer>
#include <QString>

#include <functional>

class QWidget;
class QLabel;
class QVBoxLayout;
class QShowEvent;
class QScrollArea;
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
        AudioDevices,
        ApplicationConfiguration,
        BandScope,
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
    void bandscopeCenterLineColorChanged(const QColor& color);
    void bandscopeBackgroundColorChanged(const QColor& color);
    void bandscopeGridLineColorChanged(const QColor& color);
    void bandscopeGridDensityChanged(int density);
    void reverseMouseWheelTuningChanged(bool reversed);
    void memoriesChanged(const QString& message);
#ifdef HAVE_HIDAPI
    void icomRC28EncoderSettingsChanged(const QString& field, const QString& value);
#endif

  protected:
    void showEvent(QShowEvent* event) override;

  private:
    QTreeWidgetItem* addCategory(const QString& title, const QString& keywords);
    void addPage(QTreeWidgetItem* parent, Page page, const QString& title, const QString& keywords,
                 std::function<QWidget*()> builder);
    void buildDeferredPage(Page page);
    void filterNavigation(const QString& text);
    void selectPage(Page page);
    void updatePageScrollGutter();
    static QString itemSearchText(QTreeWidgetItem* item);

#ifdef HAVE_HIDAPI
    IcomRC28Manager* m_icomRC28Manager{nullptr};
#endif
    QPointer<QWidget> m_centerHost;
    QTreeWidget* m_navigation{nullptr};
    QScrollArea* m_pageScroll{nullptr};
    QVBoxLayout* m_pageLayout{nullptr};
    QStackedWidget* m_pages{nullptr};
    QLabel* m_pageTitle{nullptr};
    QHash<int, std::function<QWidget*()>> m_deferredBuilders;
    QHash<int, int> m_pageIndexes;
    QHash<int, QTreeWidgetItem*> m_pageItems;
};
