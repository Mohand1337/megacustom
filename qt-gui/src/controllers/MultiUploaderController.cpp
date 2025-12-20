#include "MultiUploaderController.h"
#include "TransferProgressListener.h"
#include "megaapi.h"
#include <QDir>
#include <QFileInfo>
#include <QDirIterator>
#include <QRegularExpression>
#include <QMutexLocker>
#include <QtConcurrent>
#include <QDebug>

namespace MegaCustom {

MultiUploaderController::MultiUploaderController(void* megaApi, QObject* parent)
    : QObject(parent)
    , m_megaApi(megaApi)
{
}

MultiUploaderController::~MultiUploaderController() {
    if (m_isUploading) {
        cancelUpload();
    }
}

int MultiUploaderController::getPendingTaskCount() const {
    QMutexLocker locker(&m_tasksMutex);
    int count = 0;
    for (const auto& task : m_tasks) {
        if (task.status == UploadTask::Status::PENDING ||
            task.status == UploadTask::Status::PAUSED) {
            count++;
        }
    }
    return count;
}

int MultiUploaderController::getCompletedTaskCount() const {
    QMutexLocker locker(&m_tasksMutex);
    int count = 0;
    for (const auto& task : m_tasks) {
        if (task.status == UploadTask::Status::COMPLETED) {
            count++;
        }
    }
    return count;
}

void MultiUploaderController::addFiles(const QStringList& filePaths) {
    for (const QString& path : filePaths) {
        QFileInfo fi(path);
        if (fi.exists() && fi.isFile() && !m_sourceFiles.contains(path)) {
            m_sourceFiles.append(path);
            qint64 size = fi.size();
            m_fileSizes[path] = size;
            m_totalSourceBytes += size;
        }
    }

    emit sourceFilesChanged(m_sourceFiles.size(), m_totalSourceBytes);
    qDebug() << "Added files, total count:" << m_sourceFiles.size();
}

void MultiUploaderController::addFolder(const QString& folderPath, bool recursive) {
    QDir dir(folderPath);
    if (!dir.exists()) {
        emit error("Add Folder", "Folder does not exist: " + folderPath);
        return;
    }

    QStringList files;
    QDirIterator::IteratorFlags flags = recursive
        ? QDirIterator::Subdirectories
        : QDirIterator::NoIteratorFlags;

    QDirIterator it(folderPath, QDir::Files, flags);
    while (it.hasNext()) {
        files.append(it.next());
    }

    addFiles(files);
}

void MultiUploaderController::removeFile(const QString& filePath) {
    int index = m_sourceFiles.indexOf(filePath);
    if (index >= 0) {
        m_sourceFiles.removeAt(index);
        if (m_fileSizes.contains(filePath)) {
            m_totalSourceBytes -= m_fileSizes[filePath];
            m_fileSizes.remove(filePath);
        }
        emit sourceFilesChanged(m_sourceFiles.size(), m_totalSourceBytes);
    }
}

void MultiUploaderController::clearFiles() {
    m_sourceFiles.clear();
    m_fileSizes.clear();
    m_totalSourceBytes = 0;
    emit sourceFilesChanged(0, 0);
}

void MultiUploaderController::addDestination(const QString& remotePath) {
    if (!m_destinations.contains(remotePath)) {
        m_destinations.append(remotePath);
        emit destinationsChanged(m_destinations);
        qDebug() << "Added destination:" << remotePath;
    }
}

void MultiUploaderController::removeDestination(const QString& remotePath) {
    int index = m_destinations.indexOf(remotePath);
    if (index >= 0) {
        m_destinations.removeAt(index);

        // Also remove rules pointing to this destination
        for (int i = m_rules.size() - 1; i >= 0; --i) {
            if (m_rules[i].destination == remotePath) {
                m_rules.removeAt(i);
            }
        }

        emit destinationsChanged(m_destinations);
        emit rulesChanged(m_rules.size());
    }
}

void MultiUploaderController::clearDestinations() {
    m_destinations.clear();
    m_rules.clear();
    emit destinationsChanged(m_destinations);
    emit rulesChanged(0);
}

void MultiUploaderController::addRule(DistributionRule::RuleType type,
                                       const QString& pattern,
                                       const QString& destination) {
    if (!m_destinations.contains(destination)) {
        emit error("Add Rule", "Destination not in list: " + destination);
        return;
    }

    DistributionRule rule;
    rule.id = generateRuleId();
    rule.type = type;
    rule.pattern = pattern;
    rule.destination = destination;
    rule.enabled = true;

    m_rules.append(rule);
    emit rulesChanged(m_rules.size());
    qDebug() << "Added rule:" << pattern << "->" << destination;
}

void MultiUploaderController::removeRule(int ruleId) {
    for (int i = 0; i < m_rules.size(); ++i) {
        if (m_rules[i].id == ruleId) {
            m_rules.removeAt(i);
            emit rulesChanged(m_rules.size());
            return;
        }
    }
}

void MultiUploaderController::updateRule(int ruleId, const QString& pattern,
                                          const QString& destination) {
    for (auto& rule : m_rules) {
        if (rule.id == ruleId) {
            rule.pattern = pattern;
            rule.destination = destination;
            emit rulesChanged(m_rules.size());
            return;
        }
    }
}

void MultiUploaderController::setRuleEnabled(int ruleId, bool enabled) {
    for (auto& rule : m_rules) {
        if (rule.id == ruleId) {
            rule.enabled = enabled;
            emit rulesChanged(m_rules.size());
            return;
        }
    }
}

void MultiUploaderController::clearRules() {
    m_rules.clear();
    emit rulesChanged(0);
}

QString MultiUploaderController::determineDestination(const QString& filePath, qint64 fileSize) {
    QFileInfo fi(filePath);
    QString extension = fi.suffix().toLower();
    QString fileName = fi.fileName();

    // Check each rule in order
    for (const auto& rule : m_rules) {
        if (!rule.enabled) continue;

        bool matches = false;

        switch (rule.type) {
            case DistributionRule::RuleType::BY_EXTENSION: {
                // Pattern is comma-separated list of extensions
                QStringList extensions = rule.pattern.split(',', Qt::SkipEmptyParts);
                for (QString ext : extensions) {
                    ext = ext.trimmed().toLower();
                    if (ext.startsWith('.')) ext = ext.mid(1);
                    if (extension == ext) {
                        matches = true;
                        break;
                    }
                }
                break;
            }
            case DistributionRule::RuleType::BY_SIZE: {
                // Pattern is "min-max" in MB, e.g., "0-10" or "10-100"
                QStringList parts = rule.pattern.split('-');
                if (parts.size() == 2) {
                    qint64 minMB = parts[0].trimmed().toLongLong();
                    qint64 maxMB = parts[1].trimmed().toLongLong();
                    qint64 sizeMB = fileSize / (1024 * 1024);
                    matches = (sizeMB >= minMB && sizeMB <= maxMB);
                }
                break;
            }
            case DistributionRule::RuleType::BY_NAME: {
                // Pattern is a wildcard pattern, e.g., "report_*" or "*_backup*"
                QRegularExpression rx(QRegularExpression::wildcardToRegularExpression(rule.pattern),
                                     QRegularExpression::CaseInsensitiveOption);
                matches = rx.match(fileName).hasMatch();
                break;
            }
            case DistributionRule::RuleType::DEFAULT:
                matches = true;
                break;
        }

        if (matches) {
            return rule.destination;
        }
    }

    // Return first destination as fallback, or root
    return m_destinations.isEmpty() ? "/" : m_destinations.first();
}

void MultiUploaderController::createUploadTasks() {
    QMutexLocker locker(&m_tasksMutex);
    m_tasks.clear();

    for (const QString& filePath : m_sourceFiles) {
        QFileInfo fi(filePath);
        qint64 fileSize = m_fileSizes.value(filePath, fi.size());

        UploadTask task;
        task.id = generateTaskId();
        task.localPath = filePath;
        task.fileName = fi.fileName();
        task.fileSize = fileSize;
        task.remotePath = determineDestination(filePath, fileSize);
        task.status = UploadTask::Status::PENDING;

        m_tasks.append(task);
        emit taskCreated(task.id, task.fileName, task.remotePath);
    }

    qDebug() << "Created" << m_tasks.size() << "upload tasks";
}

void MultiUploaderController::startUpload() {
    if (m_isUploading) {
        qDebug() << "Upload already in progress";
        return;
    }

    if (m_sourceFiles.isEmpty()) {
        emit error("Start Upload", "No source files selected");
        return;
    }

    if (m_destinations.isEmpty()) {
        emit error("Start Upload", "No destinations configured");
        return;
    }

    m_isUploading = true;
    m_isPaused = false;
    m_cancelRequested = false;
    m_successCount = 0;
    m_failCount = 0;
    m_skipCount = 0;
    m_bytesUploaded = 0;
    m_currentTaskIndex = -1;

    // Create tasks based on rules
    createUploadTasks();

    emit uploadStarted(m_tasks.size());

    // Start processing
    processNextTask();
}

void MultiUploaderController::pauseUpload() {
    if (!m_isUploading || m_isPaused) return;

    m_isPaused = true;

    // Mark current task as paused
    {
        QMutexLocker locker(&m_tasksMutex);
        if (m_currentTaskIndex >= 0 && m_currentTaskIndex < m_tasks.size()) {
            m_tasks[m_currentTaskIndex].status = UploadTask::Status::PAUSED;
            emit taskStatusChanged(m_tasks[m_currentTaskIndex].id, "Paused");
        }
    }

    emit uploadPaused();
    qDebug() << "Upload paused";
}

void MultiUploaderController::resumeUpload() {
    if (!m_isUploading || !m_isPaused) return;

    m_isPaused = false;

    // Resume current task
    {
        QMutexLocker locker(&m_tasksMutex);
        if (m_currentTaskIndex >= 0 && m_currentTaskIndex < m_tasks.size()) {
            m_tasks[m_currentTaskIndex].status = UploadTask::Status::PENDING;
        }
    }

    processNextTask();
    qDebug() << "Upload resumed";
}

void MultiUploaderController::cancelUpload() {
    m_cancelRequested = true;
    m_isPaused = false;

    // Cancel current upload via Mega API
    mega::MegaApi* api = static_cast<mega::MegaApi*>(m_megaApi);
    if (api) {
        api->cancelTransfers(mega::MegaTransfer::TYPE_UPLOAD);
    }

    // Mark remaining tasks as cancelled
    {
        QMutexLocker locker(&m_tasksMutex);
        for (auto& task : m_tasks) {
            if (task.status == UploadTask::Status::PENDING ||
                task.status == UploadTask::Status::UPLOADING ||
                task.status == UploadTask::Status::PAUSED) {
                task.status = UploadTask::Status::CANCELLED;
                emit taskStatusChanged(task.id, "Cancelled");
            }
        }
    }

    m_isUploading = false;
    emit uploadCancelled();
    qDebug() << "Upload cancelled";
}

void MultiUploaderController::clearCompletedTasks() {
    QMutexLocker locker(&m_tasksMutex);
    for (int i = m_tasks.size() - 1; i >= 0; --i) {
        if (m_tasks[i].status == UploadTask::Status::COMPLETED ||
            m_tasks[i].status == UploadTask::Status::CANCELLED) {
            m_tasks.removeAt(i);
        }
    }
}

void MultiUploaderController::retryFailedTask(int taskId) {
    bool shouldStartProcessing = false;
    {
        QMutexLocker locker(&m_tasksMutex);
        for (auto& task : m_tasks) {
            if (task.id == taskId && task.status == UploadTask::Status::FAILED) {
                task.status = UploadTask::Status::PENDING;
                task.bytesUploaded = 0;
                task.errorMessage.clear();
                emit taskStatusChanged(task.id, "Pending");

                if (!m_isUploading) {
                    m_isUploading = true;
                    m_isPaused = false;
                    m_cancelRequested = false;
                    shouldStartProcessing = true;
                }
                break;
            }
        }
    }
    if (shouldStartProcessing) {
        processNextTask();
    }
}

void MultiUploaderController::retryAllFailed() {
    bool shouldStartProcessing = false;
    {
        QMutexLocker locker(&m_tasksMutex);
        for (auto& task : m_tasks) {
            if (task.status == UploadTask::Status::FAILED) {
                task.status = UploadTask::Status::PENDING;
                task.bytesUploaded = 0;
                task.errorMessage.clear();
                emit taskStatusChanged(task.id, "Pending");
            }
        }
    }

    if (!m_isUploading && getPendingTaskCount() > 0) {
        m_isUploading = true;
        m_isPaused = false;
        m_cancelRequested = false;
        m_currentTaskIndex = -1;
        shouldStartProcessing = true;
    }
    if (shouldStartProcessing) {
        processNextTask();
    }
}

void MultiUploaderController::processNextTask() {
    if (m_cancelRequested || m_isPaused) return;

    int taskIndex = -1;
    {
        QMutexLocker locker(&m_tasksMutex);
        // Find next pending task
        for (int i = 0; i < m_tasks.size(); ++i) {
            if (m_tasks[i].status == UploadTask::Status::PENDING) {
                taskIndex = i;
                break;
            }
        }
        m_currentTaskIndex = taskIndex;
    }

    if (taskIndex < 0) {
        // All tasks completed
        m_isUploading = false;
        emit uploadComplete(m_successCount, m_failCount, m_skipCount);
        qDebug() << "Upload complete. Success:" << m_successCount
                 << "Failed:" << m_failCount << "Skipped:" << m_skipCount;
        return;
    }

    QMutexLocker locker(&m_tasksMutex);
    startFileUpload(m_tasks[taskIndex]);
}

void MultiUploaderController::startFileUpload(UploadTask& task) {
    task.status = UploadTask::Status::UPLOADING;
    emit taskStatusChanged(task.id, "Uploading");

    mega::MegaApi* api = static_cast<mega::MegaApi*>(m_megaApi);
    if (!api) {
        task.status = UploadTask::Status::FAILED;
        task.errorMessage = "API not available";
        m_failCount++;
        emit taskCompleted(task.id, false, task.errorMessage);
        QMetaObject::invokeMethod(this, "processNextTask", Qt::QueuedConnection);
        return;
    }

    int taskId = task.id;
    QString localPath = task.localPath;
    QString remotePath = task.remotePath;
    qint64 fileSize = task.fileSize;

    // Find the remote folder (this is synchronous for simplicity, but quick)
    std::unique_ptr<mega::MegaNode> parentNode(api->getNodeByPath(remotePath.toUtf8().constData()));

    if (!parentNode) {
        QMutexLocker locker(&m_tasksMutex);
        for (auto& t : m_tasks) {
            if (t.id == taskId) {
                t.status = UploadTask::Status::FAILED;
                t.errorMessage = "Destination folder not found";
                m_failCount++;
                emit taskCompleted(taskId, false, t.errorMessage);
                break;
            }
        }
        QMetaObject::invokeMethod(this, "processNextTask", Qt::QueuedConnection);
        return;
    }

    // Create a transfer listener for real progress
    auto* listener = new TransferProgressListener(this);
    listener->setTaskId(taskId);

    // Connect progress signal
    connect(listener, &TransferProgressListener::progressUpdated,
            this, [this, fileSize](int tid, qint64 transferred, qint64 total, double speed) {
        Q_UNUSED(total);
        emit taskProgress(tid, transferred, fileSize, speed);

        // Update task bytes
        QMutexLocker locker(&m_tasksMutex);
        for (auto& t : m_tasks) {
            if (t.id == tid) {
                t.bytesUploaded = transferred;
                break;
            }
        }
    });

    // Connect completion signal
    connect(listener, &TransferProgressListener::transferFinished,
            this, [this, fileSize](int tid, bool success, const QString& error) {
        {
            QMutexLocker locker(&m_tasksMutex);
            for (auto& t : m_tasks) {
                if (t.id == tid) {
                    if (success) {
                        t.status = UploadTask::Status::COMPLETED;
                        t.bytesUploaded = fileSize;
                        m_successCount++;
                        m_bytesUploaded += fileSize;
                    } else {
                        t.status = UploadTask::Status::FAILED;
                        t.errorMessage = error;
                        m_failCount++;
                    }
                    break;
                }
            }
        }

        emit taskCompleted(tid, success, success ? "Upload completed" : error);

        // Update overall progress
        int completedCount;
        int totalCount;
        {
            QMutexLocker locker(&m_tasksMutex);
            completedCount = m_successCount + m_failCount;
            totalCount = m_tasks.size();
        }
        emit uploadProgress(completedCount, totalCount, m_bytesUploaded, m_totalSourceBytes);

        processNextTask();
    });

    // Start the upload with listener
    api->startUpload(localPath.toUtf8().constData(),
                    parentNode.get(),
                    nullptr,  // filename (use original)
                    0,        // mtime (use file's mtime)
                    nullptr,  // appData
                    false,    // isSourceTemporary
                    false,    // startFirst
                    nullptr,  // cancelToken
                    listener);
}

int MultiUploaderController::generateTaskId() {
    return m_nextTaskId++;
}

int MultiUploaderController::generateRuleId() {
    return m_nextRuleId++;
}

} // namespace MegaCustom
