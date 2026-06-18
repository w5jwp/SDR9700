// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QString>
#include <QUuid>
#include <QList>
#include <QObject>

struct RadioProfile
{
    QUuid id;
    QString name;
    QString host;
    quint16 port{50001};
    QString username;
    QString password; // plaintext in memory; encrypted at rest
};

// Passwords are encrypted at rest with AES-256-GCM using a key derived from
// local Linux machine/user material. Plaintext is kept only in memory while the
// profile is loaded or edited.
class RadioProfileStore : public QObject
{
    Q_OBJECT

  public:
    static RadioProfileStore& instance();

    void load();
    bool save() const;

    const QList<RadioProfile>& profiles() const { return m_profiles; }
    const RadioProfile* profileById(const QUuid& id) const;

    bool addProfile(const RadioProfile& p);
    bool updateProfile(const RadioProfile& p);
    bool removeProfile(const QUuid& id);

    QUuid lastProfileId() const { return m_lastProfileId; }
    bool setLastProfileId(const QUuid& id);

  private:
    RadioProfileStore() = default;

    static QByteArray legacyMachineKey();
    static QByteArray passwordKeyMaterial();
    static QString encryptPassword(const QString& plain);
    static QString decryptPassword(const QString& stored, bool* usedLegacyKey);
    static QString decryptLegacyPassword(const QString& stored);

    QList<RadioProfile> m_profiles;
    QUuid m_lastProfileId;
};
