#ifndef MEGA_MANAGER_H
#define MEGA_MANAGER_H

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>

// Forward declarations
namespace mega {
    class MegaApi;
    class MegaListener;
    class MegaRequest;
    class MegaTransfer;
    class MegaError;
    class MegaNode;
}

namespace MegaCustom {

// Forward declaration
class MegaManagerListener;

/**
 * Main controller class for the Mega Custom Application
 * Manages authentication, operations, and coordinates all modules
 */
class MegaManager {
    // Friend class for listener access
    friend class MegaManagerListener;

public:
    // Singleton pattern for global access
    static MegaManager& getInstance();

    // Delete copy constructor and assignment operator
    MegaManager(const MegaManager&) = delete;
    MegaManager& operator=(const MegaManager&) = delete;

    /**
     * Initialize the Mega SDK with app key
     * @param appKey The application key from Mega
     * @param basePath Optional base path for local cache
     * @return true if initialization successful
     */
    bool initialize(const std::string& appKey, const std::string& basePath = "");

    /**
     * Check if MegaManager is initialized
     * @return true if initialized
     */
    bool isInitialized() const;

    /**
     * Login to Mega account
     * @param email User email
     * @param password User password
     * @param sessionKey Optional session key for persistent login
     * @return true if login successful
     */
    bool login(const std::string& email, const std::string& password, const std::string& sessionKey = "");

    /**
     * Login with 2FA
     * @param email User email
     * @param password User password
     * @param pin 2FA pin code
     * @return true if login successful
     */
    bool loginWith2FA(const std::string& email, const std::string& password, const std::string& pin);

    /**
     * Logout from current session
     */
    void logout();

    /**
     * Get current session key for persistent login
     * @return Session key string
     */
    std::string getSessionKey() const;

    /**
     * Check if user is logged in
     * @return true if logged in
     */
    bool isLoggedIn() const;

    /**
     * Get the Mega API instance
     * @return Pointer to MegaApi
     */
    mega::MegaApi* getMegaApi() const;

    /**
     * Set bandwidth limits
     * @param downloadLimit Download limit in bytes/sec (0 = unlimited)
     * @param uploadLimit Upload limit in bytes/sec (0 = unlimited)
     */
    void setBandwidthLimits(int downloadLimit, int uploadLimit);

    /**
     * Get account information
     * @return Account details as JSON string
     */
    std::string getAccountInfo() const;

    /**
     * Enable/disable debug logging
     * @param enable Enable flag
     * @param logLevel Log level (0-5)
     */
    void setDebugLogging(bool enable, int logLevel = 3);

    /**
     * Get root node of the account
     * @return Pointer to root MegaNode
     */
    mega::MegaNode* getRootNode() const;

    /**
     * Get node by path
     * @param path Path from root (e.g., "/folder/subfolder")
     * @return Pointer to MegaNode or nullptr if not found
     */
    mega::MegaNode* getNodeByPath(const std::string& path) const;

    /**
     * Register a global progress callback
     * @param callback Function to call with progress updates
     */
    void setProgressCallback(std::function<void(int, int, long long, long long)> callback);

    /**
     * Set error callback
     * @param callback Function to call on errors
     */
    void setErrorCallback(std::function<void(int, const std::string&)> callback);

    /**
     * Shutdown the manager and cleanup resources
     */
    void shutdown();

    /**
     * Get last error message
     * @return Error message string
     */
    std::string getLastError() const;

    /**
     * Check if operation is in progress
     * @return true if any operation is running
     */
    bool isOperationInProgress() const;

    /**
     * Cancel all ongoing operations
     */
    void cancelAllOperations();

    /**
     * Get storage quota information
     * @return Quota info as JSON string
     */
    std::string getStorageQuota() const;

private:
    // Private constructor for singleton
    MegaManager();
    ~MegaManager();

    // Mega SDK instance
    std::unique_ptr<mega::MegaApi> m_megaApi;

    // Listener for Mega events
    std::unique_ptr<mega::MegaListener> m_listener;

    // State management
    std::atomic<bool> m_isLoggedIn;
    std::atomic<bool> m_isInitialized;
    std::atomic<bool> m_operationInProgress;

    // Thread safety
    mutable std::mutex m_mutex;

    // Callbacks
    std::function<void(int, int, long long, long long)> m_progressCallback;
    std::function<void(int, const std::string&)> m_errorCallback;

    // Error handling
    mutable std::string m_lastError;

    // Session management
    std::string m_currentSessionKey;

    // Internal helper methods
    void handleMegaRequest(mega::MegaRequest* request, mega::MegaError* error);
    void handleMegaTransfer(mega::MegaTransfer* transfer, mega::MegaError* error);
    void updateLastError(const mega::MegaError* error);
};

} // namespace MegaCustom

#endif // MEGA_MANAGER_H