#include "controllers/CloudCopierController.h"
#include "features/CloudCopier.h"
#include "accounts/AccountManager.h"
#include "utils/MemberRegistry.h"
#include "utils/TemplateExpander.h"
#include <megaapi.h>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>

namespace MegaCustom {

CloudCopierController::CloudCopierController(void* megaApi, QObject* parent)
    : QObject(parent)
    , m_megaApi(megaApi)
{
    // Create the CLI CloudCopier module
    m_cloudCopier = std::make_unique<CloudCopier>(static_cast<mega::MegaApi*>(megaApi));

    // Set up callbacks from CLI module
    // NOTE: These callbacks are called from background threads!
    m_cloudCopier->setProgressCallback([this](const CopyProgress& progress) {
        // Copy data we need before marshaling to main thread
        int completed = progress.completedItems;
        int total = progress.totalItems;
        QString currentItem = QString::fromStdString(progress.currentItem);
        QString currentDest = QString::fromStdString(progress.currentDestination);

        // Marshal to main thread
        QMetaObject::invokeMethod(this, [this, completed, total, currentItem, currentDest]() {
            emit copyProgress(completed, total, currentItem, currentDest);

            // Find and update the task that's currently being copied
            for (int i = 0; i < m_tasks.size(); ++i) {
                if (m_tasks[i].status == "Pending" || m_tasks[i].status == "Copying...") {
                    // Check if this task matches the current destination
                    if (m_tasks[i].destinationPath.contains(currentDest) ||
                        currentDest.contains(m_tasks[i].destinationPath)) {
                        if (m_tasks[i].status != "Copying...") {
                            m_tasks[i].status = "Copying...";
                            emit taskStatusChanged(m_tasks[i].taskId, "Copying...");
                        }
                        break;
                    }
                }
            }
        }, Qt::QueuedConnection);
    });

    m_cloudCopier->setCompletionCallback([this](const CopyReport& report) {
        // This callback is called from a background thread!
        // Marshal to main thread to safely access m_tasks and emit signals
        int successCopies = report.successfulCopies;
        int failedCopies = report.failedCopies;
        int skippedCopies = report.skippedCopies;

        QMetaObject::invokeMethod(this, [this, successCopies, failedCopies, skippedCopies]() {
            int totalSuccess, totalFail, totalSkip;
            bool allDone = false;
            int sourceIdx;
            {
                QMutexLocker locker(&m_statsMutex);
                m_successCount += successCopies;
                m_failCount += failedCopies;
                m_skipCount += skippedCopies;
                sourceIdx = m_tasksCompleted;  // Which source just completed
                m_tasksCompleted++;

                totalSuccess = m_successCount;
                totalFail = m_failCount;
                totalSkip = m_skipCount;

                // Only emit completion when ALL tasks are done
                allDone = (m_tasksCompleted >= m_totalTasksStarted);
            }

            // Update task statuses for ALL destinations of this source
            // Tasks are organized as: [src0->dest0, src0->dest1, src1->dest0, src1->dest1, ...]
            int destCount = m_destinations.size();
            int startTask = sourceIdx * destCount;
            int endTask = startTask + destCount;

            QString status = (failedCopies > 0) ? "Failed" : "Completed";
            for (int i = startTask; i < endTask && i < m_tasks.size(); ++i) {
                m_tasks[i].status = status;
                m_tasks[i].progress = 100;
                emit taskStatusChanged(m_tasks[i].taskId, status);
            }

            if (allDone) {
                m_isCopying = false;
                emit copyCompleted(totalSuccess, totalFail, totalSkip);
            }
        }, Qt::QueuedConnection);
    });

    m_cloudCopier->setErrorCallback([this](const std::string& taskId, const std::string& error) {
        // Marshal to main thread - callback is called from background thread
        QString errorMsg = QString::fromStdString(error);
        QString taskIdStr = QString::fromStdString(taskId);
        QMetaObject::invokeMethod(this, [this, taskIdStr, errorMsg]() {
            qDebug() << "CloudCopierController: Error for task" << taskIdStr << ":" << errorMsg;
            emit this->error("Copy", errorMsg);
        }, Qt::QueuedConnection);
    });

    m_cloudCopier->setConflictCallback([this](const CopyConflict& conflict) -> ConflictResolution {
        // If we have an "apply to all" resolution, use it
        if (m_hasApplyToAll.load()) {
            switch (m_applyToAllResolution.load()) {
                case CopyConflictResolution::SKIP_ALL:
                case CopyConflictResolution::SKIP:
                    return ConflictResolution::SKIP;
                case CopyConflictResolution::OVERWRITE_ALL:
                case CopyConflictResolution::OVERWRITE:
                    return ConflictResolution::OVERWRITE;
                default:
                    break;
            }
        }

        // Store conflict info and emit signal
        m_pendingConflict.sourcePath = QString::fromStdString(conflict.sourcePath);
        m_pendingConflict.destinationPath = QString::fromStdString(conflict.destinationPath);
        m_pendingConflict.existingName = QString::fromStdString(conflict.existingName);
        m_pendingConflict.existingSize = conflict.existingSize;
        m_pendingConflict.sourceSize = conflict.sourceSize;
        m_pendingConflict.isFolder = conflict.isFolder;

        // Convert time points to QDateTime
        auto existingTime = std::chrono::system_clock::to_time_t(conflict.existingModTime);
        auto sourceTime = std::chrono::system_clock::to_time_t(conflict.sourceModTime);
        m_pendingConflict.existingModTime = QDateTime::fromSecsSinceEpoch(existingTime);
        m_pendingConflict.sourceModTime = QDateTime::fromSecsSinceEpoch(sourceTime);

        emit conflictDetected(m_pendingConflict);

        // Default to skip if no response
        return ConflictResolution::SKIP;
    });

    // Load templates
    auto templates = m_cloudCopier->getTemplates();
    for (const auto& [name, tmpl] : templates) {
        CopyTemplateInfo info;
        info.name = QString::fromStdString(name);
        for (const auto& dest : tmpl.destinations) {
            info.destinations.append(QString::fromStdString(dest));
        }
        auto createdTime = std::chrono::system_clock::to_time_t(tmpl.created);
        auto lastUsedTime = std::chrono::system_clock::to_time_t(tmpl.lastUsed);
        info.created = QDateTime::fromSecsSinceEpoch(createdTime);
        info.lastUsed = QDateTime::fromSecsSinceEpoch(lastUsedTime);
        m_templates[info.name] = info;
    }
}

CloudCopierController::~CloudCopierController() {
    if (m_isCopying) {
        cancelCopy();
    }
}

// State queries
int CloudCopierController::getPendingTaskCount() const {
    int count = 0;
    for (const auto& task : m_tasks) {
        if (task.status == "Pending") {
            count++;
        }
    }
    return count;
}

int CloudCopierController::getCompletedTaskCount() const {
    int count = 0;
    for (const auto& task : m_tasks) {
        if (task.status == "Completed" || task.status == "Failed" || task.status == "Skipped") {
            count++;
        }
    }
    return count;
}

QStringList CloudCopierController::getTemplateNames() const {
    return m_templates.keys();
}

CopyTemplateInfo CloudCopierController::getTemplate(const QString& name) const {
    return m_templates.value(name);
}

// Source management
void CloudCopierController::addSource(const QString& remotePath) {
    if (!m_sources.contains(remotePath)) {
        m_sources.append(remotePath);
        emit sourcesChanged(m_sources);
    }
}

void CloudCopierController::addSources(const QStringList& remotePaths) {
    bool changed = false;
    for (const auto& path : remotePaths) {
        if (!m_sources.contains(path)) {
            m_sources.append(path);
            changed = true;
        }
    }
    if (changed) {
        emit sourcesChanged(m_sources);
    }
}

void CloudCopierController::removeSource(const QString& remotePath) {
    if (m_sources.removeOne(remotePath)) {
        emit sourcesChanged(m_sources);
    }
}

void CloudCopierController::clearSources() {
    if (!m_sources.isEmpty()) {
        m_sources.clear();
        emit sourcesChanged(m_sources);
    }
}

// Destination management
void CloudCopierController::addDestination(const QString& remotePath) {
    if (!m_destinations.contains(remotePath)) {
        m_destinations.append(remotePath);
        emit destinationsChanged(m_destinations);
    }
}

void CloudCopierController::addDestinations(const QStringList& remotePaths) {
    bool changed = false;
    for (const auto& path : remotePaths) {
        if (!m_destinations.contains(path)) {
            m_destinations.append(path);
            changed = true;
        }
    }
    if (changed) {
        emit destinationsChanged(m_destinations);
    }
}

void CloudCopierController::removeDestination(const QString& remotePath) {
    if (m_destinations.removeOne(remotePath)) {
        emit destinationsChanged(m_destinations);
    }
}

void CloudCopierController::clearDestinations() {
    if (!m_destinations.isEmpty()) {
        m_destinations.clear();
        emit destinationsChanged(m_destinations);
    }
}

// Template management
void CloudCopierController::saveTemplate(const QString& name) {
    std::vector<std::string> dests;
    for (const auto& dest : m_destinations) {
        dests.push_back(dest.toStdString());
    }

    if (m_cloudCopier->saveTemplate(name.toStdString(), dests)) {
        CopyTemplateInfo info;
        info.name = name;
        info.destinations = m_destinations;
        info.created = QDateTime::currentDateTime();
        info.lastUsed = info.created;
        m_templates[name] = info;
        emit templatesChanged();
    } else {
        emit error("Save Template", "Failed to save template: " + name);
    }
}

void CloudCopierController::loadTemplate(const QString& name) {
    auto dests = m_cloudCopier->loadTemplate(name.toStdString());
    if (!dests.empty()) {
        m_destinations.clear();
        for (const auto& dest : dests) {
            m_destinations.append(QString::fromStdString(dest));
        }
        emit destinationsChanged(m_destinations);

        // Update last used time
        if (m_templates.contains(name)) {
            m_templates[name].lastUsed = QDateTime::currentDateTime();
        }
    } else {
        emit error("Load Template", "Template not found: " + name);
    }
}

void CloudCopierController::deleteTemplate(const QString& name) {
    if (m_cloudCopier->deleteTemplate(name.toStdString())) {
        m_templates.remove(name);
        emit templatesChanged();
    } else {
        emit error("Delete Template", "Failed to delete template: " + name);
    }
}

// Import/Export
void CloudCopierController::importDestinationsFromFile(const QString& filePath) {
    auto dests = m_cloudCopier->importDestinationsFromFile(filePath.toStdString());
    if (!dests.empty()) {
        for (const auto& dest : dests) {
            QString path = QString::fromStdString(dest);
            if (!m_destinations.contains(path)) {
                m_destinations.append(path);
            }
        }
        emit destinationsChanged(m_destinations);
    } else {
        emit error("Import", "Failed to import destinations from: " + filePath);
    }
}

void CloudCopierController::exportDestinationsToFile(const QString& filePath) {
    std::vector<std::string> dests;
    for (const auto& dest : m_destinations) {
        dests.push_back(dest.toStdString());
    }

    if (!m_cloudCopier->exportDestinationsToFile(dests, filePath.toStdString())) {
        emit error("Export", "Failed to export destinations to: " + filePath);
    }
}

// Copy control
void CloudCopierController::previewCopy(bool copyContentsOnly) {
    if (m_sources.isEmpty()) {
        emit error("Preview", "No sources selected");
        return;
    }
    if (m_destinations.isEmpty()) {
        emit error("Preview", "No destinations selected");
        return;
    }

    // Use AccountManager's active API for proper multi-account support
    mega::MegaApi* megaApi = AccountManager::instance().activeApi();
    if (!megaApi) {
        // Fallback to legacy MegaManager API if no active account
        megaApi = static_cast<mega::MegaApi*>(m_megaApi);
    }

    if (!megaApi) {
        emit error("Preview", "No active MEGA session");
        return;
    }

    QVector<CopyPreviewItem> previewItems;

    // Get effective sources (expanded if copyContentsOnly)
    QStringList effectiveSources;
    if (copyContentsOnly) {
        for (const QString& source : m_sources) {
            mega::MegaNode* sourceNode = megaApi->getNodeByPath(source.toUtf8().constData());
            if (sourceNode && sourceNode->isFolder()) {
                mega::MegaNodeList* children = megaApi->getChildren(sourceNode);
                if (children) {
                    for (int i = 0; i < children->size(); ++i) {
                        mega::MegaNode* child = children->get(i);
                        if (child) {
                            QString childPath = source;
                            if (!childPath.endsWith('/')) childPath += '/';
                            childPath += QString::fromUtf8(child->getName());
                            effectiveSources.append(childPath);
                        }
                    }
                    delete children;
                }
                delete sourceNode;
            } else {
                effectiveSources.append(source);
                if (sourceNode) delete sourceNode;
            }
        }
    } else {
        effectiveSources = m_sources;
    }

    // Generate preview items for each source -> destination combination
    for (const QString& source : effectiveSources) {
        // Extract the name from the source path
        QString sourceName = source;
        int lastSlash = source.lastIndexOf('/');
        if (lastSlash >= 0 && lastSlash < source.length() - 1) {
            sourceName = source.mid(lastSlash + 1);
        }

        // Check if source is a folder
        bool isFolder = false;
        mega::MegaNode* sourceNode = megaApi->getNodeByPath(source.toUtf8().constData());
        if (sourceNode) {
            isFolder = sourceNode->isFolder();
            delete sourceNode;
        }

        for (const QString& dest : m_destinations) {
            CopyPreviewItem item;
            item.sourcePath = source;
            item.sourceName = sourceName;
            // The final destination path is where the item will be created
            item.destinationPath = dest;
            if (!item.destinationPath.endsWith('/')) {
                item.destinationPath += '/';
            }
            item.destinationPath += sourceName;
            item.isFolder = isFolder;
            previewItems.append(item);
        }
    }

    qDebug() << "CloudCopier: Preview generated with" << previewItems.size() << "items";
    emit previewReady(previewItems);
}

void CloudCopierController::setMoveMode(bool moveMode) {
    if (m_moveMode != moveMode) {
        m_moveMode = moveMode;
        emit moveModeChanged(moveMode);
    }
}

void CloudCopierController::startCopy(bool copyContentsOnly, bool skipExisting, bool moveMode) {
    if (m_sources.isEmpty()) {
        emit error("Start Copy", "No sources selected");
        return;
    }
    if (m_destinations.isEmpty()) {
        emit error("Start Copy", "No destinations selected");
        return;
    }
    if (m_isCopying) {
        emit error("Start Copy", "Copy already in progress");
        return;
    }

    // Update CloudCopier to use the active account's API
    mega::MegaApi* activeApi = AccountManager::instance().activeApi();
    if (activeApi) {
        m_cloudCopier->setMegaApi(activeApi);
    } else if (!m_megaApi) {
        emit error("Start Copy", "No active MEGA session");
        return;
    }

    m_isCopying = true;
    m_isPaused = false;
    m_cancelRequested = false;
    m_hasApplyToAll = false;
    m_moveMode = moveMode;

    // Set operation mode (COPY or MOVE)
    if (moveMode) {
        m_cloudCopier->setOperationMode(OperationMode::MOVE);
        qDebug() << "CloudCopier: Move mode enabled - source files will be deleted after transfer";
    } else {
        m_cloudCopier->setOperationMode(OperationMode::COPY);
    }

    // Set conflict resolution based on skipExisting flag
    if (skipExisting) {
        m_applyToAllResolution.store(CopyConflictResolution::SKIP_ALL);
        m_hasApplyToAll.store(true);
        m_cloudCopier->setDefaultConflictResolution(ConflictResolution::SKIP);
    } else {
        m_applyToAllResolution.store(CopyConflictResolution::OVERWRITE_ALL);
        m_hasApplyToAll.store(true);
        m_cloudCopier->setDefaultConflictResolution(ConflictResolution::OVERWRITE);
    }

    {
        QMutexLocker locker(&m_statsMutex);
        m_successCount = 0;
        m_failCount = 0;
        m_skipCount = 0;
        m_tasksCompleted = 0;
    }

    // If copyContentsOnly is enabled, we need to expand folder sources to their children
    QStringList effectiveSources;
    if (copyContentsOnly) {
        // Use AccountManager's active API for proper multi-account support
        mega::MegaApi* megaApi = AccountManager::instance().activeApi();
        if (!megaApi) {
            // Fallback to legacy MegaManager API if no active account
            megaApi = static_cast<mega::MegaApi*>(m_megaApi);
        }
        if (!megaApi) {
            emit error("Start Copy", "No active MEGA session");
            m_isCopying = false;
            return;
        }
        for (const QString& source : m_sources) {
            mega::MegaNode* sourceNode = megaApi->getNodeByPath(source.toUtf8().constData());
            if (sourceNode && sourceNode->isFolder()) {
                // Get children and add them as sources
                mega::MegaNodeList* children = megaApi->getChildren(sourceNode);
                if (children) {
                    for (int i = 0; i < children->size(); ++i) {
                        mega::MegaNode* child = children->get(i);
                        if (child) {
                            QString childPath = source;
                            if (!childPath.endsWith('/')) childPath += '/';
                            childPath += QString::fromUtf8(child->getName());
                            effectiveSources.append(childPath);
                            qDebug() << "CloudCopier: Expanded source child:" << childPath;
                        }
                    }
                    delete children;
                }
                delete sourceNode;
            } else {
                // Not a folder, add as-is
                effectiveSources.append(source);
                if (sourceNode) delete sourceNode;
            }
        }
        qDebug() << "CloudCopier: copyContentsOnly - expanded" << m_sources.size()
                 << "sources to" << effectiveSources.size() << "items";
    } else {
        effectiveSources = m_sources;
    }

    // Signal that we're about to clear tasks (so UI can clear its table)
    qDebug() << "CloudCopier: Emitting tasksClearing signal";
    emit tasksClearing();

    // Create tasks for UI
    m_tasks.clear();
    for (const auto& source : effectiveSources) {
        for (const auto& dest : m_destinations) {
            CopyTaskInfo task;
            task.taskId = generateTaskId();
            task.sourcePath = source;
            task.destinationPath = dest;
            task.status = "Pending";
            task.progress = 0;
            m_tasks.append(task);
            emit taskCreated(task.taskId, source, dest);
        }
    }

    // Track total tasks started
    m_totalTasksStarted = effectiveSources.size();

    // Total tasks for UI = sources × destinations
    int totalTasks = effectiveSources.size() * m_destinations.size();
    emit copyStarted(totalTasks);

    // Build destinations vector
    std::vector<CopyDestination> dests;
    for (const auto& dest : m_destinations) {
        CopyDestination d;
        d.remotePath = dest.toStdString();
        d.createIfMissing = true;
        dests.push_back(d);
    }

    // Start copy for each effective source
    try {
        for (const auto& source : effectiveSources) {
            qDebug() << "CloudCopier: Starting copy for source:" << source;
            QString taskId = QString::fromStdString(
                m_cloudCopier->copyToMultiple(source.toStdString(), dests)
            );
            qDebug() << "CloudCopier: Created task" << taskId << "- now starting...";
            m_cloudCopier->startTask(taskId.toStdString());
        }
        qDebug() << "CloudCopier: All copy tasks started successfully";
    } catch (const std::exception& e) {
        qDebug() << "CloudCopier: Exception during copy start:" << e.what();
        m_isCopying = false;
        emit error("Start Copy", QString("Exception: %1").arg(e.what()));
    } catch (...) {
        qDebug() << "CloudCopier: Unknown exception during copy start";
        m_isCopying = false;
        emit error("Start Copy", "Unknown exception occurred");
    }
}

void CloudCopierController::pauseCopy() {
    // Note: Pausing individual copy operations is complex with the current SDK
    // For now, this sets a flag that can be checked
    m_isPaused = true;
    emit copyPaused();
}

void CloudCopierController::resumeCopy() {
    m_isPaused = false;
    // Resume would need task ID tracking
}

void CloudCopierController::cancelCopy() {
    m_cancelRequested = true;
    m_isCopying = false;
    emit copyCancelled();
}

void CloudCopierController::clearCompletedTasks() {
    qDebug() << "CloudCopierController: Clearing completed tasks from" << m_tasks.size() << "total";

    m_tasks.erase(
        std::remove_if(m_tasks.begin(), m_tasks.end(),
            [](const CopyTaskInfo& task) {
                return task.status == "Completed" ||
                       task.status == "Failed" ||
                       task.status == "Skipped";
            }),
        m_tasks.end()
    );

    qDebug() << "CloudCopierController: After erase," << m_tasks.size() << "tasks remain";

    // Only call CLI module if it exists
    if (m_cloudCopier) {
        try {
            m_cloudCopier->clearCompletedTasks(0);
            qDebug() << "CloudCopierController: CLI clearCompletedTasks completed";
        } catch (const std::exception& e) {
            qDebug() << "CloudCopierController: Exception in clearCompletedTasks:" << e.what();
        } catch (...) {
            qDebug() << "CloudCopierController: Unknown exception in clearCompletedTasks";
        }
    }
}

// Conflict resolution
void CloudCopierController::resolveConflict(CopyConflictResolution resolution) {
    // Handle "apply to all" options
    if (resolution == CopyConflictResolution::SKIP_ALL ||
        resolution == CopyConflictResolution::OVERWRITE_ALL) {
        m_applyToAllResolution.store(resolution);
        m_hasApplyToAll.store(true);
    }

    // The actual resolution is handled in the callback
    // This would need a more complex synchronization mechanism
    // for true blocking resolution
}

// Utility
void CloudCopierController::verifyDestinations() {
    std::vector<std::string> dests;
    for (const auto& dest : m_destinations) {
        dests.push_back(dest.toStdString());
    }

    auto results = m_cloudCopier->verifyDestinations(dests);

    QStringList missing;
    for (const auto& [path, exists] : results) {
        if (!exists) {
            missing.append(QString::fromStdString(path));
        }
    }

    if (!missing.isEmpty()) {
        emit error("Verify", "Missing destinations: " + missing.join(", "));
    }
}

void CloudCopierController::createMissingDestinations() {
    std::vector<std::string> dests;
    for (const auto& dest : m_destinations) {
        dests.push_back(dest.toStdString());
    }

    if (!m_cloudCopier->createDestinations(dests)) {
        emit error("Create Destinations", "Failed to create some destinations");
    }
}

void CloudCopierController::browseRemoteFolder() {
    // This would trigger opening the remote folder browser dialog
    // Implemented in the panel
}

void CloudCopierController::packageSourcesIntoFolders(const QString& destParentPath) {
    if (!m_cloudCopier || m_sources.isEmpty()) {
        emit error("packageSources", "No sources to package");
        return;
    }

    qDebug() << "Packaging" << m_sources.size() << "sources into folders at" << destParentPath;

    QStringList packagedFolders;
    for (const QString& source : m_sources) {
        std::string resultPath = m_cloudCopier->packageFileIntoFolder(
            source.toStdString(),
            destParentPath.toStdString()
        );

        if (!resultPath.empty()) {
            packagedFolders.append(QString::fromStdString(resultPath));
            qDebug() << "  Packaged:" << source << "->" << QString::fromStdString(resultPath);
        } else {
            qDebug() << "  Failed to package:" << source;
            emit error("packageSources", "Failed to package: " + source);
        }
    }

    // Replace sources with the new packaged folders
    if (!packagedFolders.isEmpty()) {
        m_sources = packagedFolders;
        emit sourcesChanged(m_sources);
        qDebug() << "Sources updated to packaged folders:" << m_sources.size() << "items";
    }
}

// Private methods
void CloudCopierController::createCopyTasks() {
    m_tasks.clear();

    for (const auto& source : m_sources) {
        for (const auto& dest : m_destinations) {
            CopyTaskInfo task;
            task.taskId = generateTaskId();
            task.sourcePath = source;
            task.destinationPath = dest;
            task.status = "Pending";
            task.progress = 0;
            m_tasks.append(task);

            emit taskCreated(task.taskId, source, dest);
        }
    }
}

int CloudCopierController::generateTaskId() {
    return m_nextTaskId++;
}

void CloudCopierController::processNextConflict() {
    // This method is called after a conflict is resolved to continue processing
    // Currently conflict resolution is handled synchronously via the callback
    // This could be extended to support a queue of pending conflicts
    if (m_pendingConflict.sourcePath.isEmpty()) {
        return;  // No pending conflict
    }

    // Clear the pending conflict after processing
    m_pendingConflict = CopyConflictInfo();
}

void CloudCopierController::onCopyProgress(int completed, int total, const QString& currentItem) {
    emit copyProgress(completed, total, currentItem, QString());
}

void CloudCopierController::onCopyCompleted(int successful, int failed, int skipped) {
    m_isCopying = false;
    emit copyCompleted(successful, failed, skipped);
}

void CloudCopierController::onCopyError(const QString& taskId, const QString& errorMsg) {
    emit error("Copy", errorMsg);
}

void CloudCopierController::validateSources() {
    if (m_sources.isEmpty()) {
        emit error("Validate", "No sources to validate");
        return;
    }

    // Use AccountManager's active API for proper multi-account support
    mega::MegaApi* megaApi = AccountManager::instance().activeApi();
    if (!megaApi) {
        // Fallback to legacy MegaManager API if no active account
        megaApi = static_cast<mega::MegaApi*>(m_megaApi);
    }

    if (!megaApi) {
        emit error("Validate", "No active MEGA session");
        return;
    }

    QVector<PathValidationResult> results;

    for (const QString& path : m_sources) {
        PathValidationResult result;
        result.path = path;

        mega::MegaNode* node = megaApi->getNodeByPath(path.toUtf8().constData());
        if (node) {
            result.exists = true;
            result.isFolder = node->isFolder();
            delete node;
        } else {
            result.exists = false;
            result.errorMessage = "Path not found";
        }

        results.append(result);
    }

    qDebug() << "CloudCopier: Validated" << results.size() << "sources";
    emit sourcesValidated(results);
}

void CloudCopierController::validateDestinations() {
    if (m_destinations.isEmpty()) {
        emit error("Validate", "No destinations to validate");
        return;
    }

    // Use AccountManager's active API for proper multi-account support
    mega::MegaApi* megaApi = AccountManager::instance().activeApi();
    if (!megaApi) {
        // Fallback to legacy MegaManager API if no active account
        megaApi = static_cast<mega::MegaApi*>(m_megaApi);
    }

    if (!megaApi) {
        emit error("Validate", "No active MEGA session");
        return;
    }

    QVector<PathValidationResult> results;

    for (const QString& path : m_destinations) {
        PathValidationResult result;
        result.path = path;

        mega::MegaNode* node = megaApi->getNodeByPath(path.toUtf8().constData());
        if (node) {
            result.exists = true;
            result.isFolder = node->isFolder();
            if (!node->isFolder()) {
                result.errorMessage = "Path exists but is not a folder";
            }
            delete node;
        } else {
            result.exists = false;
            result.errorMessage = "Path not found";
        }

        results.append(result);
    }

    qDebug() << "CloudCopier: Validated" << results.size() << "destinations";
    emit destinationsValidated(results);
}

// === Member Mode Implementation ===

QList<MemberInfo> CloudCopierController::getAvailableMembers() const {
    return m_availableMembers;
}

void CloudCopierController::setMemberMode(bool enabled) {
    if (m_memberModeEnabled != enabled) {
        m_memberModeEnabled = enabled;
        emit memberModeChanged(enabled);

        if (enabled) {
            // Refresh available members when enabling member mode
            refreshAvailableMembers();
        }
    }
}

void CloudCopierController::selectMember(const QString& memberId) {
    m_selectedMemberId = memberId;
    m_allMembersSelected = false;

    // Find member name for signal
    QString memberName;
    for (const MemberInfo& member : m_availableMembers) {
        if (member.id == memberId) {
            memberName = member.displayName;
            break;
        }
    }

    emit allMembersSelectionChanged(false);
    emit selectedMemberChanged(memberId, memberName);
}

void CloudCopierController::selectAllMembers(bool selectAll) {
    m_allMembersSelected = selectAll;
    if (selectAll) {
        m_selectedMemberId.clear();
    }
    emit allMembersSelectionChanged(selectAll);
}

void CloudCopierController::setDestinationTemplate(const QString& templatePath) {
    if (m_destinationTemplate != templatePath) {
        m_destinationTemplate = templatePath;
        emit destinationTemplateChanged(templatePath);
    }
}

void CloudCopierController::refreshAvailableMembers() {
    // Get members with distribution folders from the registry
    m_availableMembers = MemberRegistry::instance()->getMembersWithDistributionFolders();

    qDebug() << "CloudCopierController: Refreshed available members -"
             << m_availableMembers.size() << "members with distribution folders";

    emit availableMembersChanged(m_availableMembers);
}

void CloudCopierController::previewTemplateExpansion() {
    if (m_destinationTemplate.isEmpty()) {
        emit error("Preview", "No destination template set");
        return;
    }

    // Validate the template
    QString validationError;
    if (!TemplateExpander::validateTemplate(m_destinationTemplate, &validationError)) {
        emit error("Preview", "Invalid template: " + validationError);
        return;
    }

    TemplateExpansionPreview preview;
    preview.templatePath = m_destinationTemplate;

    // Get target members
    QList<MemberInfo> targetMembers;
    if (m_allMembersSelected) {
        targetMembers = m_availableMembers;
    } else if (!m_selectedMemberId.isEmpty()) {
        for (const MemberInfo& member : m_availableMembers) {
            if (member.id == m_selectedMemberId) {
                targetMembers.append(member);
                break;
            }
        }
    }

    if (targetMembers.isEmpty()) {
        emit error("Preview", "No members selected for preview");
        return;
    }

    // Expand template for each member
    QList<TemplateExpander::ExpansionResult> results =
        TemplateExpander::expandForMembers(m_destinationTemplate, targetMembers);

    for (const auto& result : results) {
        MemberDestinationInfo destInfo;
        destInfo.memberId = result.memberId;
        destInfo.memberName = result.memberName;
        destInfo.expandedPath = result.expandedPath;
        destInfo.isValid = result.isValid;
        destInfo.errorMessage = result.errorMessage;

        preview.members.append(destInfo);

        if (result.isValid) {
            preview.validCount++;
        } else {
            preview.invalidCount++;
        }
    }

    qDebug() << "CloudCopierController: Template expansion preview -"
             << preview.validCount << "valid," << preview.invalidCount << "invalid";

    emit templateExpansionReady(preview);
}

void CloudCopierController::startMemberCopy(bool copyContentsOnly, bool skipExisting) {
    if (m_sources.isEmpty()) {
        emit error("Start Copy", "No sources selected");
        return;
    }
    if (m_destinationTemplate.isEmpty()) {
        emit error("Start Copy", "No destination template set");
        return;
    }
    if (m_isCopying) {
        emit error("Start Copy", "Copy already in progress");
        return;
    }

    // Get target members
    QList<MemberInfo> targetMembers;
    if (m_allMembersSelected) {
        targetMembers = m_availableMembers;
    } else if (!m_selectedMemberId.isEmpty()) {
        for (const MemberInfo& member : m_availableMembers) {
            if (member.id == m_selectedMemberId) {
                targetMembers.append(member);
                break;
            }
        }
    }

    if (targetMembers.isEmpty()) {
        emit error("Start Copy", "No members selected");
        return;
    }

    // Expand template for each member to get destinations
    QList<TemplateExpander::ExpansionResult> expansions =
        TemplateExpander::expandForMembers(m_destinationTemplate, targetMembers);

    // Build destinations list from valid expansions
    QStringList memberDestinations;
    QMap<QString, QPair<QString, QString>> destToMember; // destination -> (memberId, memberName)

    for (const auto& expansion : expansions) {
        if (expansion.isValid) {
            memberDestinations.append(expansion.expandedPath);
            destToMember[expansion.expandedPath] = qMakePair(expansion.memberId, expansion.memberName);
        } else {
            qDebug() << "CloudCopierController: Skipping invalid member expansion -"
                     << expansion.memberName << ":" << expansion.errorMessage;
        }
    }

    if (memberDestinations.isEmpty()) {
        emit error("Start Copy", "No valid member destinations after template expansion");
        return;
    }

    // Store expanded destinations temporarily (replacing manual destinations)
    QStringList originalDestinations = m_destinations;
    m_destinations = memberDestinations;

    // Update CloudCopier to use the active account's API
    mega::MegaApi* activeApi = AccountManager::instance().activeApi();
    if (activeApi) {
        m_cloudCopier->setMegaApi(activeApi);
    } else if (!m_megaApi) {
        m_destinations = originalDestinations;
        emit error("Start Copy", "No active MEGA session");
        return;
    }

    m_isCopying = true;
    m_isPaused = false;
    m_cancelRequested = false;
    m_hasApplyToAll = false;

    // Set conflict resolution
    if (skipExisting) {
        m_applyToAllResolution.store(CopyConflictResolution::SKIP_ALL);
        m_hasApplyToAll.store(true);
        m_cloudCopier->setDefaultConflictResolution(ConflictResolution::SKIP);
    } else {
        m_applyToAllResolution.store(CopyConflictResolution::OVERWRITE_ALL);
        m_hasApplyToAll.store(true);
        m_cloudCopier->setDefaultConflictResolution(ConflictResolution::OVERWRITE);
    }

    {
        QMutexLocker locker(&m_statsMutex);
        m_successCount = 0;
        m_failCount = 0;
        m_skipCount = 0;
        m_tasksCompleted = 0;
    }

    // Handle copyContentsOnly expansion
    QStringList effectiveSources;
    if (copyContentsOnly) {
        mega::MegaApi* megaApi = AccountManager::instance().activeApi();
        if (!megaApi) {
            megaApi = static_cast<mega::MegaApi*>(m_megaApi);
        }
        if (!megaApi) {
            m_destinations = originalDestinations;
            emit error("Start Copy", "No active MEGA session");
            m_isCopying = false;
            return;
        }
        for (const QString& source : m_sources) {
            mega::MegaNode* sourceNode = megaApi->getNodeByPath(source.toUtf8().constData());
            if (sourceNode && sourceNode->isFolder()) {
                mega::MegaNodeList* children = megaApi->getChildren(sourceNode);
                if (children) {
                    for (int i = 0; i < children->size(); ++i) {
                        mega::MegaNode* child = children->get(i);
                        if (child) {
                            QString childPath = source;
                            if (!childPath.endsWith('/')) childPath += '/';
                            childPath += QString::fromUtf8(child->getName());
                            effectiveSources.append(childPath);
                        }
                    }
                    delete children;
                }
                delete sourceNode;
            } else {
                effectiveSources.append(source);
                if (sourceNode) delete sourceNode;
            }
        }
    } else {
        effectiveSources = m_sources;
    }

    // Signal that we're about to clear tasks
    emit tasksClearing();

    // Create tasks for UI with member information
    m_tasks.clear();
    for (const auto& source : effectiveSources) {
        for (const auto& dest : memberDestinations) {
            CopyTaskInfo task;
            task.taskId = generateTaskId();
            task.sourcePath = source;
            task.destinationPath = dest;
            task.status = "Pending";
            task.progress = 0;
            m_tasks.append(task);

            // Emit both regular and member-specific task created signals
            emit taskCreated(task.taskId, source, dest);

            if (destToMember.contains(dest)) {
                emit memberTaskCreated(task.taskId, source, dest,
                                       destToMember[dest].first,
                                       destToMember[dest].second);
            }
        }
    }

    m_totalTasksStarted = effectiveSources.size();

    int totalTasks = effectiveSources.size() * memberDestinations.size();
    emit copyStarted(totalTasks);

    qDebug() << "CloudCopierController: Starting member copy -"
             << effectiveSources.size() << "sources to"
             << memberDestinations.size() << "member destinations";

    // Build destinations vector for CloudCopier
    std::vector<CopyDestination> dests;
    for (const auto& dest : memberDestinations) {
        CopyDestination d;
        d.remotePath = dest.toStdString();
        d.createIfMissing = true;
        dests.push_back(d);
    }

    // Start copy for each source
    try {
        for (const auto& source : effectiveSources) {
            QString taskId = QString::fromStdString(
                m_cloudCopier->copyToMultiple(source.toStdString(), dests)
            );
            m_cloudCopier->startTask(taskId.toStdString());
        }
    } catch (const std::exception& e) {
        m_isCopying = false;
        m_destinations = originalDestinations;
        emit error("Start Copy", QString("Exception: %1").arg(e.what()));
    } catch (...) {
        m_isCopying = false;
        m_destinations = originalDestinations;
        emit error("Start Copy", "Unknown exception occurred");
    }

    // Note: Destinations will be restored when copy completes via callback
}

} // namespace MegaCustom
