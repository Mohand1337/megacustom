#ifndef MEGACUSTOM_CREDENTIALSTORE_H
#define MEGACUSTOM_CREDENTIALSTORE_H

#include <QObject>
#include <QString>
#include <QMap>

namespace MegaCustom {

/**
 * @brief Secure storage for session tokens
 *
 * Attempts to use OS-level secure storage:
 * - Linux: Secret Service (GNOME Keyring / KWallet)
 * - Windows: Windows Credential Manager
 * - macOS: Keychain
 *
 * Falls back to AES-256 encrypted file if OS keychain unavailable.
 */
class CredentialStore : public QObject {
    Q_OBJECT

public:
    explicit CredentialStore(QObject* parent = nullptr);
    ~CredentialStore();

    /**
     * @brief Check if secure storage is available
     * @return true if OS keychain is available, false if using fallback
     */
    bool isSecureStorageAvailable() const;

    /**
     * @brief Save a session token for an account
     * @param accountId The account identifier
     * @param sessionToken The MEGA session token to store
     *
     * Emits sessionSaved() on completion
     */
    void saveSession(const QString& accountId, const QString& sessionToken);

    /**
     * @brief Load a session token for an account
     * @param accountId The account identifier
     *
     * Emits sessionLoaded() with the token, or error() if not found
     */
    void loadSession(const QString& accountId);

    /**
     * @brief Delete a session token
     * @param accountId The account identifier
     *
     * Emits sessionDeleted() on completion
     */
    void deleteSession(const QString& accountId);

    /**
     * @brief Check if a session exists for an account
     * @param accountId The account identifier
     * @return true if session token exists
     */
    bool hasSession(const QString& accountId) const;

    /**
     * @brief Get all stored account IDs
     * @return List of account IDs with stored sessions
     */
    QStringList storedAccountIds() const;

    /**
     * @brief Clear all stored sessions
     */
    void clearAll();

signals:
    /**
     * @brief Emitted when a session is successfully loaded
     * @param accountId The account identifier
     * @param sessionToken The loaded session token
     */
    void sessionLoaded(const QString& accountId, const QString& sessionToken);

    /**
     * @brief Emitted when a session is successfully saved
     * @param accountId The account identifier
     * @param success Whether the save was successful
     */
    void sessionSaved(const QString& accountId, bool success);

    /**
     * @brief Emitted when a session is deleted
     * @param accountId The account identifier
     */
    void sessionDeleted(const QString& accountId);

    /**
     * @brief Emitted on error
     * @param accountId The account identifier (empty for general errors)
     * @param errorMessage Description of the error
     */
    void error(const QString& accountId, const QString& errorMessage);

private:
    // Fallback encrypted file storage
    void initializeFallbackStorage();
    QString getFallbackFilePath() const;
    bool saveFallbackStorage();
    bool loadFallbackStorage();
    QString encrypt(const QString& plaintext) const;
    QString decrypt(const QString& ciphertext) const;
    QString generateMachineKey() const;
    QString getOrCreateSalt() const;  // Per-installation random salt

    // In-memory cache for fallback mode
    QMap<QString, QString> m_sessionCache;
    QString m_encryptionKey;
    bool m_useSecureStorage;
    bool m_fallbackLoaded;

    static const QString SERVICE_NAME;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_CREDENTIALSTORE_H
