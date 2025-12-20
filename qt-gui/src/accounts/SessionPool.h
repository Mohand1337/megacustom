#ifndef MEGACUSTOM_SESSIONPOOL_H
#define MEGACUSTOM_SESSIONPOOL_H

#include <QObject>
#include <QMap>
#include <QDateTime>
#include <QString>
#include <QMutex>

// Forward declaration for MEGA SDK
namespace mega {
class MegaApi;
}

namespace MegaCustom {

class CredentialStore;

/**
 * @brief Manages multiple MegaApi instances for fast account switching
 *
 * The SessionPool maintains a cache of active MegaApi sessions, allowing
 * users to quickly switch between accounts without re-authenticating.
 * Sessions are created on-demand and evicted based on LRU policy when
 * the pool reaches its maximum size.
 */
class SessionPool : public QObject {
    Q_OBJECT

public:
    explicit SessionPool(CredentialStore* credentialStore, QObject* parent = nullptr);
    ~SessionPool();

    /**
     * @brief Get or create a session for an account
     * @param accountId The account identifier
     * @param sessionToken Optional session token for login (if not in credential store)
     * @return The MegaApi instance, or nullptr if creation failed
     *
     * If the session exists in the pool, returns it immediately.
     * If not, attempts to create a new session using stored credentials.
     */
    mega::MegaApi* getSession(const QString& accountId, const QString& sessionToken = QString());

    /**
     * @brief Check if a session is active and connected
     * @param accountId The account identifier
     * @return true if session exists and is connected
     */
    bool isSessionActive(const QString& accountId) const;

    /**
     * @brief Check if a session exists in the pool
     * @param accountId The account identifier
     * @return true if session exists (may or may not be connected)
     */
    bool hasSession(const QString& accountId) const;

    /**
     * @brief Refresh an expired session
     * @param accountId The account identifier
     *
     * Attempts to re-authenticate using stored credentials.
     * Emits sessionReady() or sessionError() on completion.
     */
    void refreshSession(const QString& accountId);

    /**
     * @brief Release a session (logout)
     * @param accountId The account identifier
     * @param keepCredentials If true, keeps session token in credential store
     */
    void releaseSession(const QString& accountId, bool keepCredentials = true);

    /**
     * @brief Release all sessions
     * @param keepCredentials If true, keeps session tokens in credential store
     */
    void releaseAllSessions(bool keepCredentials = true);

    /**
     * @brief Set maximum number of cached sessions
     * @param max Maximum sessions (default: 5)
     *
     * When limit is reached, least recently used sessions are evicted.
     */
    void setMaxSessions(int max);

    /**
     * @brief Get the current session count
     * @return Number of active sessions in pool
     */
    int sessionCount() const;

    /**
     * @brief Get all account IDs with active sessions
     * @return List of account IDs
     */
    QStringList activeAccountIds() const;

    /**
     * @brief Mark a session as recently used (updates LRU timestamp)
     * @param accountId The account identifier
     */
    void touchSession(const QString& accountId);

    /**
     * @brief Wait for a session to become active (blocking)
     * @param accountId The account identifier
     * @param timeoutMs Maximum time to wait in milliseconds
     * @return true if session is active, false if timeout or error
     *
     * This method blocks until the session is connected and nodes are fetched,
     * or until the timeout is reached. Use this when you need to perform operations
     * that require the full node tree (like getNodeByPath).
     */
    bool waitForSession(const QString& accountId, int timeoutMs = 60000);

signals:
    /**
     * @brief Emitted when a session is ready to use
     * @param accountId The account identifier
     */
    void sessionReady(const QString& accountId);

    /**
     * @brief Emitted when a session expires or becomes invalid
     * @param accountId The account identifier
     */
    void sessionExpired(const QString& accountId);

    /**
     * @brief Emitted on session error
     * @param accountId The account identifier
     * @param errorMessage Description of the error
     */
    void sessionError(const QString& accountId, const QString& errorMessage);

    /**
     * @brief Emitted when a session is created
     * @param accountId The account identifier
     */
    void sessionCreated(const QString& accountId);

    /**
     * @brief Emitted when a session is released
     * @param accountId The account identifier
     */
    void sessionReleased(const QString& accountId);

    /**
     * @brief Emitted when login is required (no stored credentials)
     * @param accountId The account identifier
     */
    void loginRequired(const QString& accountId);

private slots:
    void onSessionLoaded(const QString& accountId, const QString& sessionToken);
    void onCredentialError(const QString& accountId, const QString& errorMessage);

private:
    struct CachedSession {
        mega::MegaApi* api = nullptr;
        QDateTime lastUsed;
        bool isConnected = false;
        bool isLoggingIn = false;
    };

    void evictLeastRecentlyUsed();
    mega::MegaApi* createApiInstance(const QString& accountId);
    void performLogin(const QString& accountId, const QString& sessionToken);
    void cleanupSession(CachedSession& session);

    QMap<QString, CachedSession> m_pool;
    mutable QMutex m_poolMutex;  // Protects m_pool from concurrent access
    CredentialStore* m_credentialStore;
    int m_maxSessions;
    QString m_pendingAccountId;  // Account ID waiting for credential load
};

} // namespace MegaCustom

#endif // MEGACUSTOM_SESSIONPOOL_H
