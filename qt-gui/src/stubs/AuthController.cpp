#include "controllers/AuthController.h"
#include "core/AuthenticationModule.h"
#include "core/MegaManager.h"
#include "megaapi.h"
#include <QDebug>
#include <QTimer>
#include <QSettings>
#include <QStandardPaths>

namespace MegaCustom {

AuthController::AuthController(void* api) : QObject(), m_isLoggedIn(false) {
    Q_UNUSED(api);
    qDebug() << "AuthController constructed (with real backend)";
}

QString AuthController::currentUser() const {
    return m_currentUser;
}

void AuthController::login(const QString& email, const QString& password) {
    qDebug() << "Attempting real login for:" << email;

    // Get the MegaManager singleton
    MegaManager& manager = MegaManager::getInstance();
    mega::MegaApi* megaApi = manager.getMegaApi();

    if (!megaApi) {
        qDebug() << "Error: MegaApi not initialized";
        emit loginFailed("SDK not initialized");
        return;
    }

    // Create authentication module
    AuthenticationModule authModule(megaApi);

    // Attempt login
    AuthResult result = authModule.login(email.toStdString(), password.toStdString());

    if (result.success) {
        qDebug() << "Login successful!";
        m_currentUser = email;
        m_isLoggedIn = true;
        emit loginSuccess(email);
    } else {
        qDebug() << "Login failed:" << QString::fromStdString(result.errorMessage);
        m_isLoggedIn = false;
        emit loginFailed(QString::fromStdString(result.errorMessage));
    }
}

void AuthController::logout() {
    qDebug() << "Logging out...";

    // Get the MegaManager singleton
    MegaManager& manager = MegaManager::getInstance();
    mega::MegaApi* megaApi = manager.getMegaApi();

    if (megaApi) {
        AuthenticationModule authModule(megaApi);
        authModule.logout(false); // Don't clear local data
    }

    m_currentUser.clear();
    m_isLoggedIn = false;
    emit logoutComplete();
}

void AuthController::saveSession(const QString& sessionFile, const QString& encryptionKey) {
    qDebug() << "Saving session to:" << sessionFile;

    // Get the MegaManager singleton
    MegaManager& manager = MegaManager::getInstance();
    mega::MegaApi* megaApi = manager.getMegaApi();

    if (!megaApi) {
        qDebug() << "Error: MegaApi not initialized";
        return;
    }

    // Create authentication module
    AuthenticationModule authModule(megaApi);

    // Use email-based key if encryption key not provided
    QString key = encryptionKey.isEmpty() ? m_currentUser : encryptionKey;

    // Save session
    if (authModule.saveSession(sessionFile.toStdString(), key.toStdString())) {
        qDebug() << "Session saved successfully";
    } else {
        qDebug() << "Failed to save session";
    }
}

void AuthController::restoreSession(const QString& sessionFile) {
    qDebug() << "Restoring session from:" << sessionFile;

    // Get the MegaManager singleton
    MegaManager& manager = MegaManager::getInstance();
    mega::MegaApi* megaApi = manager.getMegaApi();

    if (!megaApi) {
        qDebug() << "Error: MegaApi not initialized";
        emit loginFailed("SDK not initialized");
        return;
    }

    // Create authentication module
    AuthenticationModule authModule(megaApi);

    // Get saved email from settings to use as decryption key
    // Include Settings header for this
    QString savedEmail;
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/MegaCustom/settings.ini", QSettings::IniFormat);
    savedEmail = settings.value("auth/lastEmail", "").toString();

    // Use saved email as encryption key, fallback to default
    QString encryptionKey = savedEmail.isEmpty() ? "megacustom_session" : savedEmail;
    qDebug() << "Using encryption key from email:" << (savedEmail.isEmpty() ? "default" : "saved");

    std::string sessionKey = authModule.loadSession(sessionFile.toStdString(),
                                                     encryptionKey.toStdString());

    if (sessionKey.empty()) {
        qDebug() << "No valid session found or decryption failed";
        emit loginFailed("No saved session found");
        return;
    }

    // Login with session key
    AuthResult result = authModule.loginWithSession(sessionKey);

    if (result.success) {
        qDebug() << "Session restored successfully";
        m_isLoggedIn = true;
        // Try to get email from the account
        mega::MegaUser* user = megaApi->getMyUser();
        if (user) {
            m_currentUser = QString::fromUtf8(user->getEmail());
            delete user;
        }
        emit loginSuccess(m_currentUser);
    } else {
        qDebug() << "Session restore failed:" << QString::fromStdString(result.errorMessage);
        m_isLoggedIn = false;
        emit loginFailed(QString::fromStdString(result.errorMessage));
    }
}

} // namespace MegaCustom

#include "moc_AuthController.cpp"