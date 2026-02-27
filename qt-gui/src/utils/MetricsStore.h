#ifndef MEGACUSTOM_METRICSSTORE_H
#define MEGACUSTOM_METRICSSTORE_H

#include <QObject>
#include <QString>
#include <QSqlDatabase>
#include <QMutex>

namespace MegaCustom {

/**
 * SQLite-backed metrics database that records every watermark and upload
 * operation. Maintains EMA (Exponential Moving Average) caches that learn
 * from historical data to predict output sizes, durations, and upload speeds.
 *
 * The store gets smarter over time:
 *   0 runs:    conservative defaults (1.1x ratio, 50% safety margin)
 *   1-5 runs:  "learning..." — EMA adapting, rough estimates
 *   5-20 runs: "improving" — useful predictions, ~20% margin
 *   20+ runs:  "confident" — high-accuracy predictions, minimal margin
 *
 * Thread-safe: all public methods are protected by a mutex.
 */
class MetricsStore : public QObject {
    Q_OBJECT

public:
    static MetricsStore& instance();

    // Record a completed watermark operation
    void recordWatermark(const QString& fileCategory, const QString& memberId,
                         qint64 inputSize, qint64 outputSize, qint64 durationMs,
                         bool success, const QString& error = {},
                         qint64 diskFreeBefore = 0, qint64 diskFreeAfter = 0);

    // Record a completed upload operation
    void recordUpload(const QString& fileCategory, const QString& memberId,
                      qint64 fileSize, qint64 durationMs, qint64 speedBps,
                      bool success, const QString& error = {});

    // --- Predictions (from EMA cache) ---

    // Predict output file size for a given input size and file category
    // Returns conservative default if insufficient data
    qint64 predictOutputSize(const QString& fileCategory, qint64 inputSize) const;

    // Predict watermark processing duration (ms) for a given input size
    qint64 predictWatermarkDuration(const QString& fileCategory, qint64 inputSize) const;

    // Predict upload duration (ms) for a given file size
    qint64 predictUploadDuration(qint64 fileSize) const;

    // How confident are we in predictions for this category? (0.0 - 1.0)
    double predictionConfidence(const QString& fileCategory) const;

    // --- Aggregate queries ---

    // Recent success rate for a job type (e.g., "watermark_video", "upload")
    double recentSuccessRate(const QString& jobType, int days = 7) const;

    // Average upload speed from recent successful uploads (bytes/sec)
    qint64 averageUploadSpeed(int lastN = 20) const;

    // Total operations recorded (all time)
    int totalOperations() const;

private:
    explicit MetricsStore(QObject* parent = nullptr);
    ~MetricsStore() override;

    void initDatabase();
    void initSchema();

    // Update EMA cache after recording a new data point
    void updateWatermarkEma(const QString& fileCategory, double sizeRatio, double durationPerMb);
    void updateUploadEma(double speedBps);

    // Read EMA cache values
    struct EmaData {
        double sizeRatio = 1.1;
        double durationPerMb = 500.0;  // ms per MB default
        double uploadSpeedBps = 0;
        double variance = 0;
        int sampleCount = 0;
    };
    EmaData getEmaData(const QString& category) const;

    QSqlDatabase m_db;
    mutable QMutex m_mutex;

    static constexpr double ALPHA = 0.2;         // EMA adaptation rate
    static constexpr double DEFAULT_SIZE_RATIO = 1.1;
    static constexpr double DEFAULT_DURATION_PER_MB = 500.0;  // ms
    static constexpr double DEFAULT_UPLOAD_SPEED = 2 * 1024 * 1024;  // 2 MB/s
};

} // namespace MegaCustom

#endif // MEGACUSTOM_METRICSSTORE_H
