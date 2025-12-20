#ifndef TRANSFER_BRIDGE_H
#define TRANSFER_BRIDGE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QQueue>
#include <QDateTime>
#include <QSet>
#include <memory>

namespace MegaCustom {
    // Forward declarations
    class TransferController;
    class TransferManager;

/**
 * TransferBridge - Adapter between GUI TransferController and CLI TransferManager
 *
 * This class manages the transfer queue and translates between:
 * - Qt signals/slots (GUI side)
 * - Callbacks and direct calls (CLI side)
 */
class TransferBridge : public QObject {
    Q_OBJECT

public:
    explicit TransferBridge(QObject* parent = nullptr);
    virtual ~TransferBridge();

    /**
     * Set the CLI transfer manager module
     */
    void setTransferModule(TransferManager* module);

    /**
     * Connect to GUI controller
     */
    void connectToGUI(TransferController* guiController);

    /**
     * Transfer queue management
     */
    int getActiveTransferCount() const { return m_activeTransfers.size(); }
    int getPendingTransferCount() const { return m_pendingQueue.size(); }
    int getMaxConcurrentTransfers() const { return m_maxConcurrent; }
    void setMaxConcurrentTransfers(int max);

public slots:
    /**
     * Handle transfer queue operations from GUI
     */
    void handleAddTransfer(const QString& type, const QString& sourcePath,
                           const QString& destPath, qint64 size = 0);
    void handlePauseTransfer(const QString& transferId);
    void handleResumeTransfer(const QString& transferId);
    void handleCancelTransfer(const QString& transferId);
    void handleRetryTransfer(const QString& transferId);

    /**
     * Queue management
     */
    void handlePauseAllTransfers();
    void handleResumeAllTransfers();
    void handleClearCompleted();
    void handleClearFailed();
    void handleClearAll();

    /**
     * Get transfer list
     */
    void handleGetTransferList();

    /**
     * Priority management
     */
    void handleSetTransferPriority(const QString& transferId, int priority);
    void handleMoveTransferUp(const QString& transferId);
    void handleMoveTransferDown(const QString& transferId);

signals:
    /**
     * Signals to GUI
     */
    void transferAdded(const QVariantMap& transfer);
    void transferStarted(const QString& transferId);
    void transferPaused(const QString& transferId);
    void transferResumed(const QString& transferId);
    void transferProgress(const QString& transferId, qint64 bytesTransferred,
                         qint64 totalBytes, qint64 speed, int timeRemaining);
    void transferCompleted(const QString& transferId);
    void transferFailed(const QString& transferId, const QString& error);
    void transferCancelled(const QString& transferId);

    void transferListUpdated(const QVariantList& transfers);
    void queueStatusChanged(int active, int pending, int completed, int failed);
    void globalSpeedUpdate(qint64 uploadSpeed, qint64 downloadSpeed);

private:
    /**
     * Internal transfer structure
     */
    struct TransferInfo {
        QString id;
        QString type;        // "upload" or "download"
        QString sourcePath;
        QString destPath;
        qint64 size;
        qint64 transferred;
        QString status;      // "pending", "active", "paused", "completed", "failed", "cancelled"
        QString error;
        qint64 speed;
        int priority;
        int retryCount;
        QDateTime startTime;
        QDateTime endTime;
    };

    /**
     * Internal callbacks for CLI module
     */
    void onTransferStateChange(const QString& transferId, const QString& state);
    void onTransferProgress(const QString& transferId, size_t transferred, size_t total, size_t speed);
    void onTransferComplete(const QString& transferId, bool success, const QString& error);

    /**
     * Queue processing
     */
    void processQueue();
    void startNextTransfer();
    bool canStartTransfer() const;
    void updateQueueStatus();

    /**
     * Helper methods
     */
    QString generateTransferId();
    QVariantMap transferToVariant(const TransferInfo& transfer) const;
    int calculateTimeRemaining(qint64 bytesRemaining, qint64 speed) const;

private:
    MegaCustom::TransferManager* m_transferModule = nullptr;
    TransferController* m_guiController = nullptr;

    // Transfer management
    QMap<QString, TransferInfo> m_transfers;       // All transfers
    QQueue<QString> m_pendingQueue;                // Pending transfer IDs
    QSet<QString> m_activeTransfers;               // Active transfer IDs
    QSet<QString> m_pausedTransfers;               // Paused transfer IDs
    QSet<QString> m_completedTransfers;            // Completed transfer IDs
    QSet<QString> m_failedTransfers;               // Failed transfer IDs

    // Configuration
    int m_maxConcurrent = 3;
    int m_nextTransferId = 1;
    bool m_queuePaused = false;

    // Statistics
    qint64 m_totalUploadSpeed = 0;
    qint64 m_totalDownloadSpeed = 0;
    int m_totalCompleted = 0;
    int m_totalFailed = 0;
};

} // namespace MegaCustom

#endif // TRANSFER_BRIDGE_H