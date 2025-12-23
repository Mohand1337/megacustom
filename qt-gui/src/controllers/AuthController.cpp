/**
 * AuthController implementation
 * Handles MEGA authentication operations with async listeners
 */

#include "AuthController.h"
#include "../accounts/CredentialStore.h"
#include <megaapi.h>
#include <QDebug>
#include <QMetaObject>
#include <QCoreApplication>

namespace MegaCustom {

/**
 * Request listener for auth operations
 * Marshals callbacks to the main thread via QMetaObject::invokeMethod
 */
class AuthRequestListener : public mega::MegaRequestListener {
public:
    enum class Operation { Login, FastLogin, FetchNodes, Logout };

    AuthRequestListener(AuthController* controller, Operation op)
        : m_controller(controller), m_operation(op) {}

    void onRequestFinish(mega::MegaApi* api, mega::MegaRequest* request, mega::MegaError* error) override {
        Q_UNUSED(api);

        bool success = (error->getErrorCode() == mega::MegaError::API_OK);
        QString errorMsg = success ? QString() : QString::fromUtf8(error->getErrorString());
        QString sessionKey;

        // Get session for login operations
        if (success && (m_operation == Operation::Login || m_operation == Operation::FastLogin)) {
            const char* session = api->dumpSession();
            if (session) {
                sessionKey = QString::fromUtf8(session);
                delete[] session;
            }
        }

        // Marshal to main thread
        QMetaObject::invokeMethod(m_controller, [this, success, errorMsg, sessionKey]() {
            switch (m_operation) {
                case Operation::Login:
                case Operation::FastLogin:
                    m_controller->handleLoginComplete(success, errorMsg);
                    break;
                case Operation::FetchNodes:
                    m_controller->handleFetchNodesComplete(success, errorMsg);
                    break;
                case Operation::Logout:
                    if (success) {
                        emit m_controller->logoutComplete();
                    }
                    break;
            }
            // Self-delete after callback
            deleteLater();
        }, Qt::QueuedConnection);
    }

    void deleteLater() {
        // Will be deleted by Qt's event loop
        QMetaObject::invokeMethod(qApp, [this]() {
            delete this;
        }, Qt::QueuedConnection);
    }

private:
    AuthController* m_controller;
    Operation m_operation;
};

AuthController::AuthController(void* api, QObject* parent)
    : QObject(parent)
    , m_api(static_cast<mega::MegaApi*>(api))
    , m_credentialStore(new CredentialStore(this))
{
    // Connect to credential store signals
    connect(m_credentialStore, &CredentialStore::sessionLoaded,
            this, &AuthController::onSessionLoaded);
    connect(m_credentialStore, &CredentialStore::error,
            this, &AuthController::onSessionLoadError);

    qDebug() << "AuthController initialized";
}

AuthController::~AuthController() {
    // Cancel any pending operations
    if (m_isLoggingIn) {
        cancelLogin();
    }
    qDebug() << "AuthController destroyed";
}

void AuthController::login(const QString& email, const QString& password) {
    if (m_isLoggingIn.exchange(true)) {
        qWarning() << "Login already in progress";
        return;
    }

    m_cancelRequested = false;
    m_pendingEmail = email;
    emit loginStarted(email);

    qDebug() << "Starting login for:" << email;

    auto* listener = new AuthRequestListener(this, AuthRequestListener::Operation::Login);
    m_activeListener = listener;

    m_api->login(email.toUtf8().constData(), password.toUtf8().constData(), listener);
}

void AuthController::loginWithSession(const QString& email, const QString& sessionToken) {
    if (m_isLoggingIn.exchange(true)) {
        qWarning() << "Login already in progress";
        return;
    }

    m_cancelRequested = false;
    m_pendingEmail = email;
    emit loginStarted(email);

    qDebug() << "Starting session login for:" << email;

    auto* listener = new AuthRequestListener(this, AuthRequestListener::Operation::FastLogin);
    m_activeListener = listener;

    m_api->fastLogin(sessionToken.toUtf8().constData(), listener);
}

void AuthController::logout() {
    if (!m_isLoggedIn) {
        qWarning() << "Not logged in";
        emit logoutComplete();
        return;
    }

    qDebug() << "Logging out:" << m_currentUser;

    auto* listener = new AuthRequestListener(this, AuthRequestListener::Operation::Logout);
    m_api->logout(false, listener);  // false = don't keep sync configs

    m_isLoggedIn = false;
    m_currentUser.clear();
}

void AuthController::cancelLogin() {
    if (!m_isLoggingIn) {
        return;
    }

    qDebug() << "Cancelling login";
    m_cancelRequested = true;

    // Note: MEGA SDK doesn't have a clean way to cancel login
    // The listener will check m_cancelRequested and emit failure
}

void AuthController::saveSession(const QString& email) {
    if (!m_isLoggedIn || !m_api) {
        qWarning() << "Cannot save session - not logged in";
        emit sessionSaved(email, false);
        return;
    }

    const char* session = m_api->dumpSession();
    if (!session) {
        qWarning() << "Failed to dump session";
        emit sessionSaved(email, false);
        return;
    }

    QString sessionToken = QString::fromUtf8(session);
    delete[] session;

    m_credentialStore->saveSession(email, sessionToken);

    // CredentialStore emits sessionSaved signal, but we emit our own for clarity
    emit sessionSaved(email, true);
    qDebug() << "Session saved for:" << email;
}

void AuthController::restoreSession(const QString& email) {
    if (m_isLoggingIn) {
        qWarning() << "Login already in progress";
        return;
    }

    if (!m_credentialStore->hasSession(email)) {
        emit sessionRestoreFailed(email, "No stored session found");
        return;
    }

    m_pendingEmail = email;
    m_credentialStore->loadSession(email);
}

bool AuthController::hasStoredSession(const QString& email) const {
    return m_credentialStore->hasSession(email);
}

QStringList AuthController::storedAccounts() const {
    return m_credentialStore->storedAccountIds();
}

void AuthController::onSessionLoaded(const QString& accountId, const QString& sessionToken) {
    qDebug() << "Session loaded for:" << accountId;
    loginWithSession(accountId, sessionToken);
}

void AuthController::onSessionLoadError(const QString& accountId, const QString& error) {
    qWarning() << "Failed to load session for" << accountId << ":" << error;
    emit sessionRestoreFailed(accountId, error);
}

void AuthController::handleLoginComplete(bool success, const QString& error) {
    m_activeListener = nullptr;

    if (m_cancelRequested) {
        m_isLoggingIn = false;
        m_cancelRequested = false;
        emit loginFailed("Login cancelled");
        return;
    }

    if (!success) {
        m_isLoggingIn = false;
        qWarning() << "Login failed:" << error;
        emit loginFailed(error);
        return;
    }

    qDebug() << "Login successful, fetching nodes...";

    // Login succeeded, now fetch nodes
    auto* listener = new AuthRequestListener(this, AuthRequestListener::Operation::FetchNodes);
    m_activeListener = listener;

    m_api->fetchNodes(listener);
}

void AuthController::handleFetchNodesComplete(bool success, const QString& error) {
    m_activeListener = nullptr;
    m_isLoggingIn = false;

    if (m_cancelRequested) {
        m_cancelRequested = false;
        emit loginFailed("Login cancelled");
        return;
    }

    if (!success) {
        qWarning() << "Fetch nodes failed:" << error;
        emit loginFailed("Failed to load account data: " + error);
        return;
    }

    m_isLoggedIn = true;
    m_currentUser = m_pendingEmail;
    m_pendingEmail.clear();

    qDebug() << "Login complete for:" << m_currentUser;
    emit loginSuccess(m_currentUser);
}

} // namespace MegaCustom
