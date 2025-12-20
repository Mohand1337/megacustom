/**
 * MegaManager Implementation
 * Main controller for the Mega Custom Application
 */

#include "core/MegaManager.h"
#include "core/ConfigManager.h"
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <cstring>

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

// Include real Mega SDK headers
// Define MEGA_SDK_AVAILABLE when the SDK library is built and linked
#ifdef MEGA_SDK_AVAILABLE
#include "megaapi.h"
#else
// Temporary minimal definitions until SDK is built
namespace mega {
    class MegaApi {
    public:
        MegaApi(const char*, const char*, const char*) {}
        bool isLoggedIn() { return false; }
        void setLogLevel(int) {}
        const char* getMyEmail() { return ""; }
    };
    class MegaListener {
    public:
        virtual ~MegaListener() {}
    };
    class MegaRequest {
    public:
        int getType() const { return 0; }
    };
    class MegaTransfer {
    public:
        int getType() const { return 0; }
        long long getTotalBytes() const { return 0; }
        long long getTransferredBytes() const { return 0; }
    };
    class MegaError {
    public:
        static const int API_OK = 0;
        int getErrorCode() const { return 0; }
        const char* getErrorString() const { return ""; }
    };
    class MegaNode {};
}
#endif

namespace MegaCustom {

// Internal listener implementation
class MegaManagerListener : public mega::MegaListener {
private:
    MegaManager* m_manager;

public:
    explicit MegaManagerListener(MegaManager* manager) : m_manager(manager) {}

    void onRequestFinish(mega::MegaApi* api, mega::MegaRequest* request, mega::MegaError* error) {
        if (m_manager) {
            m_manager->handleMegaRequest(request, error);
        }
    }

    void onTransferFinish(mega::MegaApi* api, mega::MegaTransfer* transfer, mega::MegaError* error) {
        if (m_manager) {
            m_manager->handleMegaTransfer(transfer, error);
        }
    }
};

// Constructor
MegaManager::MegaManager()
    : m_isLoggedIn(false)
    , m_isInitialized(false)
    , m_operationInProgress(false) {
    std::cout << "MegaManager: Initializing core..." << std::endl;
}

// Destructor
MegaManager::~MegaManager() {
    shutdown();
}

// Singleton instance
MegaManager& MegaManager::getInstance() {
    static MegaManager instance;
    return instance;
}

// Initialize the Mega SDK
bool MegaManager::initialize(const std::string& appKey, const std::string& basePath) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_isInitialized) {
        std::cout << "MegaManager: Already initialized" << std::endl;
        return true;
    }

    try {
        std::cout << "MegaManager: Initializing Mega SDK..." << std::endl;

        // Create Mega API instance
        std::string userAgent = "MegaCustomApp/1.0.0";
        m_megaApi = std::make_unique<mega::MegaApi>(
            appKey.c_str(),
            basePath.empty() ? nullptr : basePath.c_str(),
            userAgent.c_str()
        );

        // Create and register listener
        m_listener = std::make_unique<MegaManagerListener>(this);
        // m_megaApi->addListener(m_listener.get()); // Uncomment when SDK available

        // Load configuration
        ConfigManager& config = ConfigManager::getInstance();
        auto authConfig = config.getAuthConfig();

        // Set debug logging if configured
        auto uiConfig = config.getUIConfig();
        if (uiConfig.logLevel > 0) {
            setDebugLogging(true, uiConfig.logLevel);
        }

        m_isInitialized = true;
        std::cout << "MegaManager: Initialization complete" << std::endl;
        return true;

    } catch (const std::exception& e) {
        m_lastError = std::string("Initialization failed: ") + e.what();
        std::cerr << "MegaManager: " << m_lastError << std::endl;
        return false;
    }
}

// Check if MegaManager is initialized
bool MegaManager::isInitialized() const {
    return m_isInitialized;
}

// Login with email and password
bool MegaManager::login(const std::string& email, const std::string& password, const std::string& sessionKey) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_isInitialized) {
        m_lastError = "MegaManager not initialized";
        return false;
    }

    if (m_isLoggedIn) {
        std::cout << "MegaManager: Already logged in" << std::endl;
        return true;
    }

    m_operationInProgress = true;

    try {
        if (!sessionKey.empty()) {
            std::cout << "MegaManager: Logging in with session key..." << std::endl;
            // m_megaApi->fastLogin(sessionKey.c_str()); // Uncomment when SDK available
            m_currentSessionKey = sessionKey;
        } else {
            std::cout << "MegaManager: Logging in with email/password..." << std::endl;
            // m_megaApi->login(email.c_str(), password.c_str()); // Uncomment when SDK available
        }

        // Simulate async operation
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // For now, just set logged in status
        m_isLoggedIn = true;
        m_operationInProgress = false;

        // Save session for persistence
        ConfigManager& config = ConfigManager::getInstance();
        config.setString("auth.lastEmail", email);

        std::cout << "MegaManager: Login successful" << std::endl;
        return true;

    } catch (const std::exception& e) {
        m_lastError = std::string("Login failed: ") + e.what();
        std::cerr << "MegaManager: " << m_lastError << std::endl;
        m_operationInProgress = false;
        return false;
    }
}

// Login with 2FA
bool MegaManager::loginWith2FA(const std::string& email, const std::string& password, const std::string& pin) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_isInitialized) {
        m_lastError = "MegaManager not initialized";
        return false;
    }

    std::cout << "MegaManager: Logging in with 2FA..." << std::endl;

    // First attempt normal login
    // Then provide 2FA pin
    // m_megaApi->multiFactorAuthLogin(email.c_str(), password.c_str(), pin.c_str()); // When SDK available

    // Simulate for now
    m_isLoggedIn = true;
    std::cout << "MegaManager: 2FA login successful" << std::endl;
    return true;
}

// Logout
void MegaManager::logout() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_isLoggedIn) {
        return;
    }

    std::cout << "MegaManager: Logging out..." << std::endl;

    // m_megaApi->logout(); // Uncomment when SDK available

    m_isLoggedIn = false;
    secureErase(m_currentSessionKey);

    std::cout << "MegaManager: Logout complete" << std::endl;
}

// Get session key
std::string MegaManager::getSessionKey() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_isLoggedIn || !m_megaApi) {
        return "";
    }

    // return m_megaApi->dumpSession(); // When SDK available
    return m_currentSessionKey;
}

// Check login status
bool MegaManager::isLoggedIn() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_megaApi) {
        return false;
    }

    return m_megaApi->isLoggedIn();
}

// Get Mega API instance
mega::MegaApi* MegaManager::getMegaApi() const {
    return m_megaApi.get();
}

// Set bandwidth limits
void MegaManager::setBandwidthLimits(int downloadLimit, int uploadLimit) {
    if (!m_megaApi) return;

    std::cout << "MegaManager: Setting bandwidth limits - "
              << "Download: " << downloadLimit << " bytes/sec, "
              << "Upload: " << uploadLimit << " bytes/sec" << std::endl;

    // m_megaApi->setUploadLimit(uploadLimit); // When SDK available
    // m_megaApi->setDownloadLimit(downloadLimit); // When SDK available
}

// Get account information
std::string MegaManager::getAccountInfo() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_isLoggedIn || !m_megaApi) {
        return "{}";
    }

    // Build account info JSON
    std::string info = "{";
    info += "\"email\":\"" + std::string(m_megaApi->getMyEmail()) + "\",";
    info += "\"logged_in\":" + std::string(m_isLoggedIn ? "true" : "false");
    info += "}";

    return info;
}

// Enable debug logging
void MegaManager::setDebugLogging(bool enable, int logLevel) {
    if (!m_megaApi) return;

    std::cout << "MegaManager: Debug logging "
              << (enable ? "enabled" : "disabled")
              << " (level: " << logLevel << ")" << std::endl;

    m_megaApi->setLogLevel(logLevel);
}

// Get root node
mega::MegaNode* MegaManager::getRootNode() const {
    if (!m_megaApi || !m_isLoggedIn) {
        return nullptr;
    }

    // return m_megaApi->getRootNode(); // When SDK available
    return nullptr;
}

// Get node by path
mega::MegaNode* MegaManager::getNodeByPath(const std::string& path) const {
    if (!m_megaApi || !m_isLoggedIn) {
        return nullptr;
    }

    // return m_megaApi->getNodeByPath(path.c_str()); // When SDK available
    return nullptr;
}

// Set progress callback
void MegaManager::setProgressCallback(std::function<void(int, int, long long, long long)> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_progressCallback = callback;
}

// Set error callback
void MegaManager::setErrorCallback(std::function<void(int, const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_errorCallback = callback;
}

// Shutdown
void MegaManager::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_isInitialized) {
        return;
    }

    std::cout << "MegaManager: Shutting down..." << std::endl;

    // Logout if logged in
    if (m_isLoggedIn) {
        logout();
    }

    // Remove listener
    if (m_megaApi && m_listener) {
        // m_megaApi->removeListener(m_listener.get()); // When SDK available
    }

    // Clear resources
    m_listener.reset();
    m_megaApi.reset();

    m_isInitialized = false;

    std::cout << "MegaManager: Shutdown complete" << std::endl;
}

// Get last error
std::string MegaManager::getLastError() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastError;
}

// Check if operation in progress
bool MegaManager::isOperationInProgress() const {
    return m_operationInProgress.load();
}

// Cancel all operations
void MegaManager::cancelAllOperations() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_megaApi) return;

    std::cout << "MegaManager: Cancelling all operations..." << std::endl;

    // m_megaApi->cancelTransfers(); // When SDK available

    m_operationInProgress = false;
}

// Get storage quota
std::string MegaManager::getStorageQuota() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_isLoggedIn || !m_megaApi) {
        return "{}";
    }

    // Build quota info JSON
    std::string quota = "{";
    quota += "\"used\":0,";
    quota += "\"total\":0,";
    quota += "\"percentage\":0";
    quota += "}";

    // When SDK available:
    // m_megaApi->getAccountDetails();
    // Then parse the response

    return quota;
}

// Handle Mega request callback
void MegaManager::handleMegaRequest(mega::MegaRequest* request, mega::MegaError* error) {
    if (!request || !error) return;

    if (error->getErrorCode() != mega::MegaError::API_OK) {
        updateLastError(error);
        if (m_errorCallback) {
            m_errorCallback(error->getErrorCode(), error->getErrorString());
        }
    }

    // Handle specific request types
    switch (request->getType()) {
        // Add cases for different request types when SDK available
        default:
            break;
    }
}

// Handle Mega transfer callback
void MegaManager::handleMegaTransfer(mega::MegaTransfer* transfer, mega::MegaError* error) {
    if (!transfer) return;

    if (error && error->getErrorCode() != mega::MegaError::API_OK) {
        updateLastError(error);
        if (m_errorCallback) {
            m_errorCallback(error->getErrorCode(), error->getErrorString());
        }
        return;
    }

    // Call progress callback
    if (m_progressCallback) {
        m_progressCallback(
            transfer->getType(),
            0, // Progress percentage - calculate when SDK available
            transfer->getTransferredBytes(),
            transfer->getTotalBytes()
        );
    }
}

// Update last error
void MegaManager::updateLastError(const mega::MegaError* error) {
    if (!error) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastError = std::string("Error ") + std::to_string(error->getErrorCode())
                  + ": " + error->getErrorString();
}

} // namespace MegaCustom