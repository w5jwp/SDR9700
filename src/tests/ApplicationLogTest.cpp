#include "ApplicationLog.h"

#include <QtTest>

class ApplicationLogTest : public QObject
{
    Q_OBJECT

  private slots:
    void storesFormattedEntriesByCategory();
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

QTEST_GUILESS_MAIN(ApplicationLogTest)
#include "ApplicationLogTest.moc"
