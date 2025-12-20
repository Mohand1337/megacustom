#include "TransferBridge.h"
#include "controllers/TransferController.h"
#include "bridge/BackendModules.h"  // Real CLI modules
#include "utils/Constants.h"
#include <QDebug>
#include <QTimer>
#include <QDateTime>
#include <algorithm>

namespace MegaCustom {

TransferBridge::TransferBridge(QObject* parent)
    : QObject(parent) {
    qDebug() << "TransferBridge: Created transfer management bridge";
}

TransferBridge::~TransferBridge() {
    qDebug() << "TransferBridge: Destroyed";
}

void TransferBridge::setTransferModule(TransferManager* module) {
    m_transferModule = module;
    qDebug() << "TransferBridge: Transfer module set";

    if (m_transferModule) {
        // TODO: Set up callbacks from CLI module
        // m_transferModule->setProgressCallback(
        //     [this](const std::string& id, size_t transferred, size_t total, size_t speed) {
        //         onTransferProgress(QString::fromStdString(id), transferred, total, speed);
        //     });
        // m_transferModule->setStateCallback(
        //     [this](const std::string& id, const std::string& state) {
        //         onTransferStateChange(QString::fromStdString(id), QString::fromStdString(state));
        //     });
    }
}

void TransferBridge::connectToGUI(TransferController* guiController) {
    if (!guiController) {
        qDebug() << "TransferBridge: Cannot connect - null GUI controller";
        return;
    }

    m_guiController = guiController;

    // Disconnect any existing connections
    disconnect(m_guiController, nullptr, this, nullptr);
    disconnect(this, nullptr, m_guiController, nullptr);

    // Connect GUI signals to bridge slots
    connect(m_guiController, &TransferController::addTransfer,
            this, &TransferBridge::handleAddTransfer);

    connect(m_guiController, &TransferController::pauseTransfer,
            this, &TransferBridge::handlePauseTransfer);

    connect(m_guiController, &TransferController::resumeTransfer,
            this, &TransferBridge::handleResumeTransfer);

    connect(m_guiController, &TransferController::cancelTransfer,
            this, &TransferBridge::handleCancelTransfer);

    // Connect bridge signals to GUI signals
    connect(this, &TransferBridge::transferProgress,
            m_guiController, &TransferController::transferProgress);

    connect(this, &TransferBridge::transferCompleted,
            m_guiController, &TransferController::transferComplete);

    connect(this, &TransferBridge::transferFailed,
            [this](const QString& transferId, const QString& error) {
                emit m_guiController->transferFailed(transferId, error);
            });

    connect(this, &TransferBridge::queueStatusChanged,
            m_guiController, &TransferController::queueStatusChanged);

    qDebug() << "TransferBridge: Connected to GUI controller";
}

void TransferBridge::setMaxConcurrentTransfers(int max) {
    if (max > 0 && max <= 10) {
        m_maxConcurrent = max;
        qDebug() << "TransferBridge: Max concurrent transfers set to" << max;
        processQueue();
    }
}

void TransferBridge::handleAddTransfer(const QString& type, const QString& sourcePath,
                                       const QString& destPath, qint64 size) {
    QString transferId = generateTransferId();

    TransferInfo transfer;
    transfer.id = transferId;
    transfer.type = type;
    transfer.sourcePath = sourcePath;
    transfer.destPath = destPath;
    transfer.size = (size > 0) ? size : Constants::DEFAULT_FILE_SIZE_ESTIMATE;
    transfer.transferred = 0;
    transfer.status = "pending";
    transfer.speed = 0;
    transfer.priority = 0;
    transfer.retryCount = 0;
    transfer.startTime = QDateTime::currentDateTime();

    m_transfers[transferId] = transfer;
    m_pendingQueue.enqueue(transferId);

    qDebug() << "TransferBridge: Added" << type << "transfer" << transferId
             << "for" << sourcePath;

    emit transferAdded(transferToVariant(transfer));
    updateQueueStatus();

    // Start processing if we have room
    processQueue();
}

void TransferBridge::handlePauseTransfer(const QString& transferId) {
    if (!m_transfers.contains(transferId)) {
        qDebug() << "TransferBridge: Cannot pause - transfer not found:" << transferId;
        return;
    }

    if (m_activeTransfers.contains(transferId)) {
        // TODO: Call actual CLI pause
        // m_transferModule->pauseTransfer(transferId.toStdString());

        m_activeTransfers.remove(transferId);
        m_pausedTransfers.insert(transferId);
        m_transfers[transferId].status = "paused";

        emit transferPaused(transferId);
        updateQueueStatus();

        // Start next transfer since we freed a slot
        processQueue();
    }
}

void TransferBridge::handleResumeTransfer(const QString& transferId) {
    if (!m_transfers.contains(transferId)) {
        qDebug() << "TransferBridge: Cannot resume - transfer not found:" << transferId;
        return;
    }

    if (m_pausedTransfers.contains(transferId)) {
        m_pausedTransfers.remove(transferId);
        m_pendingQueue.enqueue(transferId);
        m_transfers[transferId].status = "pending";

        emit transferResumed(transferId);
        updateQueueStatus();

        // Try to start it immediately if we have room
        processQueue();
    }
}

void TransferBridge::handleCancelTransfer(const QString& transferId) {
    if (!m_transfers.contains(transferId)) {
        qDebug() << "TransferBridge: Cannot cancel - transfer not found:" << transferId;
        return;
    }

    // TODO: Call actual CLI cancel
    // m_transferModule->cancelTransfer(transferId.toStdString());

    // Remove from all queues
    m_activeTransfers.remove(transferId);
    m_pausedTransfers.remove(transferId);
    // Remove from pending queue
    QQueue<QString> newQueue;
    while (!m_pendingQueue.isEmpty()) {
        QString id = m_pendingQueue.dequeue();
        if (id != transferId) {
            newQueue.enqueue(id);
        }
    }
    m_pendingQueue = newQueue;

    m_transfers[transferId].status = "cancelled";
    m_transfers[transferId].endTime = QDateTime::currentDateTime();

    emit transferCancelled(transferId);
    updateQueueStatus();

    // Start next transfer since we freed a slot
    processQueue();
}

void TransferBridge::handleRetryTransfer(const QString& transferId) {
    if (!m_transfers.contains(transferId)) {
        qDebug() << "TransferBridge: Cannot retry - transfer not found:" << transferId;
        return;
    }

    auto& transfer = m_transfers[transferId];
    if (transfer.status == "failed") {
        m_failedTransfers.remove(transferId);
        transfer.status = "pending";
        transfer.transferred = 0;
        transfer.error.clear();
        transfer.retryCount++;
        m_pendingQueue.enqueue(transferId);

        emit transferAdded(transferToVariant(transfer));
        updateQueueStatus();
        processQueue();
    }
}

void TransferBridge::handlePauseAllTransfers() {
    m_queuePaused = true;

    // Pause all active transfers
    QSet<QString> activeCopy = m_activeTransfers;
    for (const auto& id : activeCopy) {
        handlePauseTransfer(id);
    }
}

void TransferBridge::handleResumeAllTransfers() {
    m_queuePaused = false;

    // Resume all paused transfers
    QSet<QString> pausedCopy = m_pausedTransfers;
    for (const auto& id : pausedCopy) {
        handleResumeTransfer(id);
    }
}

void TransferBridge::handleClearCompleted() {
    QList<QString> toRemove;
    for (const auto& id : m_completedTransfers) {
        toRemove.append(id);
    }

    for (const auto& id : toRemove) {
        m_transfers.remove(id);
        m_completedTransfers.remove(id);
    }

    updateQueueStatus();
    handleGetTransferList();
}

void TransferBridge::handleClearFailed() {
    QList<QString> toRemove;
    for (const auto& id : m_failedTransfers) {
        toRemove.append(id);
    }

    for (const auto& id : toRemove) {
        m_transfers.remove(id);
        m_failedTransfers.remove(id);
    }

    updateQueueStatus();
    handleGetTransferList();
}

void TransferBridge::handleClearAll() {
    // Cancel all active transfers first
    QSet<QString> activeCopy = m_activeTransfers;
    for (const auto& id : activeCopy) {
        handleCancelTransfer(id);
    }

    // Clear everything
    m_transfers.clear();
    m_pendingQueue.clear();
    m_activeTransfers.clear();
    m_pausedTransfers.clear();
    m_completedTransfers.clear();
    m_failedTransfers.clear();

    updateQueueStatus();
    handleGetTransferList();
}

void TransferBridge::handleGetTransferList() {
    QVariantList transferList;

    // Add transfers in order: active, pending, paused, completed, failed
    for (const auto& id : m_activeTransfers) {
        transferList.append(transferToVariant(m_transfers[id]));
    }

    for (const auto& id : m_pendingQueue) {
        transferList.append(transferToVariant(m_transfers[id]));
    }

    for (const auto& id : m_pausedTransfers) {
        transferList.append(transferToVariant(m_transfers[id]));
    }

    for (const auto& id : m_completedTransfers) {
        transferList.append(transferToVariant(m_transfers[id]));
    }

    for (const auto& id : m_failedTransfers) {
        transferList.append(transferToVariant(m_transfers[id]));
    }

    emit transferListUpdated(transferList);
}

void TransferBridge::handleSetTransferPriority(const QString& transferId, int priority) {
    if (m_transfers.contains(transferId)) {
        m_transfers[transferId].priority = priority;
        // TODO: Reorder queue based on priority
        handleGetTransferList();
    }
}

void TransferBridge::handleMoveTransferUp(const QString& transferId) {
    // Find transfer in pending queue and move up
    QList<QString> queueList = m_pendingQueue.toList();
    int index = queueList.indexOf(transferId);

    if (index > 0) {
        std::swap(queueList[index], queueList[index - 1]);
        m_pendingQueue.clear();
        for (const auto& id : queueList) {
            m_pendingQueue.enqueue(id);
        }
        handleGetTransferList();
    }
}

void TransferBridge::handleMoveTransferDown(const QString& transferId) {
    // Find transfer in pending queue and move down
    QList<QString> queueList = m_pendingQueue.toList();
    int index = queueList.indexOf(transferId);

    if (index >= 0 && index < queueList.size() - 1) {
        std::swap(queueList[index], queueList[index + 1]);
        m_pendingQueue.clear();
        for (const auto& id : queueList) {
            m_pendingQueue.enqueue(id);
        }
        handleGetTransferList();
    }
}

void TransferBridge::processQueue() {
    if (m_queuePaused) {
        return;
    }

    while (canStartTransfer() && !m_pendingQueue.isEmpty()) {
        startNextTransfer();
    }
}

void TransferBridge::startNextTransfer() {
    if (m_pendingQueue.isEmpty()) {
        return;
    }

    QString transferId = m_pendingQueue.dequeue();
    auto& transfer = m_transfers[transferId];

    m_activeTransfers.insert(transferId);
    transfer.status = "active";
    transfer.startTime = QDateTime::currentDateTime();

    qDebug() << "TransferBridge: Starting transfer" << transferId;

    // TODO: Start actual CLI transfer
    // if (transfer.type == "upload") {
    //     m_transferModule->addUpload(transfer.sourcePath.toStdString(),
    //                                 transfer.destPath.toStdString());
    // } else {
    //     m_transferModule->addDownload(transfer.sourcePath.toStdString(),
    //                                   transfer.destPath.toStdString());
    // }

    emit transferStarted(transferId);

    // STUB: Simulate transfer progress
    QTimer* progressTimer = new QTimer(this);
    connect(progressTimer, &QTimer::timeout, [this, transferId, progressTimer]() {
        if (!m_transfers.contains(transferId)) {
            progressTimer->stop();
            progressTimer->deleteLater();
            return;
        }

        auto& transfer = m_transfers[transferId];

        if (transfer.status != "active") {
            progressTimer->stop();
            progressTimer->deleteLater();
            return;
        }

        // Simulate progress
        qint64 increment = transfer.size / 20;  // 5% per update
        transfer.transferred += increment;
        transfer.speed = increment * 5;  // Fake speed calculation

        if (transfer.transferred >= transfer.size) {
            transfer.transferred = transfer.size;
            transfer.status = "completed";
            transfer.endTime = QDateTime::currentDateTime();

            m_activeTransfers.remove(transferId);
            m_completedTransfers.insert(transferId);
            m_totalCompleted++;

            emit transferProgress(transferId, transfer.transferred, transfer.size,
                                transfer.speed, 0);
            emit transferCompleted(transferId);

            progressTimer->stop();
            progressTimer->deleteLater();

            updateQueueStatus();
            processQueue();  // Start next transfer
        } else {
            int remaining = calculateTimeRemaining(transfer.size - transfer.transferred,
                                                  transfer.speed);
            emit transferProgress(transferId, transfer.transferred, transfer.size,
                                transfer.speed, remaining);
        }
    });
    progressTimer->start(200);  // Update every 200ms

    updateQueueStatus();
}

bool TransferBridge::canStartTransfer() const {
    return m_activeTransfers.size() < m_maxConcurrent;
}

void TransferBridge::updateQueueStatus() {
    emit queueStatusChanged(m_activeTransfers.size(),
                           m_pendingQueue.size(),
                           m_completedTransfers.size(),
                           m_failedTransfers.size());

    // Calculate global speeds
    qint64 uploadSpeed = 0;
    qint64 downloadSpeed = 0;

    for (const auto& id : m_activeTransfers) {
        const auto& transfer = m_transfers[id];
        if (transfer.type == "upload") {
            uploadSpeed += transfer.speed;
        } else {
            downloadSpeed += transfer.speed;
        }
    }

    if (uploadSpeed != m_totalUploadSpeed || downloadSpeed != m_totalDownloadSpeed) {
        m_totalUploadSpeed = uploadSpeed;
        m_totalDownloadSpeed = downloadSpeed;
        emit globalSpeedUpdate(uploadSpeed, downloadSpeed);
    }
}

void TransferBridge::onTransferStateChange(const QString& transferId, const QString& state) {
    if (!m_transfers.contains(transferId)) {
        return;
    }

    auto& transfer = m_transfers[transferId];
    QString oldStatus = transfer.status;
    transfer.status = state;

    qDebug() << "TransferBridge: Transfer" << transferId
             << "state changed from" << oldStatus << "to" << state;

    // Handle state transitions
    if (state == "completed" && oldStatus != "completed") {
        onTransferComplete(transferId, true, QString());
    } else if (state == "failed" && oldStatus != "failed") {
        onTransferComplete(transferId, false, "Transfer failed");
    }
}

void TransferBridge::onTransferProgress(const QString& transferId, size_t transferred,
                                        size_t total, size_t speed) {
    if (!m_transfers.contains(transferId)) {
        return;
    }

    auto& transfer = m_transfers[transferId];
    transfer.transferred = transferred;
    transfer.size = total;
    transfer.speed = speed;

    int remaining = calculateTimeRemaining(total - transferred, speed);
    emit transferProgress(transferId, transferred, total, speed, remaining);
}

void TransferBridge::onTransferComplete(const QString& transferId, bool success,
                                        const QString& error) {
    if (!m_transfers.contains(transferId)) {
        return;
    }

    auto& transfer = m_transfers[transferId];
    transfer.endTime = QDateTime::currentDateTime();

    m_activeTransfers.remove(transferId);

    if (success) {
        transfer.status = "completed";
        m_completedTransfers.insert(transferId);
        m_totalCompleted++;
        emit transferCompleted(transferId);
    } else {
        transfer.status = "failed";
        transfer.error = error;
        m_failedTransfers.insert(transferId);
        m_totalFailed++;
        emit transferFailed(transferId, error);
    }

    updateQueueStatus();
    processQueue();  // Start next transfer
}

QString TransferBridge::generateTransferId() {
    return QString("t%1").arg(m_nextTransferId++);
}

QVariantMap TransferBridge::transferToVariant(const TransferInfo& transfer) const {
    QVariantMap map;
    map["id"] = transfer.id;
    map["type"] = transfer.type;
    map["sourcePath"] = transfer.sourcePath;
    map["destPath"] = transfer.destPath;
    map["size"] = transfer.size;
    map["transferred"] = transfer.transferred;
    map["status"] = transfer.status;
    map["error"] = transfer.error;
    map["speed"] = transfer.speed;
    map["priority"] = transfer.priority;
    map["retryCount"] = transfer.retryCount;
    map["startTime"] = transfer.startTime.toString(Qt::ISODate);

    if (transfer.endTime.isValid()) {
        map["endTime"] = transfer.endTime.toString(Qt::ISODate);
    }

    // Calculate progress percentage
    if (transfer.size > 0) {
        map["progress"] = (transfer.transferred * 100) / transfer.size;
    } else {
        map["progress"] = 0;
    }

    // Add human-readable sizes
    auto formatSize = [](qint64 bytes) -> QString {
        const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        int unitIndex = 0;
        double size = bytes;

        while (size >= 1024 && unitIndex < 4) {
            size /= 1024.0;
            unitIndex++;
        }

        return QString::number(size, 'f', 2) + " " + units[unitIndex];
    };

    map["sizeFormatted"] = formatSize(transfer.size);
    map["transferredFormatted"] = formatSize(transfer.transferred);

    if (transfer.speed > 0) {
        map["speedFormatted"] = formatSize(transfer.speed) + "/s";
    } else {
        map["speedFormatted"] = "0 B/s";
    }

    return map;
}

int TransferBridge::calculateTimeRemaining(qint64 bytesRemaining, qint64 speed) const {
    if (speed <= 0) {
        return -1;  // Unknown
    }
    return bytesRemaining / speed;
}

} // namespace MegaCustom

#include "moc_TransferBridge.cpp"