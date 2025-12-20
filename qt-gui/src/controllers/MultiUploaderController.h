#ifndef MEGACUSTOM_MULTIUPLOADERCONTROLLER_H
#define MEGACUSTOM_MULTIUPLOADERCONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
#include <QMutex>
#include <memory>
#include <atomic>

namespace MegaCustom {

/**
 * @brief Distribution rule for multi-destination uploads
 */
struct DistributionRule {
    enum class RuleType {
        BY_EXTENSION,   // Route by file extension
        BY_SIZE,        // Route by file size range
        BY_NAME,        // Route by file name pattern
        DEFAULT         // Default destination for unmatched files
    };

    int id = 0;
    RuleType type = RuleType::DEFAULT;
    QString pattern;        // Extension, size range, or name pattern
    QString destination;    // Remote path destination
    bool enabled = true;
};

/**
 * @brief Upload task representing a file to upload
 */
struct UploadTask {
    enum class Status {
        PENDING,
        UPLOADING,
        COMPLETED,
        FAILED,
        PAUSED,
        CANCELLED
    };

    int id = 0;
    QString localPath;
    QString remotePath;
    QString fileName;
    qint64 fileSize = 0;
    qint64 bytesUploaded = 0;
    Status status = Status::PENDING;
    QString errorMessage;
    QString destinationRule;  // Which rule matched
};

/**
 * Controller for MultiUploader feature
 * Bridges between MultiUploaderPanel (GUI) and Mega SDK
 */
class MultiUploaderController : public QObject {
    Q_OBJECT

public:
    explicit MultiUploaderController(void* megaApi, QObject* parent = nullptr);
    ~MultiUploaderController();

    // State queries
    bool hasActiveUpload() const { return m_isUploading; }
    int getSourceFileCount() const { return m_sourceFiles.size(); }
    int getDestinationCount() const { return m_destinations.size(); }
    int getRuleCount() const { return m_rules.size(); }
    int getPendingTaskCount() const;
    int getCompletedTaskCount() const;

signals:
    // Signals to GUI (MultiUploaderPanel)
    void sourceFilesChanged(int count, qint64 totalBytes);
    void destinationsChanged(const QStringList& destinations);
    void rulesChanged(int count);
    void taskCreated(int taskId, const QString& fileName, const QString& destination);
    void taskProgress(int taskId, qint64 bytesUploaded, qint64 totalBytes, double speed);
    void taskCompleted(int taskId, bool success, const QString& message);
    void taskStatusChanged(int taskId, const QString& status);

    void uploadStarted(int totalTasks);
    void uploadProgress(int completedTasks, int totalTasks, qint64 bytesUploaded, qint64 totalBytes);
    void uploadComplete(int successful, int failed, int skipped);
    void uploadPaused();
    void uploadCancelled();

    void error(const QString& operation, const QString& message);

public slots:
    // Source file management
    void addFiles(const QStringList& filePaths);
    void addFolder(const QString& folderPath, bool recursive);
    void removeFile(const QString& filePath);
    void clearFiles();

    // Destination management
    void addDestination(const QString& remotePath);
    void removeDestination(const QString& remotePath);
    void clearDestinations();

    // Rule management
    void addRule(DistributionRule::RuleType type, const QString& pattern,
                 const QString& destination);
    void removeRule(int ruleId);
    void updateRule(int ruleId, const QString& pattern, const QString& destination);
    void setRuleEnabled(int ruleId, bool enabled);
    void clearRules();

    // Upload control
    void startUpload();
    void pauseUpload();
    void resumeUpload();
    void cancelUpload();
    void clearCompletedTasks();

    // Task management
    void retryFailedTask(int taskId);
    void retryAllFailed();

private:
    QString determineDestination(const QString& filePath, qint64 fileSize);
    void createUploadTasks();
    void processNextTask();
    void startFileUpload(UploadTask& task);
    int generateTaskId();
    int generateRuleId();

private:
    void* m_megaApi;
    std::atomic<bool> m_isUploading{false};
    std::atomic<bool> m_isPaused{false};
    std::atomic<bool> m_cancelRequested{false};

    QStringList m_sourceFiles;
    QHash<QString, qint64> m_fileSizes;  // Cache file sizes
    qint64 m_totalSourceBytes = 0;

    QStringList m_destinations;
    QVector<DistributionRule> m_rules;
    QVector<UploadTask> m_tasks;
    mutable QMutex m_tasksMutex;  // Protects m_tasks

    int m_nextTaskId = 1;
    int m_nextRuleId = 1;
    int m_currentTaskIndex = -1;

    // Statistics
    int m_successCount = 0;
    int m_failCount = 0;
    int m_skipCount = 0;
    qint64 m_bytesUploaded = 0;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_MULTIUPLOADERCONTROLLER_H
