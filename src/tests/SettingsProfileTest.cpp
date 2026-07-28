// QtTest invokes private slots through the generated meta-object.
#include "AppPaths.h"
#include "AppSettings.h"
#include "RadioProfile.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QtTest>

class SettingsProfileTest : public QObject
{
    Q_OBJECT

  private slots:
    void initTestCase();
    void cleanupTestCase();
    void storesSettingsInCurrentSchema();
    void profilePasswordIsEncryptedAndRoundTrips();
    void corruptedProfilePasswordIsRejected();

  private:
    static QJsonObject settingsDocument();
};

void SettingsProfileTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QDir(sdr9700::configDirectory()).removeRecursively();
    RadioProfileStore::instance().load();
}

void SettingsProfileTest::cleanupTestCase()
{
    QDir(sdr9700::configDirectory()).removeRecursively();
}

QJsonObject SettingsProfileTest::settingsDocument()
{
    QFile file(AppSettings::configPath());
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

void SettingsProfileTest::storesSettingsInCurrentSchema()
{
    AppSettings& settings = AppSettings::instance();
    QVERIFY(settings.setValue(QStringLiteral("autoConnect"), true));
    QVERIFY(settings.setValue(QStringLiteral("audioOutputChannels"), 2));
    QVERIFY(settings.setValue(QStringLiteral("spectrumScopeSpanHZ"), 500000));
    QVERIFY(settings.setValue(QStringLiteral("tuningStepHZ"), 100));

    const QJsonObject root = settingsDocument();
    QCOMPARE(root.value(QStringLiteral("radioChooser")).toObject().value(QStringLiteral("autoConnect")).toString(),
             QStringLiteral("True"));
    QCOMPARE(root.value(QStringLiteral("audio")).toObject().value(QStringLiteral("outputChannels")).toString(),
             QStringLiteral("2"));
    QCOMPARE(root.value(QStringLiteral("spectrumScope")).toObject().value(QStringLiteral("spanHZ")).toString(),
             QStringLiteral("500000"));
    QCOMPARE(root.value(QStringLiteral("radio")).toObject().value(QStringLiteral("tuningStepHZ")).toString(),
             QStringLiteral("100"));
    QVERIFY(!root.contains(QStringLiteral("autoConnect")));
}

void SettingsProfileTest::profilePasswordIsEncryptedAndRoundTrips()
{
    RadioProfileStore& store = RadioProfileStore::instance();
    const RadioProfile profile{
        QUuid::createUuid(),        QStringLiteral("Test Radio"),      QStringLiteral("192.0.2.10"), 50001,
        QStringLiteral("operator"), QStringLiteral("secret-password"),
    };

    QVERIFY(store.addProfile(profile));

    const QJsonObject root = settingsDocument();
    const QJsonObject profiles =
        root.value(QStringLiteral("radioChooser")).toObject().value(QStringLiteral("radioProfiles")).toObject();
    const QJsonObject storedProfile = profiles.value(QStringLiteral("profiles")).toArray().at(0).toObject();
    const QString storedPassword = storedProfile.value(QStringLiteral("password")).toString();
    QVERIFY(storedPassword.startsWith(QStringLiteral("v2:")));
    QVERIFY(!storedPassword.contains(profile.password));
    QVERIFY(!QJsonDocument(root).toJson().contains(profile.password.toUtf8()));

    store.load();
    const RadioProfile* loaded = store.profileById(profile.id);
    QVERIFY(loaded != nullptr);
    QCOMPARE(loaded->name, profile.name);
    QCOMPARE(loaded->host, profile.host);
    QCOMPARE(loaded->port, profile.port);
    QCOMPARE(loaded->username, profile.username);
    QCOMPARE(loaded->password, profile.password);
}

void SettingsProfileTest::corruptedProfilePasswordIsRejected()
{
    AppSettings& settings = AppSettings::instance();
    QJsonObject profiles = settingsDocument()
                               .value(QStringLiteral("radioChooser"))
                               .toObject()
                               .value(QStringLiteral("radioProfiles"))
                               .toObject();
    QJsonArray profileArray = profiles.value(QStringLiteral("profiles")).toArray();
    QJsonObject profile = profileArray.at(0).toObject();
    QString encrypted = profile.value(QStringLiteral("password")).toString();
    encrypted[encrypted.size() - 1] = encrypted.endsWith(QLatin1Char('A')) ? QLatin1Char('B') : QLatin1Char('A');
    profile.insert(QStringLiteral("password"), encrypted);
    profileArray[0] = profile;
    profiles.insert(QStringLiteral("profiles"), profileArray);
    QVERIFY(settings.setValue(QStringLiteral("radioProfiles"),
                              QString::fromUtf8(QJsonDocument(profiles).toJson(QJsonDocument::Compact))));

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Skipping radio profile.*")));
    RadioProfileStore::instance().load();
    QVERIFY(RadioProfileStore::instance().profiles().isEmpty());
}

QTEST_GUILESS_MAIN(SettingsProfileTest)
#include "SettingsProfileTest.moc"
