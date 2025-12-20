#ifndef MEGACUSTOM_CROSSACCOUNTTRANSFERMANAGER_H
#define MEGACUSTOM_CROSSACCOUNTTRANSFERMANAGER_H

#include <QObject>
#include <QMap>
#include <QQueue>
#include <QThread>
#include "AccountModels.h"

namespace mega {
class MegaApi;
class MegaNode;
}

namespace MegaCustom {

class TransferLogStore;
class SessionPool;

/**
 * @brief Manages cross-account file transfers (copy/move between MEGA accounts)
 *
 * Handles the complex process of:
 * 1. Getting public link from source account
 * 2. Importing to target account
 * 3. Optionally deleting from source (for move)
 *
 * Supports queuing, progress tracking, retry, and cancellation.
 */
class CrossAccountTransferManager : public QObject
{
    Q_OBJECT

public:
    explicit CrossAccountTransferManager(SessionPool* sessionPool,
                                         TransferLogStore* logStore,
                                         QObject* parent = nullptr);
    ~CrossAccountTransferManager();

    /**
     * @brief Copy files/folders to another account
     * @param sourcePaths Paths in source account
     * @param sourceAccountId Source account ID
     * @param targetAccountId Target account ID
     * @param targetPath Destination path in target account
     * @return Transfer ID for tracking
     */
    QString copyToAccount(const QStringList& sourcePaths,
                          const QString& sourceAccountId,
                          const QString& targetAccountId,
                          const QString& targetPath);

    /**
     * @brief Move files/folders to another account
     * @param sourcePaths Paths in source account
     * @param sourceAccountId Source account ID
     * @param targetAccountId Target account ID
     * @param targetPath Destination path in target account
     * @param skipSharedLinkWarning If true, skip the warning about shared links being broken
     * @return Transfer ID for tracking (empty if blocked by shared link warning)
     */
    QString moveToAccount(const QStringList& sourcePaths,
                          const QString& sourceAccountId,
                          const QString& targetAccountId,
                          const QString& targetPath,
                          bool skipSharedLinkWarning = false);

    /**
     * @brief Check if any of the source paths have existing shared links
     * @param sourcePaths Paths to check
     * @param sourceAccountId Account to check in
     * @return List of paths that have shared links
     */
    QStringList getPathsWithSharedLinks(const QStringList& sourcePaths,
                                        const QString& sourceAccountId);

    /**
     * @brief Cancel an active transfer
     * @param transferId The transfer ID
     */
    void cancelTransfer(const QString& transferId);

    /**
     * @brief Retry a failed transfer
     * @param transferId The transfer ID
     * @return New transfer ID if retry started
     */
    QString retryTransfer(const QString& transferId);

    /**
     * @brief Get currently active transfers
     */
    QList<CrossAccountTransfer> getActiveTransfers() const;

    /**
     * @brief Get transfer history
     * @param limit Maximum number of results
     */
    QList<CrossAccountTransfer> getHistory(int limit = 100) const;

    /**
     * @brief Check if there are active transfers
     */
    bool hasActiveTransfers() const;

    /**
     * @brief Get count of active transfers
     */
    int activeTransferCount() const;

    /**
     * @brief Check if a specific account has active transfers
     * @param accountId The account to check
     * @return true if account is source or target of any active transfer
     */
    bool hasActiveTransfersForAccount(const QString& accountId) const;

signals:
    void transferStarted(const CrossAccountTransfer& transfer);
    void transferProgress(const QString& transferId, int percent, qint64 bytesTransferred, qint64 bytesTotal);
    void transferCompleted(const CrossAccountTransfer& transfer);
    void transferFailed(const CrossAccountTransfer& transfer);
    void transferCancelled(const QString& transferId);

    /**
     * @brief Emitted when a Move operation would break existing shared links
     * @param sourcePaths All paths being moved
     * @param pathsWithLinks Paths that have shared links (will break after move)
     * @param sourceAccountId Source account
     * @param targetAccountId Target account
     * @param targetPath Target path
     *
     * Connect to this signal to show a confirmation dialog. If user confirms,
     * call moveToAccount() again with skipSharedLinkWarning=true
     */
    void sharedLinksWillBreak(const QStringList& sourcePaths,
                              const QStringList& pathsWithLinks,
                              const QString& sourceAccountId,
                              const QString& targetAccountId,
                              const QString& targetPath);

private slots:
    void processNextInQueue();
    void onTransferStepComplete(const QString& transferId, bool success, const QString& error);

private:
    struct TransferTask {
        CrossAccountTransfer transfer;
        int currentStep;  // 0=pending, 1=getting link, 2=importing, 3=cleanup/delete
        QString tempLink;           // Keep for backward compat (single file)
        QStringList tempLinks;      // Store multiple links for multi-file transfers
        QStringList newlyExportedPaths;  // Track which paths WE exported (vs already had links)
        int currentFileIndex;       // Track which file we're processing
        bool cancelled;
    };

    QString startTransfer(const QStringList& sourcePaths,
                          const QString& sourceAccountId,
                          const QString& targetAccountId,
                          const QString& targetPath,
                          CrossAccountTransfer::Operation operation);

    void executeTransfer(const QString& transferId);
    void stepGetPublicLink(TransferTask& task);
    void stepImportToTarget(TransferTask& task);
    void stepDeleteSource(TransferTask& task);
    void stepCleanupExports(TransferTask& task);
    void finishTransfer(const QString& transferId, bool success, const QString& error = QString());

    QString generateTransferId() const;
    qint64 calculateTotalSize(mega::MegaApi* api, const QStringList& paths) const;
    int countFiles(mega::MegaApi* api, const QStringList& paths) const;

    SessionPool* m_sessionPool;
    TransferLogStore* m_logStore;

    QMap<QString, TransferTask> m_activeTasks;
    QQueue<QString> m_queue;
    int m_maxConcurrent;
    int m_currentConcurrent;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_CROSSACCOUNTTRANSFERMANAGER_H
