#include "core/AuthenticationModule.h"
#include "core/Crypto.h"
#include "megaapi.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <openssl/sha.h>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <regex>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cstring>

namespace MegaCustom {

namespace {
// Securely erase a string by overwriting memory with zeros before clearing
// This prevents sensitive data from lingering in memory
void secureErase(std::string& str) {
    if (!str.empty()) {
        // Use volatile to prevent compiler from optimizing away the memset
        volatile char* ptr = &str[0];
        std::memset(const_cast<char*>(ptr), 0, str.size());
    }
    str.clear();
    str.shrink_to_fit();  // Release memory back to allocator
}
} // anonymous namespace

/**
 * Helper function to get detailed error message from Mega error code
 */
std::string getDetailedErrorMessage(int errorCode, const std::string& context = "") {
    std::string prefix = context.empty() ? "" : context + ": ";

    switch (errorCode) {
        case mega::MegaError::API_OK:
            return prefix + "Success";
        case mega::MegaError::API_EINTERNAL:
            return prefix + "Internal error. Please try again";
        case mega::MegaError::API_EARGS:
            return prefix + "Invalid arguments provided";
        case mega::MegaError::API_EAGAIN:
            return prefix + "Service temporarily unavailable. Please try again";
        case mega::MegaError::API_ERATELIMIT:
            return prefix + "Too many requests. Please slow down";
        case mega::MegaError::API_EFAILED:
            return prefix + "Operation failed. Please try again";
        case mega::MegaError::API_ETOOMANY:
            return prefix + "Too many attempts. Please wait before retrying";
        case mega::MegaError::API_ERANGE:
            return prefix + "Value out of allowed range";
        case mega::MegaError::API_EEXPIRED:
            return prefix + "Resource has expired";
        case mega::MegaError::API_ENOENT:
            return prefix + "Resource not found or invalid credentials";
        case mega::MegaError::API_ECIRCULAR:
            return prefix + "Circular reference detected";
        case mega::MegaError::API_EACCESS:
            return prefix + "Access denied. Check permissions";
        case mega::MegaError::API_EEXIST:
            return prefix + "Resource already exists";
        case mega::MegaError::API_EINCOMPLETE:
            return prefix + "Operation incomplete. Account may need confirmation";
        case mega::MegaError::API_EKEY:
            return prefix + "Invalid cryptographic key";
        case mega::MegaError::API_ESID:
            return prefix + "Invalid or expired session";
        case mega::MegaError::API_EBLOCKED:
            return prefix + "Resource is blocked or account suspended";
        case mega::MegaError::API_EOVERQUOTA:
            return prefix + "Storage quota exceeded";
        case mega::MegaError::API_ETEMPUNAVAIL:
            return prefix + "Resource temporarily unavailable";
        case mega::MegaError::API_ETOOMANYCONNECTIONS:
            return prefix + "Too many connections";
        case mega::MegaError::API_EWRITE:
            return prefix + "Write operation failed";
        case mega::MegaError::API_EREAD:
            return prefix + "Read operation failed";
        case mega::MegaError::API_EAPPKEY:
            return prefix + "Invalid application key";
        case mega::MegaError::API_ESSL:
            return prefix + "SSL/TLS error. Check network security";
        case mega::MegaError::API_EGOINGOVERQUOTA:
            return prefix + "Operation would exceed quota";
        case mega::MegaError::API_EMFAREQUIRED:
            return prefix + "Two-factor authentication required";
        default:
            return prefix + "Unknown error (code: " + std::to_string(errorCode) + ")";
    }
}

// Internal listener class for async operations
class AuthenticationModule::AuthListener : public mega::MegaRequestListener {
public:
    AuthListener(AuthenticationModule* auth) : m_auth(auth), m_completed(false) {}

    void onRequestFinish(mega::MegaApi* api, mega::MegaRequest* request,
                         mega::MegaError* error) override {
        m_lastRequest = request->getType();
        m_lastError = error->getErrorCode();
        m_errorString = error->getErrorString() ? error->getErrorString() : "";

        // Check if 2FA is required
        if (error->getErrorCode() == mega::MegaError::API_EMFAREQUIRED) {
            m_requires2FA = true;
        }

        m_completed = true;
        m_cv.notify_all();
    }

    bool waitForCompletion(int timeoutSeconds = 30) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, std::chrono::seconds(timeoutSeconds),
                            [this] { return m_completed; });
    }

    void reset() {
        m_completed = false;
        m_requires2FA = false;
        m_lastError = mega::MegaError::API_OK;
        m_errorString.clear();
    }

    int getLastError() const { return m_lastError; }
    std::string getErrorString() const { return m_errorString; }
    bool requires2FA() const { return m_requires2FA; }
    int getLastRequestType() const { return m_lastRequest; }

private:
    AuthenticationModule* m_auth;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_completed;
    bool m_requires2FA = false;
    int m_lastError = mega::MegaError::API_OK;
    int m_lastRequest = 0;
    std::string m_errorString;
};

// Constructor
AuthenticationModule::AuthenticationModule(mega::MegaApi* megaApi)
    : m_megaApi(megaApi), m_isLoggedIn(false) {
    if (!m_megaApi) {
        throw std::runtime_error("MegaApi instance is null");
    }
    m_listener = std::make_unique<AuthListener>(this);

    // Check if already logged in from a previous session
    // isLoggedIn() returns the account type (0 = not logged in, 1+ = logged in with various account types)
    int loginStatus = m_megaApi->isLoggedIn();
    m_isLoggedIn = (loginStatus > 0);

    if (m_isLoggedIn) {
        // Fetch the session key
        char* session = m_megaApi->dumpSession();
        if (session) {
            m_currentSessionKey = session;
            delete[] session;
        }
    } else {
        // If not directly logged in, check if there's a pending session
        // The SDK might still be processing a fastLogin from session restore
        mega::MegaNode* rootNode = m_megaApi->getRootNode();
        if (rootNode) {
            // If we have a root node, we're actually logged in
            m_isLoggedIn = true;
            delete rootNode;

            // Try to get session key
            char* session = m_megaApi->dumpSession();
            if (session) {
                m_currentSessionKey = session;
                delete[] session;
            }
        }
    }
}

// Destructor
AuthenticationModule::~AuthenticationModule() {
    // Don't auto-logout on destruction - session should persist in SDK
    // The m_isLoggedIn flag is just local tracking, not actual SDK state
    // Explicit logout() should be called when user wants to log out
}

// Standard login with email and password
AuthResult AuthenticationModule::login(const std::string& email, const std::string& password) {
    AuthResult result{false, "", "", mega::MegaError::API_OK, false};

    // Validate email format
    if (!isValidEmail(email)) {
        result.errorMessage = "Invalid email format";
        result.errorCode = mega::MegaError::API_EARGS;
        return result;
    }

    // Store credentials for potential 2FA retry
    m_pendingEmail = email;
    m_pendingPassword = password;

    // Reset listener
    m_listener->reset();

    // Initiate login
    m_megaApi->login(email.c_str(), password.c_str(), m_listener.get());

    // Wait for completion
    if (!m_listener->waitForCompletion(60)) {
        result.errorMessage = "Login timeout";
        result.errorCode = mega::MegaError::API_EAGAIN;
        return result;
    }

    // Process result
    result = processAuthRequest(nullptr, nullptr);

    if (result.success) {
        m_isLoggedIn = true;

        // Get session key
        char* session = m_megaApi->dumpSession();
        if (session) {
            result.sessionKey = session;
            m_currentSessionKey = session;
            delete[] session;
        }

        // Clear pending credentials
        secureErase(m_pendingEmail);
        secureErase(m_pendingPassword);

        // Fetch nodes from the server - this is required after login
        std::cout << "Fetching account data..." << std::endl;  // endl flushes
        m_listener->reset();
        m_megaApi->fetchNodes(m_listener.get());

        // Wait for fetchNodes to complete (up to 60 seconds)
        std::cout << "Waiting for fetchNodes to complete..." << std::endl;
        bool fetchComplete = m_listener->waitForCompletion(60);
        if (!fetchComplete) {
            std::cerr << "Warning: Timeout fetching account data." << std::endl;
        } else {
            int fetchError = m_listener->getLastError();
            if (fetchError != mega::MegaError::API_OK) {
                std::cerr << "fetchNodes failed with error: " << fetchError
                          << " - " << m_listener->getErrorString() << std::endl;
            } else {
                std::cout << "fetchNodes completed successfully." << std::endl;
            }
        }

        // Verify nodes are loaded - must also wait for children to be populated
        int retries = 60;  // More retries - up to 12 seconds
        bool nodesReady = false;
        int childCount = 0;

        while (retries > 0) {
            mega::MegaNode* rootNode = m_megaApi->getRootNode();
            if (rootNode) {
                childCount = m_megaApi->getNumChildren(rootNode);
                delete rootNode;

                if (childCount > 0) {
                    nodesReady = true;
                    std::cout << "Account data loaded successfully (" << childCount << " root items)." << std::endl;
                    break;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            retries--;
        }

        if (!nodesReady) {
            // Root node exists but may have no children - still considered ready
            mega::MegaNode* rootNode = m_megaApi->getRootNode();
            if (rootNode) {
                delete rootNode;
                std::cout << "Account data loaded (no files in cloud drive)." << std::endl;
            } else {
                std::cerr << "Warning: Account data not fully loaded after " << (60-retries) << " retries." << std::endl;
            }
        }

        // Trigger callback if set
        if (m_authCallback) {
            m_authCallback(result);
        }
    }

    return result;
}

// Login with existing session key
AuthResult AuthenticationModule::loginWithSession(const std::string& sessionKey) {
    AuthResult result{false, "", "", mega::MegaError::API_OK, false};

    if (sessionKey.empty()) {
        result.errorMessage = "Empty session key";
        result.errorCode = mega::MegaError::API_EARGS;
        return result;
    }

    // Reset listener
    m_listener->reset();

    // Fast login with session
    m_megaApi->fastLogin(sessionKey.c_str(), m_listener.get());

    // Wait for completion
    if (!m_listener->waitForCompletion(30)) {
        result.errorMessage = "Session login timeout";
        result.errorCode = mega::MegaError::API_EAGAIN;
        return result;
    }

    // Process result
    result = processAuthRequest(nullptr, nullptr);

    if (result.success) {
        m_isLoggedIn = true;
        m_currentSessionKey = sessionKey;
        result.sessionKey = sessionKey;

        // IMPORTANT: After fastLogin, we MUST call fetchNodes to download the node tree
        // Without this, getRootNode() and getChildren() will return empty results
        std::cout << "Fetching account data..." << std::endl;
        m_listener->reset();
        m_megaApi->fetchNodes(m_listener.get());

        // Wait for fetchNodes to complete (up to 60 seconds)
        std::cout << "Waiting for fetchNodes to complete..." << std::endl;
        bool fetchComplete = m_listener->waitForCompletion(60);
        if (!fetchComplete) {
            std::cerr << "Warning: Timeout fetching account data." << std::endl;
        } else {
            int fetchError = m_listener->getLastError();
            if (fetchError != mega::MegaError::API_OK) {
                std::cerr << "fetchNodes failed with error: " << fetchError
                          << " - " << m_listener->getErrorString() << std::endl;
            } else {
                std::cout << "fetchNodes completed successfully." << std::endl;
            }
        }

        // Verify nodes are loaded - must also wait for children to be populated
        int retries = 60;  // More retries - up to 12 seconds
        bool nodesReady = false;
        int childCount = 0;

        while (retries > 0) {
            mega::MegaNode* rootNode = m_megaApi->getRootNode();
            if (rootNode) {
                childCount = m_megaApi->getNumChildren(rootNode);
                delete rootNode;

                if (childCount > 0) {
                    nodesReady = true;
                    std::cout << "Account data loaded successfully (" << childCount << " root items)." << std::endl;
                    break;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            retries--;
        }

        if (!nodesReady) {
            // Root node exists but may have no children - still considered ready
            mega::MegaNode* rootNode = m_megaApi->getRootNode();
            if (rootNode) {
                delete rootNode;
                std::cout << "Account data loaded (no files in cloud drive)." << std::endl;
            } else {
                std::cerr << "Warning: Account data not fully loaded after " << (60-retries) << " retries." << std::endl;
            }
        }

        // Trigger callback
        if (m_authCallback) {
            m_authCallback(result);
        }
    }

    return result;
}

// Complete 2FA login
AuthResult AuthenticationModule::complete2FA(const std::string& pin) {
    AuthResult result{false, "", "", mega::MegaError::API_OK, false};

    if (pin.empty() || m_pendingEmail.empty() || m_pendingPassword.empty()) {
        result.errorMessage = "Invalid 2FA state or empty pin";
        result.errorCode = mega::MegaError::API_EARGS;
        return result;
    }

    // Reset listener
    m_listener->reset();

    // Multi-factor authentication
    m_megaApi->multiFactorAuthLogin(m_pendingEmail.c_str(),
                                    m_pendingPassword.c_str(),
                                    pin.c_str(),
                                    m_listener.get());

    // Wait for completion
    if (!m_listener->waitForCompletion(60)) {
        result.errorMessage = "2FA login timeout";
        result.errorCode = mega::MegaError::API_EAGAIN;
        return result;
    }

    // Process result
    result = processAuthRequest(nullptr, nullptr);

    if (result.success) {
        m_isLoggedIn = true;

        // Get session key
        char* session = m_megaApi->dumpSession();
        if (session) {
            result.sessionKey = session;
            m_currentSessionKey = session;
            delete[] session;
        }

        // Clear pending credentials
        secureErase(m_pendingEmail);
        secureErase(m_pendingPassword);

        // Wait for nodes to be loaded after 2FA login
        std::cout << "Fetching account data...\n";
        int retries = 30;
        bool nodesReady = false;

        while (retries > 0) {
            mega::MegaNode* rootNode = m_megaApi->getRootNode();
            if (rootNode) {
                delete rootNode;
                nodesReady = true;
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            retries--;
        }

        if (!nodesReady) {
            std::cerr << "Warning: Account data not fully loaded, some operations may fail.\n";
        }

        // Trigger callback
        if (m_authCallback) {
            m_authCallback(result);
        }
    }

    return result;
}

// Fast login with email and password hash
AuthResult AuthenticationModule::fastLogin(const std::string& email,
                                          const std::string& passwordHash) {
    AuthResult result{false, "", "", mega::MegaError::API_OK, false};

    if (!isValidEmail(email) || passwordHash.empty()) {
        result.errorMessage = "Invalid email or password hash";
        result.errorCode = mega::MegaError::API_EARGS;
        return result;
    }

    // Reset listener
    m_listener->reset();

    // Note: Mega SDK doesn't have direct fast login with hash
    // We use regular login, but could cache the hash for future use
    m_megaApi->login(email.c_str(), passwordHash.c_str(), m_listener.get());

    // Wait for completion
    if (!m_listener->waitForCompletion(60)) {
        result.errorMessage = "Fast login timeout";
        result.errorCode = mega::MegaError::API_EAGAIN;
        return result;
    }

    // Process result
    result = processAuthRequest(nullptr, nullptr);

    if (result.success) {
        m_isLoggedIn = true;

        // Get session key
        char* session = m_megaApi->dumpSession();
        if (session) {
            result.sessionKey = session;
            m_currentSessionKey = session;
            delete[] session;
        }

        // Trigger callback
        if (m_authCallback) {
            m_authCallback(result);
        }
    }

    return result;
}

// Logout and clear session
void AuthenticationModule::logout(bool clearLocalCache) {
    if (!m_isLoggedIn) {
        return;
    }

    // Reset listener
    m_listener->reset();

    if (clearLocalCache) {
        // Full logout clearing all local cache
        m_megaApi->logout(false, m_listener.get());  // false = don't keep sync configs
    } else {
        // Just logout from server, keep cache
        m_megaApi->localLogout(m_listener.get());
    }

    // Wait for logout to complete
    m_listener->waitForCompletion(10);

    // Clear auth state
    clearAuthState();
    m_isLoggedIn = false;

    // Trigger callback with logout result
    if (m_authCallback) {
        AuthResult result{false, "", "Logged out", mega::MegaError::API_OK, false};
        m_authCallback(result);
    }
}

// Check if currently logged in
bool AuthenticationModule::isLoggedIn() const {
    // Double-check with SDK
    int loginStatus = m_megaApi->isLoggedIn();
    return m_isLoggedIn && (loginStatus > 0);  // 0 means not logged in
}

// Get current session key
std::string AuthenticationModule::getSessionKey() const {
    if (!m_isLoggedIn) {
        return "";
    }

    // Get fresh session from SDK
    char* session = m_megaApi->dumpSession();
    std::string sessionKey;
    if (session) {
        sessionKey = session;
        delete[] session;
    }

    return sessionKey;
}

// Save session to encrypted file
bool AuthenticationModule::saveSession(const std::string& filePath,
                                      const std::string& encryptionKey) {
    if (!m_isLoggedIn || m_currentSessionKey.empty()) {
        return false;
    }

    try {
        // Encrypt session key
        std::string encrypted = encryptData(m_currentSessionKey, encryptionKey);

        // Save to file
        std::ofstream file(filePath, std::ios::binary);
        if (!file) {
            return false;
        }

        file.write(encrypted.c_str(), encrypted.size());
        file.close();

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to save session: " << e.what() << std::endl;
        return false;
    }
}

// Load session from encrypted file
std::string AuthenticationModule::loadSession(const std::string& filePath,
                                             const std::string& encryptionKey) {
    try {
        // Read encrypted data
        std::ifstream file(filePath, std::ios::binary);
        if (!file) {
            return "";
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string encrypted = buffer.str();
        file.close();

        // Decrypt session key
        std::string sessionKey = decryptData(encrypted, encryptionKey);

        return sessionKey;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load session: " << e.what() << std::endl;
        return "";
    }
}

// Get account information
AccountInfo AuthenticationModule::getAccountInfo() const {
    AccountInfo info{};

    if (!m_isLoggedIn) {
        return info;
    }

    // Get user information
    mega::MegaUser* user = m_megaApi->getMyUser();
    if (user) {
        const char* email = user->getEmail();
        if (email) {
            info.email = email;
        }
        delete user;
    }

    // Note: Account details require async request with getAccountDetails()
    // For now, return basic info. Full implementation would need async callback
    // m_megaApi->getAccountDetails(m_listener.get());

    // Get basic info that's available synchronously
    info.accountType = m_megaApi->isLoggedIn();  // Returns account type

    return info;
}

// Change account password
bool AuthenticationModule::changePassword(const std::string& currentPassword,
                                         const std::string& newPassword) {
    if (!m_isLoggedIn) {
        return false;
    }

    // Check password strength
    if (checkPasswordStrength(newPassword) < 50) {
        std::cerr << "New password is too weak" << std::endl;
        return false;
    }

    // Reset listener
    m_listener->reset();

    // Change password
    m_megaApi->changePassword(currentPassword.c_str(), newPassword.c_str(), m_listener.get());

    // Wait for completion
    if (!m_listener->waitForCompletion(30)) {
        return false;
    }

    return m_listener->getLastError() == mega::MegaError::API_OK;
}

// Enable 2FA for account
std::string AuthenticationModule::enable2FA() {
    if (!m_isLoggedIn) {
        return "";
    }

    // Reset listener
    m_listener->reset();

    // Get 2FA secret
    m_megaApi->multiFactorAuthGetCode(m_listener.get());

    // Wait for completion
    if (!m_listener->waitForCompletion(30)) {
        return "";
    }

    if (m_listener->getLastError() != mega::MegaError::API_OK) {
        return "";
    }

    // The secret would be in the request result
    // This is a simplified implementation
    return "MEGA2FA_SECRET_KEY";  // Placeholder - actual implementation would extract from request
}

// Disable 2FA for account
bool AuthenticationModule::disable2FA(const std::string& pin) {
    if (!m_isLoggedIn || pin.empty()) {
        return false;
    }

    // Reset listener
    m_listener->reset();

    // Disable 2FA
    m_megaApi->multiFactorAuthDisable(pin.c_str(), m_listener.get());

    // Wait for completion
    if (!m_listener->waitForCompletion(30)) {
        return false;
    }

    return m_listener->getLastError() == mega::MegaError::API_OK;
}

// Check if 2FA is enabled
bool AuthenticationModule::is2FAEnabled() const {
    if (!m_isLoggedIn) {
        return false;
    }

    // Check 2FA status
    return m_megaApi->multiFactorAuthAvailable();
}

// Register new account
AuthResult AuthenticationModule::registerAccount(const std::string& email,
                                                const std::string& password,
                                                const std::string& name) {
    AuthResult result{false, "", "", mega::MegaError::API_OK, false};

    if (!isValidEmail(email)) {
        result.errorMessage = "Invalid email format";
        result.errorCode = mega::MegaError::API_EARGS;
        return result;
    }

    if (checkPasswordStrength(password) < 50) {
        result.errorMessage = "Password is too weak";
        result.errorCode = mega::MegaError::API_EARGS;
        return result;
    }

    // Reset listener
    m_listener->reset();

    // Create account
    m_megaApi->createAccount(email.c_str(), password.c_str(),
                            name.c_str(), nullptr, m_listener.get());

    // Wait for completion
    if (!m_listener->waitForCompletion(60)) {
        result.errorMessage = "Registration timeout";
        result.errorCode = mega::MegaError::API_EAGAIN;
        return result;
    }

    // Process result
    result = processAuthRequest(nullptr, nullptr);

    if (result.success) {
        result.errorMessage = "Account created. Please check email for verification.";
    }

    return result;
}

// Verify account with confirmation link
bool AuthenticationModule::verifyAccount(const std::string& confirmationLink,
                                        const std::string& email) {
    if (confirmationLink.empty() || !isValidEmail(email)) {
        return false;
    }

    // Reset listener
    m_listener->reset();

    // Confirm account - the deprecated warning suggests using confirmAccount with password
    // The email parameter is actually used as password in the new API
    m_megaApi->confirmAccount(confirmationLink.c_str(), email.c_str(), m_listener.get());

    // Wait for completion
    if (!m_listener->waitForCompletion(30)) {
        return false;
    }

    return m_listener->getLastError() == mega::MegaError::API_OK;
}

// Request password reset
bool AuthenticationModule::requestPasswordReset(const std::string& email) {
    if (!isValidEmail(email)) {
        return false;
    }

    // Reset listener
    m_listener->reset();

    // Reset password
    m_megaApi->resetPassword(email.c_str(), true, m_listener.get());

    // Wait for completion
    if (!m_listener->waitForCompletion(30)) {
        return false;
    }

    return m_listener->getLastError() == mega::MegaError::API_OK;
}

// Confirm password reset
bool AuthenticationModule::confirmPasswordReset(const std::string& resetLink,
                                               const std::string& newPassword) {
    if (resetLink.empty() || checkPasswordStrength(newPassword) < 50) {
        return false;
    }

    // Reset listener
    m_listener->reset();

    // Confirm reset - API requires masterKey parameter (can be NULL)
    m_megaApi->confirmResetPassword(resetLink.c_str(), newPassword.c_str(), nullptr, m_listener.get());

    // Wait for completion
    if (!m_listener->waitForCompletion(30)) {
        return false;
    }

    return m_listener->getLastError() == mega::MegaError::API_OK;
}

// Set callback for authentication events
void AuthenticationModule::setAuthCallback(std::function<void(const AuthResult&)> callback) {
    m_authCallback = callback;
}

// Compute password hash (static)
std::string AuthenticationModule::computePasswordHash(const std::string& password) {
    if (password.empty()) {
        return "";
    }

    // Use SHA256 for hashing
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(password.c_str()),
           password.size(), hash);

    // Convert to hex string
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }

    return ss.str();
}

// Validate email format (static)
bool AuthenticationModule::isValidEmail(const std::string& email) {
    const std::regex pattern(R"(^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$)");
    return std::regex_match(email, pattern);
}

// Check password strength (static)
int AuthenticationModule::checkPasswordStrength(const std::string& password) {
    if (password.empty()) {
        return 0;
    }

    int score = 0;

    // Length score
    if (password.length() >= 8) score += 20;
    if (password.length() >= 12) score += 20;
    if (password.length() >= 16) score += 10;

    // Character variety
    bool hasLower = false, hasUpper = false, hasDigit = false, hasSpecial = false;
    for (char c : password) {
        if (std::islower(c)) hasLower = true;
        else if (std::isupper(c)) hasUpper = true;
        else if (std::isdigit(c)) hasDigit = true;
        else hasSpecial = true;
    }

    if (hasLower) score += 10;
    if (hasUpper) score += 10;
    if (hasDigit) score += 10;
    if (hasSpecial) score += 20;

    return std::min(score, 100);
}

// Private: Process authentication request result
AuthResult AuthenticationModule::processAuthRequest(mega::MegaRequest* request,
                                                   mega::MegaError* error) {
    AuthResult result{false, "", "", mega::MegaError::API_OK, false};

    // Get error from listener if not provided
    int errorCode = m_listener->getLastError();
    std::string errorString = m_listener->getErrorString();

    result.errorCode = errorCode;
    result.requires2FA = m_listener->requires2FA();

    if (errorCode == mega::MegaError::API_OK) {
        result.success = true;
        result.errorMessage = "Authentication successful";
    } else if (errorCode == mega::MegaError::API_EMFAREQUIRED) {
        result.errorMessage = "2FA required";
        result.requires2FA = true;
    } else {
        // Provide detailed error messages based on error code
        std::string detailedError;
        switch (errorCode) {
            case mega::MegaError::API_ENOENT:
                detailedError = "Invalid email or password. Please check your credentials";
                break;
            case mega::MegaError::API_ETOOMANY:
                detailedError = "Too many login attempts. Please wait before trying again";
                break;
            case mega::MegaError::API_EINCOMPLETE:
                detailedError = "Account not confirmed. Please check your email for verification link";
                break;
            case mega::MegaError::API_EBLOCKED:
                detailedError = "Account has been suspended. Contact support@mega.nz for assistance";
                break;
            case mega::MegaError::API_ESID:
                detailedError = "Invalid or expired session. Please login again";
                break;
            case mega::MegaError::API_ESSL:
                detailedError = "SSL/TLS connection error. Check your network security settings";
                break;
            case mega::MegaError::API_EAGAIN:
                detailedError = "Service temporarily unavailable. Please try again in a few moments";
                break;
            case mega::MegaError::API_ERATELIMIT:
                detailedError = "Rate limit exceeded. Please wait a moment before trying again";
                break;
            case mega::MegaError::API_EEXPIRED:
                detailedError = "Session expired. Please login again";
                break;
            case mega::MegaError::API_EARGS:
                detailedError = "Invalid arguments provided";
                break;
            default:
                detailedError = errorString.empty() ?
                    "Authentication failed (error code: " + std::to_string(errorCode) + ")" :
                    errorString + " (error code: " + std::to_string(errorCode) + ")";
                break;
        }
        result.errorMessage = detailedError;
    }

    return result;
}

// Private: Clear authentication state
void AuthenticationModule::clearAuthState() {
    secureErase(m_currentSessionKey);
    secureErase(m_pendingEmail);
    secureErase(m_pendingPassword);
}

// Private: Encrypt data using AES-256-GCM
std::string AuthenticationModule::encryptData(const std::string& data,
                                             const std::string& key) {
    try {
        // Derive a proper 32-byte key from the user-provided key
        auto salt = megacustom::Crypto::generateSalt(16);
        std::string derivedKey = megacustom::Crypto::deriveKey(key, salt, 10000);

        // Encrypt the data
        std::string encrypted = megacustom::Crypto::encrypt(data, derivedKey);

        // Prepend salt (base64 encoded) to the ciphertext for decryption
        std::string saltBase64;
        for (unsigned char byte : salt) {
            saltBase64 += byte;
        }
        // Simple base64 encode the salt manually or use our internal method
        // For simplicity, store as: salt_size (1 byte) + salt + encrypted
        std::string result;
        result += static_cast<char>(salt.size());
        result.append(reinterpret_cast<const char*>(salt.data()), salt.size());
        result += encrypted;

        return result;
    } catch (const megacustom::CryptoException& e) {
        std::cerr << "Encryption error: " << e.what() << std::endl;
        return "";
    }
}

// Private: Decrypt data using AES-256-GCM
std::string AuthenticationModule::decryptData(const std::string& encryptedData,
                                             const std::string& key) {
    if (encryptedData.size() < 2) {
        return "";
    }

    try {
        // Extract salt size and salt
        size_t saltSize = static_cast<unsigned char>(encryptedData[0]);
        if (encryptedData.size() < 1 + saltSize + 1) {
            std::cerr << "Invalid encrypted data format" << std::endl;
            return "";
        }

        std::vector<unsigned char> salt(encryptedData.begin() + 1,
                                         encryptedData.begin() + 1 + saltSize);
        std::string ciphertext = encryptedData.substr(1 + saltSize);

        // Derive the same key
        std::string derivedKey = megacustom::Crypto::deriveKey(key, salt, 10000);

        // Decrypt
        return megacustom::Crypto::decrypt(ciphertext, derivedKey);
    } catch (const megacustom::CryptoException& e) {
        std::cerr << "Decryption error: " << e.what() << std::endl;
        return "";
    }
}

} // namespace MegaCustom