#ifndef FILE_BRIDGE_H
#define FILE_BRIDGE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <memory>

namespace MegaCustom {
    // Forward declarations
    class FileController;
    class FileOperations;

/**
 * FileBridge - Adapter between GUI FileController and CLI FileOperations
 *
 * This class translates between:
 * - Qt signals/slots (GUI side)
 * - Callbacks and direct calls (CLI side)
 */
class FileBridge : public QObject {
    Q_OBJECT

public:
    explicit FileBridge(QObject* parent = nullptr);
    virtual ~FileBridge();

    /**
     * Set the CLI file operations module
     */
    void setFileModule(FileOperations* module);

    /**
     * Connect to GUI controller
     */
    void connectToGUI(FileController* guiController);

public slots:
    /**
     * Handle file list request from GUI
     */
    void handleListFiles(const QString& path);

    /**
     * Handle file upload request from GUI
     */
    void handleUploadFiles(const QStringList& localPaths, const QString& remotePath);

    /**
     * Handle file download request from GUI
     */
    void handleDownloadFiles(const QStringList& remotePaths, const QString& localPath);

    /**
     * Handle file deletion request from GUI
     */
    void handleDeleteFiles(const QStringList& paths);

    /**
     * Handle create folder request from GUI
     */
    void handleCreateFolder(const QString& path, const QString& name);

    /**
     * Handle file move/rename request from GUI
     */
    void handleMoveFile(const QString& sourcePath, const QString& destPath);

    /**
     * Handle file copy request from GUI
     */
    void handleCopyFile(const QString& sourcePath, const QString& destPath);

    /**
     * Handle file sharing request from GUI
     */
    void handleShareFile(const QString& path, const QString& email, bool readOnly);

    /**
     * Handle storage quota request from GUI
     */
    void handleGetStorageInfo();

signals:
    /**
     * Signals to GUI
     */
    void fileListReceived(const QVariantList& files);
    void fileListError(const QString& error);

    void uploadStarted(const QString& path, const QString& transferId);
    void uploadProgress(const QString& transferId, qint64 bytesTransferred, qint64 totalBytes);
    void uploadCompleted(const QString& transferId);
    void uploadFailed(const QString& transferId, const QString& error);

    void downloadStarted(const QString& path, const QString& transferId);
    void downloadProgress(const QString& transferId, qint64 bytesTransferred, qint64 totalBytes);
    void downloadCompleted(const QString& transferId);
    void downloadFailed(const QString& transferId, const QString& error);

    void fileDeleted(const QString& path);
    void deletionFailed(const QString& path, const QString& error);

    void folderCreated(const QString& path);
    void folderCreationFailed(const QString& error);

    void fileMoved(const QString& oldPath, const QString& newPath);
    void moveFailed(const QString& error);

    void fileCopied(const QString& sourcePath, const QString& destPath);
    void copyFailed(const QString& error);

    void fileShared(const QString& path, const QString& shareLink);
    void shareFailed(const QString& error);

    void storageInfoReceived(qint64 usedBytes, qint64 totalBytes, qint64 availableBytes);
    void storageInfoError(const QString& error);

private:
    /**
     * Internal callbacks for CLI module
     */
    void onFileListReceived(const std::vector<void*>& files);
    void onOperationComplete(const QString& operation, bool success, const QString& result);
    void onTransferProgress(const QString& transferId, double progress, size_t speed);

    /**
     * Helper methods
     */
    QVariantMap convertFileInfo(void* megaNode) const;
    QString generateTransferId();

private:
    MegaCustom::FileOperations* m_fileModule = nullptr;
    FileController* m_guiController = nullptr;

    // Track active transfers
    QMap<QString, QString> m_activeTransfers;  // transferId -> path
    int m_nextTransferId = 1;
};

} // namespace MegaCustom

#endif // FILE_BRIDGE_H