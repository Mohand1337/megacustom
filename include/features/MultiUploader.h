#ifndef MULTI_UPLOADER_H
#define MULTI_UPLOADER_H

#include <string>
#include <vector>
#include <queue>
#include <map>
#include <functional>
#include <memory>
#include <atomic>
#include <regex>
#include <optional>
#include <chrono>
#include <thread>

namespace mega {
    class MegaApi;
    class MegaNode;
    class MegaTransfer;
}

namespace MegaCustom {

/**
 * Upload destination configuration
 */
struct UploadDestination {
    std::string remotePath;           // Target folder path
    std::optional<std::string> namePattern;  // Optional rename pattern
    bool createIfMissing = true;      // Create folder if doesn't exist
    std::vector<std::string> tags;    // Tags for organization
    int priority = 0;                  // Upload priority for this destination
};

/**
 * File distribution rule
 */
struct DistributionRule {
    enum RuleType {
        BY_EXTENSION,    // Distribute by file extension
        BY_SIZE,         // Distribute by file size
        BY_DATE,         // Distribute by modification date
        BY_REGEX,        // Distribute by regex pattern
        BY_METADATA,     // Distribute by file metadata
        ROUND_ROBIN,     // Distribute evenly across destinations
        RANDOM,          // Random distribution
        CUSTOM           // Custom function-based distribution
    } type;

    // Rule-specific parameters
    std::vector<std::string> extensions;     // For BY_EXTENSION
    long long sizeThreshold = 0;             // For BY_SIZE (bytes)
    std::chrono::system_clock::time_point dateThreshold;  // For BY_DATE
    std::string regexPattern;                // For BY_REGEX
    std::string metadataKey;                 // For BY_METADATA
    std::string metadataValue;               // For BY_METADATA

    // Custom distribution function
    std::function<int(const std::string& filePath)> customSelector;

    // Target destination index
    int destinationIndex = 0;
};

/**
 * Bulk upload task
 */
struct BulkUploadTask {
    std::string taskId;
    std::string localPath;                   // File or directory path
    std::vector<UploadDestination> destinations;  // Multiple destinations
    std::vector<DistributionRule> rules;     // Distribution rules
    bool recursive = true;                   // For directory uploads
    bool skipDuplicates = true;              // Skip if file exists
    bool deleteAfterUpload = false;          // Delete local file after success
    int maxRetries = 3;
    int priority = 0;                        // Task priority
};

/**
 * Upload progress information
 */
struct BulkUploadProgress {
    std::string taskId;
    int totalFiles;
    int completedFiles;
    int failedFiles;
    int skippedFiles;
    long long totalBytes;
    long long uploadedBytes;
    double averageSpeed;  // bytes per second
    std::chrono::seconds estimatedTimeRemaining;
    std::string currentFile;
    std::string currentDestination;
    double overallProgress;  // 0.0 to 100.0
};

/**
 * Upload result for a single file
 */
struct FileUploadResult {
    std::string fileName;
    std::string localPath;
    std::string destination;
    bool success;
    bool skipped;
    std::string errorMessage;
    long long fileSize;
    std::chrono::milliseconds uploadTime;
};

/**
 * Bulk upload report
 */
struct BulkUploadReport {
    std::string taskId;
    std::chrono::system_clock::time_point startTime;
    std::chrono::system_clock::time_point endTime;
    std::vector<FileUploadResult> results;
    int totalFiles;
    int successfulUploads;
    int failedUploads;
    int skippedFiles;
    long long totalBytesUploaded;
    std::map<std::string, int> destinationCounts;  // Files per destination
};

/**
 * Advanced multi-destination bulk uploader
 */
class MultiUploader {
public:
    explicit MultiUploader(mega::MegaApi* megaApi);
    ~MultiUploader();

    /**
     * Create a bulk upload task
     * @param task Upload task configuration
     * @return Task ID
     */
    std::string createUploadTask(const BulkUploadTask& task);

    /**
     * Upload files to multiple destinations
     * @param files List of file paths
     * @param destinations List of destinations
     * @param rules Distribution rules
     * @return Task ID
     */
    std::string uploadToMultipleDestinations(
        const std::vector<std::string>& files,
        const std::vector<UploadDestination>& destinations,
        const std::vector<DistributionRule>& rules);

    /**
     * Upload directory to multiple destinations
     * @param directoryPath Local directory path
     * @param destinations Target destinations
     * @param rules Distribution rules
     * @param recursive Include subdirectories
     * @return Task ID
     */
    std::string uploadDirectoryToMultiple(
        const std::string& directoryPath,
        const std::vector<UploadDestination>& destinations,
        const std::vector<DistributionRule>& rules,
        bool recursive = true);

    /**
     * Start upload task
     * @param taskId Task to start
     * @param maxConcurrent Maximum concurrent uploads
     * @return true if started successfully
     */
    bool startTask(const std::string& taskId, int maxConcurrent = 4);

    /**
     * Pause upload task
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
     * Cancel upload task
     * @param taskId Task to cancel
     * @param deletePartial Delete partially uploaded files
     * @return true if cancelled successfully
     */
    bool cancelTask(const std::string& taskId, bool deletePartial = false);

    /**
     * Get task progress
     * @param taskId Task ID
     * @return Progress information or nullopt if not found
     */
    std::optional<BulkUploadProgress> getTaskProgress(const std::string& taskId);

    /**
     * Get all active tasks
     * @return Vector of task IDs
     */
    std::vector<std::string> getActiveTasks() const;

    /**
     * Get task report
     * @param taskId Task ID
     * @return Upload report or nullopt if not complete
     */
    std::optional<BulkUploadReport> getTaskReport(const std::string& taskId);

    /**
     * Schedule upload task
     * @param task Upload task
     * @param scheduleTime Time to start
     * @return Task ID
     */
    std::string scheduleTask(
        const BulkUploadTask& task,
        std::chrono::system_clock::time_point scheduleTime);

    /**
     * Add distribution rule
     * @param name Rule name
     * @param rule Distribution rule
     */
    void addDistributionRule(const std::string& name, const DistributionRule& rule);

    /**
     * Get predefined distribution rules
     * @return Map of rule names to rules
     */
    std::map<std::string, DistributionRule> getPredefinedRules() const;

    /**
     * Analyze files for optimal distribution
     * @param files List of file paths
     * @param destinations Available destinations
     * @return Suggested distribution map
     */
    std::map<std::string, int> analyzeDistribution(
        const std::vector<std::string>& files,
        const std::vector<UploadDestination>& destinations);

    /**
     * Verify destinations exist
     * @param destinations List of destinations to verify
     * @return Map of destination to existence status
     */
    std::map<std::string, bool> verifyDestinations(
        const std::vector<UploadDestination>& destinations);

    /**
     * Create missing destinations
     * @param destinations Destinations to create
     * @return true if all created successfully
     */
    bool createDestinations(const std::vector<UploadDestination>& destinations);

    /**
     * Check for duplicates
     * @param files Local files to check
     * @param destination Target destination
     * @return Map of file to duplicate status
     */
    std::map<std::string, bool> checkDuplicates(
        const std::vector<std::string>& files,
        const std::string& destination);

    /**
     * Set bandwidth limit for uploads
     * @param bytesPerSecond Limit in bytes/sec (0 = unlimited)
     */
    void setBandwidthLimit(int bytesPerSecond);

    /**
     * Set file filter
     * @param filter Function to filter files
     */
    void setFileFilter(std::function<bool(const std::string&)> filter);

    /**
     * Set progress callback
     * @param callback Function called with progress updates
     */
    void setProgressCallback(
        std::function<void(const BulkUploadProgress&)> callback);

    /**
     * Set completion callback
     * @param callback Function called when task completes
     */
    void setCompletionCallback(
        std::function<void(const BulkUploadReport&)> callback);

    /**
     * Set error callback
     * @param callback Function called on errors
     */
    void setErrorCallback(
        std::function<void(const std::string& taskId, const std::string& error)> callback);

    /**
     * Export task configuration
     * @param taskId Task to export
     * @param filePath Export file path
     * @return true if exported successfully
     */
    bool exportTaskConfig(const std::string& taskId, const std::string& filePath);

    /**
     * Import task configuration
     * @param filePath Import file path
     * @return Task ID or empty if failed
     */
    std::string importTaskConfig(const std::string& filePath);

    /**
     * Get upload statistics
     * @return JSON string with statistics
     */
    std::string getStatistics() const;

    /**
     * Clear completed tasks
     * @param olderThan Clear tasks older than this duration (in hours)
     */
    void clearCompletedTasks(int olderThanHours = 24);

private:
    mega::MegaApi* m_megaApi;

    // Task management
    struct UploadTaskImpl;
    std::map<std::string, std::unique_ptr<UploadTaskImpl>> m_tasks;
    std::queue<std::string> m_scheduledTasks;

    // Predefined rules
    std::map<std::string, DistributionRule> m_predefinedRules;

    // State management
    std::atomic<int> m_activeTasks;
    std::atomic<int> m_maxConcurrentUploads;
    std::atomic<int> m_bandwidthLimit;

    // Scheduler thread for scheduled tasks
    std::thread m_schedulerThread;
    std::atomic<bool> m_schedulerRunning{false};

    // Callbacks
    std::function<bool(const std::string&)> m_fileFilter;
    std::function<void(const BulkUploadProgress&)> m_progressCallback;
    std::function<void(const BulkUploadReport&)> m_completionCallback;
    std::function<void(const std::string&, const std::string&)> m_errorCallback;

    // Statistics
    struct UploadStats {
        long long totalBytesUploaded;
        int totalFilesUploaded;
        int totalTasks;
        int successfulTasks;
        int failedTasks;
        std::chrono::steady_clock::time_point startTime;
    } m_stats;

    // Helper methods
    int selectDestination(const std::string& filePath,
                         const std::vector<DistributionRule>& rules);
    std::vector<std::string> collectFiles(const std::string& path, bool recursive);
    mega::MegaNode* ensureDestinationExists(const UploadDestination& destination);
    bool isDuplicate(const std::string& localFile, mega::MegaNode* remoteFolder);
    std::string generateTaskId();
    void processScheduledTasks();
    void executeUploadTask(UploadTaskImpl* task);
    void handleUploadCompletion(const std::string& taskId,
                               const FileUploadResult& result);
    void initializePredefinedRules();
    long long calculateTotalSize(const std::vector<std::string>& files);
    std::string applyNamePattern(const std::string& fileName,
                                const std::string& pattern);

    // Upload task implementation
    class UploadTaskImpl;
    class UploadListener;
    std::unique_ptr<UploadListener> m_listener;
};

} // namespace MegaCustom

#endif // MULTI_UPLOADER_H