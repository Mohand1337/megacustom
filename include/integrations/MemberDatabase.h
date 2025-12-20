#ifndef MEMBER_DATABASE_H
#define MEMBER_DATABASE_H

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <functional>
#include <cstdint>

namespace MegaCustom {

/**
 * Member data structure
 * Represents a member with their info and MEGA folder binding
 */
struct Member {
    std::string id;              // Unique member ID (e.g., "EGB001")
    std::string name;            // Display name
    std::string email;           // Email address
    std::string ipAddress;       // IP address for watermark
    std::string macAddress;      // MAC address for watermark
    std::string socialHandle;    // Social media handle

    // Flexible custom fields for additional data
    std::map<std::string, std::string> customFields;

    // MEGA folder binding
    std::string megaFolderPath;   // e.g., "/Members/John_EGB001/"
    std::string megaFolderHandle; // MEGA node handle for fast access

    // Watermark configuration per member
    std::vector<std::string> watermarkFields;  // e.g., {"name", "email", "ip"}
    bool useGlobalWatermark = false;           // Override with global only

    // WordPress sync info
    std::string wpUserId;         // WordPress user ID
    int64_t lastSynced = 0;       // Unix timestamp of last WP sync

    // Status
    bool active = true;           // Whether member is active
    int64_t createdAt = 0;        // Unix timestamp when created
    int64_t updatedAt = 0;        // Unix timestamp when last updated

    /**
     * Build watermark text based on selected fields
     * @param brandText Optional brand text to prepend
     * @return Formatted watermark text
     */
    std::string buildWatermarkText(const std::string& brandText = "") const;

    /**
     * Build secondary watermark line (email, IP, etc.)
     * @return Formatted secondary text
     */
    std::string buildSecondaryWatermarkText() const;

    /**
     * Check if member has a valid MEGA folder binding
     */
    bool hasFolderBinding() const { return !megaFolderPath.empty(); }

    /**
     * Get display string for member
     */
    std::string getDisplayString() const;
};

/**
 * Member filter options for queries
 */
struct MemberFilter {
    std::string searchText;      // Search in id, name, email
    bool activeOnly = false;     // Only active members
    bool withFolderBinding = false;  // Only members with MEGA folder bound
    std::string wpSyncStatus;    // "synced", "unsynced", "all"
};

/**
 * Result of member operations
 */
struct MemberResult {
    bool success = false;
    std::string error;
    std::optional<Member> member;
    std::vector<Member> members;  // For list operations
};

/**
 * Member database class
 * Handles persistence and CRUD operations for members
 *
 * IMPORTANT: This class shares storage with the Qt MemberRegistry class.
 * Both read/write to ~/.config/MegaCustom/members.json to ensure a single
 * source of truth for member data across C++ core and Qt GUI layers.
 *
 * The JSON format is compatible between both implementations:
 * - Uses "displayName" (Qt format) and "name" (C++ format) interchangeably
 * - Uses "distributionFolder" (Qt format) and "megaFolderPath" (C++ format) interchangeably
 */
class MemberDatabase {
public:
    /**
     * Constructor
     * @param storagePath Path to storage file (default: ~/.config/MegaCustom/members.json)
     */
    explicit MemberDatabase(const std::string& storagePath = "");

    ~MemberDatabase() = default;

    // Prevent copying
    MemberDatabase(const MemberDatabase&) = delete;
    MemberDatabase& operator=(const MemberDatabase&) = delete;

    // ==================== CRUD Operations ====================

    /**
     * Add a new member
     * @param member Member to add
     * @return Result with success status
     */
    MemberResult addMember(const Member& member);

    /**
     * Update an existing member
     * @param member Member with updated data (id must match existing)
     * @return Result with success status
     */
    MemberResult updateMember(const Member& member);

    /**
     * Remove a member by ID
     * @param memberId Member ID to remove
     * @return Result with success status
     */
    MemberResult removeMember(const std::string& memberId);

    /**
     * Get a member by ID
     * @param memberId Member ID to find
     * @return Result with member if found
     */
    MemberResult getMember(const std::string& memberId) const;

    /**
     * Get all members, optionally filtered
     * @param filter Optional filter criteria
     * @return Result with list of members
     */
    MemberResult getAllMembers(const MemberFilter& filter = {}) const;

    /**
     * Check if a member exists
     * @param memberId Member ID to check
     */
    bool memberExists(const std::string& memberId) const;

    /**
     * Get total member count
     */
    size_t getMemberCount() const { return m_members.size(); }

    // ==================== Batch Operations ====================

    /**
     * Import members from CSV file
     * @param csvPath Path to CSV file
     * @param skipHeader Whether first row is header
     * @return Result with imported members
     */
    MemberResult importFromCsv(const std::string& csvPath, bool skipHeader = true);

    /**
     * Export members to CSV file
     * @param csvPath Path to output CSV file
     * @param filter Optional filter for which members to export
     * @return Result with success status
     */
    MemberResult exportToCsv(const std::string& csvPath, const MemberFilter& filter = {}) const;

    /**
     * Import members from JSON file
     * @param jsonPath Path to JSON file
     * @return Result with imported members
     */
    MemberResult importFromJson(const std::string& jsonPath);

    /**
     * Export members to JSON file
     * @param jsonPath Path to output JSON file
     * @param filter Optional filter
     * @return Result with success status
     */
    MemberResult exportToJson(const std::string& jsonPath, const MemberFilter& filter = {}) const;

    // ==================== MEGA Folder Operations ====================

    /**
     * Bind a member to a MEGA folder
     * @param memberId Member ID
     * @param folderPath MEGA folder path
     * @param folderHandle MEGA node handle (optional)
     * @return Result with success status
     */
    MemberResult bindFolder(const std::string& memberId,
                           const std::string& folderPath,
                           const std::string& folderHandle = "");

    /**
     * Unbind a member from their MEGA folder
     * @param memberId Member ID
     * @return Result with success status
     */
    MemberResult unbindFolder(const std::string& memberId);

    /**
     * Get all members with folder bindings
     * @return Result with members that have folder bindings
     */
    MemberResult getMembersWithFolders() const;

    // ==================== Watermark Configuration ====================

    /**
     * Set watermark fields for a member
     * @param memberId Member ID
     * @param fields Vector of field names to include in watermark
     * @return Result with success status
     */
    MemberResult setWatermarkFields(const std::string& memberId,
                                    const std::vector<std::string>& fields);

    /**
     * Get available watermark field names
     * @return Vector of available field names
     */
    static std::vector<std::string> getAvailableWatermarkFields();

    // ==================== WordPress Sync ====================

    /**
     * Update WordPress sync info for a member
     * @param memberId Member ID
     * @param wpUserId WordPress user ID
     * @return Result with success status
     */
    MemberResult setWordPressUserId(const std::string& memberId,
                                    const std::string& wpUserId);

    /**
     * Mark member as synced from WordPress
     * @param memberId Member ID
     * @return Result with success status
     */
    MemberResult markAsSynced(const std::string& memberId);

    /**
     * Get members that need WordPress sync
     * @return Result with unsynced members
     */
    MemberResult getUnsyncedMembers() const;

    // ==================== Persistence ====================

    /**
     * Save database to disk
     * @return true if successful
     */
    bool save();

    /**
     * Reload database from disk
     * @return true if successful
     */
    bool reload();

    /**
     * Get the storage file path
     */
    std::string getStoragePath() const { return m_storagePath; }

    /**
     * Check if database has unsaved changes
     */
    bool hasUnsavedChanges() const { return m_dirty; }

    // ==================== Callbacks ====================

    using MemberCallback = std::function<void(const Member&)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    void setOnMemberAdded(MemberCallback callback) { m_onMemberAdded = callback; }
    void setOnMemberUpdated(MemberCallback callback) { m_onMemberUpdated = callback; }
    void setOnMemberRemoved(MemberCallback callback) { m_onMemberRemoved = callback; }
    void setOnError(ErrorCallback callback) { m_onError = callback; }

private:
    // Storage
    std::string m_storagePath;
    std::map<std::string, Member> m_members;  // Map by member ID
    bool m_dirty = false;

    // Callbacks
    MemberCallback m_onMemberAdded;
    MemberCallback m_onMemberUpdated;
    MemberCallback m_onMemberRemoved;
    ErrorCallback m_onError;

    // Internal helpers
    bool loadFromFile();
    bool saveToFile();
    std::string generateMemberId() const;
    void notifyError(const std::string& error);

    // JSON serialization
    std::string memberToJson(const Member& member) const;
    std::optional<Member> memberFromJson(const std::string& json) const;
    std::string allMembersToJson() const;
    bool loadMembersFromJson(const std::string& json);

    // CSV helpers
    std::vector<std::string> parseCsvLine(const std::string& line) const;
    std::string memberToCsvLine(const Member& member) const;
};

} // namespace MegaCustom

#endif // MEMBER_DATABASE_H
