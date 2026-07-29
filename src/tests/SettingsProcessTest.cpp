#include "AppPaths.h"
#include "AppSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>

namespace
{
int runChild(const QString& mode)
{
    QStandardPaths::setTestModeEnabled(true);
    AppSettings& settings = AppSettings::instance();
    if (mode == QStringLiteral("write"))
    {
        return settings.setValue(QStringLiteral("volumeLevel"), 173) ? 0 : 2;
    }
    if (mode == QStringLiteral("read"))
    {
        return settings.value(QStringLiteral("volumeLevel"), -1).toInt() == 173 ? 0 : 3;
    }
    if (mode == QStringLiteral("deferred"))
    {
        settings.setValueDeferred(QStringLiteral("tuningStepHZ"), 500);
        QTimer::singleShot(350, QCoreApplication::instance(), &QCoreApplication::quit);
        QCoreApplication::exec();
        return QFile::exists(AppSettings::configPath()) ? 0 : 4;
    }
    if (mode == QStringLiteral("default"))
    {
        return settings.value(QStringLiteral("missing"), QStringLiteral("fallback")).toString() ==
                       QStringLiteral("fallback")
                   ? 0
                   : 5;
    }
    return 1;
}

int launchChild(const QString& mode)
{
    QProcess process;
    process.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--settings-child"), mode});
    if (!process.waitForStarted(5000) || !process.waitForFinished(10000))
    {
        return -1;
    }
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -2;
}
} // namespace

class SettingsProcessTest : public QObject
{
    Q_OBJECT

  private slots:
    void initTestCase();
    void persistsAcrossProcesses();
    void flushesDeferredSetting();
    void malformedFileStartsWithDefaults();
    void cleanupTestCase();
};

void SettingsProcessTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QDir(sdr9700::configDirectory()).removeRecursively();
}

void SettingsProcessTest::persistsAcrossProcesses()
{
    QCOMPARE(launchChild(QStringLiteral("write")), 0);
    QCOMPARE(launchChild(QStringLiteral("read")), 0);
}

void SettingsProcessTest::flushesDeferredSetting()
{
    QCOMPARE(launchChild(QStringLiteral("deferred")), 0);
    QFile file(AppSettings::configPath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.readAll().contains("\"tuningStepHZ\": \"500\""));
}

void SettingsProcessTest::malformedFileStartsWithDefaults()
{
    QFile file(AppSettings::configPath());
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write("{not-json"), qint64(9));
    file.close();
    QCOMPARE(launchChild(QStringLiteral("default")), 0);
}

void SettingsProcessTest::cleanupTestCase()
{
    QDir(sdr9700::configDirectory()).removeRecursively();
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() == 3 && arguments.at(1) == QStringLiteral("--settings-child"))
    {
        return runChild(arguments.at(2));
    }
    SettingsProcessTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "SettingsProcessTest.moc"
