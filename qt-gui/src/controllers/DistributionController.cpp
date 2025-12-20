#include "DistributionController.h"
#include "features/DistributionPipeline.h"
#include <QDebug>

namespace MegaCustom {

// ==================== Helper Conversions ====================

static DistributionConfig toNativeConfig(const QtDistributionConfig& qtConfig) {
    DistributionConfig config;

    switch (qtConfig.watermarkMode) {
        case QtDistributionConfig::WatermarkMode::None:
            config.watermarkMode = DistributionConfig::WatermarkMode::None;
            break;
        case QtDistributionConfig::WatermarkMode::Global:
            config.watermarkMode = DistributionConfig::WatermarkMode::Global;
            break;
        case QtDistributionConfig::WatermarkMode::PerMember:
        default:
            config.watermarkMode = DistributionConfig::WatermarkMode::PerMember;
            break;
    }

    config.globalPrimaryText = qtConfig.globalPrimaryText.toStdString();
    config.globalSecondaryText = qtConfig.globalSecondaryText.toStdString();
    config.tempDirectory = qtConfig.tempDirectory.toStdString();
    config.deleteTempAfterUpload = qtConfig.deleteTempAfterUpload;
    config.keepLocalCopies = qtConfig.keepLocalCopies;
    config.localCopiesDir = qtConfig.localCopiesDir.toStdString();
    config.parallelWatermarkJobs = qtConfig.parallelWatermarkJobs;
    config.parallelUploadJobs = qtConfig.parallelUploadJobs;
    config.resumeOnError = qtConfig.resumeOnError;
    config.createFolderIfMissing = qtConfig.createFolderIfMissing;
    config.overwriteExisting = qtConfig.overwriteExisting;

    return config;
}

static QString stateToString(MemberDistributionStatus::State state) {
    switch (state) {
        case MemberDistributionStatus::State::Pending: return "pending";
        case MemberDistributionStatus::State::Watermarking: return "watermarking";
        case MemberDistributionStatus::State::Uploading: return "uploading";
        case MemberDistributionStatus::State::Completed: return "completed";
        case MemberDistributionStatus::State::Failed: return "failed";
        case MemberDistributionStatus::State::Skipped: return "skipped";
        default: return "unknown";
    }
}

static QtMemberStatus toQtMemberStatus(const MemberDistributionStatus& status) {
    QtMemberStatus qt;
    qt.memberId = QString::fromStdString(status.memberId);
    qt.memberName = QString::fromStdString(status.memberName);
    qt.destinationFolder = QString::fromStdString(status.destinationFolder);
    qt.state = stateToString(status.state);
    qt.filesWatermarked = status.filesWatermarked;
    qt.filesUploaded = status.filesUploaded;
    qt.filesFailed = status.filesFailed;
    qt.lastError = QString::fromStdString(status.lastError);
    return qt;
}

static QtDistributionResult toQtResult(const DistributionResult& result) {
    QtDistributionResult qt;
    qt.success = result.success;
    qt.jobId = QString::fromStdString(result.jobId);

    for (const auto& f : result.sourceFiles) {
        qt.sourceFiles.append(QString::fromStdString(f));
    }

    for (const auto& m : result.memberResults) {
        qt.memberResults.append(toQtMemberStatus(m));
    }

    qt.totalMembers = result.totalMembers;
    qt.membersCompleted = result.membersCompleted;
    qt.membersFailed = result.membersFailed;
    qt.membersSkipped = result.membersSkipped;
    qt.totalFiles = result.totalFiles;
    qt.filesWatermarked = result.filesWatermarked;
    qt.filesUploaded = result.filesUploaded;
    qt.filesFailed = result.filesFailed;

    for (const auto& e : result.errors) {
        qt.errors.append(QString::fromStdString(e));
    }

    return qt;
}

static QtDistributionProgress toQtProgress(const DistributionProgress& progress) {
    QtDistributionProgress qt;
    qt.jobId = QString::fromStdString(progress.jobId);
    qt.overallPercent = progress.overallPercent;
    qt.phase = QString::fromStdString(progress.phase);
    qt.currentMember = QString::fromStdString(progress.currentMember);
    qt.currentFile = QString::fromStdString(progress.currentFile);
    qt.currentOperation = QString::fromStdString(progress.currentOperation);
    qt.membersProcessed = progress.membersProcessed;
    qt.totalMembers = progress.totalMembers;
    qt.filesProcessed = progress.filesProcessed;
    qt.totalFiles = progress.totalFiles;
    qt.elapsedMs = progress.elapsedMs;
    qt.estimatedRemainingMs = progress.estimatedRemainingMs;
    qt.errorCount = progress.errorCount;
    return qt;
}

// ==================== DistributionWorker ====================

DistributionWorker::DistributionWorker(QObject* parent)
    : QObject(parent)
    , m_pipeline(std::make_unique<DistributionPipeline>())
{
}

DistributionWorker::~DistributionWorker() = default;

void DistributionWorker::setSourceFiles(const QStringList& files) {
    m_sourceFiles = files;
}

void DistributionWorker::setMemberIds(const QStringList& memberIds) {
    m_memberIds = memberIds;
}

void DistributionWorker::setConfig(const QtDistributionConfig& config) {
    m_config = config;
}

void DistributionWorker::setPreviewOnly(bool preview) {
    m_previewOnly = preview;
}

void DistributionWorker::process() {
    // Convert Qt types to native
    std::vector<std::string> sourceFiles;
    for (const QString& f : m_sourceFiles) {
        sourceFiles.push_back(f.toStdString());
    }

    std::vector<std::string> memberIds;
    for (const QString& m : m_memberIds) {
        memberIds.push_back(m.toStdString());
    }

    // Configure pipeline
    m_pipeline->setConfig(toNativeConfig(m_config));

    // Set progress callback
    m_pipeline->setProgressCallback([this](const DistributionProgress& progress) {
        emit this->progress(toQtProgress(progress));
    });

    // Generate job ID and emit started
    QString jobId = QString::fromStdString(DistributionPipeline::generateJobId());
    emit started(jobId);

    // Execute distribution or preview
    DistributionResult result;
    if (m_previewOnly) {
        result = m_pipeline->previewDistribution(sourceFiles, memberIds);
    } else {
        result = m_pipeline->distribute(sourceFiles, memberIds);
    }

    // Emit per-member results
    for (const auto& memberStatus : result.memberResults) {
        emit memberCompleted(toQtMemberStatus(memberStatus));
    }

    // Emit final result
    emit finished(toQtResult(result));
}

void DistributionWorker::cancel() {
    if (m_pipeline) {
        m_pipeline->cancel();
    }
}

void DistributionWorker::pause() {
    if (m_pipeline) {
        m_pipeline->pause();
    }
}

void DistributionWorker::resume() {
    if (m_pipeline) {
        m_pipeline->resume();
    }
}

// ==================== DistributionController ====================

DistributionController::DistributionController(QObject* parent)
    : QObject(parent)
{
    qDebug() << "DistributionController: Initialized";
}

DistributionController::~DistributionController() {
    cleanupWorker();
}

void DistributionController::setConfig(const QtDistributionConfig& config) {
    m_config = config;
}

void DistributionController::startDistribution(const QStringList& sourceFiles,
                                                const QStringList& memberIds) {
    if (m_isRunning) {
        qWarning() << "DistributionController: Distribution already running";
        return;
    }

    if (sourceFiles.isEmpty()) {
        emit distributionError("No source files specified");
        return;
    }

    m_pendingSourceFiles = sourceFiles;
    m_pendingMemberIds = memberIds;

    qDebug() << "DistributionController: Starting distribution of"
             << sourceFiles.size() << "files to"
             << (memberIds.isEmpty() ? "all members" : QString::number(memberIds.size()) + " members");

    startWorker(false);
}

void DistributionController::previewDistribution(const QStringList& sourceFiles,
                                                  const QStringList& memberIds) {
    if (m_isRunning) {
        qWarning() << "DistributionController: Distribution already running";
        return;
    }

    m_pendingSourceFiles = sourceFiles;
    m_pendingMemberIds = memberIds;

    qDebug() << "DistributionController: Previewing distribution";

    startWorker(true);
}

void DistributionController::retryFailed(const QtDistributionResult& previousResult) {
    // Extract failed member IDs
    QStringList failedMemberIds;
    for (const QtMemberStatus& status : previousResult.memberResults) {
        if (status.state == "failed") {
            failedMemberIds.append(status.memberId);
        }
    }

    if (failedMemberIds.isEmpty()) {
        qDebug() << "DistributionController: No failed members to retry";
        return;
    }

    qDebug() << "DistributionController: Retrying" << failedMemberIds.size() << "failed members";

    startDistribution(previousResult.sourceFiles, failedMemberIds);
}

void DistributionController::cancel() {
    if (m_worker) {
        qDebug() << "DistributionController: Cancelling distribution";
        m_worker->cancel();
    }
}

void DistributionController::pause() {
    if (m_worker && m_isRunning && !m_isPaused) {
        qDebug() << "DistributionController: Pausing distribution";
        m_worker->pause();
        m_isPaused = true;
        emit pausedChanged(true);
    }
}

void DistributionController::resume() {
    if (m_worker && m_isRunning && m_isPaused) {
        qDebug() << "DistributionController: Resuming distribution";
        m_worker->resume();
        m_isPaused = false;
        emit pausedChanged(false);
    }
}

QStringList DistributionController::getMembersWithFolders() {
    DistributionPipeline pipeline;
    std::vector<std::string> members = pipeline.getMembersWithFolders();

    QStringList result;
    for (const auto& m : members) {
        result.append(QString::fromStdString(m));
    }
    return result;
}

void DistributionController::startWorker(bool previewOnly) {
    cleanupWorker();

    m_workerThread = new QThread(this);
    m_worker = new DistributionWorker();
    m_worker->moveToThread(m_workerThread);

    // Configure worker
    m_worker->setSourceFiles(m_pendingSourceFiles);
    m_worker->setMemberIds(m_pendingMemberIds);
    m_worker->setConfig(m_config);
    m_worker->setPreviewOnly(previewOnly);

    // Connect signals
    connect(m_workerThread, &QThread::started, m_worker, &DistributionWorker::process);
    connect(m_worker, &DistributionWorker::started, this, &DistributionController::onWorkerStarted);
    connect(m_worker, &DistributionWorker::progress, this, &DistributionController::onWorkerProgress);
    connect(m_worker, &DistributionWorker::memberCompleted, this, &DistributionController::onWorkerMemberCompleted);
    connect(m_worker, &DistributionWorker::finished, this, &DistributionController::onWorkerFinished);
    connect(m_worker, &DistributionWorker::error, this, &DistributionController::onWorkerError);

    // Cleanup connections
    connect(m_worker, &DistributionWorker::finished, m_workerThread, &QThread::quit);
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_isRunning = true;
    emit runningChanged(true);

    m_workerThread->start();
}

void DistributionController::cleanupWorker() {
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

void DistributionController::onWorkerStarted(const QString& jobId) {
    qDebug() << "DistributionController: Distribution started, job:" << jobId;
    emit distributionStarted(jobId);
}

void DistributionController::onWorkerProgress(const QtDistributionProgress& progress) {
    emit distributionProgress(progress);
}

void DistributionController::onWorkerMemberCompleted(const QtMemberStatus& status) {
    qDebug() << "DistributionController: Member completed:" << status.memberId
             << "state:" << status.state;
    emit memberCompleted(status);
}

void DistributionController::onWorkerFinished(const QtDistributionResult& result) {
    m_lastResult = result;
    m_isRunning = false;
    m_isPaused = false;

    qDebug() << "DistributionController: Distribution finished. Success:" << result.success
             << "Members:" << result.membersCompleted << "/" << result.totalMembers
             << "Files:" << result.filesUploaded << "/" << result.totalFiles;

    emit runningChanged(false);
    emit distributionFinished(result);

    // Cleanup worker thread
    if (m_workerThread) {
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
        m_worker = nullptr;
    }
}

void DistributionController::onWorkerError(const QString& message) {
    qWarning() << "DistributionController: Error:" << message;
    emit distributionError(message);
}

} // namespace MegaCustom
