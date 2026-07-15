#include "RadioProfile.h"
#include "AppSettings.h"
#include "LogCategories.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QDebug>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>

namespace
{
constexpr auto kEncryptedPasswordPrefix = "v2:";
constexpr int kPasswordSaltBytes = 16;
constexpr int kPasswordNonceBytes = 12;
constexpr int kPasswordTagBytes = 16;
constexpr int kPasswordKeyBytes = 32;
constexpr int kPasswordKdfIterations = 210000;
constexpr auto kPasswordKeyFileName = "profile-key.bin";

QByteArray randomBytes(int size)
{
    QByteArray bytes(size, Qt::Uninitialized);
    if (RAND_bytes(reinterpret_cast<unsigned char*>(bytes.data()), size) != 1)
    {
        return {};
    }
    return bytes;
}

QByteArray derivePasswordKey(const QByteArray& material, const QByteArray& salt)
{
    QByteArray key(kPasswordKeyBytes, Qt::Uninitialized);
    if (PKCS5_PBKDF2_HMAC(material.constData(), material.size(),
                          reinterpret_cast<const unsigned char*>(salt.constData()), salt.size(), kPasswordKdfIterations,
                          EVP_sha256(), key.size(), reinterpret_cast<unsigned char*>(key.data())) != 1)
    {
        return {};
    }
    return key;
}

QString profileKeyPath()
{
    // Store the SDR9700 profile key beside other app-owned settings under
    // ~/.config (QStandardPaths::GenericConfigLocation) because it is part of
    // this application's configuration, not user content. We intentionally do
    // not depend on libsecret/KWallet yet: those services vary by Linux desktop
    // environment, while SDR9700 needs deterministic headless/test behavior and
    // no additional runtime package requirement. The tradeoff is explicit: if
    // an attacker obtains both sdr9700.json and this owner-readable key file,
    // saved radio passwords can be decrypted offline.
    QString configRoot = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (configRoot.isEmpty())
    {
        configRoot = QDir::homePath() + "/.config";
    }
    return QDir(configRoot).filePath(QStringLiteral("SDR9700/%1").arg(QString::fromLatin1(kPasswordKeyFileName)));
}

void secureZero(QByteArray& data)
{
    if (!data.isEmpty())
    {
        OPENSSL_cleanse(data.data(), static_cast<size_t>(data.size()));
        data.clear();
    }
}

QByteArray readOrCreateProfileKey()
{
    const QString path = profileKeyPath();
    QFile keyFile(path);
    if (keyFile.open(QIODevice::ReadOnly))
    {
        const QByteArray key = keyFile.readAll();
        if (key.size() >= kPasswordKeyBytes)
        {
            if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner))
            {
                qWarning(logSystem()) << "Could not tighten profile key permissions for" << path;
            }
            return key.left(kPasswordKeyBytes);
        }
    }

    const QByteArray key = randomBytes(kPasswordKeyBytes);
    if (key.isEmpty())
    {
        return {};
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile saveFile(path);
    if (!saveFile.open(QIODevice::WriteOnly))
    {
        return {};
    }
    if (saveFile.write(key) != key.size() || !saveFile.commit())
    {
        return {};
    }
    if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner))
    {
        qWarning(logSystem()) << "Could not tighten profile key permissions for" << path;
    }
    return key;
}
} // namespace

RadioProfileStore& RadioProfileStore::instance()
{
    static RadioProfileStore s;
    return s;
}

QByteArray RadioProfileStore::passwordKeyMaterial()
{
    // Current profile encryption key scheme: a per-user random secret stored in
    // the SDR9700 config directory, domain-separated for AES-GCM radio-profile
    // password encryption, then combined with each record's random salt. The
    // key file is protected with owner-only permissions but remains an app-local
    // secret, not a system-keyring secret; see profileKeyPath() for the accepted
    // risk and portability rationale.
    QByteArray material = readOrCreateProfileKey();
    if (material.isEmpty())
    {
        return {};
    }
    material += "|SDR9700-radio-profiles-aes-gcm";
    return QCryptographicHash::hash(material, QCryptographicHash::Sha256);
}

QString RadioProfileStore::encryptPassword(const QString& plain)
{
    if (plain.isEmpty())
    {
        return {};
    }

    const QByteArray salt = randomBytes(kPasswordSaltBytes);
    const QByteArray nonce = randomBytes(kPasswordNonceBytes);
    const QByteArray key = derivePasswordKey(passwordKeyMaterial(), salt);
    if (salt.isEmpty() || nonce.isEmpty() || key.isEmpty())
    {
        return {};
    }

    const QByteArray plaintext = plain.toUtf8();
    QByteArray ciphertext(plaintext.size(), Qt::Uninitialized);
    std::array<unsigned char, kPasswordTagBytes> tag{};

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
    {
        return {};
    }

    int len = 0;
    int ciphertextLen = 0;
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1;
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) == 1;
    ok = ok && EVP_EncryptInit_ex(ctx, nullptr, nullptr, reinterpret_cast<const unsigned char*>(key.constData()),
                                  reinterpret_cast<const unsigned char*>(nonce.constData())) == 1;
    ok = ok && EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(ciphertext.data()), &len,
                                 reinterpret_cast<const unsigned char*>(plaintext.constData()), plaintext.size()) == 1;
    ciphertextLen = len;
    ok = ok && EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(ciphertext.data()) + ciphertextLen, &len) == 1;
    ciphertextLen += len;
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, tag.size(), tag.data()) == 1;
    EVP_CIPHER_CTX_free(ctx);

    if (!ok)
    {
        return {};
    }

    ciphertext.resize(ciphertextLen);
    const QByteArray payload =
        salt + nonce + QByteArray(reinterpret_cast<const char*>(tag.data()), tag.size()) + ciphertext;
    return QString::fromLatin1(kEncryptedPasswordPrefix) + QString::fromLatin1(payload.toBase64());
}

static bool tryDecryptAesGcm(const QByteArray& key, const QByteArray& nonce, const QByteArray& tag,
                             const QByteArray& ciphertext, QByteArray& plaintext)
{
    plaintext.resize(ciphertext.size());
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
    {
        return false;
    }
    int len = 0;
    int plaintextLen = 0;
    bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1;
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) == 1;
    ok = ok && EVP_DecryptInit_ex(ctx, nullptr, nullptr, reinterpret_cast<const unsigned char*>(key.constData()),
                                  reinterpret_cast<const unsigned char*>(nonce.constData())) == 1;
    ok =
        ok && EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(plaintext.data()), &len,
                                reinterpret_cast<const unsigned char*>(ciphertext.constData()), ciphertext.size()) == 1;
    plaintextLen = len;
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, tag.size(), const_cast<char*>(tag.constData())) == 1;
    ok = ok && EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(plaintext.data()) + plaintextLen, &len) == 1;
    plaintextLen += len;
    EVP_CIPHER_CTX_free(ctx);
    if (ok)
    {
        plaintext.resize(plaintextLen);
    }
    return ok;
}

QString RadioProfileStore::decryptPassword(const QString& stored)
{
    if (stored.isEmpty())
    {
        return {};
    }
    if (!stored.startsWith(QString::fromLatin1(kEncryptedPasswordPrefix)))
    {
        return {};
    }

    const QByteArray payload =
        QByteArray::fromBase64(stored.mid(QString::fromLatin1(kEncryptedPasswordPrefix).size()).toLatin1());
    const int headerBytes = kPasswordSaltBytes + kPasswordNonceBytes + kPasswordTagBytes;
    if (payload.size() <= headerBytes)
    {
        return {};
    }

    const QByteArray salt = payload.mid(0, kPasswordSaltBytes);
    const QByteArray nonce = payload.mid(kPasswordSaltBytes, kPasswordNonceBytes);
    const QByteArray tag = payload.mid(kPasswordSaltBytes + kPasswordNonceBytes, kPasswordTagBytes);
    const QByteArray ciphertext = payload.mid(headerBytes);
    QByteArray key = derivePasswordKey(passwordKeyMaterial(), salt);
    if (key.isEmpty())
    {
        secureZero(key);
        return {};
    }

    QByteArray plaintext;
    if (!tryDecryptAesGcm(key, nonce, tag, ciphertext, plaintext))
    {
        secureZero(key);
        secureZero(plaintext);
        return {};
    }

    QString decrypted = QString::fromUtf8(plaintext);
    secureZero(key);
    secureZero(plaintext);
    return decrypted;
}

void RadioProfileStore::load()
{
    m_profiles.clear();
    AppSettings& settings = AppSettings::instance();
    const QString stored = settings.value("radioProfiles", "{}").toString();
    const QJsonDocument doc = QJsonDocument::fromJson(stored.toUtf8());
    const QJsonObject root = doc.object();

    m_lastProfileId = QUuid(root.value("lastProfileID").toString());

    const QJsonArray profileArray = root.value("profiles").toArray();
    for (const QJsonValue& value : profileArray)
    {
        const QJsonObject obj = value.toObject();
        RadioProfile p;
        p.id = QUuid(obj.value("ID").toString());
        p.name = obj.value("name").toString();
        p.host = obj.value("host").toString();
        p.port = static_cast<quint16>(obj.value("port").toInt(50001));
        p.username = obj.value("username").toString();
        const QString storedPassword = obj.value("password").toString();
        p.password = decryptPassword(storedPassword);
        if (!storedPassword.isEmpty() && p.password.isEmpty())
        {
            qWarning(logSystem()) << "Skipping radio profile with unreadable encrypted password:" << p.name;
            continue;
        }
        if (!p.id.isNull() && !p.host.isEmpty())
        {
            m_profiles.append(p);
        }
    }
}

bool RadioProfileStore::save() const
{
    QJsonObject root;
    root.insert("lastProfileID", m_lastProfileId.toString());

    QJsonArray profileArray;
    for (const RadioProfile& p : m_profiles)
    {
        QJsonObject obj;
        obj.insert("ID", p.id.toString());
        obj.insert("name", p.name);
        obj.insert("host", p.host);
        obj.insert("port", static_cast<int>(p.port));
        obj.insert("username", p.username);
        const QString encryptedPassword = encryptPassword(p.password);
        if (!p.password.isEmpty() && encryptedPassword.isEmpty())
        {
            qWarning(logSystem()) << "Radio profile password encryption failed; refusing to overwrite saved profiles";
            return false;
        }
        obj.insert("password", encryptedPassword);
        profileArray.append(obj);
    }
    root.insert("profiles", profileArray);

    return AppSettings::instance().setValue("radioProfiles",
                                            QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}

const RadioProfile* RadioProfileStore::profileById(const QUuid& id) const
{
    const auto it = std::find_if(m_profiles.cbegin(), m_profiles.cend(),
                                 [&id](const RadioProfile& profile) { return profile.id == id; });
    return it != m_profiles.cend() ? &(*it) : nullptr;
}

bool RadioProfileStore::addProfile(const RadioProfile& p)
{
    m_profiles.append(p);
    if (!save())
    {
        m_profiles.removeLast();
        return false;
    }
    return true;
}

bool RadioProfileStore::updateProfile(const RadioProfile& p)
{
    for (RadioProfile& existing : m_profiles)
    {
        if (existing.id == p.id)
        {
            const RadioProfile previous = existing;
            existing = p;
            if (!save())
            {
                existing = previous;
                return false;
            }
            return true;
        }
    }
    return false;
}

bool RadioProfileStore::removeProfile(const QUuid& id)
{
    const QList<RadioProfile> previousProfiles = m_profiles;
    const QUuid previousLastProfileId = m_lastProfileId;
    m_profiles.removeIf([&](const RadioProfile& p) { return p.id == id; });
    if (m_lastProfileId == id)
    {
        m_lastProfileId = QUuid();
    }
    if (!save())
    {
        m_profiles = previousProfiles;
        m_lastProfileId = previousLastProfileId;
        return false;
    }
    return true;
}

bool RadioProfileStore::setLastProfileId(const QUuid& id)
{
    const QUuid previousLastProfileId = m_lastProfileId;
    m_lastProfileId = id;
    if (!save())
    {
        m_lastProfileId = previousLastProfileId;
        return false;
    }
    return true;
}
