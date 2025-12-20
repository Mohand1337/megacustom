#include "features/SmartSync.h"
#include <megaapi.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <cstring>
#include <openssl/md5.h>

// Try to use nlohmann/json if available, fall back to json_simple
#ifdef __has_include
    #if __has_include(<nlohmann/json.hpp>)
        #include <nlohmann/json.hpp>
        using json = nlohmann::json;
        #define HAS_NLOHMANN_JSON
    #else
        #include "json_simple.hpp"
        namespace json_simple = nlohmann;
        using json = nlohmann::json;
    #endif
#else
    #include "json_simple.hpp"
    namespace json_simple = nlohmann;
    using json = nlohmann::json;
#endif

namespace fs = std::filesystem;

namespace MegaCustom {

// Sync instance implementation
struct SmartSync::SyncInstance {
    std::string syncId;
    SyncConfig config;
    SyncProgress progress;
    SyncPlan plan;

    enum SyncState {
        IDLE,
        ANALYZING,
        SYNCING,
        PAUSED,
        COMPLETED,
        FAILED
    } state = IDLE;

    std::atomic<bool> shouldStop{false};
    std::atomic<bool> isPaused{false};
    std::thread syncThread;
    std::chrono::steady_clock::time_point startTime;

    void reset() {
        progress = SyncProgress();
        progress.syncName = config.name;
        plan = SyncPlan();
        state = IDLE;
        shouldStop = false;
        isPaused = false;
    }
};

// Sync listener for handling SDK callbacks
class SmartSync::SyncListener : public mega::MegaTransferListener {
public:
    struct TransferResult {
        bool completed = false;
        bool success = false;
        std::string errorMessage;
        long long bytesTransferred = 0;
    };

    std::map<int, TransferResult> results;
    std::atomic<int> nextHandle{1};
    std::function<void(int, long long, long long)> progressCallback;
    std::function<void(int, const std::string&, bool)> startCallback;     // Transfer started: (handle, filename, isUpload)
    std::function<void(int, const std::string&)> tempErrorCallback;       // Temporary error: (handle, error message)

    int getNextHandle() {
        return nextHandle++;
    }

    void onTransferFinish(mega::MegaApi* api, mega::MegaTransfer* transfer, mega::MegaError* error) override {
        (void)api;  // Suppress unused parameter warning
        int handle = transfer->getTag();
        if (handle > 0) {
            TransferResult& result = results[handle];
            result.completed = true;
            result.success = (error->getErrorCode() == mega::MegaError::API_OK);
            if (!result.success) {
                result.errorMessage = error->getErrorString();
            }
            result.bytesTransferred = transfer->getTransferredBytes();
        }
    }

    void onTransferUpdate(mega::MegaApi* api, mega::MegaTransfer* transfer) override {
        (void)api;  // Suppress unused parameter warning
        if (progressCallback) {
            int handle = transfer->getTag();
            long long transferred = transfer->getTransferredBytes();
            long long total = transfer->getTotalBytes();
            progressCallback(handle, transferred, total);
        }
    }

    void onTransferStart(mega::MegaApi* api, mega::MegaTransfer* transfer) override {
        (void)api;  // Suppress unused parameter warning
        int handle = transfer->getTag();
        if (handle > 0) {
            // Initialize result entry for this transfer
            TransferResult& result = results[handle];
            result.completed = false;
            result.success = false;
            result.bytesTransferred = 0;

            // Notify start callback if set
            if (startCallback) {
                const char* fileName = transfer->getFileName();
                bool isUpload = (transfer->getType() == mega::MegaTransfer::TYPE_UPLOAD);
                startCallback(handle, fileName ? fileName : "", isUpload);
            }
        }
    }

    void onTransferTemporaryError(mega::MegaApi* api, mega::MegaTransfer* transfer, mega::MegaError* error) override {
        (void)api;  // Suppress unused parameter warning
        int handle = transfer->getTag();
        if (handle > 0 && tempErrorCallback) {
            std::string errorMsg = error->getErrorString() ? error->getErrorString() : "Unknown temporary error";
            int errorCode = error->getErrorCode();

            // Include error code in the message
            std::string fullMsg = "Temporary error (code " + std::to_string(errorCode) + "): " + errorMsg;

            // Log the temporary error
            tempErrorCallback(handle, fullMsg);
        }
    }
};

// Constructor
SmartSync::SmartSync(mega::MegaApi* megaApi)
    : m_megaApi(megaApi),
      m_schedulerRunning(false),
      m_listener(std::make_unique<SyncListener>()) {

    // Initialize statistics
    m_stats = {0, 0, 0, 0, 0, std::chrono::steady_clock::now()};
}

// Destructor
SmartSync::~SmartSync() {
    // Stop all active syncs and scheduler
    m_schedulerRunning = false;

    // Wait for scheduler thread to complete
    if (m_schedulerThread.joinable()) {
        m_schedulerThread.join();
    }

    // Wait for all sync threads to complete
    for (auto& [syncId, instance] : m_activeSyncs) {
        if (instance) {
            instance->shouldStop = true;
            if (instance->syncThread.joinable()) {
                instance->syncThread.join();
            }
        }
    }
}

// Generate unique profile ID
std::string SmartSync::generateProfileId() {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    std::stringstream ss;
    ss << "profile_" << timestamp << "_" << (rand() % 10000);
    return ss.str();
}

// Generate unique sync ID
std::string SmartSync::generateSyncId() {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    std::stringstream ss;
    ss << "sync_" << timestamp << "_" << (rand() % 10000);
    return ss.str();
}

// Generate unique backup ID
std::string SmartSync::generateBackupId() {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    std::stringstream ss;
    ss << "backup_" << timestamp << "_" << (rand() % 10000);
    return ss.str();
}

// Create sync profile
std::string SmartSync::createSyncProfile(const SyncConfig& config) {
    std::string profileId = generateProfileId();
    m_profiles[profileId] = std::make_unique<SyncConfig>(config);
    return profileId;
}

// Update sync profile
bool SmartSync::updateSyncProfile(const std::string& profileId, const SyncConfig& config) {
    auto it = m_profiles.find(profileId);
    if (it == m_profiles.end()) {
        return false;
    }

    *it->second = config;
    return true;
}

// Delete sync profile
bool SmartSync::deleteSyncProfile(const std::string& profileId) {
    auto it = m_profiles.find(profileId);
    if (it == m_profiles.end()) {
        return false;
    }

    // Stop any active sync using this profile
    for (auto& [syncId, instance] : m_activeSyncs) {
        if (instance && instance->config.name == it->second->name) {
            instance->shouldStop = true;
        }
    }

    m_profiles.erase(it);
    return true;
}

// Get sync profile
std::optional<SyncConfig> SmartSync::getSyncProfile(const std::string& profileId) {
    auto it = m_profiles.find(profileId);
    if (it == m_profiles.end()) {
        return std::nullopt;
    }

    return *it->second;
}

// List sync profiles
std::vector<std::pair<std::string, std::string>> SmartSync::listSyncProfiles() {
    std::vector<std::pair<std::string, std::string>> profiles;

    for (const auto& [profileId, config] : m_profiles) {
        profiles.push_back({profileId, config->name});
    }

    return profiles;
}

// Calculate checksum of file
std::string SmartSync::calculateChecksum(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    MD5_CTX md5Context;
    MD5_Init(&md5Context);

    char buffer[8192];
    while (file.read(buffer, sizeof(buffer)) || file.gcount()) {
        MD5_Update(&md5Context, buffer, file.gcount());
    }

    unsigned char result[MD5_DIGEST_LENGTH];
    MD5_Final(result, &md5Context);

    std::stringstream ss;
    for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)result[i];
    }

    return ss.str();
}

// Check if file should be included based on filter
bool SmartSync::shouldIncludeFile(const std::string& path, const SyncFilter& filter) {
    fs::path filePath(path);
    std::string filename = filePath.filename().string();
    std::string extension = filePath.extension().string();

    // Check custom filter first
    if (filter.customFilter) {
        if (!filter.customFilter(path, fs::is_directory(path))) {
            return false;
        }
    }

    // Check hidden files
    if (filter.excludeHiddenFiles && filename[0] == '.') {
        return false;
    }

    // Check temporary files
    if (filter.excludeTemporaryFiles) {
        // Check if filename ends with temporary extensions
        if (filename.size() > 0 && filename[filename.size() - 1] == '~') {
            return false;
        }
        if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".tmp") {
            return false;
        }
        if (filename.size() >= 5 && filename.substr(filename.size() - 5) == ".temp") {
            return false;
        }
    }

    // Check file size
    if (!fs::is_directory(path)) {
        auto fileSize = fs::file_size(path);
        if (fileSize < filter.minFileSize || fileSize > filter.maxFileSize) {
            return false;
        }

        // Check modification time
        if (filter.modifiedAfter.has_value() || filter.modifiedBefore.has_value()) {
            auto modTime = fs::last_write_time(path);
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                modTime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
            );

            if (filter.modifiedAfter.has_value() && sctp < filter.modifiedAfter.value()) {
                return false;
            }
            if (filter.modifiedBefore.has_value() && sctp > filter.modifiedBefore.value()) {
                return false;
            }
        }
    }

    // Check extensions
    if (!filter.excludeExtensions.empty()) {
        for (const auto& ext : filter.excludeExtensions) {
            if (extension == ext) {
                return false;
            }
        }
    }

    if (!filter.includeExtensions.empty()) {
        bool found = false;
        for (const auto& ext : filter.includeExtensions) {
            if (extension == ext) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }

    // Check patterns
    if (!filter.excludePatterns.empty()) {
        for (const auto& pattern : filter.excludePatterns) {
            // Simple glob pattern matching (could be enhanced)
            if (filename.find(pattern) != std::string::npos) {
                return false;
            }
        }
    }

    if (!filter.includePatterns.empty()) {
        bool matches = false;
        for (const auto& pattern : filter.includePatterns) {
            if (filename.find(pattern) != std::string::npos) {
                matches = true;
                break;
            }
        }
        if (!matches) {
            return false;
        }
    }

    return true;
}

// Compare files
FileComparison SmartSync::compareFiles(const std::string& localPath, mega::MegaNode* remoteNode) {
    FileComparison comparison;
    comparison.path = localPath;

    // Check local file
    comparison.existsLocal = fs::exists(localPath);
    if (comparison.existsLocal) {
        comparison.localSize = fs::file_size(localPath);
        auto modTime = fs::last_write_time(localPath);
        comparison.localModTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            modTime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        comparison.localChecksum = calculateChecksum(localPath);
    }

    // Check remote file
    comparison.existsRemote = (remoteNode != nullptr);
    if (comparison.existsRemote) {
        comparison.remoteSize = remoteNode->getSize();
        comparison.remoteModTime = std::chrono::system_clock::from_time_t(remoteNode->getModificationTime());
        // Note: SDK doesn't provide checksum directly, would need to download to calculate
        comparison.remoteChecksum = ""; // Simplified for now
    }

    // Determine if different
    comparison.isDifferent = false;
    if (comparison.existsLocal && comparison.existsRemote) {
        if (comparison.localSize != comparison.remoteSize) {
            comparison.isDifferent = true;
            comparison.differenceReason = "Size mismatch";
        } else if (comparison.localModTime != comparison.remoteModTime) {
            comparison.isDifferent = true;
            comparison.differenceReason = "Modification time differs";
        } else if (!comparison.localChecksum.empty() && !comparison.remoteChecksum.empty() &&
                   comparison.localChecksum != comparison.remoteChecksum) {
            comparison.isDifferent = true;
            comparison.differenceReason = "Checksum mismatch";
        }
    } else if (comparison.existsLocal != comparison.existsRemote) {
        comparison.isDifferent = true;
        comparison.differenceReason = comparison.existsLocal ? "Only exists locally" : "Only exists remotely";
    }

    return comparison;
}

// Calculate folder differences
std::map<std::string, FileComparison> SmartSync::calculateDifferences(
    const std::string& localPath,
    const std::string& remotePath) {

    std::map<std::string, FileComparison> differences;

    // Get remote folder node
    mega::MegaNode* remoteFolder = m_megaApi->getNodeByPath(remotePath.c_str());
    if (!remoteFolder) {
        std::cerr << "Remote folder not found: " << remotePath << std::endl;
        return differences;
    }

    // Collect local files
    std::set<std::string> localFiles;
    if (fs::exists(localPath) && fs::is_directory(localPath)) {
        for (const auto& entry : fs::recursive_directory_iterator(localPath)) {
            if (entry.is_regular_file()) {
                std::string relativePath = fs::relative(entry.path(), localPath).string();
                localFiles.insert(relativePath);

                // Compare with remote
                std::string remoteFilePath = remotePath + "/" + relativePath;
                mega::MegaNode* remoteFile = m_megaApi->getNodeByPath(remoteFilePath.c_str());

                FileComparison comp = compareFiles(entry.path().string(), remoteFile);
                comp.path = relativePath;
                differences[relativePath] = comp;

                if (remoteFile) {
                    delete remoteFile;
                }
            }
        }
    }

    // Check remote files not in local
    mega::MegaNodeList* children = m_megaApi->getChildren(remoteFolder);
    if (children) {
        for (int i = 0; i < children->size(); i++) {
            mega::MegaNode* child = children->get(i);
            if (child->getType() == mega::MegaNode::TYPE_FILE) {
                std::string relativePath = child->getName();

                if (localFiles.find(relativePath) == localFiles.end()) {
                    // File only exists remotely
                    FileComparison comp;
                    comp.path = relativePath;
                    comp.existsLocal = false;
                    comp.existsRemote = true;
                    comp.remoteSize = child->getSize();
                    comp.remoteModTime = std::chrono::system_clock::from_time_t(child->getModificationTime());
                    comp.isDifferent = true;
                    comp.differenceReason = "Only exists remotely";

                    differences[relativePath] = comp;
                }
            }
        }
        delete children;
    }

    delete remoteFolder;
    return differences;
}

// Detect file conflict
SyncConflict SmartSync::detectFileConflict(const FileComparison& comparison) {
    SyncConflict conflict;
    conflict.path = comparison.path;
    conflict.comparison = comparison;

    if (comparison.existsLocal && comparison.existsRemote && comparison.isDifferent) {
        // Both modified
        conflict.conflictType = "both_modified";
        conflict.description = "File has been modified in both local and remote locations";

        // Suggest resolution based on timestamps
        if (comparison.localModTime > comparison.remoteModTime) {
            conflict.suggestedResolution = ConflictResolution::NEWER_WINS;
        } else {
            conflict.suggestedResolution = ConflictResolution::NEWER_WINS;
        }
    } else if (comparison.existsLocal && !comparison.existsRemote) {
        // Local only - might have been deleted remotely
        conflict.conflictType = "local_only";
        conflict.description = "File exists only locally";
        conflict.suggestedResolution = ConflictResolution::LOCAL_WINS;
    } else if (!comparison.existsLocal && comparison.existsRemote) {
        // Remote only - might have been deleted locally
        conflict.conflictType = "remote_only";
        conflict.description = "File exists only remotely";
        conflict.suggestedResolution = ConflictResolution::REMOTE_WINS;
    }

    return conflict;
}

// Detect conflicts
std::vector<SyncConflict> SmartSync::detectConflicts(const SyncConfig& config) {
    std::vector<SyncConflict> conflicts;

    auto differences = calculateDifferences(config.localPath, config.remotePath);

    for (const auto& [path, comparison] : differences) {
        if (comparison.isDifferent) {
            // Apply filter
            std::string fullPath = config.localPath + "/" + path;
            if (!shouldIncludeFile(fullPath, config.filter)) {
                continue;
            }

            SyncConflict conflict = detectFileConflict(comparison);

            // Check if it's actually a conflict based on sync direction
            bool isConflict = false;
            switch (config.direction) {
                case SyncDirection::BIDIRECTIONAL:
                    // Conflict if both sides have changes
                    isConflict = comparison.existsLocal && comparison.existsRemote && comparison.isDifferent;
                    break;

                case SyncDirection::LOCAL_TO_REMOTE:
                case SyncDirection::MIRROR_LOCAL:
                    // No conflict for upload-only modes
                    break;

                case SyncDirection::REMOTE_TO_LOCAL:
                case SyncDirection::MIRROR_REMOTE:
                    // No conflict for download-only modes
                    break;
            }

            if (isConflict) {
                conflicts.push_back(conflict);
            }
        }
    }

    return conflicts;
}

// Analyze folders and create sync plan
SyncPlan SmartSync::analyzeFolders(const SyncConfig& config, bool dryRun) {
    SyncPlan plan;

    auto differences = calculateDifferences(config.localPath, config.remotePath);
    auto conflicts = detectConflicts(config);

    // Add conflicts to plan
    plan.conflicts = conflicts;

    // Process differences based on sync direction
    for (const auto& [path, comparison] : differences) {
        std::string fullLocalPath = config.localPath + "/" + path;

        // Apply filter
        if (!shouldIncludeFile(fullLocalPath, config.filter)) {
            continue;
        }

        // Skip if it's a conflict (will be handled separately)
        bool isConflict = false;
        for (const auto& conflict : conflicts) {
            if (conflict.path == path) {
                isConflict = true;
                break;
            }
        }
        if (isConflict) {
            continue;
        }

        // Determine action based on sync direction and file state
        switch (config.direction) {
            case SyncDirection::BIDIRECTIONAL:
                if (comparison.existsLocal && !comparison.existsRemote) {
                    plan.filesToUpload.push_back(path);
                    plan.totalUploadSize += comparison.localSize;
                } else if (!comparison.existsLocal && comparison.existsRemote) {
                    plan.filesToDownload.push_back(path);
                    plan.totalDownloadSize += comparison.remoteSize;
                } else if (comparison.isDifferent) {
                    // Determine direction based on modification time
                    if (comparison.localModTime > comparison.remoteModTime) {
                        plan.filesToUpload.push_back(path);
                        plan.totalUploadSize += comparison.localSize;
                    } else {
                        plan.filesToDownload.push_back(path);
                        plan.totalDownloadSize += comparison.remoteSize;
                    }
                }
                break;

            case SyncDirection::LOCAL_TO_REMOTE:
                if (comparison.existsLocal && (!comparison.existsRemote || comparison.isDifferent)) {
                    plan.filesToUpload.push_back(path);
                    plan.totalUploadSize += comparison.localSize;
                }
                if (!comparison.existsLocal && comparison.existsRemote && config.deleteOrphans) {
                    plan.filesToDelete.push_back(path);
                }
                break;

            case SyncDirection::REMOTE_TO_LOCAL:
                if (comparison.existsRemote && (!comparison.existsLocal || comparison.isDifferent)) {
                    plan.filesToDownload.push_back(path);
                    plan.totalDownloadSize += comparison.remoteSize;
                }
                if (comparison.existsLocal && !comparison.existsRemote && config.deleteOrphans) {
                    plan.filesToDelete.push_back(path);
                }
                break;

            case SyncDirection::MIRROR_LOCAL:
                if (comparison.existsLocal) {
                    if (!comparison.existsRemote || comparison.isDifferent) {
                        plan.filesToUpload.push_back(path);
                        plan.totalUploadSize += comparison.localSize;
                    }
                } else if (comparison.existsRemote) {
                    plan.filesToDelete.push_back(path);
                }
                break;

            case SyncDirection::MIRROR_REMOTE:
                if (comparison.existsRemote) {
                    if (!comparison.existsLocal || comparison.isDifferent) {
                        plan.filesToDownload.push_back(path);
                        plan.totalDownloadSize += comparison.remoteSize;
                    }
                } else if (comparison.existsLocal) {
                    plan.filesToDelete.push_back(path);
                }
                break;
        }
    }

    // Estimate time (simplified calculation)
    long long totalBytes = plan.totalUploadSize + plan.totalDownloadSize;
    double estimatedSpeed = 1024 * 1024; // 1 MB/s estimated
    plan.estimatedTimeSeconds = static_cast<int>(totalBytes / estimatedSpeed);

    return plan;
}

// Ensure remote path exists
mega::MegaNode* SmartSync::ensureRemotePath(const std::string& path) {
    mega::MegaNode* node = m_megaApi->getNodeByPath(path.c_str());

    if (!node) {
        // Create folder hierarchy
        std::vector<std::string> parts;
        std::stringstream ss(path);
        std::string part;

        while (std::getline(ss, part, '/')) {
            if (!part.empty()) {
                parts.push_back(part);
            }
        }

        // Store root node pointer once - getRootNode() returns a NEW pointer each call
        mega::MegaNode* rootNode = m_megaApi->getRootNode();
        mega::MegaNode* parent = rootNode;
        std::string currentPath = "";

        for (const auto& folderName : parts) {
            currentPath += "/" + folderName;
            mega::MegaNode* child = m_megaApi->getNodeByPath(currentPath.c_str());

            if (!child) {
                // Create folder
                m_megaApi->createFolder(folderName.c_str(), parent);

                // Wait a bit for folder creation
                std::this_thread::sleep_for(std::chrono::milliseconds(500));

                child = m_megaApi->getNodeByPath(currentPath.c_str());
            }

            // Only delete non-root nodes (compare against stored pointer)
            if (parent != rootNode) {
                delete parent;
            }
            parent = child;
        }

        node = parent;

        // Clean up root node if it wasn't returned as the final result
        if (node != rootNode) {
            delete rootNode;
        }
    }

    return node;
}

// Execute sync plan
void SmartSync::executeSyncPlan(const SyncPlan& plan, SyncInstance* instance) {
    if (!instance) return;

    instance->progress.totalOperations = plan.filesToUpload.size() +
                                        plan.filesToDownload.size() +
                                        plan.filesToDelete.size();
    instance->progress.totalBytes = plan.totalUploadSize + plan.totalDownloadSize;
    instance->progress.completedOperations = 0;
    instance->progress.failedOperations = 0;

    auto startTime = std::chrono::steady_clock::now();

    // Process uploads
    for (const auto& relativePath : plan.filesToUpload) {
        if (instance->shouldStop) break;
        if (instance->isPaused) {
            while (instance->isPaused && !instance->shouldStop) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        std::string localPath = instance->config.localPath + "/" + relativePath;
        std::string remotePath = instance->config.remotePath + "/" + relativePath;

        instance->progress.currentOperation = "Uploading";
        instance->progress.currentFile = relativePath;

        // Ensure parent directory exists
        fs::path remoteFilePath(remotePath);
        std::string remoteDir = remoteFilePath.parent_path().string();
        mega::MegaNode* parentNode = ensureRemotePath(remoteDir);

        if (parentNode) {
            // Start upload
            m_megaApi->startUpload(localPath.c_str(), parentNode, nullptr, 0, nullptr,
                                 false, false, nullptr, m_listener.get());

            instance->progress.completedOperations++;
            instance->progress.bytesTransferred += fs::file_size(localPath);

            delete parentNode;
        } else {
            instance->progress.failedOperations++;
        }

        // Update progress
        if (m_progressCallback) {
            auto elapsed = std::chrono::steady_clock::now() - startTime;
            instance->progress.elapsedTime = std::chrono::duration_cast<std::chrono::seconds>(elapsed);

            if (instance->progress.bytesTransferred > 0 && instance->progress.elapsedTime.count() > 0) {
                instance->progress.currentSpeed = instance->progress.bytesTransferred /
                                                 instance->progress.elapsedTime.count();

                long long remainingBytes = instance->progress.totalBytes - instance->progress.bytesTransferred;
                if (instance->progress.currentSpeed > 0) {
                    instance->progress.estimatedTimeRemaining =
                        std::chrono::seconds(static_cast<long>(remainingBytes / instance->progress.currentSpeed));
                }
            }

            instance->progress.progressPercentage = (instance->progress.completedOperations * 100.0) /
                                                   instance->progress.totalOperations;

            m_progressCallback(instance->progress);
        }
    }

    // Process downloads
    for (const auto& relativePath : plan.filesToDownload) {
        if (instance->shouldStop) break;
        if (instance->isPaused) {
            while (instance->isPaused && !instance->shouldStop) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        std::string localPath = instance->config.localPath + "/" + relativePath;
        std::string remotePath = instance->config.remotePath + "/" + relativePath;

        instance->progress.currentOperation = "Downloading";
        instance->progress.currentFile = relativePath;

        // Ensure local directory exists
        fs::path localFilePath(localPath);
        fs::create_directories(localFilePath.parent_path());

        // Get remote node
        mega::MegaNode* remoteNode = m_megaApi->getNodeByPath(remotePath.c_str());
        if (remoteNode) {
            // Start download
            m_megaApi->startDownload(remoteNode, localPath.c_str(), nullptr, nullptr,
                                   false, nullptr, mega::MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                                   mega::MegaTransfer::COLLISION_RESOLUTION_OVERWRITE,
                                   false, m_listener.get());

            instance->progress.completedOperations++;
            instance->progress.bytesTransferred += remoteNode->getSize();

            delete remoteNode;
        } else {
            instance->progress.failedOperations++;
        }

        // Update progress
        if (m_progressCallback) {
            instance->progress.progressPercentage = (instance->progress.completedOperations * 100.0) /
                                                   instance->progress.totalOperations;
            m_progressCallback(instance->progress);
        }
    }

    // Process deletions
    for (const auto& relativePath : plan.filesToDelete) {
        if (instance->shouldStop) break;

        instance->progress.currentOperation = "Deleting";
        instance->progress.currentFile = relativePath;

        // Determine if local or remote deletion
        std::string localPath = instance->config.localPath + "/" + relativePath;
        std::string remotePath = instance->config.remotePath + "/" + relativePath;

        if (fs::exists(localPath)) {
            // Delete local file
            try {
                fs::remove(localPath);
                instance->progress.completedOperations++;
            } catch (const fs::filesystem_error& e) {
                instance->progress.failedOperations++;
                std::cerr << "Failed to delete local file: " << e.what() << std::endl;
            }
        } else {
            // Delete remote file
            mega::MegaNode* remoteNode = m_megaApi->getNodeByPath(remotePath.c_str());
            if (remoteNode) {
                m_megaApi->remove(remoteNode);
                instance->progress.completedOperations++;
                delete remoteNode;
            } else {
                instance->progress.failedOperations++;
            }
        }

        // Update progress
        if (m_progressCallback) {
            instance->progress.progressPercentage = (instance->progress.completedOperations * 100.0) /
                                                   instance->progress.totalOperations;
            m_progressCallback(instance->progress);
        }
    }

    // Mark as completed
    instance->state = SyncInstance::COMPLETED;
}

// Start sync
bool SmartSync::startSync(const std::string& profileId) {
    auto profileIt = m_profiles.find(profileId);
    if (profileIt == m_profiles.end()) {
        return false;
    }

    std::string syncId = generateSyncId();
    auto instance = std::make_unique<SyncInstance>();
    instance->syncId = syncId;
    instance->config = *profileIt->second;
    instance->reset();

    // Analyze folders
    instance->plan = analyzeFolders(instance->config, false);

    // Handle conflicts if needed
    if (!instance->plan.conflicts.empty() && instance->config.conflictStrategy == ConflictResolution::ASK_USER) {
        if (m_conflictResolver) {
            for (const auto& conflict : instance->plan.conflicts) {
                ConflictResolution resolution = m_conflictResolver(conflict);
                resolveConflict(conflict, resolution);
            }
        }
    }

    // Start sync in separate thread
    instance->state = SyncInstance::SYNCING;
    instance->startTime = std::chrono::steady_clock::now();

    SyncInstance* instancePtr = instance.get();
    instance->syncThread = std::thread([this, instancePtr]() {
        executeSyncPlan(instancePtr->plan, instancePtr);

        // Create report
        SyncReport report;
        report.syncName = instancePtr->config.name;
        report.startTime = std::chrono::system_clock::now();
        report.endTime = std::chrono::system_clock::now();
        report.filesUploaded = instancePtr->plan.filesToUpload.size();
        report.filesDownloaded = instancePtr->plan.filesToDownload.size();
        report.filesDeleted = instancePtr->plan.filesToDelete.size();
        report.filesFailed = instancePtr->progress.failedOperations;
        report.bytesUploaded = instancePtr->plan.totalUploadSize;
        report.bytesDownloaded = instancePtr->plan.totalDownloadSize;
        report.success = (instancePtr->progress.failedOperations == 0);

        m_syncReports[instancePtr->syncId] = report;

        // Update statistics
        m_stats.totalBytesUploaded += report.bytesUploaded;
        m_stats.totalBytesDownloaded += report.bytesDownloaded;
        m_stats.totalSyncs++;
        if (report.success) {
            m_stats.successfulSyncs++;
        } else {
            m_stats.failedSyncs++;
        }
    });

    m_activeSyncs[syncId] = std::move(instance);
    return true;
}

// Start custom sync
std::string SmartSync::startCustomSync(const SyncConfig& config) {
    std::string profileId = createSyncProfile(config);
    if (startSync(profileId)) {
        return profileId;
    }
    return "";
}

// Pause sync
bool SmartSync::pauseSync(const std::string& syncId) {
    auto it = m_activeSyncs.find(syncId);
    if (it == m_activeSyncs.end()) {
        return false;
    }

    it->second->isPaused = true;
    it->second->state = SyncInstance::PAUSED;
    return true;
}

// Resume sync
bool SmartSync::resumeSync(const std::string& syncId) {
    auto it = m_activeSyncs.find(syncId);
    if (it == m_activeSyncs.end()) {
        return false;
    }

    it->second->isPaused = false;
    it->second->state = SyncInstance::SYNCING;
    return true;
}

// Stop sync
bool SmartSync::stopSync(const std::string& syncId) {
    auto it = m_activeSyncs.find(syncId);
    if (it == m_activeSyncs.end()) {
        return false;
    }

    it->second->shouldStop = true;

    if (it->second->syncThread.joinable()) {
        it->second->syncThread.join();
    }

    m_activeSyncs.erase(it);
    return true;
}

// Get sync progress
std::optional<SyncProgress> SmartSync::getSyncProgress(const std::string& syncId) {
    auto it = m_activeSyncs.find(syncId);
    if (it == m_activeSyncs.end()) {
        return std::nullopt;
    }

    return it->second->progress;
}

// Get active syncs
std::vector<std::string> SmartSync::getActiveSyncs() {
    std::vector<std::string> activeSyncs;

    for (const auto& [syncId, instance] : m_activeSyncs) {
        if (instance->state == SyncInstance::SYNCING ||
            instance->state == SyncInstance::PAUSED) {
            activeSyncs.push_back(syncId);
        }
    }

    return activeSyncs;
}

// Get sync report
std::optional<SyncReport> SmartSync::getSyncReport(const std::string& syncId) {
    auto it = m_syncReports.find(syncId);
    if (it == m_syncReports.end()) {
        return std::nullopt;
    }

    return it->second;
}

// Resolve conflict
bool SmartSync::resolveConflict(const SyncConflict& conflict, ConflictResolution resolution) {
    std::string localPath = conflict.comparison.path;
    std::string remotePath = conflict.comparison.path; // Assuming relative path

    switch (resolution) {
        case ConflictResolution::NEWER_WINS:
            if (conflict.comparison.localModTime > conflict.comparison.remoteModTime) {
                // Upload local version
                // Implementation would go here
            } else {
                // Download remote version
                // Implementation would go here
            }
            break;

        case ConflictResolution::LOCAL_WINS:
            // Upload local version
            // Implementation would go here
            break;

        case ConflictResolution::REMOTE_WINS:
            // Download remote version
            // Implementation would go here
            break;

        case ConflictResolution::RENAME_BOTH:
            // Rename and keep both versions
            // Implementation would go here
            break;

        default:
            break;
    }

    return true;
}

// Create backup
std::string SmartSync::createBackup(const std::string& path) {
    std::string backupId = generateBackupId();

    BackupInfo backup;
    backup.backupId = backupId;
    backup.originalPath = path;
    backup.backupPath = path + ".backup_" + backupId;
    backup.timestamp = std::chrono::system_clock::now();

    try {
        if (fs::exists(path)) {
            fs::copy(path, backup.backupPath, fs::copy_options::recursive);
            m_backups[backupId] = backup;
            return backupId;
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Failed to create backup: " << e.what() << std::endl;
    }

    return "";
}

// Restore backup
bool SmartSync::restoreBackup(const std::string& backupId) {
    auto it = m_backups.find(backupId);
    if (it == m_backups.end()) {
        return false;
    }

    try {
        if (fs::exists(it->second.backupPath)) {
            // Remove current version
            if (fs::exists(it->second.originalPath)) {
                fs::remove_all(it->second.originalPath);
            }

            // Restore backup
            fs::copy(it->second.backupPath, it->second.originalPath, fs::copy_options::recursive);
            return true;
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Failed to restore backup: " << e.what() << std::endl;
    }

    return false;
}

// Enable auto-sync
bool SmartSync::enableAutoSync(const std::string& profileId, std::chrono::minutes interval) {
    auto it = m_profiles.find(profileId);
    if (it == m_profiles.end()) {
        return false;
    }

    ScheduledSync scheduled;
    scheduled.profileId = profileId;
    scheduled.interval = interval;
    scheduled.nextRun = std::chrono::system_clock::now() + interval;
    scheduled.enabled = true;

    m_scheduledSyncs.push_back(scheduled);

    // Start scheduler thread if not running (atomic check-and-set to prevent race)
    bool expected = false;
    if (m_schedulerRunning.compare_exchange_strong(expected, true)) {
        // Join any previous scheduler thread if exists
        if (m_schedulerThread.joinable()) {
            m_schedulerThread.join();
        }

        // Start scheduler thread - store handle for proper cleanup
        m_schedulerThread = std::thread([this]() {
            processScheduledSyncs();
        });
    }

    return true;
}

// Disable auto-sync
bool SmartSync::disableAutoSync(const std::string& profileId) {
    for (auto& scheduled : m_scheduledSyncs) {
        if (scheduled.profileId == profileId) {
            scheduled.enabled = false;
            return true;
        }
    }
    return false;
}

// Schedule sync
bool SmartSync::scheduleSync(const std::string& profileId,
                            std::chrono::system_clock::time_point scheduleTime) {
    auto it = m_profiles.find(profileId);
    if (it == m_profiles.end()) {
        return false;
    }

    ScheduledSync scheduled;
    scheduled.profileId = profileId;
    scheduled.nextRun = scheduleTime;
    scheduled.enabled = true;
    scheduled.interval = std::chrono::minutes(0); // One-time

    m_scheduledSyncs.push_back(scheduled);

    // Start scheduler if needed (atomic check-and-set to prevent race)
    bool expected = false;
    if (m_schedulerRunning.compare_exchange_strong(expected, true)) {
        // Join any previous scheduler thread if exists
        if (m_schedulerThread.joinable()) {
            m_schedulerThread.join();
        }

        // Start scheduler thread - store handle for proper cleanup
        m_schedulerThread = std::thread([this]() {
            processScheduledSyncs();
        });
    }

    return true;
}

// Process scheduled syncs
void SmartSync::processScheduledSyncs() {
    while (m_schedulerRunning) {
        auto now = std::chrono::system_clock::now();

        for (auto& scheduled : m_scheduledSyncs) {
            if (scheduled.enabled && now >= scheduled.nextRun) {
                // Start sync
                startSync(scheduled.profileId);

                // Update next run time
                if (scheduled.interval.count() > 0) {
                    scheduled.nextRun = now + scheduled.interval;
                } else {
                    scheduled.enabled = false; // One-time sync
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(60));
    }
}

// Set callbacks
void SmartSync::setConflictResolver(
    std::function<ConflictResolution(const SyncConflict&)> resolver) {
    m_conflictResolver = resolver;
}

void SmartSync::setProgressCallback(std::function<void(const SyncProgress&)> callback) {
    m_progressCallback = callback;
}

void SmartSync::setErrorCallback(
    std::function<void(const std::string&, const std::string&)> callback) {
    m_errorCallback = callback;
}

// Export profile
bool SmartSync::exportProfile(const std::string& profileId, const std::string& filePath) {
    auto it = m_profiles.find(profileId);
    if (it == m_profiles.end()) {
        return false;
    }

    try {
        json j;
        const auto& config = *it->second;

        j["name"] = config.name;
        j["localPath"] = config.localPath;
        j["remotePath"] = config.remotePath;
        j["direction"] = static_cast<int>(config.direction);
        j["conflictStrategy"] = static_cast<int>(config.conflictStrategy);
        j["maxConcurrentTransfers"] = config.maxConcurrentTransfers;
        j["bandwidthLimit"] = config.bandwidthLimit;
        j["useDeltaSync"] = config.useDeltaSync;
        j["deleteOrphans"] = config.deleteOrphans;
        j["preserveTimestamps"] = config.preserveTimestamps;
        j["autoSync"] = config.autoSync;
        j["syncInterval"] = static_cast<int>(config.syncInterval.count());

        std::ofstream file(filePath);
        if (!file.is_open()) {
            return false;
        }

        #ifdef HAS_NLOHMANN_JSON
            file << j.dump(4);
        #else
            file << j.dump();
        #endif

        file.close();
        return true;

    } catch (const std::exception& e) {
        std::cerr << "Failed to export profile: " << e.what() << std::endl;
        return false;
    }
}

// Import profile
std::string SmartSync::importProfile(const std::string& filePath) {
    try {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            return "";
        }

        json j;
        file >> j;
        file.close();

        SyncConfig config;

        #ifdef HAS_NLOHMANN_JSON
            config.name = j["name"].get<std::string>();
            config.localPath = j["localPath"].get<std::string>();
            config.remotePath = j["remotePath"].get<std::string>();
            config.direction = static_cast<SyncDirection>(j["direction"].get<int>());
            config.conflictStrategy = static_cast<ConflictResolution>(j["conflictStrategy"].get<int>());
        #else
            // Simplified for json_simple
            config.name = j["name"].dump();
            config.localPath = j["localPath"].dump();
            config.remotePath = j["remotePath"].dump();
            // Can't properly handle enums with json_simple
        #endif

        return createSyncProfile(config);

    } catch (const std::exception& e) {
        std::cerr << "Failed to import profile: " << e.what() << std::endl;
        return "";
    }
}

// Get statistics
std::string SmartSync::getStatistics() const {
    try {
        json j;

        j["totalBytesUploaded"] = std::to_string(m_stats.totalBytesUploaded);
        j["totalBytesDownloaded"] = std::to_string(m_stats.totalBytesDownloaded);
        j["totalSyncs"] = std::to_string(m_stats.totalSyncs);
        j["successfulSyncs"] = std::to_string(m_stats.successfulSyncs);
        j["failedSyncs"] = std::to_string(m_stats.failedSyncs);

        // Calculate uptime
        auto now = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
            now - m_stats.startTime).count();
        j["uptimeSeconds"] = std::to_string(uptime);

        j["activeProfiles"] = std::to_string(m_profiles.size());
        j["activeSyncs"] = std::to_string(m_activeSyncs.size());
        j["scheduledSyncs"] = std::to_string(m_scheduledSyncs.size());

        #ifdef HAS_NLOHMANN_JSON
            return j.dump(4);
        #else
            return j.dump();
        #endif

    } catch (const std::exception& e) {
        return "{}";
    }
}

// Verify sync integrity
std::vector<std::string> SmartSync::verifySyncIntegrity(const std::string& profileId) {
    std::vector<std::string> issues;

    auto it = m_profiles.find(profileId);
    if (it == m_profiles.end()) {
        issues.push_back("Profile not found");
        return issues;
    }

    const auto& config = *it->second;

    // Check local path exists
    if (!fs::exists(config.localPath)) {
        issues.push_back("Local path does not exist: " + config.localPath);
    }

    // Check remote path exists
    mega::MegaNode* remoteNode = m_megaApi->getNodeByPath(config.remotePath.c_str());
    if (!remoteNode) {
        issues.push_back("Remote path does not exist: " + config.remotePath);
    } else {
        delete remoteNode;
    }

    // Check for conflicts
    auto conflicts = detectConflicts(config);
    if (!conflicts.empty()) {
        issues.push_back("Found " + std::to_string(conflicts.size()) + " conflicts");
    }

    // Check file integrity by comparing checksums
    auto differences = calculateDifferences(config.localPath, config.remotePath);
    int corruptedFiles = 0;
    for (const auto& [path, comparison] : differences) {
        if (comparison.existsLocal && comparison.existsRemote &&
            !comparison.localChecksum.empty() && !comparison.remoteChecksum.empty() &&
            comparison.localChecksum != comparison.remoteChecksum &&
            comparison.localSize == comparison.remoteSize) {
            corruptedFiles++;
        }
    }

    if (corruptedFiles > 0) {
        issues.push_back("Found " + std::to_string(corruptedFiles) + " potentially corrupted files");
    }

    return issues;
}

} // namespace MegaCustom