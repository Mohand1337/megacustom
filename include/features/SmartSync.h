#ifndef SMART_SYNC_H
#define SMART_SYNC_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <memory>
#include <chrono>
#include <optional>
#include <atomic>
#include <climits>
#include <thread>

namespace mega {
    class MegaApi;
    class MegaNode;
    class MegaSync;
}

namespace MegaCustom {

/**
 * Sync direction modes
 */
enum class SyncDirection {
    BIDIRECTIONAL,    // Two-way sync (default)
    LOCAL_TO_REMOTE,  // Upload only (backup mode)
    REMOTE_TO_LOCAL,  // Download only (restore mode)
    MIRROR_LOCAL,     // Make remote identical to local
    MIRROR_REMOTE     // Make local identical to remote
};

/**
 * Conflict resolution strategies
 */
enum class ConflictResolution {
    ASK_USER,         // Prompt user for each conflict
    NEWER_WINS,       // Keep newer file
    OLDER_WINS,       // Keep older file
    LARGER_WINS,      // Keep larger file
    SMALLER_WINS,     // Keep smaller file
    LOCAL_WINS,       // Always keep local version
    REMOTE_WINS,      // Always keep remote version
    RENAME_BOTH,      // Keep both with renamed versions
    CUSTOM           // Use custom resolution function
};

/**
 * Sync filter configuration
 */
struct SyncFilter {
    // Include/exclude patterns
    std::vector<std::string> includePatterns;  // Glob patterns to include
    std::vector<std::string> excludePatterns;  // Glob patterns to exclude
    std::vector<std::string> includeExtensions;  // File extensions to include
    std::vector<std::string> excludeExtensions;  // File extensions to exclude

    // Size filters
    long long minFileSize = 0;        // Minimum file size (bytes)
    long long maxFileSize = LLONG_MAX;  // Maximum file size (bytes)

    // Date filters
    std::optional<std::chrono::system_clock::time_point> modifiedAfter;
    std::optional<std::chrono::system_clock::time_point> modifiedBefore;

    // Special filters
    bool excludeHiddenFiles = false;
    bool excludeSystemFiles = false;
    bool excludeTemporaryFiles = true;
    bool followSymlinks = false;

    // Custom filter function
    std::function<bool(const std::string& path, bool isDirectory)> customFilter;
};

/**
 * Sync configuration
 */
struct SyncConfig {
    std::string name;                    // Sync profile name
    std::string localPath;               // Local folder path
    std::string remotePath;              // Remote folder path
    SyncDirection direction;             // Sync direction
    ConflictResolution conflictStrategy; // Conflict resolution
    SyncFilter filter;                   // File filters

    // Performance settings
    int maxConcurrentTransfers = 4;
    int bandwidthLimit = 0;             // bytes/sec, 0 = unlimited
    bool useDeltaSync = true;           // Use checksum-based delta sync
    size_t chunkSize = 10 * 1024 * 1024;  // 10MB chunks

    // Behavior settings
    bool deleteOrphans = false;         // Delete files not in source
    bool preserveTimestamps = true;     // Preserve modification times
    bool preservePermissions = false;   // Preserve file permissions (Unix)
    bool caseInsensitive = false;       // Case-insensitive matching
    bool verifyTransfers = true;        // Verify checksums after transfer

    // Scheduling
    bool autoSync = false;               // Enable automatic sync
    std::chrono::minutes syncInterval{30};  // Auto-sync interval
    std::vector<std::chrono::system_clock::time_point> scheduledTimes;  // Specific times

    // Advanced options
    bool createBackups = true;          // Backup before overwriting
    int maxBackupVersions = 5;          // Maximum backup versions to keep
    bool syncEmptyFolders = true;       // Sync empty directories
    bool retryOnError = true;           // Retry failed operations
    int maxRetries = 3;                 // Maximum retry attempts
};

/**
 * File comparison result
 */
struct FileComparison {
    std::string path;
    bool existsLocal;
    bool existsRemote;
    long long localSize;
    long long remoteSize;
    std::chrono::system_clock::time_point localModTime;
    std::chrono::system_clock::time_point remoteModTime;
    std::string localChecksum;
    std::string remoteChecksum;
    bool isDifferent;
    std::string differenceReason;  // Size, time, checksum, etc.
};

/**
 * Sync conflict information
 */
struct SyncConflict {
    std::string path;
    std::string conflictType;  // "both_modified", "delete_modified", etc.
    FileComparison comparison;
    ConflictResolution suggestedResolution;
    std::string description;
};

/**
 * Sync operation plan
 */
struct SyncPlan {
    std::vector<std::string> filesToUpload;
    std::vector<std::string> filesToDownload;
    std::vector<std::string> filesToDelete;
    std::vector<std::string> foldersToCreate;
    std::vector<std::string> foldersToDelete;
    std::vector<SyncConflict> conflicts;
    long long totalUploadSize;
    long long totalDownloadSize;
    int estimatedTimeSeconds;
};

/**
 * Sync progress information
 */
struct SyncProgress {
    std::string syncName;
    int totalOperations;
    int completedOperations;
    int failedOperations;
    long long bytesTransferred;
    long long totalBytes;
    double progressPercentage;
    std::string currentOperation;
    std::string currentFile;
    std::chrono::seconds elapsedTime;
    std::chrono::seconds estimatedTimeRemaining;
    double currentSpeed;  // bytes/sec
};

/**
 * Sync result report
 */
struct SyncReport {
    std::string syncName;
    std::chrono::system_clock::time_point startTime;
    std::chrono::system_clock::time_point endTime;
    int filesUploaded;
    int filesDownloaded;
    int filesDeleted;
    int filesSkipped;
    int filesFailed;
    int conflictsResolved;
    long long bytesUploaded;
    long long bytesDownloaded;
    std::vector<std::string> errors;
    bool success;
};

/**
 * Intelligent folder synchronization engine
 */
class SmartSync {
public:
    explicit SmartSync(mega::MegaApi* megaApi);
    ~SmartSync();

    /**
     * Create a new sync profile
     * @param config Sync configuration
     * @return Sync profile ID
     */
    std::string createSyncProfile(const SyncConfig& config);

    /**
     * Update existing sync profile
     * @param profileId Profile to update
     * @param config New configuration
     * @return true if updated successfully
     */
    bool updateSyncProfile(const std::string& profileId, const SyncConfig& config);

    /**
     * Delete sync profile
     * @param profileId Profile to delete
     * @return true if deleted successfully
     */
    bool deleteSyncProfile(const std::string& profileId);

    /**
     * Get sync profile
     * @param profileId Profile ID
     * @return Sync configuration or nullopt
     */
    std::optional<SyncConfig> getSyncProfile(const std::string& profileId);

    /**
     * List all sync profiles
     * @return Vector of profile IDs and names
     */
    std::vector<std::pair<std::string, std::string>> listSyncProfiles();

    /**
     * Analyze folders and create sync plan
     * @param config Sync configuration
     * @param dryRun Don't execute, just analyze
     * @return Sync plan
     */
    SyncPlan analyzeFolders(const SyncConfig& config, bool dryRun = true);

    /**
     * Start synchronization
     * @param profileId Profile to sync
     * @return true if started successfully
     */
    bool startSync(const std::string& profileId);

    /**
     * Start sync with custom config (one-time sync)
     * @param config Sync configuration
     * @return Sync ID
     */
    std::string startCustomSync(const SyncConfig& config);

    /**
     * Pause synchronization
     * @param syncId Sync to pause
     * @return true if paused successfully
     */
    bool pauseSync(const std::string& syncId);

    /**
     * Resume paused sync
     * @param syncId Sync to resume
     * @return true if resumed successfully
     */
    bool resumeSync(const std::string& syncId);

    /**
     * Stop synchronization
     * @param syncId Sync to stop
     * @return true if stopped successfully
     */
    bool stopSync(const std::string& syncId);

    /**
     * Get sync progress
     * @param syncId Sync ID
     * @return Progress information or nullopt
     */
    std::optional<SyncProgress> getSyncProgress(const std::string& syncId);

    /**
     * Get all active syncs
     * @return Vector of sync IDs
     */
    std::vector<std::string> getActiveSyncs();

    /**
     * Get sync report
     * @param syncId Sync ID
     * @return Sync report or nullopt
     */
    std::optional<SyncReport> getSyncReport(const std::string& syncId);

    /**
     * Detect and report conflicts
     * @param config Sync configuration
     * @return Vector of conflicts
     */
    std::vector<SyncConflict> detectConflicts(const SyncConfig& config);

    /**
     * Resolve conflict
     * @param conflict Conflict to resolve
     * @param resolution Resolution strategy
     * @return true if resolved successfully
     */
    bool resolveConflict(const SyncConflict& conflict, ConflictResolution resolution);

    /**
     * Compare files for differences
     * @param localPath Local file path
     * @param remoteNode Remote file node
     * @return File comparison result
     */
    FileComparison compareFiles(const std::string& localPath, mega::MegaNode* remoteNode);

    /**
     * Calculate folder differences
     * @param localPath Local folder path
     * @param remotePath Remote folder path
     * @return Map of paths to comparisons
     */
    std::map<std::string, FileComparison> calculateDifferences(
        const std::string& localPath,
        const std::string& remotePath);

    /**
     * Enable auto-sync for profile
     * @param profileId Profile ID
     * @param interval Sync interval
     * @return true if enabled successfully
     */
    bool enableAutoSync(const std::string& profileId, std::chrono::minutes interval);

    /**
     * Disable auto-sync for profile
     * @param profileId Profile ID
     * @return true if disabled successfully
     */
    bool disableAutoSync(const std::string& profileId);

    /**
     * Schedule sync at specific time
     * @param profileId Profile ID
     * @param scheduleTime Time to sync
     * @return true if scheduled successfully
     */
    bool scheduleSync(const std::string& profileId,
                     std::chrono::system_clock::time_point scheduleTime);

    /**
     * Create backup before sync
     * @param path Path to backup
     * @return Backup ID or empty if failed
     */
    std::string createBackup(const std::string& path);

    /**
     * Restore from backup
     * @param backupId Backup to restore
     * @return true if restored successfully
     */
    bool restoreBackup(const std::string& backupId);

    /**
     * Set conflict resolver callback
     * @param resolver Function to resolve conflicts
     */
    void setConflictResolver(
        std::function<ConflictResolution(const SyncConflict&)> resolver);

    /**
     * Set progress callback
     * @param callback Function called with progress
     */
    void setProgressCallback(std::function<void(const SyncProgress&)> callback);

    /**
     * Set error callback
     * @param callback Function called on errors
     */
    void setErrorCallback(
        std::function<void(const std::string& syncId, const std::string& error)> callback);

    /**
     * Export sync profile
     * @param profileId Profile to export
     * @param filePath Export file path
     * @return true if exported successfully
     */
    bool exportProfile(const std::string& profileId, const std::string& filePath);

    /**
     * Import sync profile
     * @param filePath Import file path
     * @return Profile ID or empty if failed
     */
    std::string importProfile(const std::string& filePath);

    /**
     * Get sync statistics
     * @return JSON string with statistics
     */
    std::string getStatistics() const;

    /**
     * Verify sync integrity
     * @param profileId Profile to verify
     * @return Vector of integrity issues
     */
    std::vector<std::string> verifySyncIntegrity(const std::string& profileId);

private:
    mega::MegaApi* m_megaApi;

    // Sync management
    struct SyncInstance;
    std::map<std::string, std::unique_ptr<SyncConfig>> m_profiles;
    std::map<std::string, std::unique_ptr<SyncInstance>> m_activeSyncs;
    std::map<std::string, SyncReport> m_syncReports;

    // Scheduling
    struct ScheduledSync {
        std::string profileId;
        std::chrono::system_clock::time_point nextRun;
        std::chrono::minutes interval;
        bool enabled;
    };
    std::vector<ScheduledSync> m_scheduledSyncs;

    // Backup management
    struct BackupInfo {
        std::string backupId;
        std::string originalPath;
        std::string backupPath;
        std::chrono::system_clock::time_point timestamp;
    };
    std::map<std::string, BackupInfo> m_backups;

    // State
    std::atomic<bool> m_schedulerRunning;
    std::thread m_schedulerThread;  // Scheduler thread for proper cleanup

    // Callbacks
    std::function<ConflictResolution(const SyncConflict&)> m_conflictResolver;
    std::function<void(const SyncProgress&)> m_progressCallback;
    std::function<void(const std::string&, const std::string&)> m_errorCallback;

    // Statistics
    struct SyncStats {
        long long totalBytesUploaded;
        long long totalBytesDownloaded;
        int totalSyncs;
        int successfulSyncs;
        int failedSyncs;
        std::chrono::steady_clock::time_point startTime;
    } m_stats;

    // Helper methods
    std::string generateProfileId();
    std::string generateSyncId();
    std::string generateBackupId();
    void processScheduledSyncs();
    void executeSyncPlan(const SyncPlan& plan, SyncInstance* instance);
    bool shouldIncludeFile(const std::string& path, const SyncFilter& filter);
    std::string calculateChecksum(const std::string& filePath);
    mega::MegaNode* ensureRemotePath(const std::string& path);
    void performDeltaSync(const std::string& localFile, mega::MegaNode* remoteFile);
    SyncConflict detectFileConflict(const FileComparison& comparison);
    void cleanupOldBackups(const std::string& path, int maxVersions);

    // Sync instance implementation
    class SyncInstance;
    class SyncListener;
    std::unique_ptr<SyncListener> m_listener;
};

} // namespace MegaCustom

#endif // SMART_SYNC_H