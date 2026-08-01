#include "ApplicationLog.h"

#include <QtTest>

class ApplicationLogTest : public QObject
{
    Q_OBJECT

  private slots:
    void storesFormattedEntriesByCategory();
    void providesIncrementalSnapshots();
    void removesCategoriesWhenTheirEntriesExpire();
};

void ApplicationLogTest::storesFormattedEntriesByCategory()
{
    ApplicationLog& log = ApplicationLog::instance();
    log.clear();

    const QMessageLogContext radioContext("test.cpp", 12, "test", "radio");
    const QMessageLogContext audioContext("test.cpp", 13, "test", "audio");
    log.append(QtInfoMsg, radioContext, QStringLiteral("Radio connected"));
    log.append(QtWarningMsg, audioContext, QStringLiteral("Audio stopped"));

    const auto entries = log.entries();
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0).category, QStringLiteral("radio"));
    QVERIFY(entries.at(0).text.contains(QStringLiteral("INFO [radio] Radio connected")));
    QCOMPARE(entries.at(1).category, QStringLiteral("audio"));
    QVERIFY(entries.at(1).text.contains(QStringLiteral("WARN [audio] Audio stopped")));
    QCOMPARE(log.categories(), QStringList({QStringLiteral("audio"), QStringLiteral("radio")}));
}

void ApplicationLogTest::providesIncrementalSnapshots()
{
    ApplicationLog& log = ApplicationLog::instance();
    log.clear();
    const QMessageLogContext radioContext("test.cpp", 12, "test", "radio");
    const QMessageLogContext audioContext("test.cpp", 13, "test", "audio");
    log.append(QtInfoMsg, radioContext, QStringLiteral("first"));

    bool resetRequired = false;
    quint64 sequence = 0;
    QStringList categories;
    QVector<ApplicationLog::Entry> entries = log.entriesAfter(0, QString(), &resetRequired, &sequence, &categories);
    QCOMPARE(entries.size(), 1);
    QVERIFY(!resetRequired);
    QCOMPARE(categories, QStringList({QStringLiteral("radio")}));

    log.append(QtInfoMsg, audioContext, QStringLiteral("second"));
    entries = log.entriesAfter(sequence, QStringLiteral("audio"), &resetRequired, &sequence, &categories);
    QCOMPARE(entries.size(), 1);
    QVERIFY(entries.constFirst().text.endsWith(QStringLiteral("second")));
    QCOMPARE(categories, QStringList({QStringLiteral("audio"), QStringLiteral("radio")}));
}

void ApplicationLogTest::removesCategoriesWhenTheirEntriesExpire()
{
    ApplicationLog& log = ApplicationLog::instance();
    log.clear();
    const QMessageLogContext retiredContext("test.cpp", 12, "test", "retired");
    const QMessageLogContext currentContext("test.cpp", 13, "test", "current");
    const QString largeMessage(3 * 1024 * 1024, QLatin1Char('x'));
    log.append(QtInfoMsg, retiredContext, largeMessage);
    log.append(QtInfoMsg, currentContext, largeMessage);

    QCOMPARE(log.entries().size(), 1);
    QCOMPARE(log.categories(), QStringList({QStringLiteral("current")}));
}

QTEST_GUILESS_MAIN(ApplicationLogTest)
#include "ApplicationLogTest.moc"
