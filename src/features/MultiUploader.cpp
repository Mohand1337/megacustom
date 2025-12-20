#include "features/MultiUploader.h"
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
#include <condition_variable>

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

// Upload task implementation
struct MultiUploader::UploadTaskImpl {
    BulkUploadTask config;
    BulkUploadProgress progress;
    BulkUploadReport report;

    enum TaskState {
        PENDING,
        RUNNING,
        PAUSED,
        COMPLETED,
        CANCELLED,
        FAILED
    } state = PENDING;

    std::queue<std::pair<std::string, int>> uploadQueue; // file path, destination index
    std::atomic<bool> shouldStop{false};
    std::atomic<bool> isPaused{false};
    std::atomic<int> activeUploads{0};
    int maxConcurrent = 4;

    // Thread for this task - stored to enable proper cleanup
    std::thread workerThread;

    // Condition variable for pause/resume and upload slot availability
    std::mutex cvMutex;
    std::condition_variable stateCV;

    std::chrono::steady_clock::time_point startTime;
    std::chrono::system_clock::time_point scheduleTime;

    void reset() {
        progress = BulkUploadProgress();
        progress.taskId = config.taskId;
        report = BulkUploadReport();
        report.taskId = config.taskId;
        shouldStop = false;
        isPaused = false;
        activeUploads = 0;
        state = PENDING;
    }

    // Notify waiting threads when state changes
    void notifyStateChange() {
        stateCV.notify_all();
    }
};

// Upload listener for handling SDK callbacks
class MultiUploader::UploadListener : public mega::MegaTransferListener {
public:
    struct UploadResult {
        bool completed = false;
        bool success = false;
        std::string errorMessage;
        long long bytesTransferred = 0;
    };

    std::map<int, UploadResult> results;
    std::atomic<int> nextHandle{1};
    std::function<void(int, long long, long long)> progressCallback;
    std::function<void(int, const std::string&)> startCallback;       // Transfer started: (handle, filename)
    std::function<void(int, const std::string&)> tempErrorCallback;   // Temporary error: (handle, error message)

    // Condition variable for transfer completion signaling
    std::mutex completionMutex;
    std::condition_variable completionCV;

    int getNextHandle() {
        return nextHandle++;
    }

    void onTransferFinish(mega::MegaApi* api, mega::MegaTransfer* transfer, mega::MegaError* error) override {
        (void)api;  // Suppress unused parameter warning
        int handle = transfer->getTag();
        if (handle > 0) {
            {
                std::lock_guard<std::mutex> lock(completionMutex);
                UploadResult& result = results[handle];
                result.completed = true;
                result.success = (error->getErrorCode() == mega::MegaError::API_OK);
                if (!result.success) {
                    result.errorMessage = error->getErrorString();
                }
                result.bytesTransferred = transfer->getTransferredBytes();
            }
            completionCV.notify_all();  // Wake up waiting threads
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
            UploadResult& result = results[handle];
            result.completed = false;
            result.success = false;
            result.bytesTransferred = 0;

            // Notify start callback if set
            if (startCallback) {
                const char* fileName = transfer->getFileName();
                startCallback(handle, fileName ? fileName : "");
            }
        }
    }

    void onTransferTemporaryError(mega::MegaApi* api, mega::MegaTransfer* transfer, mega::MegaError* error) override {
        (void)api;  // Suppress unused parameter warning
        int handle = transfer->getTag();
        if (handle > 0 && tempErrorCallback) {
            std::string errorMsg = error->getErrorString() ? error->getErrorString() : "Unknown temporary error";
            int errorCode = error->getErrorCode();

            // Include error code and retry info in the message
            std::string fullMsg = "Temporary error (code " + std::to_string(errorCode) + "): " + errorMsg;

            // Log the temporary error
            tempErrorCallback(handle, fullMsg);
        }
    }
};

// Constructor
MultiUploader::MultiUploader(mega::MegaApi* megaApi)
    : m_megaApi(megaApi),
      m_activeTasks(0),
      m_maxConcurrentUploads(4),
      m_bandwidthLimit(0),
      m_listener(std::make_unique<UploadListener>()) {

    // Initialize statistics
    m_stats = {0, 0, 0, 0, 0, std::chrono::steady_clock::now()};

    // Initialize predefined distribution rules
    initializePredefinedRules();
}

// Destructor
MultiUploader::~MultiUploader() {
    // Stop the scheduler thread
    m_schedulerRunning = false;
    if (m_schedulerThread.joinable()) {
        m_schedulerThread.join();
    }

    // Signal all active tasks to stop
    for (const auto& [taskId, task] : m_tasks) {
        if (task->state == UploadTaskImpl::RUNNING) {
            task->shouldStop = true;
        }
    }

    // Wait for all worker threads to complete
    for (auto& [taskId, task] : m_tasks) {
        if (task->workerThread.joinable()) {
            task->workerThread.join();
        }
    }
}

// Initialize predefined distribution rules
void MultiUploader::initializePredefinedRules() {
    // Image files rule
    DistributionRule imageRule;
    imageRule.type = DistributionRule::BY_EXTENSION;
    imageRule.extensions = {".jpg", ".jpeg", ".png", ".gif", ".bmp", ".svg", ".webp"};
    imageRule.destinationIndex = 0;
    m_predefinedRules["images"] = imageRule;

    // Video files rule
    DistributionRule videoRule;
    videoRule.type = DistributionRule::BY_EXTENSION;
    videoRule.extensions = {".mp4", ".avi", ".mkv", ".mov", ".wmv", ".flv", ".webm"};
    videoRule.destinationIndex = 0;
    m_predefinedRules["videos"] = videoRule;

    // Document files rule
    DistributionRule docRule;
    docRule.type = DistributionRule::BY_EXTENSION;
    docRule.extensions = {".pdf", ".doc", ".docx", ".txt", ".odt", ".rtf", ".tex"};
    docRule.destinationIndex = 0;
    m_predefinedRules["documents"] = docRule;

    // Large files rule (> 100MB)
    DistributionRule largeRule;
    largeRule.type = DistributionRule::BY_SIZE;
    largeRule.sizeThreshold = 100 * 1024 * 1024; // 100MB
    largeRule.destinationIndex = 0;
    m_predefinedRules["large_files"] = largeRule;

    // Recent files rule (modified in last 7 days)
    DistributionRule recentRule;
    recentRule.type = DistributionRule::BY_DATE;
    auto now = std::chrono::system_clock::now();
    recentRule.dateThreshold = now - std::chrono::hours(7 * 24);
    recentRule.destinationIndex = 0;
    m_predefinedRules["recent_files"] = recentRule;
}

// Generate unique task ID
std::string MultiUploader::generateTaskId() {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    std::stringstream ss;
    ss << "upload_" << timestamp << "_" << (rand() % 10000);
    return ss.str();
}

// Create upload task
std::string MultiUploader::createUploadTask(const BulkUploadTask& task) {
    auto taskImpl = std::make_unique<UploadTaskImpl>();
    taskImpl->config = task;

    if (taskImpl->config.taskId.empty()) {
        taskImpl->config.taskId = generateTaskId();
    }

    taskImpl->reset();

    std::string taskId = taskImpl->config.taskId;
    m_tasks[taskId] = std::move(taskImpl);

    return taskId;
}

// Upload files to multiple destinations
std::string MultiUploader::uploadToMultipleDestinations(
    const std::vector<std::string>& files,
    const std::vector<UploadDestination>& destinations,
    const std::vector<DistributionRule>& rules) {

    BulkUploadTask task;
    task.localPath = ""; // Multiple files
    task.destinations = destinations;
    task.rules = rules;
    task.recursive = false;

    std::string taskId = createUploadTask(task);
    auto& taskImpl = m_tasks[taskId];

    // Build upload queue
    for (const auto& file : files) {
        if (!fs::exists(file)) {
            std::cerr << "File not found: " << file << std::endl;
            continue;
        }

        int destIndex = selectDestination(file, rules);
        if (destIndex >= 0 && destIndex < destinations.size()) {
            taskImpl->uploadQueue.push({file, destIndex});
            taskImpl->progress.totalFiles++;
            taskImpl->progress.totalBytes += fs::file_size(file);
        }
    }

    return taskId;
}

// Upload directory to multiple destinations
std::string MultiUploader::uploadDirectoryToMultiple(
    const std::string& directoryPath,
    const std::vector<UploadDestination>& destinations,
    const std::vector<DistributionRule>& rules,
    bool recursive) {

    BulkUploadTask task;
    task.localPath = directoryPath;
    task.destinations = destinations;
    task.rules = rules;
    task.recursive = recursive;

    std::string taskId = createUploadTask(task);
    auto& taskImpl = m_tasks[taskId];

    // Collect all files
    std::vector<std::string> files = collectFiles(directoryPath, recursive);

    // Build upload queue
    for (const auto& file : files) {
        if (m_fileFilter && !m_fileFilter(file)) {
            continue;
        }

        int destIndex = selectDestination(file, rules);
        if (destIndex >= 0 && destIndex < destinations.size()) {
            taskImpl->uploadQueue.push({file, destIndex});
            taskImpl->progress.totalFiles++;
            taskImpl->progress.totalBytes += fs::file_size(file);
        }
    }

    return taskId;
}

// Select destination based on rules
int MultiUploader::selectDestination(const std::string& filePath,
                                    const std::vector<DistributionRule>& rules) {

    static int roundRobinCounter = 0;
    static std::random_device rd;
    static std::mt19937 gen(rd());

    for (const auto& rule : rules) {
        bool matches = false;

        switch (rule.type) {
            case DistributionRule::BY_EXTENSION: {
                std::string ext = fs::path(filePath).extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                for (const auto& allowedExt : rule.extensions) {
                    if (ext == allowedExt) {
                        matches = true;
                        break;
                    }
                }
                break;
            }

            case DistributionRule::BY_SIZE: {
                auto fileSize = fs::file_size(filePath);
                matches = (fileSize >= rule.sizeThreshold);
                break;
            }

            case DistributionRule::BY_DATE: {
                auto fileTime = fs::last_write_time(filePath);
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    fileTime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
                );
                matches = (sctp >= rule.dateThreshold);
                break;
            }

            case DistributionRule::BY_REGEX: {
                try {
                    std::regex pattern(rule.regexPattern);
                    matches = std::regex_match(fs::path(filePath).filename().string(), pattern);
                } catch (const std::regex_error& e) {
                    std::cerr << "Invalid regex pattern: " << e.what() << std::endl;
                }
                break;
            }

            case DistributionRule::BY_METADATA: {
                // This would require platform-specific metadata extraction
                // For now, skip
                break;
            }

            case DistributionRule::ROUND_ROBIN: {
                return roundRobinCounter++ % rule.destinationIndex;
            }

            case DistributionRule::RANDOM: {
                std::uniform_int_distribution<> dis(0, rule.destinationIndex - 1);
                return dis(gen);
            }

            case DistributionRule::CUSTOM: {
                if (rule.customSelector) {
                    return rule.customSelector(filePath);
                }
                break;
            }
        }

        if (matches) {
            return rule.destinationIndex;
        }
    }

    // Default to first destination if no rule matches
    return 0;
}

// Collect files from directory
std::vector<std::string> MultiUploader::collectFiles(const std::string& path, bool recursive) {
    std::vector<std::string> files;

    try {
        if (fs::is_regular_file(path)) {
            files.push_back(path);
        } else if (fs::is_directory(path)) {
            if (recursive) {
                for (const auto& entry : fs::recursive_directory_iterator(path)) {
                    if (entry.is_regular_file()) {
                        files.push_back(entry.path().string());
                    }
                }
            } else {
                for (const auto& entry : fs::directory_iterator(path)) {
                    if (entry.is_regular_file()) {
                        files.push_back(entry.path().string());
                    }
                }
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Error collecting files: " << e.what() << std::endl;
    }

    return files;
}

// Start upload task
bool MultiUploader::startTask(const std::string& taskId, int maxConcurrent) {
    auto it = m_tasks.find(taskId);
    if (it == m_tasks.end()) {
        return false;
    }

    auto& task = it->second;

    if (task->state != UploadTaskImpl::PENDING &&
        task->state != UploadTaskImpl::PAUSED) {
        return false;
    }

    task->maxConcurrent = maxConcurrent;
    task->state = UploadTaskImpl::RUNNING;
    task->isPaused = false;
    task->shouldStop = false;
    task->startTime = std::chrono::steady_clock::now();
    task->report.startTime = std::chrono::system_clock::now();

    // Join any previous thread if exists
    if (task->workerThread.joinable()) {
        task->workerThread.join();
    }

    // Start processing in a separate thread - store handle for proper cleanup
    task->workerThread = std::thread([this, taskId]() {
        executeUploadTask(m_tasks[taskId].get());
    });

    m_activeTasks++;
    m_stats.totalTasks++;

    return true;
}

// Execute upload task
void MultiUploader::executeUploadTask(UploadTaskImpl* task) {
    while (!task->uploadQueue.empty() && !task->shouldStop) {
        // Check if paused - use CV instead of polling
        {
            std::unique_lock<std::mutex> lock(task->cvMutex);
            task->stateCV.wait(lock, [task] {
                return !task->isPaused || task->shouldStop;
            });
        }

        // Wait if too many concurrent uploads - use CV instead of polling
        {
            std::unique_lock<std::mutex> lock(task->cvMutex);
            task->stateCV.wait(lock, [task] {
                return task->activeUploads < task->maxConcurrent || task->shouldStop;
            });
        }

        if (task->shouldStop) break;

        // Get next file to upload
        auto [filePath, destIndex] = task->uploadQueue.front();
        task->uploadQueue.pop();

        // Get destination
        if (destIndex >= task->config.destinations.size()) {
            continue;
        }

        const auto& destination = task->config.destinations[destIndex];

        // Ensure destination exists
        mega::MegaNode* destNode = ensureDestinationExists(destination);
        if (!destNode) {
            FileUploadResult result;
            result.fileName = fs::path(filePath).filename().string();
            result.localPath = filePath;
            result.destination = destination.remotePath;
            result.success = false;
            result.errorMessage = "Failed to create destination folder";

            handleUploadCompletion(task->config.taskId, result);
            continue;
        }

        // Check for duplicates
        if (task->config.skipDuplicates && isDuplicate(filePath, destNode)) {
            FileUploadResult result;
            result.fileName = fs::path(filePath).filename().string();
            result.localPath = filePath;
            result.destination = destination.remotePath;
            result.success = true;
            result.skipped = true;
            result.errorMessage = "File already exists";

            task->progress.skippedFiles++;
            handleUploadCompletion(task->config.taskId, result);
            delete destNode;
            continue;
        }

        // Apply name pattern if specified
        std::string uploadName = fs::path(filePath).filename().string();
        if (destination.namePattern.has_value()) {
            uploadName = applyNamePattern(uploadName, destination.namePattern.value());
        }

        // Start upload
        task->activeUploads++;
        task->progress.currentFile = filePath;
        task->progress.currentDestination = destination.remotePath;

        auto startTime = std::chrono::steady_clock::now();

        int handle = m_listener->getNextHandle();
        m_megaApi->startUpload(filePath.c_str(), destNode, uploadName.c_str(),
                               0, nullptr, false, false, nullptr, m_listener.get());

        // Wait for completion - use CV with timeout for progress updates
        {
            std::unique_lock<std::mutex> lock(m_listener->completionMutex);
            while (!m_listener->results[handle].completed && !task->shouldStop) {
                // Wait with timeout to allow periodic progress updates
                m_listener->completionCV.wait_for(lock, std::chrono::milliseconds(500));

                // Update progress
                if (m_progressCallback) {
                    task->progress.completedFiles = task->report.successfulUploads;
                    task->progress.failedFiles = task->report.failedUploads;
                    task->progress.overallProgress =
                        (task->progress.completedFiles * 100.0) / task->progress.totalFiles;

                    m_progressCallback(task->progress);
                }
            }
        }

        auto endTime = std::chrono::steady_clock::now();
        auto uploadTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime);

        // Process result
        FileUploadResult result;
        result.fileName = uploadName;
        result.localPath = filePath;
        result.destination = destination.remotePath;
        result.success = m_listener->results[handle].success;
        result.skipped = false;
        result.errorMessage = m_listener->results[handle].errorMessage;
        result.fileSize = fs::file_size(filePath);
        result.uploadTime = uploadTime;

        if (result.success) {
            task->progress.completedFiles++;
            task->progress.uploadedBytes += result.fileSize;
            task->report.successfulUploads++;
            task->report.totalBytesUploaded += result.fileSize;

            // Update statistics
            m_stats.totalBytesUploaded += result.fileSize;
            m_stats.totalFilesUploaded++;

            // Delete local file if requested
            if (task->config.deleteAfterUpload) {
                try {
                    fs::remove(filePath);
                } catch (const fs::filesystem_error& e) {
                    std::cerr << "Failed to delete file: " << e.what() << std::endl;
                }
            }
        } else {
            task->progress.failedFiles++;
            task->report.failedUploads++;
        }

        task->activeUploads--;
        task->notifyStateChange();  // Wake up threads waiting for upload slots
        handleUploadCompletion(task->config.taskId, result);

        delete destNode;
    }

    // Task completed
    task->state = task->shouldStop ? UploadTaskImpl::CANCELLED : UploadTaskImpl::COMPLETED;
    task->report.endTime = std::chrono::system_clock::now();

    if (m_completionCallback) {
        m_completionCallback(task->report);
    }

    m_activeTasks--;

    if (task->state == UploadTaskImpl::COMPLETED) {
        m_stats.successfulTasks++;
    } else {
        m_stats.failedTasks++;
    }
}

// Ensure destination exists
mega::MegaNode* MultiUploader::ensureDestinationExists(const UploadDestination& destination) {
    mega::MegaNode* node = m_megaApi->getNodeByPath(destination.remotePath.c_str());

    if (!node && destination.createIfMissing) {
        // Create folder hierarchy
        std::vector<std::string> parts;
        std::stringstream ss(destination.remotePath);
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

// Check if file is duplicate
bool MultiUploader::isDuplicate(const std::string& localFile, mega::MegaNode* remoteFolder) {
    std::string fileName = fs::path(localFile).filename().string();
    mega::MegaNodeList* children = m_megaApi->getChildren(remoteFolder);

    if (!children) {
        return false;
    }

    bool isDup = false;
    for (int i = 0; i < children->size(); i++) {
        mega::MegaNode* child = children->get(i);
        if (child->getName() && fileName == child->getName()) {
            // Check size to confirm it's the same file
            if (child->getSize() == fs::file_size(localFile)) {
                isDup = true;
                break;
            }
        }
    }

    delete children;
    return isDup;
}

// Apply name pattern to file
std::string MultiUploader::applyNamePattern(const std::string& fileName,
                                           const std::string& pattern) {
    std::string result = pattern;

    // Replace placeholders
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);

    // {name} - original file name without extension
    size_t dotPos = fileName.find_last_of('.');
    std::string nameWithoutExt = fileName;
    std::string extension = "";

    if (dotPos != std::string::npos && dotPos > 0) {
        nameWithoutExt = fileName.substr(0, dotPos);
        extension = fileName.substr(dotPos);
    }

    // Replace placeholders
    size_t pos = 0;
    while ((pos = result.find("{name}", pos)) != std::string::npos) {
        result.replace(pos, 6, nameWithoutExt);
        pos += nameWithoutExt.length();
    }

    pos = 0;
    while ((pos = result.find("{ext}", pos)) != std::string::npos) {
        result.replace(pos, 5, extension);
        pos += extension.length();
    }

    // Date/time placeholders
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y%m%d");
    std::string dateStr = ss.str();

    pos = 0;
    while ((pos = result.find("{date}", pos)) != std::string::npos) {
        result.replace(pos, 6, dateStr);
        pos += dateStr.length();
    }

    ss.str("");
    ss << std::put_time(std::localtime(&time), "%H%M%S");
    std::string timeStr = ss.str();

    pos = 0;
    while ((pos = result.find("{time}", pos)) != std::string::npos) {
        result.replace(pos, 6, timeStr);
        pos += timeStr.length();
    }

    // Random number placeholder
    pos = 0;
    while ((pos = result.find("{rand}", pos)) != std::string::npos) {
        int randNum = rand() % 10000;
        std::string randStr = std::to_string(randNum);
        result.replace(pos, 6, randStr);
        pos += randStr.length();
    }

    return result;
}

// Handle upload completion
void MultiUploader::handleUploadCompletion(const std::string& taskId,
                                          const FileUploadResult& result) {
    auto it = m_tasks.find(taskId);
    if (it == m_tasks.end()) {
        return;
    }

    auto& task = it->second;
    task->report.results.push_back(result);

    // Update destination counts
    task->report.destinationCounts[result.destination]++;

    // Update progress
    if (m_progressCallback) {
        task->progress.completedFiles = task->report.successfulUploads +
                                       task->report.failedUploads +
                                       task->report.skippedFiles;
        task->progress.overallProgress =
            (task->progress.completedFiles * 100.0) / task->progress.totalFiles;

        // Calculate average speed
        auto elapsed = std::chrono::steady_clock::now() - task->startTime;
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        if (seconds > 0) {
            task->progress.averageSpeed = task->progress.uploadedBytes / seconds;

            // Estimate remaining time
            if (task->progress.averageSpeed > 0) {
                long long remainingBytes = task->progress.totalBytes - task->progress.uploadedBytes;
                task->progress.estimatedTimeRemaining =
                    std::chrono::seconds(static_cast<long>(remainingBytes / task->progress.averageSpeed));
            }
        }

        m_progressCallback(task->progress);
    }
}

// Pause task
bool MultiUploader::pauseTask(const std::string& taskId) {
    auto it = m_tasks.find(taskId);
    if (it == m_tasks.end()) {
        return false;
    }

    auto& task = it->second;
    if (task->state != UploadTaskImpl::RUNNING) {
        return false;
    }

    task->isPaused = true;
    task->state = UploadTaskImpl::PAUSED;
    task->notifyStateChange();  // Wake up paused threads
    return true;
}

// Resume task
bool MultiUploader::resumeTask(const std::string& taskId) {
    auto it = m_tasks.find(taskId);
    if (it == m_tasks.end()) {
        return false;
    }

    auto& task = it->second;
    if (task->state != UploadTaskImpl::PAUSED) {
        return false;
    }

    task->isPaused = false;
    task->state = UploadTaskImpl::RUNNING;
    task->notifyStateChange();  // Wake up waiting threads
    return true;
}

// Cancel task
bool MultiUploader::cancelTask(const std::string& taskId, bool deletePartial) {
    auto it = m_tasks.find(taskId);
    if (it == m_tasks.end()) {
        return false;
    }

    auto& task = it->second;
    task->shouldStop = true;
    task->state = UploadTaskImpl::CANCELLED;
    task->notifyStateChange();  // Wake up any waiting threads to exit

    if (deletePartial) {
        // TODO: Implement deletion of partially uploaded files
        // This would require tracking which files were partially uploaded
    }

    return true;
}

// Get task progress
std::optional<BulkUploadProgress> MultiUploader::getTaskProgress(const std::string& taskId) {
    auto it = m_tasks.find(taskId);
    if (it == m_tasks.end()) {
        return std::nullopt;
    }

    return it->second->progress;
}

// Get active tasks
std::vector<std::string> MultiUploader::getActiveTasks() const {
    std::vector<std::string> activeTasks;

    for (const auto& [taskId, task] : m_tasks) {
        if (task->state == UploadTaskImpl::RUNNING ||
            task->state == UploadTaskImpl::PAUSED) {
            activeTasks.push_back(taskId);
        }
    }

    return activeTasks;
}

// Get task report
std::optional<BulkUploadReport> MultiUploader::getTaskReport(const std::string& taskId) {
    auto it = m_tasks.find(taskId);
    if (it == m_tasks.end()) {
        return std::nullopt;
    }

    if (it->second->state != UploadTaskImpl::COMPLETED &&
        it->second->state != UploadTaskImpl::CANCELLED) {
        return std::nullopt;
    }

    return it->second->report;
}

// Schedule task
std::string MultiUploader::scheduleTask(
    const BulkUploadTask& task,
    std::chrono::system_clock::time_point scheduleTime) {

    std::string taskId = createUploadTask(task);
    auto& taskImpl = m_tasks[taskId];
    taskImpl->scheduleTime = scheduleTime;

    m_scheduledTasks.push(taskId);

    // Start scheduler thread if not already running
    if (!m_schedulerRunning) {
        m_schedulerRunning = true;

        // Join any previous scheduler thread if exists
        if (m_schedulerThread.joinable()) {
            m_schedulerThread.join();
        }

        // Start scheduler thread - store handle for proper cleanup
        m_schedulerThread = std::thread([this]() {
            processScheduledTasks();
        });
    }

    return taskId;
}

// Process scheduled tasks
void MultiUploader::processScheduledTasks() {
    while (m_schedulerRunning && !m_scheduledTasks.empty()) {
        std::string taskId = m_scheduledTasks.front();

        auto it = m_tasks.find(taskId);
        if (it == m_tasks.end()) {
            m_scheduledTasks.pop();
            continue;
        }

        auto now = std::chrono::system_clock::now();
        if (now >= it->second->scheduleTime) {
            m_scheduledTasks.pop();
            startTask(taskId, it->second->maxConcurrent);
        } else {
            // Wait until schedule time (with periodic check for shutdown)
            while (m_schedulerRunning && std::chrono::system_clock::now() < it->second->scheduleTime) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
    m_schedulerRunning = false;
}

// Add distribution rule
void MultiUploader::addDistributionRule(const std::string& name, const DistributionRule& rule) {
    m_predefinedRules[name] = rule;
}

// Get predefined rules
std::map<std::string, DistributionRule> MultiUploader::getPredefinedRules() const {
    return m_predefinedRules;
}

// Analyze distribution
std::map<std::string, int> MultiUploader::analyzeDistribution(
    const std::vector<std::string>& files,
    const std::vector<UploadDestination>& destinations) {

    std::map<std::string, int> distribution;

    // Initialize counters
    for (size_t i = 0; i < destinations.size(); i++) {
        distribution[destinations[i].remotePath] = 0;
    }

    // Count files per destination based on current rules
    for (const auto& file : files) {
        // Use default rules for analysis
        std::vector<DistributionRule> defaultRules;
        for (const auto& [name, rule] : m_predefinedRules) {
            defaultRules.push_back(rule);
        }

        int destIndex = selectDestination(file, defaultRules);
        if (destIndex >= 0 && destIndex < destinations.size()) {
            distribution[destinations[destIndex].remotePath]++;
        }
    }

    return distribution;
}

// Verify destinations
std::map<std::string, bool> MultiUploader::verifyDestinations(
    const std::vector<UploadDestination>& destinations) {

    std::map<std::string, bool> results;

    for (const auto& dest : destinations) {
        mega::MegaNode* node = m_megaApi->getNodeByPath(dest.remotePath.c_str());
        results[dest.remotePath] = (node != nullptr);
        if (node) {
            delete node;
        }
    }

    return results;
}

// Create destinations
bool MultiUploader::createDestinations(const std::vector<UploadDestination>& destinations) {
    bool allCreated = true;

    for (const auto& dest : destinations) {
        if (!dest.createIfMissing) {
            continue;
        }

        mega::MegaNode* node = ensureDestinationExists(dest);
        if (!node) {
            allCreated = false;
            std::cerr << "Failed to create destination: " << dest.remotePath << std::endl;
        } else {
            delete node;
        }
    }

    return allCreated;
}

// Check duplicates
std::map<std::string, bool> MultiUploader::checkDuplicates(
    const std::vector<std::string>& files,
    const std::string& destination) {

    std::map<std::string, bool> duplicates;

    mega::MegaNode* destNode = m_megaApi->getNodeByPath(destination.c_str());
    if (!destNode) {
        return duplicates;
    }

    for (const auto& file : files) {
        duplicates[file] = isDuplicate(file, destNode);
    }

    delete destNode;
    return duplicates;
}

// Set bandwidth limit
void MultiUploader::setBandwidthLimit(int bytesPerSecond) {
    m_bandwidthLimit = bytesPerSecond;

    if (m_megaApi) {
        // Note: Actual bandwidth limiting would require SDK support
        // This is a placeholder for the interface
    }
}

// Set file filter
void MultiUploader::setFileFilter(std::function<bool(const std::string&)> filter) {
    m_fileFilter = filter;
}

// Set progress callback
void MultiUploader::setProgressCallback(
    std::function<void(const BulkUploadProgress&)> callback) {
    m_progressCallback = callback;
}

// Set completion callback
void MultiUploader::setCompletionCallback(
    std::function<void(const BulkUploadReport&)> callback) {
    m_completionCallback = callback;
}

// Set error callback
void MultiUploader::setErrorCallback(
    std::function<void(const std::string&, const std::string&)> callback) {
    m_errorCallback = callback;
}

// Export task configuration
bool MultiUploader::exportTaskConfig(const std::string& taskId, const std::string& filePath) {
    auto it = m_tasks.find(taskId);
    if (it == m_tasks.end()) {
        return false;
    }

    try {
        json j;
        const auto& config = it->second->config;

        j["taskId"] = config.taskId;
        j["localPath"] = config.localPath;
        j["recursive"] = config.recursive;
        j["skipDuplicates"] = config.skipDuplicates;
        j["deleteAfterUpload"] = config.deleteAfterUpload;
        j["maxRetries"] = config.maxRetries;
        j["priority"] = config.priority;

        #ifdef HAS_NLOHMANN_JSON
            // Export destinations with full nlohmann/json support
            json destArray = json::array();
            for (const auto& dest : config.destinations) {
                json destObj;
                destObj["remotePath"] = dest.remotePath;
                if (dest.namePattern.has_value()) {
                    destObj["namePattern"] = dest.namePattern.value();
                }
                destObj["createIfMissing"] = dest.createIfMissing;
                destObj["priority"] = dest.priority;

                json tagsArray = json::array();
                for (const auto& tag : dest.tags) {
                    tagsArray.push_back(tag);
                }
                destObj["tags"] = tagsArray;

                destArray.push_back(destObj);
            }
            j["destinations"] = destArray;

            // Export rules
            json rulesArray = json::array();
            for (const auto& rule : config.rules) {
                json ruleObj;
                ruleObj["type"] = static_cast<int>(rule.type);
                ruleObj["destinationIndex"] = rule.destinationIndex;

                if (!rule.extensions.empty()) {
                    json extArray = json::array();
                    for (const auto& ext : rule.extensions) {
                        extArray.push_back(ext);
                    }
                    ruleObj["extensions"] = extArray;
                }

                if (rule.sizeThreshold > 0) {
                    ruleObj["sizeThreshold"] = rule.sizeThreshold;
                }

                if (!rule.regexPattern.empty()) {
                    ruleObj["regexPattern"] = rule.regexPattern;
                }

                rulesArray.push_back(ruleObj);
            }
            j["rules"] = rulesArray;
        #else
            // Simplified export for json_simple
            j["taskId"] = config.taskId;
            j["localPath"] = config.localPath;
            j["recursive"] = config.recursive;
            j["skipDuplicates"] = config.skipDuplicates;
            j["deleteAfterUpload"] = config.deleteAfterUpload;
            j["maxRetries"] = config.maxRetries;
            j["priority"] = config.priority;
            // Can't properly export complex nested structures with json_simple
            j["destinationsCount"] = static_cast<int>(config.destinations.size());
            j["rulesCount"] = static_cast<int>(config.rules.size());
        #endif

        // Write to file
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
        std::cerr << "Failed to export task config: " << e.what() << std::endl;
        return false;
    }
}

// Import task configuration
std::string MultiUploader::importTaskConfig(const std::string& filePath) {
    try {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            return "";
        }

        json j;
        file >> j;
        file.close();

        BulkUploadTask task;

        #ifdef HAS_NLOHMANN_JSON
            task.taskId = j["taskId"].get<std::string>();
            task.localPath = j["localPath"].get<std::string>();
            task.recursive = j["recursive"].get<bool>();
            task.skipDuplicates = j["skipDuplicates"].get<bool>();
            task.deleteAfterUpload = j["deleteAfterUpload"].get<bool>();
            task.maxRetries = j["maxRetries"].get<int>();
            task.priority = j["priority"].get<int>();
        #else
            // json_simple doesn't support .get<T>() properly
            // This is a limitation, returning empty for now
            std::cerr << "Task import not fully supported with json_simple" << std::endl;
            return "";
        #endif

        // Import destinations
        for (const auto& destObj : j["destinations"]) {
            UploadDestination dest;

            #ifdef HAS_NLOHMANN_JSON
                dest.remotePath = destObj["remotePath"].get<std::string>();

                if (destObj.contains("namePattern")) {
                    dest.namePattern = destObj["namePattern"].get<std::string>();
                }

                dest.createIfMissing = destObj["createIfMissing"].get<bool>();
                dest.priority = destObj["priority"].get<int>();

                for (const auto& tag : destObj["tags"]) {
                    dest.tags.push_back(tag.get<std::string>());
                }
            #endif

            task.destinations.push_back(dest);
        }

        // Import rules
        for (const auto& ruleObj : j["rules"]) {
            DistributionRule rule;

            #ifdef HAS_NLOHMANN_JSON
                rule.type = static_cast<DistributionRule::RuleType>(
                    ruleObj["type"].get<int>());
                rule.destinationIndex = ruleObj["destinationIndex"].get<int>();

                if (ruleObj.contains("extensions")) {
                    for (const auto& ext : ruleObj["extensions"]) {
                        rule.extensions.push_back(ext.get<std::string>());
                    }
                }

                if (ruleObj.contains("sizeThreshold")) {
                    rule.sizeThreshold = ruleObj["sizeThreshold"].get<long long>();
                }

                if (ruleObj.contains("regexPattern")) {
                    rule.regexPattern = ruleObj["regexPattern"].get<std::string>();
                }
            #endif

            task.rules.push_back(rule);
        }

        return createUploadTask(task);

    } catch (const std::exception& e) {
        std::cerr << "Failed to import task config: " << e.what() << std::endl;
        return "";
    }
}

// Get statistics
std::string MultiUploader::getStatistics() const {
    try {
        json j;

        j["totalBytesUploaded"] = std::to_string(m_stats.totalBytesUploaded);
        j["totalFilesUploaded"] = std::to_string(m_stats.totalFilesUploaded);
        j["totalTasks"] = std::to_string(m_stats.totalTasks);
        j["successfulTasks"] = std::to_string(m_stats.successfulTasks);
        j["failedTasks"] = std::to_string(m_stats.failedTasks);
        j["activeTasks"] = std::to_string(m_activeTasks.load());

        // Calculate uptime
        auto now = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
            now - m_stats.startTime).count();
        j["uptimeSeconds"] = std::to_string(uptime);

        // Calculate average speed
        if (uptime > 0) {
            j["averageBytesPerSecond"] = std::to_string(m_stats.totalBytesUploaded / uptime);
        }

        #ifdef HAS_NLOHMANN_JSON
            return j.dump(4);
        #else
            return j.dump();
        #endif

    } catch (const std::exception& e) {
        return "{}";
    }
}

// Clear completed tasks
void MultiUploader::clearCompletedTasks(int olderThanHours) {
    auto now = std::chrono::system_clock::now();
    auto olderThan = std::chrono::hours(olderThanHours);

    for (auto it = m_tasks.begin(); it != m_tasks.end();) {
        if (it->second->state == UploadTaskImpl::COMPLETED ||
            it->second->state == UploadTaskImpl::CANCELLED ||
            it->second->state == UploadTaskImpl::FAILED) {

            auto age = now - it->second->report.endTime;
            if (age >= olderThan) {
                it = m_tasks.erase(it);
            } else {
                ++it;
            }
        } else {
            ++it;
        }
    }
}

// Calculate total size
long long MultiUploader::calculateTotalSize(const std::vector<std::string>& files) {
    long long totalSize = 0;

    for (const auto& file : files) {
        if (fs::exists(file) && fs::is_regular_file(file)) {
            totalSize += fs::file_size(file);
        }
    }

    return totalSize;
}

} // namespace MegaCustom