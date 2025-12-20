#ifndef DISTRIBUTIONPIPELINE_H
#define DISTRIBUTIONPIPELINE_H

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <atomic>
#include <mutex>
#include <cstdint>

namespace MegaCustom {

// Forward declarations
class Watermarker;
class MemberDatabase;

/**
 * Configuration for a distribution job
 */
struct DistributionConfig {
    // Watermark mode
    enum class WatermarkMode {
        None,           // Upload files as-is (no watermarking)
        Global,         // Same watermark for all (brand only)
        PerMember       // Personalized watermark per member
    };

    WatermarkMode watermarkMode = WatermarkMode::PerMember;

    // Global watermark text (when mode is Global)
    std::string globalPrimaryText;
    std::string globalSecondaryText;

    // Temp file handling
    std::string tempDirectory;      // Where to store watermarked files temporarily
    bool deleteTempAfterUpload = true;
    bool keepLocalCopies = false;
    std::string localCopiesDir;     // If keeping local copies

    // Processing options
    int parallelWatermarkJobs = 2;  // Parallel FFmpeg/Python processes
    int parallelUploadJobs = 4;     // Parallel MEGA uploads
    bool resumeOnError = true;      // Continue with other members if one fails

    // Upload options
    bool createFolderIfMissing = true;  // Auto-create member folder if not exists
    bool overwriteExisting = false;     // Overwrite files with same name in destination
};

/**
 * Status of a single member's distribution
 */
struct MemberDistributionStatus {
    std::string memberId;
    std::string memberName;
    std::string destinationFolder;

    enum class State {
        Pending,
        Watermarking,
        Uploading,
        Completed,
        Failed,
        Skipped       // Member has no folder binding
    };

    State state = State::Pending;

    // Per-file status for this member
    struct FileStatus {
        std::string sourceFile;
        std::string watermarkedFile;  // Temp file path
        std::string uploadedPath;     // Final MEGA path
        bool watermarkDone = false;
        bool uploadDone = false;
        std::string error;
    };
    std::vector<FileStatus> files;

    // Aggregate progress
    int filesWatermarked = 0;
    int filesUploaded = 0;
    int filesFailed = 0;
    std::string lastError;
};

/**
 * Overall distribution job result
 */
struct DistributionResult {
    bool success = false;           // True if all succeeded
    std::string jobId;              // Unique job identifier
    int64_t startTime = 0;          // Unix timestamp
    int64_t endTime = 0;

    // Source files
    std::vector<std::string> sourceFiles;

    // Per-member results
    std::vector<MemberDistributionStatus> memberResults;

    // Summary counts
    int totalMembers = 0;
    int membersCompleted = 0;
    int membersFailed = 0;
    int membersSkipped = 0;

    int totalFiles = 0;             // sourceFiles.size() * members
    int filesWatermarked = 0;
    int filesUploaded = 0;
    int filesFailed = 0;

    // Temp files created (for cleanup tracking)
    std::vector<std::string> tempFilesCreated;

    // Errors
    std::vector<std::string> errors;
};

/**
 * Progress callback for distribution operations
 */
struct DistributionProgress {
    std::string jobId;

    // Overall progress
    double overallPercent = 0.0;
    std::string phase;              // "watermarking", "uploading", "cleanup", "complete"

    // Current operation
    std::string currentMember;
    std::string currentFile;
    std::string currentOperation;   // Detailed description

    // Counts
    int membersProcessed = 0;
    int totalMembers = 0;
    int filesProcessed = 0;
    int totalFiles = 0;

    // Timing
    int64_t elapsedMs = 0;
    int64_t estimatedRemainingMs = 0;

    // Errors encountered so far
    int errorCount = 0;
};

using DistributionProgressCallback = std::function<void(const DistributionProgress&)>;

/**
 * DistributionPipeline - Orchestrates the complete distribution workflow
 *
 * Workflow:
 * 1. Select source files (videos/PDFs)
 * 2. Select target members (with MEGA folder bindings)
 * 3. For each member:
 *    a. Watermark each file with member-specific info
 *    b. Upload watermarked file to member's MEGA folder
 *    c. Clean up temp watermarked file
 * 4. Report results
 */
class DistributionPipeline {
public:
    DistributionPipeline();
    ~DistributionPipeline();

    // ==================== Configuration ====================

    /**
     * Set distribution configuration
     */
    void setConfig(const DistributionConfig& config) { m_config = config; }
    DistributionConfig getConfig() const { return m_config; }

    /**
     * Set progress callback
     */
    void setProgressCallback(DistributionProgressCallback callback) {
        m_progressCallback = callback;
    }

    /**
     * Set path to member database file
     * Default: ~/.megacustom/members.json
     */
    void setMemberDatabasePath(const std::string& path) { m_memberDbPath = path; }

    // ==================== Distribution Operations ====================

    /**
     * Distribute files to selected members
     * @param sourceFiles List of source file paths (videos/PDFs)
     * @param memberIds List of member IDs to distribute to (empty = all with folders)
     * @return Distribution result with per-member status
     */
    DistributionResult distribute(
        const std::vector<std::string>& sourceFiles,
        const std::vector<std::string>& memberIds = {});

    /**
     * Distribute files to a single member
     * @param sourceFiles List of source file paths
     * @param memberId Target member ID
     * @return Distribution result
     */
    DistributionResult distributeToMember(
        const std::vector<std::string>& sourceFiles,
        const std::string& memberId);

    /**
     * Preview distribution without executing
     * Shows which files would go where
     * @param sourceFiles List of source file paths
     * @param memberIds List of member IDs (empty = all with folders)
     * @return Preview result with planned operations
     */
    DistributionResult previewDistribution(
        const std::vector<std::string>& sourceFiles,
        const std::vector<std::string>& memberIds = {});

    /**
     * Retry failed distributions from a previous job
     * @param previousResult Result from a failed distribution
     * @return New result with retry attempts
     */
    DistributionResult retryFailed(const DistributionResult& previousResult);

    // ==================== Control ====================

    /**
     * Cancel ongoing distribution
     */
    void cancel() { m_cancelled = true; }
    bool isCancelled() const { return m_cancelled; }

    /**
     * Pause distribution (between files)
     */
    void pause() { m_paused = true; }
    void resume() { m_paused = false; }
    bool isPaused() const { return m_paused; }

    // ==================== Utilities ====================

    /**
     * Get members with distribution folders bound
     * @return List of member IDs
     */
    std::vector<std::string> getMembersWithFolders();

    /**
     * Validate source files exist and are supported types
     * @param sourceFiles List of file paths
     * @return Map of file path to error (empty string if valid)
     */
    std::map<std::string, std::string> validateSourceFiles(
        const std::vector<std::string>& sourceFiles);

    /**
     * Clean up temp files from a distribution job
     * @param result Distribution result containing temp file list
     * @return Number of files deleted
     */
    int cleanupTempFiles(const DistributionResult& result);

    /**
     * Generate unique job ID
     */
    static std::string generateJobId();

    /**
     * Get default temp directory
     */
    static std::string getDefaultTempDirectory();

private:
    DistributionConfig m_config;
    DistributionProgressCallback m_progressCallback;
    std::string m_memberDbPath;

    std::atomic<bool> m_cancelled{false};
    std::atomic<bool> m_paused{false};

    // Progress tracking
    std::mutex m_progressMutex;
    DistributionProgress m_currentProgress;
    int64_t m_startTime = 0;

    // Internal methods

    /**
     * Load members from database
     */
    bool loadMembers(std::vector<std::string>& memberIds,
                     std::map<std::string, std::string>& memberNames,
                     std::map<std::string, std::string>& memberFolders);

    /**
     * Watermark a single file for a single member
     */
    bool watermarkForMember(const std::string& sourceFile,
                            const std::string& memberId,
                            std::string& outputPath,
                            std::string& error);

    /**
     * Upload a file to MEGA folder
     */
    bool uploadToMegaFolder(const std::string& localPath,
                            const std::string& megaFolder,
                            std::string& error);

    /**
     * Process a single member's distribution (all files)
     */
    MemberDistributionStatus processOneMember(
        const std::string& memberId,
        const std::string& memberName,
        const std::string& memberFolder,
        const std::vector<std::string>& sourceFiles);

    /**
     * Update and report progress
     */
    void reportProgress(const std::string& phase,
                        const std::string& member,
                        const std::string& file,
                        const std::string& operation,
                        int membersProcessed,
                        int totalMembers,
                        int filesProcessed,
                        int totalFiles);

    /**
     * Calculate overall progress percentage
     */
    double calculateOverallProgress(int membersProcessed, int totalMembers,
                                     int filesProcessed, int totalFiles);

    /**
     * Wait if paused, check if cancelled
     * @return false if cancelled
     */
    bool checkPauseCancel();

    /**
     * Get current timestamp in milliseconds
     */
    static int64_t currentTimeMs();
};

} // namespace MegaCustom

#endif // DISTRIBUTIONPIPELINE_H
