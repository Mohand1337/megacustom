#ifndef FILE_OPERATIONS_H
#define FILE_OPERATIONS_H

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <queue>
#include <atomic>
#include <optional>
#include <map>
#include <chrono>
#include <thread>

namespace mega {
    class MegaApi;
    class MegaNode;
    class MegaTransfer;
    class MegaError;
}

namespace MegaCustom {

/**
 * Transfer progress information
 */
struct TransferProgress {
    std::string fileName;
    std::string transferId;
    long long bytesTransferred;
    long long totalBytes;
    double speed;  // bytes per second
    int progressPercentage;
    std::chrono::seconds estimatedTimeRemaining;
    bool isPaused;
};

/**
 * Transfer result information
 */
struct TransferResult {
    bool success;
    std::string fileName;
    std::string remotePath;
    long long fileSize;
    std::string errorMessage;
    int errorCode;
    std::chrono::milliseconds duration;
};

/**
 * Upload configuration
 */
struct UploadConfig {
    bool useChunking = true;
    size_t chunkSize = 10 * 1024 * 1024;  // 10MB default
    bool preserveTimestamp = true;
    bool detectDuplicates = true;
    int maxRetries = 3;
    int parallelUploads = 4;
    std::optional<std::string> customName;  // Rename on upload
    std::optional<std::string> description;
};

/**
 * Download configuration
 */
struct DownloadConfig {
    bool resumeIfExists = true;
    bool verifyChecksum = true;
    bool preserveTimestamp = true;
    int maxRetries = 3;
    int parallelDownloads = 4;
    std::optional<std::string> customName;  // Rename on download
};

/**
 * Handles all file transfer operations
 */
class FileOperations {
public:
    explicit FileOperations(mega::MegaApi* megaApi);
    ~FileOperations();

    /**
     * Upload a single file
     * @param localPath Path to local file
     * @param remotePath Destination path in Mega
     * @param config Upload configuration
     * @return Transfer result
     */
    TransferResult uploadFile(const std::string& localPath,
                             const std::string& remotePath,
                             const UploadConfig& config = {});

    /**
     * Upload multiple files
     * @param files Vector of {localPath, remotePath} pairs
     * @param config Upload configuration
     * @param progressCallback Called for each file progress
     * @return Vector of transfer results
     */
    std::vector<TransferResult> uploadFiles(
        const std::vector<std::pair<std::string, std::string>>& files,
        const UploadConfig& config = {},
        std::function<void(const TransferProgress&)> progressCallback = nullptr);

    /**
     * Upload entire directory
     * @param localDir Local directory path
     * @param remoteDir Remote directory path
     * @param recursive Include subdirectories
     * @param config Upload configuration
     * @return Vector of transfer results
     */
    std::vector<TransferResult> uploadDirectory(
        const std::string& localDir,
        const std::string& remoteDir,
        bool recursive = true,
        const UploadConfig& config = {});

    /**
     * Download a single file
     * @param remoteFile Remote file node
     * @param localPath Local destination path
     * @param config Download configuration
     * @return Transfer result
     */
    TransferResult downloadFile(mega::MegaNode* remoteFile,
                               const std::string& localPath,
                               const DownloadConfig& config = {});

    /**
     * Download multiple files
     * @param files Vector of {node, localPath} pairs
     * @param config Download configuration
     * @param progressCallback Called for each file progress
     * @return Vector of transfer results
     */
    std::vector<TransferResult> downloadFiles(
        const std::vector<std::pair<mega::MegaNode*, std::string>>& files,
        const DownloadConfig& config = {},
        std::function<void(const TransferProgress&)> progressCallback = nullptr);

    /**
     * Download entire directory
     * @param remoteDir Remote directory node
     * @param localDir Local destination directory
     * @param config Download configuration
     * @return Vector of transfer results
     */
    std::vector<TransferResult> downloadDirectory(
        mega::MegaNode* remoteDir,
        const std::string& localDir,
        const DownloadConfig& config = {});

    /**
     * Queue file for upload
     * @param localPath Local file path
     * @param remotePath Remote destination
     * @param priority Priority (higher = sooner)
     * @return Queue ID
     */
    std::string queueUpload(const std::string& localPath,
                           const std::string& remotePath,
                           int priority = 0);

    /**
     * Queue file for download
     * @param remoteNode Remote file node
     * @param localPath Local destination
     * @param priority Priority (higher = sooner)
     * @return Queue ID
     */
    std::string queueDownload(mega::MegaNode* remoteNode,
                             const std::string& localPath,
                             int priority = 0);

    /**
     * Start processing queued transfers
     * @param maxConcurrent Maximum concurrent transfers
     */
    void startQueueProcessing(int maxConcurrent = 4);

    /**
     * Stop queue processing
     * @param cancelPending Cancel pending transfers
     */
    void stopQueueProcessing(bool cancelPending = false);

    /**
     * Pause a transfer
     * @param transferId Transfer ID
     * @return true if paused successfully
     */
    bool pauseTransfer(const std::string& transferId);

    /**
     * Resume a paused transfer
     * @param transferId Transfer ID
     * @return true if resumed successfully
     */
    bool resumeTransfer(const std::string& transferId);

    /**
     * Cancel a transfer
     * @param transferId Transfer ID
     * @return true if cancelled successfully
     */
    bool cancelTransfer(const std::string& transferId);

    /**
     * Cancel all active transfers
     */
    void cancelAllTransfers();

    /**
     * Get current transfer progress
     * @param transferId Transfer ID
     * @return Transfer progress or nullopt if not found
     */
    std::optional<TransferProgress> getTransferProgress(const std::string& transferId);

    /**
     * Get all active transfers
     * @return Vector of transfer progress
     */
    std::vector<TransferProgress> getAllActiveTransfers();

    /**
     * Set bandwidth limits
     * @param uploadBps Upload limit in bytes/sec (0 = unlimited)
     * @param downloadBps Download limit in bytes/sec (0 = unlimited)
     */
    void setBandwidthLimits(int uploadBps, int downloadBps);

    /**
     * Get transfer statistics
     * @return JSON string with statistics
     */
    std::string getTransferStatistics() const;

    /**
     * Check if file exists in remote path
     * @param remotePath Path to check
     * @return true if file exists
     */
    bool remoteFileExists(const std::string& remotePath);

    /**
     * Calculate file checksum
     * @param filePath Path to file
     * @return Checksum string
     */
    static std::string calculateChecksum(const std::string& filePath);

    /**
     * Compare local and remote file
     * @param localPath Local file path
     * @param remoteNode Remote file node
     * @return true if files are identical
     */
    bool compareFiles(const std::string& localPath, mega::MegaNode* remoteNode);

    /**
     * Set global progress callback
     * @param callback Function to call with progress updates
     */
    void setProgressCallback(std::function<void(const TransferProgress&)> callback);

    /**
     * Set global completion callback
     * @param callback Function to call when transfer completes
     */
    void setCompletionCallback(std::function<void(const TransferResult&)> callback);

private:
    mega::MegaApi* m_megaApi;

    // Transfer management
    struct TransferInfo;
    std::map<std::string, std::unique_ptr<TransferInfo>> m_activeTransfers;
    std::priority_queue<std::unique_ptr<TransferInfo>> m_uploadQueue;
    std::priority_queue<std::unique_ptr<TransferInfo>> m_downloadQueue;

    // State management
    std::atomic<bool> m_queueProcessing;
    std::atomic<int> m_activeUploads;
    std::atomic<int> m_activeDownloads;
    int m_maxConcurrentTransfers;

    // Queue processing threads - stored for proper cleanup
    std::thread m_uploadQueueThread;
    std::thread m_downloadQueueThread;

    // Callbacks
    std::function<void(const TransferProgress&)> m_progressCallback;
    std::function<void(const TransferResult&)> m_completionCallback;

    // Statistics
    struct TransferStats {
        long long totalBytesUploaded;
        long long totalBytesDownloaded;
        int successfulUploads;
        int failedUploads;
        int successfulDownloads;
        int failedDownloads;
        std::chrono::steady_clock::time_point startTime;
    } m_stats;

    // Helper methods
    mega::MegaNode* getOrCreateRemoteFolder(const std::string& path);
    std::vector<std::string> listLocalFiles(const std::string& directory, bool recursive);
    void processUploadQueue();
    void processDownloadQueue();
    TransferResult handleTransferCompletion(mega::MegaTransfer* transfer, mega::MegaError* error);
    std::string generateTransferId();

    // Listener for transfer events
    class TransferListener;
    std::unique_ptr<TransferListener> m_listener;
};

} // namespace MegaCustom

#endif // FILE_OPERATIONS_H