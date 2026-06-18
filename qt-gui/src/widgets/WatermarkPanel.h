#ifndef MEGACUSTOM_WATERMARKPANEL_H
#define MEGACUSTOM_WATERMARKPANEL_H

#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QProgressBar>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QListWidget>
#include <QThread>
#include <QMap>
#include <QJsonArray>
#include <QJsonObject>
#include <memory>
#include <atomic>

class EmptyStateWidget;

namespace mega { class MegaApi; }

namespace MegaCustom {

class Watermarker;
class MemberRegistry;
class WatermarkerController;
class MetricsStore;
struct WatermarkConfig;
struct WatermarkResult;
struct OperationJobRecord;

/**
 * Info about a file to be watermarked
 */
struct WatermarkFileInfo {
    QString filePath;
    QString fileName;
    qint64 fileSize;
    QString fileType;       // "video", "pdf", or "audio"
    QString memberName;     // empty in global mode, member display name in per-member mode
    QString memberId;       // empty in global mode, member ID in per-member mode
    QString status;         // "pending", "processing", "complete", "error", "uploading", "uploaded"
    QString outputPath;
    QString error;
    QString jobId;          // owning watermark job while live table state is available
    int progressPercent = 0;
    bool isHeader = false;  // true for member section header rows
};

struct WatermarkResumeTask {
    QString filePath;
    QString memberId;
    int rowIndex = -1;
    QString existingOutputPath;
    bool watermarkNeeded = true;
};

/**
 * Worker thread for watermarking operations
 */
class WatermarkWorker : public QObject {
    Q_OBJECT
public:
    explicit WatermarkWorker(QObject* parent = nullptr);

    void setFiles(const QStringList& files);
    void setOutputDir(const QString& dir);
    void setConfig(const WatermarkConfig& config);
    void setMemberId(const QString& memberId);
    void setMemberIds(const QStringList& memberIds);
    void setRawTemplates(const QString& primary, const QString& secondary);
    void setAutoUpload(bool enabled, void* megaApi);
    void setCustomUploadPath(const QString& path);
    void setRootDir(const QString& rootDir);
    void setMetricsStore(MetricsStore* store);
    void setResumeTasks(const QList<WatermarkResumeTask>& tasks);

public slots:
    void process();
    void cancel();

signals:
    void started();
    void progress(int fileIndex, int totalFiles, const QString& currentFile, int percent);
    void fileCompleted(int fileIndex, bool success, const QString& outputPath, const QString& error);
    void finished(int successCount, int failCount);
    void finishedWithMapping(int successCount, int failCount,
                             const QMap<QString, QStringList>& memberFileMap);
    void memberBatchUploading(const QString& memberId, int fileIdx, int totalFiles, const QString& fileName);
    void memberBatchCleanedUp(const QString& memberId, int uploaded, int failed, int deleted);
    void memberAutoUploadSkipped(const QString& memberId, const QString& reason);
    void diskSpaceWarning(qint64 available, qint64 needed);

private:
    qint64 estimateOutputBytes(const QString& inputPath) const;
    bool ensureDiskSpaceForNextOutput(const QString& inputPath, const QString& outputBaseDir);
    void pauseForDiskSpace(const QString& inputPath, const QString& outputBaseDir);
    bool isDiskSpaceError(const WatermarkResult& result) const;
    WatermarkResult watermarkInput(Watermarker& watermarker,
                                   const WatermarkConfig& baseConfig,
                                   const QString& inputPath,
                                   const QString& memberId,
                                   const std::string& outputDir);
    void processResumeTasks(Watermarker& watermarker,
                            const WatermarkConfig& baseConfig,
                            const std::string& outputDir,
                            int& successCount,
                            int& failCount,
                            QMap<QString, QStringList>& memberFileMap);

    QStringList m_files;
    QString m_outputDir;
    QString m_memberId;
    QStringList m_memberIds;
    QString m_rawPrimaryTemplate;
    QString m_rawSecondaryTemplate;
    std::shared_ptr<WatermarkConfig> m_config;
    std::atomic<bool> m_cancelled{false};
    bool m_autoUpload = false;
    void* m_megaApi = nullptr;
    QString m_customUploadPath;
    QString m_rootDir;
    MetricsStore* m_metricsStore = nullptr;
    QList<WatermarkResumeTask> m_resumeTasks;
};

/**
 * Panel for watermarking videos and PDFs
 * Provides UI to select files, configure watermark settings, and process
 */
class WatermarkPanel : public QWidget {
    Q_OBJECT

public:
    explicit WatermarkPanel(QWidget* parent = nullptr);
    ~WatermarkPanel();

    void setController(WatermarkerController* controller);
    void setMegaApi(mega::MegaApi* api);
    void setMetricsStore(MetricsStore* store);

signals:
    void watermarkStarted();
    void watermarkProgress(int current, int total, const QString& file);
    void watermarkCompleted(int success, int failed);
    void sendToDistribution(const QStringList& filePaths);
    void sendToDistributionMapped(const QMap<QString, QStringList>& memberFileMap);

public slots:
    void refresh();
    void addFilesFromDownloader(const QStringList& filePaths);
    void selectMember(const QString& memberId);
    void retryJob(const QString& jobId);
    void resumeJob(const QString& jobId);
    void cleanupJob(const QString& jobId);

private slots:
    void onAddFiles();
    void onAddFolder();
    void onRemoveSelected();
    void onClearAll();
    void onBrowseOutput();
    void onStartWatermark();
    void onStopWatermark();
    void onOpenSettings();
    void onCheckDependencies();
    void onTableSelectionChanged();
    void onModeChanged(int index);
    void onMemberSelectionChanged();
    void onSelectAllMembers();
    void onDeselectAllMembers();
    void onGroupQuickSelect(int index);
    void onMemberSearchChanged();
    void onSendToDistribution();
    void onWatermarkHelpClicked();
    void onPreviewWatermarkClicked();
    void onSavePreset();
    void onDeletePreset();
    void onPresetChanged(int index);

    // Worker signals
    void onWorkerProgress(int fileIndex, int totalFiles, const QString& currentFile, int percent);
    void onWorkerFileCompleted(int fileIndex, bool success, const QString& outputPath, const QString& error);
    void onWorkerFinished(int successCount, int failCount);

private:
    void setupUI();
    void populateTable();
    void updateEmptyState();
    void updateStats();
    void updateButtonStates();
    void updateCurrentJobProgress(const QString& summary = {});
    QString watermarkReportRootDir() const;
    QString writeWatermarkCompletionReport(int successCount, int failCount) const;
    void saveWatermarkCheckpoint(const QString& reason, const QString& jobId = {});
    QJsonArray serializeWatermarkRows() const;
    bool restoreWatermarkRowsFromJob(const OperationJobRecord& record);
    void applyWatermarkJobMetadataToUi(const QJsonObject& metadata);
    void onResumePausedWatermark();
    void loadMembers();
    QStringList getSelectedMemberIds() const;
    WatermarkConfig buildConfig() const;
    QString formatFileSize(qint64 bytes) const;
    void loadPresets();
    void applyPreset(const QString& presetName);
    void updateSmartEstimate();
    void updateSingleRow(int row);
    void updateMemberHeader(int headerRow);
    void markMemberRowsUploaded(const QString& memberId, const QString& note = {});
    int findMemberHeaderRow(const QString& memberId) const;

    // Empty state
    EmptyStateWidget* m_emptyState = nullptr;

    // UI Components - File Selection
    QTableWidget* m_fileTable;
    QPushButton* m_addFilesBtn;
    QPushButton* m_addFolderBtn;
    QPushButton* m_removeBtn;
    QPushButton* m_clearBtn;

    // UI Components - Output
    QLineEdit* m_outputDirEdit;
    QPushButton* m_browseOutputBtn;
    QCheckBox* m_sameAsInputCheck;

    // UI Components - Mode & Member Selection
    QComboBox* m_modeCombo;              // Global / Per-Member
    QWidget* m_memberWidget;             // Container for member multi-select
    QListWidget* m_memberListWidget;     // Checkable member/group list
    QPushButton* m_selectAllMembersBtn;
    QPushButton* m_deselectAllMembersBtn;
    QComboBox* m_groupQuickSelectCombo;  // Additive group selection
    QLineEdit* m_memberSearchEdit;       // Filter the member list
    QLabel* m_selectionSummaryLabel;     // "N members selected"

    // UI Components - Quick Settings
    QLineEdit* m_primaryTextEdit;
    QLineEdit* m_secondaryTextEdit;
    QPushButton* m_watermarkHelpBtn;     // Help button for template variables
    QPushButton* m_watermarkPreviewBtn;  // Preview expanded watermark text
    QComboBox* m_presetCombo;            // FFmpeg preset
    QSpinBox* m_crfSpin;              // Quality
    QSpinBox* m_intervalSpin;         // Watermark interval (seconds)
    QSpinBox* m_durationSpin;         // Watermark duration (seconds)
    QPushButton* m_settingsBtn;       // Open full settings dialog

    // Metadata embedding
    QCheckBox* m_embedMetadataCheck;
    QLineEdit* m_metaTitleEdit;
    QLineEdit* m_metaAuthorEdit;
    QLineEdit* m_metaCommentEdit;
    QLineEdit* m_metaKeywordsEdit;

    // Preset management
    QComboBox* m_presetNameCombo;     // Preset selection
    QPushButton* m_savePresetBtn;
    QPushButton* m_deletePresetBtn;

    // UI Components - Actions
    QPushButton* m_startBtn;
    QPushButton* m_stopBtn;
    QPushButton* m_checkDepsBtn;
    QPushButton* m_sendToDistBtn;

    // UI Components - Progress
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;
    QLabel* m_statsLabel;

    // Data
    QList<WatermarkFileInfo> m_files;
    MemberRegistry* m_registry;
    bool m_isRunning = false;
    bool m_pausedForDiskSpace = false;
    QString m_diskSpacePauseMessage;
    QString m_currentJobId;
    QString m_pausedJobId;
    QString m_retrySourceJobId;
    bool m_currentJobCancelled = false;
    QString m_sourceRootDir;  // Root folder from "Add Folder" for subfolder structure

    // Stored member file map from last multi-member watermark (for manual send to distribution)
    QMap<QString, QStringList> m_lastMemberFileMap;

    // Worker index → m_files row mapping (accounts for header rows)
    QMap<int, int> m_workerIdxToRow;

    // Worker thread
    QThread* m_workerThread = nullptr;
    WatermarkWorker* m_worker = nullptr;

    // Controller (optional - for advanced functionality)
    WatermarkerController* m_controller = nullptr;

    // Smart Engine: auto-upload & disk management
    QCheckBox* m_autoUploadCheck = nullptr;
    QCheckBox* m_customPathCheck = nullptr;
    QLineEdit* m_customPathEdit = nullptr;
    QPushButton* m_browseCustomPathBtn = nullptr;
    QLabel* m_smartEstimateLabel = nullptr;
    mega::MegaApi* m_megaApi = nullptr;
    MetricsStore* m_metricsStore = nullptr;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_WATERMARKPANEL_H
