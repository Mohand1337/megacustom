#include "controllers/TransferController.h"
#include "core/MegaManager.h"
#include "operations/FileOperations.h"
#include "operations/FolderManager.h"
#include "megaapi.h"
#include <QDebug>
#include <QTimer>
#include <QFileInfo>
#include <QUuid>
#include <QtConcurrent>
#include <atomic>
#include <mutex>
#include <map>

namespace MegaCustom {

// Transfer tracking structure
struct TransferItem {
    QString transferId;
    QString type;  // "upload" or "download"
    QString sourcePath;
    QString destPath;
    qint64 totalBytes;
    qint64 transferredBytes;
    qint64 speed;
    int progressPercent;
    bool active;
    bool completed;
    bool failed;
    QString errorMessage;
};

// Private implementation for managing transfers
class TransferControllerPrivate {
public:
    std::atomic<int> activeTransferCount{0};
    std::unique_ptr<FileOperations> fileOps;
    mega::MegaApi* megaApi;
    TransferController* controller;

    // Transfer tracking
    std::map<QString, TransferItem> transfers;
    std::mutex transfersMutex;

    // Queue counters
    int pendingCount = 0;
    int completedCount = 0;
    int failedCount = 0;

    // Global speed tracking
    qint64 totalUploadSpeed = 0;
    qint64 totalDownloadSpeed = 0;

    TransferControllerPrivate(mega::MegaApi* api, TransferController* ctrl)
        : megaApi(api), controller(ctrl) {
        if (megaApi) {
            fileOps = std::make_unique<FileOperations>(megaApi);

            // Set up progress callback to emit Qt signals
            fileOps->setProgressCallback([this](const TransferProgress& progress) {
                handleProgress(progress);
            });

            // Set up completion callback
            fileOps->setCompletionCallback([this](const TransferResult& result) {
                handleCompletion(result);
            });
        }
    }

    QString generateTransferId() {
        return QUuid::createUuid().toString(QUuid::WithoutBraces).left(16);
    }

    void handleProgress(const TransferProgress& progress) {
        QString fileName = QString::fromStdString(progress.fileName);
        QString transferId;
        QString transferType;

        // Find the transfer by filename
        {
            std::lock_guard<std::mutex> lock(transfersMutex);
            for (auto& [id, item] : transfers) {
                if (item.sourcePath.endsWith(fileName) || item.destPath.endsWith(fileName)) {
                    transferId = id;
                    transferType = item.type;
                    item.transferredBytes = progress.bytesTransferred;
                    item.totalBytes = progress.totalBytes;
                    item.speed = static_cast<qint64>(progress.speed);
                    item.progressPercent = progress.progressPercentage;
                    break;
                }
            }

            // Calculate global speeds by summing all active transfers
            totalUploadSpeed = 0;
            totalDownloadSpeed = 0;
            for (const auto& [id, item] : transfers) {
                if (item.active && !item.completed && !item.failed) {
                    if (item.type == "upload") {
                        totalUploadSpeed += item.speed;
                    } else {
                        totalDownloadSpeed += item.speed;
                    }
                }
            }
        }

        if (!transferId.isEmpty()) {
            int timeRemaining = 0;
            if (progress.speed > 0) {
                timeRemaining = static_cast<int>(
                    (progress.totalBytes - progress.bytesTransferred) / progress.speed);
            }

            qint64 upSpeed = totalUploadSpeed;
            qint64 downSpeed = totalDownloadSpeed;

            // Emit on Qt thread
            QTimer::singleShot(0, controller, [=]() {
                emit controller->transferProgress(
                    transferId,
                    progress.bytesTransferred,
                    progress.totalBytes,
                    static_cast<qint64>(progress.speed),
                    timeRemaining
                );

                // Also emit global speed update
                emit controller->globalSpeedUpdate(upSpeed, downSpeed);
            });
        }
    }

    void handleCompletion(const TransferResult& result) {
        QString fileName = QString::fromStdString(result.fileName);
        QString transferId;
        QString path;

        // Find and update the transfer
        {
            std::lock_guard<std::mutex> lock(transfersMutex);
            for (auto& [id, item] : transfers) {
                if (item.sourcePath.endsWith(fileName) || item.destPath.endsWith(fileName)) {
                    transferId = id;
                    path = item.sourcePath;
                    item.completed = result.success;
                    item.failed = !result.success;
                    item.active = false;
                    item.errorMessage = QString::fromStdString(result.errorMessage);

                    if (result.success) {
                        completedCount++;
                    } else {
                        failedCount++;
                    }
                    break;
                }
            }

            // Update active count
            activeTransferCount--;
            if (pendingCount > 0) pendingCount--;
        }

        // Emit signals on Qt thread, then clean up completed transfer
        QTimer::singleShot(0, controller, [=]() {
            if (result.success) {
                emit controller->transferComplete(transferId);
                emit controller->transferCompleted(path);
            } else {
                emit controller->transferFailed(path, QString::fromStdString(result.errorMessage));
            }

            emit controller->queueStatusChanged(
                activeTransferCount.load(), pendingCount, completedCount, failedCount);

            // Clean up completed/failed transfer from map to prevent memory leak
            if (!transferId.isEmpty()) {
                std::lock_guard<std::mutex> lock(transfersMutex);
                transfers.erase(transferId);
            }
        });
    }

    TransferItem* addTransfer(const QString& type, const QString& source, const QString& dest, qint64 size) {
        QString id = generateTransferId();

        TransferItem item;
        item.transferId = id;
        item.type = type;
        item.sourcePath = source;
        item.destPath = dest;
        item.totalBytes = size;
        item.transferredBytes = 0;
        item.speed = 0;
        item.progressPercent = 0;
        item.active = true;
        item.completed = false;
        item.failed = false;

        std::lock_guard<std::mutex> lock(transfersMutex);
        transfers[id] = item;
        pendingCount++;

        return &transfers[id];
    }
};

TransferController::TransferController(void* api) : QObject() {
    mega::MegaApi* megaApi = static_cast<mega::MegaApi*>(api);
    m_d = std::make_unique<TransferControllerPrivate>(megaApi, this);
    qDebug() << "TransferController constructed (with real SDK backend and progress tracking)";
}

TransferController::~TransferController() = default;

bool TransferController::hasActiveTransfers() const {
    return m_d && m_d->activeTransferCount > 0;
}

void TransferController::cancelAllTransfers() {
    qDebug() << "Canceling all transfers...";
    if (m_d && m_d->megaApi) {
        m_d->megaApi->cancelTransfers(mega::MegaTransfer::TYPE_UPLOAD);
        m_d->megaApi->cancelTransfers(mega::MegaTransfer::TYPE_DOWNLOAD);
        m_d->activeTransferCount = 0;

        // Clear transfer tracking
        std::lock_guard<std::mutex> lock(m_d->transfersMutex);
        m_d->transfers.clear();
        m_d->pendingCount = 0;
    }
}

void TransferController::uploadFile(const QString& localPath, const QString& remotePath) {
    qDebug() << "Uploading file:" << localPath << "to" << remotePath;

    if (!m_d || !m_d->fileOps) {
        emit transferFailed(localPath, "Transfer system not initialized");
        return;
    }

    if (!m_d->megaApi->isLoggedIn()) {
        emit transferFailed(localPath, "Not logged in");
        return;
    }

    // Get file size
    QFileInfo fileInfo(localPath);
    qint64 fileSize = fileInfo.size();

    // Track the transfer
    auto* transfer = m_d->addTransfer("upload", localPath, remotePath, fileSize);
    QString transferId = transfer->transferId;

    // Emit signals
    emit transferStarted(localPath);
    emit addTransfer("upload", localPath, remotePath, fileSize);

    m_d->activeTransferCount++;
    emit queueStatusChanged(m_d->activeTransferCount.load(), m_d->pendingCount,
                           m_d->completedCount, m_d->failedCount);

    // Run upload in background thread using QtConcurrent (safe lifecycle management)
    QtConcurrent::run([this, localPath, remotePath, transferId]() {
        UploadConfig config;
        config.preserveTimestamp = true;
        config.detectDuplicates = false;  // Allow overwrites

        auto result = m_d->fileOps->uploadFile(
            localPath.toStdString(),
            remotePath.toStdString(),
            config
        );

        // Completion is handled by the callback
    });
}

void TransferController::uploadFolder(const QString& localPath, const QString& remotePath) {
    qDebug() << "Uploading folder:" << localPath << "to" << remotePath;

    if (!m_d || !m_d->fileOps) {
        emit transferFailed(localPath, "Transfer system not initialized");
        return;
    }

    if (!m_d->megaApi->isLoggedIn()) {
        emit transferFailed(localPath, "Not logged in");
        return;
    }

    // Track the folder upload
    m_d->addTransfer("upload", localPath, remotePath, 0);

    emit transferStarted(localPath);
    m_d->activeTransferCount++;

    // Run upload in background thread using QtConcurrent (safe lifecycle management)
    QtConcurrent::run([this, localPath, remotePath]() {
        UploadConfig config;
        config.preserveTimestamp = true;

        auto results = m_d->fileOps->uploadDirectory(
            localPath.toStdString(),
            remotePath.toStdString(),
            true,
            config
        );

        m_d->activeTransferCount--;

        bool allSuccess = true;
        QString errorMsg;
        for (const auto& result : results) {
            if (!result.success) {
                allSuccess = false;
                errorMsg = QString::fromStdString(result.errorMessage);
                break;
            }
        }

        QTimer::singleShot(0, this, [this, localPath, allSuccess, errorMsg]() {
            if (allSuccess) {
                emit transferCompleted(localPath);
            } else {
                emit transferFailed(localPath, errorMsg);
            }
        });
    });
}

void TransferController::downloadFile(const QString& remotePath, const QString& localPath) {
    qDebug() << "Downloading file:" << remotePath << "to" << localPath;

    if (!m_d || !m_d->fileOps || !m_d->megaApi) {
        emit transferFailed(remotePath, "Transfer system not initialized");
        return;
    }

    if (!m_d->megaApi->isLoggedIn()) {
        emit transferFailed(remotePath, "Not logged in");
        return;
    }

    // Get remote file size
    mega::MegaNode* node = m_d->megaApi->getNodeByPath(remotePath.toStdString().c_str());
    qint64 fileSize = 0;
    if (node) {
        fileSize = node->getSize();
    }

    // Track the transfer
    auto* transfer = m_d->addTransfer("download", remotePath, localPath, fileSize);
    QString transferId = transfer->transferId;

    emit transferStarted(remotePath);
    emit addTransfer("download", remotePath, localPath, fileSize);

    m_d->activeTransferCount++;
    emit queueStatusChanged(m_d->activeTransferCount.load(), m_d->pendingCount,
                           m_d->completedCount, m_d->failedCount);

    // Run download in background thread using QtConcurrent (safe lifecycle management)
    QtConcurrent::run([this, remotePath, localPath, node]() {
        if (!node) {
            m_d->activeTransferCount--;
            QTimer::singleShot(0, this, [this, remotePath]() {
                emit transferFailed(remotePath, "Remote file not found");
            });
            return;
        }

        DownloadConfig config;
        config.resumeIfExists = true;
        config.verifyChecksum = true;

        auto result = m_d->fileOps->downloadFile(
            node,
            localPath.toStdString(),
            config
        );

        delete node;
        // Completion is handled by the callback
    });
}

} // namespace MegaCustom

#include "moc_TransferController.cpp"
