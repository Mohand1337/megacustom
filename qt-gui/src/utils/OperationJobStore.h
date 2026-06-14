#ifndef MEGACUSTOM_OPERATIONJOBSTORE_H
#define MEGACUSTOM_OPERATIONJOBSTORE_H

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QString>
#include <QStringList>

namespace MegaCustom {

enum class OperationJobType {
    Unknown,
    Download,
    Watermark,
    Distribution
};

enum class OperationJobStatus {
    Queued,
    Running,
    Paused,
    Completed,
    Failed,
    Cancelled,
    CleanupRequired
};

struct OperationJobRecord {
    QString id;
    OperationJobType type = OperationJobType::Unknown;
    OperationJobStatus status = OperationJobStatus::Queued;
    QString title;
    QString summary;
    QString lastError;
    QDateTime createdAt;
    QDateTime startedAt;
    QDateTime updatedAt;
    QDateTime finishedAt;
    int plannedCount = 0;
    int completedCount = 0;
    int failedCount = 0;
    int skippedCount = 0;
    QStringList sourceRoots;
    QStringList destinationRoots;
    QStringList memberIds;
    QJsonObject metadata;

    bool isTerminal() const;
    QJsonObject toJson() const;
    static OperationJobRecord fromJson(const QJsonObject& object);
};

class OperationJobStore {
public:
    static OperationJobStore& instance();

    QString createJob(OperationJobType type,
                      const QString& title,
                      int plannedCount = 0,
                      const QJsonObject& metadata = {},
                      const QString& preferredId = {});

    void markRunning(const QString& jobId, const QString& summary = {});
    void markPaused(const QString& jobId, const QString& summary = {});
    void markCompleted(const QString& jobId, int completed, int failed = 0,
                       int skipped = 0, const QString& summary = {});
    void markFailed(const QString& jobId, const QString& error,
                    int completed = 0, int failed = 0, int skipped = 0);
    void markCancelled(const QString& jobId, const QString& summary = {});
    void markCleanupRequired(const QString& jobId, const QString& summary = {});

    void updateProgress(const QString& jobId, int completed, int failed,
                        int skipped = 0, const QString& summary = {});
    void setLastError(const QString& jobId, const QString& error);

    OperationJobRecord job(const QString& jobId) const;
    QList<OperationJobRecord> recentJobs(int limit = 100) const;
    void clearAll();

    static QString typeToString(OperationJobType type);
    static OperationJobType typeFromString(const QString& value);
    static QString statusToString(OperationJobStatus status);
    static OperationJobStatus statusFromString(const QString& value);

private:
    OperationJobStore();

    QString storagePath() const;
    QString generateJobId(OperationJobType type);
    OperationJobRecord* findJobLocked(const QString& jobId);
    void loadLocked();
    void trimLocked();
    void persistLocked();
    void persistSoonLocked(const OperationJobRecord& record, bool force);
    void setStatusLocked(OperationJobRecord& record,
                         OperationJobStatus status,
                         const QString& summary,
                         bool terminal,
                         bool forceLog);
    void logJobEventLocked(const OperationJobRecord& record,
                           const QString& eventName,
                           const QString& message,
                           const QJsonObject& extraDetails = {});

    mutable QMutex m_mutex;
    QList<OperationJobRecord> m_jobs;
    QHash<QString, QDateTime> m_lastPersistByJob;
    int m_counter = 0;
    bool m_loaded = false;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_OPERATIONJOBSTORE_H
