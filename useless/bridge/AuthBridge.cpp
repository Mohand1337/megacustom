#include "AuthBridge.h"
#include "controllers/AuthController.h"
#include "bridge/BackendModules.h"  // Real CLI modules
#include <QDebug>
#include <QTimer>

namespace MegaCustom {

AuthBridge::AuthBridge(QObject* parent)
    : QObject(parent) {
    qDebug() << "AuthBridge: Created authentication bridge";
}

AuthBridge::~AuthBridge() {
    qDebug() << "AuthBridge: Destroyed";
}

void AuthBridge::setAuthModule(AuthenticationModule* module) {
    m_authModule = module;
    qDebug() << "AuthBridge: Auth module set";

    if (m_authModule) {
        // Set up callbacks from CLI module for async auth events
        m_authModule->setAuthCallback([this](const AuthResult& result) {
            if (result.requires2FA) {
                QMetaObject::invokeMethod(this, [this]() {
                    emit twoFactorRequired();
                }, Qt::QueuedConnection);
            } else {
                QMetaObject::invokeMethod(this, [this, result]() {
                    onLoginComplete(result.success,
                        result.success ? m_pendingEmail : QString::fromStdString(result.errorMessage));
                }, Qt::QueuedConnection);
            }
        });
    }
}

void AuthBridge::connectToGUI(AuthController* guiController) {
    if (!guiController) {
        qDebug() << "AuthBridge: Cannot connect - null GUI controller";
        return;
    }

    m_guiController = guiController;

    // Disconnect any existing connections
    disconnect(m_guiController, nullptr, this, nullptr);
    disconnect(this, nullptr, m_guiController, nullptr);

    // Connect GUI signals to bridge slots
    connect(m_guiController, &AuthController::login,
            this, &AuthBridge::handleLogin);

    connect(m_guiController, &AuthController::logout,
            this, &AuthBridge::handleLogout);

    // Connect bridge signals to GUI signals
    connect(this, &AuthBridge::loginSucceeded,
            m_guiController, &AuthController::loginSuccess);

    connect(this, &AuthBridge::loginFailed,
            m_guiController, &AuthController::loginFailed);

    connect(this, &AuthBridge::logoutCompleted,
            m_guiController, &AuthController::logoutComplete);

    qDebug() << "AuthBridge: Connected to GUI controller";
}

void AuthBridge::handleLogin(const QString& email, const QString& password) {
    qDebug() << "AuthBridge: Login requested for" << email;

    if (!m_authModule) {
        emit loginFailed("Backend not initialized");
        return;
    }

    m_pendingEmail = email;

    // Call actual CLI login
    if (m_authModule) {
        MegaCustom::AuthResult result = m_authModule->login(
            email.toStdString(),
            password.toStdString()
        );

        if (result.requires2FA) {
            emit twoFactorRequired();
        } else {
            onLoginComplete(result.success,
                          result.success ? email : QString::fromStdString(result.errorMessage));
        }
    } else {
        onLoginComplete(false, "Authentication module not initialized");
    }
}

void AuthBridge::handleLogout() {
    qDebug() << "AuthBridge: Logout requested";

    if (!m_authModule) {
        emit logoutCompleted();
        return;
    }

    // Call actual CLI logout
    m_authModule->logout();
    onLogoutComplete();
}

void AuthBridge::handle2FA(const QString& code) {
    qDebug() << "AuthBridge: 2FA code submitted";

    if (!m_authModule) {
        emit loginFailed("Backend not initialized");
        return;
    }

    // Submit 2FA code to CLI module
    AuthResult result = m_authModule->complete2FA(code.toStdString());
    if (result.success) {
        m_isLoggedIn = true;
        m_currentUser = m_pendingEmail;
        emit loginSucceeded(m_currentUser);
    } else {
        emit loginFailed(QString::fromStdString(result.errorMessage));
    }
}

void AuthBridge::restoreSession() {
    qDebug() << "AuthBridge: Attempting to restore session";

    if (!m_authModule) {
        return;
    }

    // Check if already logged in from a previous session
    if (m_authModule->isLoggedIn()) {
        AccountInfo info = m_authModule->getAccountInfo();
        m_currentUser = QString::fromStdString(info.email);
        m_isLoggedIn = true;
        emit sessionRestored(m_currentUser);
        qDebug() << "AuthBridge: Session restored for" << m_currentUser;
    }
}

void AuthBridge::onLoginComplete(bool success, const QString& result) {
    qDebug() << "AuthBridge: Login complete -" << (success ? "success" : "failed");

    if (success) {
        m_isLoggedIn = true;
        m_currentUser = result;
        emit loginSucceeded(m_currentUser);
    } else {
        m_isLoggedIn = false;
        m_currentUser.clear();
        emit loginFailed(result);
    }
}

void AuthBridge::onLogoutComplete() {
    qDebug() << "AuthBridge: Logout complete";

    m_isLoggedIn = false;
    m_currentUser.clear();
    emit logoutCompleted();
}

} // namespace MegaCustom

#include "moc_AuthBridge.cpp"