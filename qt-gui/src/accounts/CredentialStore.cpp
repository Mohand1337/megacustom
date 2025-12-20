#include "CredentialStore.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QSysInfo>
#include <QDebug>
#include <QRandomGenerator>

// OpenSSL for AES-256-GCM encryption
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>

// Try to include QtKeychain if available
#ifdef HAVE_QTKEYCHAIN
#include <qt6keychain/keychain.h>
#endif

namespace MegaCustom {

const QString CredentialStore::SERVICE_NAME = "MegaCustomApp";

CredentialStore::CredentialStore(QObject* parent)
    : QObject(parent)
    , m_useSecureStorage(false)
    , m_fallbackLoaded(false)
{
#ifdef HAVE_QTKEYCHAIN
    // Check if QtKeychain is functional
    m_useSecureStorage = true;
    qDebug() << "CredentialStore: Using OS secure storage (QtKeychain)";
#else
    // Use fallback encrypted file storage
    m_useSecureStorage = false;
    qDebug() << "CredentialStore: Using encrypted file storage (fallback)";
    initializeFallbackStorage();
#endif
}

CredentialStore::~CredentialStore()
{
    // Save any pending changes to fallback storage
    if (!m_useSecureStorage && m_fallbackLoaded) {
        saveFallbackStorage();
    }
}

bool CredentialStore::isSecureStorageAvailable() const
{
    return m_useSecureStorage;
}

void CredentialStore::saveSession(const QString& accountId, const QString& sessionToken)
{
    if (accountId.isEmpty() || sessionToken.isEmpty()) {
        emit error(accountId, "Invalid account ID or session token");
        return;
    }

#ifdef HAVE_QTKEYCHAIN
    if (m_useSecureStorage) {
        auto* job = new QKeychain::WritePasswordJob(SERVICE_NAME, this);
        job->setKey(accountId);
        job->setTextData(sessionToken);

        connect(job, &QKeychain::Job::finished, this, [this, accountId](QKeychain::Job* job) {
            if (job->error() == QKeychain::NoError) {
                emit sessionSaved(accountId, true);
            } else {
                qWarning() << "CredentialStore: Failed to save session:" << job->errorString();
                emit error(accountId, job->errorString());
                emit sessionSaved(accountId, false);
            }
            job->deleteLater();
        });

        job->start();
        return;
    }
#endif

    // Fallback: encrypted file storage
    m_sessionCache[accountId] = sessionToken;
    bool success = saveFallbackStorage();
    emit sessionSaved(accountId, success);
    if (!success) {
        emit error(accountId, "Failed to save session to encrypted storage");
    }
}

void CredentialStore::loadSession(const QString& accountId)
{
    if (accountId.isEmpty()) {
        emit error(accountId, "Invalid account ID");
        return;
    }

#ifdef HAVE_QTKEYCHAIN
    if (m_useSecureStorage) {
        auto* job = new QKeychain::ReadPasswordJob(SERVICE_NAME, this);
        job->setKey(accountId);

        connect(job, &QKeychain::Job::finished, this, [this, accountId](QKeychain::Job* job) {
            auto* readJob = qobject_cast<QKeychain::ReadPasswordJob*>(job);
            if (job->error() == QKeychain::NoError && readJob) {
                emit sessionLoaded(accountId, readJob->textData());
            } else if (job->error() == QKeychain::EntryNotFound) {
                emit error(accountId, "Session not found");
            } else {
                qWarning() << "CredentialStore: Failed to load session:" << job->errorString();
                emit error(accountId, job->errorString());
            }
            job->deleteLater();
        });

        job->start();
        return;
    }
#endif

    // Fallback: encrypted file storage
    if (!m_fallbackLoaded) {
        loadFallbackStorage();
    }

    if (m_sessionCache.contains(accountId)) {
        emit sessionLoaded(accountId, m_sessionCache[accountId]);
    } else {
        emit error(accountId, "Session not found");
    }
}

void CredentialStore::deleteSession(const QString& accountId)
{
    if (accountId.isEmpty()) {
        emit error(accountId, "Invalid account ID");
        return;
    }

#ifdef HAVE_QTKEYCHAIN
    if (m_useSecureStorage) {
        auto* job = new QKeychain::DeletePasswordJob(SERVICE_NAME, this);
        job->setKey(accountId);

        connect(job, &QKeychain::Job::finished, this, [this, accountId](QKeychain::Job* job) {
            if (job->error() == QKeychain::NoError || job->error() == QKeychain::EntryNotFound) {
                emit sessionDeleted(accountId);
            } else {
                qWarning() << "CredentialStore: Failed to delete session:" << job->errorString();
                emit error(accountId, job->errorString());
            }
            job->deleteLater();
        });

        job->start();
        return;
    }
#endif

    // Fallback: encrypted file storage
    m_sessionCache.remove(accountId);
    saveFallbackStorage();
    emit sessionDeleted(accountId);
}

bool CredentialStore::hasSession(const QString& accountId) const
{
#ifdef HAVE_QTKEYCHAIN
    if (m_useSecureStorage) {
        // For keychain, we can't synchronously check - caller should use loadSession()
        // This is a limitation; for UI purposes, we maintain a local cache
        return false;
    }
#endif

    return m_sessionCache.contains(accountId);
}

QStringList CredentialStore::storedAccountIds() const
{
    return m_sessionCache.keys();
}

void CredentialStore::clearAll()
{
#ifdef HAVE_QTKEYCHAIN
    if (m_useSecureStorage) {
        // Delete each stored session
        for (const QString& accountId : m_sessionCache.keys()) {
            deleteSession(accountId);
        }
        m_sessionCache.clear();
        return;
    }
#endif

    // Fallback: clear cache and file
    m_sessionCache.clear();
    saveFallbackStorage();
}

// ============================================================================
// Fallback Encrypted File Storage
// ============================================================================

void CredentialStore::initializeFallbackStorage()
{
    m_encryptionKey = generateMachineKey();
    loadFallbackStorage();
}

QString CredentialStore::getFallbackFilePath() const
{
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QDir dir(configPath);
    if (!dir.exists("MegaCustom")) {
        dir.mkpath("MegaCustom");
    }
    return configPath + "/MegaCustom/.sessions.enc";
}

bool CredentialStore::saveFallbackStorage()
{
    QJsonObject root;
    for (auto it = m_sessionCache.constBegin(); it != m_sessionCache.constEnd(); ++it) {
        root[it.key()] = encrypt(it.value());
    }

    QJsonDocument doc(root);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

    QFile file(getFallbackFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "CredentialStore: Cannot open file for writing:" << file.errorString();
        return false;
    }

    file.write(data);
    file.close();

    // Set restrictive permissions (owner read/write only)
#ifndef Q_OS_WIN
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
#endif

    return true;
}

bool CredentialStore::loadFallbackStorage()
{
    m_fallbackLoaded = true;
    m_sessionCache.clear();

    QFile file(getFallbackFilePath());
    if (!file.exists()) {
        return true; // No sessions stored yet
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "CredentialStore: Cannot open file for reading:" << file.errorString();
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "CredentialStore: JSON parse error:" << parseError.errorString();
        return false;
    }

    QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        QString decrypted = decrypt(it.value().toString());
        if (!decrypted.isEmpty()) {
            m_sessionCache[it.key()] = decrypted;
        }
    }

    return true;
}

QString CredentialStore::encrypt(const QString& plaintext) const
{
    if (plaintext.isEmpty() || m_encryptionKey.isEmpty()) {
        return QString();
    }

    // Use AES-256-GCM for proper authenticated encryption
    static constexpr int KEY_SIZE = 32;  // 256 bits
    static constexpr int IV_SIZE = 12;   // 96 bits for GCM
    static constexpr int TAG_SIZE = 16;  // 128 bits

    // Derive 32-byte key from the machine key
    QByteArray key = QCryptographicHash::hash(m_encryptionKey.toUtf8(), QCryptographicHash::Sha256);

    // Generate random IV
    QByteArray iv(IV_SIZE, 0);
    if (RAND_bytes(reinterpret_cast<unsigned char*>(iv.data()), IV_SIZE) != 1) {
        qWarning() << "CredentialStore: Failed to generate random IV";
        return QString();
    }

    QByteArray plain = plaintext.toUtf8();

    // Create cipher context
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        qWarning() << "CredentialStore: Failed to create cipher context";
        return QString();
    }

    // Initialize encryption
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                           reinterpret_cast<const unsigned char*>(key.constData()),
                           reinterpret_cast<const unsigned char*>(iv.constData())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        qWarning() << "CredentialStore: Failed to initialize encryption";
        return QString();
    }

    // Encrypt
    QByteArray ciphertext(plain.size() + EVP_MAX_BLOCK_LENGTH, 0);
    int len = 0;
    int ciphertextLen = 0;

    if (EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(ciphertext.data()), &len,
                          reinterpret_cast<const unsigned char*>(plain.constData()),
                          plain.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        qWarning() << "CredentialStore: Encryption failed";
        return QString();
    }
    ciphertextLen = len;

    if (EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(ciphertext.data()) + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        qWarning() << "CredentialStore: Encryption finalization failed";
        return QString();
    }
    ciphertextLen += len;
    ciphertext.resize(ciphertextLen);

    // Get authentication tag
    QByteArray tag(TAG_SIZE, 0);
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_SIZE, tag.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        qWarning() << "CredentialStore: Failed to get auth tag";
        return QString();
    }

    EVP_CIPHER_CTX_free(ctx);

    // Combine: IV + ciphertext + tag and encode as base64
    QByteArray combined = iv + ciphertext + tag;
    return QString::fromLatin1(combined.toBase64());
}

QString CredentialStore::decrypt(const QString& ciphertext) const
{
    if (ciphertext.isEmpty() || m_encryptionKey.isEmpty()) {
        return QString();
    }

    static constexpr int KEY_SIZE = 32;
    static constexpr int IV_SIZE = 12;
    static constexpr int TAG_SIZE = 16;

    // Decode from base64
    QByteArray combined = QByteArray::fromBase64(ciphertext.toLatin1());

    if (combined.size() < IV_SIZE + TAG_SIZE) {
        qWarning() << "CredentialStore: Ciphertext too short";
        return QString();
    }

    // Extract IV, ciphertext, and tag
    QByteArray iv = combined.left(IV_SIZE);
    QByteArray tag = combined.right(TAG_SIZE);
    QByteArray cipher = combined.mid(IV_SIZE, combined.size() - IV_SIZE - TAG_SIZE);

    // Derive key
    QByteArray key = QCryptographicHash::hash(m_encryptionKey.toUtf8(), QCryptographicHash::Sha256);

    // Create cipher context
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        qWarning() << "CredentialStore: Failed to create cipher context";
        return QString();
    }

    // Initialize decryption
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                           reinterpret_cast<const unsigned char*>(key.constData()),
                           reinterpret_cast<const unsigned char*>(iv.constData())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        qWarning() << "CredentialStore: Failed to initialize decryption";
        return QString();
    }

    // Decrypt
    QByteArray plaintext(cipher.size() + EVP_MAX_BLOCK_LENGTH, 0);
    int len = 0;
    int plaintextLen = 0;

    if (EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(plaintext.data()), &len,
                          reinterpret_cast<const unsigned char*>(cipher.constData()),
                          cipher.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        qWarning() << "CredentialStore: Decryption failed";
        return QString();
    }
    plaintextLen = len;

    // Set auth tag for verification
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_SIZE,
                            const_cast<char*>(tag.constData())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        qWarning() << "CredentialStore: Failed to set auth tag";
        return QString();
    }

    // Finalize and verify tag
    if (EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(plaintext.data()) + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        qWarning() << "CredentialStore: Authentication failed - data may be tampered";
        return QString();
    }
    plaintextLen += len;

    EVP_CIPHER_CTX_free(ctx);

    plaintext.resize(plaintextLen);
    return QString::fromUtf8(plaintext);
}

QString CredentialStore::generateMachineKey() const
{
    // Generate a machine-specific key based on hardware/OS identifiers
    // This provides some protection even without OS keychain
    QString machineInfo;
    machineInfo += QSysInfo::machineUniqueId();
    machineInfo += QSysInfo::machineHostName();
    machineInfo += QSysInfo::kernelType();
    machineInfo += QSysInfo::productType();

    // Add per-installation random salt instead of hardcoded salt
    QString salt = getOrCreateSalt();
    machineInfo += salt;

    // Hash to create a consistent key
    QByteArray hash = QCryptographicHash::hash(machineInfo.toUtf8(), QCryptographicHash::Sha256);
    return hash.toHex();
}

QString CredentialStore::getOrCreateSalt() const
{
    // Get or create a per-installation random salt
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QString saltPath = configPath + "/MegaCustom/.salt.bin";

    QFile file(saltPath);

    // Check if salt already exists
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        QByteArray salt = file.readAll();
        file.close();
        if (salt.size() >= 32) {
            return QString::fromLatin1(salt.toBase64());
        }
    }

    // Generate new random salt (32 bytes = 256 bits)
    QByteArray salt(32, 0);
    if (RAND_bytes(reinterpret_cast<unsigned char*>(salt.data()), 32) != 1) {
        // Fallback to Qt random generator if OpenSSL fails
        for (int i = 0; i < salt.size(); ++i) {
            salt[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
        }
    }

    // Ensure directory exists
    QDir dir(configPath);
    if (!dir.exists("MegaCustom")) {
        dir.mkpath("MegaCustom");
    }

    // Save salt to file with restrictive permissions
    if (file.open(QIODevice::WriteOnly)) {
#ifndef Q_OS_WIN
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
#endif
        file.write(salt);
        file.close();
    } else {
        qWarning() << "CredentialStore: Failed to save salt file";
    }

    return QString::fromLatin1(salt.toBase64());
}

} // namespace MegaCustom
