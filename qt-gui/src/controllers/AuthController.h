#ifndef AUTH_CONTROLLER_H
#define AUTH_CONTROLLER_H

#include <QObject>
#include <QString>
#include <atomic>

// Forward declarations
namespace mega {
    class MegaApi;
    class MegaRequestListener;
}

namespace MegaCustom {

class CredentialStore;

/**
 * @brief Controller for authentication operations
 *
 * Handles login, logout, and session management through the MEGA API.
 * Uses CredentialStore for secure session token persistence.
 */
class AuthRequestListener;  // Forward declare for friend

class AuthController : public QObject {
    Q_OBJECT
    friend class AuthRequestListener;

public:
    explicit AuthController(void* api, QObject* parent = nullptr);
    ~AuthController();

    // State queries
    QString currentUser() const { return m_currentUser; }
    bool isLoggedIn() const { return m_isLoggedIn; }
    bool isLoggingIn() const { return m_isLoggingIn; }

    // Operations
    void login(const QString& email, const QString& password);
    void loginWithSession(const QString& email, const QString& sessionToken);
    void logout();
    void cancelLogin();

    // Session management
    void saveSession(const QString& email);
    void restoreSession(const QString& email);
    bool hasStoredSession(const QString& email) const;
    QStringList storedAccounts() const;

signals:
    void loginStarted(const QString& email);
    void loginSuccess(const QString& email);
    void loginFailed(const QString& error);
    void logoutComplete();
    void sessionSaved(const QString& email, bool success);
    void sessionRestored(const QString& email);
    void sessionRestoreFailed(const QString& email, const QString& error);

private slots:
    void onSessionLoaded(const QString& accountId, const QString& sessionToken);
    void onSessionLoadError(const QString& accountId, const QString& error);

private:
    void handleLoginComplete(bool success, const QString& error);
    void handleFetchNodesComplete(bool success, const QString& error);

    mega::MegaApi* m_api;
    CredentialStore* m_credentialStore;
    mega::MegaRequestListener* m_activeListener = nullptr;

    QString m_currentUser;
    QString m_pendingEmail;
    std::atomic<bool> m_isLoggedIn{false};
    std::atomic<bool> m_isLoggingIn{false};
    std::atomic<bool> m_cancelRequested{false};
};

} // namespace MegaCustom

#endif // AUTH_CONTROLLER_H