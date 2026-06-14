#include "OperationJobStore.h"
#include "Settings.h"
#include "core/LogManager.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>

namespace MegaCustom {

namespace {
constexpr int kMaxJobs = 500;
constexpr int kProgressPersistThrottleSeconds = 5;

QStringList jsonArrayToStringList(const QJsonArray& array) {
    QStringList values;
    values.reserve(array.size());
    for (const QJsonValue& value : array) {
        values.append(value.toString());
    }
    return values;
}

QJsonArray stringListToJsonArray(const QStringList& values) {
    QJsonArray array;
    for (const QString& value : values) {
        array.append(value);
    }
    return array;
}

QString formatDate(const QDateTime& dateTime) {
    return dateTime.isValid()
        ? dateTime.toUTC().toString(Qt::ISODateWithMs)
        : QString();
}

QDateTime parseDate(const QJsonValue& value) {
    const QString text = value.toString();
    if (text.isEmpty()) {
        return {};
    }

    QDateTime parsed = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(text, Qt::ISODate);
    }
    return parsed;
}

LogCategory categoryForType(OperationJobType type) {
    switch (type) {
    case OperationJobType::Download:
        return LogCategory::Download;
    case OperationJobType::Watermark:
        return LogCategory::Watermark;
    case OperationJobType::Distribution:
        return LogCategory::Distribution;
    case OperationJobType::Unknown:
    default:
        return LogCategory::System;
    }
}
} // namespace

bool OperationJobRecord::isTerminal() const {
    return status == OperationJobStatus::Completed
        || status == OperationJobStatus::Failed
        || status == OperationJobStatus::Cancelled
        || status == OperationJobStatus::CleanupRequired;
}

QJsonObject OperationJobRecord::toJson() const {
    QJsonObject object;
    object["id"] = id;
    object["type"] = OperationJobStore::typeToString(type);
    object["status"] = OperationJobStore::statusToString(status);
    object["title"] = title;
    object["summary"] = summary;
    object["lastError"] = lastError;
    object["createdAt"] = formatDate(createdAt);
    object["startedAt"] = formatDate(startedAt);
    object["updatedAt"] = formatDate(updatedAt);
    object["finishedAt"] = formatDate(finishedAt);
    object["plannedCount"] = plannedCount;
    object["completedCount"] = completedCount;
    object["failedCount"] = failedCount;
    object["skippedCount"] = skippedCount;
    object["sourceRoots"] = stringListToJsonArray(sourceRoots);
    object["destinationRoots"] = stringListToJsonArray(destinationRoots);
    object["memberIds"] = stringListToJsonArray(memberIds);
    object["metadata"] = metadata;
    return object;
}

OperationJobRecord OperationJobRecord::fromJson(const QJsonObject& object) {
    OperationJobRecord record;
    record.id = object["id"].toString();
    record.type = OperationJobStore::typeFromString(object["type"].toString());
    record.status = OperationJobStore::statusFromString(object["status"].toString());
    record.title = object["title"].toString();
    record.summary = object["summary"].toString();
    record.lastError = object["lastError"].toString();
    record.createdAt = parseDate(object["createdAt"]);
    record.startedAt = parseDate(object["startedAt"]);
    record.updatedAt = parseDate(object["updatedAt"]);
    record.finishedAt = parseDate(object["finishedAt"]);
    record.plannedCount = object["plannedCount"].toInt();
    record.completedCount = object["completedCount"].toInt();
    record.failedCount = object["failedCount"].toInt();
    record.skippedCount = object["skippedCount"].toInt();
    record.sourceRoots = jsonArrayToStringList(object["sourceRoots"].toArray());
    record.destinationRoots = jsonArrayToStringList(object["destinationRoots"].toArray());
    record.memberIds = jsonArrayToStringList(object["memberIds"].toArray());
    record.metadata = object["metadata"].toObject();

    if (!record.createdAt.isValid()) {
        record.createdAt = QDateTime::currentDateTimeUtc();
    }
    if (!record.updatedAt.isValid()) {
        record.updatedAt = record.createdAt;
    }
    return record;
}

OperationJobStore& OperationJobStore::instance() {
    static OperationJobStore store;
    return store;
}

OperationJobStore::OperationJobStore() = default;

QString OperationJobStore::createJob(OperationJobType type,
                                     const QString& title,
                                     int plannedCount,
                                     const QJsonObject& metadata,
                                     const QString& preferredId) {
    QMutexLocker locker(&m_mutex);
    loadLocked();

    const QString id = preferredId.trimmed().isEmpty()
        ? generateJobId(type)
        : preferredId.trimmed();

    OperationJobRecord* existing = findJobLocked(id);
    if (existing) {
        if (!title.trimmed().isEmpty()) {
            existing->title = title.trimmed();
        }
        existing->type = type;
        existing->plannedCount = qMax(existing->plannedCount, plannedCount);
        existing->metadata = metadata;
        existing->updatedAt = QDateTime::currentDateTimeUtc();
        persistSoonLocked(*existing, true);
        return existing->id;
    }

    OperationJobRecord record;
    record.id = id;
    record.type = type;
    record.status = OperationJobStatus::Queued;
    record.title = title.trimmed().isEmpty()
        ? QString("%1 job").arg(typeToString(type))
        : title.trimmed();
    record.plannedCount = qMax(0, plannedCount);
    record.metadata = metadata;
    record.createdAt = QDateTime::currentDateTimeUtc();
    record.updatedAt = record.createdAt;

    m_jobs.append(record);
    trimLocked();
    persistSoonLocked(record, true);
    logJobEventLocked(record, "job.created", QString("Created job: %1").arg(record.title));
    return record.id;
}

void OperationJobStore::markRunning(const QString& jobId, const QString& summary) {
    QMutexLocker locker(&m_mutex);
    loadLocked();
    OperationJobRecord* record = findJobLocked(jobId);
    if (!record || record->isTerminal()) {
        return;
    }
    setStatusLocked(*record, OperationJobStatus::Running, summary, false, true);
}

void OperationJobStore::markPaused(const QString& jobId, const QString& summary) {
    QMutexLocker locker(&m_mutex);
    loadLocked();
    OperationJobRecord* record = findJobLocked(jobId);
    if (!record || record->isTerminal()) {
        return;
    }
    setStatusLocked(*record, OperationJobStatus::Paused, summary, false, true);
}

void OperationJobStore::markCompleted(const QString& jobId, int completed, int failed,
                                      int skipped, const QString& summary) {
    QMutexLocker locker(&m_mutex);
    loadLocked();
    OperationJobRecord* record = findJobLocked(jobId);
    if (!record || (record->isTerminal() && record->status != OperationJobStatus::Completed)) {
        return;
    }
    record->completedCount = qMax(0, completed);
    record->failedCount = qMax(0, failed);
    record->skippedCount = qMax(0, skipped);
    setStatusLocked(*record, OperationJobStatus::Completed, summary, true, true);
}

void OperationJobStore::markFailed(const QString& jobId, const QString& error,
                                   int completed, int failed, int skipped) {
    QMutexLocker locker(&m_mutex);
    loadLocked();
    OperationJobRecord* record = findJobLocked(jobId);
    if (!record || (record->isTerminal() && record->status != OperationJobStatus::Failed)) {
        return;
    }
    record->completedCount = qMax(0, completed);
    record->failedCount = qMax(0, failed);
    record->skippedCount = qMax(0, skipped);
    record->lastError = error;
    setStatusLocked(*record, OperationJobStatus::Failed, error, true, true);
}

void OperationJobStore::markCancelled(const QString& jobId, const QString& summary) {
    QMutexLocker locker(&m_mutex);
    loadLocked();
    OperationJobRecord* record = findJobLocked(jobId);
    if (!record || (record->isTerminal() && record->status != OperationJobStatus::Cancelled)) {
        return;
    }
    setStatusLocked(*record, OperationJobStatus::Cancelled, summary, true, true);
}

void OperationJobStore::markCleanupRequired(const QString& jobId, const QString& summary) {
    QMutexLocker locker(&m_mutex);
    loadLocked();
    OperationJobRecord* record = findJobLocked(jobId);
    if (!record || (record->isTerminal() && record->status != OperationJobStatus::CleanupRequired)) {
        return;
    }
    setStatusLocked(*record, OperationJobStatus::CleanupRequired, summary, true, true);
}

void OperationJobStore::updateProgress(const QString& jobId, int completed, int failed,
                                       int skipped, const QString& summary) {
    if (jobId.trimmed().isEmpty()) {
        return;
    }

    QMutexLocker locker(&m_mutex);
    loadLocked();
    OperationJobRecord* record = findJobLocked(jobId);
    if (!record || record->isTerminal()) {
        return;
    }

    const bool countsChanged = record->completedCount != completed
        || record->failedCount != failed
        || record->skippedCount != skipped;
    record->completedCount = qMax(0, completed);
    record->failedCount = qMax(0, failed);
    record->skippedCount = qMax(0, skipped);
    if (!summary.trimmed().isEmpty()) {
        record->summary = summary.trimmed();
    }
    record->updatedAt = QDateTime::currentDateTimeUtc();
    persistSoonLocked(*record, countsChanged);
}

void OperationJobStore::setLastError(const QString& jobId, const QString& error) {
    if (jobId.trimmed().isEmpty() || error.trimmed().isEmpty()) {
        return;
    }

    QMutexLocker locker(&m_mutex);
    loadLocked();
    OperationJobRecord* record = findJobLocked(jobId);
    if (!record) {
        return;
    }

    record->lastError = error.trimmed();
    record->updatedAt = QDateTime::currentDateTimeUtc();
    persistSoonLocked(*record, true);

    QJsonObject details;
    details["error"] = record->lastError;
    logJobEventLocked(*record, "job.error", record->lastError, details);
}

OperationJobRecord OperationJobStore::job(const QString& jobId) const {
    QMutexLocker locker(&m_mutex);
    const_cast<OperationJobStore*>(this)->loadLocked();
    for (const OperationJobRecord& record : m_jobs) {
        if (record.id == jobId) {
            return record;
        }
    }
    return {};
}

QList<OperationJobRecord> OperationJobStore::recentJobs(int limit) const {
    QMutexLocker locker(&m_mutex);
    const_cast<OperationJobStore*>(this)->loadLocked();

    QList<OperationJobRecord> records = m_jobs;
    std::sort(records.begin(), records.end(), [](const OperationJobRecord& a,
                                                 const OperationJobRecord& b) {
        return a.updatedAt > b.updatedAt;
    });

    if (limit > 0 && records.size() > limit) {
        records.erase(records.begin() + limit, records.end());
    }
    return records;
}

QString OperationJobStore::typeToString(OperationJobType type) {
    switch (type) {
    case OperationJobType::Download:
        return "download";
    case OperationJobType::Watermark:
        return "watermark";
    case OperationJobType::Distribution:
        return "distribution";
    case OperationJobType::Unknown:
    default:
        return "unknown";
    }
}

OperationJobType OperationJobStore::typeFromString(const QString& value) {
    const QString normalized = value.toLower();
    if (normalized == "download") {
        return OperationJobType::Download;
    }
    if (normalized == "watermark") {
        return OperationJobType::Watermark;
    }
    if (normalized == "distribution") {
        return OperationJobType::Distribution;
    }
    return OperationJobType::Unknown;
}

QString OperationJobStore::statusToString(OperationJobStatus status) {
    switch (status) {
    case OperationJobStatus::Queued:
        return "queued";
    case OperationJobStatus::Running:
        return "running";
    case OperationJobStatus::Paused:
        return "paused";
    case OperationJobStatus::Completed:
        return "completed";
    case OperationJobStatus::Failed:
        return "failed";
    case OperationJobStatus::Cancelled:
        return "cancelled";
    case OperationJobStatus::CleanupRequired:
        return "cleanup_required";
    default:
        return "queued";
    }
}

OperationJobStatus OperationJobStore::statusFromString(const QString& value) {
    const QString normalized = value.toLower();
    if (normalized == "running") {
        return OperationJobStatus::Running;
    }
    if (normalized == "paused") {
        return OperationJobStatus::Paused;
    }
    if (normalized == "completed") {
        return OperationJobStatus::Completed;
    }
    if (normalized == "failed") {
        return OperationJobStatus::Failed;
    }
    if (normalized == "cancelled" || normalized == "canceled") {
        return OperationJobStatus::Cancelled;
    }
    if (normalized == "cleanup_required") {
        return OperationJobStatus::CleanupRequired;
    }
    return OperationJobStatus::Queued;
}

QString OperationJobStore::storagePath() const {
    QDir dir(Settings::instance().configDirectory());
    dir.mkpath(".");
    return dir.filePath("operation_jobs.json");
}

QString OperationJobStore::generateJobId(OperationJobType type) {
    QString prefix = "job";
    switch (type) {
    case OperationJobType::Download:
        prefix = "dl";
        break;
    case OperationJobType::Watermark:
        prefix = "wm";
        break;
    case OperationJobType::Distribution:
        prefix = "dist";
        break;
    case OperationJobType::Unknown:
    default:
        break;
    }

    ++m_counter;
    return QString("%1_%2_%3")
        .arg(prefix)
        .arg(QDateTime::currentDateTimeUtc().toString("yyyyMMdd_hhmmss_zzz"))
        .arg(m_counter, 3, 10, QLatin1Char('0'));
}

OperationJobRecord* OperationJobStore::findJobLocked(const QString& jobId) {
    if (jobId.trimmed().isEmpty()) {
        return nullptr;
    }
    for (OperationJobRecord& record : m_jobs) {
        if (record.id == jobId) {
            return &record;
        }
    }
    return nullptr;
}

void OperationJobStore::loadLocked() {
    if (m_loaded) {
        return;
    }
    m_loaded = true;

    QFile file(storagePath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return;
    }

    QJsonParseError error;
    QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return;
    }

    const QJsonArray jobs = document.object()["jobs"].toArray();
    for (const QJsonValue& value : jobs) {
        OperationJobRecord record = OperationJobRecord::fromJson(value.toObject());
        if (!record.id.trimmed().isEmpty()) {
            m_jobs.append(record);
        }
    }
    trimLocked();
}

void OperationJobStore::trimLocked() {
    if (m_jobs.size() <= kMaxJobs) {
        return;
    }

    std::sort(m_jobs.begin(), m_jobs.end(), [](const OperationJobRecord& a,
                                               const OperationJobRecord& b) {
        return a.updatedAt < b.updatedAt;
    });

    while (m_jobs.size() > kMaxJobs) {
        m_lastPersistByJob.remove(m_jobs.first().id);
        m_jobs.removeFirst();
    }
}

void OperationJobStore::persistLocked() {
    QJsonArray jobs;
    for (const OperationJobRecord& record : m_jobs) {
        jobs.append(record.toJson());
    }

    QJsonObject root;
    root["version"] = 1;
    root["jobs"] = jobs;

    QSaveFile file(storagePath());
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.commit();
}

void OperationJobStore::persistSoonLocked(const OperationJobRecord& record, bool force) {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime last = m_lastPersistByJob.value(record.id);
    const bool due = !last.isValid()
        || last.secsTo(now) >= kProgressPersistThrottleSeconds;

    if (force || due || record.isTerminal()) {
        m_lastPersistByJob[record.id] = now;
        persistLocked();
    }
}

void OperationJobStore::setStatusLocked(OperationJobRecord& record,
                                        OperationJobStatus status,
                                        const QString& summary,
                                        bool terminal,
                                        bool forceLog) {
    const OperationJobStatus previous = record.status;
    record.status = status;
    if (!summary.trimmed().isEmpty()) {
        record.summary = summary.trimmed();
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (status == OperationJobStatus::Running && !record.startedAt.isValid()) {
        record.startedAt = now;
    }
    if (terminal) {
        record.finishedAt = now;
    }
    record.updatedAt = now;

    persistSoonLocked(record, true);

    if (forceLog || previous != status) {
        const QString eventName = QString("job.%1").arg(statusToString(status));
        const QString message = record.summary.trimmed().isEmpty()
            ? QString("%1 is %2").arg(record.title, statusToString(status))
            : record.summary;
        logJobEventLocked(record, eventName, message);
    }
}

void OperationJobStore::logJobEventLocked(const OperationJobRecord& record,
                                          const QString& eventName,
                                          const QString& message,
                                          const QJsonObject& extraDetails) {
    QJsonObject details = extraDetails;
    details["type"] = typeToString(record.type);
    details["status"] = statusToString(record.status);
    details["title"] = record.title;
    details["summary"] = record.summary;
    details["plannedCount"] = record.plannedCount;
    details["completedCount"] = record.completedCount;
    details["failedCount"] = record.failedCount;
    details["skippedCount"] = record.skippedCount;
    if (!record.lastError.isEmpty()) {
        details["lastError"] = record.lastError;
    }

    LogLevel level = LogLevel::Info;
    if (record.status == OperationJobStatus::Paused) {
        level = LogLevel::Warning;
    } else if (record.status == OperationJobStatus::Failed
               || record.status == OperationJobStatus::CleanupRequired) {
        level = LogLevel::Error;
    }

    LogManager::instance().logWithContext(
        level,
        categoryForType(record.type),
        eventName.toStdString(),
        message.toStdString(),
        "",
        "",
        record.id.toStdString(),
        QString::fromUtf8(QJsonDocument(details).toJson(QJsonDocument::Compact)).toStdString());
}

} // namespace MegaCustom
