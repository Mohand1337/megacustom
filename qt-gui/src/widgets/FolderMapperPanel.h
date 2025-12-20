#ifndef MEGACUSTOM_FOLDERMAPPERPANEL_H
#define MEGACUSTOM_FOLDERMAPPERPANEL_H

#include <QWidget>
#include <QTableWidget>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace MegaCustom {

class FolderMapperController;
class FileController;

/**
 * Panel for managing folder mappings (VPS-to-MEGA uploads)
 */
class FolderMapperPanel : public QWidget {
    Q_OBJECT

public:
    explicit FolderMapperPanel(QWidget* parent = nullptr);
    ~FolderMapperPanel();

    void setController(FolderMapperController* controller);
    void setFileController(FileController* controller);

public slots:
    // Data updates from controller
    void clearMappingsTable();  // Call before loadMappings to prevent doubling
    void onMappingsLoaded(int count);
    void onMappingAdded(const QString& name, const QString& localPath,
                        const QString& remotePath, bool enabled);
    void onMappingRemoved(const QString& name);
    void onMappingUpdated(const QString& name);

    // Progress updates
    void onUploadStarted(const QString& mappingName);
    void onUploadProgress(const QString& mappingName, const QString& currentFile,
                          int filesCompleted, int totalFiles,
                          qint64 bytesUploaded, qint64 totalBytes,
                          double speedBytesPerSec);
    void onUploadComplete(const QString& mappingName, bool success,
                          int filesUploaded, int filesSkipped, int filesFailed);
    void onPreviewReady(const QString& mappingName, int filesToUpload,
                        int filesToSkip, qint64 totalBytes);
    void onError(const QString& operation, const QString& message);

signals:
    // User actions
    void addMappingRequested(const QString& name, const QString& localPath,
                             const QString& remotePath);
    void removeMappingRequested(const QString& name);
    void editMappingRequested(const QString& name, const QString& localPath,
                              const QString& remotePath);
    void toggleMappingEnabled(const QString& name, bool enabled);
    void uploadMappingRequested(const QString& name, bool dryRun, bool incremental);
    void uploadAllRequested(bool dryRun, bool incremental);
    void previewUploadRequested(const QString& name);
    void cancelUploadRequested();
    void refreshMappingsRequested();

private slots:
    void onAddClicked();
    void onUpdateClicked();  // Update edited mapping
    void onClearEditClicked();  // Clear edit mode
    void onRemoveClicked();
    void onEditClicked();
    void onUploadSelectedClicked();
    void onUploadAllClicked();
    void onPreviewClicked();
    void onCancelClicked();
    void onMappingSelectionChanged();
    void onMappingDoubleClicked(int row, int column);
    void onEnabledCheckboxChanged(int state);
    void onBrowseLocalClicked();
    void onBrowseRemoteClicked();

private:
    void setupUI();
    void setupToolbar(QVBoxLayout* mainLayout);
    void setupInputSection(QVBoxLayout* mainLayout);
    void setupMappingTable(QVBoxLayout* mainLayout);
    void setupProgressSection(QVBoxLayout* mainLayout);
    void setupSettingsSection(QVBoxLayout* mainLayout);
    void updateButtonStates();
    void clearInputFields();
    int findRowByName(const QString& name);
    QString formatSize(qint64 bytes);
    QString formatSpeed(double bytesPerSec);

private:
    FolderMapperController* m_controller = nullptr;
    FileController* m_fileController = nullptr;

    // Input section
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_localPathEdit = nullptr;
    QLineEdit* m_remotePathEdit = nullptr;
    QPushButton* m_browseLocalBtn = nullptr;
    QPushButton* m_browseRemoteBtn = nullptr;

    // Toolbar buttons
    QPushButton* m_addButton = nullptr;
    QPushButton* m_updateButton = nullptr;  // For updating edited mapping
    QPushButton* m_removeButton = nullptr;
    QPushButton* m_editButton = nullptr;

    // Edit state tracking
    QString m_editingMappingName;  // Name of mapping being edited (empty if adding new)
    QPushButton* m_uploadSelectedButton = nullptr;
    QPushButton* m_uploadAllButton = nullptr;
    QPushButton* m_previewButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QPushButton* m_refreshButton = nullptr;

    // Mapping table
    QTableWidget* m_mappingTable = nullptr;

    // Progress section
    QGroupBox* m_progressGroup = nullptr;
    QLabel* m_currentFileLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_statsLabel = nullptr;

    // Settings section
    QCheckBox* m_incrementalCheckbox = nullptr;
    QCheckBox* m_recursiveCheckbox = nullptr;
    QSpinBox* m_concurrentSpinBox = nullptr;
    QLineEdit* m_excludePatternsEdit = nullptr;

    // State
    bool m_isUploading = false;
    QString m_currentMappingName;

    // Table column indices
    enum TableColumns {
        COL_NAME = 0,
        COL_LOCAL_PATH,
        COL_REMOTE_PATH,
        COL_STATUS,
        COL_ENABLED,
        COL_COUNT
    };
};

} // namespace MegaCustom

#endif // MEGACUSTOM_FOLDERMAPPERPANEL_H
