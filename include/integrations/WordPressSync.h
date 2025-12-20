#ifndef WORDPRESS_SYNC_H
#define WORDPRESS_SYNC_H

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <cstdint>

namespace MegaCustom {

// Forward declaration
struct Member;

/**
 * WordPress site configuration
 */
struct WordPressConfig {
    std::string siteUrl;          // e.g., "https://example.com"
    std::string username;         // WordPress username
    std::string applicationPassword;  // WordPress application password (not user password)

    // API endpoints (relative to siteUrl)
    std::string usersEndpoint = "/wp-json/wp/v2/users";
    std::string customEndpoint;   // Optional custom endpoint for member data

    // Field mappings: WordPress field -> Member field
    std::map<std::string, std::string> fieldMappings;

    // Sync options
    bool syncAllFields = true;    // Sync all available fields
    bool createNewMembers = true; // Create members that don't exist locally
    bool updateExisting = true;   // Update existing members with WP data
    int perPage = 100;            // Users per page for API requests
    int timeout = 30;             // Request timeout in seconds

    // Filters
    std::string roleFilter;       // Filter users by role (empty = all roles)
};

/**
 * Result of a single user sync
 */
struct UserSyncResult {
    bool success = false;
    std::string wpUserId;
    std::string memberId;         // Local member ID (if synced/created)
    std::string action;           // "created", "updated", "skipped", "error"
    std::string error;

    // WordPress user data retrieved
    std::map<std::string, std::string> wpData;
};

/**
 * Result of a sync operation
 */
struct SyncResult {
    bool success = false;
    std::string error;

    int totalUsers = 0;           // Total WP users found
    int usersCreated = 0;         // New members created
    int usersUpdated = 0;         // Existing members updated
    int usersSkipped = 0;         // Skipped (no changes or filtered out)
    int usersFailed = 0;          // Failed to sync

    std::vector<UserSyncResult> results;  // Per-user results

    int64_t syncStartTime = 0;
    int64_t syncEndTime = 0;
};

/**
 * Progress callback for WordPress sync operations
 */
struct WpSyncProgress {
    int currentUser = 0;
    int totalUsers = 0;
    std::string currentUsername;
    std::string status;           // "fetching", "syncing", "complete"
    double percentComplete = 0.0;
};

using WpSyncProgressCallback = std::function<void(const WpSyncProgress&)>;

/**
 * WordPress user data for preview
 */
struct WpUser {
    int id = 0;
    std::string username;
    std::string displayName;
    std::string email;
    std::string role;
    std::string registeredDate;   // ISO 8601 format
    std::map<std::string, std::string> meta;  // Custom meta fields
};

/**
 * WordPressSync - Syncs member data from WordPress via REST API
 *
 * Supports:
 * - WordPress REST API v2 (WP 4.7+)
 * - Application Passwords (WP 5.6+)
 * - Custom endpoints for membership plugins
 * - Field mapping for flexible data import
 */
class WordPressSync {
public:
    WordPressSync();
    ~WordPressSync() = default;

    // ==================== Configuration ====================

    /**
     * Set WordPress configuration
     */
    void setConfig(const WordPressConfig& config) { m_config = config; }
    WordPressConfig getConfig() const { return m_config; }

    /**
     * Set progress callback
     */
    void setProgressCallback(WpSyncProgressCallback callback) {
        m_progressCallback = callback;
    }

    /**
     * Set path to member database
     * Default: ~/.megacustom/members.json
     */
    void setMemberDatabasePath(const std::string& path) { m_memberDbPath = path; }

    /**
     * Load configuration from file
     * Default: ~/.megacustom/wordpress.json
     */
    bool loadConfig(const std::string& configPath = "");

    /**
     * Save configuration to file
     */
    bool saveConfig(const std::string& configPath = "");

    // ==================== Connection Testing ====================

    /**
     * Test connection to WordPress site
     * @return true if connection successful and authenticated
     */
    bool testConnection(std::string& error);

    /**
     * Get WordPress site info
     * @return Map of site info (name, description, url, etc.)
     */
    std::map<std::string, std::string> getSiteInfo(std::string& error);

    /**
     * Get available user fields from WordPress
     * @return List of field names that can be synced
     */
    std::vector<std::string> getAvailableFields(std::string& error);

    // ==================== Sync Operations ====================

    /**
     * Sync all users from WordPress
     * @return Sync result with statistics
     */
    SyncResult syncAll();

    /**
     * Sync a specific WordPress user by ID
     * @param wpUserId WordPress user ID
     * @return Sync result for single user
     */
    SyncResult syncUser(const std::string& wpUserId);

    /**
     * Sync a specific WordPress user by email
     * @param email User email address
     * @return Sync result for single user
     */
    SyncResult syncUserByEmail(const std::string& email);

    /**
     * Sync users matching a role
     * @param role WordPress role (e.g., "subscriber", "customer")
     * @return Sync result
     */
    SyncResult syncByRole(const std::string& role);

    /**
     * Preview sync without making changes
     * Shows what would be created/updated
     * @return Sync result with preview data
     */
    SyncResult previewSync();

    /**
     * Fetch all users from WordPress (for preview/selection)
     * Does not sync to member database
     * @param error Output error message if failed
     * @return Vector of WordPress users
     */
    std::vector<WpUser> fetchAllUsers(std::string& error);

    // ==================== Field Mapping ====================

    /**
     * Set field mapping from WordPress field to Member field
     * @param wpField WordPress field name (e.g., "user_email", "meta.ip_address")
     * @param memberField Member struct field name (e.g., "email", "ipAddress")
     */
    void setFieldMapping(const std::string& wpField, const std::string& memberField);

    /**
     * Get default field mappings
     * Maps common WordPress fields to Member struct fields
     */
    static std::map<std::string, std::string> getDefaultFieldMappings();

    /**
     * Get list of supported Member fields for mapping
     */
    static std::vector<std::string> getSupportedMemberFields();

    // ==================== Utilities ====================

    /**
     * Cancel ongoing sync operation
     */
    void cancel() { m_cancelled = true; }
    bool isCancelled() const { return m_cancelled; }

    /**
     * Get last error message
     */
    std::string getLastError() const { return m_lastError; }

    /**
     * Build member ID from WordPress user data
     * Uses configurable pattern (default: WP{user_id})
     */
    std::string buildMemberId(const std::map<std::string, std::string>& wpData) const;

private:
    WordPressConfig m_config;
    WpSyncProgressCallback m_progressCallback;
    std::string m_memberDbPath;
    std::string m_lastError;
    bool m_cancelled = false;

    // HTTP request helpers
    struct HttpResponse {
        int statusCode = 0;
        std::string body;
        std::map<std::string, std::string> headers;
        std::string error;
    };

    /**
     * Make authenticated HTTP GET request
     */
    HttpResponse httpGet(const std::string& url);

    /**
     * Make authenticated HTTP POST request
     */
    HttpResponse httpPost(const std::string& url, const std::string& body);

    /**
     * Build Authorization header for Basic Auth
     */
    std::string buildAuthHeader() const;

    /**
     * Parse JSON response to map
     */
    std::map<std::string, std::string> parseUserJson(const std::string& json);

    /**
     * Parse JSON array of users
     */
    std::vector<std::map<std::string, std::string>> parseUsersJson(const std::string& json);

    /**
     * Convert WordPress user data to Member struct
     */
    Member wpDataToMember(const std::map<std::string, std::string>& wpData);

    /**
     * Merge WordPress data into existing member
     */
    void mergeWpDataToMember(Member& member, const std::map<std::string, std::string>& wpData);

    /**
     * Get config file path
     */
    std::string getConfigFilePath() const;

    /**
     * Report progress
     */
    void reportProgress(int current, int total, const std::string& username, const std::string& status);

    /**
     * URL encode string
     */
    static std::string urlEncode(const std::string& str);

    /**
     * Base64 encode string
     */
    static std::string base64Encode(const std::string& str);
};

} // namespace MegaCustom

#endif // WORDPRESS_SYNC_H
