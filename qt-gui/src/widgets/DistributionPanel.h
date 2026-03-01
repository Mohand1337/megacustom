#ifndef MEGACUSTOM_DISTRIBUTIONPANEL_H
#define MEGACUSTOM_DISTRIBUTIONPANEL_H

#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QProgressBar>
#include <QCheckBox>
#include <QComboBox>
#include <QMap>
#include <QList>
#include <QThread>
#include <memory>

class EmptyStateWidget;

namespace mega {
    class MegaApi;
}

namespace MegaCustom {

class MemberRegistry;
class FileController;
class CloudCopier;
class DistributionController;
class FolderCopyWorker;
struct MemberInfo;

/**
 * Info about a scanned folder with smart member matching
 */
struct WmFolderInfo {
    QString folderName;      // e.g., "vanchow555_20251125_113923"
    QString memberId;        // e.g., "vanchow555"
    QString timestamp;       // e.g., "20251125_113923"
    QString fullPath;        // e.g., "/latest-wm/vanchow555_20251125_113923"
    QString matchType;       // "pattern", "id", "email", "name", "fuzzy", "manual", "broadcast", "none"
    int matchConfidence = 0; // 1-5
    bool matched = false;    // True if member exists in registry
    bool selected = false;   // True if selected for distribution
};

/**
 * Panel for distributing watermarked content to members
 */
class DistributionPanel : public QWidget {
    Q_OBJECT

public:
    explicit DistributionPanel(QWidget* parent = nullptr);
    ~DistributionPanel();

    void setFileController(FileController* controller);
    void setMegaApi(mega::MegaApi* api);
    void setDistributionController(DistributionController* controller);

signals:
    void distributionStarted();
    void distributionProgress(int current, int total, const QString& member);
    void distributionCompleted(int success, int failed);
    void distributionError(const QString& error);

public slots:
    void refresh();
    void addFilesFromWatermark(const QStringList& filePaths);

    /**
     * Prepare the table for a direct upload operation.
     * Called before DistributionController::uploadToMembers() to populate
     * member rows and set up the UI in upload mode.
     */
    void prepareForUpload(const QMap<QString, QStringList>& memberFileMap);

private slots:
    void onScanWmFolder();
    void onBroadcastScan();
    void onSelectAll();
    void onDeselectAll();
    void onStartDistribution();
    void onStopDistribution();
    void onPauseDistribution();
    void onBulkRename();
    void onPreviewDistribution();
    void onFileListReceived(const QVariantList& files);

    // Worker thread slots
    void onWorkerTaskStarted(int index, const QString& source, const QString& dest);
    void onWorkerTaskCompleted(int index, bool success, const QString& error);
    void onWorkerAllCompleted(int success, int failed);
    void onWorkerProgress(int current, int total, const QString& currentItem);

    // Template helper slots
    void onVariableHelpClicked();
    void onPreviewPathsClicked();
    void onQuickTemplateChanged(int index);
    void onGenerateDestinations();

    // Saved template slots
    void onSaveTemplate();
    void onDeleteTemplate();
    void onLoadTemplate(int index);

    // Import/Export slots
    void onImportDestinations();
    void onExportDestinations();

    // Manual destination slots
    void onAddRow();
    void onPasteDestinations();
    void onClearAllRows();

private:
    void setupUI();
    void populateTable();
    void updateEmptyState();
    void populateBroadcastTable(const QString& sourcePath);
    QString getDestinationPath(const QString& memberId);
    void executeBulkRename(const QString& folderPath);

    // UI Components - Configuration
    QLineEdit* m_wmPathEdit;
    QPushButton* m_scanBtn;
    QCheckBox* m_broadcastCheck;
    QLineEdit* m_destTemplateEdit;
    QComboBox* m_quickTemplateCombo;
    QComboBox* m_monthCombo;
    QPushButton* m_variableHelpBtn;
    QPushButton* m_previewPathsBtn;
    QPushButton* m_generateDestsBtn;

    // Saved templates
    QComboBox* m_savedTemplateCombo = nullptr;
    QPushButton* m_saveTemplateBtn = nullptr;
    QPushButton* m_deleteTemplateBtn = nullptr;

    // Import/Export
    QPushButton* m_importDestsBtn = nullptr;
    QPushButton* m_exportDestsBtn = nullptr;

    // Manual destination management
    QPushButton* m_addRowBtn = nullptr;
    QPushButton* m_pasteDestsBtn = nullptr;
    QPushButton* m_clearAllBtn = nullptr;

    // Empty state
    EmptyStateWidget* m_emptyState = nullptr;

    // Table
    QTableWidget* m_memberTable;

    // Selection & action buttons
    QPushButton* m_selectAllBtn;
    QPushButton* m_deselectAllBtn;
    QComboBox* m_groupCombo;
    QPushButton* m_previewBtn;
    QPushButton* m_startBtn;
    QPushButton* m_pauseBtn;
    QPushButton* m_stopBtn;
    QPushButton* m_bulkRenameBtn;
    QLabel* m_moveWarningBanner = nullptr;

    // Upload mode banner
    QWidget* m_uploadBanner = nullptr;
    QLabel* m_uploadBannerLabel = nullptr;
    QPushButton* m_uploadBannerCancelBtn = nullptr;

    // Options
    QCheckBox* m_removeWatermarkSuffixCheck;
    QCheckBox* m_createDestFolderCheck;
    QCheckBox* m_copyContentsOnlyCheck;  // Copy contents only (standardized naming)
    QCheckBox* m_skipExistingCheck;
    QCheckBox* m_moveFilesCheck;

    // Status
    QLabel* m_modeIndicator;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;
    QLabel* m_statsLabel;

    // Data
    QList<WmFolderInfo> m_wmFolders;
    MemberRegistry* m_registry;
    FileController* m_fileController;
    mega::MegaApi* m_megaApi;
    std::unique_ptr<CloudCopier> m_cloudCopier;
    DistributionController* m_distController = nullptr;

    // Worker thread for async copy
    QThread* m_workerThread = nullptr;
    FolderCopyWorker* m_copyWorker = nullptr;

    // State
    bool m_isRunning = false;
    bool m_isPaused = false;
    bool m_controllerActive = false;  // True when DistributionController is driving the UI
    int m_successCount = 0;
    int m_failCount = 0;

    // Pending member file map (stored from prepareForUpload, started on user click)
    QMap<QString, QStringList> m_pendingMemberFileMap;

    // Maps member IDs to table row indices (for controller-driven uploads)
    QMap<QString, int> m_memberRowMap;

    // Files received from the Watermark pipeline (for reference/highlighting)
    QStringList m_receivedWatermarkFiles;

    // Helper methods
    void cleanupWorkerThread();
    void loadGroups();
    void loadSavedTemplates();
    void saveSavedTemplates();
    QString savedTemplatesPath() const;

    // Table column indices
    enum TableColumns {
        COL_CHECK = 0,
        COL_SOURCE_FOLDER,
        COL_MATCHED_MEMBER,
        COL_MATCH_TYPE,
        COL_DESTINATION,
        COL_STATUS,
        COL_COUNT
    };
};

} // namespace MegaCustom

#endif // MEGACUSTOM_DISTRIBUTIONPANEL_H
