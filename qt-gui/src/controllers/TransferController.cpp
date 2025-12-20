/**
 * TransferController implementation
 * Manages file uploads and downloads with progress tracking
 */

#include "TransferController.h"
#include "TransferProgressListener.h"
#include "operations/FileOperations.h"
#include "megaapi.h"
#include <QDebug>
#include <QTimer>
#include <QFileInfo>
#include <QUuid>
#include <QMutex>
#include <QMutexLocker>
#include <QtConcurrent>

namespace MegaCustom {

// Transfer tracking structure
struct TransferItem {
    QString transferId;
    QString type;  // "upload" or "download"
    QString sourcePath;
    QString destPath;
    qint64 totalBytes = 0;
    qint64 transferredBytes = 0;
    qint64 speed = 0;
    int progressPercent = 0;
    bool active = false;
    bool completed = false;
    bool failed = false;
    QString errorMessage;
};

// Private implementation for managing transfers
class TransferControllerPrivate {
public:
    std::atomic<int> activeTransferCount{0};
    mega::MegaApi* megaApi = nullptr;
    TransferController* controller = nullptr;

    // Transfer tracking
    QMap<QString, TransferItem> transfers;
    mutable QMutex transfersMutex;

    // Queue counters
    std::atomic<int> pendingCount{0};
    std::atomic<int> completedCount{0};
    std::atomic<int> failedCount{0};

    // Global speed tracking
    std::atomic<qint64> totalUploadSpeed{0};
    std::atomic<qint64> totalDownloadSpeed{0};

    TransferControllerPrivate(mega::MegaApi* api, TransferController* ctrl)
        : megaApi(api), controller(ctrl) {
    }

    QString generateTransferId() {
        return QUuid::createUuid().toString(QUuid::WithoutBraces).left(16);
    }

    void updateGlobalSpeeds() {
        QMutexLocker locker(&transfersMutex);
        qint64 upSpeed = 0;
        qint64 downSpeed = 0;
        for (const auto& item : transfers) {
            if (item.active && !item.completed && !item.failed) {
                if (item.type == "upload") {
                    upSpeed += item.speed;
                } else {
                    downSpeed += item.speed;
                }
            }
        }
        totalUploadSpeed = upSpeed;
        totalDownloadSpeed = downSpeed;
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

        QMutexLocker locker(&transfersMutex);
        transfers[id] = item;
        pendingCount++;

        return &transfers[id];
    }

    void updateTransferProgress(const QString& transferId, qint64 transferred, qint64 total, qint64 speed) {
        QMutexLocker locker(&transfersMutex);
        if (transfers.contains(transferId)) {
            transfers[transferId].transferredBytes = transferred;
            transfers[transferId].totalBytes = total;
            transfers[transferId].speed = speed;
            if (total > 0) {
                transfers[transferId].progressPercent = static_cast<int>((transferred * 100) / total);
            }
        }
    }

    void completeTransfer(const QString& transferId, bool success, const QString& error) {
        QMutexLocker locker(&transfersMutex);
        if (transfers.contains(transferId)) {
            transfers[transferId].completed = success;
            transfers[transferId].failed = !success;
            transfers[transferId].active = false;
            transfers[transferId].errorMessage = error;

            if (success) {
                completedCount++;
            } else {
                failedCount++;
            }
            activeTransferCount--;
            if (pendingCount > 0) pendingCount--;
        }
    }

    void removeTransfer(const QString& transferId) {
        QMutexLocker locker(&transfersMutex);
        transfers.remove(transferId);
    }
};

TransferController::TransferController(void* api)
    : QObject()
    , m_d(std::make_unique<TransferControllerPrivate>(static_cast<mega::MegaApi*>(api), this))
{
    qDebug() << "TransferController initialized";
}

TransferController::~TransferController() {
    if (hasActiveTransfers()) {
        cancelAllTransfers();
    }
}

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
        QMutexLocker locker(&m_d->transfersMutex);
        m_d->transfers.clear();
        m_d->pendingCount = 0;
    }
}

void TransferController::uploadFile(const QString& localPath, const QString& remotePath) {
    qDebug() << "TransferController: Uploading file:" << localPath << "to" << remotePath;

    if (!m_d || !m_d->megaApi) {
        emit transferFailed(localPath, "Transfer system not initialized");
        return;
    }

    if (!m_d->megaApi->isLoggedIn()) {
        emit transferFailed(localPath, "Not logged in");
        return;
    }

    // Get file size
    QFileInfo fileInfo(localPath);
    if (!fileInfo.exists()) {
        emit transferFailed(localPath, "File does not exist");
        return;
    }
    qint64 fileSize = fileInfo.size();

    // Track the transfer
    auto* transfer = m_d->addTransfer("upload", localPath, remotePath, fileSize);
    QString transferId = transfer->transferId;

    // Emit signals
    emit transferStarted(localPath);
    emit addTransfer("upload", localPath, remotePath, fileSize);

    m_d->activeTransferCount++;
    emit queueStatusChanged(m_d->activeTransferCount.load(), m_d->pendingCount.load(),
                           m_d->completedCount.load(), m_d->failedCount.load());

    // Find parent folder
    std::unique_ptr<mega::MegaNode> parentNode(m_d->megaApi->getNodeByPath(remotePath.toUtf8().constData()));
    if (!parentNode) {
        m_d->completeTransfer(transferId, false, "Destination folder not found");
        emit transferFailed(localPath, "Destination folder not found");
        emit queueStatusChanged(m_d->activeTransferCount.load(), m_d->pendingCount.load(),
                               m_d->completedCount.load(), m_d->failedCount.load());
        return;
    }

    // Create listener for progress
    auto* listener = new TransferProgressListener(this);
    listener->setTaskId(0);
    listener->setUserData(transferId);

    connect(listener, &TransferProgressListener::progressUpdated,
            this, [this, transferId, fileSize](int, qint64 transferred, qint64 total, double speed) {
        m_d->updateTransferProgress(transferId, transferred, total, static_cast<qint64>(speed));
        m_d->updateGlobalSpeeds();

        int timeRemaining = 0;
        if (speed > 0) {
            timeRemaining = static_cast<int>((total - transferred) / speed);
        }

        emit transferProgress(transferId, transferred, total, static_cast<qint64>(speed), timeRemaining);
        emit globalSpeedUpdate(m_d->totalUploadSpeed.load(), m_d->totalDownloadSpeed.load());
    });

    connect(listener, &TransferProgressListener::transferFinished,
            this, [this, transferId, localPath](int, bool success, const QString& error) {
        m_d->completeTransfer(transferId, success, error);
        m_d->updateGlobalSpeeds();

        if (success) {
            emit transferComplete(transferId);
            emit transferCompleted(localPath);
        } else {
            emit transferFailed(localPath, error);
        }

        emit queueStatusChanged(m_d->activeTransferCount.load(), m_d->pendingCount.load(),
                               m_d->completedCount.load(), m_d->failedCount.load());
        emit globalSpeedUpdate(m_d->totalUploadSpeed.load(), m_d->totalDownloadSpeed.load());

        // Clean up transfer from map
        QTimer::singleShot(5000, this, [this, transferId]() {
            m_d->removeTransfer(transferId);
        });
    });

    // Start upload with listener
    m_d->megaApi->startUpload(localPath.toUtf8().constData(),
                             parentNode.get(),
                             nullptr,  // filename
                             0,        // mtime
                             nullptr,  // appData
                             false,    // isSourceTemporary
                             false,    // startFirst
                             nullptr,  // cancelToken
                             listener);
}

void TransferController::uploadFolder(const QString& localPath, const QString& remotePath) {
    qDebug() << "TransferController: Uploading folder:" << localPath << "to" << remotePath;

    if (!m_d || !m_d->megaApi) {
        emit transferFailed(localPath, "Transfer system not initialized");
        return;
    }

    if (!m_d->megaApi->isLoggedIn()) {
        emit transferFailed(localPath, "Not logged in");
        return;
    }

    QDir dir(localPath);
    if (!dir.exists()) {
        emit transferFailed(localPath, "Folder does not exist");
        return;
    }

    // Track the folder upload
    auto* transfer = m_d->addTransfer("upload", localPath, remotePath, 0);
    QString transferId = transfer->transferId;

    emit transferStarted(localPath);
    m_d->activeTransferCount++;

    // Get all files in directory
    QStringList files;
    QDirIterator it(localPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        files.append(it.next());
    }

    if (files.isEmpty()) {
        m_d->completeTransfer(transferId, true, QString());
        emit transferCompleted(localPath);
        return;
    }

    // Upload files sequentially in background
    QtConcurrent::run([this, files, remotePath, localPath, transferId]() {
        int completed = 0;
        int failed = 0;
        QString lastError;

        for (const QString& filePath : files) {
            // Get relative path
            QString relativePath = filePath.mid(localPath.length());
            if (relativePath.startsWith('/')) relativePath = relativePath.mid(1);
            QString destPath = remotePath + "/" + QFileInfo(relativePath).path();

            // Find/create parent folder
            std::unique_ptr<mega::MegaNode> parentNode(
                m_d->megaApi->getNodeByPath(destPath.toUtf8().constData()));

            if (!parentNode) {
                // Try to create the folder structure
                m_d->megaApi->createFolder(destPath.toUtf8().constData(),
                                          m_d->megaApi->getRootNode());
                QThread::msleep(500);  // Wait for folder creation
                parentNode.reset(m_d->megaApi->getNodeByPath(destPath.toUtf8().constData()));
            }

            if (parentNode) {
                m_d->megaApi->startUpload(filePath.toUtf8().constData(),
                                         parentNode.get(),
                                         nullptr, 0, nullptr, false, false, nullptr, nullptr);
                completed++;
            } else {
                failed++;
                lastError = "Could not create destination folder";
            }

            // Small delay between files
            QThread::msleep(100);
        }

        // Signal completion
        QMetaObject::invokeMethod(this, [this, localPath, transferId, completed, failed, lastError]() {
            bool success = (failed == 0);
            m_d->completeTransfer(transferId, success, lastError);

            if (success) {
                emit transferCompleted(localPath);
            } else {
                emit transferFailed(localPath, lastError.isEmpty() ?
                    QString("%1 files failed").arg(failed) : lastError);
            }

            emit queueStatusChanged(m_d->activeTransferCount.load(), m_d->pendingCount.load(),
                                   m_d->completedCount.load(), m_d->failedCount.load());
        }, Qt::QueuedConnection);
    });
}

void TransferController::downloadFile(const QString& remotePath, const QString& localPath) {
    qDebug() << "TransferController: Downloading file:" << remotePath << "to" << localPath;

    if (!m_d || !m_d->megaApi) {
        emit transferFailed(remotePath, "Transfer system not initialized");
        return;
    }

    if (!m_d->megaApi->isLoggedIn()) {
        emit transferFailed(remotePath, "Not logged in");
        return;
    }

    // Get remote node
    std::unique_ptr<mega::MegaNode> node(m_d->megaApi->getNodeByPath(remotePath.toUtf8().constData()));
    if (!node) {
        emit transferFailed(remotePath, "Remote file not found");
        return;
    }

    qint64 fileSize = node->getSize();

    // Track the transfer
    auto* transfer = m_d->addTransfer("download", remotePath, localPath, fileSize);
    QString transferId = transfer->transferId;

    emit transferStarted(remotePath);
    emit addTransfer("download", remotePath, localPath, fileSize);

    m_d->activeTransferCount++;
    emit queueStatusChanged(m_d->activeTransferCount.load(), m_d->pendingCount.load(),
                           m_d->completedCount.load(), m_d->failedCount.load());

    // Create listener for progress
    auto* listener = new TransferProgressListener(this);
    listener->setUserData(transferId);

    connect(listener, &TransferProgressListener::progressUpdated,
            this, [this, transferId](int, qint64 transferred, qint64 total, double speed) {
        m_d->updateTransferProgress(transferId, transferred, total, static_cast<qint64>(speed));
        m_d->updateGlobalSpeeds();

        int timeRemaining = 0;
        if (speed > 0) {
            timeRemaining = static_cast<int>((total - transferred) / speed);
        }

        emit transferProgress(transferId, transferred, total, static_cast<qint64>(speed), timeRemaining);
        emit globalSpeedUpdate(m_d->totalUploadSpeed.load(), m_d->totalDownloadSpeed.load());
    });

    connect(listener, &TransferProgressListener::transferFinished,
            this, [this, transferId, remotePath](int, bool success, const QString& error) {
        m_d->completeTransfer(transferId, success, error);
        m_d->updateGlobalSpeeds();

        if (success) {
            emit transferComplete(transferId);
            emit transferCompleted(remotePath);
        } else {
            emit transferFailed(remotePath, error);
        }

        emit queueStatusChanged(m_d->activeTransferCount.load(), m_d->pendingCount.load(),
                               m_d->completedCount.load(), m_d->failedCount.load());
        emit globalSpeedUpdate(m_d->totalUploadSpeed.load(), m_d->totalDownloadSpeed.load());

        QTimer::singleShot(5000, this, [this, transferId]() {
            m_d->removeTransfer(transferId);
        });
    });

    // Ensure local directory exists
    QFileInfo localInfo(localPath);
    QDir().mkpath(localInfo.absolutePath());

    // Start download with listener
    m_d->megaApi->startDownload(node.get(),
                               localPath.toUtf8().constData(),
                               nullptr,  // customName
                               nullptr,  // appData
                               false,    // startFirst
                               nullptr,  // cancelToken
                               mega::MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                               mega::MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N,
                               false,    // undelete
                               listener);
}

} // namespace MegaCustom
