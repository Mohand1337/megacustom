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
 * Info about a watermarked folder in /latest-wm/
 */
struct WmFolderInfo {
    QString folderName;      // e.g., "vanchow555_20251125_113923"
    QString memberId;        // e.g., "vanchow555"
    QString timestamp;       // e.g., "20251125_113923"
    QString fullPath;        // e.g., "/latest-wm/vanchow555_20251125_113923"
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

private slots:
    void onScanWmFolder();
    void onSelectAll();
    void onDeselectAll();
    void onStartDistribution();
    void onStopDistribution();
    void onPauseDistribution();
    void onTableSelectionChanged();
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

private:
    void setupUI();
    void populateTable();
    QString getDestinationPath(const QString& memberId);
    void executeBulkRename(const QString& folderPath);

    // UI Components
    QLineEdit* m_wmPathEdit;
    QPushButton* m_scanBtn;
    QLineEdit* m_destTemplateEdit;
    QComboBox* m_monthCombo;
    QPushButton* m_variableHelpBtn;
    QPushButton* m_previewPathsBtn;

    QTableWidget* m_memberTable;

    QPushButton* m_selectAllBtn;
    QPushButton* m_deselectAllBtn;
    QPushButton* m_previewBtn;
    QPushButton* m_startBtn;
    QPushButton* m_pauseBtn;
    QPushButton* m_stopBtn;
    QPushButton* m_bulkRenameBtn;

    QCheckBox* m_removeWatermarkSuffixCheck;
    QCheckBox* m_createDestFolderCheck;
    QCheckBox* m_copyFolderItselfCheck;  // Copy folder itself vs contents only
    QCheckBox* m_skipExistingCheck;      // Skip existing files instead of overwriting
    QCheckBox* m_moveFilesCheck;         // Move files (delete source after transfer)

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
    int m_successCount = 0;
    int m_failCount = 0;

    // Async bulk rename tracking
    int m_renameProgress = 0;
    int m_renameTotal = 0;

    // Helper methods
    void cleanupWorkerThread();
};

} // namespace MegaCustom

#endif // MEGACUSTOM_DISTRIBUTIONPANEL_H
