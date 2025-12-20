#ifndef CLOUD_COPIER_H
#define CLOUD_COPIER_H

#include <string>
#include <vector>
#include <map>
#include <queue>
#include <functional>
#include <memory>
#include <atomic>
#include <optional>
#include <chrono>
#include <mutex>
#include <condition_variable>

namespace mega {
    class MegaApi;
    class MegaNode;
    class MegaRequest;
}

namespace MegaCustom {

/**
 * Operation mode - Copy vs Move
 */
enum class OperationMode {
    COPY,   // Copy files (keep originals)
    MOVE    // Move files (delete source after transfer)
};

/**
 * Conflict resolution options
 */
enum class ConflictResolution {
    SKIP,           // Skip the item
    OVERWRITE,      // Overwrite existing
    RENAME,         // Rename (add suffix)
    ASK,            // Ask user (default)
    SKIP_ALL,       // Skip all future conflicts
    OVERWRITE_ALL,  // Overwrite all future conflicts
    CANCEL          // Cancel the entire operation
};

/**
 * Copy destination configuration
 */
struct CopyDestination {
    std::string remotePath;                    // Target folder path (e.g., "/Backup/2025/")
    std::optional<std::string> newName;        // Optional rename for copied item
    bool createIfMissing = true;               // Create folder if doesn't exist
};

/**
 * Copy task configuration
 */
struct CopyTask {
    std::string taskId;
    std::string sourcePath;                    // Source file/folder path
    std::vector<CopyDestination> destinations; // Multiple destinations
    bool recursive = true;                     // For folders: copy contents recursively
    ConflictResolution defaultResolution = ConflictResolution::ASK;
};

/**
 * Copy progress information
 */
struct CopyProgress {
    std::string taskId;
    std::string currentItem;                   // Current file/folder being copied
    int totalItems;                            // Total copy operations
    int completedItems;                        // Successfully completed
    int failedItems;                           // Failed operations
    int skippedItems;                          // Skipped (conflicts, etc.)
    std::string currentDestination;            // Current destination path
    double overallProgress;                    // 0.0 to 100.0
};

/**
 * Copy result for a single operation
 */
struct CopyResult {
    bool success;
    std::string sourcePath;
    std::string destinationPath;
    std::string newNodeHandle;                 // Handle of newly copied node
    std::string errorMessage;
    int errorCode;
    bool skipped = false;                      // True if skipped due to conflict
};

/**
 * Copy report (task completion summary)
 */
struct CopyReport {
    std::string taskId;
    std::vector<CopyResult> results;
    int totalCopies;
    int successfulCopies;
    int failedCopies;
    int skippedCopies;
    std::chrono::system_clock::time_point startTime;
    std::chrono::system_clock::time_point endTime;
    std::map<std::string, int> destinationCounts;  // Files per destination
};

/**
 * Conflict information for callback
 */
struct CopyConflict {
    std::string sourcePath;
    std::string destinationPath;
    std::string existingName;
    long long existingSize;
    std::chrono::system_clock::time_point existingModTime;
    long long sourceSize;
    std::chrono::system_clock::time_point sourceModTime;
    bool isFolder;
};

/**
 * Copy template (saved destination sets)
 */
struct CopyTemplate {
    std::string name;
    std::vector<std::string> destinations;
    std::chrono::system_clock::time_point created;
    std::chrono::system_clock::time_point lastUsed;
};

/**
 * Cloud-to-Cloud copy manager for MEGA
 * Copies files/folders within the same MEGA account to multiple destinations
 */
class CloudCopier {
public:
    explicit CloudCopier(mega::MegaApi* megaApi);
    ~CloudCopier();

    // ===== Single/Multi-destination Copy =====

    /**
     * Copy a file or folder to a single destination
     * @param sourcePath Source path in MEGA cloud
     * @param destinationPath Target folder path
     * @param newName Optional new name for the copy
     * @return Copy result
     */
    CopyResult copyTo(const std::string& sourcePath,
                      const std::string& destinationPath,
                      const std::optional<std::string>& newName = std::nullopt);

    /**
     * Copy a file or folder to multiple destinations
     * @param sourcePath Source path in MEGA cloud
     * @param destinations List of destinations
     * @return Task ID for tracking
     */
    std::string copyToMultiple(const std::string& sourcePath,
                               const std::vector<CopyDestination>& destinations);

    // ===== Single/Multi-destination Move =====

    /**
     * Move a file or folder to a single destination
     * Uses server-side move (no bandwidth, atomic)
     * @param sourcePath Source path in MEGA cloud
     * @param destinationPath Target folder path
     * @param newName Optional new name for the moved item
     * @return Copy result (success, error info)
     */
    CopyResult moveTo(const std::string& sourcePath,
                      const std::string& destinationPath,
                      const std::optional<std::string>& newName = std::nullopt);

    /**
     * Move a file or folder to multiple destinations
     * Note: For multiple destinations, moves to first then copies to rest
     * @param sourcePath Source path in MEGA cloud
     * @param destinations List of destinations
     * @return Task ID for tracking
     */
    std::string moveToMultiple(const std::string& sourcePath,
                               const std::vector<CopyDestination>& destinations);

    // ===== Operation Mode =====

    /**
     * Set the operation mode (COPY or MOVE)
     * Affects task execution behavior
     * @param mode The operation mode to use
     */
    void setOperationMode(OperationMode mode) { m_operationMode = mode; }

    /**
     * Get current operation mode
     * @return Current operation mode
     */
    OperationMode getOperationMode() const { return m_operationMode; }

    // ===== Bulk Copy =====

    /**
     * Create a bulk copy task (multiple sources to multiple destinations)
     * @param tasks List of copy tasks
     * @return Task ID
     */
    std::string createBulkTask(const std::vector<CopyTask>& tasks);

    /**
     * Add sources to pending task
     * @param taskId Task ID
     * @param sourcePaths Source paths to add
     */
    void addSources(const std::string& taskId, const std::vector<std::string>& sourcePaths);

    /**
     * Add destinations to pending task
     * @param taskId Task ID
     * @param destinations Destinations to add
     */
    void addDestinations(const std::string& taskId, const std::vector<CopyDestination>& destinations);

    // ===== Task Control =====

    /**
     * Start copy task
     * @param taskId Task to start
     * @return true if started successfully
     */
    bool startTask(const std::string& taskId);

    /**
     * Pause copy task
     * @param taskId Task to pause
     * @return true if paused successfully
     */
    bool pauseTask(const std::string& taskId);

    /**
     * Resume paused task
     * @param taskId Task to resume
     * @return true if resumed successfully
     */
    bool resumeTask(const std::string& taskId);

    /**
     * Cancel copy task
     * @param taskId Task to cancel
     * @return true if cancelled successfully
     */
    bool cancelTask(const std::string& taskId);

    // ===== Task Status =====

    /**
     * Get task progress
     * @param taskId Task ID
     * @return Progress information or nullopt if not found
     */
    std::optional<CopyProgress> getTaskProgress(const std::string& taskId);

    /**
     * Get task report (after completion)
     * @param taskId Task ID
     * @return Copy report or nullopt if not complete
     */
    std::optional<CopyReport> getTaskReport(const std::string& taskId);

    /**
     * Get all active tasks
     * @return Vector of task IDs
     */
    std::vector<std::string> getActiveTasks() const;

    /**
     * Clear completed tasks
     * @param olderThanHours Clear tasks older than this (0 = all)
     */
    void clearCompletedTasks(int olderThanHours = 0);

    // ===== Conflict Handling =====

    /**
     * Check if item exists at destination
     * @param sourcePath Source path
     * @param destinationPath Destination folder path
     * @return true if conflict exists
     */
    bool checkConflict(const std::string& sourcePath, const std::string& destinationPath);

    /**
     * Get conflict information
     * @param sourcePath Source path
     * @param destinationPath Destination folder path
     * @return Conflict info or nullopt if no conflict
     */
    std::optional<CopyConflict> getConflictInfo(const std::string& sourcePath,
                                                const std::string& destinationPath);

    /**
     * Set conflict resolution callback
     * Called when a conflict is detected and resolution is ASK
     * @param callback Function that returns resolution for the conflict
     */
    void setConflictCallback(
        std::function<ConflictResolution(const CopyConflict&)> callback);

    /**
     * Set default conflict resolution
     * @param resolution Default resolution for all conflicts
     */
    void setDefaultConflictResolution(ConflictResolution resolution);

    // ===== Template Management =====

    /**
     * Save destinations as template
     * @param name Template name
     * @param destinations Destination paths
     * @return true if saved successfully
     */
    bool saveTemplate(const std::string& name, const std::vector<std::string>& destinations);

    /**
     * Load template destinations
     * @param name Template name
     * @return Destinations or empty if not found
     */
    std::vector<std::string> loadTemplate(const std::string& name);

    /**
     * Get all templates
     * @return Map of template names to templates
     */
    std::map<std::string, CopyTemplate> getTemplates() const;

    /**
     * Delete template
     * @param name Template name
     * @return true if deleted
     */
    bool deleteTemplate(const std::string& name);

    /**
     * Import destinations from file (one path per line)
     * @param filePath Path to text file
     * @return Vector of destination paths
     */
    std::vector<std::string> importDestinationsFromFile(const std::string& filePath);

    /**
     * Export destinations to file
     * @param destinations Destinations to export
     * @param filePath Output file path
     * @return true if exported
     */
    bool exportDestinationsToFile(const std::vector<std::string>& destinations,
                                  const std::string& filePath);

    // ===== Callbacks =====

    /**
     * Set progress callback
     * @param callback Function called with progress updates
     */
    void setProgressCallback(std::function<void(const CopyProgress&)> callback);

    /**
     * Set completion callback
     * @param callback Function called when task completes
     */
    void setCompletionCallback(std::function<void(const CopyReport&)> callback);

    /**
     * Set error callback
     * @param callback Function called on errors
     */
    void setErrorCallback(
        std::function<void(const std::string& taskId, const std::string& error)> callback);

    // ===== Utility =====

    /**
     * Package a file into a folder with the same name (minus extension)
     * Creates: /parent/filename (folder)/filename.ext (file)
     * @param sourceFilePath Path to the source file
     * @param destParentPath Destination parent folder (where the new folder will be created)
     * @return Path to the newly created folder, or empty string on failure
     */
    std::string packageFileIntoFolder(const std::string& sourceFilePath,
                                      const std::string& destParentPath);

    /**
     * Verify destinations exist
     * @param destinations Destinations to verify
     * @return Map of destination to existence status
     */
    std::map<std::string, bool> verifyDestinations(const std::vector<std::string>& destinations);

    /**
     * Create missing destinations
     * @param destinations Destinations to create
     * @return true if all created successfully
     */
    bool createDestinations(const std::vector<std::string>& destinations);

    /**
     * Get node by path
     * @param path Remote path
     * @return MegaNode or nullptr if not found
     */
    mega::MegaNode* getNodeByPath(const std::string& path);

    /**
     * Set/update the MegaApi instance
     * Used when switching accounts in multi-account mode
     * @param megaApi The new MegaApi instance to use
     */
    void setMegaApi(mega::MegaApi* megaApi) { m_megaApi = megaApi; }

    /**
     * Get the current MegaApi instance
     * @return Current MegaApi pointer
     */
    mega::MegaApi* getMegaApi() const { return m_megaApi; }

private:
    mega::MegaApi* m_megaApi;
    OperationMode m_operationMode = OperationMode::COPY;

    // Task management
    struct CopyTaskImpl;
    std::map<std::string, std::unique_ptr<CopyTaskImpl>> m_tasks;
    mutable std::mutex m_tasksMutex;

    // Templates
    std::map<std::string, CopyTemplate> m_templates;
    std::string m_templatesPath;

    // Callbacks
    std::function<ConflictResolution(const CopyConflict&)> m_conflictCallback;
    std::function<void(const CopyProgress&)> m_progressCallback;
    std::function<void(const CopyReport&)> m_completionCallback;
    std::function<void(const std::string&, const std::string&)> m_errorCallback;

    // Default settings
    ConflictResolution m_defaultResolution = ConflictResolution::ASK;

    // Helper methods
    std::string generateTaskId();
    mega::MegaNode* ensureFolderExists(const std::string& path);
    CopyResult performCopy(mega::MegaNode* sourceNode, mega::MegaNode* destParent,
                           const std::optional<std::string>& newName = std::nullopt);
    CopyResult performMove(mega::MegaNode* sourceNode, mega::MegaNode* destParent,
                           const std::optional<std::string>& newName = std::nullopt);
    ConflictResolution resolveConflict(const CopyConflict& conflict);
    std::string generateRenamedName(const std::string& originalName);
    void executeCopyTask(CopyTaskImpl* task);
    void loadTemplates();
    void saveTemplates();

    // Listener for copy operations
    class CopyListener;
    std::unique_ptr<CopyListener> m_listener;
};

} // namespace MegaCustom

#endif // CLOUD_COPIER_H
