#ifndef FILE_CONTROLLER_H
#define FILE_CONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace MegaCustom {

// Forward declaration
class CloudSearchIndex;

class FileController : public QObject {
    Q_OBJECT

public:
    FileController(void* api);

    QString currentLocalPath() const;
    QString currentRemotePath() const;
    void navigateToLocal(const QString& path);
    void navigateToRemote(const QString& path);
    void refreshRemote(const QString& path);
    void createRemoteFolder(const QString& name);
    void createRemoteFile(const QString& name);
    void deleteRemote(const QString& path);
    void renameRemote(const QString& oldPath, const QString& newName);
    void searchRemote(const QString& query);
    void getStorageInfo();

    /**
     * Build the cloud search index for instant Everything-like search
     * This traverses all nodes in the cloud storage and builds an in-memory index
     * @param index The CloudSearchIndex to populate
     */
    void buildSearchIndex(CloudSearchIndex* index);

    /**
     * Get the MegaApi instance used by this controller
     * @return The MegaApi pointer (may be null if using default/active account)
     */
    void* megaApi() const { return m_megaApi; }

    /**
     * Check if this controller has a specific MegaApi bound
     * @return true if a specific API was provided at construction
     */
    bool hasSpecificApi() const { return m_megaApi != nullptr; }

signals:
    // Loading state signals (for responsive UI)
    void loadingStarted(const QString& path);
    void loadingFinished();
    void loadingError(const QString& error);

    // Existing signals
    void localPathChanged(const QString& path);
    void remotePathChanged(const QString& path);
    void remoteListUpdated();
    void operationFailed(const QString& error);

    // Bridge signals
    void listFiles(const QString& path);
    void uploadFiles(const QStringList& localPaths, const QString& remotePath);
    void downloadFiles(const QStringList& remotePaths, const QString& localPath);
    void deleteFiles(const QStringList& paths);
    void createFolder(const QString& path, const QString& name);

    // Response signals
    void fileListReceived(const QVariantList& files);
    void searchResultsReceived(const QVariantList& results);
    void uploadProgress(const QString& transferId, qint64 bytesTransferred, qint64 totalBytes);
    void downloadProgress(const QString& transferId, qint64 bytesTransferred, qint64 totalBytes);
    void storageInfoReceived(qint64 usedBytes, qint64 totalBytes);

    // Search index signals
    void searchIndexBuildStarted();
    void searchIndexBuildProgress(int nodesIndexed);
    void searchIndexBuildCompleted(int totalNodes);

private:
    void* m_megaApi;  // The MegaApi instance to use (nullptr = use active account)
    QString m_currentLocalPath;
    QString m_currentRemotePath;
};

} // namespace MegaCustom

#endif // FILE_CONTROLLER_H