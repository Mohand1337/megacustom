/**
 * CloudCopier Implementation
 * Cloud-to-Cloud copy operations within the same MEGA account
 */

#include "features/CloudCopier.h"
#include "core/PathValidator.h"
#include <megaapi.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <random>
#include <iomanip>
#include <ctime>
#include <filesystem>
#include <set>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace fs = std::filesystem;

namespace {
// Cross-platform function to get user home directory
std::string getHomeDirectory() {
#ifdef _WIN32
    char* userProfile = std::getenv("USERPROFILE");
    if (userProfile) {
        return std::string(userProfile);
    }
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, path))) {
        return std::string(path);
    }
    return "C:\\Users\\Default";
#else
    const char* home = std::getenv("HOME");
    return home ? std::string(home) : "/tmp";
#endif
}
} // anonymous namespace

namespace MegaCustom {

// ============================================================================
// CopyListener - Internal listener for copy operations
// ============================================================================

class CloudCopier::CopyListener : public mega::MegaRequestListener {
public:
    CopyListener() : m_finished(false), m_success(false), m_errorCode(0) {}

    void onRequestFinish(mega::MegaApi* api, mega::MegaRequest* request,
                        mega::MegaError* error) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_success = (error->getErrorCode() == mega::MegaError::API_OK);
        m_errorCode = error->getErrorCode();
        m_errorString = error->getErrorString() ? error->getErrorString() : "";

        if (request->getNodeHandle()) {
            m_nodeHandle = request->getNodeHandle();
        }

        m_finished = true;
        m_cv.notify_all();
    }

    bool waitForCompletion(int timeoutSeconds = 60) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, std::chrono::seconds(timeoutSeconds),
                            [this] { return m_finished; });
    }

    bool isSuccess() const { return m_success; }
    int getErrorCode() const { return m_errorCode; }
    std::string getErrorString() const { return m_errorString; }
    mega::MegaHandle getNodeHandle() const { return m_nodeHandle; }

    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_finished = false;
        m_success = false;
        m_errorCode = 0;
        m_errorString.clear();
        m_nodeHandle = mega::INVALID_HANDLE;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_finished;
    bool m_success;
    int m_errorCode;
    std::string m_errorString;
    mega::MegaHandle m_nodeHandle = mega::INVALID_HANDLE;
};

// ============================================================================
// CopyTaskImpl - Internal task implementation
// ============================================================================

struct CloudCopier::CopyTaskImpl {
    std::string taskId;
    std::vector<std::string> sourcePaths;
    std::vector<CopyDestination> destinations;
    CopyProgress progress;
    CopyReport report;
    ConflictResolution defaultResolution = ConflictResolution::ASK;

    enum class TaskState {
        PENDING,
        RUNNING,
        PAUSED,
        COMPLETED,
        CANCELLED,
        FAILED
    } state = TaskState::PENDING;

    std::atomic<bool> shouldStop{false};
    std::atomic<bool> isPaused{false};

    // Thread for this task - stored to enable proper cleanup
    std::thread workerThread;

    // Destructor ensures thread is joined to prevent std::terminate
    ~CopyTaskImpl() {
        if (workerThread.joinable()) {
            shouldStop = true;  // Signal thread to stop
            workerThread.join();
        }
    }

    // For "apply to all" conflict resolution
    std::optional<ConflictResolution> applyToAllResolution;

    void reset() {
        progress.taskId = taskId;
        progress.totalItems = 0;
        progress.completedItems = 0;
        progress.failedItems = 0;
        progress.skippedItems = 0;
        progress.overallProgress = 0.0;
        progress.currentItem.clear();
        progress.currentDestination.clear();

        report.taskId = taskId;
        report.results.clear();
        report.totalCopies = 0;
        report.successfulCopies = 0;
        report.failedCopies = 0;
        report.skippedCopies = 0;
        report.destinationCounts.clear();

        shouldStop = false;
        isPaused = false;
        applyToAllResolution = std::nullopt;
    }
};

// ============================================================================
// Constructor / Destructor
// ============================================================================

CloudCopier::CloudCopier(mega::MegaApi* megaApi)
    : m_megaApi(megaApi),
      m_listener(std::make_unique<CopyListener>()),
      m_defaultResolution(ConflictResolution::ASK) {

    // Set default templates path
    std::string homeDir = getHomeDirectory();
    if (!homeDir.empty()) {
#ifdef _WIN32
        m_templatesPath = homeDir + "\\.config\\MegaCustom\\copy_templates.json";
#else
        m_templatesPath = homeDir + "/.config/MegaCustom/copy_templates.json";
#endif
    } else {
        m_templatesPath = "./copy_templates.json";
    }

    loadTemplates();
}

CloudCopier::~CloudCopier() {
    // Signal all active tasks to stop
    for (auto& [taskId, task] : m_tasks) {
        task->shouldStop = true;
    }

    // Wait for all worker threads to complete
    for (auto& [taskId, task] : m_tasks) {
        if (task->workerThread.joinable()) {
            task->workerThread.join();
        }
    }
}

// ============================================================================
// Single/Multi-destination Copy
// ============================================================================

CopyResult CloudCopier::copyTo(const std::string& sourcePath,
                               const std::string& destinationPath,
                               const std::optional<std::string>& newName) {
    CopyResult result;
    result.sourcePath = sourcePath;
    result.destinationPath = destinationPath;

    if (!m_megaApi) {
        result.success = false;
        result.errorMessage = "MEGA API not initialized";
        result.errorCode = -1;
        return result;
    }

    // Get source node
    std::unique_ptr<mega::MegaNode> sourceNode(getNodeByPath(sourcePath));
    if (!sourceNode) {
        result.success = false;
        result.errorMessage = "Source not found: " + sourcePath;
        result.errorCode = mega::MegaError::API_ENOENT;
        return result;
    }

    // Get/create destination folder
    std::unique_ptr<mega::MegaNode> destParent(ensureFolderExists(destinationPath));
    if (!destParent) {
        result.success = false;
        result.errorMessage = "Cannot access destination: " + destinationPath;
        result.errorCode = mega::MegaError::API_EFAILED;
        return result;
    }

    // Check for conflict
    std::string itemName = newName.value_or(sourceNode->getName() ? sourceNode->getName() : "");
    std::unique_ptr<mega::MegaNode> existingNode(
        m_megaApi->getChildNode(destParent.get(), itemName.c_str())
    );

    if (existingNode) {
        // Conflict exists - handle based on default resolution or callback
        CopyConflict conflict;
        conflict.sourcePath = sourcePath;
        conflict.destinationPath = destinationPath;
        conflict.existingName = itemName;
        conflict.existingSize = existingNode->getSize();
        conflict.sourceSize = sourceNode->getSize();
        conflict.isFolder = sourceNode->isFolder();

        ConflictResolution resolution = resolveConflict(conflict);

        switch (resolution) {
            case ConflictResolution::SKIP:
            case ConflictResolution::SKIP_ALL:
                result.success = true;
                result.skipped = true;
                result.errorMessage = "Skipped - item already exists";
                return result;

            case ConflictResolution::OVERWRITE:
            case ConflictResolution::OVERWRITE_ALL:
                // Delete existing node first
                m_listener->reset();
                m_megaApi->remove(existingNode.get(), m_listener.get());
                if (!m_listener->waitForCompletion(30)) {
                    result.success = false;
                    result.errorMessage = "Timeout removing existing item";
                    result.errorCode = mega::MegaError::API_EFAILED;
                    return result;
                }
                // Check if delete actually succeeded (not just didn't timeout)
                if (!m_listener->isSuccess()) {
                    result.success = false;
                    result.errorMessage = "Failed to delete existing item: " + m_listener->getErrorString();
                    result.errorCode = m_listener->getErrorCode();
                    return result;
                }
                break;

            case ConflictResolution::RENAME:
                itemName = generateRenamedName(itemName);
                break;

            case ConflictResolution::CANCEL:
                result.success = false;
                result.errorMessage = "Operation cancelled by user";
                result.errorCode = -2;
                return result;

            case ConflictResolution::ASK:
                // If callback not set, skip by default
                result.success = true;
                result.skipped = true;
                result.errorMessage = "Skipped - no conflict handler";
                return result;
        }
    }

    // Perform the copy
    return performCopy(sourceNode.get(), destParent.get(),
                       newName.has_value() || existingNode ? std::optional<std::string>(itemName) : std::nullopt);
}

std::string CloudCopier::copyToMultiple(const std::string& sourcePath,
                                        const std::vector<CopyDestination>& destinations) {
    std::string taskId = generateTaskId();

    auto task = std::make_unique<CopyTaskImpl>();
    task->taskId = taskId;
    task->sourcePaths.push_back(sourcePath);
    task->destinations = destinations;
    task->defaultResolution = m_defaultResolution;
    task->reset();

    // Calculate total items (source × destinations)
    task->progress.totalItems = static_cast<int>(destinations.size());
    task->report.totalCopies = task->progress.totalItems;

    {
        std::lock_guard<std::mutex> lock(m_tasksMutex);
        m_tasks[taskId] = std::move(task);
    }

    return taskId;
}

// ============================================================================
// Single/Multi-destination Move
// ============================================================================

CopyResult CloudCopier::moveTo(const std::string& sourcePath,
                               const std::string& destinationPath,
                               const std::optional<std::string>& newName) {
    CopyResult result;
    result.sourcePath = sourcePath;
    result.destinationPath = destinationPath;

    if (!m_megaApi) {
        result.success = false;
        result.errorMessage = "MEGA API not initialized";
        result.errorCode = -1;
        return result;
    }

    // Get source node
    std::unique_ptr<mega::MegaNode> sourceNode(getNodeByPath(sourcePath));
    if (!sourceNode) {
        result.success = false;
        result.errorMessage = "Source not found: " + sourcePath;
        result.errorCode = mega::MegaError::API_ENOENT;
        return result;
    }

    // Get/create destination folder
    std::unique_ptr<mega::MegaNode> destParent(ensureFolderExists(destinationPath));
    if (!destParent) {
        result.success = false;
        result.errorMessage = "Cannot access destination: " + destinationPath;
        result.errorCode = mega::MegaError::API_EFAILED;
        return result;
    }

    // Check for conflict
    std::string itemName = newName.value_or(sourceNode->getName() ? sourceNode->getName() : "");
    std::unique_ptr<mega::MegaNode> existingNode(
        m_megaApi->getChildNode(destParent.get(), itemName.c_str())
    );

    if (existingNode) {
        // Conflict exists - handle based on default resolution or callback
        CopyConflict conflict;
        conflict.sourcePath = sourcePath;
        conflict.destinationPath = destinationPath;
        conflict.existingName = itemName;
        conflict.existingSize = existingNode->getSize();
        conflict.sourceSize = sourceNode->getSize();
        conflict.isFolder = sourceNode->isFolder();

        ConflictResolution resolution = resolveConflict(conflict);

        switch (resolution) {
            case ConflictResolution::SKIP:
            case ConflictResolution::SKIP_ALL:
                result.success = true;
                result.skipped = true;
                result.errorMessage = "Skipped - item already exists";
                return result;

            case ConflictResolution::OVERWRITE:
            case ConflictResolution::OVERWRITE_ALL:
                // Delete existing node first
                m_listener->reset();
                m_megaApi->remove(existingNode.get(), m_listener.get());
                if (!m_listener->waitForCompletion(30)) {
                    result.success = false;
                    result.errorMessage = "Timeout removing existing item";
                    result.errorCode = mega::MegaError::API_EFAILED;
                    return result;
                }
                // Check if delete actually succeeded (not just didn't timeout)
                if (!m_listener->isSuccess()) {
                    result.success = false;
                    result.errorMessage = "Failed to delete existing item: " + m_listener->getErrorString();
                    result.errorCode = m_listener->getErrorCode();
                    return result;
                }
                break;

            case ConflictResolution::RENAME:
                itemName = generateRenamedName(itemName);
                break;

            case ConflictResolution::CANCEL:
                result.success = false;
                result.errorMessage = "Operation cancelled by user";
                result.errorCode = -2;
                return result;

            case ConflictResolution::ASK:
                // If callback not set, skip by default
                result.success = true;
                result.skipped = true;
                result.errorMessage = "Skipped - no conflict handler";
                return result;
        }
    }

    // Perform the move
    return performMove(sourceNode.get(), destParent.get(),
                       newName.has_value() || existingNode ? std::optional<std::string>(itemName) : std::nullopt);
}

std::string CloudCopier::moveToMultiple(const std::string& sourcePath,
                                        const std::vector<CopyDestination>& destinations) {
    // For multi-destination move: move to first, then copy to the rest
    // This is because we can only move once (source is deleted)
    std::string taskId = generateTaskId();

    auto task = std::make_unique<CopyTaskImpl>();
    task->taskId = taskId;
    task->sourcePaths.push_back(sourcePath);
    task->destinations = destinations;
    task->defaultResolution = m_defaultResolution;
    task->reset();

    // Total items = 1 move + (N-1) copies
    task->progress.totalItems = static_cast<int>(destinations.size());
    task->report.totalCopies = task->progress.totalItems;

    {
        std::lock_guard<std::mutex> lock(m_tasksMutex);
        m_tasks[taskId] = std::move(task);
    }

    // Note: The executeCopyTask needs to handle move mode
    // For now, set operation mode to move - task executor will handle it
    m_operationMode = OperationMode::MOVE;

    return taskId;
}

// ============================================================================
// Bulk Copy
// ============================================================================

std::string CloudCopier::createBulkTask(const std::vector<CopyTask>& tasks) {
    std::string taskId = generateTaskId();

    auto taskImpl = std::make_unique<CopyTaskImpl>();
    taskImpl->taskId = taskId;
    taskImpl->defaultResolution = m_defaultResolution;
    taskImpl->reset();

    // Combine all sources and destinations
    for (const auto& task : tasks) {
        taskImpl->sourcePaths.push_back(task.sourcePath);
        for (const auto& dest : task.destinations) {
            taskImpl->destinations.push_back(dest);
        }
    }

    // Calculate total items
    int totalItems = 0;
    for (const auto& task : tasks) {
        totalItems += static_cast<int>(task.destinations.size());
    }
    taskImpl->progress.totalItems = totalItems;
    taskImpl->report.totalCopies = totalItems;

    {
        std::lock_guard<std::mutex> lock(m_tasksMutex);
        m_tasks[taskId] = std::move(taskImpl);
    }

    return taskId;
}

void CloudCopier::addSources(const std::string& taskId, const std::vector<std::string>& sourcePaths) {
    std::lock_guard<std::mutex> lock(m_tasksMutex);
    auto it = m_tasks.find(taskId);
    if (it != m_tasks.end() && it->second->state == CopyTaskImpl::TaskState::PENDING) {
        for (const auto& path : sourcePaths) {
            it->second->sourcePaths.push_back(path);
        }
        // Recalculate total
        it->second->progress.totalItems =
            static_cast<int>(it->second->sourcePaths.size() * it->second->destinations.size());
        it->second->report.totalCopies = it->second->progress.totalItems;
    }
}

void CloudCopier::addDestinations(const std::string& taskId, const std::vector<CopyDestination>& destinations) {
    std::lock_guard<std::mutex> lock(m_tasksMutex);
    auto it = m_tasks.find(taskId);
    if (it != m_tasks.end() && it->second->state == CopyTaskImpl::TaskState::PENDING) {
        for (const auto& dest : destinations) {
            it->second->destinations.push_back(dest);
        }
        // Recalculate total
        it->second->progress.totalItems =
            static_cast<int>(it->second->sourcePaths.size() * it->second->destinations.size());
        it->second->report.totalCopies = it->second->progress.totalItems;
    }
}

// ============================================================================
// Task Control
// ============================================================================

bool CloudCopier::startTask(const std::string& taskId) {
    CopyTaskImpl* task = nullptr;

    {
        std::lock_guard<std::mutex> lock(m_tasksMutex);
        auto it = m_tasks.find(taskId);
        if (it == m_tasks.end()) {
            return false;
        }
        if (it->second->state != CopyTaskImpl::TaskState::PENDING) {
            return false;
        }
        it->second->state = CopyTaskImpl::TaskState::RUNNING;
        it->second->report.startTime = std::chrono::system_clock::now();
        task = it->second.get();

        // Join any previous thread if exists
        if (task->workerThread.joinable()) {
            task->workerThread.join();
        }

        // Start processing in a separate thread - store handle for proper cleanup
        task->workerThread = std::thread([this, task]() {
            executeCopyTask(task);
        });
    }

    return true;
}

bool CloudCopier::pauseTask(const std::string& taskId) {
    std::lock_guard<std::mutex> lock(m_tasksMutex);
    auto it = m_tasks.find(taskId);
    if (it == m_tasks.end() || it->second->state != CopyTaskImpl::TaskState::RUNNING) {
        return false;
    }
    it->second->isPaused = true;
    it->second->state = CopyTaskImpl::TaskState::PAUSED;
    return true;
}

bool CloudCopier::resumeTask(const std::string& taskId) {
    std::lock_guard<std::mutex> lock(m_tasksMutex);
    auto it = m_tasks.find(taskId);
    if (it == m_tasks.end() || it->second->state != CopyTaskImpl::TaskState::PAUSED) {
        return false;
    }
    it->second->isPaused = false;
    it->second->state = CopyTaskImpl::TaskState::RUNNING;
    return true;
}

bool CloudCopier::cancelTask(const std::string& taskId) {
    std::lock_guard<std::mutex> lock(m_tasksMutex);
    auto it = m_tasks.find(taskId);
    if (it == m_tasks.end()) {
        return false;
    }
    it->second->shouldStop = true;
    it->second->state = CopyTaskImpl::TaskState::CANCELLED;
    return true;
}

// ============================================================================
// Task Status
// ============================================================================

std::optional<CopyProgress> CloudCopier::getTaskProgress(const std::string& taskId) {
    std::lock_guard<std::mutex> lock(m_tasksMutex);
    auto it = m_tasks.find(taskId);
    if (it == m_tasks.end()) {
        return std::nullopt;
    }
    return it->second->progress;
}

std::optional<CopyReport> CloudCopier::getTaskReport(const std::string& taskId) {
    std::lock_guard<std::mutex> lock(m_tasksMutex);
    auto it = m_tasks.find(taskId);
    if (it == m_tasks.end()) {
        return std::nullopt;
    }
    if (it->second->state != CopyTaskImpl::TaskState::COMPLETED &&
        it->second->state != CopyTaskImpl::TaskState::CANCELLED &&
        it->second->state != CopyTaskImpl::TaskState::FAILED) {
        return std::nullopt;
    }
    return it->second->report;
}

std::vector<std::string> CloudCopier::getActiveTasks() const {
    std::vector<std::string> active;
    std::lock_guard<std::mutex> lock(m_tasksMutex);
    for (const auto& [taskId, task] : m_tasks) {
        if (task->state == CopyTaskImpl::TaskState::RUNNING ||
            task->state == CopyTaskImpl::TaskState::PAUSED ||
            task->state == CopyTaskImpl::TaskState::PENDING) {
            active.push_back(taskId);
        }
    }
    return active;
}

void CloudCopier::clearCompletedTasks(int olderThanHours) {
    std::lock_guard<std::mutex> lock(m_tasksMutex);
    auto now = std::chrono::system_clock::now();

    for (auto it = m_tasks.begin(); it != m_tasks.end();) {
        if (it->second->state == CopyTaskImpl::TaskState::COMPLETED ||
            it->second->state == CopyTaskImpl::TaskState::CANCELLED ||
            it->second->state == CopyTaskImpl::TaskState::FAILED) {

            bool shouldErase = false;
            if (olderThanHours == 0) {
                shouldErase = true;
            } else {
                auto age = std::chrono::duration_cast<std::chrono::hours>(
                    now - it->second->report.endTime).count();
                shouldErase = (age >= olderThanHours);
            }

            if (shouldErase) {
                // Join thread before destruction to prevent std::terminate
                if (it->second->workerThread.joinable()) {
                    it->second->workerThread.join();
                }
                it = m_tasks.erase(it);
            } else {
                ++it;
            }
        } else {
            ++it;
        }
    }
}

// ============================================================================
// Conflict Handling
// ============================================================================

bool CloudCopier::checkConflict(const std::string& sourcePath, const std::string& destinationPath) {
    std::unique_ptr<mega::MegaNode> sourceNode(getNodeByPath(sourcePath));
    if (!sourceNode) {
        return false;
    }

    std::unique_ptr<mega::MegaNode> destParent(getNodeByPath(destinationPath));
    if (!destParent) {
        return false;
    }

    const char* name = sourceNode->getName();
    if (!name) {
        return false;
    }

    std::unique_ptr<mega::MegaNode> existing(
        m_megaApi->getChildNode(destParent.get(), name)
    );

    return existing != nullptr;
}

std::optional<CopyConflict> CloudCopier::getConflictInfo(const std::string& sourcePath,
                                                         const std::string& destinationPath) {
    std::unique_ptr<mega::MegaNode> sourceNode(getNodeByPath(sourcePath));
    if (!sourceNode) {
        return std::nullopt;
    }

    std::unique_ptr<mega::MegaNode> destParent(getNodeByPath(destinationPath));
    if (!destParent) {
        return std::nullopt;
    }

    const char* name = sourceNode->getName();
    if (!name) {
        return std::nullopt;
    }

    std::unique_ptr<mega::MegaNode> existing(
        m_megaApi->getChildNode(destParent.get(), name)
    );

    if (!existing) {
        return std::nullopt;
    }

    CopyConflict conflict;
    conflict.sourcePath = sourcePath;
    conflict.destinationPath = destinationPath;
    conflict.existingName = name;
    conflict.existingSize = existing->getSize();
    conflict.sourceSize = sourceNode->getSize();
    conflict.isFolder = sourceNode->isFolder();

    // Get modification times
    conflict.existingModTime = std::chrono::system_clock::from_time_t(existing->getModificationTime());
    conflict.sourceModTime = std::chrono::system_clock::from_time_t(sourceNode->getModificationTime());

    return conflict;
}

void CloudCopier::setConflictCallback(
    std::function<ConflictResolution(const CopyConflict&)> callback) {
    m_conflictCallback = callback;
}

void CloudCopier::setDefaultConflictResolution(ConflictResolution resolution) {
    m_defaultResolution = resolution;
}

// ============================================================================
// Template Management
// ============================================================================

bool CloudCopier::saveTemplate(const std::string& name, const std::vector<std::string>& destinations) {
    CopyTemplate tmpl;
    tmpl.name = name;
    tmpl.destinations = destinations;
    tmpl.created = std::chrono::system_clock::now();
    tmpl.lastUsed = tmpl.created;

    m_templates[name] = tmpl;
    saveTemplates();
    return true;
}

std::vector<std::string> CloudCopier::loadTemplate(const std::string& name) {
    auto it = m_templates.find(name);
    if (it == m_templates.end()) {
        return {};
    }
    it->second.lastUsed = std::chrono::system_clock::now();
    saveTemplates();
    return it->second.destinations;
}

std::map<std::string, CopyTemplate> CloudCopier::getTemplates() const {
    return m_templates;
}

bool CloudCopier::deleteTemplate(const std::string& name) {
    auto it = m_templates.find(name);
    if (it == m_templates.end()) {
        return false;
    }
    m_templates.erase(it);
    saveTemplates();
    return true;
}

std::vector<std::string> CloudCopier::importDestinationsFromFile(const std::string& filePath) {
    std::vector<std::string> destinations;
    std::ifstream file(filePath);

    if (!file.is_open()) {
        if (m_errorCallback) {
            m_errorCallback("", "Cannot open file: " + filePath);
        }
        return destinations;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        size_t end = line.find_last_not_of(" \t\r\n");

        if (start != std::string::npos && end != std::string::npos) {
            std::string path = line.substr(start, end - start + 1);
            if (!path.empty() && path[0] == '/') {
                destinations.push_back(path);
            }
        }
    }

    return destinations;
}

bool CloudCopier::exportDestinationsToFile(const std::vector<std::string>& destinations,
                                           const std::string& filePath) {
    std::ofstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    for (const auto& dest : destinations) {
        file << dest << "\n";
    }

    return true;
}

// ============================================================================
// Callbacks
// ============================================================================

void CloudCopier::setProgressCallback(std::function<void(const CopyProgress&)> callback) {
    m_progressCallback = callback;
}

void CloudCopier::setCompletionCallback(std::function<void(const CopyReport&)> callback) {
    m_completionCallback = callback;
}

void CloudCopier::setErrorCallback(
    std::function<void(const std::string& taskId, const std::string& error)> callback) {
    m_errorCallback = callback;
}

// ============================================================================
// Utility
// ============================================================================

std::string CloudCopier::packageFileIntoFolder(const std::string& sourceFilePath,
                                              const std::string& destParentPath) {
    if (!m_megaApi || sourceFilePath.empty() || destParentPath.empty()) {
        return "";
    }

    // Get source file
    std::unique_ptr<mega::MegaNode> sourceNode(getNodeByPath(sourceFilePath));
    if (!sourceNode || sourceNode->isFolder()) {
        std::cerr << "packageFileIntoFolder: Source not found or is a folder: " << sourceFilePath << std::endl;
        return "";
    }

    // Get filename and create folder name (remove extension)
    std::string fileName = sourceNode->getName() ? sourceNode->getName() : "";
    if (fileName.empty()) {
        return "";
    }

    std::string folderName = fileName;
    size_t dotPos = folderName.rfind('.');
    if (dotPos != std::string::npos && dotPos > 0) {
        folderName = folderName.substr(0, dotPos);
    }

    // Get/create destination parent folder
    std::unique_ptr<mega::MegaNode> destParent(ensureFolderExists(destParentPath));
    if (!destParent) {
        std::cerr << "packageFileIntoFolder: Cannot access destination: " << destParentPath << std::endl;
        return "";
    }

    // Create the new folder for this file
    std::string newFolderPath = destParentPath;
    if (newFolderPath.back() != '/') newFolderPath += "/";
    newFolderPath += folderName;

    std::unique_ptr<mega::MegaNode> newFolder(ensureFolderExists(newFolderPath));
    if (!newFolder) {
        std::cerr << "packageFileIntoFolder: Failed to create folder: " << newFolderPath << std::endl;
        return "";
    }

    // Copy the file into the new folder
    m_listener->reset();
    m_megaApi->copyNode(sourceNode.get(), newFolder.get(), m_listener.get());

    if (!m_listener->waitForCompletion(60)) {
        std::cerr << "packageFileIntoFolder: Copy timed out" << std::endl;
        return "";
    }

    if (!m_listener->isSuccess()) {
        std::cerr << "packageFileIntoFolder: Copy failed: " << m_listener->getErrorString() << std::endl;
        return "";
    }

    std::cout << "packageFileIntoFolder: Created " << newFolderPath << "/" << fileName << std::endl;
    return newFolderPath;
}

std::map<std::string, bool> CloudCopier::verifyDestinations(const std::vector<std::string>& destinations) {
    std::map<std::string, bool> results;
    for (const auto& dest : destinations) {
        std::unique_ptr<mega::MegaNode> node(getNodeByPath(dest));
        results[dest] = (node != nullptr && node->isFolder());
    }
    return results;
}

bool CloudCopier::createDestinations(const std::vector<std::string>& destinations) {
    for (const auto& dest : destinations) {
        mega::MegaNode* node = ensureFolderExists(dest);
        if (!node) {
            return false;
        }
        delete node;
    }
    return true;
}

mega::MegaNode* CloudCopier::getNodeByPath(const std::string& path) {
    if (!m_megaApi || path.empty()) {
        return nullptr;
    }
    return m_megaApi->getNodeByPath(path.c_str());
}

// ============================================================================
// Private Helper Methods
// ============================================================================

std::string CloudCopier::generateTaskId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static const char* hex = "0123456789abcdef";

    std::string uuid = "copy-";
    for (int i = 0; i < 8; ++i) {
        uuid += hex[dis(gen)];
    }
    uuid += "-";
    for (int i = 0; i < 4; ++i) {
        uuid += hex[dis(gen)];
    }

    return uuid;
}

mega::MegaNode* CloudCopier::ensureFolderExists(const std::string& path) {
    if (!m_megaApi || path.empty()) {
        return nullptr;
    }

    // Try to get existing node
    mega::MegaNode* node = m_megaApi->getNodeByPath(path.c_str());
    if (node) {
        return node;
    }

    // Split path and create folders
    std::vector<std::string> components;
    std::string current;
    for (char c : path) {
        if (c == '/') {
            if (!current.empty()) {
                components.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        components.push_back(current);
    }

    // Start from root
    std::unique_ptr<mega::MegaNode> currentNode(m_megaApi->getRootNode());
    if (!currentNode) {
        return nullptr;
    }

    for (const auto& component : components) {
        std::unique_ptr<mega::MegaNode> childNode(
            m_megaApi->getChildNode(currentNode.get(), component.c_str())
        );

        if (childNode && childNode->isFolder()) {
            currentNode = std::move(childNode);
        } else if (!childNode) {
            // Create folder
            m_listener->reset();
            m_megaApi->createFolder(component.c_str(), currentNode.get(), m_listener.get());

            if (!m_listener->waitForCompletion(30)) {
                return nullptr;
            }

            if (!m_listener->isSuccess()) {
                return nullptr;
            }

            // Get the newly created folder
            currentNode.reset(m_megaApi->getChildNode(currentNode.get(), component.c_str()));
            if (!currentNode) {
                return nullptr;
            }
        } else {
            // Component exists but is not a folder
            return nullptr;
        }
    }

    return currentNode.release();
}

CopyResult CloudCopier::performCopy(mega::MegaNode* sourceNode, mega::MegaNode* destParent,
                                    const std::optional<std::string>& newName) {
    CopyResult result;
    result.sourcePath = sourceNode->getName() ? sourceNode->getName() : "";
    result.success = false;

    m_listener->reset();

    if (newName.has_value()) {
        m_megaApi->copyNode(sourceNode, destParent, newName->c_str(), m_listener.get());
    } else {
        m_megaApi->copyNode(sourceNode, destParent, m_listener.get());
    }

    if (!m_listener->waitForCompletion(120)) {  // 2 minute timeout for large files
        result.errorMessage = "Timeout during copy operation";
        result.errorCode = mega::MegaError::API_EFAILED;
        return result;
    }

    result.success = m_listener->isSuccess();
    result.errorCode = m_listener->getErrorCode();
    result.errorMessage = result.success ? "" : m_listener->getErrorString();

    if (result.success) {
        // Convert handle to string
        std::stringstream ss;
        ss << m_listener->getNodeHandle();
        result.newNodeHandle = ss.str();
    }

    return result;
}

CopyResult CloudCopier::performMove(mega::MegaNode* sourceNode, mega::MegaNode* destParent,
                                    const std::optional<std::string>& newName) {
    CopyResult result;
    result.sourcePath = sourceNode->getName() ? sourceNode->getName() : "";
    result.success = false;

    m_listener->reset();

    // Move uses moveNode() - server-side operation, no bandwidth usage
    m_megaApi->moveNode(sourceNode, destParent, m_listener.get());

    if (!m_listener->waitForCompletion(60)) {  // 1 minute timeout (moves are fast)
        result.errorMessage = "Timeout during move operation";
        result.errorCode = mega::MegaError::API_EFAILED;
        return result;
    }

    if (!m_listener->isSuccess()) {
        result.success = false;
        result.errorCode = m_listener->getErrorCode();
        result.errorMessage = m_listener->getErrorString();
        return result;
    }

    // If rename is requested, do it after the move
    if (newName.has_value() && !newName->empty()) {
        // Get the moved node from destination
        std::unique_ptr<mega::MegaNode> movedNode(
            m_megaApi->getChildNode(destParent, sourceNode->getName())
        );

        if (movedNode && newName.value() != std::string(sourceNode->getName())) {
            m_listener->reset();
            m_megaApi->renameNode(movedNode.get(), newName->c_str(), m_listener.get());
            if (!m_listener->waitForCompletion(30)) {
                // Move succeeded but rename failed - report partial success
                result.success = true;
                result.errorMessage = "Moved but rename failed (timeout)";
                return result;
            }
            if (!m_listener->isSuccess()) {
                result.success = true;
                result.errorMessage = "Moved but rename failed: " + m_listener->getErrorString();
                return result;
            }
        }
    }

    result.success = true;
    result.errorCode = 0;
    result.errorMessage = "";

    // Convert handle to string
    std::stringstream ss;
    ss << m_listener->getNodeHandle();
    result.newNodeHandle = ss.str();

    return result;
}

ConflictResolution CloudCopier::resolveConflict(const CopyConflict& conflict) {
    // If callback is set, use it
    if (m_conflictCallback) {
        return m_conflictCallback(conflict);
    }

    // Otherwise use default
    return m_defaultResolution;
}

std::string CloudCopier::generateRenamedName(const std::string& originalName) {
    // Find extension
    size_t dotPos = originalName.rfind('.');
    std::string baseName, extension;

    if (dotPos != std::string::npos && dotPos > 0) {
        baseName = originalName.substr(0, dotPos);
        extension = originalName.substr(dotPos);
    } else {
        baseName = originalName;
    }

    // Add timestamp
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << baseName << " (copy " << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S") << ")" << extension;

    return ss.str();
}

void CloudCopier::executeCopyTask(CopyTaskImpl* task) {
    if (!task) return;

    task->report.startTime = std::chrono::system_clock::now();

    // Process each source to each destination
    for (const auto& sourcePath : task->sourcePaths) {
        if (task->shouldStop) break;

        // Get source node
        std::unique_ptr<mega::MegaNode> sourceNode(getNodeByPath(sourcePath));
        if (!sourceNode) {
            // Add failed result for all destinations
            for (const auto& dest : task->destinations) {
                CopyResult result;
                result.success = false;
                result.sourcePath = sourcePath;
                result.destinationPath = dest.remotePath;
                result.errorMessage = "Source not found";
                result.errorCode = mega::MegaError::API_ENOENT;
                task->report.results.push_back(result);
                task->progress.failedItems++;
                task->report.failedCopies++;
            }
            continue;
        }

        for (const auto& dest : task->destinations) {
            if (task->shouldStop) break;

            // Handle pause
            while (task->isPaused && !task->shouldStop) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            task->progress.currentItem = sourcePath;
            task->progress.currentDestination = dest.remotePath;

            // Get/create destination folder
            std::unique_ptr<mega::MegaNode> destParent(ensureFolderExists(dest.remotePath));
            if (!destParent) {
                CopyResult result;
                result.success = false;
                result.sourcePath = sourcePath;
                result.destinationPath = dest.remotePath;
                result.errorMessage = "Cannot access destination";
                result.errorCode = mega::MegaError::API_EFAILED;
                task->report.results.push_back(result);
                task->progress.failedItems++;
                task->report.failedCopies++;

                if (m_errorCallback) {
                    m_errorCallback(task->taskId, result.errorMessage);
                }
                continue;
            }

            // Check for conflict
            std::string itemName = dest.newName.value_or(sourceNode->getName() ? sourceNode->getName() : "");
            std::unique_ptr<mega::MegaNode> existingNode(
                m_megaApi->getChildNode(destParent.get(), itemName.c_str())
            );

            bool shouldCopy = true;
            std::optional<std::string> finalName = dest.newName;

            if (existingNode) {
                ConflictResolution resolution;

                // Check if we have an "apply to all" resolution
                if (task->applyToAllResolution.has_value()) {
                    resolution = task->applyToAllResolution.value();
                } else {
                    // Get conflict info
                    CopyConflict conflict;
                    conflict.sourcePath = sourcePath;
                    conflict.destinationPath = dest.remotePath;
                    conflict.existingName = itemName;
                    conflict.existingSize = existingNode->getSize();
                    conflict.sourceSize = sourceNode->getSize();
                    conflict.isFolder = sourceNode->isFolder();
                    conflict.existingModTime = std::chrono::system_clock::from_time_t(existingNode->getModificationTime());
                    conflict.sourceModTime = std::chrono::system_clock::from_time_t(sourceNode->getModificationTime());

                    resolution = resolveConflict(conflict);

                    // Handle "apply to all" options
                    if (resolution == ConflictResolution::SKIP_ALL) {
                        task->applyToAllResolution = ConflictResolution::SKIP;
                        resolution = ConflictResolution::SKIP;
                    } else if (resolution == ConflictResolution::OVERWRITE_ALL) {
                        task->applyToAllResolution = ConflictResolution::OVERWRITE;
                        resolution = ConflictResolution::OVERWRITE;
                    }
                }

                switch (resolution) {
                    case ConflictResolution::SKIP:
                    case ConflictResolution::SKIP_ALL:
                        shouldCopy = false;
                        {
                            CopyResult result;
                            result.success = true;
                            result.skipped = true;
                            result.sourcePath = sourcePath;
                            result.destinationPath = dest.remotePath;
                            result.errorMessage = "Skipped - item exists";
                            task->report.results.push_back(result);
                            task->progress.skippedItems++;
                            task->report.skippedCopies++;
                        }
                        break;

                    case ConflictResolution::OVERWRITE:
                    case ConflictResolution::OVERWRITE_ALL:
                        // Delete existing
                        m_listener->reset();
                        m_megaApi->remove(existingNode.get(), m_listener.get());
                        m_listener->waitForCompletion(30);
                        // Check if delete succeeded before proceeding
                        if (!m_listener->isSuccess()) {
                            shouldCopy = false;
                            CopyResult result;
                            result.success = false;
                            result.sourcePath = sourcePath;
                            result.destinationPath = dest.remotePath;
                            result.errorMessage = "Failed to delete existing file: " + m_listener->getErrorString();
                            task->report.results.push_back(result);
                            task->progress.failedItems++;
                            task->report.failedCopies++;
                        }
                        break;

                    case ConflictResolution::RENAME:
                        finalName = generateRenamedName(itemName);
                        break;

                    case ConflictResolution::CANCEL:
                        task->shouldStop = true;
                        shouldCopy = false;
                        break;

                    case ConflictResolution::ASK:
                    default:
                        shouldCopy = false;
                        {
                            CopyResult result;
                            result.success = true;
                            result.skipped = true;
                            result.sourcePath = sourcePath;
                            result.destinationPath = dest.remotePath;
                            result.errorMessage = "Skipped - conflict unresolved";
                            task->report.results.push_back(result);
                            task->progress.skippedItems++;
                            task->report.skippedCopies++;
                        }
                        break;
                }
            }

            if (shouldCopy && !task->shouldStop) {
                CopyResult result;

                // In MOVE mode: move to first destination, copy to rest
                // We need to track if this source has been moved already
                static thread_local std::set<std::string> movedSources;
                bool shouldMove = (m_operationMode == OperationMode::MOVE &&
                                   movedSources.find(sourcePath) == movedSources.end());

                if (shouldMove) {
                    // First destination for this source in move mode - actually move
                    result = performMove(sourceNode.get(), destParent.get(), finalName);
                    if (result.success && !result.skipped) {
                        movedSources.insert(sourcePath);
                        // Update sourceNode to point to the new location for subsequent copies
                        std::string newPath = dest.remotePath;
                        if (!newPath.empty() && newPath.back() != '/') newPath += "/";
                        newPath += finalName.value_or(sourceNode->getName() ? sourceNode->getName() : "");
                        sourceNode.reset(getNodeByPath(newPath));
                        // Check if node was found at new location
                        if (!sourceNode) {
                            result.success = false;
                            result.errorMessage = "Node not found at new location after move: " + newPath;
                            task->progress.failedItems++;
                            task->report.failedCopies++;
                            break;  // Exit destination loop - can't copy to remaining destinations
                        }
                    }
                } else {
                    // Either copy mode, or subsequent destination in move mode
                    result = performCopy(sourceNode.get(), destParent.get(), finalName);
                }

                result.sourcePath = sourcePath;
                result.destinationPath = dest.remotePath;

                task->report.results.push_back(result);

                if (result.success) {
                    task->progress.completedItems++;
                    task->report.successfulCopies++;
                    task->report.destinationCounts[dest.remotePath]++;
                } else {
                    task->progress.failedItems++;
                    task->report.failedCopies++;

                    if (m_errorCallback) {
                        m_errorCallback(task->taskId, result.errorMessage);
                    }
                }
            }

            // Update progress
            int processedItems = task->progress.completedItems +
                                task->progress.failedItems +
                                task->progress.skippedItems;
            if (task->progress.totalItems > 0) {
                task->progress.overallProgress =
                    (processedItems * 100.0) / task->progress.totalItems;
            }

            if (m_progressCallback) {
                m_progressCallback(task->progress);
            }
        }
    }

    // Task completed
    task->report.endTime = std::chrono::system_clock::now();

    if (task->shouldStop) {
        task->state = CopyTaskImpl::TaskState::CANCELLED;
    } else if (task->progress.failedItems > 0 && task->progress.completedItems == 0) {
        task->state = CopyTaskImpl::TaskState::FAILED;
    } else {
        task->state = CopyTaskImpl::TaskState::COMPLETED;
    }

    if (m_completionCallback) {
        m_completionCallback(task->report);
    }
}

void CloudCopier::loadTemplates() {
    std::ifstream file(m_templatesPath);
    if (!file.is_open()) {
        return;
    }

    try {
        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());

        // Simple JSON parsing (basic implementation)
        // For production, use a proper JSON library
        // This is a simplified version for demonstration

        // Look for template entries
        size_t pos = 0;
        while ((pos = content.find("\"name\":", pos)) != std::string::npos) {
            CopyTemplate tmpl;

            // Extract name
            size_t nameStart = content.find("\"", pos + 7) + 1;
            size_t nameEnd = content.find("\"", nameStart);
            if (nameStart != std::string::npos && nameEnd != std::string::npos) {
                tmpl.name = content.substr(nameStart, nameEnd - nameStart);
            }

            // Extract destinations array
            size_t destStart = content.find("\"destinations\":", pos);
            if (destStart != std::string::npos) {
                size_t arrayStart = content.find("[", destStart);
                size_t arrayEnd = content.find("]", arrayStart);

                if (arrayStart != std::string::npos && arrayEnd != std::string::npos) {
                    std::string arrayContent = content.substr(arrayStart + 1, arrayEnd - arrayStart - 1);

                    // Parse destination strings
                    size_t destPos = 0;
                    while ((destPos = arrayContent.find("\"", destPos)) != std::string::npos) {
                        size_t destEnd = arrayContent.find("\"", destPos + 1);
                        if (destEnd != std::string::npos) {
                            std::string dest = arrayContent.substr(destPos + 1, destEnd - destPos - 1);
                            if (!dest.empty() && dest[0] == '/') {
                                tmpl.destinations.push_back(dest);
                            }
                            destPos = destEnd + 1;
                        } else {
                            break;
                        }
                    }
                }
            }

            tmpl.created = std::chrono::system_clock::now();
            tmpl.lastUsed = tmpl.created;

            if (!tmpl.name.empty() && !tmpl.destinations.empty()) {
                m_templates[tmpl.name] = tmpl;
            }

            pos++;
        }
    } catch (...) {
        // Ignore parsing errors
    }
}

void CloudCopier::saveTemplates() {
    // Create directory if needed (safe, no shell injection)
    size_t lastSlash = m_templatesPath.rfind('/');
    if (lastSlash != std::string::npos) {
        std::string dir = m_templatesPath.substr(0, lastSlash);
        if (megacustom::PathValidator::isValidPath(dir)) {
            try {
                fs::create_directories(dir);
            } catch (const fs::filesystem_error& e) {
                std::cerr << "Failed to create templates directory: " << e.what() << std::endl;
                return;
            }
        } else {
            std::cerr << "Invalid templates directory path" << std::endl;
            return;
        }
    }

    std::ofstream file(m_templatesPath);
    if (!file.is_open()) {
        return;
    }

    file << "{\n  \"templates\": [\n";

    bool first = true;
    for (const auto& [name, tmpl] : m_templates) {
        if (!first) {
            file << ",\n";
        }
        first = false;

        file << "    {\n";
        file << "      \"name\": \"" << name << "\",\n";
        file << "      \"destinations\": [\n";

        bool firstDest = true;
        for (const auto& dest : tmpl.destinations) {
            if (!firstDest) {
                file << ",\n";
            }
            firstDest = false;
            file << "        \"" << dest << "\"";
        }

        file << "\n      ]\n";
        file << "    }";
    }

    file << "\n  ]\n}\n";
}

} // namespace MegaCustom
