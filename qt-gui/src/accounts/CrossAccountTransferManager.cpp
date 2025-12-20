#include "CrossAccountTransferManager.h"
#include "TransferLogStore.h"
#include "SessionPool.h"
#include <megaapi.h>
#include <QUuid>
#include <QTimer>
#include <QDebug>
#include <QCoreApplication>
#include <QEventLoop>

namespace MegaCustom {

namespace {
// Helper to wait for async operation with proper event loop handling
// Returns true if condition was met, false if timed out or cancelled
template<typename CheckFunc, typename ProgressFunc>
bool waitForCondition(int timeoutMs, int checkIntervalMs,
                      CheckFunc&& isFinished,
                      ProgressFunc&& onProgress,
                      const bool* cancelled = nullptr) {
    QEventLoop loop;
    QTimer timeoutTimer;
    QTimer checkTimer;

    timeoutTimer.setSingleShot(true);

    int elapsed = 0;
    // Use explicit captures to prevent dangling reference risks
    // - cancelled captured by value (pointer copy) to avoid reference to pointer
    // - checkIntervalMs captured by value since it's a simple int
    QObject::connect(&checkTimer, &QTimer::timeout,
        [&loop, &elapsed, checkIntervalMs, cancelled, &isFinished, &onProgress]() {
        elapsed += checkIntervalMs;

        // Check cancellation
        if (cancelled && *cancelled) {
            loop.quit();
            return;
        }

        // Check if finished
        if (isFinished()) {
            loop.quit();
            return;
        }

        // Report progress
        onProgress(elapsed);
    });

    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timeoutTimer.start(timeoutMs);
    checkTimer.start(checkIntervalMs);

    loop.exec();

    checkTimer.stop();
    timeoutTimer.stop();

    return isFinished();
}
} // anonymous namespace

CrossAccountTransferManager::CrossAccountTransferManager(SessionPool* sessionPool,
                                                         TransferLogStore* logStore,
                                                         QObject* parent)
    : QObject(parent)
    , m_sessionPool(sessionPool)
    , m_logStore(logStore)
    , m_maxConcurrent(2)
    , m_currentConcurrent(0)
{
}

CrossAccountTransferManager::~CrossAccountTransferManager()
{
    // Cancel all active transfers
    for (auto it = m_activeTasks.begin(); it != m_activeTasks.end(); ++it) {
        it->cancelled = true;
    }
}

QString CrossAccountTransferManager::copyToAccount(const QStringList& sourcePaths,
                                                   const QString& sourceAccountId,
                                                   const QString& targetAccountId,
                                                   const QString& targetPath)
{
    return startTransfer(sourcePaths, sourceAccountId, targetAccountId, targetPath,
                         CrossAccountTransfer::Copy);
}

QString CrossAccountTransferManager::moveToAccount(const QStringList& sourcePaths,
                                                   const QString& sourceAccountId,
                                                   const QString& targetAccountId,
                                                   const QString& targetPath,
                                                   bool skipSharedLinkWarning)
{
    // Check for existing shared links that will be broken by this move
    if (!skipSharedLinkWarning) {
        QStringList pathsWithLinks = getPathsWithSharedLinks(sourcePaths, sourceAccountId);
        if (!pathsWithLinks.isEmpty()) {
            qDebug() << "CrossAccountTransferManager: Move blocked -"
                     << pathsWithLinks.size() << "paths have shared links";
            emit sharedLinksWillBreak(sourcePaths, pathsWithLinks,
                                      sourceAccountId, targetAccountId, targetPath);
            return QString();  // Return empty - caller should wait for user confirmation
        }
    }

    return startTransfer(sourcePaths, sourceAccountId, targetAccountId, targetPath,
                         CrossAccountTransfer::Move);
}

QStringList CrossAccountTransferManager::getPathsWithSharedLinks(const QStringList& sourcePaths,
                                                                  const QString& sourceAccountId)
{
    QStringList result;

    mega::MegaApi* sourceApi = m_sessionPool->getSession(sourceAccountId);
    if (!sourceApi) {
        qWarning() << "CrossAccountTransferManager: Cannot check shared links, session unavailable";
        return result;
    }

    for (const QString& path : sourcePaths) {
        mega::MegaNode* node = sourceApi->getNodeByPath(path.toUtf8().constData());
        if (node) {
            if (node->isExported()) {
                result.append(path);
            }
            delete node;
        }
    }

    return result;
}

QString CrossAccountTransferManager::startTransfer(const QStringList& sourcePaths,
                                                   const QString& sourceAccountId,
                                                   const QString& targetAccountId,
                                                   const QString& targetPath,
                                                   CrossAccountTransfer::Operation operation)
{
    if (sourcePaths.isEmpty() || sourceAccountId.isEmpty() || targetAccountId.isEmpty()) {
        qWarning() << "CrossAccountTransferManager: Invalid transfer parameters";
        return QString();
    }

    if (sourceAccountId == targetAccountId) {
        qWarning() << "CrossAccountTransferManager: Source and target are the same account";
        return QString();
    }

    // Create transfer record
    CrossAccountTransfer transfer;
    transfer.id = generateTransferId();
    transfer.timestamp = QDateTime::currentDateTime();
    transfer.sourceAccountId = sourceAccountId;
    transfer.sourcePath = sourcePaths.join(";"); // Store multiple paths
    transfer.targetAccountId = targetAccountId;
    transfer.targetPath = targetPath;
    transfer.operation = operation;
    transfer.status = CrossAccountTransfer::Pending;
    transfer.bytesTransferred = 0;
    transfer.bytesTotal = 0;
    transfer.filesTransferred = 0;
    transfer.filesTotal = sourcePaths.size();
    transfer.retryCount = 0;
    transfer.canRetry = true;

    // Try to get size info from source account
    mega::MegaApi* sourceApi = m_sessionPool->getSession(sourceAccountId);
    if (sourceApi) {
        transfer.bytesTotal = calculateTotalSize(sourceApi, sourcePaths);
        transfer.filesTotal = countFiles(sourceApi, sourcePaths);
    }

    // Log transfer
    m_logStore->logTransfer(transfer);

    // Create task
    TransferTask task;
    task.transfer = transfer;
    task.currentStep = 0;
    task.cancelled = false;
    task.currentFileIndex = 0;
    task.tempLinks.clear();
    task.newlyExportedPaths.clear();
    m_activeTasks[transfer.id] = task;

    // Queue for processing
    m_queue.enqueue(transfer.id);

    qDebug() << "CrossAccountTransferManager: Queued transfer" << transfer.id
             << "from" << sourceAccountId << "to" << targetAccountId;

    // Start processing
    QTimer::singleShot(0, this, &CrossAccountTransferManager::processNextInQueue);

    return transfer.id;
}

void CrossAccountTransferManager::cancelTransfer(const QString& transferId)
{
    if (!m_activeTasks.contains(transferId)) {
        return;
    }

    TransferTask& task = m_activeTasks[transferId];
    task.cancelled = true;
    task.transfer.status = CrossAccountTransfer::Cancelled;

    m_logStore->updateTransfer(task.transfer);
    emit transferCancelled(transferId);

    // Remove from queue if pending
    m_queue.removeAll(transferId);
}

QString CrossAccountTransferManager::retryTransfer(const QString& transferId)
{
    CrossAccountTransfer original = m_logStore->getTransfer(transferId);
    if (original.id.isEmpty() || !original.canRetry) {
        return QString();
    }

    if (original.status != CrossAccountTransfer::Failed) {
        return QString();
    }

    // Parse source paths
    QStringList sourcePaths = original.sourcePath.split(";", Qt::SkipEmptyParts);

    // Create new transfer with incremented retry count
    QString newId = startTransfer(sourcePaths, original.sourceAccountId,
                                  original.targetAccountId, original.targetPath,
                                  original.operation);

    if (!newId.isEmpty() && m_activeTasks.contains(newId)) {
        m_activeTasks[newId].transfer.retryCount = original.retryCount + 1;
        m_logStore->updateTransfer(m_activeTasks[newId].transfer);
    }

    return newId;
}

QList<CrossAccountTransfer> CrossAccountTransferManager::getActiveTransfers() const
{
    QList<CrossAccountTransfer> result;
    for (auto it = m_activeTasks.constBegin(); it != m_activeTasks.constEnd(); ++it) {
        if (!it->cancelled) {
            result.append(it->transfer);
        }
    }
    return result;
}

QList<CrossAccountTransfer> CrossAccountTransferManager::getHistory(int limit) const
{
    return m_logStore->getAll(limit);
}

bool CrossAccountTransferManager::hasActiveTransfers() const
{
    return !m_activeTasks.isEmpty();
}

int CrossAccountTransferManager::activeTransferCount() const
{
    int count = 0;
    for (auto it = m_activeTasks.constBegin(); it != m_activeTasks.constEnd(); ++it) {
        if (!it->cancelled) {
            ++count;
        }
    }
    return count;
}

bool CrossAccountTransferManager::hasActiveTransfersForAccount(const QString& accountId) const
{
    for (auto it = m_activeTasks.constBegin(); it != m_activeTasks.constEnd(); ++it) {
        if (!it->cancelled) {
            const CrossAccountTransfer& transfer = it->transfer;
            if (transfer.sourceAccountId == accountId || transfer.targetAccountId == accountId) {
                return true;
            }
        }
    }
    return false;
}

void CrossAccountTransferManager::processNextInQueue()
{
    while (m_currentConcurrent < m_maxConcurrent && !m_queue.isEmpty()) {
        QString transferId = m_queue.dequeue();

        if (!m_activeTasks.contains(transferId)) {
            continue;
        }

        TransferTask& task = m_activeTasks[transferId];
        if (task.cancelled) {
            m_activeTasks.remove(transferId);
            continue;
        }

        m_currentConcurrent++;
        executeTransfer(transferId);
    }
}

void CrossAccountTransferManager::executeTransfer(const QString& transferId)
{
    if (!m_activeTasks.contains(transferId)) {
        m_currentConcurrent--;
        processNextInQueue();
        return;
    }

    TransferTask& task = m_activeTasks[transferId];

    if (task.cancelled) {
        m_activeTasks.remove(transferId);
        m_currentConcurrent--;
        processNextInQueue();
        return;
    }

    // Update status
    task.transfer.status = CrossAccountTransfer::InProgress;
    m_logStore->updateTransfer(task.transfer);
    emit transferStarted(task.transfer);

    qDebug() << "CrossAccountTransferManager: Executing transfer" << transferId
             << "step" << task.currentStep;

    // Execute based on current step
    switch (task.currentStep) {
    case 0:
        task.currentStep = 1;
        stepGetPublicLink(task);
        break;
    case 1:
        task.currentStep = 2;
        stepImportToTarget(task);
        break;
    case 2:
        if (task.transfer.operation == CrossAccountTransfer::Move) {
            task.currentStep = 3;
            stepDeleteSource(task);  // Move: delete source (which also disables export)
        } else {
            task.currentStep = 3;
            stepCleanupExports(task);  // Copy: cleanup exports only (security fix)
        }
        break;
    case 3:
        finishTransfer(transferId, true);
        break;
    default:
        finishTransfer(transferId, false, "Invalid transfer state");
        break;
    }
}

void CrossAccountTransferManager::stepGetPublicLink(TransferTask& task)
{
    // Wait for session to be fully ready (including fetchNodes)
    if (!m_sessionPool->waitForSession(task.transfer.sourceAccountId, 60000)) {
        finishTransfer(task.transfer.id, false, "Source account session not ready");
        return;
    }

    mega::MegaApi* sourceApi = m_sessionPool->getSession(task.transfer.sourceAccountId);
    if (!sourceApi) {
        finishTransfer(task.transfer.id, false, "Source account not available");
        return;
    }

    QStringList paths = task.transfer.sourcePath.split(";", Qt::SkipEmptyParts);
    if (paths.isEmpty()) {
        finishTransfer(task.transfer.id, false, "No source paths specified");
        return;
    }

    // Export listener class for each node
    class ExportListener : public mega::MegaRequestListener {
    public:
        bool finished = false;
        bool success = false;
        QString link;
        QString error;

        void onRequestFinish(mega::MegaApi*, mega::MegaRequest* request, mega::MegaError* e) override {
            finished = true;
            if (e->getErrorCode() == mega::MegaError::API_OK) {
                success = true;
                link = QString::fromUtf8(request->getLink());
            } else {
                success = false;
                error = QString::fromUtf8(e->getErrorString());
            }
        }
    };

    // Process all files/folders sequentially
    for (int i = task.currentFileIndex; i < paths.size(); ++i) {
        // Check for cancellation
        if (task.cancelled) {
            finishTransfer(task.transfer.id, false, "Transfer cancelled");
            return;
        }

        const QString& path = paths[i];

        mega::MegaNode* node = sourceApi->getNodeByPath(path.toUtf8().constData());
        if (!node) {
            finishTransfer(task.transfer.id, false, "Source file not found: " + path);
            return;
        }

        // Check if node is already exported (has existing public link)
        bool wasAlreadyExported = node->isExported();
        QString existingLink;

        if (wasAlreadyExported) {
            // Use existing public link - don't create a new one
            char* linkPtr = node->getPublicLink(true);
            if (linkPtr) {
                existingLink = QString::fromUtf8(linkPtr);
                delete[] linkPtr;
            }
        }

        if (!existingLink.isEmpty()) {
            // Node already has a public link - reuse it
            qDebug() << "CrossAccountTransferManager: Reusing existing link for" << path;
            task.tempLinks.append(existingLink);
            task.tempLink = existingLink;
            delete node;
        } else {
            // Need to create new public link
            ExportListener* listener = new ExportListener();

            // exportNode(node, expireTime, writable, megaHosted, listener)
            // 0 means no expiry, false for non-writable, false for not megaHosted
            sourceApi->exportNode(node, 0, false, false, listener);

            // Wait for completion with proper event loop
            bool finished = waitForCondition(30000, 100,
                [&]() { return listener->finished; },
                [](int) { /* No progress needed */ },
                &task.cancelled);

            delete node;

            if (task.cancelled) {
                delete listener;
                finishTransfer(task.transfer.id, false, "Transfer cancelled");
                return;
            }

            if (!finished || !listener->finished) {
                delete listener;
                finishTransfer(task.transfer.id, false, "Timeout getting public link for: " + path);
                return;
            }

            if (!listener->success) {
                QString error = listener->error;
                delete listener;
                finishTransfer(task.transfer.id, false, "Failed to get link for: " + path + " - " + error);
                return;
            }

            task.tempLinks.append(listener->link);
            task.tempLink = listener->link;
            delete listener;

            // Track that WE created this export (so we can clean it up later)
            task.newlyExportedPaths.append(path);
            qDebug() << "CrossAccountTransferManager: Created new link for" << path;
        }

        task.currentFileIndex = i + 1;

        // Update progress (export phase is ~33% of total transfer)
        int fileProgress = ((i + 1) * 100) / paths.size();
        int overallProgress = fileProgress / 3;  // Export is first third
        emit transferProgress(task.transfer.id, overallProgress,
                              0, task.transfer.bytesTotal);
    }

    qDebug() << "CrossAccountTransferManager: Got" << task.tempLinks.size()
             << "links for" << task.transfer.id;

    // Continue to next step
    QTimer::singleShot(0, this, [this, id = task.transfer.id]() {
        if (m_activeTasks.contains(id)) {
            executeTransfer(id);
        }
    });
}

void CrossAccountTransferManager::stepImportToTarget(TransferTask& task)
{
    // Wait for target session to be fully ready
    if (!m_sessionPool->waitForSession(task.transfer.targetAccountId, 60000)) {
        finishTransfer(task.transfer.id, false, "Target account session not ready");
        return;
    }

    mega::MegaApi* targetApi = m_sessionPool->getSession(task.transfer.targetAccountId);
    if (!targetApi) {
        finishTransfer(task.transfer.id, false, "Target account not available");
        return;
    }

    // Check we have links to import
    if (task.tempLinks.isEmpty()) {
        // Fallback to single link for backward compatibility
        if (!task.tempLink.isEmpty()) {
            task.tempLinks.append(task.tempLink);
        } else {
            finishTransfer(task.transfer.id, false, "No links available for import");
            return;
        }
    }

    // Get target folder
    mega::MegaNode* targetFolder = targetApi->getNodeByPath(task.transfer.targetPath.toUtf8().constData());
    if (!targetFolder) {
        // Try to use root
        targetFolder = targetApi->getRootNode();
    }

    if (!targetFolder) {
        finishTransfer(task.transfer.id, false, "Target folder not accessible");
        return;
    }

    // Listener class for getting public node from link
    class GetPublicNodeListener : public mega::MegaRequestListener {
    public:
        bool finished = false;
        bool success = false;
        mega::MegaNode* publicNode = nullptr;
        QString error;

        void onRequestFinish(mega::MegaApi*, mega::MegaRequest* request, mega::MegaError* e) override {
            finished = true;
            if (e->getErrorCode() == mega::MegaError::API_OK) {
                success = true;
                publicNode = request->getPublicMegaNode()->copy();
            } else {
                success = false;
                error = QString::fromUtf8(e->getErrorString());
            }
        }
    };

    // Listener class for import (copyNode)
    class ImportListener : public mega::MegaRequestListener {
    public:
        bool finished = false;
        bool success = false;
        QString error;

        void onRequestFinish(mega::MegaApi*, mega::MegaRequest*, mega::MegaError* e) override {
            finished = true;
            if (e->getErrorCode() == mega::MegaError::API_OK) {
                success = true;
            } else {
                success = false;
                error = QString::fromUtf8(e->getErrorString());
            }
        }
    };

    int successCount = 0;
    int totalLinks = task.tempLinks.size();

    // Import all links sequentially
    for (int i = 0; i < totalLinks; ++i) {
        // Check for cancellation
        if (task.cancelled) {
            delete targetFolder;
            finishTransfer(task.transfer.id, false, "Transfer cancelled");
            return;
        }

        const QString& link = task.tempLinks[i];

        // Step 1: Get public node from link
        GetPublicNodeListener* getListener = new GetPublicNodeListener();
        targetApi->getPublicNode(link.toUtf8().constData(), getListener);

        // Wait for getPublicNode with proper event loop (30s timeout)
        waitForCondition(30000, 100,
            [&]() { return getListener->finished; },
            [](int) { /* No progress needed for quick operation */ },
            &task.cancelled);

        if (task.cancelled || !getListener->finished || !getListener->success || !getListener->publicNode) {
            qWarning() << "CrossAccountTransferManager: Failed to get public node for link" << (i + 1)
                       << "of" << totalLinks << "-" << getListener->error;
            delete getListener;
            continue;  // Skip failed files, continue with others
        }

        mega::MegaNode* publicNode = getListener->publicNode;
        qDebug() << "CrossAccountTransferManager: Got public node for import -"
                 << "name:" << QString::fromUtf8(publicNode->getName())
                 << "isFolder:" << publicNode->isFolder()
                 << "size:" << publicNode->getSize();
        delete getListener;

        // Step 2: Import (copy) the node to target
        ImportListener* importListener = new ImportListener();
        qDebug() << "CrossAccountTransferManager: Starting copyNode to folder:"
                 << QString::fromUtf8(targetFolder->getName());
        targetApi->copyNode(publicNode, targetFolder, importListener);

        // Wait for import with progress reporting (120s timeout per file)
        constexpr int importTimeout = 120000;
        waitForCondition(importTimeout, 100,
            [&]() { return importListener->finished; },
            [&, this](int elapsed) {
                // Update progress: Export phase was 0-33%, import phase is 33-100%
                int withinFileProgress = qMin(100, (elapsed * 100) / importTimeout);
                int fileProgress = (i * 100 + withinFileProgress) / totalLinks;
                int overallProgress = 33 + (fileProgress * 67) / 100;

                qint64 estimatedBytes = (task.transfer.bytesTotal * overallProgress) / 100;
                emit transferProgress(task.transfer.id, overallProgress,
                                      estimatedBytes, task.transfer.bytesTotal);
            },
            &task.cancelled);

        delete publicNode;

        if (task.cancelled) {
            delete importListener;
            break;  // Exit loop on cancellation
        }

        if (importListener->finished && importListener->success) {
            successCount++;
            qDebug() << "CrossAccountTransferManager: Imported file" << (i + 1)
                     << "of" << totalLinks << "for" << task.transfer.id;
        } else {
            qWarning() << "CrossAccountTransferManager: Failed to import file" << (i + 1)
                       << "of" << totalLinks << "-"
                       << (importListener->finished ? importListener->error : "Timeout");
        }
        delete importListener;
    }

    delete targetFolder;

    task.transfer.filesTransferred = successCount;
    task.transfer.bytesTransferred = task.transfer.bytesTotal;
    emit transferProgress(task.transfer.id, 100, task.transfer.bytesTotal, task.transfer.bytesTotal);

    qDebug() << "CrossAccountTransferManager: Imported" << successCount << "of"
             << totalLinks << "items for" << task.transfer.id;

    // Check if any imports actually succeeded
    if (successCount == 0) {
        finishTransfer(task.transfer.id, false, "Failed to import any files to target account");
        return;
    }

    // Warn if some files failed but don't fail the whole transfer
    if (successCount < totalLinks) {
        qWarning() << "CrossAccountTransferManager: Only" << successCount << "of"
                   << totalLinks << "files imported successfully for" << task.transfer.id;
    }

    // Continue to next step (cleanup or delete source)
    QTimer::singleShot(0, this, [this, id = task.transfer.id]() {
        if (m_activeTasks.contains(id)) {
            executeTransfer(id);
        }
    });
}

void CrossAccountTransferManager::stepDeleteSource(TransferTask& task)
{
    // Only for move operations
    if (task.transfer.operation != CrossAccountTransfer::Move) {
        finishTransfer(task.transfer.id, true);
        return;
    }

    // Wait for source session (should already be active from stepGetPublicLink, but be safe)
    if (!m_sessionPool->waitForSession(task.transfer.sourceAccountId, 30000)) {
        // Import succeeded, but can't delete source - still consider it a success
        qWarning() << "CrossAccountTransferManager: Can't delete source, session not ready";
        finishTransfer(task.transfer.id, true);
        return;
    }

    mega::MegaApi* sourceApi = m_sessionPool->getSession(task.transfer.sourceAccountId);
    if (!sourceApi) {
        // Import succeeded, but can't delete source - still consider it a success
        qWarning() << "CrossAccountTransferManager: Can't delete source, account not available";
        finishTransfer(task.transfer.id, true);
        return;
    }

    QStringList paths = task.transfer.sourcePath.split(";", Qt::SkipEmptyParts);
    for (const QString& path : paths) {
        mega::MegaNode* node = sourceApi->getNodeByPath(path.toUtf8().constData());
        if (node) {
            // Only disable export if WE created it (preserve pre-existing links until deletion)
            // Note: The file is being deleted anyway, but this is cleaner
            if (task.newlyExportedPaths.contains(path)) {
                sourceApi->disableExport(node);
            }

            // Delete the node
            class DeleteListener : public mega::MegaRequestListener {
            public:
                bool finished = false;
                void onRequestFinish(mega::MegaApi*, mega::MegaRequest*, mega::MegaError*) override {
                    finished = true;
                }
            };

            DeleteListener listener;
            sourceApi->remove(node, &listener);

            // Wait for deletion with proper event loop
            waitForCondition(10000, 100,
                [&]() { return listener.finished; },
                [](int) { /* No progress needed */ });

            delete node;
        }
    }

    qDebug() << "CrossAccountTransferManager: Deleted source for" << task.transfer.id;
    finishTransfer(task.transfer.id, true);
}

void CrossAccountTransferManager::stepCleanupExports(TransferTask& task)
{
    // For Copy operations: disable ONLY the public links WE created (security cleanup)
    // Don't touch pre-existing public links that the user may have intentionally shared
    // Move operations already do cleanup in stepDeleteSource before deleting

    // If we didn't create any new exports, nothing to clean up
    if (task.newlyExportedPaths.isEmpty()) {
        qDebug() << "CrossAccountTransferManager: No newly created exports to clean up for" << task.transfer.id;
        finishTransfer(task.transfer.id, true);
        return;
    }

    mega::MegaApi* sourceApi = m_sessionPool->getSession(task.transfer.sourceAccountId);
    if (!sourceApi) {
        // Can't cleanup, but transfer was successful
        qWarning() << "CrossAccountTransferManager: Cannot cleanup exports, source session unavailable";
        finishTransfer(task.transfer.id, true);
        return;
    }

    int cleanedCount = 0;

    // Only disable exports for paths WE created (not pre-existing ones)
    for (const QString& path : task.newlyExportedPaths) {
        mega::MegaNode* node = sourceApi->getNodeByPath(path.toUtf8().constData());
        if (node) {
            // Disable the export (removes public link)
            class DisableExportListener : public mega::MegaRequestListener {
            public:
                bool finished = false;
                void onRequestFinish(mega::MegaApi*, mega::MegaRequest*, mega::MegaError*) override {
                    finished = true;
                }
            };

            DisableExportListener listener;
            sourceApi->disableExport(node, &listener);

            // Wait briefly for completion with proper event loop (not critical if it times out)
            waitForCondition(5000, 50,
                [&]() { return listener.finished; },
                [](int) { /* No progress needed */ });

            delete node;
            cleanedCount++;
        }
    }

    qDebug() << "CrossAccountTransferManager: Disabled" << cleanedCount
             << "newly-created exports for" << task.transfer.id
             << "(preserved" << (task.tempLinks.size() - cleanedCount) << "pre-existing links)";
    finishTransfer(task.transfer.id, true);
}

void CrossAccountTransferManager::finishTransfer(const QString& transferId, bool success, const QString& error)
{
    if (!m_activeTasks.contains(transferId)) {
        m_currentConcurrent--;
        processNextInQueue();
        return;
    }

    TransferTask task = m_activeTasks.take(transferId);
    m_currentConcurrent--;

    task.transfer.status = success ? CrossAccountTransfer::Completed : CrossAccountTransfer::Failed;
    if (!success) {
        task.transfer.errorMessage = error;
        task.transfer.canRetry = (task.transfer.retryCount < 3);
    }

    m_logStore->updateTransfer(task.transfer);

    if (success) {
        qDebug() << "CrossAccountTransferManager: Transfer completed" << transferId;
        emit transferCompleted(task.transfer);
    } else {
        qDebug() << "CrossAccountTransferManager: Transfer failed" << transferId << "-" << error;
        emit transferFailed(task.transfer);
    }

    // Process next in queue
    QTimer::singleShot(0, this, &CrossAccountTransferManager::processNextInQueue);
}

void CrossAccountTransferManager::onTransferStepComplete(const QString& transferId, bool success, const QString& error)
{
    if (!success) {
        finishTransfer(transferId, false, error);
        return;
    }

    if (m_activeTasks.contains(transferId)) {
        executeTransfer(transferId);
    }
}

QString CrossAccountTransferManager::generateTransferId() const
{
    return "xfr-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
}

qint64 CrossAccountTransferManager::calculateTotalSize(mega::MegaApi* api, const QStringList& paths) const
{
    qint64 total = 0;

    for (const QString& path : paths) {
        mega::MegaNode* node = api->getNodeByPath(path.toUtf8().constData());
        if (node) {
            if (node->isFolder()) {
                total += api->getSize(node);
            } else {
                total += node->getSize();
            }
            delete node;
        }
    }

    return total;
}

int CrossAccountTransferManager::countFiles(mega::MegaApi* api, const QStringList& paths) const
{
    int count = 0;

    for (const QString& path : paths) {
        mega::MegaNode* node = api->getNodeByPath(path.toUtf8().constData());
        if (node) {
            if (node->isFolder()) {
                count += api->getNumChildFiles(node);
            } else {
                count += 1;
            }
            delete node;
        }
    }

    return count;
}

} // namespace MegaCustom
