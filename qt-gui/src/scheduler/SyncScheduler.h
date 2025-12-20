#ifndef SYNC_SCHEDULER_H
#define SYNC_SCHEDULER_H

#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <QVector>
#include <QString>
#include <memory>

namespace MegaCustom {

// Forward declarations
class FolderMapperController;
class SmartSyncController;
class MultiUploaderController;

/**
 * @brief Defines a scheduled task for automated execution
 */
struct ScheduledTask {
    enum class TaskType {
        FOLDER_MAPPING,
        SMART_SYNC,
        MULTI_UPLOAD
    };

    enum class RepeatMode {
        ONCE,
        HOURLY,
        DAILY,
        WEEKLY
    };

    int id = 0;
    QString name;
    TaskType type = TaskType::FOLDER_MAPPING;
    RepeatMode repeatMode = RepeatMode::ONCE;
    QDateTime nextRunTime;
    QDateTime lastRunTime;
    bool enabled = true;

    // Task-specific configuration
    QString localPath;
    QString remotePath;
    QString profileName;  // For sync profiles

    // Status
    bool isRunning = false;
    QString lastStatus;
    int consecutiveFailures = 0;
};

/**
 * @brief SyncScheduler manages automated execution of sync/upload tasks
 *
 * Uses QTimer to check for due tasks every 60 seconds (configurable).
 * Supports folder mapping, smart sync, and multi-upload task types.
 */
class SyncScheduler : public QObject {
    Q_OBJECT

public:
    explicit SyncScheduler(QObject* parent = nullptr);
    ~SyncScheduler();

    // Scheduler control
    void start();
    void stop();
    bool isRunning() const;

    // Configuration
    void setCheckInterval(int seconds);
    int checkInterval() const;

    // Task management
    int addTask(const ScheduledTask& task);
    bool removeTask(int taskId);
    bool updateTask(const ScheduledTask& task);
    ScheduledTask* getTask(int taskId);
    QVector<ScheduledTask> getAllTasks() const;

    // Enable/disable individual tasks
    void setTaskEnabled(int taskId, bool enabled);
    bool isTaskEnabled(int taskId) const;

    // Manual execution
    void runTaskNow(int taskId);

    // Controller connections
    void setFolderMapperController(FolderMapperController* controller);
    void setSmartSyncController(SmartSyncController* controller);
    void setMultiUploaderController(MultiUploaderController* controller);

    // Persistence
    void loadTasks();
    void saveTasks();

signals:
    void schedulerStarted();
    void schedulerStopped();
    void taskStarted(int taskId, const QString& taskName);
    void taskCompleted(int taskId, const QString& taskName, bool success, const QString& message);
    void taskProgress(int taskId, int percent, const QString& status);
    void tasksChanged();

private slots:
    void onTimerTick();
    void onFolderMapperProgress(const QString& mappingName, const QString& currentFile,
                                 int filesCompleted, int totalFiles,
                                 qint64 bytesUploaded, qint64 totalBytes,
                                 double speedBytesPerSec);
    void onFolderMapperComplete(const QString& mappingName, bool success,
                                 int filesUploaded, int filesSkipped, int filesFailed);

private:
    void executeTask(ScheduledTask& task);
    void executeFolderMapping(ScheduledTask& task);
    void executeSmartSync(ScheduledTask& task);
    void executeMultiUpload(ScheduledTask& task);
    void updateNextRunTime(ScheduledTask& task);
    int generateTaskId();

private:
    QTimer* m_checkTimer;
    int m_checkIntervalSec = 60;
    bool m_isRunning = false;

    QVector<ScheduledTask> m_tasks;
    int m_nextTaskId = 1;
    int m_currentRunningTaskId = -1;

    // Controller references
    FolderMapperController* m_folderMapperController = nullptr;
    SmartSyncController* m_smartSyncController = nullptr;
    MultiUploaderController* m_multiUploaderController = nullptr;
};

} // namespace MegaCustom

#endif // SYNC_SCHEDULER_H
