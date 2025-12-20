#ifndef MEGACUSTOM_FOLDERMAPPERCONTROLLER_H
#define MEGACUSTOM_FOLDERMAPPERCONTROLLER_H

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <memory>
#include <atomic>

namespace MegaCustom {

/**
 * Controller for FolderMapper feature
 * Bridges between FolderMapperPanel (GUI) and FolderMapper (CLI backend)
 */
class FolderMapperController : public QObject {
    Q_OBJECT

public:
    explicit FolderMapperController(void* megaApi, QObject* parent = nullptr);
    ~FolderMapperController();

    // State queries
    bool hasActiveUpload() const { return m_isUploading; }
    int getMappingCount() const { return m_mappingCount; }

signals:
    // Signals to GUI (FolderMapperPanel)
    void clearMappings();  // Emitted before loading to clear the table
    void mappingsLoaded(int count);
    void mappingAdded(const QString& name, const QString& localPath,
                      const QString& remotePath, bool enabled);
    void mappingRemoved(const QString& name);
    void mappingUpdated(const QString& name);

    void uploadStarted(const QString& mappingName);
    void uploadProgress(const QString& mappingName, const QString& currentFile,
                        int filesCompleted, int totalFiles,
                        qint64 bytesUploaded, qint64 totalBytes,
                        double speedBytesPerSec);
    void uploadComplete(const QString& mappingName, bool success,
                        int filesUploaded, int filesSkipped, int filesFailed);
    void previewReady(const QString& mappingName, int filesToUpload,
                      int filesToSkip, qint64 totalBytes);
    void error(const QString& operation, const QString& message);

public slots:
    // Slots from GUI (FolderMapperPanel)
    void loadMappings();
    void saveMappings();
    void addMapping(const QString& name, const QString& localPath,
                    const QString& remotePath);
    void removeMapping(const QString& name);
    void updateMapping(const QString& name, const QString& localPath,
                       const QString& remotePath);
    void setMappingEnabled(const QString& name, bool enabled);
    void uploadMapping(const QString& name, bool dryRun, bool incremental);
    void uploadAll(bool dryRun, bool incremental);
    void previewUpload(const QString& name);
    void cancelUpload();

private:
    void* m_megaApi;
    std::atomic<bool> m_isUploading{false};
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<int> m_mappingCount{0};
};

} // namespace MegaCustom

#endif // MEGACUSTOM_FOLDERMAPPERCONTROLLER_H
