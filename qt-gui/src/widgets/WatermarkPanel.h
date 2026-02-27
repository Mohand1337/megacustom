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
#include <memory>
#include <atomic>

namespace mega { class MegaApi; }

namespace MegaCustom {

class Watermarker;
class MemberRegistry;
class WatermarkerController;
class MetricsStore;
struct WatermarkConfig;
struct WatermarkResult;

/**
 * Info about a file to be watermarked
 */
struct WatermarkFileInfo {
    QString filePath;
    QString fileName;
    qint64 fileSize;
    QString fileType;       // "video" or "pdf"
    QString memberName;     // empty in global mode, member display name in per-member mode
    QString memberId;       // empty in global mode, member ID in per-member mode
    QString status;         // "pending", "processing", "complete", "error"
    QString outputPath;
    QString error;
    int progressPercent = 0;
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
    void setMetricsStore(MetricsStore* store);

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
    void diskSpaceWarning(qint64 available, qint64 needed);

private:
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
    MetricsStore* m_metricsStore = nullptr;
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
    void updateStats();
    void updateButtonStates();
    void loadMembers();
    QStringList getSelectedMemberIds() const;
    WatermarkConfig buildConfig() const;
    QString formatFileSize(qint64 bytes) const;
    void loadPresets();
    void applyPreset(const QString& presetName);
    void updateSmartEstimate();

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

    // Stored member file map from last multi-member watermark (for manual send to distribution)
    QMap<QString, QStringList> m_lastMemberFileMap;

    // Worker thread
    QThread* m_workerThread = nullptr;
    WatermarkWorker* m_worker = nullptr;

    // Controller (optional - for advanced functionality)
    WatermarkerController* m_controller = nullptr;

    // Smart Engine: auto-upload & disk management
    QCheckBox* m_autoUploadCheck = nullptr;
    QLabel* m_smartEstimateLabel = nullptr;
    mega::MegaApi* m_megaApi = nullptr;
    MetricsStore* m_metricsStore = nullptr;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_WATERMARKPANEL_H
