#ifndef AUTH_BRIDGE_H
#define AUTH_BRIDGE_H

#include <QObject>
#include <QString>
#include <memory>

namespace MegaCustom {
    // Forward declarations
    class AuthController;
    class AuthenticationModule;

/**
 * AuthBridge - Adapter between GUI AuthController and CLI AuthenticationModule
 *
 * This class translates between:
 * - Qt signals/slots (GUI side)
 * - Callbacks and direct calls (CLI side)
 */
class AuthBridge : public QObject {
    Q_OBJECT

public:
    explicit AuthBridge(QObject* parent = nullptr);
    virtual ~AuthBridge();

    /**
     * Set the CLI authentication module
     */
    void setAuthModule(AuthenticationModule* module);

    /**
     * Connect to GUI controller
     */
    void connectToGUI(AuthController* guiController);

    /**
     * Check if currently logged in
     */
    bool isLoggedIn() const { return m_isLoggedIn; }
    QString currentUser() const { return m_currentUser; }

public slots:
    /**
     * Handle login request from GUI
     */
    void handleLogin(const QString& email, const QString& password);

    /**
     * Handle logout request from GUI
     */
    void handleLogout();

    /**
     * Handle 2FA code submission
     */
    void handle2FA(const QString& code);

    /**
     * Restore saved session
     */
    void restoreSession();

signals:
    /**
     * Signals to GUI
     */
    void loginSucceeded(const QString& email);
    void loginFailed(const QString& error);
    void twoFactorRequired();
    void logoutCompleted();
    void sessionRestored(const QString& email);

private:
    /**
     * Internal callbacks for CLI module
     */
    void onLoginComplete(bool success, const QString& result);
    void onLogoutComplete();

private:
    MegaCustom::AuthenticationModule* m_authModule = nullptr;
    AuthController* m_guiController = nullptr;

    bool m_isLoggedIn = false;
    QString m_currentUser;
    QString m_pendingEmail;  // For 2FA flow
};

} // namespace MegaCustom

#endif // AUTH_BRIDGE_H