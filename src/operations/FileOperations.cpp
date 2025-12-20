#include "operations/FileOperations.h"
#include "megaapi.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <sstream>
#include <iomanip>
#include <openssl/sha.h>
#include <random>
#include <map>

namespace fs = std::filesystem;

namespace MegaCustom {

// Transfer information structure
struct FileOperations::TransferInfo {
    std::string transferId;
    std::string localPath;
    std::string remotePath;
    mega::MegaNode* node;
    int priority;
    bool isUpload;
    TransferProgress progress;
    std::chrono::steady_clock::time_point startTime;
};

// Internal transfer listener
class FileOperations::TransferListener : public mega::MegaTransferListener {
public:
    TransferListener(FileOperations* ops) : m_ops(ops), m_completed(false) {}

    void onTransferStart(mega::MegaApi* api, mega::MegaTransfer* transfer) override {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Initialize transfer progress
        m_currentTransfer = transfer->getTag();
        m_progress.fileName = transfer->getFileName() ? transfer->getFileName() : "";
        m_progress.totalBytes = transfer->getTotalBytes();
        m_progress.bytesTransferred = 0;
        m_progress.progressPercentage = 0;
        m_progress.isPaused = false;

        std::cout << "Transfer started: " << m_progress.fileName << std::endl;
    }

    void onTransferUpdate(mega::MegaApi* api, mega::MegaTransfer* transfer) override {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Update progress
        m_progress.bytesTransferred = transfer->getTransferredBytes();
        m_progress.speed = transfer->getSpeed();

        if (m_progress.totalBytes > 0) {
            m_progress.progressPercentage = static_cast<int>(
                (m_progress.bytesTransferred * 100) / m_progress.totalBytes);
        }

        // Calculate ETA
        if (m_progress.speed > 0) {
            long long remaining = m_progress.totalBytes - m_progress.bytesTransferred;
            m_progress.estimatedTimeRemaining = std::chrono::seconds(remaining / static_cast<long long>(m_progress.speed));
        }

        // Call progress callback if set
        if (m_ops->m_progressCallback) {
            m_ops->m_progressCallback(m_progress);
        }
    }

    void onTransferFinish(mega::MegaApi* api, mega::MegaTransfer* transfer, mega::MegaError* error) override {
        std::lock_guard<std::mutex> lock(m_mutex);

        m_lastError = error->getErrorCode();
        m_errorString = error->getErrorString() ? error->getErrorString() : "";
        m_transferType = transfer->getType();

        // Create result
        m_result.success = (m_lastError == mega::MegaError::API_OK);
        m_result.fileName = transfer->getFileName() ? transfer->getFileName() : "";
        m_result.fileSize = transfer->getTotalBytes();
        m_result.errorCode = m_lastError;
        m_result.errorMessage = m_errorString;

        auto endTime = std::chrono::steady_clock::now();
        auto duration = endTime - m_startTime;
        m_result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(duration);

        // Update statistics
        if (m_result.success) {
            if (m_transferType == mega::MegaTransfer::TYPE_UPLOAD) {
                m_ops->m_stats.successfulUploads++;
                m_ops->m_stats.totalBytesUploaded += m_result.fileSize;
            } else {
                m_ops->m_stats.successfulDownloads++;
                m_ops->m_stats.totalBytesDownloaded += m_result.fileSize;
            }
        } else {
            if (m_transferType == mega::MegaTransfer::TYPE_UPLOAD) {
                m_ops->m_stats.failedUploads++;
            } else {
                m_ops->m_stats.failedDownloads++;
            }
        }

        // Call completion callback if set
        if (m_ops->m_completionCallback) {
            m_ops->m_completionCallback(m_result);
        }

        m_completed = true;
        m_cv.notify_all();
    }

    void onTransferTemporaryError(mega::MegaApi* api, mega::MegaTransfer* transfer, mega::MegaError* error) override {
        std::cerr << "Transfer temporary error: " << error->getErrorString() << std::endl;
        // The SDK will retry automatically
    }

    bool waitForCompletion(int timeoutSeconds = 300) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, std::chrono::seconds(timeoutSeconds),
                            [this] { return m_completed; });
    }

    void reset() {
        m_completed = false;
        m_lastError = mega::MegaError::API_OK;
        m_errorString.clear();
        m_progress = {};
        m_result = {};
        m_startTime = std::chrono::steady_clock::now();
    }

    TransferProgress getProgress() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_progress;
    }

    TransferResult getResult() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_result;
    }

private:
    FileOperations* m_ops;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_completed;

    int m_lastError = mega::MegaError::API_OK;
    std::string m_errorString;
    int m_transferType;
    int m_currentTransfer;

    TransferProgress m_progress;
    TransferResult m_result;
    std::chrono::steady_clock::time_point m_startTime;
};

// Constructor
FileOperations::FileOperations(mega::MegaApi* megaApi)
    : m_megaApi(megaApi), m_queueProcessing(false),
      m_activeUploads(0), m_activeDownloads(0), m_maxConcurrentTransfers(4) {

    if (!m_megaApi) {
        throw std::runtime_error("MegaApi instance is null");
    }

    m_listener = std::make_unique<TransferListener>(this);

    // Initialize statistics
    m_stats = {};
    m_stats.startTime = std::chrono::steady_clock::now();
}

// Destructor
FileOperations::~FileOperations() {
    cancelAllTransfers();
    stopQueueProcessing(true);
}

// Upload a single file
TransferResult FileOperations::uploadFile(const std::string& localPath,
                                         const std::string& remotePath,
                                         const UploadConfig& config) {
    // Check if file exists
    if (!fs::exists(localPath)) {
        TransferResult result;
        result.success = false;
        result.fileName = localPath;
        result.errorMessage = "File does not exist";
        result.errorCode = mega::MegaError::API_ENOENT;
        return result;
    }

    // Get or create remote folder
    mega::MegaNode* parentNode = getOrCreateRemoteFolder(remotePath);
    if (!parentNode) {
        TransferResult result;
        result.success = false;
        result.fileName = localPath;
        result.remotePath = remotePath;
        result.errorMessage = "Failed to access remote folder";
        result.errorCode = mega::MegaError::API_EACCESS;
        return result;
    }

    // Check for duplicates if enabled
    if (config.detectDuplicates) {
        // Get file checksum
        std::string checksum = calculateChecksum(localPath);

        // Check if file with same checksum exists
        mega::MegaNodeList* children = m_megaApi->getChildren(parentNode);
        if (children) {
            for (int i = 0; i < children->size(); i++) {
                mega::MegaNode* child = children->get(i);
                if (child && !child->isFolder()) {
                    // Compare checksums (simplified - real implementation would use fingerprint)
                    fs::path localFile(localPath);
                    if (std::string(child->getName()) == localFile.filename().string()) {
                        delete children;
                        delete parentNode;

                        TransferResult result;
                        result.success = false;
                        result.fileName = localPath;
                        result.remotePath = remotePath;
                        result.errorMessage = "Duplicate file detected";
                        result.errorCode = mega::MegaError::API_EEXIST;
                        return result;
                    }
                }
            }
            delete children;
        }
    }

    // Reset listener
    m_listener->reset();

    // Determine final filename
    std::string uploadName = config.customName.value_or(fs::path(localPath).filename().string());

    // Start upload
    // Parameters: localPath, parent, fileName, mtime, appData, isSourceTemp, startFirst, cancelToken, listener
    m_megaApi->startUpload(localPath.c_str(), parentNode, uploadName.c_str(),
                          0, nullptr, false, false, nullptr, m_listener.get());

    // Wait for completion
    if (!m_listener->waitForCompletion()) {
        delete parentNode;

        TransferResult result;
        result.success = false;
        result.fileName = localPath;
        result.remotePath = remotePath;
        result.errorMessage = "Upload timeout";
        result.errorCode = mega::MegaError::API_EAGAIN;
        return result;
    }

    // Get result
    TransferResult result = m_listener->getResult();
    result.remotePath = remotePath;

    delete parentNode;
    return result;
}

// Upload multiple files
std::vector<TransferResult> FileOperations::uploadFiles(
    const std::vector<std::pair<std::string, std::string>>& files,
    const UploadConfig& config,
    std::function<void(const TransferProgress&)> progressCallback) {

    std::vector<TransferResult> results;

    // Store original callback
    auto originalCallback = m_progressCallback;
    if (progressCallback) {
        m_progressCallback = progressCallback;
    }

    // Upload each file
    for (const auto& [localPath, remotePath] : files) {
        auto result = uploadFile(localPath, remotePath, config);
        results.push_back(result);

        // Respect parallel upload limit
        if (config.parallelUploads > 0) {
            while (m_activeUploads >= config.parallelUploads) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    // Restore original callback
    m_progressCallback = originalCallback;

    return results;
}

// Upload entire directory
std::vector<TransferResult> FileOperations::uploadDirectory(
    const std::string& localDir,
    const std::string& remoteDir,
    bool recursive,
    const UploadConfig& config) {

    std::vector<TransferResult> results;

    if (!fs::exists(localDir) || !fs::is_directory(localDir)) {
        TransferResult result;
        result.success = false;
        result.fileName = localDir;
        result.errorMessage = "Directory does not exist";
        result.errorCode = mega::MegaError::API_ENOENT;
        results.push_back(result);
        return results;
    }

    // Get list of files
    auto files = listLocalFiles(localDir, recursive);

    // Prepare upload pairs
    std::vector<std::pair<std::string, std::string>> uploadPairs;
    for (const auto& file : files) {
        fs::path relative = fs::relative(file, localDir);
        std::string remotePath = remoteDir + "/" + relative.string();
        uploadPairs.push_back({file, remotePath});
    }

    // Upload all files
    return uploadFiles(uploadPairs, config);
}

// Download a single file
TransferResult FileOperations::downloadFile(mega::MegaNode* remoteFile,
                                           const std::string& localPath,
                                           const DownloadConfig& config) {
    if (!remoteFile || remoteFile->isFolder()) {
        TransferResult result;
        result.success = false;
        result.errorMessage = "Invalid remote file";
        result.errorCode = mega::MegaError::API_EARGS;
        return result;
    }

    // Check if local file exists and resume is disabled
    if (fs::exists(localPath) && !config.resumeIfExists) {
        TransferResult result;
        result.success = false;
        result.fileName = localPath;
        result.errorMessage = "Local file already exists";
        result.errorCode = mega::MegaError::API_EEXIST;
        return result;
    }

    // Reset listener
    m_listener->reset();

    // Determine final filename
    std::string downloadPath = localPath;
    if (config.customName.has_value()) {
        fs::path dir = fs::path(localPath).parent_path();
        downloadPath = (dir / config.customName.value()).string();
    }

    // Start download
    // Parameters: node, localPath, customName, appData, startFirst, cancelToken, collisionCheck, collisionResolution, undelete, listener
    m_megaApi->startDownload(remoteFile, downloadPath.c_str(), nullptr, nullptr,
                            false, nullptr, 0, 0, false, m_listener.get());

    // Wait for completion
    if (!m_listener->waitForCompletion()) {
        TransferResult result;
        result.success = false;
        result.fileName = downloadPath;
        result.errorMessage = "Download timeout";
        result.errorCode = mega::MegaError::API_EAGAIN;
        return result;
    }

    // Get result
    TransferResult result = m_listener->getResult();

    // Verify checksum if enabled
    if (result.success && config.verifyChecksum) {
        if (!compareFiles(downloadPath, remoteFile)) {
            result.success = false;
            result.errorMessage = "Checksum verification failed";
            result.errorCode = mega::MegaError::API_EINCOMPLETE;
        }
    }

    return result;
}

// Calculate file checksum
std::string FileOperations::calculateChecksum(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        return "";
    }

    SHA256_CTX sha256;
    SHA256_Init(&sha256);

    char buffer[8192];
    while (file.good()) {
        file.read(buffer, sizeof(buffer));
        SHA256_Update(&sha256, buffer, file.gcount());
    }

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &sha256);

    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }

    return ss.str();
}

// Compare local and remote file
bool FileOperations::compareFiles(const std::string& localPath, mega::MegaNode* remoteNode) {
    if (!remoteNode || !fs::exists(localPath)) {
        return false;
    }

    // Compare sizes first
    auto localSize = fs::file_size(localPath);
    if (localSize != static_cast<uintmax_t>(remoteNode->getSize())) {
        return false;
    }

    // For more thorough comparison, would compare fingerprints
    // This is a simplified version
    return true;
}

// Get or create remote folder
mega::MegaNode* FileOperations::getOrCreateRemoteFolder(const std::string& path) {
    // Parse path
    std::vector<std::string> parts;
    std::stringstream ss(path);
    std::string part;

    while (std::getline(ss, part, '/')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }

    if (parts.empty()) {
        // Return root node
        return m_megaApi->getRootNode();
    }

    // Navigate/create path
    mega::MegaNode* currentNode = m_megaApi->getRootNode();

    for (size_t i = 0; i < parts.size() - 1; i++) {
        mega::MegaNode* child = m_megaApi->getChildNode(currentNode, parts[i].c_str());

        if (!child) {
            // Create folder
            m_megaApi->createFolder(parts[i].c_str(), currentNode);
            // Wait a bit for folder creation
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            child = m_megaApi->getChildNode(currentNode, parts[i].c_str());
        }

        if (currentNode != m_megaApi->getRootNode()) {
            delete currentNode;
        }
        currentNode = child;

        if (!currentNode) {
            return nullptr;
        }
    }

    return currentNode;
}

// List local files
std::vector<std::string> FileOperations::listLocalFiles(const std::string& directory, bool recursive) {
    std::vector<std::string> files;

    try {
        if (recursive) {
            for (const auto& entry : fs::recursive_directory_iterator(directory)) {
                if (entry.is_regular_file()) {
                    files.push_back(entry.path().string());
                }
            }
        } else {
            for (const auto& entry : fs::directory_iterator(directory)) {
                if (entry.is_regular_file()) {
                    files.push_back(entry.path().string());
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error listing files: " << e.what() << std::endl;
    }

    return files;
}

// Generate transfer ID
std::string FileOperations::generateTransferId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);

    std::stringstream ss;
    for (int i = 0; i < 16; i++) {
        ss << std::hex << dis(gen);
    }

    return ss.str();
}

// Cancel all transfers
void FileOperations::cancelAllTransfers() {
    m_megaApi->cancelTransfers(mega::MegaTransfer::TYPE_UPLOAD);
    m_megaApi->cancelTransfers(mega::MegaTransfer::TYPE_DOWNLOAD);
    m_activeTransfers.clear();
}

// Set bandwidth limits
void FileOperations::setBandwidthLimits(int uploadBps, int downloadBps) {
    // Note: Actual SDK method may differ
    // m_megaApi->setUploadLimit(uploadBps);
    // m_megaApi->setDownloadLimit(downloadBps);
}

// Get transfer statistics
std::string FileOperations::getTransferStatistics() const {
    std::stringstream ss;
    ss << "{"
       << "\"totalBytesUploaded\":" << m_stats.totalBytesUploaded << ","
       << "\"totalBytesDownloaded\":" << m_stats.totalBytesDownloaded << ","
       << "\"successfulUploads\":" << m_stats.successfulUploads << ","
       << "\"failedUploads\":" << m_stats.failedUploads << ","
       << "\"successfulDownloads\":" << m_stats.successfulDownloads << ","
       << "\"failedDownloads\":" << m_stats.failedDownloads
       << "}";
    return ss.str();
}

// Check if remote file exists
bool FileOperations::remoteFileExists(const std::string& remotePath) {
    mega::MegaNode* node = m_megaApi->getNodeByPath(remotePath.c_str());
    bool exists = (node != nullptr);
    if (node) {
        delete node;
    }
    return exists;
}

// Set progress callback
void FileOperations::setProgressCallback(std::function<void(const TransferProgress&)> callback) {
    m_progressCallback = callback;
}

// Set completion callback
void FileOperations::setCompletionCallback(std::function<void(const TransferResult&)> callback) {
    m_completionCallback = callback;
}

// Queue upload (simplified implementation)
std::string FileOperations::queueUpload(const std::string& localPath,
                                       const std::string& remotePath,
                                       int priority) {
    auto info = std::make_unique<TransferInfo>();
    info->transferId = generateTransferId();
    info->localPath = localPath;
    info->remotePath = remotePath;
    info->priority = priority;
    info->isUpload = true;

    std::string id = info->transferId;
    // Would add to queue here
    return id;
}

// Queue download (simplified implementation)
std::string FileOperations::queueDownload(mega::MegaNode* remoteNode,
                                         const std::string& localPath,
                                         int priority) {
    auto info = std::make_unique<TransferInfo>();
    info->transferId = generateTransferId();
    info->node = remoteNode;
    info->localPath = localPath;
    info->priority = priority;
    info->isUpload = false;

    std::string id = info->transferId;
    // Would add to queue here
    return id;
}

// Start queue processing
void FileOperations::startQueueProcessing(int maxConcurrent) {
    // Already processing - don't start again
    if (m_queueProcessing) {
        return;
    }

    m_maxConcurrentTransfers = maxConcurrent;
    m_queueProcessing = true;

    // Join any previous threads if they exist
    if (m_uploadQueueThread.joinable()) {
        m_uploadQueueThread.join();
    }
    if (m_downloadQueueThread.joinable()) {
        m_downloadQueueThread.join();
    }

    // Start processing threads - store handles for proper cleanup
    m_uploadQueueThread = std::thread(&FileOperations::processUploadQueue, this);
    m_downloadQueueThread = std::thread(&FileOperations::processDownloadQueue, this);
}

// Stop queue processing
void FileOperations::stopQueueProcessing(bool cancelPending) {
    // Signal threads to stop
    m_queueProcessing = false;

    // Wait for queue processing threads to finish
    if (m_uploadQueueThread.joinable()) {
        m_uploadQueueThread.join();
    }
    if (m_downloadQueueThread.joinable()) {
        m_downloadQueueThread.join();
    }

    if (cancelPending) {
        // Clear queues
        while (!m_uploadQueue.empty()) {
            m_uploadQueue.pop();
        }
        while (!m_downloadQueue.empty()) {
            m_downloadQueue.pop();
        }
    }
}

// Process upload queue
void FileOperations::processUploadQueue() {
    while (m_queueProcessing) {
        if (m_uploadQueue.empty() || m_activeUploads >= m_maxConcurrentTransfers) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Would process queue items here
    }
}

// Process download queue
void FileOperations::processDownloadQueue() {
    while (m_queueProcessing) {
        if (m_downloadQueue.empty() || m_activeDownloads >= m_maxConcurrentTransfers) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Would process queue items here
    }
}

// Pause transfer (simplified)
bool FileOperations::pauseTransfer(const std::string& transferId) {
    // SDK doesn't directly support pause/resume by ID
    // Would need to track and manage transfers
    return false;
}

// Resume transfer (simplified)
bool FileOperations::resumeTransfer(const std::string& transferId) {
    // SDK doesn't directly support pause/resume by ID
    return false;
}

// Cancel transfer (simplified)
bool FileOperations::cancelTransfer(const std::string& transferId) {
    // Would need to track transfers and cancel by tag
    return false;
}

// Get transfer progress
std::optional<TransferProgress> FileOperations::getTransferProgress(const std::string& transferId) {
    auto it = this->m_activeTransfers.find(transferId);
    if (it != this->m_activeTransfers.end()) {
        return it->second->progress;
    }
    return std::nullopt;
}

// Get all active transfers
std::vector<TransferProgress> FileOperations::getAllActiveTransfers() {
    std::vector<TransferProgress> transfers;
    for (const auto& [id, info] : this->m_activeTransfers) {
        transfers.push_back(info->progress);
    }
    return transfers;
}

// Download multiple files
std::vector<TransferResult> FileOperations::downloadFiles(
    const std::vector<std::pair<mega::MegaNode*, std::string>>& files,
    const DownloadConfig& config,
    std::function<void(const TransferProgress&)> progressCallback) {

    std::vector<TransferResult> results;

    // Store original callback
    auto originalCallback = m_progressCallback;
    if (progressCallback) {
        m_progressCallback = progressCallback;
    }

    // Download each file
    for (const auto& [node, localPath] : files) {
        auto result = downloadFile(node, localPath, config);
        results.push_back(result);

        // Respect parallel download limit
        if (config.parallelDownloads > 0) {
            while (m_activeDownloads >= config.parallelDownloads) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    // Restore original callback
    m_progressCallback = originalCallback;

    return results;
}

// Download directory
std::vector<TransferResult> FileOperations::downloadDirectory(
    mega::MegaNode* remoteDir,
    const std::string& localDir,
    const DownloadConfig& config) {

    std::vector<TransferResult> results;

    if (!remoteDir || !remoteDir->isFolder()) {
        TransferResult result;
        result.success = false;
        result.errorMessage = "Invalid remote directory";
        result.errorCode = mega::MegaError::API_EARGS;
        results.push_back(result);
        return results;
    }

    // Create local directory if it doesn't exist
    try {
        fs::create_directories(localDir);
    } catch (const std::exception& e) {
        TransferResult result;
        result.success = false;
        result.errorMessage = std::string("Failed to create local directory: ") + e.what();
        result.errorCode = mega::MegaError::API_EWRITE;
        results.push_back(result);
        return results;
    }

    // Get children and download
    mega::MegaNodeList* children = m_megaApi->getChildren(remoteDir);
    if (children) {
        std::vector<std::pair<mega::MegaNode*, std::string>> downloadPairs;

        for (int i = 0; i < children->size(); i++) {
            mega::MegaNode* child = children->get(i);
            if (child && !child->isFolder()) {
                std::string localPath = localDir + "/" + child->getName();
                downloadPairs.push_back({child, localPath});
            }
        }

        results = downloadFiles(downloadPairs, config);
        delete children;
    }

    return results;
}

} // namespace MegaCustom