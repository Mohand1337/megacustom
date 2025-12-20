#ifndef MEGACUSTOM_DISTRIBUTIONCONTROLLER_H
#define MEGACUSTOM_DISTRIBUTIONCONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <memory>

namespace MegaCustom {

class DistributionPipeline;
struct DistributionConfig;
struct DistributionResult;
struct DistributionProgress;

/**
 * Qt wrapper for DistributionConfig
 */
struct QtDistributionConfig {
    enum class WatermarkMode {
        None,           // Upload files as-is
        Global,         // Same watermark for all
        PerMember       // Personalized per member
    };

    WatermarkMode watermarkMode = WatermarkMode::PerMember;
    QString globalPrimaryText;
    QString globalSecondaryText;
    QString tempDirectory;
    bool deleteTempAfterUpload = true;
    bool keepLocalCopies = false;
    QString localCopiesDir;
    int parallelWatermarkJobs = 2;
    int parallelUploadJobs = 4;
    bool resumeOnError = true;
    bool createFolderIfMissing = true;
    bool overwriteExisting = false;
};

/**
 * Qt wrapper for member distribution status
 */
struct QtMemberStatus {
    QString memberId;
    QString memberName;
    QString destinationFolder;
    QString state;  // "pending", "watermarking", "uploading", "completed", "failed", "skipped"
    int filesWatermarked = 0;
    int filesUploaded = 0;
    int filesFailed = 0;
    QString lastError;
};

/**
 * Qt wrapper for distribution result
 */
struct QtDistributionResult {
    bool success = false;
    QString jobId;
    QStringList sourceFiles;
    QList<QtMemberStatus> memberResults;
    int totalMembers = 0;
    int membersCompleted = 0;
    int membersFailed = 0;
    int membersSkipped = 0;
    int totalFiles = 0;
    int filesWatermarked = 0;
    int filesUploaded = 0;
    int filesFailed = 0;
    QStringList errors;
};

/**
 * Qt wrapper for progress updates
 */
struct QtDistributionProgress {
    QString jobId;
    double overallPercent = 0.0;
    QString phase;          // "watermarking", "uploading", "cleanup", "complete"
    QString currentMember;
    QString currentFile;
    QString currentOperation;
    int membersProcessed = 0;
    int totalMembers = 0;
    int filesProcessed = 0;
    int totalFiles = 0;
    qint64 elapsedMs = 0;
    qint64 estimatedRemainingMs = 0;
    int errorCount = 0;
};

/**
 * Worker for running distribution in background thread
 */
class DistributionWorker : public QObject {
    Q_OBJECT
public:
    explicit DistributionWorker(QObject* parent = nullptr);
    ~DistributionWorker();

    void setSourceFiles(const QStringList& files);
    void setMemberIds(const QStringList& memberIds);
    void setConfig(const QtDistributionConfig& config);
    void setPreviewOnly(bool preview);

public slots:
    void process();
    void cancel();
    void pause();
    void resume();

signals:
    void started(const QString& jobId);
    void progress(const QtDistributionProgress& progress);
    void memberCompleted(const QtMemberStatus& status);
    void finished(const QtDistributionResult& result);
    void error(const QString& message);

private:
    QStringList m_sourceFiles;
    QStringList m_memberIds;
    QtDistributionConfig m_config;
    bool m_previewOnly = false;
    std::unique_ptr<DistributionPipeline> m_pipeline;
};

/**
 * DistributionController - Qt controller for DistributionPipeline
 *
 * Bridges the CLI DistributionPipeline class with Qt signals/slots
 * for use in the GUI. Runs distribution operations in a worker thread.
 */
class DistributionController : public QObject {
    Q_OBJECT

public:
    explicit DistributionController(QObject* parent = nullptr);
    ~DistributionController();

    // ==================== Configuration ====================

    void setConfig(const QtDistributionConfig& config);
    QtDistributionConfig config() const { return m_config; }

    // ==================== Operations ====================

    /**
     * Start distribution to selected members
     * @param sourceFiles List of source file paths
     * @param memberIds List of member IDs (empty = all with folders)
     */
    void startDistribution(const QStringList& sourceFiles,
                           const QStringList& memberIds = QStringList());

    /**
     * Preview distribution without executing
     */
    void previewDistribution(const QStringList& sourceFiles,
                             const QStringList& memberIds = QStringList());

    /**
     * Retry failed distributions from previous result
     */
    void retryFailed(const QtDistributionResult& previousResult);

    // ==================== Control ====================

    void cancel();
    void pause();
    void resume();

    bool isRunning() const { return m_isRunning; }
    bool isPaused() const { return m_isPaused; }

    // ==================== Queries ====================

    /**
     * Get members with distribution folders bound
     */
    QStringList getMembersWithFolders();

    /**
     * Get last distribution result
     */
    QtDistributionResult lastResult() const { return m_lastResult; }

signals:
    // Distribution lifecycle
    void distributionStarted(const QString& jobId);
    void distributionProgress(const QtDistributionProgress& progress);
    void memberCompleted(const QtMemberStatus& status);
    void distributionFinished(const QtDistributionResult& result);
    void distributionError(const QString& error);

    // Preview results
    void previewReady(const QtDistributionResult& preview);

    // State changes
    void runningChanged(bool running);
    void pausedChanged(bool paused);

private slots:
    void onWorkerStarted(const QString& jobId);
    void onWorkerProgress(const QtDistributionProgress& progress);
    void onWorkerMemberCompleted(const QtMemberStatus& status);
    void onWorkerFinished(const QtDistributionResult& result);
    void onWorkerError(const QString& message);

private:
    void startWorker(bool previewOnly);
    void cleanupWorker();

    QtDistributionConfig m_config;
    QtDistributionResult m_lastResult;
    bool m_isRunning = false;
    bool m_isPaused = false;

    QThread* m_workerThread = nullptr;
    DistributionWorker* m_worker = nullptr;

    // Pending operation data
    QStringList m_pendingSourceFiles;
    QStringList m_pendingMemberIds;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_DISTRIBUTIONCONTROLLER_H
