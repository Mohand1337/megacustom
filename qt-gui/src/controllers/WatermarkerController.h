#ifndef MEGACUSTOM_WATERMARKERCONTROLLER_H
#define MEGACUSTOM_WATERMARKERCONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <memory>

namespace MegaCustom {

class Watermarker;
struct WatermarkConfig;
struct WatermarkResult;
struct WatermarkProgress;

/**
 * Qt wrapper for WatermarkConfig
 */
struct QtWatermarkConfig {
    QString primaryText;
    QString secondaryText;
    int intervalSeconds = 600;
    int durationSeconds = 3;
    double randomGate = 0.15;
    QString fontPath;
    int primaryFontSize = 26;
    int secondaryFontSize = 22;
    QString primaryColor = "#d4a760";
    QString secondaryColor = "white";
    QString preset = "ultrafast";
    int crf = 23;
    bool copyAudio = true;
    double pdfOpacity = 0.3;
    int pdfAngle = 45;
    double pdfCoverage = 0.5;
    QString pdfPassword;
    QString outputSuffix = "_wm";
    bool overwrite = true;
};

/**
 * Qt wrapper for WatermarkResult
 */
struct QtWatermarkResult {
    bool success = false;
    QString inputFile;
    QString outputFile;
    QString error;
    qint64 processingTimeMs = 0;
    qint64 inputSizeBytes = 0;
    qint64 outputSizeBytes = 0;
};

/**
 * Qt wrapper for WatermarkProgress
 */
struct QtWatermarkProgress {
    QString currentFile;
    int currentIndex = 0;
    int totalFiles = 0;
    double percentComplete = 0.0;
    QString status;  // "encoding", "processing", "complete", "error"
};

/**
 * Worker for running watermark operations in background thread
 */
class WatermarkerWorker : public QObject {
    Q_OBJECT
public:
    explicit WatermarkerWorker(QObject* parent = nullptr);
    ~WatermarkerWorker();

    void setSourceFiles(const QStringList& files);
    void setMemberId(const QString& memberId);
    void setOutputDir(const QString& outputDir);
    void setConfig(const QtWatermarkConfig& config);
    void setParallelJobs(int jobs);

public slots:
    void process();
    void cancel();

signals:
    void started(int totalFiles);
    void progress(const QtWatermarkProgress& progress);
    void fileCompleted(const QtWatermarkResult& result);
    void finished(const QList<QtWatermarkResult>& results);
    void error(const QString& message);

private:
    QStringList m_sourceFiles;
    QString m_memberId;
    QString m_outputDir;
    QtWatermarkConfig m_config;
    int m_parallelJobs = 1;
    std::unique_ptr<Watermarker> m_watermarker;
};

/**
 * WatermarkerController - Qt controller for Watermarker CLI class
 *
 * Bridges the CLI Watermarker class with Qt signals/slots
 * for use in the GUI. Runs watermarking operations in a worker thread.
 */
class WatermarkerController : public QObject {
    Q_OBJECT

public:
    explicit WatermarkerController(QObject* parent = nullptr);
    ~WatermarkerController();

    // ==================== Configuration ====================

    void setConfig(const QtWatermarkConfig& config);
    QtWatermarkConfig config() const { return m_config; }

    // ==================== Operations ====================

    /**
     * Watermark files for a specific member
     * @param sourceFiles List of source file paths
     * @param memberId Member ID for personalized watermark
     * @param outputDir Output directory
     */
    void watermarkForMember(const QStringList& sourceFiles,
                            const QString& memberId,
                            const QString& outputDir);

    /**
     * Watermark files with global text (no member personalization)
     * @param sourceFiles List of source file paths
     * @param outputDir Output directory
     */
    void watermarkFiles(const QStringList& sourceFiles,
                        const QString& outputDir);

    /**
     * Watermark all files in a directory
     * @param inputDir Input directory
     * @param outputDir Output directory
     * @param recursive Process subdirectories
     */
    void watermarkDirectory(const QString& inputDir,
                            const QString& outputDir,
                            bool recursive = false);

    // ==================== Control ====================

    void cancel();

    bool isRunning() const { return m_isRunning; }

    // ==================== Queries ====================

    /**
     * Check if FFmpeg is available
     */
    static bool isFFmpegAvailable();

    /**
     * Check if Python is available
     */
    static bool isPythonAvailable();

    /**
     * Get last results
     */
    QList<QtWatermarkResult> lastResults() const { return m_lastResults; }

signals:
    // Watermarking lifecycle
    void watermarkStarted(int totalFiles);
    void watermarkProgress(const QtWatermarkProgress& progress);
    void fileCompleted(const QtWatermarkResult& result);
    void watermarkFinished(const QList<QtWatermarkResult>& results);
    void watermarkError(const QString& error);

    // State changes
    void runningChanged(bool running);

private slots:
    void onWorkerStarted(int totalFiles);
    void onWorkerProgress(const QtWatermarkProgress& progress);
    void onWorkerFileCompleted(const QtWatermarkResult& result);
    void onWorkerFinished(const QList<QtWatermarkResult>& results);
    void onWorkerError(const QString& message);

private:
    void startWorker();
    void cleanupWorker();

    QtWatermarkConfig m_config;
    QList<QtWatermarkResult> m_lastResults;
    bool m_isRunning = false;

    QThread* m_workerThread = nullptr;
    WatermarkerWorker* m_worker = nullptr;

    // Pending operation data
    QStringList m_pendingSourceFiles;
    QString m_pendingMemberId;
    QString m_pendingOutputDir;
    int m_parallelJobs = 2;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_WATERMARKERCONTROLLER_H
