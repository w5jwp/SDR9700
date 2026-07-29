// QtTest invokes private slots through the generated meta-object.
#include "MainWindow.h"
#include "models/RadioModel.h"

#include <QComboBox>
#include <QApplication>
#include <QDialog>
#include <QStandardPaths>
#include <QTableWidget>
#include <QWidget>
#include <QtTest>

class MemoryManagerSmokeTest : public QObject
{
    Q_OBJECT

  private slots:
    void initTestCase();
    void constructsMemoryManagerUi();
};

void MemoryManagerSmokeTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void MemoryManagerSmokeTest::constructsMemoryManagerUi()
{
    RadioModel model;
    MainWindow window(&model);

    QDialog* memoryWindow = nullptr;
    for (QWidget* candidate : QApplication::topLevelWidgets())
    {
        if (candidate->objectName() == QLatin1String("memoryWindow"))
        {
            memoryWindow = qobject_cast<QDialog*>(candidate);
            break;
        }
    }
    QVERIFY(memoryWindow != nullptr);
    auto* table = memoryWindow->findChild<QTableWidget*>(QStringLiteral("memoryManagerTable"));
    auto* editor = memoryWindow->findChild<QWidget*>(QStringLiteral("memoryEditorPane"));
    QVERIFY(table != nullptr);
    QVERIFY(editor != nullptr);
    QCOMPARE(table->columnCount(), 9);
    QVERIFY(!editor->isVisible());
}

QTEST_MAIN(MemoryManagerSmokeTest)

#include "MemoryManagerSmokeTest.moc"
