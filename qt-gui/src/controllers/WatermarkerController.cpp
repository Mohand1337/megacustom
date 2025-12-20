#include "WatermarkerController.h"
#include "features/Watermarker.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>

namespace MegaCustom {

// ==================== Helper Conversions ====================

static WatermarkConfig toNativeConfig(const QtWatermarkConfig& qtConfig) {
    WatermarkConfig config;
    config.primaryText = qtConfig.primaryText.toStdString();
    config.secondaryText = qtConfig.secondaryText.toStdString();
    config.intervalSeconds = qtConfig.intervalSeconds;
    config.durationSeconds = qtConfig.durationSeconds;
    config.randomGate = qtConfig.randomGate;
    config.fontPath = qtConfig.fontPath.toStdString();
    config.primaryFontSize = qtConfig.primaryFontSize;
    config.secondaryFontSize = qtConfig.secondaryFontSize;
    config.primaryColor = qtConfig.primaryColor.toStdString();
    config.secondaryColor = qtConfig.secondaryColor.toStdString();
    config.preset = qtConfig.preset.toStdString();
    config.crf = qtConfig.crf;
    config.copyAudio = qtConfig.copyAudio;
    config.pdfOpacity = qtConfig.pdfOpacity;
    config.pdfAngle = qtConfig.pdfAngle;
    config.pdfCoverage = qtConfig.pdfCoverage;
    config.pdfPassword = qtConfig.pdfPassword.toStdString();
    config.outputSuffix = qtConfig.outputSuffix.toStdString();
    config.overwrite = qtConfig.overwrite;
    return config;
}

static QtWatermarkResult toQtResult(const WatermarkResult& result) {
    QtWatermarkResult qt;
    qt.success = result.success;
    qt.inputFile = QString::fromStdString(result.inputFile);
    qt.outputFile = QString::fromStdString(result.outputFile);
    qt.error = QString::fromStdString(result.error);
    qt.processingTimeMs = result.processingTimeMs;
    qt.inputSizeBytes = result.inputSizeBytes;
    qt.outputSizeBytes = result.outputSizeBytes;
    return qt;
}

static QtWatermarkProgress toQtProgress(const WatermarkProgress& progress) {
    QtWatermarkProgress qt;
    qt.currentFile = QString::fromStdString(progress.currentFile);
    qt.currentIndex = progress.currentIndex;
    qt.totalFiles = progress.totalFiles;
    qt.percentComplete = progress.percentComplete;
    qt.status = QString::fromStdString(progress.status);
    return qt;
}

// ==================== WatermarkerWorker ====================

WatermarkerWorker::WatermarkerWorker(QObject* parent)
    : QObject(parent)
    , m_watermarker(std::make_unique<Watermarker>())
{
}

WatermarkerWorker::~WatermarkerWorker() = default;

void WatermarkerWorker::setSourceFiles(const QStringList& files) {
    m_sourceFiles = files;
}

void WatermarkerWorker::setMemberId(const QString& memberId) {
    m_memberId = memberId;
}

void WatermarkerWorker::setOutputDir(const QString& outputDir) {
    m_outputDir = outputDir;
}

void WatermarkerWorker::setConfig(const QtWatermarkConfig& config) {
    m_config = config;
}

void WatermarkerWorker::setParallelJobs(int jobs) {
    m_parallelJobs = jobs;
}

void WatermarkerWorker::process() {
    // Configure watermarker
    m_watermarker->setConfig(toNativeConfig(m_config));

    // Set progress callback
    m_watermarker->setProgressCallback([this](const WatermarkProgress& progress) {
        emit this->progress(toQtProgress(progress));
    });

    // Emit started signal
    emit started(m_sourceFiles.size());

    // Convert to native types
    std::vector<std::string> sourceFiles;
    for (const QString& f : m_sourceFiles) {
        sourceFiles.push_back(f.toStdString());
    }

    QList<QtWatermarkResult> allResults;

    // Process each file
    for (int i = 0; i < m_sourceFiles.size(); ++i) {
        if (m_watermarker->isCancelled()) {
            break;
        }

        const QString& file = m_sourceFiles[i];
        WatermarkResult result;

        if (!m_memberId.isEmpty()) {
            // Member-specific watermarking
            if (Watermarker::isVideoFile(file.toStdString())) {
                result = m_watermarker->watermarkVideoForMember(
                    file.toStdString(),
                    m_memberId.toStdString(),
                    m_outputDir.toStdString());
            } else if (Watermarker::isPdfFile(file.toStdString())) {
                result = m_watermarker->watermarkPdfForMember(
                    file.toStdString(),
                    m_memberId.toStdString(),
                    m_outputDir.toStdString());
            } else {
                result = m_watermarker->watermarkFile(
                    file.toStdString(),
                    m_outputDir.toStdString());
            }
        } else {
            // Global watermarking (no member)
            result = m_watermarker->watermarkFile(
                file.toStdString(),
                m_outputDir.isEmpty() ? "" : m_watermarker->generateOutputPath(
                    file.toStdString(), m_outputDir.toStdString()));
        }

        QtWatermarkResult qtResult = toQtResult(result);
        allResults.append(qtResult);
        emit fileCompleted(qtResult);
    }

    // Emit final result
    emit finished(allResults);
}

void WatermarkerWorker::cancel() {
    if (m_watermarker) {
        m_watermarker->cancel();
    }
}

// ==================== WatermarkerController ====================

WatermarkerController::WatermarkerController(QObject* parent)
    : QObject(parent)
{
    qDebug() << "WatermarkerController: Initialized";
}

WatermarkerController::~WatermarkerController() {
    cleanupWorker();
}

void WatermarkerController::setConfig(const QtWatermarkConfig& config) {
    m_config = config;
}

void WatermarkerController::watermarkForMember(const QStringList& sourceFiles,
                                                const QString& memberId,
                                                const QString& outputDir) {
    if (m_isRunning) {
        qWarning() << "WatermarkerController: Watermarking already running";
        return;
    }

    if (sourceFiles.isEmpty()) {
        emit watermarkError("No source files specified");
        return;
    }

    m_pendingSourceFiles = sourceFiles;
    m_pendingMemberId = memberId;
    m_pendingOutputDir = outputDir;

    qDebug() << "WatermarkerController: Starting watermark of"
             << sourceFiles.size() << "files for member" << memberId;

    startWorker();
}

void WatermarkerController::watermarkFiles(const QStringList& sourceFiles,
                                            const QString& outputDir) {
    watermarkForMember(sourceFiles, QString(), outputDir);
}

void WatermarkerController::watermarkDirectory(const QString& inputDir,
                                                const QString& outputDir,
                                                bool recursive) {
    Q_UNUSED(recursive);

    if (m_isRunning) {
        qWarning() << "WatermarkerController: Watermarking already running";
        return;
    }

    // For now, just collect files from the directory
    // A more complete implementation would scan recursively
    QDir dir(inputDir);
    QStringList filters;
    filters << "*.mp4" << "*.mkv" << "*.avi" << "*.mov" << "*.wmv"
            << "*.pdf" << "*.PDF";

    QStringList files;
    for (const QFileInfo& fi : dir.entryInfoList(filters, QDir::Files)) {
        files.append(fi.absoluteFilePath());
    }

    if (files.isEmpty()) {
        emit watermarkError("No supported files found in directory");
        return;
    }

    watermarkFiles(files, outputDir);
}

void WatermarkerController::cancel() {
    if (m_worker) {
        qDebug() << "WatermarkerController: Cancelling watermarking";
        m_worker->cancel();
    }
}

bool WatermarkerController::isFFmpegAvailable() {
    return Watermarker::isFFmpegAvailable();
}

bool WatermarkerController::isPythonAvailable() {
    return Watermarker::isPythonAvailable();
}

void WatermarkerController::startWorker() {
    cleanupWorker();

    m_workerThread = new QThread(this);
    m_worker = new WatermarkerWorker();
    m_worker->moveToThread(m_workerThread);

    // Configure worker
    m_worker->setSourceFiles(m_pendingSourceFiles);
    m_worker->setMemberId(m_pendingMemberId);
    m_worker->setOutputDir(m_pendingOutputDir);
    m_worker->setConfig(m_config);
    m_worker->setParallelJobs(m_parallelJobs);

    // Connect signals
    connect(m_workerThread, &QThread::started, m_worker, &WatermarkerWorker::process);
    connect(m_worker, &WatermarkerWorker::started, this, &WatermarkerController::onWorkerStarted);
    connect(m_worker, &WatermarkerWorker::progress, this, &WatermarkerController::onWorkerProgress);
    connect(m_worker, &WatermarkerWorker::fileCompleted, this, &WatermarkerController::onWorkerFileCompleted);
    connect(m_worker, &WatermarkerWorker::finished, this, &WatermarkerController::onWorkerFinished);
    connect(m_worker, &WatermarkerWorker::error, this, &WatermarkerController::onWorkerError);

    // Cleanup connections
    connect(m_worker, &WatermarkerWorker::finished, m_workerThread, &QThread::quit);
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_isRunning = true;
    emit runningChanged(true);

    m_workerThread->start();
}

void WatermarkerController::cleanupWorker() {
    if (m_workerThread) {
        if (m_workerThread->isRunning()) {
            if (m_worker) {
                m_worker->cancel();
            }
            m_workerThread->quit();
            m_workerThread->wait(5000);
        }
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
        m_worker = nullptr;
    }
}

void WatermarkerController::onWorkerStarted(int totalFiles) {
    qDebug() << "WatermarkerController: Watermarking started," << totalFiles << "files";
    emit watermarkStarted(totalFiles);
}

void WatermarkerController::onWorkerProgress(const QtWatermarkProgress& progress) {
    emit watermarkProgress(progress);
}

void WatermarkerController::onWorkerFileCompleted(const QtWatermarkResult& result) {
    qDebug() << "WatermarkerController: File completed:" << result.inputFile
             << "success:" << result.success;
    emit fileCompleted(result);
}

void WatermarkerController::onWorkerFinished(const QList<QtWatermarkResult>& results) {
    m_lastResults = results;
    m_isRunning = false;

    int successCount = 0;
    int failCount = 0;
    for (const QtWatermarkResult& r : results) {
        if (r.success) successCount++;
        else failCount++;
    }

    qDebug() << "WatermarkerController: Watermarking finished. Success:" << successCount
             << "Failed:" << failCount;

    emit runningChanged(false);
    emit watermarkFinished(results);

    // Cleanup worker thread
    if (m_workerThread) {
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
        m_worker = nullptr;
    }
}

void WatermarkerController::onWorkerError(const QString& message) {
    qWarning() << "WatermarkerController: Error:" << message;
    emit watermarkError(message);
}

} // namespace MegaCustom
