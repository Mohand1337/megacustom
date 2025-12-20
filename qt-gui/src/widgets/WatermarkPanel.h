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
#include <QThread>
#include <memory>

namespace MegaCustom {

class Watermarker;
class MemberRegistry;
class WatermarkerController;
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

public slots:
    void process();
    void cancel();

signals:
    void started();
    void progress(int fileIndex, int totalFiles, const QString& currentFile, int percent);
    void fileCompleted(int fileIndex, bool success, const QString& outputPath, const QString& error);
    void finished(int successCount, int failCount);

private:
    QStringList m_files;
    QString m_outputDir;
    QString m_memberId;
    std::shared_ptr<WatermarkConfig> m_config;
    bool m_cancelled = false;
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

signals:
    void watermarkStarted();
    void watermarkProgress(int current, int total, const QString& file);
    void watermarkCompleted(int success, int failed);
    void sendToDistribution(const QStringList& filePaths);

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
    void onMemberChanged(int index);
    void onSendToDistribution();
    void onWatermarkHelpClicked();
    void onPreviewWatermarkClicked();
    void onSavePreset();
    void onLoadPreset();
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
    WatermarkConfig buildConfig() const;
    QString formatFileSize(qint64 bytes) const;
    void loadPresets();
    void applyPreset(const QString& presetName);

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

    // UI Components - Mode
    QComboBox* m_modeCombo;          // Global / Per-Member
    QComboBox* m_memberCombo;         // Member selection (when per-member)
    QWidget* m_memberWidget;          // Container for member selection

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

    // Worker thread
    QThread* m_workerThread = nullptr;
    WatermarkWorker* m_worker = nullptr;

    // Controller (optional - for advanced functionality)
    WatermarkerController* m_controller = nullptr;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_WATERMARKPANEL_H
