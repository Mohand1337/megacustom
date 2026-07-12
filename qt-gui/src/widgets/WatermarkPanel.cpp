#include "WatermarkPanel.h"
#include "EmptyStateWidget.h"
#include "utils/MemberRegistry.h"
#include "utils/TemplateExpander.h"
#include "utils/CopyHelper.h"
#include "utils/Constants.h"
#include "utils/MetricsStore.h"
#include "utils/MegaUploadUtils.h"
#include "utils/OperationJobStore.h"
#include "core/LogManager.h"
#include "features/Watermarker.h"
#include "controllers/WatermarkerController.h"
#include "dialogs/WatermarkSettingsDialog.h"
#include "dialogs/RemoteFolderBrowserDialog.h"
#include "styles/ThemeManager.h"
#include "utils/AnimationHelper.h"
#include <QSettings>
#include <QInputDialog>
#include <QStorageInfo>
#include <QElapsedTimer>
#include <QAbstractItemView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QDirIterator>
#include <QGroupBox>
#include <QScrollArea>
#include <QSplitter>
#include <QMenu>
#include <QProcess>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QUrl>
#include <exception>

namespace MegaCustom {

// ==================== WatermarkWorker ====================

WatermarkWorker::WatermarkWorker(QObject* parent)
    : QObject(parent)
{
}

void WatermarkWorker::setFiles(const QStringList& files) {
    m_files = files;
}

void WatermarkWorker::setOutputDir(const QString& dir) {
    m_outputDir = dir;
}

void WatermarkWorker::setConfig(const WatermarkConfig& config) {
    m_config = std::make_shared<WatermarkConfig>(config);
}

void WatermarkWorker::setMemberId(const QString& memberId) {
    m_memberId = memberId;
}

void WatermarkWorker::setMemberIds(const QStringList& memberIds) {
    m_memberIds = memberIds;
}

void WatermarkWorker::setRawTemplates(const QString& primary, const QString& secondary) {
    m_rawPrimaryTemplate = primary;
    m_rawSecondaryTemplate = secondary;
}

void WatermarkWorker::cancel() {
    m_cancelled = true;
}

void WatermarkWorker::setAutoUpload(bool enabled, void* megaApi) {
    m_autoUpload = enabled;
    m_megaApi = megaApi;
}

void WatermarkWorker::setCustomUploadPath(const QString& path) {
    m_customUploadPath = path;
}

void WatermarkWorker::setRootDir(const QString& rootDir) {
    m_rootDir = rootDir;
}

void WatermarkWorker::setMetricsStore(MetricsStore* store) {
    m_metricsStore = store;
}

void WatermarkWorker::setResumeTasks(const QList<WatermarkResumeTask>& tasks) {
    m_resumeTasks = tasks;
}

// Helper: expand templates for a member and apply to watermarker config
static void applyMemberTemplates(Watermarker& watermarker, const WatermarkConfig& baseConfig,
                                  const QString& memberId, const QString& primaryTpl,
                                  const QString& secondaryTpl) {
    MemberInfo memberInfo = MemberRegistry::instance()->getMember(memberId);
    TemplateExpander::Variables vars = TemplateExpander::Variables::fromMember(memberInfo);

    WatermarkConfig localConfig = baseConfig;
    localConfig.primaryText = TemplateExpander::expand(primaryTpl, vars).toStdString();
    localConfig.secondaryText = TemplateExpander::expand(secondaryTpl, vars).toStdString();

    // Expand metadata templates per-member
    if (localConfig.embedMetadata) {
        if (!localConfig.metadataTitle.empty())
            localConfig.metadataTitle = TemplateExpander::expand(
                QString::fromStdString(baseConfig.metadataTitle), vars).toStdString();
        if (!localConfig.metadataAuthor.empty())
            localConfig.metadataAuthor = TemplateExpander::expand(
                QString::fromStdString(baseConfig.metadataAuthor), vars).toStdString();
        if (!localConfig.metadataComment.empty())
            localConfig.metadataComment = TemplateExpander::expand(
                QString::fromStdString(baseConfig.metadataComment), vars).toStdString();
        if (!localConfig.metadataKeywords.empty())
            localConfig.metadataKeywords = TemplateExpander::expand(
                QString::fromStdString(baseConfig.metadataKeywords), vars).toStdString();
    }

    watermarker.setConfig(localConfig);
}

static QString existingStoragePath(const QString& path) {
    if (path.isEmpty()) {
        return QDir::currentPath();
    }

    QFileInfo info(path);
    if (info.exists()) {
        return info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    }

    QDir dir(path);
    while (!dir.exists()) {
        const QString before = dir.absolutePath();
        if (!dir.cdUp() || dir.absolutePath() == before) {
            break;
        }
    }
    return dir.exists() ? dir.absolutePath() : QFileInfo(path).absolutePath();
}

qint64 WatermarkWorker::estimateOutputBytes(const QString& inputPath) const {
    const QFileInfo info(inputPath);
    const qint64 inputSize = info.size();
    if (m_metricsStore) {
        const qint64 predicted = m_metricsStore->predictOutputSize(
            info.suffix().toLower(), inputSize);
        if (predicted > 0) {
            return predicted;
        }
    }

    const QString ext = info.suffix().toLower();
    const bool video = ext == "mp4" || ext == "mkv" || ext == "avi" || ext == "mov"
        || ext == "wmv" || ext == "flv" || ext == "webm" || ext == "m4v"
        || ext == "mpeg" || ext == "mpg";
    const double multiplier = video ? 1.6 : 1.2;
    const qint64 estimated = static_cast<qint64>(inputSize * multiplier);
    return qMax(inputSize, estimated);
}

bool WatermarkWorker::ensureDiskSpaceForNextOutput(const QString& inputPath,
                                                   const QString& outputBaseDir) {
    const QString checkPath = existingStoragePath(
        outputBaseDir.isEmpty() ? QFileInfo(inputPath).absolutePath() : outputBaseDir);
    QStorageInfo storage(checkPath);
    storage.refresh();
    if (!storage.isValid() || !storage.isReady()) {
        return true;
    }

    const qint64 reserveBytes = 64LL * 1024LL * 1024LL;
    const qint64 needed = estimateOutputBytes(inputPath) + reserveBytes;
    const qint64 available = storage.bytesAvailable();
    if (available >= needed) {
        return true;
    }

    emit diskSpaceWarning(available, needed);
    m_cancelled = true;
    return false;
}

void WatermarkWorker::pauseForDiskSpace(const QString& inputPath,
                                        const QString& outputBaseDir) {
    const QString checkPath = existingStoragePath(
        outputBaseDir.isEmpty() ? QFileInfo(inputPath).absolutePath() : outputBaseDir);
    QStorageInfo storage(checkPath);
    storage.refresh();
    emit diskSpaceWarning(storage.isValid() ? storage.bytesAvailable() : 0,
                          estimateOutputBytes(inputPath));
    m_cancelled = true;
}

bool WatermarkWorker::isDiskSpaceError(const WatermarkResult& result) const {
    const QString error = QString::fromStdString(result.error).toLower();
    return error.contains("no space left")
        || error.contains("enospc")
        || error.contains("not enough space")
        || error.contains("disk full")
        || error.contains("insufficient disk")
        || error.contains("device full");
}

WatermarkResult WatermarkWorker::watermarkInput(Watermarker& watermarker,
                                                const WatermarkConfig& baseConfig,
                                                const QString& inputPath,
                                                const QString& memberId,
                                                const std::string& outputDir) {
    const std::string inputStd = inputPath.toStdString();
    WatermarkResult result;
    result.inputFile = inputStd;

    try {
        if (!memberId.isEmpty()) {
            applyMemberTemplates(watermarker, baseConfig, memberId,
                                 m_rawPrimaryTemplate, m_rawSecondaryTemplate);

            const QString outputBaseDir = m_outputDir.isEmpty()
                ? (m_rootDir.isEmpty() ? QFileInfo(inputPath).absolutePath() : m_rootDir)
                : m_outputDir;
            if (!ensureDiskSpaceForNextOutput(inputPath, outputBaseDir)) {
                result.success = false;
                result.error = "Paused: insufficient disk space";
                return result;
            }

            const std::string outPath = watermarker.buildMemberOutputPath(
                inputStd, outputDir, memberId.toStdString(), m_rootDir.toStdString());
            if (Watermarker::isVideoFile(inputStd)) {
                result = watermarker.watermarkVideo(inputStd, outPath);
            } else if (Watermarker::isPdfFile(inputStd)) {
                result = watermarker.watermarkPdf(inputStd, outPath);
            } else if (Watermarker::isAudioFile(inputStd)) {
                result = watermarker.watermarkAudio(inputStd, outPath);
            } else {
                QFile sourceFile(inputPath);
                if (sourceFile.copy(QString::fromStdString(outPath))) {
                    result.success = true;
                    result.inputFile = inputStd;
                    result.outputFile = outPath;
                } else {
                    result.success = false;
                    result.inputFile = inputStd;
                    result.error = QString("Failed to copy passthrough file: %1")
                                       .arg(sourceFile.errorString()).toStdString();
                }
            }
        } else {
            watermarker.setConfig(baseConfig);
            const QString outputBaseDir = outputDir.empty()
                ? QFileInfo(inputPath).absolutePath()
                : QString::fromStdString(outputDir);
            if (!ensureDiskSpaceForNextOutput(inputPath, outputBaseDir)) {
                result.success = false;
                result.error = "Paused: insufficient disk space";
                return result;
            }

            std::string outputPath;
            if (!outputDir.empty()) {
                outputPath = watermarker.generateOutputPath(inputStd, outputDir, m_rootDir.toStdString());
            }
            result = watermarker.watermarkFile(inputStd, outputPath);
        }
    } catch (const std::exception& e) {
        result.success = false;
        result.inputFile = inputStd;
        result.error = QString("Watermark failed safely: %1").arg(e.what()).toStdString();
        LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
            "watermark_worker_exception",
            QString("Watermark worker caught exception for %1: %2")
                .arg(QFileInfo(inputPath).fileName(), QString::fromUtf8(e.what()))
                .toStdString(),
            inputStd);
    } catch (...) {
        result.success = false;
        result.inputFile = inputStd;
        result.error = "Watermark failed safely: unknown exception";
        LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
            "watermark_worker_exception",
            "Watermark worker caught unknown exception for: " + inputStd,
            inputStd);
    }

    if (m_metricsStore && !inputPath.isEmpty()) {
        const QString ext = QFileInfo(inputPath).suffix().toLower();
        m_metricsStore->recordWatermark(
            ext,
            memberId,
            result.inputSizeBytes,
            result.outputSizeBytes,
            result.processingTimeMs,
            result.success,
            QString::fromStdString(result.error));
    }

    return result;
}

void WatermarkWorker::processResumeTasks(Watermarker& watermarker,
                                         const WatermarkConfig& baseConfig,
                                         const std::string& outputDir,
                                         int& successCount,
                                         int& failCount,
                                         QMap<QString, QStringList>& memberFileMap) {
    int totalWatermarkTasks = 0;
    for (const WatermarkResumeTask& task : m_resumeTasks) {
        if (task.watermarkNeeded) {
            ++totalWatermarkTasks;
        }
    }

    QString activeMemberId;
    QStringList memberOutputFiles;
    bool activeMemberHadFailure = false;
    int processedWatermarkTasks = 0;

    auto flushMemberUploads = [&](const QString& memberId, bool memberHadFailure) {
        if (memberHadFailure && m_autoUpload && !memberId.isEmpty()) {
            emit memberAutoUploadSkipped(
                memberId,
                "Member batch has failed watermark rows; upload blocked to avoid partial delivery.");
            memberOutputFiles.clear();
            return;
        }

        if (!m_autoUpload || !m_megaApi || m_cancelled || memberId.isEmpty()
            || memberOutputFiles.isEmpty()) {
            memberOutputFiles.clear();
            return;
        }

        mega::MegaApi* api = static_cast<mega::MegaApi*>(m_megaApi);
        MemberInfo memberInfo = MemberRegistry::instance()->getMember(memberId);
        const QString uploadDest = m_customUploadPath.isEmpty()
            ? memberInfo.distributionFolder
            : m_customUploadPath;

        if (uploadDest.isEmpty()) {
            emit memberAutoUploadSkipped(memberId, "No distribution folder configured");
            memberOutputFiles.clear();
            return;
        }

        int uploadOk = 0;
        int uploadFail = 0;
        int deleted = 0;

        for (int f = 0; f < memberOutputFiles.size() && !m_cancelled; ++f) {
            emit memberBatchUploading(memberId, f + 1,
                memberOutputFiles.size(), QFileInfo(memberOutputFiles[f]).fileName());

            QElapsedTimer uploadTimer;
            uploadTimer.start();
            std::string error;
            qint64 fileSize = QFileInfo(memberOutputFiles[f]).size();
            const QString nativeLocal = QDir::toNativeSeparators(memberOutputFiles[f]);

            bool ok = megaApiUpload(api, nativeLocal.toStdString(),
                                    uploadDest.toStdString(), error);

            qint64 uploadMs = uploadTimer.elapsed();
            qint64 speed = (uploadMs > 0) ? (fileSize * 1000 / uploadMs) : 0;
            if (m_metricsStore) {
                QString ext = QFileInfo(memberOutputFiles[f]).suffix().toLower();
                m_metricsStore->recordUpload(ext, memberId, fileSize, uploadMs,
                                              speed, ok, QString::fromStdString(error));
            }

            if (ok) {
                uploadOk++;
                if (QFile::remove(memberOutputFiles[f])) {
                    deleted++;
                }
            } else {
                uploadFail++;
                qWarning() << "Auto-upload failed during resume:" << memberId
                           << memberOutputFiles[f] << QString::fromStdString(error);
            }
        }

        if (deleted > 0 && deleted == memberOutputFiles.size()) {
            QDir().rmdir(QFileInfo(memberOutputFiles.first()).path());
        }

        emit memberBatchCleanedUp(memberId, uploadOk, uploadFail, deleted);
        memberOutputFiles.clear();
    };

    for (const WatermarkResumeTask& task : m_resumeTasks) {
        if (m_cancelled) {
            break;
        }

        if (activeMemberId.isEmpty()) {
            activeMemberId = task.memberId;
        } else if (task.memberId != activeMemberId) {
            flushMemberUploads(activeMemberId, activeMemberHadFailure);
            activeMemberId = task.memberId;
            activeMemberHadFailure = false;
        }

        if (!task.watermarkNeeded) {
            if (!task.existingOutputPath.isEmpty() && QFileInfo::exists(task.existingOutputPath)) {
                memberOutputFiles.append(task.existingOutputPath);
                if (!task.memberId.isEmpty()) {
                    memberFileMap[task.memberId].append(task.existingOutputPath);
                }
            }
            continue;
        }

        const int currentIdx = processedWatermarkTasks;
        const QString label = task.memberId.isEmpty()
            ? QFileInfo(task.filePath).fileName()
            : QString("%1 [%2]").arg(QFileInfo(task.filePath).fileName(), task.memberId);
        emit progress(currentIdx, totalWatermarkTasks, label, 0);

        watermarker.setProgressCallback([this, totalWatermarkTasks, currentIdx](const WatermarkProgress& progress) {
            emit this->progress(currentIdx, totalWatermarkTasks,
                                QString::fromStdString(progress.currentFile),
                                static_cast<int>(progress.percentComplete));
        });

        WatermarkResult result = watermarkInput(
            watermarker, baseConfig, task.filePath, task.memberId, outputDir);

        if (!result.success && isDiskSpaceError(result)) {
            if (!result.outputFile.empty()) {
                QFile::remove(QString::fromStdString(result.outputFile));
            }
            if (!m_cancelled) {
                const QString outputBaseDir = outputDir.empty()
                    ? QFileInfo(task.filePath).absolutePath()
                    : QString::fromStdString(outputDir);
                pauseForDiskSpace(task.filePath, outputBaseDir);
            }
            break;
        }

        if (result.success) {
            successCount++;
            const QString outFile = QString::fromStdString(result.outputFile);
            if (!task.memberId.isEmpty()) {
                memberOutputFiles.append(outFile);
                memberFileMap[task.memberId].append(outFile);
            }
        } else {
            failCount++;
            activeMemberHadFailure = true;
        }

        emit fileCompleted(currentIdx, result.success,
                           QString::fromStdString(result.outputFile),
                           QString::fromStdString(result.error));
        processedWatermarkTasks++;
    }

    flushMemberUploads(activeMemberId, activeMemberHadFailure);
}

void WatermarkWorker::process() {
    emit started();

    m_cancelled = false;
    int successCount = 0;
    int failCount = 0;

    Watermarker watermarker;
    WatermarkConfig baseConfig;
    if (m_config) {
        baseConfig = *m_config;
        watermarker.setConfig(baseConfig);
    }

    std::string outputDir = m_outputDir.isEmpty() ? "" : m_outputDir.toStdString();

    if (!m_resumeTasks.isEmpty()) {
        QMap<QString, QStringList> memberFileMap;
        processResumeTasks(watermarker, baseConfig, outputDir, successCount, failCount, memberFileMap);
        emit finished(successCount, failCount);
        emit finishedWithMapping(successCount, failCount, memberFileMap);
        return;
    }

    // All-members mode: iterate members x files (finish all files for one member before next)
    if (!m_memberIds.isEmpty()) {
        int total = m_files.size() * m_memberIds.size();
        int idx = 0;
        QMap<QString, QStringList> memberFileMap;

        for (int j = 0; j < m_memberIds.size() && !m_cancelled; ++j) {
            QString memberId = m_memberIds[j];
            QStringList memberOutputFiles;  // Track this member's outputs for auto-upload
            int memberFailCount = 0;

            for (int i = 0; i < m_files.size() && !m_cancelled; ++i) {
                QString inputPath = m_files[i];
                QString label = QString("%1 [%2]").arg(QFileInfo(inputPath).fileName()).arg(memberId);
                emit progress(idx, total, label, 0);

                // Set FFmpeg progress callback with captured idx for correct row update
                int currentIdx = idx;
                watermarker.setProgressCallback([this, total, currentIdx](const WatermarkProgress& progress) {
                    emit this->progress(currentIdx, total,
                                       QString::fromStdString(progress.currentFile),
                                       static_cast<int>(progress.percentComplete));
                });

                WatermarkResult result = watermarkInput(
                    watermarker, baseConfig, inputPath, memberId, outputDir);

                if (!result.success && isDiskSpaceError(result)) {
                    if (!result.outputFile.empty()) {
                        QFile::remove(QString::fromStdString(result.outputFile));
                    }
                    if (!m_cancelled) {
                        const QString outputBaseDir = m_outputDir.isEmpty()
                            ? (m_rootDir.isEmpty() ? QFileInfo(inputPath).absolutePath() : m_rootDir)
                            : m_outputDir;
                        pauseForDiskSpace(inputPath, outputBaseDir);
                    }
                    break;
                }

                if (result.success) {
                    successCount++;
                    QString outFile = QString::fromStdString(result.outputFile);
                    memberFileMap[memberId].append(outFile);
                    memberOutputFiles.append(outFile);
                } else {
                    failCount++;
                    memberFailCount++;
                }

                emit fileCompleted(idx, result.success,
                                  QString::fromStdString(result.outputFile),
                                  QString::fromStdString(result.error));
                idx++;
            }

            // === Auto-upload & cleanup after this member's batch ===
            if (m_autoUpload && memberFailCount > 0 && !memberOutputFiles.isEmpty()) {
                emit memberAutoUploadSkipped(
                    memberId,
                    QString("%1 watermark row(s) failed; upload blocked to avoid partial delivery.")
                        .arg(memberFailCount));
            } else if (m_autoUpload && m_megaApi && !m_cancelled && !memberOutputFiles.isEmpty()) {
                mega::MegaApi* api = static_cast<mega::MegaApi*>(m_megaApi);
                MemberInfo memberInfo = MemberRegistry::instance()->getMember(memberId);

                // Determine upload destination: custom path overrides member folder
                QString uploadDest = m_customUploadPath.isEmpty()
                    ? memberInfo.distributionFolder : m_customUploadPath;

                if (!uploadDest.isEmpty()) {
                    int uploadOk = 0, uploadFail = 0, deleted = 0;

                    for (int f = 0; f < memberOutputFiles.size() && !m_cancelled; ++f) {
                        emit memberBatchUploading(memberId, f + 1,
                            memberOutputFiles.size(), QFileInfo(memberOutputFiles[f]).fileName());

                        QElapsedTimer uploadTimer;
                        uploadTimer.start();
                        std::string error;
                        qint64 fileSize = QFileInfo(memberOutputFiles[f]).size();

                        // MEGA SDK's Windows FileSystemAccess fails with
                        // "Read error" on mixed forward/backward slashes —
                        // buildMemberOutputPath emits backslashes via fs::path
                        // while the parent dir keeps Qt's forward slashes, so
                        // normalize before handing it to startUpload. No-op on
                        // Linux. Same fix as commit 8b24daa for DistributionPanel.
                        const QString nativeLocal =
                            QDir::toNativeSeparators(memberOutputFiles[f]);

                        bool ok = megaApiUpload(api, nativeLocal.toStdString(),
                                                uploadDest.toStdString(), error);

                        qint64 uploadMs = uploadTimer.elapsed();
                        qint64 speed = (uploadMs > 0) ? (fileSize * 1000 / uploadMs) : 0;

                        // Record upload metrics (Smart Engine learning)
                        if (m_metricsStore) {
                            QString ext = QFileInfo(memberOutputFiles[f]).suffix().toLower();
                            m_metricsStore->recordUpload(ext, memberId, fileSize, uploadMs,
                                                          speed, ok, QString::fromStdString(error));
                        }

                        if (ok) {
                            uploadOk++;
                            if (QFile::remove(memberOutputFiles[f])) deleted++;
                        } else {
                            uploadFail++;
                            qWarning() << "Auto-upload failed:" << memberId
                                       << memberOutputFiles[f] << QString::fromStdString(error);
                        }
                    }

                    // Remove empty member subdirectory
                    if (deleted > 0 && deleted == memberOutputFiles.size()) {
                        QDir().rmdir(QFileInfo(memberOutputFiles.first()).path());
                    }

                    emit memberBatchCleanedUp(memberId, uploadOk, uploadFail, deleted);

                    // Real-time disk check before next member
                    if (j + 1 < m_memberIds.size()) {
                        QString checkPath = m_outputDir.isEmpty()
                            ? QFileInfo(m_files.first()).path() : m_outputDir;
                        QStorageInfo storage(checkPath);
                        storage.refresh();
                        qint64 available = storage.bytesAvailable();
                        // Rough estimate: next member needs same space as input files
                        qint64 totalInputSize = 0;
                        for (const QString& f : m_files) {
                            totalInputSize += QFileInfo(f).size();
                        }
                        qint64 needed = static_cast<qint64>(totalInputSize * 1.2);
                        if (available < needed) {
                            emit diskSpaceWarning(available, needed);
                            m_cancelled = true;
                        }
                    }
                } else {
                    emit memberAutoUploadSkipped(memberId, "No distribution folder configured");
                }
            }
        }

        emit finished(successCount, failCount);
        emit finishedWithMapping(successCount, failCount, memberFileMap);
        return;
    }

    // Single-member or global mode
    int total = m_files.size();

    for (int i = 0; i < m_files.size() && !m_cancelled; ++i) {
        QString inputPath = m_files[i];
        emit progress(i, total, QFileInfo(inputPath).fileName(), 0);

        // Set FFmpeg progress callback with captured index for correct row update
        int currentI = i;
        watermarker.setProgressCallback([this, total, currentI](const WatermarkProgress& progress) {
            emit this->progress(currentI, total,
                               QString::fromStdString(progress.currentFile),
                               static_cast<int>(progress.percentComplete));
        });

        WatermarkResult result = watermarkInput(
            watermarker, baseConfig, inputPath, m_memberId, outputDir);

        if (!result.success && isDiskSpaceError(result)) {
            if (!result.outputFile.empty()) {
                QFile::remove(QString::fromStdString(result.outputFile));
            }
            const QString outputBaseDir = !m_memberId.isEmpty()
                ? (m_outputDir.isEmpty()
                    ? (m_rootDir.isEmpty() ? QFileInfo(inputPath).absolutePath() : m_rootDir)
                    : m_outputDir)
                : (outputDir.empty() ? QFileInfo(inputPath).absolutePath() : QString::fromStdString(outputDir));
            if (!m_cancelled) {
                pauseForDiskSpace(inputPath, outputBaseDir);
            }
            break;
        }

        if (result.success) {
            successCount++;
        } else {
            failCount++;
        }

        emit fileCompleted(i, result.success,
                          QString::fromStdString(result.outputFile),
                          QString::fromStdString(result.error));
    }

    emit finished(successCount, failCount);
}

// ==================== WatermarkPanel ====================

WatermarkPanel::WatermarkPanel(QWidget* parent)
    : QWidget(parent)
    , m_registry(MemberRegistry::instance())
{
    setObjectName("WatermarkPanel");
    setupUI();
    loadMembers();
    updateButtonStates();

    // Connect registry signals
    connect(m_registry, &MemberRegistry::membersReloaded, this, &WatermarkPanel::loadMembers);
    connect(m_registry, &MemberRegistry::memberAdded, this, &WatermarkPanel::loadMembers);
    connect(m_registry, &MemberRegistry::memberRemoved, this, &WatermarkPanel::loadMembers);
    connect(m_registry, &MemberRegistry::groupAdded, this, &WatermarkPanel::loadMembers);
    connect(m_registry, &MemberRegistry::groupUpdated, this, &WatermarkPanel::loadMembers);
    connect(m_registry, &MemberRegistry::groupRemoved, this, &WatermarkPanel::loadMembers);
}

WatermarkPanel::~WatermarkPanel() {
    if (m_workerThread) {
        if (m_workerThread->isRunning()) {
            if (m_worker) m_worker->cancel();
            m_workerThread->quit();
            if (!m_workerThread->wait(5000)) {
                m_workerThread->terminate();
                m_workerThread->wait();
            }
        }
        // deleteLater via QThread::finished handles actual deletion
        m_workerThread = nullptr;
        m_worker = nullptr;
    }
}

void WatermarkPanel::setController(WatermarkerController* controller) {
    if (m_controller) {
        disconnect(m_controller, nullptr, this, nullptr);
    }

    m_controller = controller;

    if (m_controller) {
        // Connect controller signals to panel slots
        connect(m_controller, &WatermarkerController::watermarkStarted,
                this, [this](int totalFiles) {
            m_statusLabel->setText(QString("Starting watermark of %1 files...").arg(totalFiles));
            m_progressBar->setMaximum(totalFiles);
            m_progressBar->setValue(0);
        });

        connect(m_controller, &WatermarkerController::watermarkProgress,
                this, [this](const QtWatermarkProgress& progress) {
            AnimationHelper::animateProgress(m_progressBar, progress.currentIndex);
            m_statusLabel->setText(QString("Processing: %1 (%2%)")
                .arg(progress.currentFile)
                .arg(static_cast<int>(progress.percentComplete)));
        });

        connect(m_controller, &WatermarkerController::fileCompleted,
                this, [this](const QtWatermarkResult& result) {
            // Update the file status in the table
            for (int i = 0; i < m_files.size(); ++i) {
                if (m_files[i].filePath == result.inputFile) {
                    m_files[i].status = result.success ? "complete" : "error";
                    m_files[i].outputPath = result.outputFile;
                    m_files[i].error = result.error;
                    populateTable();
                    break;
                }
            }
        });

        connect(m_controller, &WatermarkerController::watermarkFinished,
                this, [this](const QList<QtWatermarkResult>& results) {
            m_isRunning = false;
            updateButtonStates();
            AnimationHelper::smoothHide(m_progressBar);

            int success = 0, failed = 0;
            for (const auto& r : results) {
                if (r.success) success++;
                else failed++;
            }

            m_statusLabel->setText(QString("Watermarking complete: %1 succeeded, %2 failed")
                .arg(success).arg(failed));
            emit watermarkCompleted(success, failed);
        });

        connect(m_controller, &WatermarkerController::watermarkError,
                this, [this](const QString& error) {
            m_statusLabel->setText(QString("Error: %1").arg(error));
        });

        qDebug() << "WatermarkPanel: WatermarkerController connected";
    }
}

void WatermarkPanel::setupUI() {
    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    QWidget* contentWidget = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(contentWidget);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(16);

    // Title
    QLabel* titleLabel = new QLabel("Watermark Tool");
    titleLabel->setObjectName("PanelTitle");
    mainLayout->addWidget(titleLabel);

    // Description
    QLabel* descLabel = new QLabel("Add watermarks to videos (FFmpeg) and PDFs (Python). Select files, configure settings, and process.");
    descLabel->setObjectName("PanelSubtitle");
    descLabel->setWordWrap(true);
    mainLayout->addWidget(descLabel);

    // === File Selection Section ===
    QGroupBox* fileGroup = new QGroupBox("Source Files");
    QVBoxLayout* fileLayout = new QVBoxLayout(fileGroup);

    // Empty state (shown when no files are added)
    m_emptyState = new EmptyStateWidget(
        ":/icons/droplets.svg",
        "No files to watermark",
        "Add video or image files to apply custom watermarks before distribution.",
        "Add Files",
        this);
    connect(m_emptyState, &EmptyStateWidget::actionClicked, this, &WatermarkPanel::onAddFiles);
    fileLayout->addWidget(m_emptyState);

    // File table
    m_fileTable = new QTableWidget();
    m_fileTable->setColumnCount(6);
    m_fileTable->setHorizontalHeaderLabels({"File Name", "Member", "Type", "Size", "Status", "Output"});
    m_fileTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fileTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileTable->setAlternatingRowColors(true);
    m_fileTable->verticalHeader()->setVisible(false);
    m_fileTable->setContextMenuPolicy(Qt::CustomContextMenu);

    auto* header = m_fileTable->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::Stretch);   // File Name
    header->setSectionResizeMode(1, QHeaderView::Fixed);      // Member
    m_fileTable->setColumnWidth(1, 130);
    header->setSectionResizeMode(2, QHeaderView::Fixed);      // Type
    m_fileTable->setColumnWidth(2, 60);
    header->setSectionResizeMode(3, QHeaderView::Fixed);      // Size
    m_fileTable->setColumnWidth(3, 80);
    header->setSectionResizeMode(4, QHeaderView::Fixed);      // Status
    m_fileTable->setColumnWidth(4, 100);
    header->setSectionResizeMode(5, QHeaderView::Stretch);    // Output

    connect(m_fileTable, &QTableWidget::itemSelectionChanged,
            this, &WatermarkPanel::onTableSelectionChanged);
    connect(m_fileTable, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);

        // Copy actions
        QTableWidgetItem* clickedItem = m_fileTable->itemAt(pos);
        if (clickedItem) {
            menu.addAction("Copy Cell", this, [this, clickedItem]() {
                QApplication::clipboard()->setText(clickedItem->text());
            });
            menu.addAction("Copy Row", this, [this, clickedItem]() {
                CopyHelper::copyRow(m_fileTable, clickedItem->row());
            });
            menu.addSeparator();
        }

        menu.addAction("Remove Selected", this, &WatermarkPanel::onRemoveSelected);
        menu.addAction("Clear All", this, &WatermarkPanel::onClearAll);
        menu.exec(m_fileTable->viewport()->mapToGlobal(pos));
    });

    fileLayout->addWidget(m_fileTable, 1);

    // File action buttons
    QHBoxLayout* fileActionsLayout = new QHBoxLayout();
    fileActionsLayout->setSpacing(8);

    m_addFilesBtn = new QPushButton("Add Files...");
    m_addFilesBtn->setIcon(QIcon(":/icons/plus.svg"));
    connect(m_addFilesBtn, &QPushButton::clicked, this, &WatermarkPanel::onAddFiles);

    m_addFolderBtn = new QPushButton("Add Folder...");
    m_addFolderBtn->setIcon(QIcon(":/icons/folder.svg"));
    connect(m_addFolderBtn, &QPushButton::clicked, this, &WatermarkPanel::onAddFolder);

    m_removeBtn = new QPushButton("Remove");
    m_removeBtn->setIcon(QIcon(":/icons/trash-2.svg"));
    m_removeBtn->setEnabled(false);
    connect(m_removeBtn, &QPushButton::clicked, this, &WatermarkPanel::onRemoveSelected);

    m_clearBtn = new QPushButton("Clear All");
    m_clearBtn->setObjectName("PanelDangerButton");
    connect(m_clearBtn, &QPushButton::clicked, this, &WatermarkPanel::onClearAll);

    fileActionsLayout->addWidget(m_addFilesBtn);
    fileActionsLayout->addWidget(m_addFolderBtn);
    fileActionsLayout->addWidget(m_removeBtn);
    fileActionsLayout->addWidget(m_clearBtn);
    fileActionsLayout->addStretch();

    fileLayout->addLayout(fileActionsLayout);
    mainLayout->addWidget(fileGroup, 1);

    // === Settings Section ===
    QGroupBox* settingsGroup = new QGroupBox("Watermark Settings");
    QVBoxLayout* settingsLayout = new QVBoxLayout(settingsGroup);

    // Mode selection
    QHBoxLayout* modeLayout = new QHBoxLayout();
    modeLayout->addWidget(new QLabel("Mode:"));
    m_modeCombo = new QComboBox();
    m_modeCombo->addItem("Global Watermark", "global");
    m_modeCombo->addItem("Per-Member Watermark", "member");
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &WatermarkPanel::onModeChanged);
    modeLayout->addWidget(m_modeCombo);

    modeLayout->addStretch();
    settingsLayout->addLayout(modeLayout);

    // Member multi-select (hidden by default, shown in Per-Member mode)
    m_memberWidget = new QWidget();
    QVBoxLayout* memberVLayout = new QVBoxLayout(m_memberWidget);
    memberVLayout->setContentsMargins(0, 8, 0, 4);
    memberVLayout->setSpacing(8);

    // Toolbar: All / None / Add Group / Search / count
    QHBoxLayout* memberToolbar = new QHBoxLayout();
    memberToolbar->setSpacing(10);

    m_selectAllMembersBtn = new QPushButton("All");
    m_selectAllMembersBtn->setToolTip("Select all members");
    connect(m_selectAllMembersBtn, &QPushButton::clicked,
            this, &WatermarkPanel::onSelectAllMembers);

    m_deselectAllMembersBtn = new QPushButton("None");
    m_deselectAllMembersBtn->setToolTip("Deselect all members");
    connect(m_deselectAllMembersBtn, &QPushButton::clicked,
            this, &WatermarkPanel::onDeselectAllMembers);

    m_groupQuickSelectCombo = new QComboBox();
    m_groupQuickSelectCombo->setMinimumWidth(150);
    m_groupQuickSelectCombo->setToolTip("Check each member from a group so individual members can be removed");
    m_groupQuickSelectCombo->addItem("-- Add Group --", "");
    connect(m_groupQuickSelectCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &WatermarkPanel::onGroupQuickSelect);

    m_memberSearchEdit = new QLineEdit();
    m_memberSearchEdit->setPlaceholderText("Search members...");
    m_memberSearchEdit->setClearButtonEnabled(true);
    connect(m_memberSearchEdit, &QLineEdit::textChanged,
            this, &WatermarkPanel::onMemberSearchChanged);

    m_selectionSummaryLabel = new QLabel("0 selected");
    m_selectionSummaryLabel->setProperty("type", "secondary");

    memberToolbar->addWidget(m_selectAllMembersBtn);
    memberToolbar->addWidget(m_deselectAllMembersBtn);
    memberToolbar->addWidget(m_groupQuickSelectCombo);
    memberToolbar->addWidget(m_memberSearchEdit);
    memberToolbar->addStretch();
    memberToolbar->addWidget(m_selectionSummaryLabel);
    memberVLayout->addLayout(memberToolbar);

    // Checkable member list
    m_memberListWidget = new QListWidget();
    m_memberListWidget->setMaximumHeight(160);
    m_memberListWidget->setAlternatingRowColors(true);
    m_memberListWidget->setSelectionMode(QAbstractItemView::NoSelection);
    memberVLayout->addWidget(m_memberListWidget);

    m_memberWidget->setVisible(false);
    settingsLayout->addWidget(m_memberWidget);

    // Watermark text (for global mode)
    QGridLayout* textGrid = new QGridLayout();
    textGrid->setSpacing(8);

    QLabel* primaryLabel = new QLabel("Primary Text:");
    textGrid->addWidget(primaryLabel, 0, 0);
    m_primaryTextEdit = new QLineEdit();
    m_primaryTextEdit->setPlaceholderText("e.g., {brand} - {member_name} ({member_id})");
    m_primaryTextEdit->setToolTip("Template variables: {brand}, {member_name}, {member_id}, {member_email}, {member_ip}, {member_mac}, {member_social}, {date}, {timestamp}");
    textGrid->addWidget(m_primaryTextEdit, 0, 1);

    QLabel* secondaryLabel = new QLabel("Secondary Text:");
    textGrid->addWidget(secondaryLabel, 1, 0);
    m_secondaryTextEdit = new QLineEdit();
    m_secondaryTextEdit->setPlaceholderText("e.g., {member_email} - IP: {member_ip}");
    m_secondaryTextEdit->setToolTip("Template variables: {brand}, {member_name}, {member_id}, {member_email}, {member_ip}, {member_mac}, {member_social}, {date}, {timestamp}");
    textGrid->addWidget(m_secondaryTextEdit, 1, 1);

    // Help button for template variables
    m_watermarkHelpBtn = new QPushButton("?");
    m_watermarkHelpBtn->setFixedSize(24, 24);
    m_watermarkHelpBtn->setToolTip("Show available template variables");
    connect(m_watermarkHelpBtn, &QPushButton::clicked, this, &WatermarkPanel::onWatermarkHelpClicked);
    textGrid->addWidget(m_watermarkHelpBtn, 0, 2);

    // Preview button for expanded watermark text
    m_watermarkPreviewBtn = new QPushButton("Preview");
    m_watermarkPreviewBtn->setToolTip("Preview expanded watermark text with current settings");
    connect(m_watermarkPreviewBtn, &QPushButton::clicked, this, &WatermarkPanel::onPreviewWatermarkClicked);
    textGrid->addWidget(m_watermarkPreviewBtn, 1, 2);

    settingsLayout->addLayout(textGrid);

    // Metadata embedding section
    m_embedMetadataCheck = new QCheckBox("Embed member data in file metadata");
    m_embedMetadataCheck->setToolTip("Write member information into PDF properties or video metadata fields");
    settingsLayout->addWidget(m_embedMetadataCheck);

    QGridLayout* metaGrid = new QGridLayout();
    metaGrid->setSpacing(6);
    metaGrid->setContentsMargins(20, 0, 0, 0);

    auto addMetaField = [&](int row, const QString& label, QLineEdit*& edit,
                            const QString& placeholder, const QString& defaultVal) {
        QLabel* lbl = new QLabel(label);
        metaGrid->addWidget(lbl, row, 0);
        edit = new QLineEdit();
        edit->setPlaceholderText(placeholder);
        edit->setText(defaultVal);
        edit->setToolTip("Supports template variables: {brand}, {member_name}, {member_id}, {member_email}, etc.");
        edit->setEnabled(false);
        metaGrid->addWidget(edit, row, 1);
    };

    addMetaField(0, "Title:", m_metaTitleEdit,
        "e.g., {brand} - Watermarked for {member_name}",
        "{brand} - {member_name}");
    addMetaField(1, "Author:", m_metaAuthorEdit,
        "e.g., {member_name} ({member_id})",
        "{member_name} ({member_id})");
    addMetaField(2, "Comment:", m_metaCommentEdit,
        "e.g., ID: {member_id} | Email: {member_email}",
        "ID: {member_id} | {member_email}");
    addMetaField(3, "Keywords:", m_metaKeywordsEdit,
        "e.g., {member_id}, {member_email}",
        "{member_id}, {member_email}");

    settingsLayout->addLayout(metaGrid);

    connect(m_embedMetadataCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_metaTitleEdit->setEnabled(checked);
        m_metaAuthorEdit->setEnabled(checked);
        m_metaCommentEdit->setEnabled(checked);
        m_metaKeywordsEdit->setEnabled(checked);
    });

    // Video settings
    QHBoxLayout* videoSettingsLayout = new QHBoxLayout();
    videoSettingsLayout->setSpacing(16);

    videoSettingsLayout->addWidget(new QLabel("Preset:"));
    m_presetCombo = new QComboBox();
    m_presetCombo->addItems({"ultrafast", "superfast", "veryfast", "faster", "fast", "medium"});
    m_presetCombo->setCurrentText("ultrafast");
    m_presetCombo->setToolTip("FFmpeg encoding preset (faster = lower quality, slower = better quality)");
    videoSettingsLayout->addWidget(m_presetCombo);

    videoSettingsLayout->addWidget(new QLabel("Quality (CRF):"));
    m_crfSpin = new QSpinBox();
    m_crfSpin->setRange(18, 28);
    m_crfSpin->setValue(23);
    m_crfSpin->setToolTip("Constant Rate Factor (18=best quality, 28=smallest file)");
    videoSettingsLayout->addWidget(m_crfSpin);

    videoSettingsLayout->addWidget(new QLabel("Interval (s):"));
    m_intervalSpin = new QSpinBox();
    m_intervalSpin->setRange(60, 3600);
    m_intervalSpin->setValue(600);
    m_intervalSpin->setToolTip("Seconds between watermark appearances");
    videoSettingsLayout->addWidget(m_intervalSpin);

    videoSettingsLayout->addWidget(new QLabel("Duration (s):"));
    m_durationSpin = new QSpinBox();
    m_durationSpin->setRange(1, 30);
    m_durationSpin->setValue(3);
    m_durationSpin->setToolTip("How long watermark stays visible");
    videoSettingsLayout->addWidget(m_durationSpin);

    m_fastSegmentedCheck = new QCheckBox("Fast segments");
    m_fastSegmentedCheck->setToolTip(
        "Try cached segmented video watermarking for MP4/H.264/AAC files. "
        "Unsupported files automatically fall back to standard encoding.");
    videoSettingsLayout->addWidget(m_fastSegmentedCheck);

    videoSettingsLayout->addStretch();

    m_settingsBtn = new QPushButton("More Settings...");
    connect(m_settingsBtn, &QPushButton::clicked, this, &WatermarkPanel::onOpenSettings);
    videoSettingsLayout->addWidget(m_settingsBtn);

    settingsLayout->addLayout(videoSettingsLayout);

    // Preset management row
    QHBoxLayout* presetLayout = new QHBoxLayout();
    presetLayout->addWidget(new QLabel("Preset:"));

    m_presetNameCombo = new QComboBox();
    m_presetNameCombo->setMinimumWidth(150);
    m_presetNameCombo->addItem("-- Select Preset --", "");
    connect(m_presetNameCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &WatermarkPanel::onPresetChanged);
    presetLayout->addWidget(m_presetNameCombo);

    m_savePresetBtn = new QPushButton("Save");
    m_savePresetBtn->setToolTip("Save current settings as a preset");
    connect(m_savePresetBtn, &QPushButton::clicked, this, &WatermarkPanel::onSavePreset);
    presetLayout->addWidget(m_savePresetBtn);

    m_deletePresetBtn = new QPushButton("Delete");
    m_deletePresetBtn->setObjectName("PanelDangerButton");
    m_deletePresetBtn->setToolTip("Delete selected preset");
    m_deletePresetBtn->setEnabled(false);
    connect(m_deletePresetBtn, &QPushButton::clicked, this, &WatermarkPanel::onDeletePreset);
    presetLayout->addWidget(m_deletePresetBtn);

    presetLayout->addStretch();
    settingsLayout->addLayout(presetLayout);

    // Load saved presets
    loadPresets();

    // Output directory
    QHBoxLayout* outputLayout = new QHBoxLayout();
    outputLayout->addWidget(new QLabel("Output:"));
    m_outputDirEdit = new QLineEdit();
    m_outputDirEdit->setPlaceholderText("Output directory (leave empty for same as input)");
    outputLayout->addWidget(m_outputDirEdit, 1);
    m_browseOutputBtn = new QPushButton("Browse...");
    connect(m_browseOutputBtn, &QPushButton::clicked, this, &WatermarkPanel::onBrowseOutput);
    outputLayout->addWidget(m_browseOutputBtn);
    m_sameAsInputCheck = new QCheckBox("Same as input");
    m_sameAsInputCheck->setChecked(true);
    connect(m_sameAsInputCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_outputDirEdit->setEnabled(!checked);
        m_browseOutputBtn->setEnabled(!checked);
    });
    m_outputDirEdit->setEnabled(false);
    m_browseOutputBtn->setEnabled(false);
    outputLayout->addWidget(m_sameAsInputCheck);
    settingsLayout->addLayout(outputLayout);

    // Auto-upload & Smart Estimate
    m_autoUploadCheck = new QCheckBox("Auto-upload to MEGA && cleanup after each member (saves disk space)");
    m_autoUploadCheck->setObjectName("AutoUploadCheck");
    m_autoUploadCheck->setToolTip(
        "After watermarking all files for a member:\n"
        "1. Upload to their MEGA distribution folder\n"
        "2. Delete local copies\n"
        "3. Move to next member\n\n"
        "Requires distribution folders configured in Members panel.\n"
        "The app learns from each run to make better space predictions.");
    m_autoUploadCheck->setEnabled(false);  // Only enabled in per-member mode
    settingsLayout->addWidget(m_autoUploadCheck);

    // Custom upload path (alternative to per-member distribution folders)
    auto* customPathLayout = new QHBoxLayout();
    m_customPathCheck = new QCheckBox("Upload to custom path instead of member folders");
    m_customPathCheck->setToolTip(
        "Upload all watermarked files to a single MEGA folder\n"
        "instead of each member's distribution folder.");
    m_customPathCheck->setEnabled(false);
    connect(m_customPathCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_customPathEdit->setEnabled(checked);
        m_browseCustomPathBtn->setEnabled(checked);
    });
    customPathLayout->addWidget(m_customPathCheck);

    m_customPathEdit = new QLineEdit();
    m_customPathEdit->setPlaceholderText("/Cloud/Watermarked");
    m_customPathEdit->setEnabled(false);
    customPathLayout->addWidget(m_customPathEdit, 1);

    m_browseCustomPathBtn = new QPushButton("Browse...");
    m_browseCustomPathBtn->setEnabled(false);
    connect(m_browseCustomPathBtn, &QPushButton::clicked, this, [this]() {
        if (!m_megaApi) {
            QMessageBox::warning(this, "Not Connected", "Please log in first.");
            return;
        }
        RemoteFolderBrowserDialog dialog(this);
        dialog.setMegaApi(m_megaApi);
        dialog.setSelectionMode(RemoteFolderBrowserDialog::SingleFolder);
        dialog.setTitle("Select Upload Destination");
        if (dialog.exec() == QDialog::Accepted) {
            m_customPathEdit->setText(dialog.selectedPath());
        }
    });
    customPathLayout->addWidget(m_browseCustomPathBtn);
    settingsLayout->addLayout(customPathLayout);

    // Wire auto-upload toggle to enable/disable custom path
    connect(m_autoUploadCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_customPathCheck->setEnabled(checked);
        if (!checked) {
            m_customPathCheck->setChecked(false);
        }
    });

    m_smartEstimateLabel = new QLabel();
    m_smartEstimateLabel->setObjectName("SmartEstimateLabel");
    m_smartEstimateLabel->setWordWrap(true);
    settingsLayout->addWidget(m_smartEstimateLabel);

    mainLayout->addWidget(settingsGroup);

    // === Progress Section ===
    QHBoxLayout* progressLayout = new QHBoxLayout();

    m_progressBar = new QProgressBar();
    m_progressBar->setMinimum(0);
    m_progressBar->setMaximum(100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    progressLayout->addWidget(m_progressBar, 1);

    mainLayout->addLayout(progressLayout);

    // Status
    m_statusLabel = new QLabel("Ready");
    m_statusLabel->setProperty("type", "secondary");
    mainLayout->addWidget(m_statusLabel);

    // === Action Buttons ===
    QHBoxLayout* actionsLayout = new QHBoxLayout();
    actionsLayout->setSpacing(8);

    m_checkDepsBtn = new QPushButton("Check Dependencies");
    m_checkDepsBtn->setToolTip("Check if FFmpeg and Python are available");
    connect(m_checkDepsBtn, &QPushButton::clicked, this, &WatermarkPanel::onCheckDependencies);
    actionsLayout->addWidget(m_checkDepsBtn);

    m_reviewIssuesBtn = new QPushButton("Review Issues");
    m_reviewIssuesBtn->setObjectName("PanelSecondaryButton");
    m_reviewIssuesBtn->setIcon(QIcon(":/icons/alert-circle.svg"));
    m_reviewIssuesBtn->setEnabled(false);
    m_reviewIssuesBtn->setToolTip("No actionable Watermark issues are currently detected.");
    connect(m_reviewIssuesBtn, &QPushButton::clicked, this, &WatermarkPanel::onReviewWatermarkIssues);
    actionsLayout->addWidget(m_reviewIssuesBtn);

    m_openReportFolderBtn = new QPushButton("Open Report Folder");
    m_openReportFolderBtn->setObjectName("PanelSecondaryButton");
    m_openReportFolderBtn->setIcon(QIcon(":/icons/folder.svg"));
    m_openReportFolderBtn->setEnabled(false);
    m_openReportFolderBtn->setToolTip("A Watermark completion or incomplete-output report is not available yet.");
    connect(m_openReportFolderBtn, &QPushButton::clicked, this, &WatermarkPanel::onOpenWatermarkReportFolder);
    actionsLayout->addWidget(m_openReportFolderBtn);

    m_retryFailedRowsBtn = new QPushButton("Retry Failed Rows");
    m_retryFailedRowsBtn->setObjectName("PanelSecondaryButton");
    m_retryFailedRowsBtn->setIcon(QIcon(":/icons/refresh-cw.svg"));
    m_retryFailedRowsBtn->setEnabled(false);
    m_retryFailedRowsBtn->setToolTip("No retryable failed Watermark rows are loaded.");
    connect(m_retryFailedRowsBtn, &QPushButton::clicked, this, &WatermarkPanel::onRetryFailedWatermarkRows);
    actionsLayout->addWidget(m_retryFailedRowsBtn);

    actionsLayout->addStretch();

    m_startBtn = new QPushButton("Start Watermarking");
    m_startBtn->setObjectName("PanelPrimaryButton");
    m_startBtn->setIcon(QIcon(":/icons/play.svg"));
    m_startBtn->setEnabled(false);
    connect(m_startBtn, &QPushButton::clicked, this, &WatermarkPanel::onStartWatermark);
    actionsLayout->addWidget(m_startBtn);

    m_stopBtn = new QPushButton("Stop");
    m_stopBtn->setObjectName("PanelDangerButton");
    m_stopBtn->setIcon(QIcon(":/icons/stop.svg"));
    m_stopBtn->setEnabled(false);
    connect(m_stopBtn, &QPushButton::clicked, this, &WatermarkPanel::onStopWatermark);
    actionsLayout->addWidget(m_stopBtn);

    m_sendToDistBtn = new QPushButton("Send to Distribution");
    m_sendToDistBtn->setObjectName("PanelSecondaryButton");
    m_sendToDistBtn->setIcon(QIcon(":/icons/share.svg"));
    m_sendToDistBtn->setEnabled(false);
    m_sendToDistBtn->setToolTip("Send completed watermarked files to Distribution panel");
    connect(m_sendToDistBtn, &QPushButton::clicked, this, &WatermarkPanel::onSendToDistribution);
    actionsLayout->addWidget(m_sendToDistBtn);

    mainLayout->addLayout(actionsLayout);

    // Stats
    m_statsLabel = new QLabel();
    m_statsLabel->setProperty("type", "secondary");
    mainLayout->addWidget(m_statsLabel);

    updateStats();

    scrollArea->setWidget(contentWidget);
    outerLayout->addWidget(scrollArea);
}

void WatermarkPanel::refresh() {
    loadMembers();
    updateStats();
}

void WatermarkPanel::addFilesFromDownloader(const QStringList& filePaths) {
    if (filePaths.isEmpty()) {
        return;
    }

    for (const QString& file : filePaths) {
        // Check if already in list
        bool exists = false;
        for (const WatermarkFileInfo& info : m_files) {
            if (info.filePath == file) {
                exists = true;
                break;
            }
        }
        if (exists) continue;

        QFileInfo fi(file);
        if (!fi.exists()) continue;

        WatermarkFileInfo info;
        info.filePath = file;
        info.fileName = fi.fileName();
        info.fileSize = fi.size();
        info.status = "pending";

        QString ext = fi.suffix().toLower();
        if (ext == "pdf") {
            info.fileType = "pdf";
        } else {
            info.fileType = "video";
        }

        m_files.append(info);
    }

    populateTable();
    updateStats();
    updateButtonStates();

    // Show notification
    if (!filePaths.isEmpty()) {
        m_statusLabel->setText(QString("Received %1 %2 from Downloader").arg(filePaths.size()).arg(filePaths.size() == 1 ? "file" : "files"));
    }
}

void WatermarkPanel::selectMember(const QString& memberId) {
    if (memberId.isEmpty()) {
        return;
    }

    // Switch to Per-Member mode if not already
    int perMemberIndex = m_modeCombo->findData("member");
    if (perMemberIndex >= 0 && m_modeCombo->currentIndex() != perMemberIndex) {
        m_modeCombo->setCurrentIndex(perMemberIndex);
    }

    // Find and check the member in the list widget
    m_memberListWidget->blockSignals(true);
    for (int i = 0; i < m_memberListWidget->count(); ++i) {
        QListWidgetItem* item = m_memberListWidget->item(i);
        if (item->data(Qt::UserRole).toString() == memberId) {
            item->setCheckState(Qt::Checked);
            m_memberListWidget->scrollToItem(item);
            break;
        }
    }
    m_memberListWidget->blockSignals(false);
    onMemberSelectionChanged();

    m_statusLabel->setText(QString("Selected member: %1").arg(memberId));
}

void WatermarkPanel::retryJob(const QString& jobId) {
    if (m_isRunning) {
        QMessageBox::warning(this, "Watermark Running",
            "A watermark job is already running. Stop it before retrying another job.");
        return;
    }

    OperationJobRecord record = OperationJobStore::instance().job(jobId);
    if (record.id.isEmpty() || record.type != OperationJobType::Watermark) {
        QMessageBox::warning(this, "Retry Unavailable",
            "This job cannot be retried from the Watermark panel.");
        return;
    }

    QStringList filePaths;
    const QJsonArray filesArray = record.metadata["filePaths"].toArray();
    for (const QJsonValue& value : filesArray) {
        const QString path = value.toString().trimmed();
        if (!path.isEmpty()) {
            filePaths.append(path);
        }
    }

    if (filePaths.isEmpty()) {
        QMessageBox::warning(this, "Retry Unavailable",
            "This watermark job does not contain retry metadata. New watermark jobs created after this update can be retried.");
        return;
    }

    if (!m_files.isEmpty()) {
        int reply = QMessageBox::question(this, "Replace Watermark Queue",
            "Retrying this job will replace the current watermark queue. Continue?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    const QJsonObject metadata = record.metadata;
    const QString mode = metadata["mode"].toString("global");
    const int modeIndex = m_modeCombo->findData(mode);
    if (modeIndex >= 0) {
        m_modeCombo->setCurrentIndex(modeIndex);
    }

    m_primaryTextEdit->setText(metadata["primaryText"].toString());
    m_secondaryTextEdit->setText(metadata["secondaryText"].toString());
    m_sameAsInputCheck->setChecked(metadata["sameAsInput"].toBool(true));
    m_outputDirEdit->setText(metadata["outputDirRaw"].toString());
    m_sourceRootDir = metadata["sourceRootDir"].toString();

    auto setComboValue = [](QComboBox* combo, const QString& value) {
        if (!combo || value.isEmpty()) return;
        const int index = combo->findText(value);
        if (index >= 0) {
            combo->setCurrentIndex(index);
        }
    };
    setComboValue(m_presetCombo, metadata["preset"].toString());
    if (metadata.contains("crf")) {
        m_crfSpin->setValue(metadata["crf"].toInt(m_crfSpin->value()));
    }
    if (metadata.contains("intervalSeconds")) {
        m_intervalSpin->setValue(metadata["intervalSeconds"].toInt(m_intervalSpin->value()));
    }
    if (metadata.contains("durationSeconds")) {
        m_durationSpin->setValue(metadata["durationSeconds"].toInt(m_durationSpin->value()));
    }
    if (m_fastSegmentedCheck) {
        m_fastSegmentedCheck->setChecked(metadata["fastSegmentedEncode"].toBool(false));
    }

    m_embedMetadataCheck->setChecked(metadata["embedMetadata"].toBool(false));
    m_metaTitleEdit->setText(metadata["metadataTitle"].toString());
    m_metaAuthorEdit->setText(metadata["metadataAuthor"].toString());
    m_metaCommentEdit->setText(metadata["metadataComment"].toString());
    m_metaKeywordsEdit->setText(metadata["metadataKeywords"].toString());

    if (m_autoUploadCheck) {
        m_autoUploadCheck->setChecked(metadata["autoUpload"].toBool(false));
    }
    if (m_customPathCheck) {
        m_customPathCheck->setChecked(metadata["customPathEnabled"].toBool(false));
    }
    if (m_customPathEdit) {
        m_customPathEdit->setText(metadata["customUploadPath"].toString());
    }

    QSet<QString> selectedMembers;
    const QJsonArray memberArray = metadata["selectedMemberIds"].toArray();
    for (const QJsonValue& value : memberArray) {
        const QString id = value.toString().trimmed();
        if (!id.isEmpty()) {
            selectedMembers.insert(id);
        }
    }

    loadMembers();
    m_memberListWidget->blockSignals(true);
    for (int i = 0; i < m_memberListWidget->count(); ++i) {
        QListWidgetItem* item = m_memberListWidget->item(i);
        const QString id = item->data(Qt::UserRole).toString();
        item->setCheckState(selectedMembers.contains(id) ? Qt::Checked : Qt::Unchecked);
    }
    m_memberListWidget->blockSignals(false);
    onMemberSelectionChanged();

    auto fileTypeForPath = [](const QString& path) {
        const QString ext = QFileInfo(path).suffix().toLower();
        static const QSet<QString> videoExts = {
            "mp4", "mkv", "avi", "mov", "wmv", "flv", "webm", "m4v", "mpeg", "mpg"
        };
        static const QSet<QString> audioExts = {"mp3", "flac", "wav", "aac", "ogg", "m4a", "wma", "opus"};
        if (ext == "pdf") return QString("pdf");
        if (audioExts.contains(ext)) return QString("audio");
        if (videoExts.contains(ext)) return QString("video");
        return QString("passthrough");
    };

    QStringList missingFiles;
    m_files.clear();
    m_lastMemberFileMap.clear();
    m_workerIdxToRow.clear();
    for (const QString& path : filePaths) {
        QFileInfo fi(path);
        if (!fi.exists()) {
            missingFiles.append(path);
            continue;
        }

        WatermarkFileInfo info;
        info.filePath = path;
        info.fileName = fi.fileName();
        info.fileSize = fi.size();
        info.fileType = fileTypeForPath(path);
        info.status = "pending";
        m_files.append(info);
    }

    if (!missingFiles.isEmpty()) {
        QMessageBox::warning(this, "Missing Files",
            QString("%1 file(s) from this job no longer exist and will be skipped:\n\n%2")
                .arg(missingFiles.size())
                .arg(missingFiles.join("\n")));
    }

    if (m_files.isEmpty()) {
        QMessageBox::warning(this, "Retry Unavailable",
            "None of the original files are still available.");
        m_retrySourceJobId.clear();
        populateTable();
        updateStats();
        updateButtonStates();
        return;
    }

    populateTable();
    updateStats();
    updateButtonStates();
    m_retrySourceJobId = record.id;
    m_statusLabel->setText(QString("Retrying watermark job %1").arg(record.id));

    onStartWatermark();
}

void WatermarkPanel::resumeJob(const QString& jobId) {
    if (m_isRunning) {
        QMessageBox::warning(this, "Watermark Running",
            "Wait for the current watermark job to finish before resuming another job.");
        return;
    }

    OperationJobRecord record = OperationJobStore::instance().job(jobId);
    if (record.id.isEmpty() || record.type != OperationJobType::Watermark) {
        QMessageBox::warning(this, "Resume Unavailable",
            "The selected job is not a saved Watermark job.");
        return;
    }
    if (record.status != OperationJobStatus::Paused) {
        QMessageBox::warning(this, "Resume Unavailable",
            "Only paused Watermark jobs can be resumed from the Jobs tab.");
        return;
    }
    if (!record.metadata["watermarkRows"].isArray()
        || record.metadata["watermarkRows"].toArray().isEmpty()) {
        QMessageBox::warning(this, "Resume Needs Checkpoint",
            "This paused Watermark job does not contain saved row checkpoints.\n\n"
            "Jobs created before checkpoint persistence still need the live Watermark table state.");
        return;
    }

    bool sameJobLoaded = jobId == m_pausedJobId || jobId == m_currentJobId;
    if (!sameJobLoaded) {
        for (const WatermarkFileInfo& info : m_files) {
            if (info.jobId == jobId) {
                sameJobLoaded = true;
                break;
            }
        }
    }

    if (!sameJobLoaded && !m_files.isEmpty()) {
        const int reply = QMessageBox::question(this, "Restore Watermark Checkpoint",
            "Resume needs to load this job's saved Watermark checkpoint, replacing the current Watermark table. Continue?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    if (!sameJobLoaded && !restoreWatermarkRowsFromJob(record)) {
        QMessageBox::warning(this, "Resume Failed",
            "The saved Watermark checkpoint could not be restored.");
        return;
    }

    if (sameJobLoaded && !m_pausedForDiskSpace) {
        m_pausedJobId = jobId;
        m_pausedForDiskSpace = true;
        m_diskSpacePauseMessage = record.summary.isEmpty()
            ? "Paused watermark job restored from Jobs."
            : record.summary;
        updateButtonStates();
    }

    onResumePausedWatermark();
}

QJsonArray WatermarkPanel::serializeWatermarkRows() const {
    QJsonArray rows;
    for (int row = 0; row < m_files.size(); ++row) {
        const WatermarkFileInfo& info = m_files[row];
        QJsonObject object;
        object["row"] = row;
        object["isHeader"] = info.isHeader;
        object["sourcePath"] = info.filePath;
        object["fileName"] = info.fileName;
        object["fileSizeBytes"] = QString::number(info.fileSize);
        object["fileType"] = info.fileType;
        object["memberName"] = info.memberName;
        object["memberId"] = info.memberId;
        object["status"] = info.status;
        object["outputPath"] = info.outputPath;
        object["error"] = info.error;
        object["progressPercent"] = info.progressPercent;
        object["jobId"] = info.jobId;
        rows.append(object);
    }
    return rows;
}

void WatermarkPanel::saveWatermarkCheckpoint(const QString& reason, const QString& jobId) {
    QString targetJobId = jobId.trimmed().isEmpty()
        ? (!m_currentJobId.trimmed().isEmpty() ? m_currentJobId : m_pausedJobId)
        : jobId.trimmed();
    if (targetJobId.isEmpty()) {
        for (const WatermarkFileInfo& info : m_files) {
            if (!info.isHeader && !info.jobId.trimmed().isEmpty()) {
                targetJobId = info.jobId.trimmed();
                break;
            }
        }
    }
    if (targetJobId.isEmpty() || m_files.isEmpty()) {
        return;
    }

    OperationJobRecord record = OperationJobStore::instance().job(targetJobId);
    if (record.id.isEmpty() || record.type != OperationJobType::Watermark) {
        return;
    }

    int rowCount = 0;
    int pendingCount = 0;
    int processingCount = 0;
    int completeCount = 0;
    int uploadedCount = 0;
    int errorCount = 0;
    for (const WatermarkFileInfo& info : m_files) {
        if (info.isHeader) {
            continue;
        }
        ++rowCount;
        if (info.status == "pending") ++pendingCount;
        else if (info.status == "processing") ++processingCount;
        else if (info.status == "complete") ++completeCount;
        else if (info.status == "uploaded") ++uploadedCount;
        else if (info.status == "error") ++errorCount;
    }

    QJsonObject metadata = record.metadata;
    metadata["watermarkCheckpointVersion"] = 1;
    metadata["watermarkCheckpointReason"] = reason;
    metadata["watermarkCheckpointUpdatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    metadata["watermarkRows"] = serializeWatermarkRows();
    metadata["watermarkRowCount"] = rowCount;
    metadata["watermarkPendingRows"] = pendingCount;
    metadata["watermarkProcessingRows"] = processingCount;
    metadata["watermarkCompleteRows"] = completeCount;
    metadata["watermarkUploadedRows"] = uploadedCount;
    metadata["watermarkErrorRows"] = errorCount;

    OperationJobStore::instance().createJob(
        OperationJobType::Watermark,
        record.title,
        qMax(record.plannedCount, rowCount),
        metadata,
        targetJobId);
}

void WatermarkPanel::applyWatermarkJobMetadataToUi(const QJsonObject& metadata) {
    const QString mode = metadata["mode"].toString("global");
    const int modeIndex = m_modeCombo->findData(mode);
    if (modeIndex >= 0) {
        m_modeCombo->setCurrentIndex(modeIndex);
    }

    m_primaryTextEdit->setText(metadata["primaryText"].toString());
    m_secondaryTextEdit->setText(metadata["secondaryText"].toString());
    m_sameAsInputCheck->setChecked(metadata["sameAsInput"].toBool(true));
    m_outputDirEdit->setText(metadata["outputDirRaw"].toString());
    m_sourceRootDir = metadata["sourceRootDir"].toString();

    auto setComboValue = [](QComboBox* combo, const QString& value) {
        if (!combo || value.isEmpty()) return;
        const int index = combo->findText(value);
        if (index >= 0) {
            combo->setCurrentIndex(index);
        }
    };
    setComboValue(m_presetCombo, metadata["preset"].toString());
    if (metadata.contains("crf")) {
        m_crfSpin->setValue(metadata["crf"].toInt(m_crfSpin->value()));
    }
    if (metadata.contains("intervalSeconds")) {
        m_intervalSpin->setValue(metadata["intervalSeconds"].toInt(m_intervalSpin->value()));
    }
    if (metadata.contains("durationSeconds")) {
        m_durationSpin->setValue(metadata["durationSeconds"].toInt(m_durationSpin->value()));
    }
    if (m_fastSegmentedCheck) {
        m_fastSegmentedCheck->setChecked(metadata["fastSegmentedEncode"].toBool(false));
    }

    m_embedMetadataCheck->setChecked(metadata["embedMetadata"].toBool(false));
    m_metaTitleEdit->setText(metadata["metadataTitle"].toString());
    m_metaAuthorEdit->setText(metadata["metadataAuthor"].toString());
    m_metaCommentEdit->setText(metadata["metadataComment"].toString());
    m_metaKeywordsEdit->setText(metadata["metadataKeywords"].toString());

    if (m_autoUploadCheck) {
        m_autoUploadCheck->setChecked(metadata["autoUpload"].toBool(false));
    }
    if (m_customPathCheck) {
        m_customPathCheck->setChecked(metadata["customPathEnabled"].toBool(false));
    }
    if (m_customPathEdit) {
        m_customPathEdit->setText(metadata["customUploadPath"].toString());
    }

    QSet<QString> selectedMembers;
    const QJsonArray memberArray = metadata["selectedMemberIds"].toArray();
    for (const QJsonValue& value : memberArray) {
        const QString id = value.toString().trimmed();
        if (!id.isEmpty()) {
            selectedMembers.insert(id);
        }
    }

    loadMembers();
    m_memberListWidget->blockSignals(true);
    for (int i = 0; i < m_memberListWidget->count(); ++i) {
        QListWidgetItem* item = m_memberListWidget->item(i);
        const QString id = item->data(Qt::UserRole).toString();
        item->setCheckState(selectedMembers.contains(id) ? Qt::Checked : Qt::Unchecked);
    }
    m_memberListWidget->blockSignals(false);
    onMemberSelectionChanged();
}

bool WatermarkPanel::restoreWatermarkRowsFromJob(const OperationJobRecord& record) {
    const QJsonArray rows = record.metadata["watermarkRows"].toArray();
    if (rows.isEmpty()) {
        return false;
    }

    applyWatermarkJobMetadataToUi(record.metadata);

    QList<WatermarkFileInfo> restoredRows;
    restoredRows.reserve(rows.size());
    for (const QJsonValue& value : rows) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject object = value.toObject();
        WatermarkFileInfo info;
        info.isHeader = object["isHeader"].toBool(false);
        info.filePath = object["sourcePath"].toString();
        info.fileName = object["fileName"].toString();
        if (info.fileName.isEmpty() && !info.filePath.isEmpty()) {
            info.fileName = QFileInfo(info.filePath).fileName();
        }
        info.fileSize = object["fileSizeBytes"].toString().toLongLong();
        if (info.fileSize <= 0 && QFileInfo::exists(info.filePath)) {
            info.fileSize = QFileInfo(info.filePath).size();
        }
        info.fileType = object["fileType"].toString();
        info.memberName = object["memberName"].toString();
        info.memberId = object["memberId"].toString();
        info.status = object["status"].toString("pending");
        info.outputPath = object["outputPath"].toString();
        info.error = object["error"].toString();
        info.progressPercent = object["progressPercent"].toInt(0);
        info.jobId = object["jobId"].toString(record.id);
        if (info.jobId.isEmpty()) {
            info.jobId = record.id;
        }
        restoredRows.append(info);
    }

    if (restoredRows.isEmpty()) {
        return false;
    }

    m_files = restoredRows;
    m_workerIdxToRow.clear();
    m_lastMemberFileMap.clear();
    m_currentJobId.clear();
    m_pausedJobId = record.status == OperationJobStatus::Paused ? record.id : QString();
    m_pausedForDiskSpace = record.status == OperationJobStatus::Paused;
    m_diskSpacePauseMessage = m_pausedForDiskSpace
        ? (record.summary.isEmpty() ? "Paused watermark job restored from checkpoint." : record.summary)
        : QString();
    m_currentJobCancelled = false;

    populateTable();
    updateStats();
    updateButtonStates();
    m_statusLabel->setText(QString("Restored watermark checkpoint for job %1").arg(record.id));
    return true;
}

void WatermarkPanel::cleanupJob(const QString& jobId) {
    if (m_isRunning) {
        QMessageBox::warning(this, "Watermark Running",
            "Cleanup is not available while watermarking is running.");
        return;
    }

    OperationJobRecord record = OperationJobStore::instance().job(jobId);
    if (record.id.isEmpty() || record.type != OperationJobType::Watermark) {
        QMessageBox::warning(this, "Cleanup Unavailable",
            "This job cannot be cleaned up from the Watermark panel.");
        return;
    }

    const bool cleanupStatus = record.status == OperationJobStatus::Paused
        || record.status == OperationJobStatus::Failed
        || record.status == OperationJobStatus::CleanupRequired;
    if (!cleanupStatus) {
        QMessageBox::information(this, "Cleanup Not Needed",
            "Cleanup is only available for paused, failed, or cleanup-required watermark jobs.");
        return;
    }

    bool liveTableMatchesJob = (jobId == m_pausedJobId || jobId == m_currentJobId);
    if (!m_files.isEmpty()) {
        for (const WatermarkFileInfo& info : m_files) {
            if (info.isHeader) {
                continue;
            }
            if (info.jobId == jobId) {
                liveTableMatchesJob = true;
                break;
            }
        }
    }

    if (!liveTableMatchesJob && record.metadata["watermarkRows"].isArray()) {
        if (!m_files.isEmpty()) {
            int reply = QMessageBox::question(this, "Restore Watermark Checkpoint",
                "Cleanup needs to load this job's saved Watermark checkpoint, replacing the current Watermark table. Continue?",
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            if (reply != QMessageBox::Yes) {
                return;
            }
        }
        liveTableMatchesJob = restoreWatermarkRowsFromJob(record);
    }

    if (!liveTableMatchesJob) {
        QMessageBox::warning(this, "Cleanup Needs Live State",
            "This job does not have enough live cleanup state in the Watermark panel.\n\n"
            "Cleanup is safe when the selected job's live table rows are loaded or when the job contains saved Watermark row checkpoints. "
            "Jobs from older builds without checkpoints still need manual review.");
        return;
    }

    struct CleanupCandidate {
        QString path;
        QString reason;
        bool directory = false;
    };

    auto appendUniqueCandidate = [](QList<CleanupCandidate>& candidates,
                                    QSet<QString>& seen,
                                    const QString& path,
                                    const QString& reason,
                                    bool directory) {
        const QString cleaned = QDir::cleanPath(path);
        if (cleaned.isEmpty() || seen.contains(cleaned)) {
            return;
        }
        seen.insert(cleaned);
        CleanupCandidate candidate;
        candidate.path = cleaned;
        candidate.reason = reason;
        candidate.directory = directory;
        candidates.append(candidate);
    };

    auto expectedMemberOutputDir = [this](const WatermarkFileInfo& info) {
        if (info.memberId.isEmpty() || info.filePath.isEmpty()) {
            return QString();
        }

        const QFileInfo sourceInfo(info.filePath);
        QString baseDir;
        if (!m_sameAsInputCheck->isChecked() && !m_outputDirEdit->text().trimmed().isEmpty()) {
            baseDir = m_outputDirEdit->text().trimmed();
        } else if (!m_sourceRootDir.trimmed().isEmpty()) {
            baseDir = m_sourceRootDir.trimmed();
        } else {
            baseDir = sourceInfo.absolutePath();
        }

        QString relativeSubdir;
        if (!m_sourceRootDir.trimmed().isEmpty()) {
            const QDir rootDir(m_sourceRootDir.trimmed());
            const QString relative = rootDir.relativeFilePath(sourceInfo.absolutePath());
            if (!relative.isEmpty() && relative != "." && !relative.startsWith("..")) {
                relativeSubdir = relative;
            }
        }

        QString memberDir = QDir(baseDir).filePath(info.memberId);
        if (!relativeSubdir.isEmpty()) {
            memberDir = QDir(memberDir).filePath(relativeSubdir);
        }
        return QDir::cleanPath(memberDir);
    };

    QList<CleanupCandidate> fileCandidates;
    QList<CleanupCandidate> dirCandidates;
    QSet<QString> seenFiles;
    QSet<QString> seenDirs;
    QMap<QString, QList<int>> fileRows;

    for (int row = 0; row < m_files.size(); ++row) {
        const WatermarkFileInfo& info = m_files[row];
        if (info.isHeader) {
            continue;
        }

        const bool keepOutput = info.status == "complete" || info.status == "uploaded";
        if (!keepOutput && !info.outputPath.trimmed().isEmpty() && QFileInfo::exists(info.outputPath)) {
            const QString cleaned = QDir::cleanPath(info.outputPath);
            appendUniqueCandidate(fileCandidates, seenFiles, cleaned,
                QString("%1 row partial output").arg(info.status.isEmpty() ? "unfinished" : info.status),
                false);
            fileRows[cleaned].append(row);
        }

        if (!keepOutput) {
            const QString expectedDir = expectedMemberOutputDir(info);
            if (!expectedDir.isEmpty() && QFileInfo(expectedDir).isDir()) {
                appendUniqueCandidate(dirCandidates, seenDirs, expectedDir,
                    "empty member output folder", true);
            }
        }

        if (!keepOutput && !info.outputPath.trimmed().isEmpty()) {
            const QFileInfo outputInfo(info.outputPath);
            const QString parentDir = outputInfo.absolutePath();
            if (QFileInfo(parentDir).isDir()) {
                appendUniqueCandidate(dirCandidates, seenDirs, parentDir,
                    "empty output folder after partial file cleanup", true);
            }
        }
    }

    QSet<QString> fileCandidateSet;
    for (const CleanupCandidate& candidate : fileCandidates) {
        fileCandidateSet.insert(QDir::cleanPath(candidate.path));
    }

    auto dirContainsOnlyCleanupFiles = [&fileCandidateSet](const QString& dirPath) {
        QFileInfo dirInfo(dirPath);
        if (!dirInfo.isDir()) {
            return false;
        }
        QDirIterator it(dirPath,
            QDir::AllEntries | QDir::NoDotAndDotDot,
            QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QFileInfo item = it.fileInfo();
            if (item.isDir()) {
                continue;
            }
            if (!fileCandidateSet.contains(QDir::cleanPath(item.absoluteFilePath()))) {
                return false;
            }
        }
        return true;
    };

    QList<CleanupCandidate> safeDirCandidates;
    QSet<QString> safeDirSeen;
    for (const CleanupCandidate& candidate : dirCandidates) {
        if (dirContainsOnlyCleanupFiles(candidate.path)) {
            appendUniqueCandidate(safeDirCandidates, safeDirSeen,
                candidate.path, candidate.reason, true);
        }
    }
    dirCandidates = safeDirCandidates;

    if (fileCandidates.isEmpty() && dirCandidates.isEmpty()) {
        QMessageBox::information(this, "No Cleanup Needed",
            "No safe local watermark cleanup candidates were found for the loaded job state.");
        return;
    }

    auto previewLines = [](const QList<CleanupCandidate>& candidates, int maxLines) {
        QStringList lines;
        for (int i = 0; i < candidates.size() && i < maxLines; ++i) {
            const CleanupCandidate& candidate = candidates[i];
            lines.append(QString("%1 %2\n   %3")
                .arg(candidate.directory ? "[folder]" : "[file]",
                     candidate.path,
                     candidate.reason));
        }
        if (candidates.size() > maxLines) {
            lines.append(QString("...and %1 more").arg(candidates.size() - maxLines));
        }
        return lines;
    };

    QStringList preview;
    if (!fileCandidates.isEmpty()) {
        preview << QString("Files to delete (%1):").arg(fileCandidates.size());
        preview << previewLines(fileCandidates, 20);
    }
    if (!dirCandidates.isEmpty()) {
        if (!preview.isEmpty()) {
            preview << "";
        }
        preview << QString("Empty folders to remove (%1):").arg(dirCandidates.size());
        preview << previewLines(dirCandidates, 20);
    }

    if (QMessageBox::warning(this, "Preview Watermark Cleanup",
            QString("Cleanup will delete only the local files/folders listed below. "
                    "Completed outputs are kept, and MEGA cloud folders are not deleted in this pass.\n\n%1\n\nContinue?")
                .arg(preview.join("\n")),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    QJsonObject startedDetails;
    startedDetails["plannedFiles"] = fileCandidates.size();
    startedDetails["plannedDirs"] = dirCandidates.size();
    LogManager::instance().logWithContext(
        LogLevel::Info,
        LogCategory::Watermark,
        "cleanup.started",
        QString("Watermark cleanup started for job %1").arg(jobId).toStdString(),
        "",
        "",
        jobId.toStdString(),
        QString::fromUtf8(QJsonDocument(startedDetails).toJson(QJsonDocument::Compact)).toStdString());

    int removedFiles = 0;
    int failedFiles = 0;
    QStringList removedFilePaths;
    QStringList failedFilePaths;

    for (const CleanupCandidate& candidate : fileCandidates) {
        QFileInfo info(candidate.path);
        if (!info.exists()) {
            continue;
        }
        if (QFile::remove(candidate.path)) {
            removedFiles++;
            removedFilePaths.append(candidate.path);
            const QList<int> rows = fileRows.value(QDir::cleanPath(candidate.path));
            for (int row : rows) {
                if (row >= 0 && row < m_files.size()) {
                    m_files[row].outputPath.clear();
                    if (m_files[row].status == "error" || m_files[row].status == "processing") {
                        m_files[row].error = "Partial output cleaned. Resume will recreate this row.";
                        updateSingleRow(row);
                    }
                }
            }
        } else {
            failedFiles++;
            failedFilePaths.append(candidate.path);
        }
    }

    auto removeEmptyDirAndChildren = [](const QString& path, QStringList* removed, QStringList* failed) {
        QFileInfo rootInfo(path);
        if (!rootInfo.isDir()) {
            return;
        }

        QStringList dirs;
        QDirIterator it(path,
            QDir::Dirs | QDir::NoDotAndDotDot,
            QDirIterator::Subdirectories);
        while (it.hasNext()) {
            dirs.append(QDir::cleanPath(it.next()));
        }
        std::sort(dirs.begin(), dirs.end(), [](const QString& a, const QString& b) {
            return a.count('/') > b.count('/');
        });
        dirs.append(QDir::cleanPath(path));

        for (const QString& dirPath : dirs) {
            QFileInfo dirInfo(dirPath);
            if (!dirInfo.isDir()) {
                continue;
            }

            QDir dir(dirPath);
            const bool empty = dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty();
            if (!empty) {
                continue;
            }

            QDir parent(dirInfo.absolutePath());
            if (parent.rmdir(dirInfo.fileName())) {
                removed->append(dirPath);
            } else {
                failed->append(dirPath);
            }
        }
    };

    QStringList removedDirPaths;
    QStringList failedDirPaths;
    std::sort(dirCandidates.begin(), dirCandidates.end(), [](const CleanupCandidate& a, const CleanupCandidate& b) {
        return a.path.count('/') > b.path.count('/');
    });
    for (const CleanupCandidate& candidate : dirCandidates) {
        removeEmptyDirAndChildren(candidate.path, &removedDirPaths, &failedDirPaths);
    }
    removedDirPaths.removeDuplicates();
    failedDirPaths.removeDuplicates();

    updateStats();
    updateButtonStates();

    OperationJobRecord latestRecord = OperationJobStore::instance().job(jobId);
    QJsonObject metadata = latestRecord.metadata;
    QJsonArray cleanupRuns = metadata["cleanupRuns"].toArray();
    QJsonObject cleanupRun;
    cleanupRun["at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    cleanupRun["removedFiles"] = removedFiles;
    cleanupRun["removedDirs"] = removedDirPaths.size();
    cleanupRun["failedFiles"] = failedFiles;
    cleanupRun["failedDirs"] = failedDirPaths.size();
    cleanupRun["scope"] = "local_watermark_artifacts";

    auto appendPathArray = [](const QStringList& paths) {
        QJsonArray array;
        const int maxPaths = qMin(paths.size(), 100);
        for (int i = 0; i < maxPaths; ++i) {
            array.append(paths[i]);
        }
        if (paths.size() > maxPaths) {
            array.append(QString("...and %1 more").arg(paths.size() - maxPaths));
        }
        return array;
    };

    cleanupRun["removedFilePaths"] = appendPathArray(removedFilePaths);
    cleanupRun["removedDirPaths"] = appendPathArray(removedDirPaths);
    cleanupRun["failedFilePaths"] = appendPathArray(failedFilePaths);
    cleanupRun["failedDirPaths"] = appendPathArray(failedDirPaths);
    cleanupRuns.append(cleanupRun);
    metadata["cleanupRuns"] = cleanupRuns;
    metadata["lastCleanupAt"] = cleanupRun["at"];
    metadata["lastCleanupRemovedFiles"] = removedFiles;
    metadata["lastCleanupRemovedDirs"] = removedDirPaths.size();
    metadata["lastCleanupFailedFiles"] = failedFiles;
    metadata["lastCleanupFailedDirs"] = failedDirPaths.size();

    OperationJobStore::instance().createJob(
        OperationJobType::Watermark,
        latestRecord.title,
        latestRecord.plannedCount,
        metadata,
        jobId);
    saveWatermarkCheckpoint("cleanup_applied", jobId);

    const int cleanupFailures = failedFiles + failedDirPaths.size();
    const QString summary = QString("Cleanup removed %1 partial file(s) and %2 empty folder(s)%3")
        .arg(removedFiles)
        .arg(removedDirPaths.size())
        .arg(cleanupFailures > 0
            ? QString("; %1 item(s) failed").arg(cleanupFailures)
            : QString());

    QJsonObject completedDetails = cleanupRun;
    completedDetails["summary"] = summary;
    LogManager::instance().logWithContext(
        cleanupFailures > 0 ? LogLevel::Error : LogLevel::Info,
        LogCategory::Watermark,
        cleanupFailures > 0 ? "cleanup.failed" : "cleanup.completed",
        summary.toStdString(),
        "",
        "",
        jobId.toStdString(),
        QString::fromUtf8(QJsonDocument(completedDetails).toJson(QJsonDocument::Compact)).toStdString());

    if (cleanupFailures > 0) {
        OperationJobStore::instance().markCleanupRequired(jobId, summary);
    } else if (latestRecord.status == OperationJobStatus::Paused) {
        OperationJobStore::instance().markPaused(jobId, summary + ". Resume when ready.");
    }

    m_statusLabel->setText(summary);
    QMessageBox::information(this, "Cleanup Complete", summary);
}

void WatermarkPanel::loadMembers() {
    // Preserve current selections
    QSet<QString> previouslyChecked;
    for (int i = 0; i < m_memberListWidget->count(); ++i) {
        QListWidgetItem* item = m_memberListWidget->item(i);
        if (item->checkState() == Qt::Checked) {
            previouslyChecked.insert(item->data(Qt::UserRole).toString());
        }
    }

    m_memberListWidget->blockSignals(true);
    m_memberListWidget->clear();

    // Add groups
    QStringList groupNames = m_registry->getGroupNames();
    for (const QString& name : groupNames) {
        int count = m_registry->getGroupMemberIds(name).size();
        QListWidgetItem* item = new QListWidgetItem(
            QString("[Group] %1 (%2 members)").arg(name).arg(count));
        item->setData(Qt::UserRole, "GROUP:" + name);
        item->setCheckState(previouslyChecked.contains("GROUP:" + name) ? Qt::Checked : Qt::Unchecked);
        item->setForeground(ThemeManager::instance().supportInfo());
        m_memberListWidget->addItem(item);
    }

    // Add individual members
    QList<MemberInfo> members = m_registry->getActiveMembers();
    for (const MemberInfo& m : members) {
        QString label = QString("%1 (%2)").arg(m.displayName).arg(m.id);
        // Show email/ip hints
        QStringList hints;
        if (!m.email.isEmpty()) hints << m.email;
        if (!m.ipAddress.isEmpty()) hints << "IP:" + m.ipAddress;
        if (!hints.isEmpty()) label += " — " + hints.join(", ");

        QListWidgetItem* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, m.id);
        item->setCheckState(previouslyChecked.contains(m.id) ? Qt::Checked : Qt::Unchecked);
        m_memberListWidget->addItem(item);
    }
    m_memberListWidget->blockSignals(false);

    // Connect itemChanged for selection tracking (disconnect first to avoid duplicates)
    disconnect(m_memberListWidget, &QListWidget::itemChanged, nullptr, nullptr);
    connect(m_memberListWidget, &QListWidget::itemChanged,
            this, &WatermarkPanel::onMemberSelectionChanged);

    // Populate group quick-select combo
    m_groupQuickSelectCombo->blockSignals(true);
    m_groupQuickSelectCombo->clear();
    m_groupQuickSelectCombo->addItem("-- Add Group --", "");
    for (const QString& name : groupNames) {
        int count = m_registry->getGroupMemberIds(name).size();
        m_groupQuickSelectCombo->addItem(
            QString("%1 (%2)").arg(name).arg(count), name);
    }
    m_groupQuickSelectCombo->blockSignals(false);

    onMemberSelectionChanged();
}

void WatermarkPanel::onAddFiles() {
    QStringList files = QFileDialog::getOpenFileNames(this,
        "Select Files to Watermark",
        QString(),
        "Supported Files (*.mp4 *.mkv *.avi *.mov *.wmv *.flv *.webm *.m4v *.mpeg *.mpg *.pdf *.mp3 *.flac *.wav *.aac *.ogg *.m4a);;Videos (*.mp4 *.mkv *.avi *.mov *.wmv *.flv *.webm *.m4v *.mpeg *.mpg);;PDFs (*.pdf);;Audio (*.mp3 *.flac *.wav *.aac *.ogg *.m4a);;All Files (*)");

    for (const QString& file : files) {
        // Skip already-watermarked files (e.g., *_wm.mp4)
        QFileInfo checkFi(file);
        if (checkFi.completeBaseName().endsWith("_wm")) continue;

        // Check if already in list
        bool exists = false;
        for (const WatermarkFileInfo& info : m_files) {
            if (info.filePath == file) {
                exists = true;
                break;
            }
        }
        if (exists) continue;

        QFileInfo fi(file);
        WatermarkFileInfo info;
        info.filePath = file;
        info.fileName = fi.fileName();
        info.fileSize = fi.size();
        info.status = "pending";

        QString ext = fi.suffix().toLower();
        static const QSet<QString> videoExts = {
            "mp4", "mkv", "avi", "mov", "wmv", "flv", "webm", "m4v", "mpeg", "mpg"
        };
        static const QSet<QString> audioExts = {"mp3", "flac", "wav", "aac", "ogg", "m4a", "wma", "opus"};

        if (ext == "pdf") {
            info.fileType = "pdf";
        } else if (audioExts.contains(ext)) {
            info.fileType = "audio";
        } else if (videoExts.contains(ext)) {
            info.fileType = "video";
        } else {
            info.fileType = "passthrough"; // .vtt, .docx, .txt, etc. — copy as-is
        }

        m_files.append(info);
    }

    populateTable();
    updateStats();
    updateButtonStates();
}

void WatermarkPanel::onAddFolder() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Folder to Watermark");
    if (dir.isEmpty()) return;

    // Store root directory for subfolder structure preservation
    m_sourceRootDir = dir;

    // Scan ALL files — watermarkable ones get processed, others get copied as-is
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QString file = it.next();

        // Skip already-watermarked files (e.g., *_wm.mp4)
        QFileInfo checkFi(file);
        if (checkFi.completeBaseName().endsWith("_wm")) continue;

        // Check if already in list
        bool exists = false;
        for (const WatermarkFileInfo& info : m_files) {
            if (info.filePath == file) {
                exists = true;
                break;
            }
        }
        if (exists) continue;

        QFileInfo fi(file);
        WatermarkFileInfo info;
        info.filePath = file;
        info.fileName = fi.fileName();
        info.fileSize = fi.size();
        info.status = "pending";

        QString ext = fi.suffix().toLower();
        static const QSet<QString> videoExts = {
            "mp4", "mkv", "avi", "mov", "wmv", "flv", "webm", "m4v", "mpeg", "mpg"
        };
        static const QSet<QString> audioExts = {"mp3", "flac", "wav", "aac", "ogg", "m4a", "wma", "opus"};

        if (ext == "pdf") {
            info.fileType = "pdf";
        } else if (audioExts.contains(ext)) {
            info.fileType = "audio";
        } else if (videoExts.contains(ext)) {
            info.fileType = "video";
        } else {
            info.fileType = "passthrough"; // .vtt, .docx, .txt, etc. — copy as-is
        }

        m_files.append(info);
    }

    populateTable();
    updateStats();
    updateButtonStates();
}

void WatermarkPanel::onRemoveSelected() {
    QList<int> selectedRows;
    for (const QModelIndex& index : m_fileTable->selectionModel()->selectedRows()) {
        selectedRows.append(index.row());
    }

    // Sort in reverse order to remove from end first
    std::sort(selectedRows.begin(), selectedRows.end(), std::greater<int>());

    for (int row : selectedRows) {
        if (row >= 0 && row < m_files.size()) {
            m_files.removeAt(row);
        }
    }

    populateTable();
    updateStats();
    updateButtonStates();
}

void WatermarkPanel::onClearAll() {
    m_files.clear();
    m_pausedForDiskSpace = false;
    m_diskSpacePauseMessage.clear();
    m_pausedJobId.clear();
    m_currentJobId.clear();
    populateTable();
    updateStats();
    updateButtonStates();
}

void WatermarkPanel::onBrowseOutput() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Output Directory");
    if (!dir.isEmpty()) {
        m_outputDirEdit->setText(dir);
    }
}

void WatermarkPanel::onStartWatermark() {
    if (m_isRunning) return;

    if (m_pausedForDiskSpace) {
        onResumePausedWatermark();
        return;
    }

    if (m_files.isEmpty()) {
        QMessageBox::warning(this, "No Files", "Please add files to watermark.");
        return;
    }

    // Validate mode
    if (m_modeCombo->currentData().toString() == "global") {
        if (m_primaryTextEdit->text().isEmpty()) {
            QMessageBox::warning(this, "Missing Text", "Please enter primary watermark text.");
            return;
        }
    } else {
        QStringList selectedIds = getSelectedMemberIds();
        if (selectedIds.isEmpty()) {
            QMessageBox::warning(this, "No Members",
                "Please select at least one member or group.");
            return;
        }
        if (m_primaryTextEdit->text().isEmpty()) {
            QMessageBox::warning(this, "Missing Text",
                "Please enter primary watermark text.\n\n"
                "Use template variables like {brand}, {member_name}, {member_id} "
                "to personalize the watermark per member.");
            return;
        }

        // Check for members missing fields that are used in the template
        QString combined = m_primaryTextEdit->text() + " " + m_secondaryTextEdit->text();
        QStringList missingWarnings;
        for (const QString& memberId : selectedIds) {
            MemberInfo member = m_registry->getMember(memberId);
            QStringList emptyFields;
            if (combined.contains("{member_email}") && member.email.isEmpty())
                emptyFields << "email";
            if (combined.contains("{member_ip}") && member.ipAddress.isEmpty())
                emptyFields << "IP";
            if (combined.contains("{member_mac}") && member.macAddress.isEmpty())
                emptyFields << "MAC";
            if (combined.contains("{member_social}") && member.socialHandle.isEmpty())
                emptyFields << "social";
            if (combined.contains("{member_name}") && member.displayName.isEmpty())
                emptyFields << "name";
            if (!emptyFields.isEmpty()) {
                missingWarnings << QString("%1: missing %2")
                    .arg(member.id.isEmpty() ? memberId : member.id)
                    .arg(emptyFields.join(", "));
            }
        }
        if (!missingWarnings.isEmpty()) {
            QString msg = QString("The following members are missing fields used in your watermark template:\n\n%1\n\n"
                "These fields will appear blank in their watermarks. Continue anyway?")
                .arg(missingWarnings.join("\n"));
            if (QMessageBox::warning(this, "Missing Member Fields", msg,
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
                return;
            }
        }
    }

    // Collect unique file paths (deduplicate in case m_files was expanded from a prior run)
    QStringList filePaths;
    QSet<QString> seenPaths;
    QList<WatermarkFileInfo> baseFiles;
    for (const WatermarkFileInfo& info : m_files) {
        if (!seenPaths.contains(info.filePath)) {
            seenPaths.insert(info.filePath);
            filePaths.append(info.filePath);
            WatermarkFileInfo base = info;
            base.memberName.clear();
            base.memberId.clear();
            baseFiles.append(base);
        }
    }

    // Build config
    WatermarkConfig config = buildConfig();

    // Get output directory
    QString outputDir;
    if (!m_sameAsInputCheck->isChecked() && !m_outputDirEdit->text().isEmpty()) {
        outputDir = m_outputDirEdit->text();
    }

    // Get member ID(s) (if per-member mode)
    QString memberId;
    QStringList allMemberIds;
    if (m_modeCombo->currentData().toString() == "member") {
        allMemberIds = getSelectedMemberIds();
        // Optimize: single member uses the single-member code path
        if (allMemberIds.size() == 1) {
            memberId = allMemberIds.first();
            allMemberIds.clear();
        }
    }

    // Rebuild m_files for display: expand to one row per file×member in multi-member mode
    // Order: member-first (matches worker processing order)
    m_workerIdxToRow.clear();
    if (!allMemberIds.isEmpty()) {
        QList<WatermarkFileInfo> expanded;
        expanded.reserve(allMemberIds.size() * (baseFiles.size() + 1)); // +1 per member for header
        int workerIdx = 0;
        for (const QString& mid : allMemberIds) {
            MemberInfo mi = m_registry->getMember(mid);
            QString displayName = mi.displayName.isEmpty() ? mid : mi.displayName;

            // Insert member header row
            WatermarkFileInfo header;
            header.isHeader = true;
            header.memberId = mid;
            header.memberName = displayName;
            header.fileName = QString::fromUtf8("\u25b8 %1 \u2014 Pending (%2 files)")
                .arg(displayName).arg(baseFiles.size());
            header.status = "pending";
            expanded.append(header);

            for (const WatermarkFileInfo& base : baseFiles) {
                WatermarkFileInfo entry = base;
                entry.memberId = mid;
                entry.memberName = displayName;
                entry.status = "pending";
                entry.outputPath.clear();
                entry.error.clear();
                entry.progressPercent = 0;
                m_workerIdxToRow[workerIdx] = expanded.size();
                expanded.append(entry);
                workerIdx++;
            }
        }
        m_files = expanded;
    } else {
        // Global or single-member: one row per file
        m_files = baseFiles;
        if (!memberId.isEmpty()) {
            MemberInfo mi = m_registry->getMember(memberId);
            QString name = mi.displayName.isEmpty() ? memberId : mi.displayName;
            for (auto& f : m_files) {
                f.memberId = memberId;
                f.memberName = name;
            }
        }
        for (auto& f : m_files) {
            f.status = "pending";
            f.outputPath.clear();
            f.error.clear();
            f.progressPercent = 0;
        }
    }
    populateTable();

    // === Smart Engine: Pre-flight disk space check ===
    if (!allMemberIds.isEmpty() && m_metricsStore) {
        qint64 totalInputSize = 0;
        for (const auto& fi : baseFiles) totalInputSize += fi.fileSize;

        QString primaryExt = QFileInfo(baseFiles.first().filePath).suffix().toLower();
        qint64 estimatedPerMember = m_metricsStore->predictOutputSize(primaryExt, totalInputSize);

        QString checkPath = outputDir.isEmpty()
            ? QFileInfo(baseFiles.first().filePath).path() : outputDir;
        QStorageInfo storage(checkPath);
        qint64 available = storage.bytesAvailable();
        qint64 totalEstimate = estimatedPerMember * allMemberIds.size();

        double confidence = m_metricsStore->predictionConfidence(primaryExt);
        QString confLabel = confidence > 0.7 ? "high" : confidence > 0.3 ? "medium" : "low";

        if (m_autoUploadCheck->isChecked()) {
            // Auto-upload mode: only need space for ONE member batch
            if (available < estimatedPerMember) {
                QMessageBox::warning(this, "Insufficient Disk Space",
                    QString("Not enough space for even one member's batch.\n\n"
                            "Available: %1\n"
                            "Estimated per member: %2 (%3 confidence)\n\n"
                            "Free up disk space before starting.")
                        .arg(formatFileSize(available))
                        .arg(formatFileSize(estimatedPerMember))
                        .arg(confLabel));
                return;
            }
        } else {
            // Normal mode: warn if not enough for all members
            if (available < totalEstimate) {
                auto reply = QMessageBox::question(this, "Disk Space Warning",
                    QString("Estimated output: %1 for %2 members\n"
                            "Available: %3 (%4 confidence)\n\n"
                            "Enable auto-upload mode to save disk space?")
                        .arg(formatFileSize(totalEstimate))
                        .arg(allMemberIds.size())
                        .arg(formatFileSize(available))
                        .arg(confLabel),
                    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

                if (reply == QMessageBox::Yes) {
                    m_autoUploadCheck->setChecked(true);
                } else if (reply == QMessageBox::Cancel) {
                    return;
                }
            }
        }
    }

    // Pre-flight: validate auto-upload can work
    if (m_autoUploadCheck->isChecked()) {
        if (!m_megaApi) {
            QMessageBox::warning(this, "Not Connected",
                "Auto-upload is enabled, but the MEGA connection is not available.\n\n"
                "Log in first, or disable auto-upload and use the manual Distribution handoff.");
            return;
        }

        bool useCustomPath = m_customPathCheck->isChecked() && !m_customPathEdit->text().isEmpty();
        if (useCustomPath) {
            // Custom path mode — no need to check member folders
        } else if (m_customPathCheck->isChecked() && m_customPathEdit->text().isEmpty()) {
            QMessageBox::warning(this, "Missing Path",
                "Custom upload path is enabled but no path specified.\n"
                "Enter a MEGA cloud path or browse for one.");
            return;
        } else {
            // Standard mode — check that members have distribution folders
            QStringList checkIds = allMemberIds.isEmpty() ? QStringList{memberId} : allMemberIds;
            QStringList membersWithoutFolder;
            for (const QString& mid : checkIds) {
                if (mid.isEmpty()) continue;
                MemberInfo mi = m_registry->getMember(mid);
                if (mi.distributionFolder.isEmpty()) {
                    membersWithoutFolder << (mi.displayName.isEmpty() ? mid : mi.displayName);
                }
            }
            if (!membersWithoutFolder.isEmpty()) {
                QMessageBox::warning(this, "Missing Distribution Folders",
                    QString("Auto-upload is enabled but these members have no distribution folder:\n\n%1\n\n"
                            "Configure folders in the Members panel, or enable 'Upload to custom path'.")
                        .arg(membersWithoutFolder.join("\n")));
                return;
            }
        }
    }

    int plannedCount = 0;
    for (const WatermarkFileInfo& info : m_files) {
        if (!info.isHeader) {
            ++plannedCount;
        }
    }

    QStringList selectedMemberIdsForMetadata;
    if (m_modeCombo->currentData().toString() == "member") {
        selectedMemberIdsForMetadata = allMemberIds;
        if (selectedMemberIdsForMetadata.isEmpty() && !memberId.isEmpty()) {
            selectedMemberIdsForMetadata.append(memberId);
        }
    }

    QJsonArray filePathArray;
    for (const QString& path : filePaths) {
        filePathArray.append(path);
    }
    QJsonArray memberIdArray;
    for (const QString& id : selectedMemberIdsForMetadata) {
        memberIdArray.append(id);
    }

    QJsonObject metadata;
    metadata["mode"] = m_modeCombo->currentData().toString();
    metadata["sourceFileCount"] = filePaths.size();
    metadata["plannedOutputCount"] = plannedCount;
    metadata["memberCount"] = allMemberIds.isEmpty() ? (memberId.isEmpty() ? 0 : 1) : allMemberIds.size();
    metadata["outputDir"] = outputDir.isEmpty() ? "same_as_input" : outputDir;
    metadata["outputDirRaw"] = m_outputDirEdit->text();
    metadata["sameAsInput"] = m_sameAsInputCheck->isChecked();
    metadata["filePaths"] = filePathArray;
    metadata["selectedMemberIds"] = memberIdArray;
    metadata["primaryText"] = m_primaryTextEdit->text();
    metadata["secondaryText"] = m_secondaryTextEdit->text();
    metadata["preset"] = m_presetCombo->currentText();
    metadata["crf"] = m_crfSpin->value();
    metadata["intervalSeconds"] = m_intervalSpin->value();
    metadata["durationSeconds"] = m_durationSpin->value();
    metadata["fastSegmentedEncode"] = m_fastSegmentedCheck && m_fastSegmentedCheck->isChecked();
    metadata["embedMetadata"] = m_embedMetadataCheck->isChecked();
    metadata["metadataTitle"] = m_metaTitleEdit->text();
    metadata["metadataAuthor"] = m_metaAuthorEdit->text();
    metadata["metadataComment"] = m_metaCommentEdit->text();
    metadata["metadataKeywords"] = m_metaKeywordsEdit->text();
    metadata["autoUpload"] = m_autoUploadCheck && m_autoUploadCheck->isChecked();
    metadata["customPathEnabled"] = m_customPathCheck && m_customPathCheck->isChecked();
    metadata["customUploadPath"] = m_customPathEdit ? m_customPathEdit->text() : QString();
    metadata["sourceRootDir"] = m_sourceRootDir;
    if (!m_retrySourceJobId.isEmpty()) {
        metadata["retryOfJobId"] = m_retrySourceJobId;
    }
    m_currentJobId = OperationJobStore::instance().createJob(
        OperationJobType::Watermark,
        QString("Watermark %1 %2").arg(plannedCount).arg(plannedCount == 1 ? "file" : "files"),
        plannedCount,
        metadata);
    for (WatermarkFileInfo& info : m_files) {
        info.jobId = m_currentJobId;
    }
    saveWatermarkCheckpoint("created");
    m_retrySourceJobId.clear();
    m_pausedJobId.clear();
    m_currentJobCancelled = false;

    // Create worker thread
    m_workerThread = new QThread();
    m_worker = new WatermarkWorker();
    m_worker->moveToThread(m_workerThread);
    m_pausedForDiskSpace = false;
    m_diskSpacePauseMessage.clear();

    m_worker->setFiles(filePaths);
    m_worker->setOutputDir(outputDir);
    m_worker->setConfig(config);
    m_worker->setMemberId(memberId);
    m_worker->setMemberIds(allMemberIds);
    m_worker->setRawTemplates(m_primaryTextEdit->text(), m_secondaryTextEdit->text());
    m_worker->setRootDir(m_sourceRootDir);

    // Smart Engine: pass auto-upload and metrics to worker
    const bool autoUploadActive = m_autoUploadCheck->isChecked() && m_megaApi;
    if (autoUploadActive) {
        m_worker->setAutoUpload(true, m_megaApi);
        if (m_customPathCheck->isChecked() && !m_customPathEdit->text().isEmpty()) {
            m_worker->setCustomUploadPath(m_customPathEdit->text());
        }
    }
    if (m_metricsStore) {
        m_worker->setMetricsStore(m_metricsStore);
    }

    connect(m_workerThread, &QThread::started, m_worker, &WatermarkWorker::process);
    connect(m_worker, &WatermarkWorker::progress, this, &WatermarkPanel::onWorkerProgress);
    connect(m_worker, &WatermarkWorker::fileCompleted, this, &WatermarkPanel::onWorkerFileCompleted);
    connect(m_worker, &WatermarkWorker::finished, this, &WatermarkPanel::onWorkerFinished);
    connect(m_worker, &WatermarkWorker::finishedWithMapping, this,
            [this, autoUploadActive](int, int, const QMap<QString, QStringList>& memberFileMap) {
                if (m_pausedForDiskSpace) {
                    return;
                }
                if (!memberFileMap.isEmpty()) {
                    if (autoUploadActive) {
                        // Auto-upload handled everything — files already uploaded & deleted
                        m_lastMemberFileMap.clear();
                        m_sendToDistBtn->setEnabled(false);
                        m_statusLabel->setText(QString("Auto-upload complete for %1 member%2.")
                            .arg(memberFileMap.size())
                            .arg(memberFileMap.size() == 1 ? "" : "s"));
                    } else {
                        // Manual mode — files still on disk, offer distribution
                        m_lastMemberFileMap = memberFileMap;
                        m_sendToDistBtn->setEnabled(true);
                        m_statusLabel->setText(QString("Watermarked files for %1 member%2. Click 'Send to Distribution' to upload.")
                            .arg(memberFileMap.size())
                            .arg(memberFileMap.size() == 1 ? "" : "s"));
                    }
                }
            });

    // Smart Engine: auto-upload progress signals — update both status label and member header
    connect(m_worker, &WatermarkWorker::memberBatchUploading, this,
        [this](const QString& memberId, int fileIdx, int totalFiles, const QString& fileName) {
            m_statusLabel->setText(QString("Uploading %1 to %2 (%3/%4)...")
                .arg(fileName).arg(memberId).arg(fileIdx).arg(totalFiles));
            int hdr = findMemberHeaderRow(memberId);
            if (hdr >= 0) {
                m_files[hdr].status = "uploading";
                updateMemberHeader(hdr);
            }
        });
    connect(m_worker, &WatermarkWorker::memberBatchCleanedUp, this,
        [this](const QString& memberId, int uploaded, int failed, int deleted) {
            qDebug() << "Smart pipeline:" << memberId
                     << "— uploaded:" << uploaded << "failed:" << failed << "cleaned:" << deleted;
            int hdr = findMemberHeaderRow(memberId);
            if (hdr >= 0) {
                if (failed > 0) {
                    m_files[hdr].status = "error";
                    m_files[hdr].error = QString("%1 upload(s) failed").arg(failed);
                    updateMemberHeader(hdr);
                } else {
                    markMemberRowsUploaded(
                        memberId,
                        QString("Uploaded to MEGA (%1 file(s)); cleaned up %2 local file(s)")
                            .arg(uploaded)
                            .arg(deleted));
                }
            }
        });
    connect(m_worker, &WatermarkWorker::diskSpaceWarning, this,
        [this](qint64 available, qint64 needed) {
            qWarning() << "Low disk space! Available:" << available << "Needed:" << needed;
            m_pausedForDiskSpace = true;
            m_diskSpacePauseMessage = QString("Paused: disk is full (%1 free, %2 needed). Free space and start again.")
                .arg(formatFileSize(available))
                .arg(formatFileSize(needed));
            m_statusLabel->setText(m_diskSpacePauseMessage);
            OperationJobStore::instance().markPaused(m_currentJobId, m_diskSpacePauseMessage);
        });
    connect(m_worker, &WatermarkWorker::memberAutoUploadSkipped, this,
        [this](const QString& memberId, const QString& reason) {
            qWarning() << "Auto-upload skipped for" << memberId << ":" << reason;
            m_statusLabel->setText(QString("Skipped upload for %1: %2").arg(memberId).arg(reason));
            int hdr = findMemberHeaderRow(memberId);
            if (hdr >= 0) {
                m_files[hdr].status = "error";
                m_files[hdr].error = reason;
                updateMemberHeader(hdr);
            }
        });

    connect(m_worker, &WatermarkWorker::finished, m_workerThread, &QThread::quit);
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater);
    connect(m_workerThread, &QThread::finished, this, [this]() {
        m_workerThread = nullptr;
        m_worker = nullptr;
    });

    m_isRunning = true;
    m_lastMemberFileMap.clear();  // Clear previous session's data
    m_sendToDistBtn->setEnabled(false);
    updateButtonStates();
    m_progressBar->setValue(0);
    m_statusLabel->setText("Starting...");

    emit watermarkStarted();
    OperationJobStore::instance().markRunning(
        m_currentJobId,
        QString("Watermarking %1 %2")
            .arg(plannedCount)
            .arg(plannedCount == 1 ? "file" : "files"));
    m_workerThread->start();
}

void WatermarkPanel::onResumePausedWatermark() {
    if (m_isRunning) {
        return;
    }

    if (m_files.isEmpty()) {
        QMessageBox::warning(this, "No Files", "No paused watermark rows are available to resume.");
        return;
    }

    QList<WatermarkResumeTask> tasks;
    QStringList missingSources;
    QStringList removedPartialOutputs;
    int watermarkTaskCount = 0;
    int existingOutputTaskCount = 0;
    int skippedCompleteCount = 0;
    int skippedExternalMemberCount = 0;
    int rebuildMemberCount = 0;

    QMap<QString, QList<int>> memberRows;
    for (int row = 0; row < m_files.size(); ++row) {
        const WatermarkFileInfo& info = m_files[row];
        if (!info.isHeader && !info.memberId.isEmpty()) {
            memberRows[info.memberId].append(row);
        }
    }

    QSet<QString> suspectIncompleteMembers;
    QStringList suspectMemberDescriptions;
    for (auto it = memberRows.constBegin(); it != memberRows.constEnd(); ++it) {
        const QString& memberId = it.key();
        const int headerRow = findMemberHeaderRow(memberId);
        if (headerRow >= 0 && m_files[headerRow].status == "uploaded") {
            continue;
        }

        int missingCompletedOutputs = 0;
        int existingCompletedOutputs = 0;
        int unfinishedRows = 0;
        QString memberName;

        for (int row : it.value()) {
            const WatermarkFileInfo& info = m_files[row];
            if (memberName.isEmpty()) {
                memberName = info.memberName.isEmpty() ? memberId : info.memberName;
            }

            const bool complete = info.status == "complete" || info.status == "uploaded";
            if (!complete) {
                ++unfinishedRows;
                continue;
            }

            const bool hasLocalOutput = !info.outputPath.trimmed().isEmpty()
                && QFileInfo::exists(info.outputPath);
            if (hasLocalOutput) {
                ++existingCompletedOutputs;
            } else {
                ++missingCompletedOutputs;
            }
        }

        if (missingCompletedOutputs > 0 && unfinishedRows > 0) {
            suspectIncompleteMembers.insert(memberId);
            suspectMemberDescriptions << QString("%1: %2 completed output(s) missing, %3 local output(s) still present, %4 unfinished row(s)")
                .arg(memberName.isEmpty() ? memberId : memberName)
                .arg(missingCompletedOutputs)
                .arg(existingCompletedOutputs)
                .arg(unfinishedRows);
        }
    }

    QSet<QString> skipExternalMembers;
    QSet<QString> rebuildMembers;
    if (!suspectIncompleteMembers.isEmpty()) {
        QString preview = suspectMemberDescriptions.mid(0, 12).join("\n");
        if (suspectMemberDescriptions.size() > 12) {
            preview += QString("\n... and %1 more member(s)")
                .arg(suspectMemberDescriptions.size() - 12);
        }

        QMessageBox dialog(this);
        dialog.setIcon(QMessageBox::Warning);
        dialog.setWindowTitle("Resume Member Batches");
        dialog.setText(
            "Some member batches have completed rows whose local output files are missing while other rows are still unfinished.");
        dialog.setInformativeText(
            "This usually means those member folders were manually uploaded, removed, or cleaned up outside the app to free disk space.\n\n"
            "Skipping marks the whole member batch as already handled and prevents the app from recreating that member folder with only leftover files.\n"
            "Rebuilding retries every source file for those members so the batch is complete again.");
        dialog.setDetailedText(preview);
        QPushButton* skipButton = dialog.addButton("Skip these members", QMessageBox::AcceptRole);
        QPushButton* rebuildButton = dialog.addButton("Rebuild these members", QMessageBox::DestructiveRole);
        QPushButton* cancelButton = dialog.addButton(QMessageBox::Cancel);
        dialog.setDefaultButton(skipButton);
        dialog.exec();

        if (dialog.clickedButton() == cancelButton) {
            updateButtonStates();
            return;
        }

        if (dialog.clickedButton() == rebuildButton) {
            rebuildMembers = suspectIncompleteMembers;
            rebuildMemberCount = rebuildMembers.size();
        } else {
            skipExternalMembers = suspectIncompleteMembers;
            skippedExternalMemberCount = skipExternalMembers.size();
        }
    }

    struct ResumeReviewRow {
        QString action;
        QString member;
        QString fileName;
        QString sourcePath;
        QString outputPath;
        QString detail;
        bool warning = false;
        bool blocked = false;
    };

    QList<ResumeReviewRow> reviewRows;
    int plannedWatermarkTasks = 0;
    int plannedExistingOutputs = 0;
    int plannedSkippedRows = 0;
    int plannedPartialRemovals = 0;
    int plannedMissingSources = 0;

    auto addReviewRow = [&reviewRows](const QString& action,
                                      const WatermarkFileInfo& info,
                                      const QString& detail,
                                      bool warning = false,
                                      bool blocked = false) {
        ResumeReviewRow row;
        row.action = action;
        row.member = info.memberName.isEmpty()
            ? (info.memberId.isEmpty() ? QString("Global") : info.memberId)
            : info.memberName;
        row.fileName = info.fileName.isEmpty() ? QFileInfo(info.filePath).fileName() : info.fileName;
        row.sourcePath = info.filePath;
        row.outputPath = info.outputPath;
        row.detail = detail;
        row.warning = warning;
        row.blocked = blocked;
        reviewRows.append(row);
    };

    for (int row = 0; row < m_files.size(); ++row) {
        const WatermarkFileInfo& info = m_files[row];
        if (info.isHeader) {
            continue;
        }

        const int headerRow = findMemberHeaderRow(info.memberId);
        const bool forceRebuildMember = rebuildMembers.contains(info.memberId);
        const bool memberUploaded = !forceRebuildMember
            && ((headerRow >= 0 && m_files[headerRow].status == "uploaded")
                || skipExternalMembers.contains(info.memberId));
        const bool complete = !forceRebuildMember
            && (info.status == "complete" || info.status == "uploaded");

        if (memberUploaded) {
            plannedSkippedRows++;
            addReviewRow("Skip member batch", info,
                skipExternalMembers.contains(info.memberId)
                    ? "Marked as already handled outside the app."
                    : "Member batch is already marked uploaded.");
            continue;
        }

        if (complete) {
            plannedSkippedRows++;
            const bool hasLocalOutput = !info.outputPath.trimmed().isEmpty()
                && QFileInfo::exists(info.outputPath);
            if (hasLocalOutput && !info.memberId.isEmpty()) {
                plannedExistingOutputs++;
                addReviewRow("Carry completed output", info,
                    "Existing output will be kept and included in the resumed member handoff.");
            } else {
                addReviewRow("Skip completed row", info,
                    hasLocalOutput
                        ? "Completed row already has local output."
                        : "Completed row has no local output; it will not be rebuilt unless the member batch is rebuilt.",
                    !hasLocalOutput);
            }
            continue;
        }

        if (!QFileInfo::exists(info.filePath)) {
            plannedMissingSources++;
            addReviewRow("Cannot resume", info,
                "Source file is missing; this row will be marked error and skipped.",
                true,
                true);
            continue;
        }

        const bool hasPartialOutput = !info.outputPath.isEmpty() && QFileInfo::exists(info.outputPath);
        plannedWatermarkTasks++;
        if (hasPartialOutput) {
            plannedPartialRemovals++;
            addReviewRow(forceRebuildMember ? "Remove output and rebuild" : "Remove partial and retry",
                info,
                "The existing output file will be removed only after this review is accepted.",
                true);
        } else {
            addReviewRow(forceRebuildMember ? "Rebuild from source" : "Watermark",
                info,
                forceRebuildMember
                    ? "Member batch is being rebuilt from source."
                    : "Row will be watermarked from the saved source file.");
        }
    }

    auto showResumeReview = [&]() -> bool {
        QDialog dialog(this);
        dialog.setWindowTitle("Review Watermark Resume");
        dialog.resize(1040, 600);

        auto* layout = new QVBoxLayout(&dialog);
        auto* summary = new QLabel(QString(
            "Watermark resume plan: %1 row(s) will watermark, %2 completed output(s) will carry forward, "
            "%3 row(s) will be skipped, %4 partial output(s) will be removed after confirmation, %5 source file(s) are missing.")
            .arg(plannedWatermarkTasks)
            .arg(plannedExistingOutputs)
            .arg(plannedSkippedRows)
            .arg(plannedPartialRemovals)
            .arg(plannedMissingSources));
        summary->setWordWrap(true);
        layout->addWidget(summary);

        auto* table = new QTableWidget(reviewRows.size(), 6, &dialog);
        table->setHorizontalHeaderLabels({"Action", "Member", "File", "Source", "Output", "Detail"});
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->verticalHeader()->setVisible(false);
        table->horizontalHeader()->setStretchLastSection(true);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

        auto& tm = ThemeManager::instance();
        for (int row = 0; row < reviewRows.size(); ++row) {
            const ResumeReviewRow& review = reviewRows[row];
            const QStringList values = {
                review.action,
                review.member,
                review.fileName,
                review.sourcePath,
                review.outputPath,
                review.detail
            };

            for (int column = 0; column < values.size(); ++column) {
                auto* item = new QTableWidgetItem(values[column]);
                item->setToolTip(values[column]);
                if (review.blocked) {
                    item->setForeground(tm.supportError());
                } else if (review.warning) {
                    item->setForeground(tm.supportWarning());
                }
                table->setItem(row, column, item);
            }
        }
        table->resizeColumnsToContents();
        layout->addWidget(table, 1);

        auto* buttons = new QDialogButtonBox(&dialog);
        QPushButton* startButton = buttons->addButton("Start Resume", QDialogButtonBox::AcceptRole);
        startButton->setToolTip("Accept this plan and start the resumed watermark job.");
        QPushButton* cancelButton = buttons->addButton(QDialogButtonBox::Cancel);
        cancelButton->setToolTip("Leave the paused job unchanged.");
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);

        return dialog.exec() == QDialog::Accepted;
    };

    if (!showResumeReview()) {
        updateButtonStates();
        return;
    }

    for (const QString& memberId : skipExternalMembers) {
        markMemberRowsUploaded(
            memberId,
            "Skipped on resume: member folder was already uploaded, removed, or cleaned up outside the app.");
    }

    m_workerIdxToRow.clear();
    for (int row = 0; row < m_files.size(); ++row) {
        WatermarkFileInfo& info = m_files[row];
        if (info.isHeader) {
            continue;
        }

        const bool forceRebuildMember = rebuildMembers.contains(info.memberId);
        const int headerRow = findMemberHeaderRow(info.memberId);
        const bool memberUploaded = !forceRebuildMember
            && headerRow >= 0
            && m_files[headerRow].status == "uploaded";
        const bool complete = !forceRebuildMember
            && (info.status == "complete" || info.status == "uploaded");

        if (memberUploaded) {
            skippedCompleteCount++;
            continue;
        }

        if (complete) {
            skippedCompleteCount++;
            if (!info.memberId.isEmpty()
                && !info.outputPath.isEmpty()
                && QFileInfo::exists(info.outputPath)) {
                WatermarkResumeTask task;
                task.filePath = info.filePath;
                task.memberId = info.memberId;
                task.rowIndex = row;
                task.existingOutputPath = info.outputPath;
                task.watermarkNeeded = false;
                tasks.append(task);
                existingOutputTaskCount++;
            }
            continue;
        }

        if (!QFileInfo::exists(info.filePath)) {
            missingSources.append(info.filePath);
            info.status = "error";
            info.error = "Source file missing during resume";
            updateSingleRow(row);
            continue;
        }

        if (!info.outputPath.isEmpty() && QFileInfo::exists(info.outputPath)) {
            if (QFile::remove(info.outputPath)) {
                removedPartialOutputs.append(info.outputPath);
            }
        }

        info.status = "pending";
        info.outputPath.clear();
        info.error.clear();
        info.progressPercent = 0;
        updateSingleRow(row);

        WatermarkResumeTask task;
        task.filePath = info.filePath;
        task.memberId = info.memberId;
        task.rowIndex = row;
        task.watermarkNeeded = true;
        m_workerIdxToRow[watermarkTaskCount] = row;
        tasks.append(task);
        watermarkTaskCount++;
    }

    for (int row = 0; row < m_files.size(); ++row) {
        if (m_files[row].isHeader) {
            updateMemberHeader(row);
        }
    }
    updateStats();

    if (tasks.isEmpty()) {
        m_pausedForDiskSpace = false;
        m_diskSpacePauseMessage.clear();
        if (!m_pausedJobId.isEmpty()) {
            saveWatermarkCheckpoint("resume_no_pending_work", m_pausedJobId);
            OperationJobStore::instance().markCompleted(
                m_pausedJobId,
                skippedCompleteCount,
                0,
                0,
                "Paused watermark job already has no pending work");
        }
        m_pausedJobId.clear();
        m_currentJobId.clear();
        m_statusLabel->setText("No pending watermark work remains.");
        updateButtonStates();
        return;
    }

    WatermarkConfig config = buildConfig();
    QString outputDir;
    if (!m_sameAsInputCheck->isChecked() && !m_outputDirEdit->text().isEmpty()) {
        outputDir = m_outputDirEdit->text();
    }

    if (m_autoUploadCheck && m_autoUploadCheck->isChecked()) {
        if (!m_megaApi) {
            QMessageBox::warning(this, "Not Connected",
                "Auto-upload is enabled, but the MEGA connection is not available.\n\n"
                "Log in first, or disable auto-upload and resume for manual Distribution handoff.");
            return;
        }

        bool useCustomPath = m_customPathCheck && m_customPathCheck->isChecked()
            && m_customPathEdit && !m_customPathEdit->text().isEmpty();
        if (m_customPathCheck && m_customPathCheck->isChecked() && !useCustomPath) {
            QMessageBox::warning(this, "Missing Path",
                "Custom upload path is enabled but no path specified.\n"
                "Enter a MEGA cloud path or disable the custom path option.");
            return;
        }

        if (!useCustomPath) {
            QStringList membersWithoutFolder;
            QSet<QString> memberIds;
            for (const WatermarkResumeTask& task : tasks) {
                if (!task.memberId.isEmpty()) {
                    memberIds.insert(task.memberId);
                }
            }
            for (const QString& memberId : memberIds) {
                MemberInfo mi = m_registry->getMember(memberId);
                if (mi.distributionFolder.isEmpty()) {
                    membersWithoutFolder << (mi.displayName.isEmpty() ? memberId : mi.displayName);
                }
            }
            if (!membersWithoutFolder.isEmpty()) {
                QMessageBox::warning(this, "Missing Distribution Folders",
                    QString("Auto-upload is enabled but these members have no distribution folder:\n\n%1")
                        .arg(membersWithoutFolder.join("\n")));
                return;
            }
        }
    }

    m_currentJobId = m_pausedJobId.isEmpty() ? m_currentJobId : m_pausedJobId;
    if (m_currentJobId.isEmpty()) {
        QJsonObject metadata;
        metadata["resumeMode"] = "disk_full";
        metadata["resumeWatermarkTaskCount"] = watermarkTaskCount;
        metadata["resumeExistingOutputCount"] = existingOutputTaskCount;
        metadata["resumeUploadOnlyCount"] = existingOutputTaskCount;
        metadata["resumeSkippedCompleteCount"] = skippedCompleteCount;
        metadata["resumeSkippedExternalMemberCount"] = skippedExternalMemberCount;
        metadata["resumeRebuildMemberCount"] = rebuildMemberCount;
        metadata["resumeMissingSourceCount"] = missingSources.size();
        metadata["resumeRemovedPartialOutputCount"] = removedPartialOutputs.size();
        metadata["lastResumeReviewAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        m_currentJobId = OperationJobStore::instance().createJob(
            OperationJobType::Watermark,
            QString("Resume paused watermark %1 rows").arg(watermarkTaskCount),
            watermarkTaskCount + skippedCompleteCount,
            metadata);
    } else {
        OperationJobRecord record = OperationJobStore::instance().job(m_currentJobId);
        QJsonObject metadata = record.metadata;
        metadata["resumeMode"] = "disk_full";
        metadata["resumeWatermarkTaskCount"] = watermarkTaskCount;
        metadata["resumeExistingOutputCount"] = existingOutputTaskCount;
        metadata["resumeUploadOnlyCount"] = existingOutputTaskCount;
        metadata["resumeSkippedCompleteCount"] = skippedCompleteCount;
        metadata["resumeSkippedExternalMemberCount"] = skippedExternalMemberCount;
        metadata["resumeRebuildMemberCount"] = rebuildMemberCount;
        metadata["resumeMissingSourceCount"] = missingSources.size();
        metadata["resumeRemovedPartialOutputCount"] = removedPartialOutputs.size();
        metadata["lastResumeReviewAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        metadata["lastResumeAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        OperationJobStore::instance().createJob(
            OperationJobType::Watermark,
            record.title.isEmpty()
                ? QString("Resume paused watermark %1 rows").arg(watermarkTaskCount)
                : record.title,
            qMax(record.plannedCount, watermarkTaskCount + skippedCompleteCount),
            metadata,
            m_currentJobId);
    }
    for (WatermarkFileInfo& info : m_files) {
        info.jobId = m_currentJobId;
    }
    saveWatermarkCheckpoint("resume_prepared");

    m_workerThread = new QThread();
    m_worker = new WatermarkWorker();
    m_worker->moveToThread(m_workerThread);
    m_worker->setFiles({});
    m_worker->setOutputDir(outputDir);
    m_worker->setConfig(config);
    m_worker->setRawTemplates(m_primaryTextEdit->text(), m_secondaryTextEdit->text());
    m_worker->setRootDir(m_sourceRootDir);
    m_worker->setResumeTasks(tasks);

    const bool resumeAutoUploadActive = m_autoUploadCheck && m_autoUploadCheck->isChecked() && m_megaApi;
    if (resumeAutoUploadActive) {
        m_worker->setAutoUpload(true, m_megaApi);
        if (m_customPathCheck && m_customPathCheck->isChecked()
            && m_customPathEdit && !m_customPathEdit->text().isEmpty()) {
            m_worker->setCustomUploadPath(m_customPathEdit->text());
        }
    }
    if (m_metricsStore) {
        m_worker->setMetricsStore(m_metricsStore);
    }

    connect(m_workerThread, &QThread::started, m_worker, &WatermarkWorker::process);
    connect(m_worker, &WatermarkWorker::progress, this, &WatermarkPanel::onWorkerProgress);
    connect(m_worker, &WatermarkWorker::fileCompleted, this, &WatermarkPanel::onWorkerFileCompleted);
    connect(m_worker, &WatermarkWorker::finished, this, &WatermarkPanel::onWorkerFinished);
    connect(m_worker, &WatermarkWorker::finishedWithMapping, this,
            [this, resumeAutoUploadActive](int, int, const QMap<QString, QStringList>& memberFileMap) {
                if (m_pausedForDiskSpace || memberFileMap.isEmpty()) {
                    return;
                }
                if (resumeAutoUploadActive) {
                    m_lastMemberFileMap.clear();
                    m_sendToDistBtn->setEnabled(false);
                } else {
                    m_lastMemberFileMap = memberFileMap;
                    m_sendToDistBtn->setEnabled(true);
                }
            });
    connect(m_worker, &WatermarkWorker::memberBatchUploading, this,
        [this](const QString& memberId, int fileIdx, int totalFiles, const QString& fileName) {
            m_statusLabel->setText(QString("Uploading %1 to %2 (%3/%4)...")
                .arg(fileName).arg(memberId).arg(fileIdx).arg(totalFiles));
            int hdr = findMemberHeaderRow(memberId);
            if (hdr >= 0) {
                m_files[hdr].status = "uploading";
                updateMemberHeader(hdr);
            }
        });
    connect(m_worker, &WatermarkWorker::memberBatchCleanedUp, this,
        [this](const QString& memberId, int uploaded, int failed, int deleted) {
            qDebug() << "Smart resume pipeline:" << memberId
                     << "uploaded:" << uploaded << "failed:" << failed << "cleaned:" << deleted;
            int hdr = findMemberHeaderRow(memberId);
            if (hdr >= 0) {
                if (failed > 0) {
                    m_files[hdr].status = "error";
                    m_files[hdr].error = QString("%1 upload(s) failed during resume").arg(failed);
                    updateMemberHeader(hdr);
                } else {
                    markMemberRowsUploaded(
                        memberId,
                        QString("Uploaded to MEGA during resume (%1 file(s)); cleaned up %2 local file(s)")
                            .arg(uploaded)
                            .arg(deleted));
                }
            }
        });
    connect(m_worker, &WatermarkWorker::diskSpaceWarning, this,
        [this](qint64 available, qint64 needed) {
            qWarning() << "Low disk space during resume! Available:" << available << "Needed:" << needed;
            m_pausedForDiskSpace = true;
            m_diskSpacePauseMessage = QString("Paused: disk is still full (%1 free, %2 needed). Free space and resume again.")
                .arg(formatFileSize(available))
                .arg(formatFileSize(needed));
            m_statusLabel->setText(m_diskSpacePauseMessage);
            OperationJobStore::instance().markPaused(m_currentJobId, m_diskSpacePauseMessage);
        });
    connect(m_worker, &WatermarkWorker::memberAutoUploadSkipped, this,
        [this](const QString& memberId, const QString& reason) {
            qWarning() << "Auto-upload skipped during resume for" << memberId << ":" << reason;
            m_statusLabel->setText(QString("Skipped upload for %1: %2").arg(memberId).arg(reason));
            int hdr = findMemberHeaderRow(memberId);
            if (hdr >= 0) {
                m_files[hdr].status = "error";
                m_files[hdr].error = reason;
                updateMemberHeader(hdr);
            }
        });

    connect(m_worker, &WatermarkWorker::finished, m_workerThread, &QThread::quit);
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater);
    connect(m_workerThread, &QThread::finished, this, [this]() {
        m_workerThread = nullptr;
        m_worker = nullptr;
    });

    m_pausedForDiskSpace = false;
    m_diskSpacePauseMessage.clear();
    m_pausedJobId.clear();
    m_currentJobCancelled = false;
    m_isRunning = true;
    m_sendToDistBtn->setEnabled(false);
    updateButtonStates();
    m_progressBar->setValue(0);
    m_statusLabel->setText(QString("Resuming paused watermark job: %1 row(s) pending...")
        .arg(watermarkTaskCount));

    emit watermarkStarted();
    OperationJobStore::instance().markRunning(
        m_currentJobId,
        QString("Resuming paused watermark job: %1 row(s) pending, %2 existing output(s) carried forward")
            .arg(watermarkTaskCount)
            .arg(existingOutputTaskCount));
    m_workerThread->start();
}

void WatermarkPanel::onStopWatermark() {
    if (m_worker) {
        m_currentJobCancelled = true;
        OperationJobStore::instance().markCancelled(
            m_currentJobId,
            "Watermark cancellation requested");
        m_worker->cancel();
        m_statusLabel->setText("Cancelling...");
    }
}

void WatermarkPanel::onOpenSettings() {
    WatermarkSettingsDialog dialog(this);

    // Load current settings into dialog
    WatermarkConfig config = buildConfig();
    dialog.setConfig(config);

    if (dialog.exec() == QDialog::Accepted) {
        // Get updated config and apply to quick settings UI
        WatermarkConfig newConfig = dialog.getConfig();

        // Update quick settings UI to reflect changes
        m_presetCombo->setCurrentText(QString::fromStdString(newConfig.preset));
        m_crfSpin->setValue(newConfig.crf);
        m_intervalSpin->setValue(newConfig.intervalSeconds);
        m_durationSpin->setValue(newConfig.durationSeconds);
    }
}

void WatermarkPanel::onCheckDependencies() {
    QString status;

    bool ffmpegOk = Watermarker::isFFmpegAvailable();
    bool pythonOk = Watermarker::isPythonAvailable();

    if (ffmpegOk) {
        status += "FFmpeg: Available\n";
    } else {
        status += "FFmpeg: NOT FOUND (required for video watermarking)\n";
        status += "  Install: sudo apt install ffmpeg\n";
    }

    if (pythonOk) {
        status += "Python + reportlab: Available\n";
    } else {
        status += "Python + reportlab: NOT FOUND (required for PDF watermarking)\n";
        status += "  Install: pip install reportlab PyPDF2\n";
    }

    QString scriptPath = QString::fromStdString(Watermarker::getPdfScriptPath());
    if (QFile::exists(scriptPath)) {
        status += "PDF Script: " + scriptPath + "\n";
    } else {
        status += "PDF Script: NOT FOUND at " + scriptPath + "\n";
    }

    QMessageBox::information(this, "Dependency Check", status);
}

void WatermarkPanel::onTableSelectionChanged() {
    updateButtonStates();
}

void WatermarkPanel::onModeChanged(int index) {
    Q_UNUSED(index);
    bool isGlobal = m_modeCombo->currentData().toString() == "global";
    m_memberWidget->setVisible(!isGlobal);

    // Auto-upload only available in per-member mode
    m_autoUploadCheck->setEnabled(!isGlobal);
    if (isGlobal) m_autoUploadCheck->setChecked(false);

    // Text fields are always editable — they accept template variables in both modes
    // Pre-fill with a sensible default when switching to member mode with empty fields
    if (!isGlobal && m_primaryTextEdit->text().isEmpty()) {
        m_primaryTextEdit->setText("{brand} - {member_name} ({member_id})");
    }

    updateSmartEstimate();
}

void WatermarkPanel::onMemberSelectionChanged() {
    bool expandedGroupSelection = false;
    QListWidgetItem* firstExpandedMember = nullptr;

    m_memberListWidget->blockSignals(true);
    for (int i = 0; i < m_memberListWidget->count(); ++i) {
        QListWidgetItem* item = m_memberListWidget->item(i);
        const QString data = item->data(Qt::UserRole).toString();
        if (item->checkState() != Qt::Checked || !data.startsWith("GROUP:")) {
            continue;
        }

        item->setCheckState(Qt::Unchecked);
        const QStringList groupIds = m_registry->getGroupMemberIds(data.mid(6));
        for (int j = 0; j < m_memberListWidget->count(); ++j) {
            QListWidgetItem* memberItem = m_memberListWidget->item(j);
            if (groupIds.contains(memberItem->data(Qt::UserRole).toString())) {
                memberItem->setCheckState(Qt::Checked);
                if (!firstExpandedMember) {
                    firstExpandedMember = memberItem;
                }
            }
        }
        expandedGroupSelection = true;
    }
    m_memberListWidget->blockSignals(false);

    if (expandedGroupSelection && !m_memberSearchEdit->text().isEmpty()) {
        m_memberSearchEdit->clear();
    }
    if (expandedGroupSelection && firstExpandedMember) {
        m_memberListWidget->scrollToItem(firstExpandedMember, QAbstractItemView::PositionAtCenter);
    }

    QStringList selected = getSelectedMemberIds();
    m_selectionSummaryLabel->setText(
        QString("%1 selected").arg(selected.size()));
    updateButtonStates();
    updateSmartEstimate();
}

void WatermarkPanel::onSelectAllMembers() {
    m_memberListWidget->blockSignals(true);
    for (int i = 0; i < m_memberListWidget->count(); ++i) {
        QListWidgetItem* item = m_memberListWidget->item(i);
        const QString data = item->data(Qt::UserRole).toString();
        if (!item->isHidden() && !data.startsWith("GROUP:")) {
            item->setCheckState(Qt::Checked);
        }
    }
    m_memberListWidget->blockSignals(false);
    onMemberSelectionChanged();
}

void WatermarkPanel::onDeselectAllMembers() {
    m_memberListWidget->blockSignals(true);
    for (int i = 0; i < m_memberListWidget->count(); ++i) {
        m_memberListWidget->item(i)->setCheckState(Qt::Unchecked);
    }
    m_memberListWidget->blockSignals(false);
    onMemberSelectionChanged();
}

void WatermarkPanel::onGroupQuickSelect(int index) {
    QString groupName = m_groupQuickSelectCombo->itemData(index).toString();
    if (groupName.isEmpty()) return;

    // Additive: check the group item and its individual members
    QStringList groupMemberIds = m_registry->getGroupMemberIds(groupName);
    if (!m_memberSearchEdit->text().isEmpty()) {
        m_memberSearchEdit->clear();
    }

    m_memberListWidget->blockSignals(true);
    QListWidgetItem* firstMemberItem = nullptr;
    for (int i = 0; i < m_memberListWidget->count(); ++i) {
        QListWidgetItem* item = m_memberListWidget->item(i);
        QString data = item->data(Qt::UserRole).toString();
        if (data == "GROUP:" + groupName) {
            item->setCheckState(Qt::Unchecked);
        }
        if (groupMemberIds.contains(data)) {
            item->setCheckState(Qt::Checked);
            if (!firstMemberItem) {
                firstMemberItem = item;
            }
        }
    }
    m_memberListWidget->blockSignals(false);
    if (firstMemberItem) {
        m_memberListWidget->scrollToItem(firstMemberItem, QAbstractItemView::PositionAtCenter);
    }

    // Reset combo to prompt
    m_groupQuickSelectCombo->blockSignals(true);
    m_groupQuickSelectCombo->setCurrentIndex(0);
    m_groupQuickSelectCombo->blockSignals(false);

    onMemberSelectionChanged();
    m_statusLabel->setText(QString("Added group: %1 (%2 members)").arg(groupName).arg(groupMemberIds.size()));
}

void WatermarkPanel::onMemberSearchChanged() {
    QString filter = m_memberSearchEdit->text().trimmed();
    for (int i = 0; i < m_memberListWidget->count(); ++i) {
        QListWidgetItem* item = m_memberListWidget->item(i);
        if (filter.isEmpty()) {
            item->setHidden(false);
        } else {
            item->setHidden(!item->text().contains(filter, Qt::CaseInsensitive));
        }
    }
}

QStringList WatermarkPanel::getSelectedMemberIds() const {
    QSet<QString> idSet;
    for (int i = 0; i < m_memberListWidget->count(); ++i) {
        QListWidgetItem* item = m_memberListWidget->item(i);
        if (item->checkState() != Qt::Checked) continue;

        QString data = item->data(Qt::UserRole).toString();
        if (data.startsWith("GROUP:")) {
            // Expand group to individual member IDs
            QString groupName = data.mid(6);
            QStringList groupIds = m_registry->getGroupMemberIds(groupName);
            for (const QString& id : groupIds) {
                idSet.insert(id);
            }
        } else if (!data.isEmpty()) {
            idSet.insert(data);
        }
    }
    return idSet.values();
}

void WatermarkPanel::onWorkerProgress(int fileIndex, int totalFiles, const QString& currentFile, int percent) {
    // Map worker index to actual table row (accounting for header rows)
    int row = m_workerIdxToRow.contains(fileIndex) ? m_workerIdxToRow[fileIndex] : fileIndex;

    if (row >= 0 && row < m_files.size() && !m_files[row].isHeader) {
        m_files[row].status = "processing";
        m_files[row].progressPercent = percent;
        updateSingleRow(row);

        // Update the member's header row
        int headerRow = findMemberHeaderRow(m_files[row].memberId);
        if (headerRow >= 0) updateMemberHeader(headerRow);

        // Auto-scroll to current file
        if (auto* item = m_fileTable->item(row, 0))
            m_fileTable->scrollToItem(item, QAbstractItemView::PositionAtCenter);
    }

    int overallPercent = totalFiles > 0 ? (fileIndex * 100 + percent) / totalFiles : 0;
    AnimationHelper::animateProgress(m_progressBar, overallPercent);

    QString fileName = QFileInfo(currentFile).fileName();
    if (row >= 0 && row < m_files.size() && !m_files[row].memberName.isEmpty()) {
        m_statusLabel->setText(QString("Processing %1 for %2 (%3%)")
            .arg(fileName).arg(m_files[row].memberName).arg(percent));
    } else {
        m_statusLabel->setText(QString("Processing %1 (%2%)").arg(fileName).arg(percent));
    }

    updateCurrentJobProgress(QString("Processing %1 (%2%)")
        .arg(fileName.isEmpty() ? "current file" : fileName)
        .arg(percent));

    emit watermarkProgress(fileIndex + 1, totalFiles, currentFile);
}

void WatermarkPanel::onWorkerFileCompleted(int fileIndex, bool success, const QString& outputPath, const QString& error) {
    // Map worker index to actual table row
    int row = m_workerIdxToRow.contains(fileIndex) ? m_workerIdxToRow[fileIndex] : fileIndex;

    if (row >= 0 && row < m_files.size() && !m_files[row].isHeader) {
        m_files[row].status = success ? "complete" : "error";
        m_files[row].outputPath = outputPath;
        m_files[row].error = error;
        m_files[row].progressPercent = 100;
        updateSingleRow(row);

        // Update the member's header row
        int headerRow = findMemberHeaderRow(m_files[row].memberId);
        if (headerRow >= 0) updateMemberHeader(headerRow);

        const WatermarkFileInfo& fileInfo = m_files[row];
        if (success) {
            LogManager::instance().logWithContext(LogLevel::Info, LogCategory::Watermark,
                "watermark.file_completed",
                QString("Watermarked: %1").arg(outputPath).toStdString(),
                fileInfo.memberId.toStdString(),
                outputPath.toStdString(),
                m_currentJobId.toStdString());
        } else {
            LogManager::instance().logWithContext(LogLevel::Error, LogCategory::Watermark,
                "watermark.file_failed",
                QString("Failed to watermark: %1").arg(fileInfo.filePath).toStdString(),
                fileInfo.memberId.toStdString(),
                fileInfo.filePath.toStdString(),
                m_currentJobId.toStdString(),
                error.toStdString());
            OperationJobStore::instance().setLastError(m_currentJobId, error);
        }
    }

    updateCurrentJobProgress(success
        ? QString("Watermarked %1").arg(QFileInfo(outputPath).fileName())
        : QString("Failed: %1").arg(error.left(160)));
    saveWatermarkCheckpoint(success ? "row_completed" : "row_failed");
}

QString WatermarkPanel::watermarkReportRootDir() const {
    const QString configuredOutput = m_outputDirEdit ? m_outputDirEdit->text().trimmed() : QString();
    if (m_sameAsInputCheck && !m_sameAsInputCheck->isChecked() && !configuredOutput.isEmpty()) {
        return QDir::cleanPath(configuredOutput);
    }
    if (!m_sourceRootDir.trimmed().isEmpty()) {
        return QDir::cleanPath(m_sourceRootDir.trimmed());
    }
    for (const WatermarkFileInfo& info : m_files) {
        if (!info.outputPath.trimmed().isEmpty()) {
            QString cleanOutput = QDir::fromNativeSeparators(QDir::cleanPath(info.outputPath));
            if (!info.memberId.isEmpty()) {
                const QStringList parts = cleanOutput.split('/', Qt::SkipEmptyParts);
                const int memberIndex = parts.lastIndexOf(info.memberId);
                if (memberIndex > 0) {
                    QString prefix = cleanOutput.startsWith('/') ? "/" : QString();
                    return QDir::cleanPath(prefix + parts.mid(0, memberIndex).join('/'));
                }
            }
            return QDir::cleanPath(QFileInfo(info.outputPath).absolutePath());
        }
    }
    for (const WatermarkFileInfo& info : m_files) {
        if (!info.filePath.trimmed().isEmpty()) {
            return QDir::cleanPath(QFileInfo(info.filePath).absolutePath());
        }
    }
    return QDir::homePath();
}

QString WatermarkPanel::writeWatermarkCompletionReport(int successCount, int failCount) const {
    const QString rootDir = watermarkReportRootDir();
    QDir().mkpath(rootDir);

    const QString incompleteReportPath = QDir(rootDir).filePath("_WATERMARK_INCOMPLETE_DO_NOT_UPLOAD.txt");
    const QString completeReportPath = QDir(rootDir).filePath("_WATERMARK_COMPLETE_MANIFEST.txt");
    const QString memberMarkerName = "_DO_NOT_UPLOAD_MISSING_WATERMARK_OUTPUTS.txt";

    QMap<QString, QList<WatermarkFileInfo>> failuresByMember;
    QSet<QString> allMemberIds;
    int missingCompletedOutputs = 0;
    for (const WatermarkFileInfo& info : m_files) {
        if (info.isHeader) {
            if (!info.memberId.isEmpty()) {
                allMemberIds.insert(info.memberId);
            }
            continue;
        }
        if (!info.memberId.isEmpty()) {
            allMemberIds.insert(info.memberId);
        }
        const bool completeMissingLocalOutput = hasMissingCompletedOutput(info);
        if (completeMissingLocalOutput) {
            ++missingCompletedOutputs;
        }
        if (info.status == "error" || completeMissingLocalOutput) {
            failuresByMember[info.memberId.isEmpty() ? QString("global") : info.memberId].append(info);
        }
    }
    const bool hasFailures = failCount > 0 || missingCompletedOutputs > 0 || !failuresByMember.isEmpty();
    const QString reportPath = hasFailures ? incompleteReportPath : completeReportPath;

    QString report;
    QTextStream stream(&report);
    stream << (hasFailures ? "WATERMARK SESSION INCOMPLETE - DO NOT UPLOAD\n"
                           : "WATERMARK SESSION COMPLETE\n");
    stream << "Generated: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
    stream << "Output root: " << rootDir << "\n";
    stream << "Succeeded rows: " << successCount << "\n";
    stream << "Failed rows: " << failCount << "\n\n";
    if (missingCompletedOutputs > 0) {
        stream << "Completed rows with missing local outputs: " << missingCompletedOutputs << "\n\n";
    }

    if (hasFailures) {
        stream << "This output set is incomplete. Do not manually upload member folders until every failed row is fixed.\n\n";
        for (auto it = failuresByMember.constBegin(); it != failuresByMember.constEnd(); ++it) {
            const QString memberId = it.key();
            stream << "Member: " << memberId << "\n";
            for (const WatermarkFileInfo& failed : it.value()) {
                const bool completeMissingLocalOutput = hasMissingCompletedOutput(failed);
                stream << "  Missing output for: " << failed.fileName << "\n";
                stream << "    Source: " << failed.filePath << "\n";
                if (!failed.outputPath.isEmpty()) {
                    stream << "    Expected output: " << failed.outputPath << "\n";
                }
                if (completeMissingLocalOutput) {
                    stream << "    Error: row was marked complete but the local output file is missing\n";
                }
                if (!failed.error.isEmpty()) {
                    stream << "    Error: " << failed.error << "\n";
                }
            }
            stream << "\n";
        }
    } else {
        stream << "All rows completed successfully. Completed output rows:\n\n";
        for (const WatermarkFileInfo& info : m_files) {
            if (info.isHeader || (info.status != "complete" && info.status != "uploaded")) {
                continue;
            }
            stream << "  " << (info.memberId.isEmpty() ? QString("global") : info.memberId)
                   << " | " << info.fileName
                   << " | " << info.outputPath << "\n";
        }
        QFile::remove(incompleteReportPath);
    }

    QFile reportFile(reportPath);
    if (reportFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        reportFile.write(report.toUtf8());
        reportFile.close();
    }

    for (const QString& memberId : allMemberIds) {
        if (memberId.isEmpty()) {
            continue;
        }
        const QString memberRoot = QDir(rootDir).filePath(memberId);
        const QString markerPath = QDir(memberRoot).filePath(memberMarkerName);
        const QList<WatermarkFileInfo> memberFailures = failuresByMember.value(memberId);
        if (memberFailures.isEmpty()) {
            QFile::remove(markerPath);
            continue;
        }

        QDir().mkpath(memberRoot);
        QString marker;
        QTextStream markerStream(&marker);
        markerStream << "DO NOT UPLOAD THIS MEMBER FOLDER YET\n";
        markerStream << "Generated: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
        markerStream << "Member: " << memberId << "\n";
        markerStream << "Missing watermarked outputs: " << memberFailures.size() << "\n\n";
        for (const WatermarkFileInfo& failed : memberFailures) {
            const bool completeMissingLocalOutput = hasMissingCompletedOutput(failed);
            markerStream << "- " << failed.fileName << "\n";
            markerStream << "  Source: " << failed.filePath << "\n";
            if (completeMissingLocalOutput) {
                markerStream << "  Error: row was marked complete but the local output file is missing\n";
            }
            if (!failed.error.isEmpty()) {
                markerStream << "  Error: " << failed.error << "\n";
            }
        }

        QFile markerFile(markerPath);
        if (markerFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            markerFile.write(marker.toUtf8());
            markerFile.close();
        }
    }

    return reportPath;
}

bool WatermarkPanel::hasTrustedWatermarkOutput(const WatermarkFileInfo& info) const {
    if (info.isHeader) {
        return false;
    }
    if (info.status == "uploaded") {
        return true;
    }
    return info.status == "complete"
        && !info.outputPath.trimmed().isEmpty()
        && QFileInfo::exists(info.outputPath);
}

bool WatermarkPanel::hasMissingCompletedOutput(const WatermarkFileInfo& info) const {
    return !info.isHeader
        && info.status == "complete"
        && !hasTrustedWatermarkOutput(info);
}

int WatermarkPanel::completedWatermarkRowCount() const {
    int count = 0;
    for (const WatermarkFileInfo& info : m_files) {
        if (hasTrustedWatermarkOutput(info)) {
            ++count;
        }
    }
    return count;
}

int WatermarkPanel::failedWatermarkRowCount() const {
    int count = 0;
    for (const WatermarkFileInfo& info : m_files) {
        if (!info.isHeader && (info.status == "error" || hasMissingCompletedOutput(info))) {
            ++count;
        }
    }
    return count;
}

int WatermarkPanel::resumableWatermarkRowCount() const {
    int count = 0;
    for (const WatermarkFileInfo& info : m_files) {
        if (info.isHeader) {
            continue;
        }
        if (info.status == "pending" || info.status == "processing") {
            ++count;
        } else if (info.status == "error"
            && info.error.contains("disk", Qt::CaseInsensitive)
            && info.error.contains("space", Qt::CaseInsensitive)) {
            ++count;
        }
    }
    return count;
}

QString WatermarkPanel::owningWatermarkJobId() const {
    if (!m_currentJobId.trimmed().isEmpty()) {
        return m_currentJobId.trimmed();
    }
    if (!m_pausedJobId.trimmed().isEmpty()) {
        return m_pausedJobId.trimmed();
    }
    for (const WatermarkFileInfo& info : m_files) {
        if (!info.isHeader && !info.jobId.trimmed().isEmpty()) {
            return info.jobId.trimmed();
        }
    }
    return {};
}

QString WatermarkPanel::latestWatermarkReportPath() const {
    const QString jobId = owningWatermarkJobId();
    if (!jobId.isEmpty()) {
        const OperationJobRecord record = OperationJobStore::instance().job(jobId);
        const QString reportPath = record.metadata["watermarkCompletionReportPath"].toString();
        if (!reportPath.trimmed().isEmpty() && QFileInfo::exists(reportPath)) {
            return reportPath;
        }
    }

    const QString rootDir = watermarkReportRootDir();
    const QString incomplete = QDir(rootDir).filePath("_WATERMARK_INCOMPLETE_DO_NOT_UPLOAD.txt");
    if (QFileInfo::exists(incomplete)) {
        return incomplete;
    }
    const QString complete = QDir(rootDir).filePath("_WATERMARK_COMPLETE_MANIFEST.txt");
    if (QFileInfo::exists(complete)) {
        return complete;
    }
    return {};
}

QList<WatermarkPanel::WatermarkIssue> WatermarkPanel::buildWatermarkIssues() const {
    QMap<QString, WatermarkIssue> grouped;

    auto appendIssue = [&grouped](const QString& key,
                                  const QString& category,
                                  const QString& severity,
                                  const QString& title,
                                  const QString& detail,
                                  const QString& action,
                                  const WatermarkFileInfo& info,
                                  bool retryable,
                                  bool resumable,
                                  bool blocksUpload) {
        WatermarkIssue issue = grouped.value(key);
        if (issue.category.isEmpty()) {
            issue.category = category;
            issue.severity = severity;
            issue.title = title;
            issue.detail = detail;
            issue.recommendedAction = action;
            issue.retryable = retryable;
            issue.resumable = resumable;
            issue.blocksUpload = blocksUpload;
        } else {
            issue.retryable = issue.retryable || retryable;
            issue.resumable = issue.resumable || resumable;
            issue.blocksUpload = issue.blocksUpload || blocksUpload;
        }
        issue.affectedRows++;
        const QString member = info.memberName.isEmpty()
            ? (info.memberId.isEmpty() ? QString("Global") : info.memberId)
            : info.memberName;
        if (!member.isEmpty() && !issue.members.contains(member)) {
            issue.members.append(member);
        }
        const QString source = info.fileName.isEmpty() ? QFileInfo(info.filePath).fileName() : info.fileName;
        if (!source.isEmpty() && !issue.sources.contains(source)) {
            issue.sources.append(source);
        }
        grouped[key] = issue;
    };

    if (m_pausedForDiskSpace) {
        WatermarkFileInfo pauseInfo;
        pauseInfo.fileName = "Paused Watermark job";
        appendIssue("disk_pause",
            "Disk Space",
            "Blocked",
            "Watermarking is paused because disk space is too low",
            QString("%1 row(s) remain resumable. The app will not create more member folders until Resume Watermarking is accepted.")
                .arg(resumableWatermarkRowCount()),
            "Free disk space, then use Resume Watermarking so the member-batch safety review runs first.",
            pauseInfo,
            false,
            true,
            true);
    }

    for (const WatermarkFileInfo& info : m_files) {
        if (info.isHeader) {
            continue;
        }

        const bool completeMissingLocalOutput = hasMissingCompletedOutput(info);
        if (completeMissingLocalOutput) {
            appendIssue("missing_completed_output",
                "Missing Output",
                "Blocked",
                "Completed rows have missing local output files",
                "A row marked complete no longer has its local watermarked output. Manual upload and Distribution handoff are not safe until this is fixed.",
                "Restore the missing output or rebuild that member batch before uploading.",
                info,
                false,
                false,
                true);
        }

        if (info.status != "error") {
            continue;
        }

        const QString error = info.error.toLower();
        const bool sourceExists = QFileInfo::exists(info.filePath);
        if (!sourceExists) {
            appendIssue("missing_source",
                "Missing Source",
                "Blocked",
                "Source files are missing",
                "Failed rows cannot be retried because the original source file is not available at the saved path.",
                "Restore the source file or remove the row from the queue before retrying.",
                info,
                false,
                false,
                true);
        } else if (error.contains("disk") && error.contains("space")) {
            appendIssue("disk_error",
                "Disk Space",
                m_pausedForDiskSpace ? "Blocked" : "Warning",
                "Rows stopped because disk space was insufficient",
                "These rows should be treated as unfinished work, not finished failures.",
                m_pausedForDiskSpace
                    ? "Free disk space, then use Resume Watermarking."
                    : "Free disk space, then retry the failed rows.",
                info,
                !m_pausedForDiskSpace,
                m_pausedForDiskSpace,
                true);
        } else if (error.contains("charmap")
            || error.contains("codec can't encode")
            || error.contains("unicodeencode")
            || error.contains("metadata")) {
            appendIssue("pdf_encoding",
                "PDF Encoding",
                "Error",
                "PDF watermarking failed on path or metadata encoding",
                "The PDF worker failed while handling text metadata or console/path encoding.",
                "Retry failed rows after the encoding-safe PDF path is available in this build.",
                info,
                true,
                false,
                true);
        } else if (error.contains("ffmpeg")
            || info.fileType == "video"
            || info.fileType == "audio") {
            appendIssue("media_watermark_failed",
                "Media Watermark",
                "Error",
                "Media watermarking failed",
                "One or more audio/video rows did not produce a trusted output.",
                "Review the source file and retry the failed rows.",
                info,
                true,
                false,
                true);
        } else if (error.contains("upload")) {
            appendIssue("auto_upload_failed",
                "Upload",
                "Error",
                "Auto-upload failed after watermarking",
                "The local output may exist, but automatic delivery did not complete.",
                "Review the member folder and retry upload/distribution from complete local outputs.",
                info,
                false,
                false,
                true);
        } else {
            appendIssue("watermark_failed",
                "Watermark",
                "Error",
                "Watermark rows failed",
                "One or more rows did not produce a trusted watermarked output.",
                "Retry failed rows after reviewing the first error details.",
                info,
                true,
                false,
                true);
        }
    }

    return grouped.values();
}

void WatermarkPanel::updateSafetyActionStates() {
    const QList<WatermarkIssue> issues = buildWatermarkIssues();
    const int failedRows = failedWatermarkRowCount();
    const int completedRows = completedWatermarkRowCount();
    const bool pausedForDisk = m_pausedForDiskSpace && !m_isRunning;

    int retryableIssues = 0;
    for (const WatermarkIssue& issue : issues) {
        if (issue.retryable) {
            retryableIssues++;
        }
    }

    if (m_reviewIssuesBtn) {
        m_reviewIssuesBtn->setEnabled(!issues.isEmpty());
        m_reviewIssuesBtn->setToolTip(issues.isEmpty()
            ? "No actionable Watermark issues are currently detected."
            : QString("Review %1 grouped Watermark issue%2 with affected files, members, and next actions.")
                .arg(issues.size())
                .arg(issues.size() == 1 ? "" : "s"));
    }

    if (m_openReportFolderBtn) {
        const QString reportPath = latestWatermarkReportPath();
        const bool canOpenReport = !m_files.isEmpty()
            && (!reportPath.isEmpty() || failedRows > 0 || completedRows > 0);
        m_openReportFolderBtn->setEnabled(canOpenReport && !m_isRunning);
        m_openReportFolderBtn->setToolTip(canOpenReport
            ? "Open the folder containing the Watermark completion manifest or DO_NOT_UPLOAD report."
            : "A Watermark completion or incomplete-output report is not available yet.");
    }

    if (m_retryFailedRowsBtn) {
        const bool canRetryFailedRows = failedRows > 0
            && retryableIssues > 0
            && !m_isRunning
            && !pausedForDisk;
        m_retryFailedRowsBtn->setEnabled(canRetryFailedRows);
        if (pausedForDisk) {
            m_retryFailedRowsBtn->setToolTip("Resume the paused disk-space job instead of retrying failed rows.");
        } else if (canRetryFailedRows) {
            m_retryFailedRowsBtn->setToolTip("Retry exactly the failed Watermark row/member pairs from the loaded table.");
        } else if (failedRows > 0) {
            m_retryFailedRowsBtn->setToolTip("Loaded failed rows are not retryable until blocked sources or safety issues are fixed.");
        } else {
            m_retryFailedRowsBtn->setToolTip("No retryable failed Watermark rows are loaded.");
        }
    }
}

void WatermarkPanel::onReviewWatermarkIssues() {
    const QList<WatermarkIssue> issues = buildWatermarkIssues();
    if (issues.isEmpty()) {
        QMessageBox::information(this, "No Watermark Issues",
            "No actionable Watermark issues are currently detected in the loaded table.");
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle("Watermark Issues");
    dialog.resize(1040, 560);

    auto* layout = new QVBoxLayout(&dialog);
    auto* summary = new QLabel(QString(
        "%1 grouped issue%2 found. Review the affected members/files before uploading, retrying, or cleaning up.")
        .arg(issues.size())
        .arg(issues.size() == 1 ? "" : "s"));
    summary->setWordWrap(true);
    layout->addWidget(summary);

    auto* table = new QTableWidget(issues.size(), 6, &dialog);
    table->setHorizontalHeaderLabels({"Severity", "Issue", "Affected", "Members", "Examples", "Next Action"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    auto& tm = ThemeManager::instance();
    for (int row = 0; row < issues.size(); ++row) {
        const WatermarkIssue& issue = issues[row];
        const QString members = issue.members.mid(0, 6).join(", ")
            + (issue.members.size() > 6 ? QString(" ... +%1").arg(issue.members.size() - 6) : QString());
        const QString examples = issue.sources.mid(0, 4).join("\n")
            + (issue.sources.size() > 4 ? QString("\n... +%1 more").arg(issue.sources.size() - 4) : QString());
        const QStringList values = {
            issue.severity,
            issue.title,
            QString("%1 row%2").arg(issue.affectedRows).arg(issue.affectedRows == 1 ? "" : "s"),
            members,
            examples,
            issue.recommendedAction
        };

        for (int col = 0; col < values.size(); ++col) {
            auto* item = new QTableWidgetItem(values[col]);
            item->setToolTip(values[col]);
            if (issue.severity.compare("Blocked", Qt::CaseInsensitive) == 0
                || issue.blocksUpload) {
                item->setForeground(tm.supportError());
            } else if (issue.severity.compare("Warning", Qt::CaseInsensitive) == 0) {
                item->setForeground(tm.supportWarning());
            }
            table->setItem(row, col, item);
        }
    }
    table->resizeColumnsToContents();
    layout->addWidget(table, 1);

    auto* buttons = new QDialogButtonBox(&dialog);
    QPushButton* reportButton = buttons->addButton("Open Report Folder", QDialogButtonBox::ActionRole);
    reportButton->setToolTip("Open the folder containing the Watermark manifest or DO_NOT_UPLOAD report.");
    QPushButton* retryButton = buttons->addButton("Retry Failed Rows", QDialogButtonBox::ActionRole);
    retryButton->setToolTip("Retry exactly the retryable failed row/member pairs from this loaded table.");
    retryButton->setEnabled(m_retryFailedRowsBtn && m_retryFailedRowsBtn->isEnabled());
    QPushButton* closeButton = buttons->addButton(QDialogButtonBox::Close);
    closeButton->setToolTip("Close the issue review.");
    connect(reportButton, &QPushButton::clicked, this, &WatermarkPanel::onOpenWatermarkReportFolder);
    connect(retryButton, &QPushButton::clicked, &dialog, [&]() {
        dialog.accept();
        onRetryFailedWatermarkRows();
    });
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.exec();
}

void WatermarkPanel::onOpenWatermarkReportFolder() {
    if (m_files.isEmpty()) {
        QMessageBox::information(this, "No Watermark Report",
            "No Watermark table is loaded, so there is no report folder to open.");
        return;
    }

    QString reportPath = latestWatermarkReportPath();
    if (reportPath.isEmpty() || !QFileInfo::exists(reportPath)) {
        reportPath = writeWatermarkCompletionReport(
            completedWatermarkRowCount(),
            failedWatermarkRowCount());
    }

    const QFileInfo reportInfo(reportPath);
    if (reportPath.isEmpty() || !reportInfo.exists()) {
        QMessageBox::warning(this, "Report Unavailable",
            "The Watermark report could not be written or located.");
        return;
    }

    const QString folderPath = reportInfo.absolutePath();
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath))) {
        QMessageBox::warning(this, "Open Failed",
            QString("Could not open the report folder:\n%1").arg(folderPath));
        return;
    }

    m_statusLabel->setText(QString("Opened Watermark report folder: %1").arg(folderPath));
}

void WatermarkPanel::onRetryFailedWatermarkRows() {
    if (m_isRunning) {
        QMessageBox::warning(this, "Watermark Running",
            "Wait for the current Watermark job to finish before retrying failed rows.");
        return;
    }
    if (m_pausedForDiskSpace) {
        QMessageBox::information(this, "Resume Required",
            "This job is paused for disk space. Use Resume Watermarking so the member-batch safety review runs first.");
        return;
    }

    struct RetryCandidate {
        int row = -1;
        bool blocked = false;
        QString action;
        QString detail;
    };

    QList<RetryCandidate> candidates;
    int retryableCount = 0;
    int blockedCount = 0;

    for (int row = 0; row < m_files.size(); ++row) {
        const WatermarkFileInfo& info = m_files[row];
        if (info.isHeader || info.status != "error") {
            continue;
        }

        RetryCandidate candidate;
        candidate.row = row;
        if (!QFileInfo::exists(info.filePath)) {
            candidate.blocked = true;
            candidate.action = "Blocked";
            candidate.detail = "Source file is missing.";
            blockedCount++;
        } else {
            candidate.blocked = false;
            candidate.action = !info.outputPath.isEmpty() && QFileInfo::exists(info.outputPath)
                ? "Remove partial and retry"
                : "Retry";
            candidate.detail = "The row will be watermarked again from its saved source path.";
            retryableCount++;
        }
        candidates.append(candidate);
    }

    if (retryableCount == 0) {
        QMessageBox::warning(this, "No Retryable Rows",
            blockedCount > 0
                ? "Failed rows are loaded, but their source files are missing. Restore the sources before retrying."
                : "No failed Watermark rows are loaded.");
        return;
    }

    QDialog review(this);
    review.setWindowTitle("Review Failed Row Retry");
    review.resize(1040, 560);

    auto* layout = new QVBoxLayout(&review);
    auto* summary = new QLabel(QString(
        "Retry plan: %1 failed row%2 will be retried exactly as loaded. %3 row%4 blocked.")
        .arg(retryableCount)
        .arg(retryableCount == 1 ? "" : "s")
        .arg(blockedCount)
        .arg(blockedCount == 1 ? " is" : "s are"));
    summary->setWordWrap(true);
    layout->addWidget(summary);

    auto* table = new QTableWidget(candidates.size(), 6, &review);
    table->setHorizontalHeaderLabels({"Action", "Member", "File", "Source", "Output", "Detail"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    auto& tm = ThemeManager::instance();
    for (int i = 0; i < candidates.size(); ++i) {
        const RetryCandidate& candidate = candidates[i];
        const WatermarkFileInfo& info = m_files[candidate.row];
        const QString member = info.memberName.isEmpty()
            ? (info.memberId.isEmpty() ? QString("Global") : info.memberId)
            : info.memberName;
        const QStringList values = {
            candidate.action,
            member,
            info.fileName,
            info.filePath,
            info.outputPath,
            candidate.detail
        };
        for (int col = 0; col < values.size(); ++col) {
            auto* item = new QTableWidgetItem(values[col]);
            item->setToolTip(values[col]);
            item->setForeground(candidate.blocked ? tm.supportError() : tm.textPrimary());
            table->setItem(i, col, item);
        }
    }
    table->resizeColumnsToContents();
    layout->addWidget(table, 1);

    auto* buttons = new QDialogButtonBox(&review);
    QPushButton* retryButton = buttons->addButton("Retry Failed Rows", QDialogButtonBox::AcceptRole);
    retryButton->setToolTip("Start a recovery run for the retryable failed rows only. Auto-upload is not used by this action.");
    QPushButton* cancelButton = buttons->addButton(QDialogButtonBox::Cancel);
    cancelButton->setToolTip("Leave the failed rows unchanged.");
    connect(buttons, &QDialogButtonBox::accepted, &review, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &review, &QDialog::reject);
    layout->addWidget(buttons);

    if (review.exec() != QDialog::Accepted) {
        return;
    }

    QStringList removedPartialOutputs;
    QStringList skippedPartialOutputs;
    QList<WatermarkResumeTask> tasks;
    QSet<int> retryRows;
    QMap<QString, QList<int>> rowsByMember;
    for (const RetryCandidate& candidate : candidates) {
        if (candidate.blocked) {
            continue;
        }
        const WatermarkFileInfo& info = m_files[candidate.row];
        rowsByMember[info.memberId].append(candidate.row);
        retryRows.insert(candidate.row);
    }

    int workerIdx = 0;
    m_workerIdxToRow.clear();
    QStringList retrySourcePaths;
    QJsonArray retryRowArray;
    QJsonArray affectedMemberArray;

    for (auto it = rowsByMember.constBegin(); it != rowsByMember.constEnd(); ++it) {
        const QString memberId = it.key();
        if (!memberId.isEmpty()) {
            affectedMemberArray.append(memberId);
        }

        for (int row = 0; row < m_files.size(); ++row) {
            const WatermarkFileInfo& info = m_files[row];
            if (info.isHeader || info.memberId != memberId || retryRows.contains(row)) {
                continue;
            }
            if (info.status == "complete"
                && !info.outputPath.trimmed().isEmpty()
                && QFileInfo::exists(info.outputPath)) {
                WatermarkResumeTask existing;
                existing.filePath = info.filePath;
                existing.memberId = info.memberId;
                existing.rowIndex = row;
                existing.existingOutputPath = info.outputPath;
                existing.watermarkNeeded = false;
                tasks.append(existing);
            }
        }

        for (int row : it.value()) {
            WatermarkFileInfo& info = m_files[row];
            if (!info.outputPath.trimmed().isEmpty() && QFileInfo::exists(info.outputPath)) {
                if (QFile::remove(info.outputPath)) {
                    removedPartialOutputs.append(info.outputPath);
                } else {
                    skippedPartialOutputs.append(info.outputPath);
                    info.error = "Partial output could not be removed before retry";
                    updateSingleRow(row);
                    continue;
                }
            }

            retrySourcePaths.append(info.filePath);
            retryRowArray.append(row);
            info.status = "pending";
            info.outputPath.clear();
            info.error.clear();
            info.progressPercent = 0;
            updateSingleRow(row);

            WatermarkResumeTask task;
            task.filePath = info.filePath;
            task.memberId = info.memberId;
            task.rowIndex = row;
            task.watermarkNeeded = true;
            tasks.append(task);
            m_workerIdxToRow[workerIdx] = row;
            workerIdx++;
        }
    }

    if (!skippedPartialOutputs.isEmpty()) {
        QMessageBox::warning(this, "Retry Skipped Some Rows",
            QString("%1 partial output file(s) could not be removed, so those rows were not retried:\n\n%2")
                .arg(skippedPartialOutputs.size())
                .arg(skippedPartialOutputs.mid(0, 20).join("\n")));
    }

    if (workerIdx == 0 || tasks.isEmpty()) {
        updateStats();
        updateButtonStates();
        QMessageBox::warning(this, "Retry Unavailable",
            "No retry tasks remained after safety checks.");
        return;
    }

    for (int row = 0; row < m_files.size(); ++row) {
        if (m_files[row].isHeader) {
            updateMemberHeader(row);
        }
    }
    updateStats();

    WatermarkConfig config = buildConfig();
    QString outputDir;
    if (!m_sameAsInputCheck->isChecked() && !m_outputDirEdit->text().isEmpty()) {
        outputDir = m_outputDirEdit->text();
    }

    QString jobId = owningWatermarkJobId();
    OperationJobRecord existingRecord;
    if (!jobId.isEmpty()) {
        existingRecord = OperationJobStore::instance().job(jobId);
        if (existingRecord.id.isEmpty() || existingRecord.type != OperationJobType::Watermark) {
            jobId.clear();
        }
    }

    QJsonObject metadata = existingRecord.metadata;
    metadata["retryMode"] = "failed_rows";
    metadata["failedRowRetryAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    metadata["failedRowRetryCount"] = workerIdx;
    metadata["failedRowRetryRows"] = retryRowArray;
    metadata["failedRowRetryAffectedMembers"] = affectedMemberArray;
    metadata["failedRowRetryAutoUpload"] = false;
    metadata["failedRowRetryRemovedPartialOutputs"] = removedPartialOutputs.size();
    if (metadata["filePaths"].toArray().isEmpty()) {
        QJsonArray filePathArray;
        QSet<QString> seenPaths;
        for (const QString& path : retrySourcePaths) {
            if (!seenPaths.contains(path)) {
                seenPaths.insert(path);
                filePathArray.append(path);
            }
        }
        metadata["filePaths"] = filePathArray;
    }

    if (jobId.isEmpty()) {
        jobId = OperationJobStore::instance().createJob(
            OperationJobType::Watermark,
            QString("Retry %1 failed watermark rows").arg(workerIdx),
            qMax(workerIdx, m_files.size()),
            metadata);
    } else {
        OperationJobStore::instance().createJob(
            OperationJobType::Watermark,
            existingRecord.title.isEmpty()
                ? QString("Retry %1 failed watermark rows").arg(workerIdx)
                : existingRecord.title,
            qMax(existingRecord.plannedCount, m_files.size()),
            metadata,
            jobId);
    }

    m_currentJobId = jobId;
    m_retrySourceJobId.clear();
    m_currentJobCancelled = false;
    for (WatermarkFileInfo& info : m_files) {
        if (!info.isHeader) {
            info.jobId = m_currentJobId;
        }
    }
    saveWatermarkCheckpoint("failed_rows_retry_prepared", m_currentJobId);

    QJsonObject details;
    details["retryRows"] = workerIdx;
    details["blockedRows"] = blockedCount;
    details["autoUpload"] = false;
    details["removedPartialOutputs"] = removedPartialOutputs.size();
    LogManager::instance().logWithContext(
        LogLevel::Info,
        LogCategory::Watermark,
        "retry.failed_rows_started",
        QString("Retrying %1 failed Watermark row(s)").arg(workerIdx).toStdString(),
        "",
        "",
        m_currentJobId.toStdString(),
        QString::fromUtf8(QJsonDocument(details).toJson(QJsonDocument::Compact)).toStdString());

    m_workerThread = new QThread();
    m_worker = new WatermarkWorker();
    m_worker->moveToThread(m_workerThread);
    m_worker->setFiles({});
    m_worker->setOutputDir(outputDir);
    m_worker->setConfig(config);
    m_worker->setRawTemplates(m_primaryTextEdit->text(), m_secondaryTextEdit->text());
    m_worker->setRootDir(m_sourceRootDir);
    m_worker->setResumeTasks(tasks);
    if (m_metricsStore) {
        m_worker->setMetricsStore(m_metricsStore);
    }

    connect(m_workerThread, &QThread::started, m_worker, &WatermarkWorker::process);
    connect(m_worker, &WatermarkWorker::progress, this, &WatermarkPanel::onWorkerProgress);
    connect(m_worker, &WatermarkWorker::fileCompleted, this, &WatermarkPanel::onWorkerFileCompleted);
    connect(m_worker, &WatermarkWorker::finished, this, &WatermarkPanel::onWorkerFinished);
    connect(m_worker, &WatermarkWorker::finishedWithMapping, this,
            [this](int, int, const QMap<QString, QStringList>& memberFileMap) {
                if (m_pausedForDiskSpace || memberFileMap.isEmpty()) {
                    return;
                }
                m_lastMemberFileMap = memberFileMap;
                const bool handoffSafe = failedWatermarkRowCount() == 0;
                m_sendToDistBtn->setEnabled(handoffSafe);
                m_statusLabel->setText(handoffSafe
                    ? QString("Retried failed rows for %1 member%2. Review, then send to Distribution if needed.")
                        .arg(memberFileMap.size())
                        .arg(memberFileMap.size() == 1 ? "" : "s")
                    : "Retried failed rows, but the Watermark session still has blocking issues.");
            });
    connect(m_worker, &WatermarkWorker::diskSpaceWarning, this,
        [this](qint64 available, qint64 needed) {
            qWarning() << "Low disk space during failed-row retry! Available:" << available << "Needed:" << needed;
            m_pausedForDiskSpace = true;
            m_diskSpacePauseMessage = QString("Paused during failed-row retry: disk is full (%1 free, %2 needed). Free space and resume again.")
                .arg(formatFileSize(available))
                .arg(formatFileSize(needed));
            m_statusLabel->setText(m_diskSpacePauseMessage);
            OperationJobStore::instance().markPaused(m_currentJobId, m_diskSpacePauseMessage);
        });

    connect(m_worker, &WatermarkWorker::finished, m_workerThread, &QThread::quit);
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater);
    connect(m_workerThread, &QThread::finished, this, [this]() {
        m_workerThread = nullptr;
        m_worker = nullptr;
    });

    m_isRunning = true;
    m_sendToDistBtn->setEnabled(false);
    updateButtonStates();
    m_progressBar->setValue(0);
    m_statusLabel->setText(QString("Retrying %1 failed Watermark row(s)...").arg(workerIdx));

    emit watermarkStarted();
    OperationJobStore::instance().markRunning(
        m_currentJobId,
        QString("Retrying %1 failed Watermark row(s); auto-upload disabled").arg(workerIdx));
    m_workerThread->start();
}

void WatermarkPanel::onWorkerFinished(int successCount, int failCount) {
    m_isRunning = false;
    updateButtonStates();

    if (m_pausedForDiskSpace) {
        for (int row = 0; row < m_files.size(); ++row) {
            WatermarkFileInfo& info = m_files[row];
            if (info.isHeader || info.status != "processing") {
                continue;
            }
            info.status = "pending";
            info.progressPercent = 0;
            info.outputPath.clear();
            info.error.clear();
            updateSingleRow(row);

            const int headerRow = findMemberHeaderRow(info.memberId);
            if (headerRow >= 0) {
                updateMemberHeader(headerRow);
            }
        }

        emit watermarkCompleted(successCount, failCount);
        const QString message = m_diskSpacePauseMessage.isEmpty()
            ? "Paused: disk is full. Free space and start again."
            : m_diskSpacePauseMessage;
        m_statusLabel->setText(message);
        OperationJobStore::instance().markPaused(m_currentJobId, message);
        m_pausedJobId = m_currentJobId;
        saveWatermarkCheckpoint("paused_disk_full", m_pausedJobId);
        const int resumableRows = resumableWatermarkRowCount();
        QMessageBox::warning(this, "Watermarking Paused",
            QString("%1\n\n%2 row(s) remain resumable. No more files or member folders will be created. "
                    "Free space, then click Resume Watermarking to run the member-batch resume check.")
                .arg(message)
                .arg(resumableRows));
        m_currentJobId.clear();
        m_currentJobCancelled = false;
        updateButtonStates();
        return;
    }

    int finalSuccessCount = completedWatermarkRowCount();
    int finalFailCount = failedWatermarkRowCount();
    if (finalSuccessCount == 0 && finalFailCount == 0) {
        finalSuccessCount = successCount;
        finalFailCount = failCount;
    }

    emit watermarkCompleted(finalSuccessCount, finalFailCount);

    AnimationHelper::animateProgress(m_progressBar, 100);
    const QString reportPath = writeWatermarkCompletionReport(finalSuccessCount, finalFailCount);
    m_statusLabel->setText(finalFailCount > 0
        ? QString("Incomplete: %1 success, %2 failed. Do not manually upload until fixed.")
            .arg(finalSuccessCount).arg(finalFailCount)
        : QString("Completed: %1 success, %2 failed").arg(finalSuccessCount).arg(finalFailCount));

    if (!m_currentJobId.isEmpty()) {
        OperationJobRecord record = OperationJobStore::instance().job(m_currentJobId);
        if (!record.id.isEmpty()) {
            QJsonObject metadata = record.metadata;
            metadata["watermarkCompletionReportPath"] = reportPath;
            metadata["watermarkManualUploadBlocked"] = finalFailCount > 0;
            metadata["watermarkFinalSuccessCount"] = finalSuccessCount;
            metadata["watermarkFinalFailCount"] = finalFailCount;
            OperationJobStore::instance().createJob(
                OperationJobType::Watermark,
                record.title,
                record.plannedCount,
                metadata,
                m_currentJobId);
        }

        saveWatermarkCheckpoint(m_currentJobCancelled
            ? "cancelled"
            : (finalFailCount > 0 ? "finished_with_errors" : "completed"));
        if (m_currentJobCancelled) {
            OperationJobStore::instance().markCancelled(
                m_currentJobId,
                QString("Watermark cancelled: %1 completed, %2 failed")
                    .arg(finalSuccessCount).arg(finalFailCount));
        } else if (finalFailCount > 0) {
            OperationJobStore::instance().markFailed(
                m_currentJobId,
                QString("Watermark completed with %1 failed").arg(finalFailCount),
                finalSuccessCount,
                finalFailCount);
        } else {
            OperationJobStore::instance().markCompleted(
                m_currentJobId,
                finalSuccessCount,
                finalFailCount,
                0,
                QString("Watermark complete: %1 succeeded").arg(finalSuccessCount));
        }
    }

    if (finalFailCount == 0) {
        QMessageBox::information(this, "Complete",
            QString("Successfully watermarked %1 %2.\n\nCompletion manifest:\n%3")
                .arg(finalSuccessCount)
                .arg(finalSuccessCount == 1 ? "file" : "files")
                .arg(reportPath));
    } else {
        QMessageBox::warning(this, "Complete with Errors",
            QString("This watermark session is incomplete: %1 succeeded, %2 failed.\n\n"
                    "Do not manually upload these member folders yet.\n\n"
                    "A DO_NOT_UPLOAD report was written here:\n%3\n\n"
                    "Each incomplete member folder also has a marker file listing its missing outputs.")
                .arg(finalSuccessCount)
                .arg(finalFailCount)
                .arg(reportPath));
    }

    m_currentJobId.clear();
    m_currentJobCancelled = false;
}

void WatermarkPanel::updateCurrentJobProgress(const QString& summary) {
    if (m_currentJobId.isEmpty()) {
        return;
    }

    int completed = 0;
    int failed = 0;
    for (const WatermarkFileInfo& info : m_files) {
        if (info.isHeader) {
            continue;
        }
        if (info.status == "complete" || info.status == "uploaded") {
            ++completed;
        } else if (info.status == "error") {
            ++failed;
        }
    }

    OperationJobStore::instance().updateProgress(
        m_currentJobId,
        completed,
        failed,
        0,
        summary);
}

void WatermarkPanel::populateTable() {
    m_fileTable->setRowCount(m_files.size());

    for (int row = 0; row < m_files.size(); ++row) {
        const WatermarkFileInfo& info = m_files[row];

        if (info.isHeader) {
            // Member section header — spans all columns
            auto& tm = ThemeManager::instance();
            QTableWidgetItem* headerItem = new QTableWidgetItem(info.fileName);
            QFont boldFont = headerItem->font();
            boldFont.setBold(true);
            boldFont.setPointSize(boldFont.pointSize() + 1);
            headerItem->setFont(boldFont);
            headerItem->setForeground(tm.textPrimary());
            QColor headerBg = tm.surface2();
            headerItem->setBackground(headerBg);
            headerItem->setFlags(Qt::ItemIsEnabled); // Non-selectable
            m_fileTable->setItem(row, 0, headerItem);

            // Fill remaining columns with empty styled cells
            for (int col = 1; col < 6; ++col) {
                QTableWidgetItem* filler = new QTableWidgetItem();
                filler->setBackground(headerBg);
                filler->setFlags(Qt::ItemIsEnabled);
                m_fileTable->setItem(row, col, filler);
            }
            m_fileTable->setSpan(row, 0, 1, 6);
        } else {
            updateSingleRow(row);
        }
    }

    updateEmptyState();
}

void WatermarkPanel::updateSingleRow(int row) {
    if (row < 0 || row >= m_files.size()) return;
    const WatermarkFileInfo& info = m_files[row];
    if (info.isHeader) return; // Headers are rendered separately

    auto& tm = ThemeManager::instance();

    // File name (col 0)
    QTableWidgetItem* nameItem = new QTableWidgetItem(info.fileName);
    nameItem->setToolTip(info.filePath);
    m_fileTable->setItem(row, 0, nameItem);

    // Member (col 1)
    QTableWidgetItem* memberItem = new QTableWidgetItem(info.memberName);
    if (!info.memberId.isEmpty()) {
        memberItem->setToolTip(info.memberId);
        memberItem->setForeground(tm.supportInfo());
    }
    m_fileTable->setItem(row, 1, memberItem);

    // Type (col 2)
    QTableWidgetItem* typeItem = new QTableWidgetItem(info.fileType.toUpper());
    typeItem->setTextAlignment(Qt::AlignCenter);
    if (info.fileType == "video") {
        typeItem->setForeground(tm.supportInfo());
    } else if (info.fileType == "audio") {
        typeItem->setForeground(tm.supportWarning());
    } else {
        typeItem->setForeground(tm.supportError());
    }
    m_fileTable->setItem(row, 2, typeItem);

    // Size (col 3)
    QTableWidgetItem* sizeItem = new QTableWidgetItem(formatFileSize(info.fileSize));
    sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_fileTable->setItem(row, 3, sizeItem);

    // Status (col 4)
    QTableWidgetItem* statusItem = new QTableWidgetItem();
    const bool completeMissingLocalOutput = hasMissingCompletedOutput(info);

    if (info.status == "pending") {
        statusItem->setText("Pending");
        statusItem->setForeground(tm.textSecondary());
    } else if (info.status == "processing") {
        statusItem->setText(QString("Processing %1%").arg(info.progressPercent));
        statusItem->setForeground(tm.supportWarning());
    } else if (completeMissingLocalOutput) {
        statusItem->setText("Missing Output");
        statusItem->setForeground(tm.supportError());
        statusItem->setToolTip("This row was marked complete, but the local watermarked output file is missing.");
    } else if (info.status == "complete") {
        statusItem->setText("Complete");
        statusItem->setForeground(tm.supportSuccess());
    } else if (info.status == "uploaded") {
        statusItem->setText("Uploaded");
        statusItem->setForeground(tm.supportSuccess());
        if (!info.error.isEmpty()) {
            statusItem->setToolTip(info.error);
        }
    } else if (info.status == "error") {
        statusItem->setText("Error");
        statusItem->setForeground(tm.supportError());
        statusItem->setToolTip(info.error);
    }
    statusItem->setTextAlignment(Qt::AlignCenter);
    m_fileTable->setItem(row, 4, statusItem);

    // Output (col 5)
    QTableWidgetItem* outputItem = new QTableWidgetItem(info.outputPath);
    if (completeMissingLocalOutput) {
        outputItem->setText("Missing local output: " + info.outputPath);
        outputItem->setForeground(tm.supportError());
        outputItem->setToolTip(info.outputPath);
    } else if (info.status == "error" && !info.error.isEmpty()) {
        outputItem->setText(info.error);
        outputItem->setForeground(tm.supportError());
    } else if (info.status == "uploaded") {
        outputItem->setText(info.error.isEmpty()
            ? "Uploaded to MEGA; local copy cleaned up"
            : info.error);
        outputItem->setForeground(tm.supportSuccess());
    }
    m_fileTable->setItem(row, 5, outputItem);

    // Row background highlight
    if (info.status == "error" || completeMissingLocalOutput) {
        QColor errorBg = tm.supportError(); errorBg.setAlpha(30);
        for (int c = 0; c < 6; ++c)
            if (auto* item = m_fileTable->item(row, c)) item->setBackground(errorBg);
    } else if (info.status == "complete" || info.status == "uploaded") {
        QColor successBg = tm.supportSuccess(); successBg.setAlpha(30);
        for (int c = 0; c < 6; ++c)
            if (auto* item = m_fileTable->item(row, c)) item->setBackground(successBg);
    }
}

void WatermarkPanel::updateMemberHeader(int headerRow) {
    if (headerRow < 0 || headerRow >= m_files.size()) return;
    if (!m_files[headerRow].isHeader) return;

    auto& tm = ThemeManager::instance();
    const QString& memberId = m_files[headerRow].memberId;
    const QString& memberName = m_files[headerRow].memberName;

    // Scan files belonging to this member (rows after header until next header or end)
    int totalFiles = 0, completeCount = 0, errorCount = 0, processingCount = 0;
    for (int r = headerRow + 1; r < m_files.size() && !m_files[r].isHeader; ++r) {
        totalFiles++;
        if (m_files[r].status == "complete" || m_files[r].status == "uploaded") completeCount++;
        else if (m_files[r].status == "error") errorCount++;
        else if (m_files[r].status == "processing") processingCount++;
    }

    // Build header text based on state
    QString statusText;
    QColor statusColor;
    const QString& headerStatus = m_files[headerRow].status;

    if (headerStatus == "uploading") {
        statusText = QString::fromUtf8("\u25b8 %1 \u2014 Uploading...").arg(memberName);
        statusColor = tm.supportInfo();
    } else if (headerStatus == "uploaded") {
        statusText = QString::fromUtf8("\u25b8 %1 \u2014 Uploaded").arg(memberName);
        statusColor = tm.supportSuccess();
    } else if (completeCount + errorCount == totalFiles && totalFiles > 0) {
        // All files processed
        if (errorCount == 0) {
            statusText = QString::fromUtf8("\u2713 %1 \u2014 Done (%2 files)")
                .arg(memberName).arg(completeCount);
            statusColor = tm.supportSuccess();
        } else {
            statusText = QString::fromUtf8("\u25b8 %1 \u2014 Done (%2 ok, %3 failed)")
                .arg(memberName).arg(completeCount).arg(errorCount);
            statusColor = tm.supportError();
        }
    } else if (processingCount > 0) {
        statusText = QString::fromUtf8("\u25b8 %1 \u2014 Processing (%2/%3)")
            .arg(memberName).arg(completeCount + errorCount).arg(totalFiles);
        statusColor = tm.supportWarning();
    } else {
        statusText = QString::fromUtf8("\u25b8 %1 \u2014 Pending (%2 files)")
            .arg(memberName).arg(totalFiles);
        statusColor = tm.textSecondary();
    }

    // Update the header cell
    QTableWidgetItem* item = m_fileTable->item(headerRow, 0);
    if (item) {
        item->setText(statusText);
        item->setForeground(statusColor);
    }
}

void WatermarkPanel::markMemberRowsUploaded(const QString& memberId, const QString& note) {
    const int headerRow = findMemberHeaderRow(memberId);
    if (headerRow < 0) {
        return;
    }

    m_files[headerRow].status = "uploaded";
    m_files[headerRow].error = note;

    for (int row = headerRow + 1; row < m_files.size() && !m_files[row].isHeader; ++row) {
        if (m_files[row].memberId != memberId) {
            continue;
        }

        m_files[row].status = "uploaded";
        m_files[row].progressPercent = 100;
        m_files[row].outputPath.clear();
        m_files[row].error = note;
        updateSingleRow(row);
    }

    updateMemberHeader(headerRow);
    updateStats();
    updateButtonStates();
    saveWatermarkCheckpoint("member_marked_uploaded");
}

int WatermarkPanel::findMemberHeaderRow(const QString& memberId) const {
    for (int i = 0; i < m_files.size(); ++i) {
        if (m_files[i].isHeader && m_files[i].memberId == memberId)
            return i;
    }
    return -1;
}

void WatermarkPanel::updateEmptyState() {
    bool empty = m_files.isEmpty();
    m_emptyState->setVisible(empty);
    m_fileTable->setVisible(!empty);
}

void WatermarkPanel::updateStats() {
    int videoCount = 0, pdfCount = 0, errorCount = 0, completeCount = 0, uploadedCount = 0;
    qint64 totalSize = 0;
    QSet<QString> uniquePaths;

    int headerCount = 0;
    for (const WatermarkFileInfo& info : m_files) {
        if (info.isHeader) { headerCount++; continue; }
        if (!uniquePaths.contains(info.filePath)) {
            uniquePaths.insert(info.filePath);
            if (info.fileType == "video") videoCount++;
            else pdfCount++;
            totalSize += info.fileSize;
        }
        const bool completeMissingLocalOutput = hasMissingCompletedOutput(info);
        if (info.status == "error" || completeMissingLocalOutput) errorCount++;
        else if (info.status == "complete") completeCount++;
        else if (info.status == "uploaded") uploadedCount++;
    }

    int uniqueFileCount = uniquePaths.size();
    int totalOps = m_files.size() - headerCount;

    QString statsText;
    if (totalOps > uniqueFileCount) {
        // Expanded mode: show "5 files × 3 members (15 operations)"
        int memberCount = totalOps / qMax(uniqueFileCount, 1);
        statsText = QString("Files: %1 \u00d7 %2 members (%3 ops) | Size: %4")
            .arg(uniqueFileCount).arg(memberCount).arg(totalOps)
            .arg(formatFileSize(totalSize));
    } else {
        statsText = QString("Files: %1 (%2 videos, %3 PDFs) | Size: %4")
            .arg(uniqueFileCount).arg(videoCount).arg(pdfCount)
            .arg(formatFileSize(totalSize));
    }

    if (completeCount > 0) {
        statsText += QString(" | <span style='color: green;'>%1 completed</span>").arg(completeCount);
    }
    if (uploadedCount > 0) {
        statsText += QString(" | <span style='color: green;'>%1 uploaded</span>").arg(uploadedCount);
    }

    if (errorCount > 0) {
        auto& tm = ThemeManager::instance();
        statsText += QString(" | <span style='color: %3; font-weight: bold;'>%1 %2</span>").arg(errorCount).arg(errorCount == 1 ? "error" : "errors").arg(tm.supportError().name());
    }

    m_statsLabel->setTextFormat(Qt::RichText);
    m_statsLabel->setText(statsText);

    // Update smart estimate when file list changes
    updateSmartEstimate();
}

void WatermarkPanel::updateButtonStates() {
    bool hasFiles = !m_files.isEmpty();
    bool hasSelection = m_fileTable->selectionModel()->hasSelection();
    const bool pausedForDisk = m_pausedForDiskSpace && !m_isRunning;

    const int completedCount = completedWatermarkRowCount();
    const int errorCount = failedWatermarkRowCount();

    m_startBtn->setText(pausedForDisk ? "Resume Watermarking" : "Start Watermarking");
    m_startBtn->setToolTip(pausedForDisk
        ? "Resume the paused disk-full job with a member-batch safety check before recreating any folders."
        : "Start watermarking the selected files with the current settings.");

    m_removeBtn->setEnabled(hasSelection && !m_isRunning && !pausedForDisk);
    m_clearBtn->setEnabled(hasFiles && !m_isRunning);
    m_startBtn->setEnabled(hasFiles && !m_isRunning);
    m_stopBtn->setEnabled(m_isRunning);
    m_sendToDistBtn->setEnabled(completedCount > 0 && errorCount == 0 && !m_isRunning && !pausedForDisk);
    m_sendToDistBtn->setToolTip(errorCount > 0
        ? "Disabled because this watermark session has failed rows. Fix or retry failed rows before sending/uploading."
        : "Send completed watermarked outputs to the Distribution panel.");

    m_addFilesBtn->setEnabled(!m_isRunning && !pausedForDisk);
    m_addFolderBtn->setEnabled(!m_isRunning && !pausedForDisk);
    m_outputDirEdit->setEnabled(!m_isRunning && !pausedForDisk && !m_sameAsInputCheck->isChecked());
    m_browseOutputBtn->setEnabled(!m_isRunning && !pausedForDisk && !m_sameAsInputCheck->isChecked());
    m_sameAsInputCheck->setEnabled(!m_isRunning && !pausedForDisk);
    m_modeCombo->setEnabled(!m_isRunning && !pausedForDisk);
    m_memberListWidget->setEnabled(!m_isRunning && !pausedForDisk);
    m_selectAllMembersBtn->setEnabled(!m_isRunning && !pausedForDisk);
    m_deselectAllMembersBtn->setEnabled(!m_isRunning && !pausedForDisk);
    m_groupQuickSelectCombo->setEnabled(!m_isRunning && !pausedForDisk);
    m_memberSearchEdit->setEnabled(!m_isRunning && !pausedForDisk);
    m_primaryTextEdit->setEnabled(!m_isRunning && !pausedForDisk);
    m_secondaryTextEdit->setEnabled(!m_isRunning && !pausedForDisk);
    m_presetCombo->setEnabled(!m_isRunning && !pausedForDisk);
    m_crfSpin->setEnabled(!m_isRunning && !pausedForDisk);
    m_intervalSpin->setEnabled(!m_isRunning && !pausedForDisk);
    m_durationSpin->setEnabled(!m_isRunning && !pausedForDisk);
    if (m_fastSegmentedCheck) {
        m_fastSegmentedCheck->setEnabled(!m_isRunning && !pausedForDisk);
    }
    m_settingsBtn->setEnabled(!m_isRunning && !pausedForDisk);
    m_presetNameCombo->setEnabled(!m_isRunning && !pausedForDisk);
    m_savePresetBtn->setEnabled(!m_isRunning && !pausedForDisk);
    m_deletePresetBtn->setEnabled(!m_isRunning && !pausedForDisk);
    m_watermarkHelpBtn->setEnabled(!m_isRunning && !pausedForDisk);
    m_watermarkPreviewBtn->setEnabled(!m_isRunning && !pausedForDisk);
    if (m_autoUploadCheck) {
        const bool memberMode = m_modeCombo->currentData().toString() != "global";
        m_autoUploadCheck->setEnabled(!m_isRunning && !pausedForDisk && memberMode);
    }
    if (m_customPathCheck) {
        m_customPathCheck->setEnabled(!m_isRunning && !pausedForDisk
            && m_autoUploadCheck && m_autoUploadCheck->isChecked());
    }
    if (m_customPathEdit) {
        m_customPathEdit->setEnabled(!m_isRunning && !pausedForDisk
            && m_autoUploadCheck && m_autoUploadCheck->isChecked()
            && m_customPathCheck && m_customPathCheck->isChecked());
    }
    if (m_browseCustomPathBtn) {
        m_browseCustomPathBtn->setEnabled(!m_isRunning && !pausedForDisk
            && m_autoUploadCheck && m_autoUploadCheck->isChecked()
            && m_customPathCheck && m_customPathCheck->isChecked());
    }
    m_embedMetadataCheck->setEnabled(!m_isRunning && !pausedForDisk);
    bool metaEnabled = !m_isRunning && !pausedForDisk && m_embedMetadataCheck->isChecked();
    m_metaTitleEdit->setEnabled(metaEnabled);
    m_metaAuthorEdit->setEnabled(metaEnabled);
    m_metaCommentEdit->setEnabled(metaEnabled);
    m_metaKeywordsEdit->setEnabled(metaEnabled);
    updateSafetyActionStates();
}

WatermarkConfig WatermarkPanel::buildConfig() const {
    WatermarkConfig config;

    QString primaryText = m_primaryTextEdit->text();
    QString secondaryText = m_secondaryTextEdit->text();

    if (m_modeCombo->currentData().toString() == "global") {
        // Global mode: expand templates now (no per-member expansion needed)
        if (TemplateExpander::hasVariables(primaryText) || TemplateExpander::hasVariables(secondaryText)) {
            auto vars = TemplateExpander::Variables::withCurrentDateTime();
            vars.brand = QString::fromUtf8(MegaCustom::Constants::BRAND_NAME);
            primaryText = TemplateExpander::expand(primaryText, vars);
            secondaryText = TemplateExpander::expand(secondaryText, vars);
        }
        config.primaryText = primaryText.toStdString();
        config.secondaryText = secondaryText.toStdString();
    } else {
        // Member mode: store raw text — worker expands per-member with TemplateExpander
        config.primaryText = primaryText.toStdString();
        config.secondaryText = secondaryText.toStdString();
    }

    config.preset = m_presetCombo->currentText().toStdString();
    config.crf = m_crfSpin->value();
    config.intervalSeconds = m_intervalSpin->value();
    config.durationSeconds = m_durationSpin->value();
    config.fastSegmentedEncode = m_fastSegmentedCheck && m_fastSegmentedCheck->isChecked();

    // Metadata embedding
    config.embedMetadata = m_embedMetadataCheck->isChecked();
    if (config.embedMetadata) {
        config.metadataTitle = m_metaTitleEdit->text().toStdString();
        config.metadataAuthor = m_metaAuthorEdit->text().toStdString();
        config.metadataComment = m_metaCommentEdit->text().toStdString();
        config.metadataKeywords = m_metaKeywordsEdit->text().toStdString();

        // In global mode, expand metadata templates now
        if (m_modeCombo->currentData().toString() == "global") {
            auto vars = TemplateExpander::Variables::withCurrentDateTime();
            vars.brand = QString::fromUtf8(MegaCustom::Constants::BRAND_NAME);
            config.metadataTitle = TemplateExpander::expand(
                m_metaTitleEdit->text(), vars).toStdString();
            config.metadataAuthor = TemplateExpander::expand(
                m_metaAuthorEdit->text(), vars).toStdString();
            config.metadataComment = TemplateExpander::expand(
                m_metaCommentEdit->text(), vars).toStdString();
            config.metadataKeywords = TemplateExpander::expand(
                m_metaKeywordsEdit->text(), vars).toStdString();
        }
    }

    return config;
}

QString WatermarkPanel::formatFileSize(qint64 bytes) const {
    if (bytes < 1024) {
        return QString::number(bytes) + " B";
    } else if (bytes < 1024 * 1024) {
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    } else if (bytes < 1024 * 1024 * 1024) {
        return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    } else {
        return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
    }
}

void WatermarkPanel::onSendToDistribution() {
    QStringList failedRows;
    int completedRows = 0;
    for (const WatermarkFileInfo& info : m_files) {
        if (info.isHeader) {
            continue;
        }
        const bool completeMissingLocalOutput = hasMissingCompletedOutput(info);
        if (!completeMissingLocalOutput && hasTrustedWatermarkOutput(info)) {
            completedRows++;
        } else if (info.status == "error" || completeMissingLocalOutput) {
            failedRows.append(QString("%1%2")
                .arg(info.memberId.isEmpty() ? QString() : QString("%1: ").arg(info.memberId))
                .arg(info.fileName));
        }
    }
    if (!failedRows.isEmpty()) {
        const QString reportPath = writeWatermarkCompletionReport(completedRows, failedRows.size());
        QMessageBox::warning(this, "Watermark Session Incomplete",
            QString("This session still has %1 failed watermark row(s), so sending to Distribution is blocked.\n\n"
                    "Fix or retry the failed rows before uploading member folders.\n\n"
                    "Report:\n%2\n\n%3")
                .arg(failedRows.size())
                .arg(reportPath)
                .arg(failedRows.mid(0, 30).join("\n")));
        return;
    }

    // If we have a member file map from multi-member watermarking, send that
    if (!m_lastMemberFileMap.isEmpty()) {
        QMap<QString, QStringList> runnableMap;
        QStringList missingFiles;

        for (auto it = m_lastMemberFileMap.constBegin(); it != m_lastMemberFileMap.constEnd(); ++it) {
            QStringList existingFiles;
            QStringList missingForMember;
            for (const QString& path : it.value()) {
                if (QFileInfo::exists(path)) {
                    existingFiles.append(path);
                } else {
                    missingForMember.append(path);
                    missingFiles.append(path);
                }
            }

            if (!missingForMember.isEmpty()) {
                continue;
            }
            if (!existingFiles.isEmpty()) {
                runnableMap[it.key()] = existingFiles;
            }
        }

        if (!missingFiles.isEmpty()) {
            QMessageBox::warning(this, "Missing Watermarked Files",
                QString("%1 local watermarked file(s) no longer exist. "
                        "Members with missing files were not sent to Distribution to avoid partial uploads:\n\n%2")
                    .arg(missingFiles.size())
                    .arg(missingFiles.mid(0, 30).join("\n")));
        }

        if (runnableMap.isEmpty()) {
            QMessageBox::information(this, "No Files",
                "No complete local member batches are available to send to Distribution.");
            return;
        }

        m_statusLabel->setText(QString("Sending files for %1 members to Distribution...")
            .arg(runnableMap.size()));
        emit sendToDistributionMapped(runnableMap);
        m_lastMemberFileMap.clear();
        m_sendToDistBtn->setEnabled(false);
        return;
    }

    // Otherwise send flat file list (global/single-member mode)
    QStringList completedFiles;
    QStringList missingFiles;
    for (const WatermarkFileInfo& info : m_files) {
        if (info.status == "complete" && !info.outputPath.isEmpty()) {
            if (QFileInfo::exists(info.outputPath)) {
                completedFiles.append(info.outputPath);
            } else {
                missingFiles.append(info.outputPath);
            }
        }
    }

    if (!missingFiles.isEmpty()) {
        QMessageBox::warning(this, "Missing Watermarked Files",
            QString("%1 completed local file(s) no longer exist and were skipped:\n\n%2")
                .arg(missingFiles.size())
                .arg(missingFiles.mid(0, 30).join("\n")));
    }

    if (completedFiles.isEmpty()) {
        QMessageBox::information(this, "No Files",
            "No completed watermarked files to send to Distribution.");
        return;
    }

    m_statusLabel->setText(QString("Sending %1 %2 to Distribution...").arg(completedFiles.size()).arg(completedFiles.size() == 1 ? "file" : "files"));
    emit sendToDistribution(completedFiles);
}

void WatermarkPanel::onWatermarkHelpClicked() {
    QString helpText = R"(
<h3>Watermark Template Variables</h3>
<p>Use these placeholders in your watermark text:</p>
<table style="margin-left: 10px;">
<tr><td><b>{brand}</b></td><td>Brand name (Easygroupbuys.com)</td></tr>
<tr><td colspan="2"><b>— Member Variables —</b></td></tr>
<tr><td><b>{member}</b></td><td>Member's distribution folder path</td></tr>
<tr><td><b>{member_id}</b></td><td>Member's unique ID</td></tr>
<tr><td><b>{member_name}</b></td><td>Member's display name</td></tr>
<tr><td><b>{member_email}</b></td><td>Member's email address</td></tr>
<tr><td><b>{member_ip}</b></td><td>Member's IP address</td></tr>
<tr><td><b>{member_mac}</b></td><td>Member's MAC address</td></tr>
<tr><td><b>{member_social}</b></td><td>Member's social media handle</td></tr>
<tr><td colspan="2"><b>— Date/Time Variables —</b></td></tr>
<tr><td><b>{month}</b></td><td>Current month name (e.g., December)</td></tr>
<tr><td><b>{month_num}</b></td><td>Current month number (01-12)</td></tr>
<tr><td><b>{year}</b></td><td>Current year (e.g., 2025)</td></tr>
<tr><td><b>{date}</b></td><td>Current date (YYYY-MM-DD)</td></tr>
<tr><td><b>{timestamp}</b></td><td>Current timestamp (YYYYMMDD_HHMMSS)</td></tr>
</table>
<br>
<p><b>Examples:</b></p>
<p><i>Primary:</i> <code>{brand} - {member_name} ({member_id})</code></p>
<p><i>Secondary:</i> <code>{member_email} - {date}</code></p>
<br>
<p><b>Note:</b> In Per-Member mode, all member variables are expanded per-member.
{brand} and date/time variables work in both Global and Per-Member modes.</p>
)";

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Watermark Template Variables");
    msgBox.setTextFormat(Qt::RichText);
    msgBox.setText(helpText);
    msgBox.setIcon(QMessageBox::Information);
    msgBox.exec();
}

void WatermarkPanel::loadPresets() {
    QSettings settings;
    settings.beginGroup("WatermarkPresets");
    QStringList presets = settings.childGroups();
    settings.endGroup();

    // Block signals while populating
    m_presetNameCombo->blockSignals(true);

    // Clear all except the default item
    while (m_presetNameCombo->count() > 1) {
        m_presetNameCombo->removeItem(1);
    }

    // Add saved presets
    for (const QString& preset : presets) {
        m_presetNameCombo->addItem(preset, preset);
    }

    m_presetNameCombo->blockSignals(false);
}

void WatermarkPanel::applyPreset(const QString& presetName) {
    if (presetName.isEmpty()) return;

    QSettings settings;
    settings.beginGroup("WatermarkPresets/" + presetName);

    m_primaryTextEdit->setText(settings.value("primaryText").toString());
    m_secondaryTextEdit->setText(settings.value("secondaryText").toString());
    m_presetCombo->setCurrentText(settings.value("ffmpegPreset", "ultrafast").toString());
    m_crfSpin->setValue(settings.value("crf", 23).toInt());
    m_intervalSpin->setValue(settings.value("interval", 600).toInt());
    m_durationSpin->setValue(settings.value("duration", 3).toInt());
    if (m_fastSegmentedCheck) {
        m_fastSegmentedCheck->setChecked(settings.value("fastSegmentedEncode", false).toBool());
    }

    // Metadata settings
    m_embedMetadataCheck->setChecked(settings.value("embedMetadata", false).toBool());
    m_metaTitleEdit->setText(settings.value("metadataTitle", "{brand} - {member_name}").toString());
    m_metaAuthorEdit->setText(settings.value("metadataAuthor", "{member_name} ({member_id})").toString());
    m_metaCommentEdit->setText(settings.value("metadataComment", "ID: {member_id} | {member_email}").toString());
    m_metaKeywordsEdit->setText(settings.value("metadataKeywords", "{member_id}, {member_email}").toString());

    settings.endGroup();
}

void WatermarkPanel::onSavePreset() {
    bool ok;
    QString presetName = QInputDialog::getText(this, "Save Preset",
        "Enter preset name:", QLineEdit::Normal, "", &ok);

    if (!ok || presetName.trimmed().isEmpty()) return;

    presetName = presetName.trimmed();

    QSettings settings;
    settings.beginGroup("WatermarkPresets/" + presetName);
    settings.setValue("primaryText", m_primaryTextEdit->text());
    settings.setValue("secondaryText", m_secondaryTextEdit->text());
    settings.setValue("ffmpegPreset", m_presetCombo->currentText());
    settings.setValue("crf", m_crfSpin->value());
    settings.setValue("interval", m_intervalSpin->value());
    settings.setValue("duration", m_durationSpin->value());
    settings.setValue("fastSegmentedEncode", m_fastSegmentedCheck && m_fastSegmentedCheck->isChecked());

    // Metadata settings
    settings.setValue("embedMetadata", m_embedMetadataCheck->isChecked());
    settings.setValue("metadataTitle", m_metaTitleEdit->text());
    settings.setValue("metadataAuthor", m_metaAuthorEdit->text());
    settings.setValue("metadataComment", m_metaCommentEdit->text());
    settings.setValue("metadataKeywords", m_metaKeywordsEdit->text());
    settings.endGroup();

    // Reload and select the new preset
    loadPresets();
    int idx = m_presetNameCombo->findData(presetName);
    if (idx >= 0) {
        m_presetNameCombo->setCurrentIndex(idx);
    }

    QMessageBox::information(this, "Preset Saved",
        QString("Preset '%1' has been saved.").arg(presetName));
}

void WatermarkPanel::onDeletePreset() {
    QString presetName = m_presetNameCombo->currentData().toString();
    if (presetName.isEmpty()) return;

    int result = QMessageBox::question(this, "Delete Preset",
        QString("Are you sure you want to delete preset '%1'?").arg(presetName),
        QMessageBox::Yes | QMessageBox::No);

    if (result != QMessageBox::Yes) return;

    QSettings settings;
    settings.remove("WatermarkPresets/" + presetName);

    loadPresets();
    m_presetNameCombo->setCurrentIndex(0);

    QMessageBox::information(this, "Preset Deleted",
        QString("Preset '%1' has been deleted.").arg(presetName));
}

void WatermarkPanel::onPresetChanged(int index) {
    QString presetName = m_presetNameCombo->itemData(index).toString();
    m_deletePresetBtn->setEnabled(!presetName.isEmpty());

    if (!presetName.isEmpty()) {
        applyPreset(presetName);
    }
}

void WatermarkPanel::onPreviewWatermarkClicked() {
    QString primaryText = m_primaryTextEdit->text();
    QString secondaryText = m_secondaryTextEdit->text();

    if (primaryText.isEmpty() && secondaryText.isEmpty()) {
        QMessageBox::information(this, "Preview",
            "Enter watermark text to preview.\nUse template variables like {member_name}, {date}, etc.");
        return;
    }

    // Build variables based on current mode
    TemplateExpander::Variables vars;
    QString memberInfo;
    QString mode;

    QStringList selectedIds = getSelectedMemberIds();
    if (m_modeCombo->currentData().toString() == "member" && selectedIds.size() == 1) {
        MemberInfo member = m_registry->getMember(selectedIds.first());
        if (!member.id.isEmpty()) {
            vars = TemplateExpander::Variables::fromMember(member);
            memberInfo = QString("<b>Member:</b> %1 (%2)").arg(member.displayName).arg(member.id);
            mode = "Per-Member Mode";
        } else {
            vars = TemplateExpander::Variables::withCurrentDateTime();
            memberInfo = "<i>Member not found - using date/time only</i>";
            mode = "Per-Member Mode (member not found)";
        }
    } else if (m_modeCombo->currentData().toString() == "member" && selectedIds.size() > 1) {
        vars = TemplateExpander::Variables::withCurrentDateTime();
        memberInfo = QString("<i>%1 members selected - member variables expand per-member during processing</i>").arg(selectedIds.size());
        mode = "Per-Member Mode (multi)";
    } else {
        vars = TemplateExpander::Variables::withCurrentDateTime();
        memberInfo = "<i>No member selected - using date/time only</i>";
        mode = "Global Mode";
    }

    // Expand templates
    QString expandedPrimary = TemplateExpander::expand(primaryText, vars);
    QString expandedSecondary = TemplateExpander::expand(secondaryText, vars);

    // Build preview dialog
    auto& tmPreview = ThemeManager::instance();
    QString bgColor = tmPreview.borderSubtle().name();
    QString tmplColor = tmPreview.textSecondary().name();
    QString previewText = QString(R"(
<h3>Watermark Preview</h3>
<p><b>Mode:</b> %1</p>
<p>%2</p>
<hr>
<table style="width: 100%;">
<tr>
    <td style="width: 100px;"><b>Primary Text:</b></td>
    <td style="background: %7; padding: 8px; border-radius: 4px;">
        <code>%3</code>
    </td>
</tr>
<tr><td colspan="2" style="height: 8px;"></td></tr>
<tr>
    <td><b>Template:</b></td>
    <td style="color: %8;"><i>%4</i></td>
</tr>
<tr><td colspan="2" style="height: 16px;"></td></tr>
<tr>
    <td><b>Secondary Text:</b></td>
    <td style="background: %7; padding: 8px; border-radius: 4px;">
        <code>%5</code>
    </td>
</tr>
<tr><td colspan="2" style="height: 8px;"></td></tr>
<tr>
    <td><b>Template:</b></td>
    <td style="color: %8;"><i>%6</i></td>
</tr>
</table>
)")
        .arg(mode)
        .arg(memberInfo)
        .arg(expandedPrimary.isEmpty() ? "<i>(empty)</i>" : expandedPrimary.toHtmlEscaped())
        .arg(primaryText.isEmpty() ? "<i>(empty)</i>" : primaryText.toHtmlEscaped())
        .arg(expandedSecondary.isEmpty() ? "<i>(empty)</i>" : expandedSecondary.toHtmlEscaped())
        .arg(secondaryText.isEmpty() ? "<i>(empty)</i>" : secondaryText.toHtmlEscaped())
        .arg(bgColor)
        .arg(tmplColor);

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Watermark Preview");
    msgBox.setTextFormat(Qt::RichText);
    msgBox.setText(previewText);
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setMinimumWidth(500);
    msgBox.exec();
}

// ==================== Smart Engine ====================

void WatermarkPanel::setMegaApi(mega::MegaApi* api) {
    m_megaApi = api;
}

void WatermarkPanel::setMetricsStore(MetricsStore* store) {
    m_metricsStore = store;
}

void WatermarkPanel::updateSmartEstimate() {
    if (!m_smartEstimateLabel) return;

    if (m_files.isEmpty() || !m_metricsStore) {
        m_smartEstimateLabel->clear();
        return;
    }

    bool isGlobal = m_modeCombo->currentData().toString() == "global";
    QStringList memberIds = isGlobal ? QStringList() : getSelectedMemberIds();

    qint64 totalInput = 0;
    // Deduplicate files (m_files may be expanded with per-member entries)
    QSet<QString> seenPaths;
    for (const auto& fi : m_files) {
        if (!seenPaths.contains(fi.filePath)) {
            seenPaths.insert(fi.filePath);
            totalInput += fi.fileSize;
        }
    }

    if (totalInput == 0) {
        m_smartEstimateLabel->clear();
        return;
    }

    int memberCount = memberIds.isEmpty() ? 1 : memberIds.size();
    QString ext = QFileInfo(m_files.first().filePath).suffix().toLower();

    qint64 perMember = m_metricsStore->predictOutputSize(ext, totalInput);
    qint64 totalOutput = perMember * memberCount;
    qint64 wmDuration = m_metricsStore->predictWatermarkDuration(ext, totalInput) * memberCount;
    qint64 uploadDuration = m_metricsStore->predictUploadDuration(totalOutput);

    int ops = m_metricsStore->totalOperations();
    QString learnLabel = ops < 5 ? "learning..." : ops < 20 ? "improving" : "confident";

    // Format duration
    auto fmtDur = [](qint64 ms) -> QString {
        if (ms < 60000) return QString("%1s").arg(ms / 1000);
        if (ms < 3600000) return QString("%1m %2s").arg(ms / 60000).arg((ms % 60000) / 1000);
        return QString("%1h %2m").arg(ms / 3600000).arg((ms % 3600000) / 60000);
    };

    // Get disk space
    QString outputPath = m_sameAsInputCheck->isChecked()
        ? QFileInfo(m_files.first().filePath).path() : m_outputDirEdit->text();
    QStorageInfo storage(outputPath);
    qint64 available = storage.bytesAvailable();

    m_smartEstimateLabel->setText(
        QString("Est: %1 output | %2 watermark + %3 upload | Disk: %4 free | AI: %5 (%6 ops)")
            .arg(formatFileSize(totalOutput))
            .arg(fmtDur(wmDuration))
            .arg(fmtDur(uploadDuration))
            .arg(formatFileSize(available))
            .arg(learnLabel)
            .arg(ops));
}

} // namespace MegaCustom
