#include "MetricsStore.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QDebug>
#include <cmath>

namespace MegaCustom {

MetricsStore& MetricsStore::instance()
{
    static MetricsStore s_instance;
    return s_instance;
}

MetricsStore::MetricsStore(QObject* parent)
    : QObject(parent)
{
    initDatabase();
}

MetricsStore::~MetricsStore()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
}

void MetricsStore::initDatabase()
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    QString dbPath = configDir + "/metrics.db";

    // Use a unique connection name to avoid conflicts
    m_db = QSqlDatabase::addDatabase("QSQLITE", "metrics_store");
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        qWarning() << "MetricsStore: Failed to open database:" << m_db.lastError().text();
        return;
    }

    // Enable WAL mode for better concurrent read/write performance
    QSqlQuery pragma(m_db);
    pragma.exec("PRAGMA journal_mode=WAL");
    pragma.exec("PRAGMA synchronous=NORMAL");

    initSchema();
    qDebug() << "MetricsStore: Initialized at" << dbPath;
}

void MetricsStore::initSchema()
{
    QSqlQuery q(m_db);

    q.exec(R"(
        CREATE TABLE IF NOT EXISTS job_metrics (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp INTEGER NOT NULL,
            job_type TEXT NOT NULL,
            file_category TEXT NOT NULL,
            member_id TEXT,
            input_size_bytes INTEGER,
            output_size_bytes INTEGER,
            duration_ms INTEGER,
            success INTEGER NOT NULL,
            error_code TEXT,
            disk_free_before INTEGER,
            disk_free_after INTEGER,
            upload_speed_bps INTEGER
        )
    )");

    q.exec(R"(
        CREATE TABLE IF NOT EXISTS ema_cache (
            category TEXT PRIMARY KEY,
            size_ratio REAL,
            duration_per_mb REAL,
            upload_speed_bps REAL,
            variance REAL,
            sample_count INTEGER,
            last_updated INTEGER
        )
    )");

    // Indexes for common queries
    q.exec("CREATE INDEX IF NOT EXISTS idx_metrics_category ON job_metrics(file_category, timestamp)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_metrics_type ON job_metrics(job_type, success)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_metrics_timestamp ON job_metrics(timestamp)");
}

// =============================================================================
// Recording
// =============================================================================

void MetricsStore::recordWatermark(const QString& fileCategory, const QString& memberId,
                                    qint64 inputSize, qint64 outputSize, qint64 durationMs,
                                    bool success, const QString& error,
                                    qint64 diskFreeBefore, qint64 diskFreeAfter)
{
    QMutexLocker lock(&m_mutex);

    QString jobType = fileCategory == "pdf" ? "watermark_pdf" : "watermark_video";

    QSqlQuery q(m_db);
    q.prepare("INSERT INTO job_metrics (timestamp, job_type, file_category, member_id, "
              "input_size_bytes, output_size_bytes, duration_ms, success, error_code, "
              "disk_free_before, disk_free_after, upload_speed_bps) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, NULL)");
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.addBindValue(jobType);
    q.addBindValue(fileCategory);
    q.addBindValue(memberId);
    q.addBindValue(inputSize);
    q.addBindValue(outputSize);
    q.addBindValue(durationMs);
    q.addBindValue(success ? 1 : 0);
    q.addBindValue(error.isEmpty() ? QVariant() : error);
    q.addBindValue(diskFreeBefore);
    q.addBindValue(diskFreeAfter);

    if (!q.exec()) {
        qWarning() << "MetricsStore: Failed to record watermark:" << q.lastError().text();
    }

    // Update EMA if successful and data is valid
    if (success && inputSize > 0 && outputSize > 0) {
        double sizeRatio = static_cast<double>(outputSize) / inputSize;
        double inputMb = inputSize / (1024.0 * 1024.0);
        double durationPerMb = (inputMb > 0) ? (durationMs / inputMb) : 0;
        QString emaCategory = "watermark:" + fileCategory;
        updateWatermarkEma(emaCategory, sizeRatio, durationPerMb);
    }
}

void MetricsStore::recordUpload(const QString& fileCategory, const QString& memberId,
                                 qint64 fileSize, qint64 durationMs, qint64 speedBps,
                                 bool success, const QString& error)
{
    QMutexLocker lock(&m_mutex);

    QSqlQuery q(m_db);
    q.prepare("INSERT INTO job_metrics (timestamp, job_type, file_category, member_id, "
              "input_size_bytes, output_size_bytes, duration_ms, success, error_code, "
              "disk_free_before, disk_free_after, upload_speed_bps) "
              "VALUES (?, 'upload', ?, ?, ?, NULL, ?, ?, ?, NULL, NULL, ?)");
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.addBindValue(fileCategory);
    q.addBindValue(memberId);
    q.addBindValue(fileSize);
    q.addBindValue(durationMs);
    q.addBindValue(success ? 1 : 0);
    q.addBindValue(error.isEmpty() ? QVariant() : error);
    q.addBindValue(speedBps);

    if (!q.exec()) {
        qWarning() << "MetricsStore: Failed to record upload:" << q.lastError().text();
    }

    // Update upload speed EMA if successful
    if (success && speedBps > 0) {
        updateUploadEma(static_cast<double>(speedBps));
    }
}

// =============================================================================
// EMA Updates
// =============================================================================

void MetricsStore::updateWatermarkEma(const QString& category, double sizeRatio, double durationPerMb)
{
    // Read current EMA
    QSqlQuery q(m_db);
    q.prepare("SELECT size_ratio, duration_per_mb, variance, sample_count FROM ema_cache WHERE category = ?");
    q.addBindValue(category);
    q.exec();

    if (q.next()) {
        double oldRatio = q.value(0).toDouble();
        double oldDuration = q.value(1).toDouble();
        double oldVariance = q.value(2).toDouble();
        int count = q.value(3).toInt();

        // EMA update
        double error = sizeRatio - oldRatio;
        double newVariance = ALPHA * (error * error) + (1.0 - ALPHA) * oldVariance;
        double newRatio = ALPHA * sizeRatio + (1.0 - ALPHA) * oldRatio;
        double newDuration = ALPHA * durationPerMb + (1.0 - ALPHA) * oldDuration;

        QSqlQuery upd(m_db);
        upd.prepare("UPDATE ema_cache SET size_ratio = ?, duration_per_mb = ?, "
                     "variance = ?, sample_count = ?, last_updated = ? WHERE category = ?");
        upd.addBindValue(newRatio);
        upd.addBindValue(newDuration);
        upd.addBindValue(newVariance);
        upd.addBindValue(count + 1);
        upd.addBindValue(QDateTime::currentSecsSinceEpoch());
        upd.addBindValue(category);
        upd.exec();
    } else {
        // First data point — seed the cache
        QSqlQuery ins(m_db);
        ins.prepare("INSERT INTO ema_cache (category, size_ratio, duration_per_mb, "
                     "upload_speed_bps, variance, sample_count, last_updated) "
                     "VALUES (?, ?, ?, 0, 0, 1, ?)");
        ins.addBindValue(category);
        ins.addBindValue(sizeRatio);
        ins.addBindValue(durationPerMb);
        ins.addBindValue(QDateTime::currentSecsSinceEpoch());
        ins.exec();
    }
}

void MetricsStore::updateUploadEma(double speedBps)
{
    QString category = "upload:speed";

    QSqlQuery q(m_db);
    q.prepare("SELECT upload_speed_bps, variance, sample_count FROM ema_cache WHERE category = ?");
    q.addBindValue(category);
    q.exec();

    if (q.next()) {
        double oldSpeed = q.value(0).toDouble();
        double oldVariance = q.value(1).toDouble();
        int count = q.value(2).toInt();

        double error = speedBps - oldSpeed;
        double newVariance = ALPHA * (error * error) + (1.0 - ALPHA) * oldVariance;
        double newSpeed = ALPHA * speedBps + (1.0 - ALPHA) * oldSpeed;

        QSqlQuery upd(m_db);
        upd.prepare("UPDATE ema_cache SET upload_speed_bps = ?, variance = ?, "
                     "sample_count = ?, last_updated = ? WHERE category = ?");
        upd.addBindValue(newSpeed);
        upd.addBindValue(newVariance);
        upd.addBindValue(count + 1);
        upd.addBindValue(QDateTime::currentSecsSinceEpoch());
        upd.addBindValue(category);
        upd.exec();
    } else {
        QSqlQuery ins(m_db);
        ins.prepare("INSERT INTO ema_cache (category, size_ratio, duration_per_mb, "
                     "upload_speed_bps, variance, sample_count, last_updated) "
                     "VALUES (?, 0, 0, ?, 0, 1, ?)");
        ins.addBindValue(category);
        ins.addBindValue(speedBps);
        ins.addBindValue(QDateTime::currentSecsSinceEpoch());
        ins.exec();
    }
}

// =============================================================================
// Predictions
// =============================================================================

MetricsStore::EmaData MetricsStore::getEmaData(const QString& category) const
{
    EmaData data;
    QSqlQuery q(m_db);
    q.prepare("SELECT size_ratio, duration_per_mb, upload_speed_bps, variance, sample_count "
              "FROM ema_cache WHERE category = ?");
    q.addBindValue(category);
    q.exec();

    if (q.next()) {
        data.sizeRatio = q.value(0).toDouble();
        data.durationPerMb = q.value(1).toDouble();
        data.uploadSpeedBps = q.value(2).toDouble();
        data.variance = q.value(3).toDouble();
        data.sampleCount = q.value(4).toInt();
    }
    return data;
}

qint64 MetricsStore::predictOutputSize(const QString& fileCategory, qint64 inputSize) const
{
    QMutexLocker lock(&m_mutex);
    QString category = "watermark:" + fileCategory;
    EmaData ema = getEmaData(category);

    double ratio = (ema.sampleCount >= 3) ? ema.sizeRatio : DEFAULT_SIZE_RATIO;

    // Add safety margin based on confidence (more samples = less margin)
    double margin = 1.0;
    if (ema.sampleCount < 5)
        margin = 1.5;  // 50% margin when learning
    else if (ema.sampleCount < 20)
        margin = 1.0 + std::sqrt(ema.variance) * 2.0;  // 2-sigma margin
    else
        margin = 1.0 + std::sqrt(ema.variance);  // 1-sigma margin when confident

    margin = std::max(1.05, std::min(margin, 2.0));  // Clamp between 5% and 100%

    return static_cast<qint64>(inputSize * ratio * margin);
}

qint64 MetricsStore::predictWatermarkDuration(const QString& fileCategory, qint64 inputSize) const
{
    QMutexLocker lock(&m_mutex);
    QString category = "watermark:" + fileCategory;
    EmaData ema = getEmaData(category);

    double durationPerMb = (ema.sampleCount >= 3) ? ema.durationPerMb : DEFAULT_DURATION_PER_MB;
    double inputMb = inputSize / (1024.0 * 1024.0);

    return static_cast<qint64>(inputMb * durationPerMb);
}

qint64 MetricsStore::predictUploadDuration(qint64 fileSize) const
{
    QMutexLocker lock(&m_mutex);
    EmaData ema = getEmaData("upload:speed");

    double speed = (ema.sampleCount >= 3) ? ema.uploadSpeedBps : DEFAULT_UPLOAD_SPEED;
    if (speed <= 0) speed = DEFAULT_UPLOAD_SPEED;

    return static_cast<qint64>((fileSize / speed) * 1000.0);  // ms
}

double MetricsStore::predictionConfidence(const QString& fileCategory) const
{
    QMutexLocker lock(&m_mutex);
    QString category = "watermark:" + fileCategory;
    EmaData ema = getEmaData(category);

    if (ema.sampleCount == 0) return 0.0;

    // Confidence based on sample count and variance
    double sampleFactor = std::min(1.0, ema.sampleCount / 20.0);
    double varianceFactor = 1.0 / (1.0 + std::sqrt(ema.variance));
    return sampleFactor * varianceFactor;
}

// =============================================================================
// Aggregate Queries
// =============================================================================

double MetricsStore::recentSuccessRate(const QString& jobType, int days) const
{
    QMutexLocker lock(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare("SELECT AVG(CAST(success AS REAL)) FROM job_metrics "
              "WHERE job_type = ? AND timestamp > ?");
    q.addBindValue(jobType);
    q.addBindValue(QDateTime::currentSecsSinceEpoch() - days * 86400);
    q.exec();

    if (q.next() && !q.value(0).isNull()) {
        return q.value(0).toDouble();
    }
    return 1.0;  // Default to 100% if no data
}

qint64 MetricsStore::averageUploadSpeed(int lastN) const
{
    QMutexLocker lock(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare("SELECT AVG(upload_speed_bps) FROM ("
              "  SELECT upload_speed_bps FROM job_metrics "
              "  WHERE job_type = 'upload' AND success = 1 AND upload_speed_bps > 0 "
              "  ORDER BY timestamp DESC LIMIT ?"
              ")");
    q.addBindValue(lastN);
    q.exec();

    if (q.next() && !q.value(0).isNull()) {
        return q.value(0).toLongLong();
    }
    return static_cast<qint64>(DEFAULT_UPLOAD_SPEED);
}

int MetricsStore::totalOperations() const
{
    QMutexLocker lock(&m_mutex);
    QSqlQuery q(m_db);
    q.exec("SELECT COUNT(*) FROM job_metrics");

    if (q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}

} // namespace MegaCustom
