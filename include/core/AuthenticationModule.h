#ifndef AUTHENTICATION_MODULE_H
#define AUTHENTICATION_MODULE_H

#include <string>
#include <memory>
#include <functional>
#include <chrono>
#include <optional>

namespace mega {
    class MegaApi;
    class MegaRequest;
    class MegaError;
}

namespace MegaCustom {

/**
 * Authentication result structure
 */
struct AuthResult {
    bool success;
    std::string sessionKey;
    std::string errorMessage;
    int errorCode;
    bool requires2FA;
};

/**
 * User account information
 */
struct AccountInfo {
    std::string email;
    std::string name;
    long long storageUsed;
    long long storageTotal;
    long long transferUsed;
    long long transferTotal;
    int accountType; // 0=Free, 1=ProI, 2=ProII, 3=ProIII, 4=Pro Lite
    std::chrono::system_clock::time_point proExpiration;
};

/**
 * Handles all authentication-related operations
 */
class AuthenticationModule {
public:
    explicit AuthenticationModule(mega::MegaApi* megaApi);
    ~AuthenticationModule();

    /**
     * Standard login with email and password
     * @param email User email address
     * @param password User password
     * @return Authentication result
     */
    AuthResult login(const std::string& email, const std::string& password);

    /**
     * Login with existing session key
     * @param sessionKey Previously saved session key
     * @return Authentication result
     */
    AuthResult loginWithSession(const std::string& sessionKey);

    /**
     * Complete 2FA login
     * @param pin 2FA pin code
     * @return Authentication result
     */
    AuthResult complete2FA(const std::string& pin);

    /**
     * Fast login with email and password hash
     * @param email User email
     * @param passwordHash Pre-computed password hash
     * @return Authentication result
     */
    AuthResult fastLogin(const std::string& email, const std::string& passwordHash);

    /**
     * Logout and clear session
     * @param clearLocalCache Whether to clear local cached data
     */
    void logout(bool clearLocalCache = false);

    /**
     * Check if currently logged in
     * @return true if logged in
     */
    bool isLoggedIn() const;

    /**
     * Get current session key
     * @return Session key or empty if not logged in
     */
    std::string getSessionKey() const;

    /**
     * Save session to encrypted file
     * @param filePath Path to save session file
     * @param encryptionKey Key to encrypt the session
     * @return true if saved successfully
     */
    bool saveSession(const std::string& filePath, const std::string& encryptionKey);

    /**
     * Load session from encrypted file
     * @param filePath Path to session file
     * @param encryptionKey Key to decrypt the session
     * @return Session key or empty if failed
     */
    std::string loadSession(const std::string& filePath, const std::string& encryptionKey);

    /**
     * Get account information
     * @return Account info structure
     */
    AccountInfo getAccountInfo() const;

    /**
     * Change account password
     * @param currentPassword Current password
     * @param newPassword New password
     * @return true if changed successfully
     */
    bool changePassword(const std::string& currentPassword, const std::string& newPassword);

    /**
     * Enable 2FA for account
     * @return Secret key for 2FA setup
     */
    std::string enable2FA();

    /**
     * Disable 2FA for account
     * @param pin Current 2FA pin
     * @return true if disabled successfully
     */
    bool disable2FA(const std::string& pin);

    /**
     * Check if 2FA is enabled
     * @return true if 2FA is enabled
     */
    bool is2FAEnabled() const;

    /**
     * Register new account
     * @param email Email address
     * @param password Password
     * @param name User name
     * @return Registration result
     */
    AuthResult registerAccount(const std::string& email, const std::string& password, const std::string& name);

    /**
     * Verify account with confirmation link
     * @param confirmationLink Link from email
     * @param email User email
     * @return true if verified successfully
     */
    bool verifyAccount(const std::string& confirmationLink, const std::string& email);

    /**
     * Request password reset
     * @param email Account email
     * @return true if reset email sent
     */
    bool requestPasswordReset(const std::string& email);

    /**
     * Confirm password reset
     * @param resetLink Link from reset email
     * @param newPassword New password
     * @return true if reset successfully
     */
    bool confirmPasswordReset(const std::string& resetLink, const std::string& newPassword);

    /**
     * Set callback for authentication events
     * @param callback Function to call on auth events
     */
    void setAuthCallback(std::function<void(const AuthResult&)> callback);

    /**
     * Get password hash (for fast login)
     * @param password Plain text password
     * @return Password hash
     */
    static std::string computePasswordHash(const std::string& password);

    /**
     * Validate email format
     * @param email Email to validate
     * @return true if valid email format
     */
    static bool isValidEmail(const std::string& email);

    /**
     * Check password strength
     * @param password Password to check
     * @return Strength score (0-100)
     */
    static int checkPasswordStrength(const std::string& password);

private:
    mega::MegaApi* m_megaApi;

    // Authentication state
    bool m_isLoggedIn;
    std::string m_currentSessionKey;
    std::string m_pendingEmail;  // For 2FA flow
    std::string m_pendingPassword;  // For 2FA flow

    // Callbacks
    std::function<void(const AuthResult&)> m_authCallback;

    // Helper methods
    AuthResult processAuthRequest(mega::MegaRequest* request, mega::MegaError* error);
    void clearAuthState();
    std::string encryptData(const std::string& data, const std::string& key);
    std::string decryptData(const std::string& encryptedData, const std::string& key);

    // Listener class for async operations
    class AuthListener;
    std::unique_ptr<AuthListener> m_listener;
};

} // namespace MegaCustom

#endif // AUTHENTICATION_MODULE_H