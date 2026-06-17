#include "DistributionPanel.h"
#include "EmptyStateWidget.h"
#include "utils/MemberRegistry.h"
#include "utils/TemplateExpander.h"
#include "utils/CopyHelper.h"
#include "utils/CloudPathValidator.h"
#include "utils/ContentRouter.h"
#include "utils/MegaUploadUtils.h"
#include "utils/DpiScaler.h"
#include "utils/OperationJobStore.h"
#include "widgets/ButtonFactory.h"
#include "widgets/SwitchButton.h"
#include "dialogs/SmartRouteReviewDialog.h"
#include "core/LogManager.h"
#include <QMenu>
#include <QScreen>
#include <QGuiApplication>
#include <QDialog>
#include "controllers/FileController.h"
#include "controllers/DistributionController.h"
#include "features/CloudCopier.h"
#include <megaapi.h>
#include <QtConcurrent>
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QRegularExpression>
#include <QDateTime>
#include <QFileInfo>
#include <QTimer>
#include <QScrollArea>
#include <QStyle>
#include <QMutex>
#include "utils/AnimationHelper.h"
#include "styles/ThemeManager.h"
#include <QWaitCondition>
#include <QDebug>
#include <QClipboard>
#include <QFileDialog>
#include <QTextEdit>
#include <QFont>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QInputDialog>

namespace MegaCustom {

// ==================== Copy Task Structure ====================

struct FolderCopyTask {
    int index = 0;
    QString sourcePath;
    QString destPath;
    QString memberId;
    bool copyFolderItself = false;
    bool isSmartRouteChild = false;   // true = individual child item from smart routing
    QStringList individualFiles;      // for NHB_ROOT_FILES: list of file paths to copy
    QString contentTypeLabel;         // for status display

    // Local source support — true when uploading from local filesystem
    bool isLocalSource = false;
};

static QJsonArray stringListToJsonArray(const QStringList& values) {
    QJsonArray array;
    for (const QString& value : values) {
        array.append(value);
    }
    return array;
}

static QStringList stringListFromJsonArray(const QJsonArray& array) {
    QStringList values;
    for (const QJsonValue& value : array) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty()) {
            values.append(text);
        }
    }
    return values;
}

static QJsonObject folderCopyTaskToJson(const FolderCopyTask& task) {
    QJsonObject object;
    object["index"] = task.index;
    object["sourcePath"] = task.sourcePath;
    object["destPath"] = task.destPath;
    object["memberId"] = task.memberId;
    object["copyFolderItself"] = task.copyFolderItself;
    object["isSmartRouteChild"] = task.isSmartRouteChild;
    object["individualFiles"] = stringListToJsonArray(task.individualFiles);
    object["contentTypeLabel"] = task.contentTypeLabel;
    object["isLocalSource"] = task.isLocalSource;
    return object;
}

static FolderCopyTask folderCopyTaskFromJson(const QJsonObject& object, int fallbackIndex) {
    FolderCopyTask task;
    task.index = object["index"].toInt(fallbackIndex);
    task.sourcePath = object["sourcePath"].toString();
    task.destPath = object["destPath"].toString();
    task.memberId = object["memberId"].toString();
    task.copyFolderItself = object["copyFolderItself"].toBool(false);
    task.isSmartRouteChild = object["isSmartRouteChild"].toBool(false);
    task.individualFiles = stringListFromJsonArray(object["individualFiles"].toArray());
    task.contentTypeLabel = object["contentTypeLabel"].toString();
    task.isLocalSource = object["isLocalSource"].toBool(false);
    return task;
}

static QJsonArray folderCopyTasksToJson(const QList<FolderCopyTask>& tasks) {
    QJsonArray array;
    for (const FolderCopyTask& task : tasks) {
        array.append(folderCopyTaskToJson(task));
    }
    return array;
}

static QList<FolderCopyTask> folderCopyTasksFromJson(const QJsonArray& array) {
    QList<FolderCopyTask> tasks;
    for (int i = 0; i < array.size(); ++i) {
        if (!array[i].isObject()) {
            continue;
        }
        tasks.append(folderCopyTaskFromJson(array[i].toObject(), i));
    }
    return tasks;
}

static QJsonArray memberFileMapToJson(const QMap<QString, QStringList>& memberFileMap) {
    QJsonArray array;
    for (auto it = memberFileMap.constBegin(); it != memberFileMap.constEnd(); ++it) {
        QJsonObject object;
        object["memberId"] = it.key();
        object["files"] = stringListToJsonArray(it.value());
        array.append(object);
    }
    return array;
}

static QMap<QString, QStringList> memberFileMapFromJson(const QJsonArray& array) {
    QMap<QString, QStringList> memberFileMap;
    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        const QString memberId = object["memberId"].toString().trimmed();
        if (memberId.isEmpty()) {
            continue;
        }
        const QStringList files = stringListFromJsonArray(object["files"].toArray());
        if (!files.isEmpty()) {
            memberFileMap[memberId] = files;
        }
    }
    return memberFileMap;
}

struct UploadMapValidation {
    QMap<QString, QStringList> memberFileMap;
    QStringList missingFiles;
    QStringList membersWithoutFolder;
};

static UploadMapValidation validateMemberFileMapForUpload(const QMap<QString, QStringList>& input,
                                                          MemberRegistry* registry) {
    UploadMapValidation validation;
    for (auto it = input.constBegin(); it != input.constEnd(); ++it) {
        const QString& memberId = it.key();
        const MemberInfo memberInfo = registry ? registry->getMember(memberId) : MemberInfo{};
        const QString memberLabel = memberInfo.displayName.isEmpty()
            ? memberId
            : QString("%1 (%2)").arg(memberInfo.displayName, memberId);

        if (memberInfo.distributionFolder.trimmed().isEmpty()) {
            validation.membersWithoutFolder.append(memberLabel);
            continue;
        }

        QStringList existingFiles;
        QStringList missingForMember;
        for (const QString& path : it.value()) {
            if (QFileInfo::exists(path)) {
                existingFiles.append(path);
            } else {
                missingForMember.append(path);
            }
        }

        if (!missingForMember.isEmpty()) {
            validation.missingFiles.append(missingForMember);
            continue;
        }

        if (!existingFiles.isEmpty()) {
            validation.memberFileMap[memberId] = existingFiles;
        }
    }
    return validation;
}

// ==================== FolderCopyWorker ====================

class FolderCopyWorker : public QObject {
    Q_OBJECT
public:
    explicit FolderCopyWorker(QObject* parent = nullptr)
        : QObject(parent), m_cancelled(false), m_paused(false) {}

    void setTasks(const QList<FolderCopyTask>& tasks) { m_tasks = tasks; }
    void setCloudCopier(CloudCopier* copier) { m_cloudCopier = copier; }
    void setMegaApi(mega::MegaApi* api) { m_megaApi = api; }
    void setSkipExisting(bool skip) { m_skipExisting = skip; }
    void setCreateDestFolder(bool create) { m_createDestFolder = create; }
    void setMoveMode(bool move) { m_moveMode = move; }

public slots:
    void process() {
        // Detect whether this batch contains any cloud tasks — only require CloudCopier if so
        bool anyCloudTask = false;
        for (const auto& t : m_tasks) {
            if (!t.isLocalSource) { anyCloudTask = true; break; }
        }
        if (!m_megaApi) {
            emit error("MegaApi not available");
            emit allCompleted(0, m_tasks.size());
            return;
        }
        if (anyCloudTask && !m_cloudCopier) {
            emit error("CloudCopier not available");
            emit allCompleted(0, m_tasks.size());
            return;
        }

        if (m_cloudCopier) {
            // Set conflict resolution
            m_cloudCopier->setDefaultConflictResolution(
                m_skipExisting ? ConflictResolution::SKIP : ConflictResolution::OVERWRITE);

            // Set operation mode (copy or move)
            m_cloudCopier->setOperationMode(
                m_moveMode ? OperationMode::MOVE : OperationMode::COPY);
        }

        int success = 0, failed = 0;

        for (int i = 0; i < m_tasks.size(); ++i) {
            // Check for cancellation
            {
                QMutexLocker locker(&m_mutex);
                if (m_cancelled) {
                    emit allCompleted(success, failed + (m_tasks.size() - i));
                    return;
                }
                while (m_paused && !m_cancelled) {
                    m_pauseCondition.wait(&m_mutex, 100);
                }
            }

            const FolderCopyTask& task = m_tasks[i];
            QString opMode = m_moveMode ? "MOVE" : "COPY";
            QString detail = QString("%1 %2 -> %3 [%4] files:%5")
                .arg(opMode).arg(task.sourcePath).arg(task.destPath)
                .arg(task.copyFolderItself ? "folder" : "contents")
                .arg(task.individualFiles.size());
            qDebug() << "Worker: Processing task" << i << ":" << detail;
            LogManager::instance().logDistribution("task_start", detail.toStdString(),
                "", task.memberId.toStdString());
            emit taskStarted(task.index, task.sourcePath, task.destPath);
            emit progress(i + 1, m_tasks.size(), task.memberId);

            bool taskSuccess = false;
            QString errorMsg;

            // Create destination folder if needed
            if (m_createDestFolder) {
                const bool destExistedBefore = remoteFolderExists(task.destPath);
                if (task.isLocalSource) {
                    // megaApiUpload ensures the folder exists, but do it upfront so
                    // empty uploads still create the expected destination.
                    std::unique_ptr<mega::MegaNode> created(
                        ensureFolderExists(m_megaApi, task.destPath.toStdString()));
                } else if (m_cloudCopier) {
                    m_cloudCopier->createDestinations({task.destPath.toStdString()});
                }
                emitCreatedFolderIfNew(task.index, task.destPath, destExistedBefore);
            }

            if (task.isLocalSource) {
                // Local → MEGA upload
                taskSuccess = processLocalUpload(task, errorMsg);
            } else if (task.isSmartRouteChild && !task.individualFiles.isEmpty()) {
                // Smart route: copy individual files to destination
                int fileSuccess = 0, fileFailed = 0;
                QStringList failedNames;
                for (const QString& filePath : task.individualFiles) {
                    CopyResult result = m_moveMode
                        ? m_cloudCopier->moveTo(filePath.toStdString(), task.destPath.toStdString())
                        : m_cloudCopier->copyTo(filePath.toStdString(), task.destPath.toStdString());
                    if (result.success) {
                        fileSuccess++;
                    } else {
                        fileFailed++;
                        failedNames.append(filePath.section('/', -1));
                    }
                }
                taskSuccess = (fileFailed == 0);
                if (fileFailed > 0) {
                    errorMsg = QString("%1/%2 files failed: %3")
                        .arg(fileFailed).arg(task.individualFiles.size())
                        .arg(failedNames.join(", ").left(200));
                }
            } else if (task.copyFolderItself) {
                // Copy/move the entire folder (operation mode already set on CloudCopier)
                CopyResult result = m_moveMode
                    ? m_cloudCopier->moveTo(task.sourcePath.toStdString(), task.destPath.toStdString())
                    : m_cloudCopier->copyTo(task.sourcePath.toStdString(), task.destPath.toStdString());
                taskSuccess = result.success;
                if (!taskSuccess) {
                    errorMsg = QString::fromStdString(result.errorMessage);
                }
            } else {
                // Copy/move folder contents
                taskSuccess = processFolderContents(task.sourcePath, task.destPath, errorMsg);
            }

            if (taskSuccess) {
                success++;
                qDebug() << "Worker: Task" << i << "SUCCESS";
                LogManager::instance().logDistribution("task_success",
                    (task.memberId + " <- " + task.sourcePath + " -> " + task.destPath).toStdString(),
                    "", task.memberId.toStdString());
            } else {
                failed++;
                qDebug() << "Worker: Task" << i << "FAILED:" << errorMsg;
                LogManager::instance().error(LogCategory::Distribution, "task_failed",
                    (task.memberId + " | " + task.sourcePath + " -> " + task.destPath + " | " + errorMsg).toStdString());
            }

            emit taskCompleted(task.index, taskSuccess, errorMsg);
        }

        qDebug() << "Worker: All tasks complete. Success:" << success << "Failed:" << failed;
        LogManager::instance().logDistribution("all_completed",
            ("Distribution complete. Success: " + std::to_string(success) + ", Failed: " + std::to_string(failed)));
        emit allCompleted(success, failed);
    }

    void cancel() {
        QMutexLocker locker(&m_mutex);
        m_cancelled = true;
        m_paused = false;  // Ensure we exit pause state to avoid race condition
        m_pauseCondition.wakeAll();
    }

    void pause() {
        QMutexLocker locker(&m_mutex);
        m_paused = true;
    }

    void resume() {
        QMutexLocker locker(&m_mutex);
        m_paused = false;
        m_pauseCondition.wakeAll();
    }

signals:
    void taskStarted(int index, const QString& source, const QString& dest);
    void taskCompleted(int index, bool success, const QString& error);
    void destinationFolderCreated(int index, const QString& dest);
    void remoteFileUploaded(int index,
                            const QString& memberId,
                            const QString& localPath,
                            const QString& remoteFolderPath,
                            const QString& remoteFilePath,
                            const QString& fileName);
    void allCompleted(int success, int failed);
    void progress(int current, int total, const QString& currentItem);
    void error(const QString& message);

private:
    bool remoteFolderExists(const QString& path) const {
        if (!m_megaApi || path.trimmed().isEmpty()) {
            return false;
        }
        std::unique_ptr<mega::MegaNode> node(
            m_megaApi->getNodeByPath(path.toUtf8().constData()));
        return node && node->isFolder();
    }

    void emitCreatedFolderIfNew(int index, const QString& path, bool existedBefore) {
        if (existedBefore || path.trimmed().isEmpty() || !remoteFolderExists(path)) {
            return;
        }
        emit destinationFolderCreated(index, QDir::cleanPath(path));
    }

    bool processFolderContents(const QString& sourcePath, const QString& destPath, QString& errorOut) {
        if (!m_megaApi || !m_cloudCopier) return false;

        mega::MegaNode* sourceNode = m_megaApi->getNodeByPath(sourcePath.toUtf8().constData());
        if (!sourceNode) {
            errorOut = "Source folder not found";
            return false;
        }

        mega::MegaNodeList* children = m_megaApi->getChildren(sourceNode);
        if (!children) {
            delete sourceNode;
            errorOut = "Could not get folder contents";
            return false;
        }

        bool allSuccess = true;
        for (int i = 0; i < children->size(); ++i) {
            // Check for cancellation
            {
                QMutexLocker locker(&m_mutex);
                if (m_cancelled) {
                    delete children;
                    delete sourceNode;
                    errorOut = "Operation cancelled";
                    return false;
                }
            }

            mega::MegaNode* child = children->get(i);
            if (child) {
                QString childPath = sourcePath + "/" + QString::fromUtf8(child->getName());
                // Use moveTo or copyTo based on operation mode
                CopyResult result = m_moveMode
                    ? m_cloudCopier->moveTo(childPath.toStdString(), destPath.toStdString())
                    : m_cloudCopier->copyTo(childPath.toStdString(), destPath.toStdString());
                if (!result.success) {
                    allSuccess = false;
                    errorOut = QString::fromStdString(result.errorMessage);
                }
            }
        }

        delete children;
        delete sourceNode;
        return allSuccess;
    }

    bool processLocalUpload(const FolderCopyTask& task, QString& errorOut) {
        if (!m_megaApi) {
            errorOut = "MegaApi not available";
            return false;
        }

        // Build the list of local files to upload, along with their relative subpath
        // (relative to task.sourcePath) so we preserve folder structure when needed.
        struct UploadItem {
            QString localPath;
            QString relativeDir;  // relative subfolder within sourcePath ("" = root)
        };
        QList<UploadItem> items;

        if (task.isSmartRouteChild && !task.individualFiles.isEmpty()) {
            // Smart route: specific files → flat upload to destPath
            for (const QString& f : task.individualFiles) {
                items.append({ f, QString() });
            }
        } else if (task.copyFolderItself) {
            // Full recursive upload — preserve subfolder structure
            QDir root(task.sourcePath);
            QDirIterator it(task.sourcePath, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                QString filePath = it.next();
                QString rel = root.relativeFilePath(QFileInfo(filePath).absolutePath());
                if (rel == ".") rel.clear();
                items.append({ filePath, rel });
            }
        } else {
            // Copy contents only — direct children files (non-recursive)
            QDir dir(task.sourcePath);
            const QFileInfoList entries = dir.entryInfoList(QDir::Files);
            for (const QFileInfo& fi : entries) {
                items.append({ fi.absoluteFilePath(), QString() });
            }
        }

        if (items.isEmpty()) {
            errorOut = "No files to upload";
            return false;
        }

        int success = 0, failed = 0;
        QStringList failedNames;

        for (const UploadItem& item : items) {
            // Check for cancellation
            {
                QMutexLocker locker(&m_mutex);
                if (m_cancelled) {
                    errorOut = "Operation cancelled";
                    return false;
                }
                while (m_paused && !m_cancelled) {
                    m_pauseCondition.wait(&m_mutex, 100);
                }
            }

            QString destFolder = task.destPath;
            if (!item.relativeDir.isEmpty()) {
                if (!destFolder.endsWith('/')) destFolder += '/';
                destFolder += item.relativeDir;
            }
            const bool destExistedBefore = remoteFolderExists(destFolder);

            // Use native separators on Windows — the MEGA SDK's Windows
            // FileSystemAccess is picky about forward vs backward slashes
            // on some code paths. On Linux toNativeSeparators is a no-op.
            const QString nativeLocal = QDir::toNativeSeparators(item.localPath);

            std::string err;
            MegaUploadEvidence evidence;
            bool ok = megaApiUpload(m_megaApi,
                                    nativeLocal.toStdString(),
                                    destFolder.toStdString(),
                                    err,
                                    &evidence);
            emitCreatedFolderIfNew(task.index, destFolder, destExistedBefore);
            if (ok) {
                success++;
                if (evidence.createdNewRemoteFile) {
                    emit remoteFileUploaded(
                        task.index,
                        task.memberId,
                        item.localPath,
                        QString::fromStdString(evidence.remoteFolderPath),
                        QString::fromStdString(evidence.remoteFilePath),
                        QString::fromStdString(evidence.fileName));
                }
                if (m_moveMode) {
                    // Move mode = delete local file after successful upload
                    // (QFile::remove accepts either separator on Windows)
                    if (!QFile::remove(item.localPath)) {
                        qWarning() << "processLocalUpload: Uploaded but could not delete local:"
                                   << item.localPath;
                    }
                }
            } else {
                failed++;
                failedNames.append(QFileInfo(item.localPath).fileName());
                qWarning() << "processLocalUpload: failed" << nativeLocal
                           << "->" << destFolder << ":" << QString::fromStdString(err);
                if (errorOut.isEmpty()) errorOut = QString::fromStdString(err);
            }
        }

        if (failed > 0) {
            errorOut = QString("%1/%2 files failed: %3")
                .arg(failed).arg(items.size())
                .arg(failedNames.join(", ").left(200));
        }
        return (failed == 0);
    }

    QList<FolderCopyTask> m_tasks;
    CloudCopier* m_cloudCopier = nullptr;
    mega::MegaApi* m_megaApi = nullptr;
    bool m_skipExisting = true;
    bool m_createDestFolder = true;
    bool m_moveMode = false;
    bool m_cancelled;
    bool m_paused;
    QMutex m_mutex;
    QWaitCondition m_pauseCondition;
};

// Include MOC for the worker class defined in this file
#include "DistributionPanel.moc"

DistributionPanel::DistributionPanel(QWidget* parent)
    : QWidget(parent)
    , m_registry(MemberRegistry::instance())
    , m_fileController(nullptr)
    , m_megaApi(nullptr)
    , m_successCount(0)
    , m_failCount(0)
{
    setObjectName("DistributionPanel");
    m_contentRouter = std::make_unique<ContentRouter>();
    setupUI();
    loadGroups();

    connect(m_registry, &MemberRegistry::groupAdded, this, [this]() { loadGroups(); });
    connect(m_registry, &MemberRegistry::groupUpdated, this, [this]() { loadGroups(); });
    connect(m_registry, &MemberRegistry::groupRemoved, this, [this]() { loadGroups(); });
}

DistributionPanel::~DistributionPanel() {
    cleanupWorkerThread();
}

void DistributionPanel::setFileController(FileController* controller) {
    if (m_fileController) {
        disconnect(m_fileController, nullptr, this, nullptr);
    }

    m_fileController = controller;

    if (m_fileController) {
        connect(m_fileController, &FileController::fileListReceived,
                this, &DistributionPanel::onFileListReceived);
        // Re-enable scan button on error (e.g., folder not found)
        connect(m_fileController, &FileController::loadingFinished,
                this, [this]() {
            if (!m_scanBtn->isEnabled()) {
                m_scanBtn->setEnabled(true);
            }
        });
    }
}

void DistributionPanel::setMegaApi(mega::MegaApi* api) {
    m_megaApi = api;
    if (m_megaApi) {
        m_cloudCopier = std::make_unique<CloudCopier>(m_megaApi);
        // Set default conflict resolution to overwrite
        m_cloudCopier->setDefaultConflictResolution(ConflictResolution::OVERWRITE);
    }
}

void DistributionPanel::setDistributionController(DistributionController* controller) {
    if (m_distController) {
        disconnect(m_distController, nullptr, this, nullptr);
    }

    m_distController = controller;

    if (m_distController) {
        // Distribution started — set UI running state
        connect(m_distController, &DistributionController::distributionStarted,
                this, [this](const QString& jobId) {
            if (!m_controllerActive) return;

            QJsonObject metadata;
            QString preferredId = jobId;
            if (!m_currentJobId.isEmpty()) {
                OperationJobRecord existing = OperationJobStore::instance().job(m_currentJobId);
                metadata = existing.metadata;
                preferredId = m_currentJobId;
            }
            metadata["controllerJobId"] = jobId;

            m_currentJobId = OperationJobStore::instance().createJob(
                OperationJobType::Distribution,
                QString("Upload to %1 member folders").arg(m_currentJobPlannedCount),
                m_currentJobPlannedCount,
                metadata,
                preferredId);
            m_currentJobCancelled = false;
            OperationJobStore::instance().markRunning(
                m_currentJobId,
                QString("Uploading to %1 member folders").arg(m_currentJobPlannedCount));
            m_isRunning = true;
            m_statusLabel->setText("Upload starting...");
        });

        // Progress updates — update status label with current file info
        connect(m_distController, &DistributionController::distributionProgress,
                this, [this](const QtDistributionProgress& progress) {
            if (!m_controllerActive) return;
            m_statusLabel->setText(QString("Uploading: %1 - %2")
                .arg(progress.currentMember, progress.currentFile));
            updateCurrentJobProgress(QString("Uploading %1 - %2")
                .arg(progress.currentMember, progress.currentFile));
        });

        // Member status updates — update corresponding table row
        connect(m_distController, &DistributionController::memberCompleted,
                this, [this](const QtMemberStatus& status) {
            if (!m_controllerActive) return;
            if (!m_memberRowMap.contains(status.memberId)) return;

            int row = m_memberRowMap[status.memberId];
            QTableWidgetItem* statusItem = m_memberTable->item(row, COL_STATUS);
            if (!statusItem) return;

            auto& tm = ThemeManager::instance();
            if (status.state == "uploading") {
                statusItem->setText("Uploading...");
                statusItem->setForeground(tm.supportWarning());
            } else if (status.state == "completed") {
                statusItem->setText(QString("Done (%1 files)").arg(status.filesUploaded));
                statusItem->setForeground(tm.supportSuccess());
                m_successCount++;
                AnimationHelper::animateProgress(m_progressBar, m_successCount + m_failCount);
            } else if (status.state == "failed") {
                statusItem->setText("Failed");
                statusItem->setForeground(tm.supportError());
                statusItem->setToolTip(status.lastError);
                m_failCount++;
                AnimationHelper::animateProgress(m_progressBar, m_successCount + m_failCount);
            } else if (status.state == "skipped") {
                statusItem->setText("Skipped");
                statusItem->setForeground(tm.textSecondary());
                statusItem->setToolTip(status.lastError);
                m_failCount++;
                AnimationHelper::animateProgress(m_progressBar, m_successCount + m_failCount);
            }
            if (!status.lastError.isEmpty()) {
                OperationJobStore::instance().setLastError(m_currentJobId, status.lastError);
            }
            updateCurrentJobProgress(QString("%1: %2")
                .arg(status.memberName.isEmpty() ? status.memberId : status.memberName,
                     status.state));
            saveDistributionCheckpoint(QString("member_%1").arg(status.state));
        });
        connect(m_distController, &DistributionController::remoteFileUploaded,
                this, [this](const QString& memberId,
                              const QString& localPath,
                              const QString& remoteFolderPath,
                              const QString& remoteFilePath,
                              const QString& fileName) {
            if (!m_controllerActive) return;
            recordDistributionUploadedFile(
                m_memberRowMap.value(memberId, -1),
                memberId,
                localPath,
                remoteFolderPath,
                remoteFilePath,
                fileName);
        });

        // Distribution finished — reset UI and show summary
        connect(m_distController, &DistributionController::distributionFinished,
                this, [this](const QtDistributionResult& result) {
            m_isRunning = false;
            m_controllerActive = false;
            m_startBtn->setEnabled(true);
            m_pauseBtn->setEnabled(false);
            m_pauseBtn->setText("Pause");
            m_stopBtn->setEnabled(false);
            AnimationHelper::smoothHide(m_progressBar);

            // Reset mode indicator and hide upload banner
            m_modeIndicator->setText("Mode: Cloud Copy (scan and distribute)");
            m_modeIndicator->setProperty("mode", "");
            m_modeIndicator->style()->polish(m_modeIndicator);
            m_uploadBanner->setVisible(false);

            m_statusLabel->setText(QString("Upload complete: %1 of %2 members, %3 files uploaded")
                .arg(result.membersCompleted).arg(result.totalMembers).arg(result.filesUploaded));

            if (!m_currentJobId.isEmpty()) {
                saveDistributionCheckpoint(m_currentJobCancelled
                    ? "cancelled"
                    : (result.membersFailed > 0 || result.filesFailed > 0 ? "finished_with_errors" : "completed"));
                if (m_currentJobCancelled) {
                    OperationJobStore::instance().markCancelled(
                        m_currentJobId,
                        QString("Upload cancelled: %1 completed, %2 failed")
                            .arg(result.membersCompleted).arg(result.membersFailed));
                } else if (result.membersFailed > 0 || result.filesFailed > 0) {
                    OperationJobStore::instance().markFailed(
                        m_currentJobId,
                        QString("Upload completed with %1 member failures and %2 file failures")
                            .arg(result.membersFailed).arg(result.filesFailed),
                        result.membersCompleted,
                        result.membersFailed,
                        result.membersSkipped);
                } else {
                    OperationJobStore::instance().markCompleted(
                        m_currentJobId,
                        result.membersCompleted,
                        result.membersFailed,
                        result.membersSkipped,
                        QString("Upload complete: %1 members, %2 files")
                            .arg(result.membersCompleted).arg(result.filesUploaded));
                }
            }

            QMessageBox::information(this, "Upload Complete",
                QString("Upload finished.\n\n"
                        "Members: %1 succeeded, %2 failed, %3 skipped\n"
                        "Files: %4 uploaded, %5 failed")
                .arg(result.membersCompleted).arg(result.membersFailed).arg(result.membersSkipped)
                .arg(result.filesUploaded).arg(result.filesFailed));

            m_currentJobId.clear();
            m_currentJobCancelled = false;
            m_currentJobPlannedCount = 0;
        });

        // Error handling
        connect(m_distController, &DistributionController::distributionError,
                this, [this](const QString& error) {
            m_statusLabel->setText(QString("Error: %1").arg(error));
            OperationJobStore::instance().setLastError(m_currentJobId, error);
        });

        qDebug() << "DistributionPanel: DistributionController connected";
    }
}

void DistributionPanel::setupUI() {
    auto& tm = ThemeManager::instance();
    int s = DpiScaler::scale(1); // Base scale unit

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(s*12, s*8, s*12, s*8);
    mainLayout->setSpacing(0);

    // === Upload mode banner (hidden by default) ===
    m_uploadBanner = new QWidget();
    m_uploadBanner->setObjectName("UploadBanner");
    m_uploadBanner->setVisible(false);
    QHBoxLayout* bannerLayout = new QHBoxLayout(m_uploadBanner);
    bannerLayout->setContentsMargins(12, 8, 12, 8);
    m_uploadBannerLabel = new QLabel();
    m_uploadBannerLabel->setObjectName("UploadBannerLabel");
    bannerLayout->addWidget(m_uploadBannerLabel, 1);
    m_uploadBannerCancelBtn = new QPushButton("Cancel");
    m_uploadBannerCancelBtn->setObjectName("PanelDangerButton");
    m_uploadBannerCancelBtn->setToolTip("Exit upload mode and return to normal scan/distribution mode");
    m_uploadBannerCancelBtn->setFixedWidth(80);
    connect(m_uploadBannerCancelBtn, &QPushButton::clicked, this, [this]() {
        m_controllerActive = false;
        m_pendingMemberFileMap.clear();
        m_uploadBanner->setVisible(false);
        m_modeIndicator->setText("Cloud Copy");
        m_modeIndicator->setProperty("mode", "");
        m_modeIndicator->style()->polish(m_modeIndicator);
        m_statusLabel->setText("Upload cancelled. Use Scan to detect folders.");
    });
    bannerLayout->addWidget(m_uploadBannerCancelBtn);
    mainLayout->addWidget(m_uploadBanner);

    // === Move warning banner (hidden) ===
    m_moveWarningBanner = new QLabel("WARNING: MOVE MODE \xe2\x80\x94 Source files will be DELETED after transfer");
    m_moveWarningBanner->setObjectName("WarningBanner");
    m_moveWarningBanner->setVisible(false);
    mainLayout->addWidget(m_moveWarningBanner);

    // === Config Row 1: Source + Scan + Mode switches ===
    QHBoxLayout* row1 = new QHBoxLayout();
    row1->setSpacing(s*6);

    QLabel* srcLabel = new QLabel("Source:");
    srcLabel->setStyleSheet(QString("font-weight: 600; color: %1;").arg(tm.textSecondary().name()));
    row1->addWidget(srcLabel);

    // Source type combo: Cloud (MEGA) vs Local (filesystem)
    m_sourceTypeCombo = new QComboBox();
    m_sourceTypeCombo->addItem("Cloud", "cloud");
    m_sourceTypeCombo->addItem("Local", "local");
    m_sourceTypeCombo->setFixedWidth(s*85);
    m_sourceTypeCombo->setToolTip("Source type: Cloud = MEGA folder, Local = filesystem folder");
    connect(m_sourceTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DistributionPanel::onSourceTypeChanged);
    row1->addWidget(m_sourceTypeCombo);

    m_wmPathEdit = new QLineEdit("/latest-wm");
    m_wmPathEdit->setToolTip("MEGA cloud folder to scan for member subfolders");
    row1->addWidget(m_wmPathEdit, 1);

    // Browse button — only visible in Local mode
    m_browseLocalBtn = new QPushButton();
    m_browseLocalBtn->setIcon(QIcon(":/icons/folder.svg"));
    m_browseLocalBtn->setObjectName("PanelSecondaryButton");
    m_browseLocalBtn->setToolTip("Browse for a local folder");
    m_browseLocalBtn->setFixedSize(s*36, s*36);
    m_browseLocalBtn->setVisible(false);
    connect(m_browseLocalBtn, &QPushButton::clicked, this, &DistributionPanel::onBrowseLocalFolder);
    row1->addWidget(m_browseLocalBtn);

    m_scanBtn = new QPushButton("Scan");
    m_scanBtn->setObjectName("PanelPrimaryButton");
    m_scanBtn->setIcon(QIcon(":/icons/search.svg"));
    m_scanBtn->setToolTip("Scan the source folder and match member subfolders against the registry");
    connect(m_scanBtn, &QPushButton::clicked, this, &DistributionPanel::onScanWmFolder);
    row1->addWidget(m_scanBtn);

    // Broadcast checkbox (compact)
    m_broadcastCheck = new QCheckBox("Broadcast");
    m_broadcastCheck->setToolTip("Copy source folder to all selected members");
    connect(m_broadcastCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            m_smartRouteCheck->setChecked(false);
            m_modeIndicator->setText("Broadcast");
            m_modeIndicator->setProperty("mode", "active");
        } else if (!m_smartRouteCheck->isChecked()) {
            m_modeIndicator->setText("Cloud Copy");
            m_modeIndicator->setProperty("mode", "");
        }
        m_modeIndicator->style()->polish(m_modeIndicator);
    });
    row1->addWidget(m_broadcastCheck);

    // Smart Route checkbox (compact)
    m_smartRouteCheck = new QCheckBox("Smart Route");
    m_smartRouteCheck->setToolTip(
        "Scan inside each matched member folder and route child content separately. "
        "When the destination template is NHB+ Courses or FF Courses, generic course folders "
        "such as Module 1 or Lesson 2 inherit that selected course destination.");
    connect(m_smartRouteCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            m_broadcastCheck->setChecked(false);
            m_modeIndicator->setText("Smart Route");
            m_modeIndicator->setProperty("mode", "warning");
        } else if (!m_broadcastCheck->isChecked()) {
            m_modeIndicator->setText("Cloud Copy");
            m_modeIndicator->setProperty("mode", "");
        }
        m_modeIndicator->style()->polish(m_modeIndicator);
    });
    row1->addWidget(m_smartRouteCheck);

    // Mode badge (inline)
    m_modeIndicator = new QLabel("Cloud Copy");
    m_modeIndicator->setObjectName("ModeIndicator");
    m_modeIndicator->setFixedHeight(s*24);
    row1->addWidget(m_modeIndicator);

    mainLayout->addLayout(row1);
    mainLayout->addSpacing(s*4);

    // === Config Row 2: Dest template + Month + Quick Template + Options gear ===
    QHBoxLayout* row2 = new QHBoxLayout();
    row2->setSpacing(s*6);

    QLabel* destLabel = new QLabel("Dest:");
    destLabel->setStyleSheet(QString("font-weight: 600; color: %1;").arg(tm.textSecondary().name()));
    row2->addWidget(destLabel);

    m_destTemplateEdit = new QLineEdit("{member}");
    m_destTemplateEdit->setToolTip(
        "Destination path template. Variables: {member}, {archive_root}, {fast_forward}, "
        "{nhb_calls}, {month}, etc. Course templates also tell Smart Route whether generic "
        "modules/lessons should go to NHB+ Courses or FF Courses.");
    row2->addWidget(m_destTemplateEdit, 1);

    // Quick template combo (compact)
    m_quickTemplateCombo = new QComboBox();
    m_quickTemplateCombo->addItem("Distribution Folder", "{member}");
    m_quickTemplateCombo->addItem("NHB+ Courses", "{archive_root}/NHB+ Courses");
    m_quickTemplateCombo->addItem("NHB+ Updated Courses", "{archive_root}/NHB+ 2021-2024 - Regularly Updated/NHB+ Courses");
    m_quickTemplateCombo->addItem("FF Courses", "{archive_root}/{fast_forward}/Courses");
    m_quickTemplateCombo->addItem("Hot Seats", "{archive_root}/{fast_forward}/{hot_seats}");
    m_quickTemplateCombo->addItem("Theory Calls", "{archive_root}/{fast_forward}/{theory_calls}");
    m_quickTemplateCombo->addItem("NHB Calls + Month", "{archive_root}/{nhb_calls}/{month}");
    m_quickTemplateCombo->addItem("Custom", "");
    m_quickTemplateCombo->setFixedWidth(s*140);
    m_quickTemplateCombo->setToolTip(
        "Choose a destination preset. NHB+ Courses and FF Courses also set the Smart Route "
        "course context for generic module/lesson folders.");
    connect(m_quickTemplateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DistributionPanel::onQuickTemplateChanged);
    row2->addWidget(m_quickTemplateCombo);

    // Real-time template validation
    connect(m_destTemplateEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        QString error;
        bool valid = TemplateExpander::validateTemplate(text, &error);
        m_destTemplateEdit->setProperty("error", !valid);
        m_destTemplateEdit->style()->polish(m_destTemplateEdit);
        m_destTemplateEdit->setToolTip(valid
            ? "Destination path template. Use {member}, {archive_root}, {hot_seats}, etc. Course templates also guide Smart Route."
            : QString("Invalid template: %1").arg(error));
        bool foundPreset = false;
        for (int i = 0; i < m_quickTemplateCombo->count() - 1; ++i) {
            if (m_quickTemplateCombo->itemData(i).toString() == text) {
                m_quickTemplateCombo->blockSignals(true);
                m_quickTemplateCombo->setCurrentIndex(i);
                m_quickTemplateCombo->blockSignals(false);
                foundPreset = true;
                break;
            }
        }
        if (!foundPreset) {
            m_quickTemplateCombo->blockSignals(true);
            m_quickTemplateCombo->setCurrentIndex(m_quickTemplateCombo->count() - 1);
            m_quickTemplateCombo->blockSignals(false);
        }
    });

    // Month combo
    m_monthCombo = new QComboBox();
    m_monthCombo->addItem("Auto (from filename)");
    m_monthCombo->addItems({"January", "February", "March", "April", "May", "June",
                           "July", "August", "September", "October", "November", "December"});
    m_monthCombo->setCurrentIndex(QDate::currentDate().month());
    m_monthCombo->setFixedWidth(s*120);
    m_monthCombo->setToolTip("Month for {month} variable");
    row2->addWidget(m_monthCombo);

    // Options gear button — opens menu with all options + template management
    QPushButton* optionsBtn = new QPushButton();
    optionsBtn->setIcon(QIcon(":/icons/settings.svg"));
    optionsBtn->setObjectName("PanelSecondaryButton");
    optionsBtn->setToolTip("Distribution options & template management");
    optionsBtn->setFixedSize(s*36, s*36);
    connect(optionsBtn, &QPushButton::clicked, this, [this]() {
        showDistributionSettingsDialog();
    });
    row2->addWidget(optionsBtn);

    mainLayout->addLayout(row2);
    mainLayout->addSpacing(s*6);

    // Hidden checkboxes — still need these as members for other code to read their state
    m_copyContentsOnlyCheck = new QCheckBox(); m_copyContentsOnlyCheck->setChecked(true); m_copyContentsOnlyCheck->setVisible(false);
    m_skipExistingCheck = new QCheckBox(); m_skipExistingCheck->setChecked(true); m_skipExistingCheck->setVisible(false);
    m_createDestFolderCheck = new QCheckBox(); m_createDestFolderCheck->setChecked(true); m_createDestFolderCheck->setVisible(false);
    m_removeWatermarkSuffixCheck = new QCheckBox(); m_removeWatermarkSuffixCheck->setChecked(true); m_removeWatermarkSuffixCheck->setVisible(false);
    m_moveFilesCheck = new QCheckBox(); m_moveFilesCheck->setChecked(false); m_moveFilesCheck->setVisible(false);
    connect(m_moveFilesCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) AnimationHelper::smoothShow(m_moveWarningBanner);
        else AnimationHelper::smoothHide(m_moveWarningBanner);
    });

    // Hidden widgets still needed by other code
    m_variableHelpBtn = nullptr;
    m_previewPathsBtn = nullptr;
    m_generateDestsBtn = nullptr;
    m_savedTemplateCombo = new QComboBox(); m_savedTemplateCombo->setVisible(false);
    m_saveTemplateBtn = nullptr;
    m_deleteTemplateBtn = nullptr;
    m_importDestsBtn = nullptr;
    m_exportDestsBtn = nullptr;
    m_addRowBtn = nullptr;
    m_pasteDestsBtn = nullptr;
    m_clearAllBtn = nullptr;
    loadSavedTemplates();

    // === Divider ===
    QFrame* divider = new QFrame();
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet(QString("color: %1;").arg(tm.borderSubtle().name()));
    divider->setFixedHeight(1);
    mainLayout->addWidget(divider);
    mainLayout->addSpacing(s*4);

    // === Table (dominates) ===
    m_emptyState = new EmptyStateWidget(
        ":/icons/share.svg",
        "No folders detected",
        "Set a source path and scan to detect folders for distribution.",
        "Scan Folder", this);
    connect(m_emptyState, &EmptyStateWidget::actionClicked, this, &DistributionPanel::onScanWmFolder);
    mainLayout->addWidget(m_emptyState);

    m_memberTable = new QTableWidget();
    m_memberTable->setObjectName("DistributionTable");
    m_memberTable->setColumnCount(COL_COUNT);
    m_memberTable->setHorizontalHeaderLabels({"", "Source Folder", "Matched Member", "Match", "Destination", "Status"});
    m_memberTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_memberTable->setAlternatingRowColors(true);
    m_memberTable->verticalHeader()->setVisible(false);
    m_memberTable->horizontalHeader()->setSectionResizeMode(COL_CHECK, QHeaderView::Fixed);
    m_memberTable->horizontalHeader()->setSectionResizeMode(COL_SOURCE_FOLDER, QHeaderView::Interactive);
    m_memberTable->horizontalHeader()->setSectionResizeMode(COL_MATCHED_MEMBER, QHeaderView::Interactive);
    m_memberTable->horizontalHeader()->setSectionResizeMode(COL_MATCH_TYPE, QHeaderView::Fixed);
    m_memberTable->horizontalHeader()->setSectionResizeMode(COL_DESTINATION, QHeaderView::Stretch);
    m_memberTable->horizontalHeader()->setSectionResizeMode(COL_STATUS, QHeaderView::Fixed);
    m_memberTable->setColumnWidth(COL_CHECK, s*30);
    m_memberTable->setColumnWidth(COL_SOURCE_FOLDER, s*180);
    m_memberTable->setColumnWidth(COL_MATCHED_MEMBER, s*160);
    m_memberTable->setColumnWidth(COL_MATCH_TYPE, s*90);
    m_memberTable->setColumnWidth(COL_STATUS, s*40);
    CopyHelper::installTableCopyMenu(m_memberTable);
    mainLayout->addWidget(m_memberTable, 1);

    // === Action Bar (single row) ===
    mainLayout->addSpacing(s*6);
    QHBoxLayout* actionBar = new QHBoxLayout();
    actionBar->setSpacing(s*6);

    m_selectAllBtn = ButtonFactory::createText("All", this);
    m_selectAllBtn->setToolTip("Select all");
    connect(m_selectAllBtn, &QPushButton::clicked, this, &DistributionPanel::onSelectAll);
    actionBar->addWidget(m_selectAllBtn);

    m_deselectAllBtn = ButtonFactory::createText("None", this);
    m_deselectAllBtn->setToolTip("Deselect all");
    connect(m_deselectAllBtn, &QPushButton::clicked, this, &DistributionPanel::onDeselectAll);
    actionBar->addWidget(m_deselectAllBtn);

    m_groupCombo = new QComboBox();
    m_groupCombo->setMinimumWidth(s*140);
    m_groupCombo->setToolTip("Filter selected work to a saved member group. In Broadcast mode, Scan populates only that group.");
    // Popup opens upward since the combo is near the screen bottom
    m_groupCombo->view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_groupCombo->addItem("-- Group --", "");
    connect(m_groupCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        QString groupName = m_groupCombo->itemData(index).toString();
        if (groupName.isEmpty()) return;

        // Broadcast mode: re-scan to populate ONLY the group's members.
        // Keep the group selected so the user can see the active filter.
        if (m_broadcastCheck->isChecked()) {
            QString sourcePath = m_wmPathEdit->text().trimmed();
            if (sourcePath.isEmpty()) {
                m_statusLabel->setText(
                    QString("Group filter set to '%1'. Enter source path then click Scan or Start.")
                        .arg(groupName));
                return;
            }
            onScanWmFolder();
            return;
        }

        onDeselectAll();
        QStringList groupMemberIds = m_registry->getGroupMemberIds(groupName);
        int tableRow = 0;
        for (int i = 0; i < m_wmFolders.size(); ++i) {
            if (groupMemberIds.contains(m_wmFolders[i].memberId)) {
                m_wmFolders[i].selected = true;
                QWidget* w = m_memberTable->cellWidget(tableRow, COL_CHECK);
                if (w) {
                    QCheckBox* c = w->findChild<QCheckBox*>();
                    if (c) c->setChecked(true);
                }
            }
            tableRow += m_wmFolders[i].smartRouted ? (1 + m_wmFolders[i].routes.size()) : 1;
        }
        m_groupCombo->blockSignals(true);
        m_groupCombo->setCurrentIndex(0);
        m_groupCombo->blockSignals(false);
    });
    actionBar->addWidget(m_groupCombo);

    m_bulkRenameBtn = ButtonFactory::createText("Bulk Rename", this);
    m_bulkRenameBtn->setIcon(QIcon(":/icons/edit.svg"));
    m_bulkRenameBtn->setToolTip("Remove '_watermarked' from files in the selected destination folders");
    connect(m_bulkRenameBtn, &QPushButton::clicked, this, &DistributionPanel::onBulkRename);
    actionBar->addWidget(m_bulkRenameBtn);

    actionBar->addStretch();

    m_previewBtn = ButtonFactory::createOutline("Preview", this);
    m_previewBtn->setIcon(QIcon(":/icons/eye.svg"));
    m_previewBtn->setToolTip("Preview the selected source-to-destination routes before starting");
    connect(m_previewBtn, &QPushButton::clicked, this, &DistributionPanel::onPreviewDistribution);
    actionBar->addWidget(m_previewBtn);

    m_auditBtn = ButtonFactory::createOutline("Audit", this);
    m_auditBtn->setIcon(QIcon(":/icons/search.svg"));
    m_auditBtn->setToolTip(
        "Run preflight checks for selected work, member matches, missing source paths, "
        "missing destinations, duplicate destinations, and missing group members");
    connect(m_auditBtn, &QPushButton::clicked, this, &DistributionPanel::onRunAudit);
    actionBar->addWidget(m_auditBtn);

    m_startBtn = ButtonFactory::createPrimary("Start Distribution", this);
    m_startBtn->setIcon(QIcon(":/icons/play.svg"));
    m_startBtn->setToolTip("Run the audit, confirm the job, then copy or upload the selected routes");
    connect(m_startBtn, &QPushButton::clicked, this, &DistributionPanel::onStartDistribution);
    actionBar->addWidget(m_startBtn);

    m_pauseBtn = new QPushButton("Pause");
    m_pauseBtn->setProperty("type", "warning");
    m_pauseBtn->setIcon(QIcon(":/icons/pause.svg"));
    m_pauseBtn->setToolTip("Pause the active distribution job. Click Resume to continue.");
    m_pauseBtn->setEnabled(false);
    m_pauseBtn->setVisible(false);
    connect(m_pauseBtn, &QPushButton::clicked, this, &DistributionPanel::onPauseDistribution);
    actionBar->addWidget(m_pauseBtn);

    m_stopBtn = new QPushButton("Stop");
    m_stopBtn->setObjectName("PanelDangerButton");
    m_stopBtn->setIcon(QIcon(":/icons/x.svg"));
    m_stopBtn->setToolTip("Cancel the active distribution job after confirmation");
    m_stopBtn->setEnabled(false);
    m_stopBtn->setVisible(false);
    connect(m_stopBtn, &QPushButton::clicked, this, &DistributionPanel::onStopDistribution);
    actionBar->addWidget(m_stopBtn);

    mainLayout->addLayout(actionBar);

    // === Progress & Status ===
    m_progressBar = new QProgressBar();
    m_progressBar->setVisible(false);
    mainLayout->addWidget(m_progressBar);

    QHBoxLayout* statusLayout = new QHBoxLayout();
    m_statusLabel = new QLabel("Set source path and click Scan");
    m_statusLabel->setProperty("type", "secondary");
    CopyHelper::makeSelectable(m_statusLabel);
    statusLayout->addWidget(m_statusLabel);
    m_statsLabel = new QLabel();
    m_statsLabel->setProperty("type", "secondary");
    CopyHelper::makeSelectable(m_statsLabel);
    statusLayout->addWidget(m_statsLabel);
    statusLayout->addStretch();
    mainLayout->addLayout(statusLayout);
}

void DistributionPanel::refresh() {
    onScanWmFolder();
}

void DistributionPanel::loadGroups() {
    m_groupCombo->blockSignals(true);
    m_groupCombo->clear();
    m_groupCombo->addItem("-- Select Group --", "");

    QStringList groupNames = m_registry->getGroupNames();
    for (const QString& name : groupNames) {
        int count = m_registry->getGroupMemberIds(name).size();
        m_groupCombo->addItem(QString("%1 (%2)").arg(name).arg(count), name);
    }
    m_groupCombo->blockSignals(false);
}

void DistributionPanel::addFilesFromWatermark(const QStringList& filePaths) {
    if (filePaths.isEmpty()) {
        return;
    }

    // Store received files for reference and potential highlighting
    m_receivedWatermarkFiles = filePaths;

    m_statusLabel->setText(QString("Received %1 %2 from Watermark panel — scanning cloud...").arg(filePaths.size()).arg(filePaths.size() == 1 ? "file" : "files"));

    qDebug() << "DistributionPanel: Received" << filePaths.size() << "files from Watermark:";
    for (const QString& path : filePaths) {
        qDebug() << "  -" << path;
    }

    // Scan /latest-wm/ to populate the table.
    // Note: In single-member mode, files must already be uploaded to /latest-wm/
    // on MEGA for the scan to find them. The all-members/group pipeline handles
    // this automatically via sendToDistributionMapped + DistributionController.
    onScanWmFolder();
}

void DistributionPanel::retryJob(const QString& jobId) {
    if (m_isRunning || (m_distController && m_distController->isRunning())) {
        QMessageBox::warning(this, "Distribution Running",
            "A distribution job is already running. Stop it before retrying another job.");
        return;
    }

    OperationJobRecord record = OperationJobStore::instance().job(jobId);
    if (record.id.isEmpty() || record.type != OperationJobType::Distribution) {
        QMessageBox::warning(this, "Retry Unavailable",
            "This job cannot be retried from the Distribution panel.");
        return;
    }

    QJsonObject metadata = record.metadata;
    QString retryMode = metadata["retryMode"].toString();
    if (retryMode.isEmpty()) {
        if (metadata.contains("memberFileMap")) {
            retryMode = "direct_upload";
        } else if (metadata.contains("tasks")) {
            retryMode = "folder_tasks";
        }
    }

    if (retryMode.isEmpty()) {
        QMessageBox::warning(this, "Retry Unavailable",
            "This distribution job does not contain retry metadata. New distribution jobs created after this update can be retried.");
        return;
    }

    if (m_memberTable->rowCount() > 0 || !m_wmFolders.isEmpty() || !m_pendingMemberFileMap.isEmpty()) {
        int reply = QMessageBox::question(this, "Replace Distribution Table",
            "Retrying this job will replace the current distribution table. Continue?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    if (retryMode == "direct_upload") {
        if (!m_distController) {
            QMessageBox::warning(this, "Retry Unavailable",
                "The upload controller is not available for this distribution retry.");
            return;
        }

        QMap<QString, QStringList> memberFileMap =
            memberFileMapFromJson(metadata["memberFileMap"].toArray());
        if (memberFileMap.isEmpty()) {
            QMessageBox::warning(this, "Retry Unavailable",
                "This upload job does not contain saved member file mappings.");
            return;
        }

        QMap<QString, QStringList> runnableMap;
        QStringList missingFiles;
        for (auto it = memberFileMap.constBegin(); it != memberFileMap.constEnd(); ++it) {
            QStringList existingFiles;
            for (const QString& path : it.value()) {
                if (QFileInfo::exists(path)) {
                    existingFiles.append(path);
                } else {
                    missingFiles.append(path);
                }
            }
            if (!existingFiles.isEmpty()) {
                runnableMap[it.key()] = existingFiles;
            }
        }

        if (!missingFiles.isEmpty()) {
            QMessageBox::warning(this, "Missing Files",
                QString("%1 local file(s) from this upload job no longer exist and will be skipped:\n\n%2")
                    .arg(missingFiles.size())
                    .arg(missingFiles.join("\n")));
        }

        if (runnableMap.isEmpty()) {
            QMessageBox::warning(this, "Retry Unavailable",
                "None of the original upload files are still available.");
            return;
        }

        m_retrySourceJobId = record.id;
        prepareForUpload(runnableMap);
        m_statusLabel->setText(QString("Retrying upload job %1. Confirm to start.")
            .arg(record.id));
        onStartDistribution();
        return;
    }

    if (retryMode != "folder_tasks") {
        QMessageBox::warning(this, "Retry Unavailable",
            QString("Unsupported distribution retry mode: %1").arg(retryMode));
        return;
    }

    QList<FolderCopyTask> tasks = folderCopyTasksFromJson(metadata["tasks"].toArray());
    if (tasks.isEmpty()) {
        QMessageBox::warning(this, "Retry Unavailable",
            "This distribution job does not contain saved source-to-destination tasks.");
        return;
    }

    QList<FolderCopyTask> runnableTasks;
    QStringList skippedSources;
    for (FolderCopyTask task : tasks) {
        if (task.isLocalSource) {
            if (!task.individualFiles.isEmpty()) {
                QStringList existingFiles;
                for (const QString& path : task.individualFiles) {
                    if (QFileInfo::exists(path)) {
                        existingFiles.append(path);
                    } else {
                        skippedSources.append(path);
                    }
                }
                task.individualFiles = existingFiles;
                if (task.individualFiles.isEmpty()) {
                    continue;
                }
            } else if (!QFileInfo::exists(task.sourcePath)) {
                skippedSources.append(task.sourcePath);
                continue;
            }
        }

        if (task.sourcePath.trimmed().isEmpty() && task.individualFiles.isEmpty()) {
            skippedSources.append(QString("Task for %1").arg(task.memberId));
            continue;
        }
        if (task.destPath.trimmed().isEmpty()) {
            skippedSources.append(QString("%1 has no destination").arg(task.memberId));
            continue;
        }

        task.index = runnableTasks.size();
        runnableTasks.append(task);
    }

    if (!skippedSources.isEmpty()) {
        QMessageBox::warning(this, "Skipped Tasks",
            QString("%1 saved source(s) are no longer available or complete and will be skipped:\n\n%2")
                .arg(skippedSources.size())
                .arg(skippedSources.join("\n")));
    }

    if (runnableTasks.isEmpty()) {
        QMessageBox::warning(this, "Retry Unavailable",
            "No saved distribution tasks are still runnable.");
        return;
    }

    const bool moveMode = metadata["moveFiles"].toBool(metadata["operation"].toString() == "move");
    const bool copyFolderItself = metadata["copyFolderItself"].toBool(false);

    if (m_sourceTypeCombo) {
        const QString sourceType = metadata["sourceType"].toString();
        const int sourceIndex = m_sourceTypeCombo->findData(sourceType);
        if (sourceIndex >= 0) {
            m_sourceTypeCombo->setCurrentIndex(sourceIndex);
        }
    }
    m_wmPathEdit->setText(metadata["sourcePath"].toString());
    m_destTemplateEdit->setText(metadata["templatePath"].toString(m_destTemplateEdit->text()));
    if (m_monthCombo) {
        const QString month = metadata["month"].toString();
        const int monthIndex = m_monthCombo->findText(month);
        if (monthIndex >= 0) {
            m_monthCombo->setCurrentIndex(monthIndex);
        }
    }
    if (m_broadcastCheck) {
        m_broadcastCheck->setChecked(metadata["broadcast"].toBool(false));
    }
    if (m_smartRouteCheck) {
        m_smartRouteCheck->setChecked(metadata["smartRoute"].toBool(false));
    }
    if (m_createDestFolderCheck) {
        m_createDestFolderCheck->setChecked(metadata["createDestFolder"].toBool(true));
    }
    if (m_copyContentsOnlyCheck) {
        m_copyContentsOnlyCheck->setChecked(!copyFolderItself);
    }
    if (m_skipExistingCheck) {
        m_skipExistingCheck->setChecked(metadata["skipExisting"].toBool(
            metadata["conflictMode"].toString() == "skip_existing"));
    }
    if (m_moveFilesCheck) {
        m_moveFilesCheck->setChecked(moveMode);
    }
    if (m_removeWatermarkSuffixCheck) {
        m_removeWatermarkSuffixCheck->setChecked(metadata["removeWatermarkSuffix"].toBool(true));
    }

    m_controllerActive = false;
    m_pendingMemberFileMap.clear();
    m_memberRowMap.clear();
    m_routeMap.clear();
    m_wmFolders.clear();
    m_uploadBanner->setVisible(false);
    m_modeIndicator->setText("Mode: Distribution Retry");
    m_modeIndicator->setProperty("mode", "active");
    m_modeIndicator->style()->polish(m_modeIndicator);

    m_memberTable->setRowCount(0);
    m_memberTable->setRowCount(runnableTasks.size());
    auto& tm = ThemeManager::instance();
    for (int row = 0; row < runnableTasks.size(); ++row) {
        const FolderCopyTask& task = runnableTasks[row];

        QCheckBox* check = new QCheckBox();
        check->setChecked(true);
        check->setEnabled(false);
        QWidget* checkWidget = new QWidget();
        QHBoxLayout* checkLayout = new QHBoxLayout(checkWidget);
        checkLayout->addWidget(check);
        checkLayout->setAlignment(Qt::AlignCenter);
        checkLayout->setContentsMargins(0, 0, 0, 0);
        m_memberTable->setCellWidget(row, COL_CHECK, checkWidget);

        QString sourceLabel = task.sourcePath.isEmpty()
            ? QString("%1 saved file(s)").arg(task.individualFiles.size())
            : QFileInfo(task.sourcePath).fileName();
        if (sourceLabel.isEmpty()) {
            sourceLabel = task.sourcePath;
        }
        auto* sourceItem = new QTableWidgetItem(sourceLabel);
        sourceItem->setToolTip(task.individualFiles.isEmpty()
            ? task.sourcePath
            : task.individualFiles.join("\n"));
        m_memberTable->setItem(row, COL_SOURCE_FOLDER, sourceItem);

        MemberInfo memberInfo = m_registry->getMember(task.memberId);
        QString memberDisplay = memberInfo.displayName.isEmpty()
            ? task.memberId
            : QString("%1 (%2)").arg(memberInfo.displayName, task.memberId);
        auto* memberItem = new QTableWidgetItem(memberDisplay);
        memberItem->setForeground(tm.supportSuccess());
        m_memberTable->setItem(row, COL_MATCHED_MEMBER, memberItem);

        auto* matchItem = new QTableWidgetItem(task.contentTypeLabel.isEmpty()
            ? "Retry"
            : task.contentTypeLabel);
        matchItem->setTextAlignment(Qt::AlignCenter);
        matchItem->setForeground(tm.supportInfo());
        m_memberTable->setItem(row, COL_MATCH_TYPE, matchItem);

        auto* destItem = new QTableWidgetItem(task.destPath);
        destItem->setToolTip(task.destPath);
        m_memberTable->setItem(row, COL_DESTINATION, destItem);

        auto* statusItem = new QTableWidgetItem("Pending");
        statusItem->setTextAlignment(Qt::AlignCenter);
        statusItem->setForeground(tm.textSecondary());
        m_memberTable->setItem(row, COL_STATUS, statusItem);
    }

    updateEmptyState();
    m_progressBar->setValue(0);
    m_progressBar->setMaximum(runnableTasks.size());
    m_successCount = 0;
    m_failCount = 0;
    m_currentJobCancelled = false;
    m_currentJobPlannedCount = runnableTasks.size();
    m_statusLabel->setText(QString("Ready to retry distribution job %1 with %2 task(s).")
        .arg(record.id)
        .arg(runnableTasks.size()));
    m_statsLabel->setText(QString("Retry tasks: %1 | Operation: %2")
        .arg(runnableTasks.size())
        .arg(moveMode ? "Move" : "Copy"));

    if (moveMode) {
        QMessageBox::StandardButton reply = QMessageBox::warning(
            this,
            "Confirm Move Retry",
            QString("This retries a MOVE distribution job. Source files that already moved may be missing, and remaining source files will be deleted after transfer.\n\nRetry %1 task(s)?")
                .arg(runnableTasks.size()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
    } else {
        int reply = QMessageBox::question(this, "Confirm Distribution Retry",
            QString("Retry %1 saved distribution task(s)?\n\nConflict handling: %2")
                .arg(runnableTasks.size())
                .arg((m_skipExistingCheck && m_skipExistingCheck->isChecked()) ? "Skip existing" : "Overwrite existing"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    metadata["retryMode"] = "folder_tasks";
    metadata["tasks"] = folderCopyTasksToJson(runnableTasks);
    metadata["retryOfJobId"] = record.id;
    metadata["retriedTaskCount"] = runnableTasks.size();

    m_currentJobId = OperationJobStore::instance().createJob(
        OperationJobType::Distribution,
        QString("%1 retry %2 distribution %3")
            .arg(moveMode ? "Move" : "Copy")
            .arg(runnableTasks.size())
            .arg(runnableTasks.size() == 1 ? "task" : "tasks"),
        runnableTasks.size(),
        metadata);
    saveDistributionCheckpoint("retry_created");

    m_isRunning = true;
    m_isPaused = false;
    m_startBtn->setEnabled(false);
    m_pauseBtn->setEnabled(true);
    m_stopBtn->setEnabled(true);
    AnimationHelper::smoothShow(m_progressBar);
    emit distributionStarted();
    OperationJobStore::instance().markRunning(
        m_currentJobId,
        QString("Retrying %1 distribution %2")
            .arg(moveMode ? "move" : "copy")
            .arg(runnableTasks.size() == 1 ? "task" : "tasks"));

    cleanupWorkerThread();
    m_workerThread = new QThread(this);
    m_copyWorker = new FolderCopyWorker();
    m_copyWorker->moveToThread(m_workerThread);

    m_copyWorker->setTasks(runnableTasks);
    m_copyWorker->setCloudCopier(m_cloudCopier.get());
    m_copyWorker->setMegaApi(m_megaApi);
    m_copyWorker->setSkipExisting(m_skipExistingCheck && m_skipExistingCheck->isChecked());
    m_copyWorker->setCreateDestFolder(m_createDestFolderCheck && m_createDestFolderCheck->isChecked());
    m_copyWorker->setMoveMode(moveMode);
    m_isMoving = moveMode;

    connect(m_workerThread, &QThread::started, m_copyWorker, &FolderCopyWorker::process);
    connect(m_copyWorker, &FolderCopyWorker::taskStarted,
            this, &DistributionPanel::onWorkerTaskStarted, Qt::QueuedConnection);
    connect(m_copyWorker, &FolderCopyWorker::taskCompleted,
            this, &DistributionPanel::onWorkerTaskCompleted, Qt::QueuedConnection);
    connect(m_copyWorker, &FolderCopyWorker::destinationFolderCreated,
            this, &DistributionPanel::recordDistributionCreatedFolder, Qt::QueuedConnection);
    connect(m_copyWorker, &FolderCopyWorker::remoteFileUploaded,
            this, &DistributionPanel::recordDistributionUploadedFile, Qt::QueuedConnection);
    connect(m_copyWorker, &FolderCopyWorker::allCompleted,
            this, &DistributionPanel::onWorkerAllCompleted, Qt::QueuedConnection);
    connect(m_copyWorker, &FolderCopyWorker::progress,
            this, &DistributionPanel::onWorkerProgress, Qt::QueuedConnection);
    connect(m_copyWorker, &FolderCopyWorker::allCompleted,
            m_workerThread, &QThread::quit, Qt::QueuedConnection);
    connect(m_workerThread, &QThread::finished, m_copyWorker, &QObject::deleteLater);

    m_workerThread->start();
}

QJsonArray DistributionPanel::serializeDistributionRows() const {
    QJsonArray rows;
    if (!m_memberTable) {
        return rows;
    }

    for (int row = 0; row < m_memberTable->rowCount(); ++row) {
        QJsonObject object;
        object["row"] = row;
        object["jobId"] = m_currentJobId;

        auto itemText = [this, row](int column) {
            QTableWidgetItem* item = m_memberTable->item(row, column);
            return item ? item->text() : QString();
        };
        auto itemTip = [this, row](int column) {
            QTableWidgetItem* item = m_memberTable->item(row, column);
            return item ? item->toolTip() : QString();
        };

        object["sourceText"] = itemText(COL_SOURCE_FOLDER);
        object["sourcePath"] = itemTip(COL_SOURCE_FOLDER);
        object["memberText"] = itemText(COL_MATCHED_MEMBER);
        object["matchType"] = itemText(COL_MATCH_TYPE);
        object["destinationPath"] = itemText(COL_DESTINATION);
        object["status"] = itemText(COL_STATUS);
        object["error"] = itemTip(COL_STATUS);
        rows.append(object);
    }
    return rows;
}

void DistributionPanel::saveDistributionCheckpoint(const QString& reason, const QString& jobId) {
    const QString targetJobId = jobId.trimmed().isEmpty() ? m_currentJobId : jobId.trimmed();
    if (targetJobId.isEmpty()) {
        return;
    }

    OperationJobRecord record = OperationJobStore::instance().job(targetJobId);
    if (record.id.isEmpty() || record.type != OperationJobType::Distribution) {
        return;
    }

    int rowCount = 0;
    int pendingCount = 0;
    int runningCount = 0;
    int doneCount = 0;
    int failedCount = 0;
    for (int row = 0; m_memberTable && row < m_memberTable->rowCount(); ++row) {
        QTableWidgetItem* statusItem = m_memberTable->item(row, COL_STATUS);
        const QString status = statusItem ? statusItem->text().trimmed().toLower() : QString();
        if (status.isEmpty()) {
            continue;
        }

        ++rowCount;
        if (status.contains("done") || status.contains("complete")) {
            ++doneCount;
        } else if (status.contains("fail") || status.contains("error")) {
            ++failedCount;
        } else if (status.contains("copy") || status.contains("mov") || status.contains("upload")) {
            ++runningCount;
        } else {
            ++pendingCount;
        }
    }

    QJsonObject metadata = record.metadata;
    metadata["distributionCheckpointVersion"] = 1;
    metadata["distributionCheckpointReason"] = reason;
    metadata["distributionCheckpointUpdatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    metadata["distributionRows"] = serializeDistributionRows();
    metadata["distributionRowCount"] = rowCount;
    metadata["distributionPendingRows"] = pendingCount;
    metadata["distributionRunningRows"] = runningCount;
    metadata["distributionDoneRows"] = doneCount;
    metadata["distributionFailedRows"] = failedCount;

    OperationJobStore::instance().createJob(
        OperationJobType::Distribution,
        record.title,
        qMax(record.plannedCount, rowCount),
        metadata,
        targetJobId);
}

void DistributionPanel::recordDistributionCreatedFolder(int taskIndex, const QString& path) {
    const QString cleanedPath = QDir::cleanPath(path.trimmed());
    if (m_currentJobId.isEmpty() || cleanedPath.isEmpty() || cleanedPath == ".") {
        return;
    }

    OperationJobRecord record = OperationJobStore::instance().job(m_currentJobId);
    if (record.id.isEmpty() || record.type != OperationJobType::Distribution) {
        return;
    }

    QJsonObject metadata = record.metadata;
    QJsonArray createdFolders = metadata["distributionCreatedRemoteFolders"].toArray();
    for (const QJsonValue& value : createdFolders) {
        if (!value.isObject()) {
            continue;
        }
        if (QDir::cleanPath(value.toObject()["path"].toString()) == cleanedPath) {
            return;
        }
    }

    QJsonObject entry;
    entry["path"] = cleanedPath;
    entry["taskIndex"] = taskIndex;
    entry["recordedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    createdFolders.append(entry);
    metadata["distributionCreatedRemoteFolders"] = createdFolders;
    metadata["distributionCreatedRemoteFolderCount"] = createdFolders.size();

    OperationJobStore::instance().createJob(
        OperationJobType::Distribution,
        record.title,
        record.plannedCount,
        metadata,
        m_currentJobId);
    saveDistributionCheckpoint("remote_folder_created", m_currentJobId);
}

void DistributionPanel::recordDistributionUploadedFile(int taskIndex,
                                                       const QString& memberId,
                                                       const QString& localPath,
                                                       const QString& remoteFolderPath,
                                                       const QString& remoteFilePath,
                                                       const QString& fileName) {
    const QString cleanedFilePath = QDir::cleanPath(remoteFilePath.trimmed());
    if (m_currentJobId.isEmpty() || cleanedFilePath.isEmpty() || cleanedFilePath == "." || cleanedFilePath == "/") {
        return;
    }

    OperationJobRecord record = OperationJobStore::instance().job(m_currentJobId);
    if (record.id.isEmpty() || record.type != OperationJobType::Distribution) {
        return;
    }

    QJsonObject metadata = record.metadata;
    QJsonArray uploadedFiles = metadata["distributionCreatedRemoteFiles"].toArray();
    for (const QJsonValue& value : uploadedFiles) {
        if (!value.isObject()) {
            continue;
        }
        if (QDir::cleanPath(value.toObject()["remoteFilePath"].toString()) == cleanedFilePath) {
            return;
        }
    }

    QJsonObject entry;
    entry["remoteFilePath"] = cleanedFilePath;
    entry["remoteFolderPath"] = QDir::cleanPath(remoteFolderPath.trimmed());
    entry["fileName"] = fileName.trimmed().isEmpty() ? QFileInfo(localPath).fileName() : fileName.trimmed();
    entry["localPath"] = QDir::cleanPath(localPath.trimmed());
    entry["memberId"] = memberId;
    entry["taskIndex"] = taskIndex;
    entry["recordedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    uploadedFiles.append(entry);
    metadata["distributionCreatedRemoteFiles"] = uploadedFiles;
    metadata["distributionCreatedRemoteFileCount"] = uploadedFiles.size();

    OperationJobStore::instance().createJob(
        OperationJobType::Distribution,
        record.title,
        record.plannedCount,
        metadata,
        m_currentJobId);
    saveDistributionCheckpoint("remote_file_uploaded", m_currentJobId);
}

void DistributionPanel::cleanupJob(const QString& jobId) {
    if (m_isRunning || (m_distController && m_distController->isRunning())) {
        QMessageBox::warning(this, "Distribution Running",
            "Cleanup is not available while a distribution job is running.");
        return;
    }

    OperationJobRecord record = OperationJobStore::instance().job(jobId);
    if (record.id.isEmpty() || record.type != OperationJobType::Distribution) {
        QMessageBox::warning(this, "Cleanup Unavailable",
            "This job cannot be cleaned up from the Distribution panel.");
        return;
    }

    const bool cleanupStatus = record.status == OperationJobStatus::Failed
        || record.status == OperationJobStatus::Cancelled
        || record.status == OperationJobStatus::CleanupRequired;
    if (!cleanupStatus) {
        QMessageBox::information(this, "Cleanup Not Needed",
            "Distribution cleanup is available for failed, cancelled, or cleanup-required jobs.");
        return;
    }

    if (!m_megaApi) {
        QMessageBox::warning(this, "Cleanup Unavailable",
            "MEGA is not connected, so remote distribution cleanup cannot verify or move folders.");
        return;
    }

    const QJsonArray createdFolders = record.metadata["distributionCreatedRemoteFolders"].toArray();
    const QJsonArray createdFiles = record.metadata["distributionCreatedRemoteFiles"].toArray();
    if (createdFolders.isEmpty() && createdFiles.isEmpty()) {
        QMessageBox::warning(this, "Cleanup Needs Ownership Proof",
            "This distribution job has no recorded app-created remote folders or files.\n\n"
            "Cleanup will not guess based on destination paths, because those may be real member folders or existing files.");
        return;
    }

    QMap<int, QJsonObject> rowByIndex;
    const QJsonArray rows = record.metadata["distributionRows"].toArray();
    for (const QJsonValue& value : rows) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        rowByIndex[object["row"].toInt(-1)] = object;
    }

    struct RemoteCleanupCandidate {
        QString path;
        QString status;
        QString member;
        int taskIndex = -1;
        int childCount = 0;
    };
    struct RemoteFileCleanupCandidate {
        QString path;
        QString status;
        QString member;
        QString localPath;
        int taskIndex = -1;
    };

    QList<RemoteCleanupCandidate> candidates;
    QList<RemoteFileCleanupCandidate> fileCandidates;
    QStringList missingFolders;
    QStringList missingFiles;
    QStringList skippedCompletedFolders;
    QStringList skippedCompletedFiles;
    QSet<QString> seenPaths;

    for (const QJsonValue& value : createdFolders) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject object = value.toObject();
        const QString path = QDir::cleanPath(object["path"].toString().trimmed());
        const int taskIndex = object["taskIndex"].toInt(-1);
        if (path.isEmpty() || path == "." || path == "/" || seenPaths.contains(path)) {
            continue;
        }
        seenPaths.insert(path);

        const QJsonObject row = rowByIndex.value(taskIndex);
        const QString status = row["status"].toString();
        const QString normalizedStatus = status.trimmed().toLower();
        if (normalizedStatus.contains("done") || normalizedStatus.contains("complete")) {
            skippedCompletedFolders.append(path);
            continue;
        }

        std::unique_ptr<mega::MegaNode> node(
            m_megaApi->getNodeByPath(path.toUtf8().constData()));
        if (!node || !node->isFolder()) {
            missingFolders.append(path);
            continue;
        }

        int childCount = 0;
        std::unique_ptr<mega::MegaNodeList> children(m_megaApi->getChildren(node.get()));
        if (children) {
            childCount = children->size();
        }

        RemoteCleanupCandidate candidate;
        candidate.path = path;
        candidate.taskIndex = taskIndex;
        candidate.status = status.isEmpty() ? OperationJobStore::statusToString(record.status) : status;
        candidate.member = row["memberText"].toString();
        candidate.childCount = childCount;
        candidates.append(candidate);
    }

    QSet<QString> folderCandidatePaths;
    for (const RemoteCleanupCandidate& candidate : candidates) {
        folderCandidatePaths.insert(candidate.path);
    }

    QSet<QString> seenFilePaths;
    for (const QJsonValue& value : createdFiles) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject object = value.toObject();
        const QString path = QDir::cleanPath(object["remoteFilePath"].toString().trimmed());
        const int taskIndex = object["taskIndex"].toInt(-1);
        if (path.isEmpty() || path == "." || path == "/" || seenFilePaths.contains(path)) {
            continue;
        }
        seenFilePaths.insert(path);

        bool coveredByFolderCandidate = false;
        for (const QString& folderPath : folderCandidatePaths) {
            if (path == folderPath || path.startsWith(folderPath + "/")) {
                coveredByFolderCandidate = true;
                break;
            }
        }
        if (coveredByFolderCandidate) {
            continue;
        }

        const QJsonObject row = rowByIndex.value(taskIndex);
        const QString status = row["status"].toString();
        const QString normalizedStatus = status.trimmed().toLower();
        if (normalizedStatus.contains("done") || normalizedStatus.contains("complete")) {
            skippedCompletedFiles.append(path);
            continue;
        }

        std::unique_ptr<mega::MegaNode> node(
            m_megaApi->getNodeByPath(path.toUtf8().constData()));
        if (!node || node->isFolder()) {
            missingFiles.append(path);
            continue;
        }

        RemoteFileCleanupCandidate candidate;
        candidate.path = path;
        candidate.taskIndex = taskIndex;
        candidate.status = status.isEmpty() ? OperationJobStore::statusToString(record.status) : status;
        candidate.member = row["memberText"].toString();
        if (candidate.member.isEmpty()) {
            candidate.member = object["memberId"].toString();
        }
        candidate.localPath = object["localPath"].toString();
        fileCandidates.append(candidate);
    }

    if (candidates.isEmpty() && fileCandidates.isEmpty()) {
        QStringList details;
        if (!skippedCompletedFolders.isEmpty()) {
            details << QString("%1 app-created folder(s) belong to completed rows and were kept.")
                .arg(skippedCompletedFolders.size());
        }
        if (!skippedCompletedFiles.isEmpty()) {
            details << QString("%1 app-created file(s) belong to completed rows and were kept.")
                .arg(skippedCompletedFiles.size());
        }
        if (!missingFolders.isEmpty()) {
            details << QString("%1 recorded folder(s) no longer exist.")
                .arg(missingFolders.size());
        }
        if (!missingFiles.isEmpty()) {
            details << QString("%1 recorded file(s) no longer exist.")
                .arg(missingFiles.size());
        }
        QMessageBox::information(this, "No Cleanup Needed",
            QString("No app-created remote artifacts are eligible for distribution cleanup.%1")
                .arg(details.isEmpty() ? QString() : "\n\n" + details.join("\n")));
        return;
    }

    std::sort(candidates.begin(), candidates.end(), [](const RemoteCleanupCandidate& a,
                                                       const RemoteCleanupCandidate& b) {
        return a.path.count('/') > b.path.count('/');
    });
    std::sort(fileCandidates.begin(), fileCandidates.end(), [](const RemoteFileCleanupCandidate& a,
                                                               const RemoteFileCleanupCandidate& b) {
        return a.path < b.path;
    });

    QStringList preview;
    const int maxFolderPreview = qMin(candidates.size(), 20);
    for (int i = 0; i < maxFolderPreview; ++i) {
        const RemoteCleanupCandidate& candidate = candidates[i];
        preview << QString("[folder] %1\n   Row: %2 | Status: %3 | Items inside: %4%5")
            .arg(candidate.path)
            .arg(candidate.taskIndex + 1)
            .arg(candidate.status)
            .arg(candidate.childCount)
            .arg(candidate.member.isEmpty() ? QString() : QString(" | Member: %1").arg(candidate.member));
    }
    if (candidates.size() > maxFolderPreview) {
        preview << QString("...and %1 more folder(s)").arg(candidates.size() - maxFolderPreview);
    }
    const int maxFilePreview = qMin(fileCandidates.size(), 25);
    for (int i = 0; i < maxFilePreview; ++i) {
        const RemoteFileCleanupCandidate& candidate = fileCandidates[i];
        preview << QString("[file] %1\n   Row: %2 | Status: %3%4%5")
            .arg(candidate.path)
            .arg(candidate.taskIndex + 1)
            .arg(candidate.status)
            .arg(candidate.member.isEmpty() ? QString() : QString(" | Member: %1").arg(candidate.member))
            .arg(candidate.localPath.isEmpty() ? QString() : QString(" | Source: %1").arg(candidate.localPath));
    }
    if (fileCandidates.size() > maxFilePreview) {
        preview << QString("...and %1 more file(s)").arg(fileCandidates.size() - maxFilePreview);
    }

    if (QMessageBox::warning(this, "Preview Distribution Cleanup",
            QString("Cleanup will move only remote folders that this app recorded as newly created by the selected Distribution job.\n\n"
                    "Recorded app-created files inside existing member folders are moved individually. Existing member folders are not touched. "
                    "All cleanup targets go to the MEGA rubbish bin, not permanent deletion.\n\n%1\n\nContinue?")
                .arg(preview.join("\n")),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    QJsonObject startedDetails;
    startedDetails["candidateFolders"] = candidates.size();
    startedDetails["candidateFiles"] = fileCandidates.size();
    LogManager::instance().logWithContext(
        LogLevel::Info,
        LogCategory::Distribution,
        "cleanup.started",
        QString("Distribution cleanup started for job %1").arg(jobId).toStdString(),
        "",
        "",
        jobId.toStdString(),
        QString::fromUtf8(QJsonDocument(startedDetails).toJson(QJsonDocument::Compact)).toStdString());

    std::unique_ptr<mega::MegaNode> rubbishNode(m_megaApi->getRubbishNode());
    if (!rubbishNode) {
        QMessageBox::warning(this, "Cleanup Failed",
            "MEGA rubbish bin is not available. No folders were moved.");
        return;
    }

    int movedFolders = 0;
    int failedFolders = 0;
    int movedFiles = 0;
    int failedFiles = 0;
    QStringList movedPaths;
    QStringList failedPaths;
    QStringList movedFilePaths;
    QStringList failedFilePaths;

    for (const RemoteCleanupCandidate& candidate : candidates) {
        std::unique_ptr<mega::MegaNode> node(
            m_megaApi->getNodeByPath(candidate.path.toUtf8().constData()));
        if (!node || !node->isFolder()) {
            continue;
        }

        SyncRequestListener listener;
        m_megaApi->moveNode(node.get(), rubbishNode.get(), &listener);
        if (!listener.waitForCompletion(60) || !listener.isSuccess()) {
            failedFolders++;
            failedPaths.append(QString("%1 (%2)").arg(candidate.path, QString::fromStdString(listener.errorString())));
            continue;
        }

        movedFolders++;
        movedPaths.append(candidate.path);
    }

    for (const RemoteFileCleanupCandidate& candidate : fileCandidates) {
        std::unique_ptr<mega::MegaNode> node(
            m_megaApi->getNodeByPath(candidate.path.toUtf8().constData()));
        if (!node || node->isFolder()) {
            continue;
        }

        SyncRequestListener listener;
        m_megaApi->moveNode(node.get(), rubbishNode.get(), &listener);
        if (!listener.waitForCompletion(60) || !listener.isSuccess()) {
            failedFiles++;
            failedFilePaths.append(QString("%1 (%2)").arg(candidate.path, QString::fromStdString(listener.errorString())));
            continue;
        }

        movedFiles++;
        movedFilePaths.append(candidate.path);
    }

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

    OperationJobRecord latestRecord = OperationJobStore::instance().job(jobId);
    QJsonObject metadata = latestRecord.metadata;
    QJsonArray cleanupRuns = metadata["cleanupRuns"].toArray();
    QJsonObject cleanupRun;
    cleanupRun["at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    cleanupRun["scope"] = "remote_distribution_created_artifacts";
    cleanupRun["candidateFolders"] = candidates.size();
    cleanupRun["candidateFiles"] = fileCandidates.size();
    cleanupRun["movedFolders"] = movedFolders;
    cleanupRun["failedFolders"] = failedFolders;
    cleanupRun["movedFiles"] = movedFiles;
    cleanupRun["failedFiles"] = failedFiles;
    cleanupRun["movedFolderPaths"] = appendPathArray(movedPaths);
    cleanupRun["failedFolderPaths"] = appendPathArray(failedPaths);
    cleanupRun["movedFilePaths"] = appendPathArray(movedFilePaths);
    cleanupRun["failedFilePaths"] = appendPathArray(failedFilePaths);
    cleanupRuns.append(cleanupRun);
    metadata["cleanupRuns"] = cleanupRuns;
    metadata["lastCleanupAt"] = cleanupRun["at"];
    metadata["lastCleanupMovedFolders"] = movedFolders;
    metadata["lastCleanupFailedFolders"] = failedFolders;
    metadata["lastCleanupMovedFiles"] = movedFiles;
    metadata["lastCleanupFailedFiles"] = failedFiles;

    OperationJobStore::instance().createJob(
        OperationJobType::Distribution,
        latestRecord.title,
        latestRecord.plannedCount,
        metadata,
        jobId);

    QJsonObject completedDetails;
    completedDetails["movedFolders"] = movedFolders;
    completedDetails["failedFolders"] = failedFolders;
    completedDetails["movedFiles"] = movedFiles;
    completedDetails["failedFiles"] = failedFiles;
    const int totalFailures = failedFolders + failedFiles;
    LogManager::instance().logWithContext(
        totalFailures > 0 ? LogLevel::Warning : LogLevel::Info,
        LogCategory::Distribution,
        totalFailures > 0 ? "cleanup.partial" : "cleanup.completed",
        QString("Distribution cleanup moved %1 folder(s), %2 file(s); %3 failed")
            .arg(movedFolders)
            .arg(movedFiles)
            .arg(totalFailures)
            .toStdString(),
        "",
        "",
        jobId.toStdString(),
        QString::fromUtf8(QJsonDocument(completedDetails).toJson(QJsonDocument::Compact)).toStdString());

    m_statusLabel->setText(QString("Cleanup moved %1 folder(s), %2 file(s) to MEGA rubbish bin; %3 failed.")
        .arg(movedFolders)
        .arg(movedFiles)
        .arg(totalFailures));
    QMessageBox::information(this, "Distribution Cleanup Complete",
        QString("Cleanup moved app-created remote artifacts to the MEGA rubbish bin.\n\nFolders: %1 moved\nFiles: %2 moved\nFailed: %3")
            .arg(movedFolders)
            .arg(movedFiles)
            .arg(totalFailures));
}

void DistributionPanel::prepareForUpload(const QMap<QString, QStringList>& memberFileMap) {
    const UploadMapValidation validation =
        validateMemberFileMapForUpload(memberFileMap, m_registry);

    if (!validation.missingFiles.isEmpty()) {
        QMessageBox::warning(this, "Missing Watermarked Files",
            QString("%1 local file(s) no longer exist. Members with missing files were not queued, "
                    "because partial member uploads are unsafe:\n\n%2")
                .arg(validation.missingFiles.size())
                .arg(validation.missingFiles.mid(0, 30).join("\n")));
    }

    if (!validation.membersWithoutFolder.isEmpty()) {
        QMessageBox::warning(this, "Missing Distribution Folders",
            QString("%1 member(s) have no distribution folder and were not queued:\n\n%2")
                .arg(validation.membersWithoutFolder.size())
                .arg(validation.membersWithoutFolder.mid(0, 30).join("\n")));
    }

    if (validation.memberFileMap.isEmpty()) {
        m_controllerActive = false;
        m_pendingMemberFileMap.clear();
        m_memberRowMap.clear();
        m_memberTable->setRowCount(0);
        m_startBtn->setEnabled(false);
        m_uploadBanner->setVisible(false);
        m_statusLabel->setText("No complete member upload batches are available.");
        updateEmptyState();
        return;
    }

    const QMap<QString, QStringList>& runnableMap = validation.memberFileMap;

    m_controllerActive = true;
    m_modeIndicator->setText("Mode: Auto-Upload (watermark -> member folders)");
    m_modeIndicator->setProperty("mode", "active");
    m_modeIndicator->style()->polish(m_modeIndicator);
    m_memberRowMap.clear();
    m_routeMap.clear();
    m_wmFolders.clear();
    m_memberTable->setRowCount(0);
    m_memberTable->setRowCount(runnableMap.size());

    int totalFiles = 0;
    for (const auto& files : runnableMap) totalFiles += files.size();

    // Show upload banner
    m_uploadBannerLabel->setText(QString("Upload Mode -- %1 files for %2 members. "
        "Files received from Watermark panel.").arg(totalFiles).arg(runnableMap.size()));
    m_uploadBanner->setVisible(true);

    int row = 0;
    for (auto it = runnableMap.constBegin(); it != runnableMap.constEnd(); ++it) {
        const QString& memberId = it.key();
        const QStringList& files = it.value();

        m_memberRowMap[memberId] = row;

        // COL_CHECK: Checkbox (checked, disabled during upload)
        QCheckBox* check = new QCheckBox();
        check->setChecked(true);
        check->setEnabled(false);
        QWidget* checkWidget = new QWidget();
        QHBoxLayout* checkLayout = new QHBoxLayout(checkWidget);
        checkLayout->addWidget(check);
        checkLayout->setAlignment(Qt::AlignCenter);
        checkLayout->setContentsMargins(0, 0, 0, 0);
        m_memberTable->setCellWidget(row, COL_CHECK, checkWidget);

        // COL_SOURCE_FOLDER: Source file summary
        QString sourceSummary = files.size() == 1
            ? QFileInfo(files.first()).fileName()
            : QString("%1 files").arg(files.size());
        auto* sourceItem = new QTableWidgetItem(sourceSummary);
        sourceItem->setToolTip(files.join("\n"));
        m_memberTable->setItem(row, COL_SOURCE_FOLDER, sourceItem);

        // COL_MATCHED_MEMBER: Member ID / name
        MemberInfo memberInfo = m_registry->getMember(memberId);
        QString display = memberInfo.displayName.isEmpty() ? memberId
            : QString("%1 (%2)").arg(memberInfo.displayName, memberId);
        auto& tm = ThemeManager::instance();
        auto* memberItem = new QTableWidgetItem(display);
        memberItem->setForeground(tm.supportSuccess());
        m_memberTable->setItem(row, COL_MATCHED_MEMBER, memberItem);

        // COL_MATCH_TYPE: Upload mode
        auto* matchItem = new QTableWidgetItem("Upload");
        matchItem->setTextAlignment(Qt::AlignCenter);
        matchItem->setForeground(tm.supportInfo());
        m_memberTable->setItem(row, COL_MATCH_TYPE, matchItem);

        // COL_DESTINATION
        QString dest = memberInfo.distributionFolder.isEmpty() ? "(no folder)" : memberInfo.distributionFolder;
        auto* destItem = new QTableWidgetItem(dest);
        destItem->setToolTip(dest);
        m_memberTable->setItem(row, COL_DESTINATION, destItem);

        // COL_STATUS - pending
        auto* statusItem = new QTableWidgetItem("Pending");
        statusItem->setTextAlignment(Qt::AlignCenter);
        statusItem->setForeground(tm.textSecondary());
        m_memberTable->setItem(row, COL_STATUS, statusItem);

        row++;
    }

    // Store the member file map for when user clicks Start
    m_pendingMemberFileMap = runnableMap;

    // Set UI to ready state — user must click Start
    m_startBtn->setEnabled(true);
    m_pauseBtn->setEnabled(false);
    m_stopBtn->setEnabled(false);
    m_progressBar->setValue(0);
    m_progressBar->setMaximum(runnableMap.size());
    m_successCount = 0;
    m_failCount = 0;

    m_statusLabel->setText(QString("Ready to upload %1 files to %2 members. Click Start to begin.")
        .arg(totalFiles).arg(runnableMap.size()));
    m_statsLabel->setText(QString("Members: %1 | Files: %2")
        .arg(runnableMap.size()).arg(totalFiles));

    updateEmptyState();
}

void DistributionPanel::onScanWmFolder() {
    const QString detectedIntent = autoDetectDistributionIntent();

    // Dispatch to local scan if Local mode is selected
    const bool isLocal = m_sourceTypeCombo &&
        m_sourceTypeCombo->currentData().toString() == "local";
    if (isLocal) {
        onScanLocalFolder();
        return;
    }

    if (!m_fileController) {
        m_statusLabel->setText("Error: Not connected to MEGA");
        return;
    }

    // Reset to cloud copy mode (user-initiated scan)
    m_controllerActive = false;
    m_modeIndicator->setText("Mode: Cloud Copy (scan and distribute)");
    m_modeIndicator->setProperty("mode", "");
    m_modeIndicator->style()->polish(m_modeIndicator);
    m_startBtn->setEnabled(true);

    // Reload member registry to pick up any new members
    m_registry->load();
    qDebug() << "DistributionPanel: Reloaded member registry, count:" << m_registry->getAllMembers().size();

    // Broadcast mode: validate source and populate all members
    if (m_broadcastCheck->isChecked()) {
        onBroadcastScan();
        return;
    }

    QString wmPath = m_wmPathEdit->text();
    m_statusLabel->setText(detectedIntent.isEmpty()
        ? QString("Scanning %1...").arg(wmPath)
        : QString("Scanning %1... %2").arg(wmPath, detectedIntent));
    m_scanBtn->setEnabled(false);
    m_wmFolders.clear();
    m_memberTable->setRowCount(0);

    // Request folder listing via FileController
    // The result will come back via onFileListReceived slot
    m_fileController->refreshRemote(wmPath);
}

void DistributionPanel::onSourceTypeChanged(int /*index*/) {
    if (!m_sourceTypeCombo) return;
    const bool isLocal = m_sourceTypeCombo->currentData().toString() == "local";
    if (m_browseLocalBtn) m_browseLocalBtn->setVisible(isLocal);
    if (m_wmPathEdit) {
        if (isLocal) {
            m_wmPathEdit->setPlaceholderText("/path/to/local/folder");
            m_wmPathEdit->setToolTip("Local filesystem folder to scan for member subfolders");
            // Clear the cloud default so the user sees Local placeholder
            if (m_wmPathEdit->text() == "/latest-wm") m_wmPathEdit->clear();
        } else {
            m_wmPathEdit->setPlaceholderText("/mega/cloud/path");
            m_wmPathEdit->setToolTip("MEGA cloud folder to scan for member subfolders");
            if (m_wmPathEdit->text().isEmpty()) m_wmPathEdit->setText("/latest-wm");
        }
    }
}

void DistributionPanel::onBrowseLocalFolder() {
    QString startDir = m_wmPathEdit->text();
    if (startDir.isEmpty() || !QDir(startDir).exists()) {
        startDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }
    QString dir = QFileDialog::getExistingDirectory(
        this, "Select Local Source Folder", startDir);
    if (!dir.isEmpty()) {
        m_wmPathEdit->setText(dir);
    }
}

void DistributionPanel::onScanLocalFolder() {
    m_controllerActive = false;
    m_modeIndicator->setText("Mode: Local Upload");
    m_modeIndicator->setProperty("mode", "active");
    m_modeIndicator->style()->polish(m_modeIndicator);
    m_startBtn->setEnabled(true);

    m_registry->load();
    qDebug() << "DistributionPanel[local]: Reloaded member registry, count:"
             << m_registry->getAllMembers().size();

    const QString localPath = m_wmPathEdit->text().trimmed();
    if (localPath.isEmpty()) {
        m_statusLabel->setText("Enter a local folder path");
        return;
    }

    QDir dir(localPath);
    if (!dir.exists()) {
        m_statusLabel->setText("Local path not found: " + localPath);
        return;
    }

    m_wmFolders.clear();
    m_memberTable->setRowCount(0);
    const QString detectedIntent = autoDetectDistributionIntent();
    m_statusLabel->setText(detectedIntent.isEmpty()
        ? QString("Scanning local folder %1...").arg(localPath)
        : QString("Scanning local folder %1... %2").arg(localPath, detectedIntent));

    // Broadcast mode for Local: single local source → all active members (or group)
    if (m_broadcastCheck->isChecked()) {
        QString groupFilter;
        if (m_groupCombo) {
            groupFilter = m_groupCombo->itemData(m_groupCombo->currentIndex()).toString();
        }
        QStringList allowedMemberIds;
        if (!groupFilter.isEmpty()) {
            allowedMemberIds = m_registry->getGroupMemberIds(groupFilter);
        }

        QList<MemberInfo> allMembers = m_registry->getAllMembers();
        for (const MemberInfo& member : allMembers) {
            if (!member.active) continue;
            if (!allowedMemberIds.isEmpty() && !allowedMemberIds.contains(member.id)) continue;
            WmFolderInfo info;
            info.folderName = QFileInfo(localPath).fileName();
            if (info.folderName.isEmpty()) info.folderName = localPath;
            info.fullPath = localPath;
            info.memberId = member.id;
            info.matchType = "broadcast";
            info.matchConfidence = 5;
            info.matched = true;
            info.selected = true;
            info.isLocalSource = true;
            m_wmFolders.append(info);
        }
        populateTable();
        int blockers = 0;
        int warnings = 0;
        buildDistributionAudit(false, &blockers, &warnings);
        QString groupSuffix = groupFilter.isEmpty()
            ? QString()
            : QString(" (group: %1)").arg(groupFilter);
        QString auditSuffix = blockers > 0
            ? QString(" Audit: %1 blockers.").arg(blockers)
            : (warnings > 0 ? QString(" Audit: %1 warnings.").arg(warnings) : QString(" Audit clean."));
        m_statusLabel->setText(QString("Broadcast (local)%1: %2 members ready. Source: %3.%4")
            .arg(groupSuffix).arg(m_wmFolders.size()).arg(localPath, auditSuffix));
        m_statsLabel->setText(QString("Members: %1 | Source: %2")
            .arg(m_wmFolders.size()).arg(localPath));
        return;
    }

    // Walk local folder — each subfolder = one potential member
    QRegularExpression tsRe("^(.+)_(\\d{8}_\\d{6})$");
    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QFileInfo& entry : entries) {
        const QString folderName = entry.fileName();

        WmFolderInfo info;
        info.folderName = folderName;
        info.fullPath = entry.absoluteFilePath();
        info.isLocalSource = true;

        QRegularExpressionMatch tsMatch = tsRe.match(folderName);
        if (tsMatch.hasMatch()) {
            info.timestamp = tsMatch.captured(2);
        }

        auto match = m_registry->matchFolderToMember(folderName);
        if (match.confidence > 0) {
            info.memberId = match.matchedMemberId;
            info.matchType = match.matchType;
            info.matchConfidence = match.confidence;
            info.matched = true;
        } else {
            info.memberId = folderName;
            info.matchType = "none";
            info.matchConfidence = 0;
            info.matched = false;
        }
        info.selected = info.matched;
        m_wmFolders.append(info);

        qDebug() << "  [local] Found folder:" << info.folderName
                 << "member:" << info.memberId
                 << "match:" << info.matchType;
    }

    // Smart routing pass — uses classifyLocalChildren() for local sources
    int smartRouted = 0;
    if (m_smartRouteCheck->isChecked()) {
        QString month = m_monthCombo->currentText();
        const ContentType fallbackCourseType = routeCourseTypeFromTemplate();
        for (WmFolderInfo& info : m_wmFolders) {
            if (!info.matched) continue;
            MemberInfo member = m_registry->getMember(info.memberId);
            if (member.paths.archiveRoot.isEmpty()) {
                qDebug() << "ContentRouter[local]: Skipping" << info.memberId
                         << "— no archive root configured";
                continue;
            }
            QString fallbackDest = getDestinationPath(info.memberId);
            info.routes = m_contentRouter->classifyLocalChildren(
                info.fullPath, member, month, fallbackDest, fallbackCourseType);
            info.smartRouted = !info.routes.isEmpty();
            if (info.smartRouted) smartRouted++;
            qDebug() << "ContentRouter[local]:" << info.memberId
                     << "\xe2\x86\x92" << info.routes.size() << "routes";
        }
    }

    if (smartRouted > 0) {
        SmartRouteReviewDialog reviewDialog(this);
        reviewDialog.setRoutes(m_wmFolders, m_registry);
        if (reviewDialog.exec() == QDialog::Accepted) {
            m_wmFolders = reviewDialog.getReviewedFolders();
        }
    }

    int matched = 0, unmatched = 0;
    for (const WmFolderInfo& info : m_wmFolders) {
        if (info.matched) matched++;
        else unmatched++;
    }
    QString statsText = QString("Found: %1 local folders (%2 matched, %3 unmatched)")
        .arg(m_wmFolders.size()).arg(matched).arg(unmatched);
    if (smartRouted > 0) statsText += QString(" \xe2\x80\x94 %1 smart-routed").arg(smartRouted);
    populateTable();
    int blockers = 0;
    int warnings = 0;
    buildDistributionAudit(false, &blockers, &warnings);
    QString auditSuffix = blockers > 0
        ? QString(" Audit: %1 blockers.").arg(blockers)
        : (warnings > 0 ? QString(" Audit: %1 warnings.").arg(warnings) : QString(" Audit clean."));
    m_statsLabel->setText(statsText);
    m_statusLabel->setText("Local scan complete." + auditSuffix);
    updateEmptyState();
}

void DistributionPanel::onBroadcastScan() {
    QString sourcePath = m_wmPathEdit->text().trimmed();
    if (sourcePath.isEmpty()) {
        m_statusLabel->setText("Enter a source folder path");
        m_scanBtn->setEnabled(true);
        return;
    }

    if (!m_megaApi) {
        m_statusLabel->setText("Error: Not connected to MEGA");
        m_scanBtn->setEnabled(true);
        return;
    }

    // Validate source exists on MEGA
    mega::MegaNode* sourceNode = m_megaApi->getNodeByPath(sourcePath.toUtf8().constData());
    if (!sourceNode) {
        m_statusLabel->setText("Source folder not found: " + sourcePath);
        m_scanBtn->setEnabled(true);
        return;
    }
    bool isFolder = sourceNode->isFolder();
    delete sourceNode;
    if (!isFolder) {
        m_statusLabel->setText("Source path is not a folder: " + sourcePath);
        m_scanBtn->setEnabled(true);
        return;
    }

    populateBroadcastTable(sourcePath);
    m_scanBtn->setEnabled(true);
}

void DistributionPanel::populateBroadcastTable(const QString& sourcePath) {
    m_wmFolders.clear();

    QString groupFilter;
    if (m_groupCombo) {
        groupFilter = m_groupCombo->itemData(m_groupCombo->currentIndex()).toString();
    }
    QStringList allowedMemberIds;
    if (!groupFilter.isEmpty()) {
        allowedMemberIds = m_registry->getGroupMemberIds(groupFilter);
    }

    QList<MemberInfo> allMembers = m_registry->getAllMembers();
    for (const MemberInfo& member : allMembers) {
        if (!member.active) continue;
        if (!allowedMemberIds.isEmpty() && !allowedMemberIds.contains(member.id)) continue;

        WmFolderInfo info;
        info.folderName = sourcePath.section('/', -1);
        info.fullPath = sourcePath;
        info.memberId = member.id;
        info.matchType = "broadcast";
        info.matchConfidence = 5;
        info.matched = true;
        info.selected = true;
        m_wmFolders.append(info);
    }

    populateTable();
    int blockers = 0;
    int warnings = 0;
    buildDistributionAudit(false, &blockers, &warnings);
    QString auditSuffix = blockers > 0
        ? QString(" Audit: %1 blockers.").arg(blockers)
        : (warnings > 0 ? QString(" Audit: %1 warnings.").arg(warnings) : QString(" Audit clean."));

    QString groupSuffix = groupFilter.isEmpty()
        ? QString()
        : QString(" (group: %1)").arg(groupFilter);
    m_statusLabel->setText(QString("Broadcast mode%1: %2 members ready. Source: %3.%4")
        .arg(groupSuffix).arg(m_wmFolders.size()).arg(sourcePath, auditSuffix));
    m_statsLabel->setText(QString("Members: %1 | Source: %2").arg(m_wmFolders.size()).arg(sourcePath));
}

void DistributionPanel::onFileListReceived(const QVariantList& files) {
    m_scanBtn->setEnabled(true);
    m_wmFolders.clear();

    QString wmBasePath = m_wmPathEdit->text();
    if (!wmBasePath.endsWith("/")) wmBasePath += "/";

    // Extract timestamp if present
    QRegularExpression tsRe("^(.+)_(\\d{8}_\\d{6})$");

    qDebug() << "DistributionPanel: Received" << files.size() << "items";
    qDebug() << "DistributionPanel: Registry has" << m_registry->getAllMembers().size() << "members";

    for (const QVariant& fileVar : files) {
        QVariantMap fileInfo = fileVar.toMap();

        // Only process folders
        if (!fileInfo["isFolder"].toBool()) continue;

        QString folderName = fileInfo["name"].toString();

        WmFolderInfo info;
        info.folderName = folderName;
        info.fullPath = fileInfo["path"].toString();
        if (info.fullPath.isEmpty()) {
            info.fullPath = wmBasePath + folderName;
        }

        // Extract timestamp if present
        QRegularExpressionMatch tsMatch = tsRe.match(folderName);
        if (tsMatch.hasMatch()) {
            info.timestamp = tsMatch.captured(2);
        }

        // Use MemberRegistry smart matching
        auto match = m_registry->matchFolderToMember(folderName);
        if (match.confidence > 0) {
            info.memberId = match.matchedMemberId;
            info.matchType = match.matchType;
            info.matchConfidence = match.confidence;
            info.matched = true;
        } else {
            info.memberId = folderName;  // Default to folder name for manual selection
            info.matchType = "none";
            info.matchConfidence = 0;
            info.matched = false;
        }

        info.selected = info.matched; // Auto-select matched folders
        m_wmFolders.append(info);

        qDebug() << "  Found folder:" << info.folderName
                 << "member:" << info.memberId
                 << "match:" << info.matchType
                 << "confidence:" << info.matchConfidence;
    }

    // Smart routing pass: classify children for matched members
    int smartRouted = 0;
    if (m_smartRouteCheck->isChecked() && m_megaApi) {
        QString month = m_monthCombo->currentText();
        const ContentType fallbackCourseType = routeCourseTypeFromTemplate();
        for (WmFolderInfo& info : m_wmFolders) {
            if (!info.matched) continue;

            MemberInfo member = m_registry->getMember(info.memberId);
            if (member.paths.archiveRoot.isEmpty()) {
                qDebug() << "ContentRouter: Skipping" << info.memberId
                         << "— no archive root configured";
                continue;
            }

            QString fallbackDest = getDestinationPath(info.memberId);
            info.routes = m_contentRouter->classifyChildren(
                m_megaApi, info.fullPath, member, month, fallbackDest, fallbackCourseType);
            info.smartRouted = !info.routes.isEmpty();
            if (info.smartRouted) smartRouted++;

            qDebug() << "ContentRouter:" << info.memberId
                     << "→" << info.routes.size() << "routes"
                     << (info.smartRouted ? "(smart)" : "(empty)");
        }
    }

    // Show Smart Route Review Dialog if any routes were detected
    if (smartRouted > 0) {
        SmartRouteReviewDialog reviewDialog(this);
        reviewDialog.setRoutes(m_wmFolders, m_registry);
        if (reviewDialog.exec() == QDialog::Accepted) {
            m_wmFolders = reviewDialog.getReviewedFolders();
        }
    }

    // Update stats
    int matched = 0, unmatched = 0;
    for (const WmFolderInfo& info : m_wmFolders) {
        if (info.matched) matched++;
        else unmatched++;
    }

    QString statsText = QString("Found: %1 folders (%2 matched, %3 unmatched)")
        .arg(m_wmFolders.size()).arg(matched).arg(unmatched);
    if (smartRouted > 0) {
        statsText += QString(" — %1 smart-routed").arg(smartRouted);
    }
    populateTable();
    int blockers = 0;
    int warnings = 0;
    buildDistributionAudit(false, &blockers, &warnings);
    QString auditSuffix = blockers > 0
        ? QString(" Audit: %1 blockers.").arg(blockers)
        : (warnings > 0 ? QString(" Audit: %1 warnings.").arg(warnings) : QString(" Audit clean."));
    m_statsLabel->setText(statsText);
    m_statusLabel->setText("Scan complete." + auditSuffix);
}

void DistributionPanel::populateTable() {
    m_memberTable->setRowCount(0);
    m_routeMap.clear();

    // Calculate total rows (expand smart-routed folders)
    int totalRows = 0;
    for (const WmFolderInfo& info : m_wmFolders) {
        totalRows++;  // The folder row itself
        if (info.smartRouted) {
            totalRows += info.routes.size();  // Child rows
        }
    }
    m_memberTable->setRowCount(totalRows);

    // Get all members for dropdown
    QList<MemberInfo> allMembers = m_registry->getAllMembers();
    auto& tm = ThemeManager::instance();

    int row = 0;
    for (int folderIdx = 0; folderIdx < m_wmFolders.size(); ++folderIdx) {
        WmFolderInfo& info = m_wmFolders[folderIdx];

        if (info.smartRouted) {
            // ========== Smart-routed member: header row + child rows ==========
            int headerRow = row;

            // Header checkbox: toggles all children
            QCheckBox* headerCheck = new QCheckBox();
            headerCheck->setChecked(info.selected);
            connect(headerCheck, &QCheckBox::toggled, [this, folderIdx, headerRow](bool checked) {
                if (folderIdx >= m_wmFolders.size()) return;
                m_wmFolders[folderIdx].selected = checked;
                // Toggle all child route checkboxes
                for (int r = 0; r < m_wmFolders[folderIdx].routes.size(); ++r) {
                    m_wmFolders[folderIdx].routes[r].selected = checked;
                    int childRow = headerRow + 1 + r;
                    QWidget* w = m_memberTable->cellWidget(childRow, COL_CHECK);
                    if (w) {
                        QCheckBox* c = w->findChild<QCheckBox*>();
                        if (c) { c->blockSignals(true); c->setChecked(checked); c->blockSignals(false); }
                    }
                }
            });
            QWidget* hCheckWidget = new QWidget();
            hCheckWidget->setAutoFillBackground(true);
            QPalette hPal = hCheckWidget->palette();
            hPal.setColor(QPalette::Window, tm.surface2());
            hCheckWidget->setPalette(hPal);
            QHBoxLayout* hCheckLayout = new QHBoxLayout(hCheckWidget);
            hCheckLayout->addWidget(headerCheck);
            hCheckLayout->setAlignment(Qt::AlignCenter);
            hCheckLayout->setContentsMargins(0, 0, 0, 0);
            m_memberTable->setCellWidget(row, COL_CHECK, hCheckWidget);

            // Header source: bold folder name
            QTableWidgetItem* hSource = new QTableWidgetItem(info.folderName);
            QFont boldFont = hSource->font();
            boldFont.setBold(true);
            hSource->setFont(boldFont);
            hSource->setToolTip(info.fullPath);
            hSource->setBackground(tm.surface2());
            m_memberTable->setItem(row, COL_SOURCE_FOLDER, hSource);

            // Header member display
            QString memberDisplay = info.memberId;
            if (m_registry->hasMember(info.memberId)) {
                MemberInfo mi = m_registry->getMember(info.memberId);
                if (!mi.displayName.isEmpty()) memberDisplay = mi.displayName;
            }
            QTableWidgetItem* hMember = new QTableWidgetItem(memberDisplay);
            hMember->setFont(boldFont);
            hMember->setBackground(tm.surface2());
            m_memberTable->setItem(row, COL_MATCHED_MEMBER, hMember);

            // Header match type: "Smart Route"
            QTableWidgetItem* hMatch = new QTableWidgetItem("Smart Route");
            hMatch->setTextAlignment(Qt::AlignCenter);
            hMatch->setForeground(tm.supportInfo());
            hMatch->setFont(boldFont);
            hMatch->setBackground(tm.surface2());
            m_memberTable->setItem(row, COL_MATCH_TYPE, hMatch);

            // Header destination: summary
            QTableWidgetItem* hDest = new QTableWidgetItem(
                QString("%1 routes").arg(info.routes.size()));
            hDest->setFont(boldFont);
            hDest->setBackground(tm.surface2());
            m_memberTable->setItem(row, COL_DESTINATION, hDest);

            // Header status
            QTableWidgetItem* hStatus = new QTableWidgetItem("Ready");
            hStatus->setTextAlignment(Qt::AlignCenter);
            hStatus->setForeground(tm.supportSuccess());
            hStatus->setFont(boldFont);
            hStatus->setBackground(tm.surface2());
            m_memberTable->setItem(row, COL_STATUS, hStatus);

            row++;

            // Child rows for each route
            for (int routeIdx = 0; routeIdx < info.routes.size(); ++routeIdx) {
                ContentRoute& route = info.routes[routeIdx];
                m_routeMap[row] = routeIdx;

                // Child checkbox
                QCheckBox* childCheck = new QCheckBox();
                childCheck->setChecked(route.selected);
                connect(childCheck, &QCheckBox::toggled,
                        [this, folderIdx, routeIdx](bool checked) {
                    if (folderIdx < m_wmFolders.size()
                        && routeIdx < m_wmFolders[folderIdx].routes.size()) {
                        m_wmFolders[folderIdx].routes[routeIdx].selected = checked;
                    }
                });
                QWidget* cCheckWidget = new QWidget();
                QHBoxLayout* cCheckLayout = new QHBoxLayout(cCheckWidget);
                cCheckLayout->addWidget(childCheck);
                cCheckLayout->setAlignment(Qt::AlignCenter);
                cCheckLayout->setContentsMargins(0, 0, 0, 0);
                m_memberTable->setCellWidget(row, COL_CHECK, cCheckWidget);

                // Child source: indented name
                QString prefix = QString::fromUtf8("  \xe2\x86\xb3 ");  // "  ↳ "
                QString displayName = route.isFolder
                    ? route.childName + "/"
                    : route.childName;
                QTableWidgetItem* cSource = new QTableWidgetItem(prefix + displayName);
                cSource->setToolTip(route.isFolder ? route.sourcePath : route.filePaths.join("\n"));
                m_memberTable->setItem(row, COL_SOURCE_FOLDER, cSource);

                // Child member (same as parent)
                QTableWidgetItem* cMember = new QTableWidgetItem(memberDisplay);
                m_memberTable->setItem(row, COL_MATCHED_MEMBER, cMember);

                // Child content type label with color coding
                QTableWidgetItem* cType = new QTableWidgetItem(route.contentTypeLabel);
                cType->setTextAlignment(Qt::AlignCenter);
                switch (route.contentType) {
                case ContentType::NHB_ROOT_FILES: cType->setForeground(tm.supportInfo()); break;
                case ContentType::NHB_COURSES:    cType->setForeground(tm.supportInfo()); break;
                case ContentType::HOT_SEATS:      cType->setForeground(tm.supportSuccess()); break;
                case ContentType::THEORY_CALLS:   cType->setForeground(tm.supportSuccess()); break;
                case ContentType::FF_COURSES:     cType->setForeground(tm.supportWarning()); break;
                case ContentType::FAST_FORWARD:    cType->setForeground(tm.supportWarning()); break;
                case ContentType::UNKNOWN:         cType->setForeground(tm.supportWarning()); break;
                }
                m_memberTable->setItem(row, COL_MATCH_TYPE, cType);

                // Child destination (editable)
                QTableWidgetItem* cDest = new QTableWidgetItem(route.destinationPath);
                cDest->setToolTip(route.destinationPath);
                m_memberTable->setItem(row, COL_DESTINATION, cDest);

                // Child status
                QString cStatus = route.destinationPath.isEmpty() ? "No path" : "Ready";
                QTableWidgetItem* cStatusItem = new QTableWidgetItem(cStatus);
                cStatusItem->setTextAlignment(Qt::AlignCenter);
                cStatusItem->setForeground(
                    route.destinationPath.isEmpty() ? tm.supportWarning() : tm.supportSuccess());
                m_memberTable->setItem(row, COL_STATUS, cStatusItem);

                row++;
            }
        } else {
            // ========== Legacy row: one folder → one destination ==========

            // COL_CHECK: Checkbox
            QCheckBox* check = new QCheckBox();
            check->setChecked(info.selected);
            connect(check, &QCheckBox::toggled, [this, folderIdx](bool checked) {
                if (folderIdx < m_wmFolders.size()) {
                    m_wmFolders[folderIdx].selected = checked;
                }
            });
            QWidget* checkWidget = new QWidget();
            QHBoxLayout* checkLayout = new QHBoxLayout(checkWidget);
            checkLayout->addWidget(check);
            checkLayout->setAlignment(Qt::AlignCenter);
            checkLayout->setContentsMargins(0, 0, 0, 0);
            m_memberTable->setCellWidget(row, COL_CHECK, checkWidget);

            // COL_SOURCE_FOLDER
            QTableWidgetItem* sourceItem = new QTableWidgetItem(info.folderName);
            sourceItem->setToolTip(info.fullPath);
            m_memberTable->setItem(row, COL_SOURCE_FOLDER, sourceItem);

            // COL_MATCHED_MEMBER: Member dropdown
            QComboBox* memberCombo = new QComboBox();
            memberCombo->setToolTip("Choose or correct the registry member matched to this source folder");
            memberCombo->addItem("-- No Match --", QString());
            for (const MemberInfo& member : allMembers) {
                QString display = member.displayName.isEmpty() ? member.id
                    : QString("%1 (%2)").arg(member.displayName, member.id);
                memberCombo->addItem(display, member.id);
            }
            if (info.matched) {
                for (int i = 0; i < memberCombo->count(); ++i) {
                    if (memberCombo->itemData(i).toString() == info.memberId) {
                        memberCombo->setCurrentIndex(i);
                        break;
                    }
                }
            }
            if (!info.matched) {
                memberCombo->setProperty("error", true);
            }
            int capturedRow = row;
            connect(memberCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    [this, folderIdx, capturedRow, memberCombo]() {
                if (folderIdx >= m_wmFolders.size()) return;
                QString selectedId = memberCombo->currentData().toString();
                if (!selectedId.isEmpty()) {
                    m_wmFolders[folderIdx].memberId = selectedId;
                    m_wmFolders[folderIdx].matched = true;
                    m_wmFolders[folderIdx].selected = true;
                    m_wmFolders[folderIdx].matchType = "manual";
                    m_wmFolders[folderIdx].matchConfidence = 5;
                    auto& tmInner = ThemeManager::instance();
                    if (m_memberTable->item(capturedRow, COL_MATCH_TYPE)) {
                        m_memberTable->item(capturedRow, COL_MATCH_TYPE)->setText("Manual");
                        m_memberTable->item(capturedRow, COL_MATCH_TYPE)->setForeground(tmInner.supportInfo());
                    }
                    QString dest = getDestinationPath(selectedId);
                    if (m_memberTable->item(capturedRow, COL_DESTINATION)) {
                        m_memberTable->item(capturedRow, COL_DESTINATION)->setText(dest);
                        m_memberTable->item(capturedRow, COL_DESTINATION)->setToolTip(dest);
                    }
                    if (m_memberTable->item(capturedRow, COL_STATUS)) {
                        m_memberTable->item(capturedRow, COL_STATUS)->setText("Ready");
                        m_memberTable->item(capturedRow, COL_STATUS)->setForeground(tmInner.supportSuccess());
                    }
                    QWidget* w = m_memberTable->cellWidget(capturedRow, COL_CHECK);
                    if (w) {
                        QCheckBox* c = w->findChild<QCheckBox*>();
                        if (c) c->setChecked(true);
                    }
                    memberCombo->setProperty("error", false);
                    memberCombo->style()->polish(memberCombo);
                } else {
                    m_wmFolders[folderIdx].matched = false;
                    m_wmFolders[folderIdx].matchType = "none";
                    m_wmFolders[folderIdx].matchConfidence = 0;
                    memberCombo->setProperty("error", true);
                    memberCombo->style()->polish(memberCombo);
                    auto& tmInner = ThemeManager::instance();
                    if (m_memberTable->item(capturedRow, COL_MATCH_TYPE)) {
                        m_memberTable->item(capturedRow, COL_MATCH_TYPE)->setText("No match");
                        m_memberTable->item(capturedRow, COL_MATCH_TYPE)->setForeground(tmInner.supportError());
                    }
                    if (m_memberTable->item(capturedRow, COL_STATUS)) {
                        m_memberTable->item(capturedRow, COL_STATUS)->setText("Select Member");
                        m_memberTable->item(capturedRow, COL_STATUS)->setForeground(tmInner.supportError());
                    }
                }
            });
            m_memberTable->setCellWidget(row, COL_MATCHED_MEMBER, memberCombo);

            // COL_MATCH_TYPE
            QString matchLabel;
            QColor matchColor;
            if (info.matchType == "pattern") {
                matchLabel = "Pattern";
                matchColor = tm.supportSuccess();
            } else if (info.matchType == "id") {
                matchLabel = "ID match";
                matchColor = tm.supportSuccess();
            } else if (info.matchType == "email") {
                matchLabel = "Email";
                matchColor = tm.supportInfo();
            } else if (info.matchType == "name") {
                matchLabel = "Name";
                matchColor = tm.supportInfo();
            } else if (info.matchType == "fuzzy") {
                matchLabel = "Fuzzy";
                matchColor = tm.supportWarning();
            } else if (info.matchType == "manual") {
                matchLabel = "Manual";
                matchColor = tm.supportInfo();
            } else if (info.matchType == "broadcast") {
                matchLabel = "Broadcast";
                matchColor = tm.supportInfo();
            } else {
                matchLabel = "No match";
                matchColor = tm.supportError();
            }
            QTableWidgetItem* matchItem = new QTableWidgetItem(matchLabel);
            matchItem->setTextAlignment(Qt::AlignCenter);
            matchItem->setForeground(matchColor);
            if (info.matchConfidence > 0) {
                matchItem->setToolTip(QString("Confidence: %1/5").arg(info.matchConfidence));
            }
            m_memberTable->setItem(row, COL_MATCH_TYPE, matchItem);

            // COL_DESTINATION
            QString dest = info.matched ? getDestinationPath(info.memberId) : "";
            if (m_monthCombo->currentText().startsWith("Auto") && !dest.isEmpty()) {
                // Show that month will be auto-detected per file
                dest.replace(QDate::currentDate().toString("MMMM"), "<auto>");
            }
            QTableWidgetItem* destItem = new QTableWidgetItem(dest);
            destItem->setToolTip(dest);
            m_memberTable->setItem(row, COL_DESTINATION, destItem);

            // COL_STATUS
            QString status = info.matched ? "Ready" : "Select Member";
            QTableWidgetItem* statusItem = new QTableWidgetItem(status);
            statusItem->setTextAlignment(Qt::AlignCenter);
            statusItem->setForeground(info.matched ? tm.supportSuccess() : tm.supportError());
            m_memberTable->setItem(row, COL_STATUS, statusItem);

            row++;
        }
    }

    updateEmptyState();
}

void DistributionPanel::updateEmptyState() {
    bool empty = m_memberTable->rowCount() == 0;
    m_emptyState->setVisible(empty);
    m_memberTable->setVisible(!empty);
}

// Extract month name from filename date prefix (e.g., "03-25-2026..." → "March")
static QString extractMonthFromFilename(const QString& filename) {
    static QRegularExpression dateRe("^(\\d{2})-\\d{2}-\\d{4}");
    auto match = dateRe.match(filename);
    if (!match.hasMatch()) return QString();
    int monthNum = match.captured(1).toInt();
    if (monthNum < 1 || monthNum > 12) return QString();
    return QDate(2000, monthNum, 1).toString("MMMM");
}

QString DistributionPanel::getDestinationPath(const QString& memberId, const QString& month) {
    QString templatePath = m_destTemplateEdit->text();

    // Try to find member in registry for full info
    MemberInfo memberInfo;
    if (m_registry) {
        memberInfo = m_registry->getMember(memberId);
    }

    // If member not found, create minimal info
    if (memberInfo.id.isEmpty()) {
        memberInfo.id = memberId;
        memberInfo.displayName = memberId;
        memberInfo.distributionFolder = memberId; // Use memberId as folder path
    }

    // Use TemplateExpander for full variable support
    auto result = TemplateExpander::expandForMember(templatePath, memberInfo);

    // Determine month to use
    QString monthToUse = month;
    if (monthToUse.isEmpty()) {
        monthToUse = m_monthCombo->currentText();
    }
    if (monthToUse.startsWith("Auto")) {
        monthToUse = QDate::currentDate().toString("MMMM"); // Fallback to current month
    }

    if (result.isValid) {
        // Override month in expanded path
        QString expanded = result.expandedPath;
        // TemplateExpander uses system month — replace with our month
        QString sysMonth = QDate::currentDate().toString("MMMM");
        if (sysMonth != monthToUse) {
            expanded.replace(sysMonth, monthToUse);
        }
        return expanded;
    }

    // Fallback: simple replacement if expansion fails
    QString dest = templatePath;
    dest.replace("{member}", memberInfo.distributionFolder.isEmpty() ? memberId : memberInfo.distributionFolder);
    dest.replace("{member_id}", memberId);
    dest.replace("{member_name}", memberInfo.displayName.isEmpty() ? memberId : memberInfo.displayName);
    dest.replace("{month}", monthToUse);
    dest.replace("{year}", QString::number(QDate::currentDate().year()));
    return dest;
}

QString DistributionPanel::autoDetectDistributionIntent() {
    if (!m_wmPathEdit || !m_destTemplateEdit) return {};

    const QString sourcePath = m_wmPathEdit->text().trimmed();
    if (sourcePath.isEmpty()) return {};

    QString groupSignal;
    if (m_groupCombo && m_groupCombo->currentIndex() >= 0) {
        groupSignal = m_groupCombo->currentText() + " "
            + m_groupCombo->itemData(m_groupCombo->currentIndex()).toString();
    }

    const QString signal = (sourcePath + " " + groupSignal).toLower();
    const bool mentionsCourse = signal.contains("course")
        || signal.contains("courses")
        || signal.contains("module")
        || signal.contains("modules")
        || signal.contains("lesson")
        || signal.contains("lessons");
    const bool mentionsUpdated = signal.contains("updated")
        || signal.contains("2021-2024")
        || signal.contains("regularly");
    const bool mentionsNhb = signal.contains("nhb")
        || signal.contains("nothing held back")
        || signal.contains("nothingheldback");
    const bool mentionsFf = signal.contains("fast forward")
        || signal.contains("fast-forward")
        || signal.contains("fastforward")
        || QRegularExpression("\\bff\\b", QRegularExpression::CaseInsensitiveOption)
               .match(signal).hasMatch();

    QString detectedTemplate;
    QString label;
    if (mentionsCourse && mentionsFf) {
        detectedTemplate = "{archive_root}/{fast_forward}/Courses";
        label = "Auto-detected FF Courses";
    } else if (mentionsCourse && mentionsNhb && mentionsUpdated) {
        detectedTemplate = "{archive_root}/NHB+ 2021-2024 - Regularly Updated/NHB+ Courses";
        label = "Auto-detected NHB+ Updated Courses";
    } else if (mentionsCourse && mentionsNhb) {
        detectedTemplate = "{archive_root}/NHB+ Courses";
        label = "Auto-detected NHB+ Courses";
    } else if (mentionsCourse) {
        label = "Course source detected";
    }

    if (mentionsCourse) {
        if (m_copyContentsOnlyCheck) m_copyContentsOnlyCheck->setChecked(true);
        if (m_createDestFolderCheck) m_createDestFolderCheck->setChecked(true);
        if (m_skipExistingCheck) m_skipExistingCheck->setChecked(true);
    }

    if (!detectedTemplate.isEmpty()) {
        const QString current = m_destTemplateEdit->text().trimmed();
        const QStringList replaceableTemplates = {
            "",
            "{member}",
            "{archive_root}/NHB+ Courses",
            "{archive_root}/NHB+ 2021-2024 - Regularly Updated/NHB+ Courses",
            "{archive_root}/{fast_forward}/Courses"
        };
        if (replaceableTemplates.contains(current)) {
            m_destTemplateEdit->setText(detectedTemplate);
            if (m_quickTemplateCombo) {
                const int idx = m_quickTemplateCombo->findData(detectedTemplate);
                if (idx >= 0) {
                    m_quickTemplateCombo->blockSignals(true);
                    m_quickTemplateCombo->setCurrentIndex(idx);
                    m_quickTemplateCombo->blockSignals(false);
                }
            }
        } else {
            label += " (kept custom destination)";
        }
    }

    return label;
}

ContentType DistributionPanel::routeCourseTypeFromTemplate() const {
    if (!m_destTemplateEdit) return ContentType::UNKNOWN;

    const QString tmpl = m_destTemplateEdit->text().trimmed().toLower();
    if (tmpl.isEmpty()) return ContentType::UNKNOWN;

    if (tmpl.contains("{fast_forward}")
        || tmpl.contains("fast_forward")
        || tmpl.contains("fast forward")
        || tmpl.contains("fast-forward")
        || tmpl.contains("ff courses")
        || tmpl.contains("ff/course")
        || tmpl.contains("ff/courses")) {
        return ContentType::FF_COURSES;
    }

    if (tmpl.contains("nhb+ courses")
        || tmpl.contains("nhb courses")
        || tmpl.contains("nothing held back")
        || tmpl.contains("nothingheldback")) {
        return ContentType::NHB_COURSES;
    }

    return ContentType::UNKNOWN;
}

QString DistributionPanel::buildDistributionAudit(bool includeDetails, int* blockerCount, int* warningCount) {
    int folders = m_wmFolders.size();
    int matched = 0;
    int unmatched = 0;
    int selectedTasks = 0;
    int selectedFolders = 0;
    int selectedRoutes = 0;
    int smartFolders = 0;

    QStringList blockers;
    QStringList warnings;
    QStringList selectedMemberIds;
    QMap<QString, QStringList> destMembers;

    auto rememberDestination = [&destMembers](const QString& dest, const QString& memberId) {
        if (dest.isEmpty()) return;
        QStringList& members = destMembers[dest];
        if (!members.contains(memberId)) members.append(memberId);
    };

    auto checkLocalPath = [&blockers](const QString& path, const QString& label) {
        if (path.isEmpty()) {
            blockers.append(QString("%1 has no source path").arg(label));
            return;
        }
        if (!QFileInfo::exists(path)) {
            blockers.append(QString("%1 source no longer exists: %2").arg(label, path));
        }
    };

    int tableRow = 0;
    for (int i = 0; i < m_wmFolders.size(); ++i) {
        const WmFolderInfo& info = m_wmFolders[i];
        if (info.matched) matched++;
        else unmatched++;
        if (info.smartRouted) smartFolders++;

        if (info.smartRouted) {
            tableRow++; // header row
            for (int r = 0; r < info.routes.size(); ++r) {
                const ContentRoute& route = info.routes[r];
                const int childRow = tableRow + r;
                if (!info.selected || !route.selected) continue;

                selectedTasks++;
                selectedRoutes++;
                if (!selectedMemberIds.contains(route.memberId)) {
                    selectedMemberIds.append(route.memberId);
                }
                if (!info.matched) {
                    blockers.append(QString("%1 is selected but has no matched member").arg(info.folderName));
                }

                if (route.isLocalSource || info.isLocalSource) {
                    if (route.isFolder) {
                        checkLocalPath(route.sourcePath,
                            QString("%1/%2").arg(info.memberId, route.childName));
                    } else if (route.localFilePaths.isEmpty()) {
                        blockers.append(QString("%1/%2 has no local files").arg(info.memberId, route.childName));
                    }
                } else if (route.sourcePath.isEmpty()) {
                    blockers.append(QString("%1/%2 has no cloud source path").arg(info.memberId, route.childName));
                }

                QTableWidgetItem* destItem = m_memberTable->item(childRow, COL_DESTINATION);
                QString dest = destItem ? destItem->text().trimmed() : route.destinationPath;
                if (dest.isEmpty()) {
                    blockers.append(QString("%1/%2 has no destination").arg(info.memberId, route.childName));
                } else {
                    rememberDestination(dest, route.memberId);
                }
            }
            tableRow += info.routes.size();
            continue;
        }

        if (info.selected) {
            selectedTasks++;
            selectedFolders++;
            if (!selectedMemberIds.contains(info.memberId)) selectedMemberIds.append(info.memberId);
            if (!info.matched) {
                blockers.append(QString("%1 is selected but has no matched member").arg(info.folderName));
            }

            if (info.isLocalSource) {
                checkLocalPath(info.fullPath,
                    info.memberId.isEmpty() ? info.folderName : info.memberId);
            } else if (info.fullPath.isEmpty()) {
                blockers.append(QString("%1 has no cloud source path")
                    .arg(info.memberId.isEmpty() ? info.folderName : info.memberId));
            }

            QTableWidgetItem* destItem = m_memberTable->item(tableRow, COL_DESTINATION);
            QString dest = destItem ? destItem->text().trimmed() : getDestinationPath(info.memberId);
            if (dest.isEmpty()) {
                blockers.append(QString("%1 has no destination")
                    .arg(info.memberId.isEmpty() ? info.folderName : info.memberId));
            } else {
                rememberDestination(dest, info.memberId);
            }
        }
        tableRow++;
    }

    if (selectedTasks == 0) {
        blockers.append("No selected folders or routes");
    }

    for (auto it = destMembers.constBegin(); it != destMembers.constEnd(); ++it) {
        if (it.value().size() > 1) {
            warnings.append(QString("Destination is shared by multiple members: %1 (%2)")
                .arg(it.key(), it.value().join(", ")));
        }
    }

    QString groupFilter;
    if (m_groupCombo && m_groupCombo->currentIndex() > 0) {
        groupFilter = m_groupCombo->itemData(m_groupCombo->currentIndex()).toString();
    }
    if (!groupFilter.isEmpty() && m_registry) {
        const QStringList groupMembers = m_registry->getGroupMemberIds(groupFilter);
        QStringList missingMembers;
        for (const QString& memberId : groupMembers) {
            if (!selectedMemberIds.contains(memberId)) missingMembers.append(memberId);
        }
        if (!missingMembers.isEmpty()) {
            warnings.append(QString("%1 group members are not selected/found: %2")
                .arg(missingMembers.size())
                .arg(missingMembers.mid(0, 12).join(", ")));
        }
    }

    if (blockerCount) *blockerCount = blockers.size();
    if (warningCount) *warningCount = warnings.size();

    QStringList report;
    report.append("Distribution preflight audit");
    report.append(QString("Folders scanned: %1 (%2 matched, %3 unmatched)")
        .arg(folders).arg(matched).arg(unmatched));
    report.append(QString("Selected work: %1 tasks (%2 folder rows, %3 smart-route rows)")
        .arg(selectedTasks).arg(selectedFolders).arg(selectedRoutes));
    report.append(QString("Smart-routed folders: %1").arg(smartFolders));
    report.append(QString("Blockers: %1").arg(blockers.size()));
    if (includeDetails && !blockers.isEmpty()) {
        for (const QString& blocker : blockers.mid(0, 20)) {
            report.append("  - " + blocker);
        }
        if (blockers.size() > 20) {
            report.append(QString("  - ...and %1 more").arg(blockers.size() - 20));
        }
    }
    report.append(QString("Warnings: %1").arg(warnings.size()));
    if (includeDetails && !warnings.isEmpty()) {
        for (const QString& warning : warnings.mid(0, 20)) {
            report.append("  - " + warning);
        }
        if (warnings.size() > 20) {
            report.append(QString("  - ...and %1 more").arg(warnings.size() - 20));
        }
    }

    return report.join("\n");
}

bool DistributionPanel::confirmDistributionAudit() {
    int blockers = 0;
    int warnings = 0;
    const QString report = buildDistributionAudit(true, &blockers, &warnings);

    if (blockers > 0) {
        QMessageBox::warning(this, "Distribution Audit", report);
        return false;
    }

    if (warnings > 0) {
        auto reply = QMessageBox::question(this, "Distribution Audit",
            report + "\n\nContinue anyway?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        return reply == QMessageBox::Yes;
    }

    return true;
}

void DistributionPanel::onRunAudit() {
    int blockers = 0;
    int warnings = 0;
    const QString report = buildDistributionAudit(true, &blockers, &warnings);

    if (blockers > 0) {
        QMessageBox::warning(this, "Distribution Audit", report);
    } else if (warnings > 0) {
        QMessageBox::information(this, "Distribution Audit", report);
    } else {
        QMessageBox::information(this, "Distribution Audit", report + "\n\nReady to start.");
    }
}

void DistributionPanel::onSelectAll() {
    for (int row = 0; row < m_memberTable->rowCount(); ++row) {
        QWidget* widget = m_memberTable->cellWidget(row, COL_CHECK);
        if (widget) {
            QCheckBox* check = widget->findChild<QCheckBox*>();
            if (check) check->setChecked(true);
            // Data is updated via checkbox signal connections
        }
    }
}

void DistributionPanel::onDeselectAll() {
    for (int row = 0; row < m_memberTable->rowCount(); ++row) {
        QWidget* widget = m_memberTable->cellWidget(row, COL_CHECK);
        if (widget) {
            QCheckBox* check = widget->findChild<QCheckBox*>();
            if (check) check->setChecked(false);
        }
    }
}

void DistributionPanel::onPreviewDistribution() {
    QStringList preview;
    int selectedCount = 0;
    int skippedCount = 0;

    bool copyFolder = !m_copyContentsOnlyCheck->isChecked();

    int tableRow = 0;
    for (int i = 0; i < m_wmFolders.size(); ++i) {
        const WmFolderInfo& info = m_wmFolders[i];

        if (!info.selected) {
            // Advance table row even for unselected folders
            tableRow += info.smartRouted ? (1 + info.routes.size()) : 1;
            continue;
        }

        if (info.smartRouted) {
            // Smart-routed: show each child route (read destinations from data, not table)
            preview.append(QString("--- %1 (Smart Route) ---").arg(info.memberId));
            int childStart = tableRow + 1;  // Skip header row
            for (int r = 0; r < info.routes.size(); ++r) {
                const ContentRoute& route = info.routes[r];
                if (!route.selected) continue;
                // Read destination from table cell in case user edited it
                int childRow = childStart + r;
                QTableWidgetItem* destItem = m_memberTable->item(childRow, COL_DESTINATION);
                QString dest = (destItem && !destItem->text().trimmed().isEmpty())
                    ? destItem->text().trimmed() : route.destinationPath;
                QString arrow = QString::fromUtf8(" \xe2\x86\x92 ");  // " → "
                preview.append(QString("  [%1] %2%3%4")
                    .arg(route.contentTypeLabel)
                    .arg(route.childName)
                    .arg(arrow)
                    .arg(dest));
                selectedCount++;
            }
            tableRow += 1 + info.routes.size();
        } else {
            // Legacy mode — use tableRow (not i) to read from the table
            QString source = info.fullPath;
            bool autoMonth = m_monthCombo->currentText().startsWith("Auto");

            if (source.isEmpty()) {
                QTableWidgetItem* destItem = m_memberTable->item(tableRow, COL_DESTINATION);
                QString dest = destItem ? destItem->text().trimmed() : "";
                if (dest.isEmpty()) dest = getDestinationPath(info.memberId);
                preview.append(QString("[NO SOURCE] %1 -> %2")
                    .arg(info.memberId.isEmpty() ? "(unmatched)" : info.memberId)
                    .arg(dest));
                skippedCount++;
            } else if (autoMonth) {
                // Auto-month: scan children and show per-month breakdown
                QMap<QString, int> monthCounts;
                if (info.isLocalSource) {
                    QDir srcDir(source);
                    const QFileInfoList entries = srcDir.entryInfoList(QDir::Files);
                    for (const QFileInfo& fi : entries) {
                        QString month = extractMonthFromFilename(fi.fileName());
                        if (month.isEmpty()) month = "Unknown";
                        monthCounts[month]++;
                    }
                } else if (m_megaApi) {
                    std::unique_ptr<mega::MegaNode> srcNode(
                        m_megaApi->getNodeByPath(source.toUtf8().constData()));
                    if (srcNode) {
                        std::unique_ptr<mega::MegaNodeList> children(m_megaApi->getChildren(srcNode.get()));
                        if (children) {
                            for (int c = 0; c < children->size(); ++c) {
                                mega::MegaNode* child = children->get(c);
                                if (!child || child->isFolder()) continue;
                                QString name = QString::fromUtf8(child->getName());
                                QString month = extractMonthFromFilename(name);
                                if (month.isEmpty()) month = "Unknown";
                                monthCounts[month]++;
                            }
                        }
                    }
                }
                if (!monthCounts.isEmpty()) {
                    QString arrow = QString::fromUtf8(" \xe2\x86\x92 ");
                    QString header = info.isLocalSource
                        ? QString("--- %1 (Local) ---").arg(info.memberId)
                        : QString("--- %1 ---").arg(info.memberId);
                    preview.append(header);
                    for (auto it = monthCounts.constBegin(); it != monthCounts.constEnd(); ++it) {
                        QString dest = getDestinationPath(info.memberId, it.key());
                        preview.append(QString("  %1 (%2 files)%3%4")
                            .arg(it.key()).arg(it.value()).arg(arrow).arg(dest));
                    }
                }
            } else if (copyFolder) {
                QTableWidgetItem* destItem = m_memberTable->item(tableRow, COL_DESTINATION);
                QString dest = destItem ? destItem->text().trimmed() : "";
                if (dest.isEmpty()) dest = getDestinationPath(info.memberId);
                preview.append(QString("%1 -> %2%3")
                    .arg(source).arg(dest).arg(info.folderName));
            } else {
                QTableWidgetItem* destItem = m_memberTable->item(tableRow, COL_DESTINATION);
                QString dest = destItem ? destItem->text().trimmed() : "";
                if (dest.isEmpty()) dest = getDestinationPath(info.memberId);
                preview.append(QString("%1/* -> %2").arg(source).arg(dest));
            }
            selectedCount++;
            tableRow++;
        }
    }

    if (selectedCount == 0) {
        QMessageBox::information(this, "Preview", "No members selected for distribution.");
        return;
    }

    QString msg = QString("Will copy %1 to %2 member folders:\n\n")
        .arg(copyFolder ? "folders" : "folder contents")
        .arg(selectedCount);
    if (skippedCount > 0) {
        msg += QString("(%1 %2 no source path and will be skipped)\n\n").arg(skippedCount).arg(skippedCount == 1 ? "row has" : "rows have");
    }
    msg += preview.join("\n");

    if (m_removeWatermarkSuffixCheck->isChecked()) {
        msg += "\n\nNote: '_watermarked' suffix will be removed from filenames.";
    }

    if (m_skipExistingCheck->isChecked()) {
        msg += "\n\nConflict handling: Skip existing files/folders";
    } else {
        msg += "\n\nConflict handling: Overwrite existing files/folders";
    }

    QMessageBox::information(this, "Distribution Preview", msg);
}

void DistributionPanel::onStartDistribution() {
    if (m_isRunning) return;

    // Broadcast shortcut: when the user enables Broadcast and clicks Start without
    // first clicking Scan, auto-populate from the source path. Honors any group
    // filter currently selected in the Group combo.
    if (!m_controllerActive && m_broadcastCheck && m_broadcastCheck->isChecked()
        && m_wmFolders.isEmpty()) {
        if (m_wmPathEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, "Error",
                "Enter a source path before starting a broadcast.");
            return;
        }
        onScanWmFolder();
        if (m_wmFolders.isEmpty()) {
            // Scan reported its own status/error; nothing more to do.
            return;
        }
    }

    // Controller-active mode: upload pending member files via DistributionController
    if (m_controllerActive && m_distController && !m_pendingMemberFileMap.isEmpty()) {
        const UploadMapValidation validation =
            validateMemberFileMapForUpload(m_pendingMemberFileMap, m_registry);
        if (!validation.missingFiles.isEmpty() || !validation.membersWithoutFolder.isEmpty()) {
            QStringList warnings;
            if (!validation.missingFiles.isEmpty()) {
                warnings << QString("%1 local file(s) no longer exist:\n%2")
                    .arg(validation.missingFiles.size())
                    .arg(validation.missingFiles.mid(0, 20).join("\n"));
            }
            if (!validation.membersWithoutFolder.isEmpty()) {
                warnings << QString("%1 member(s) have no distribution folder:\n%2")
                    .arg(validation.membersWithoutFolder.size())
                    .arg(validation.membersWithoutFolder.mid(0, 20).join("\n"));
            }

            QMessageBox::warning(this, "Upload List Changed",
                QString("The upload list changed before start. Incomplete member batches were removed.\n\n%1\n\n"
                        "Review the updated list and click Start again.")
                    .arg(warnings.join("\n\n")));
            prepareForUpload(validation.memberFileMap);
            return;
        }

        m_pendingMemberFileMap = validation.memberFileMap;

        int totalFiles = 0;
        for (const auto& files : m_pendingMemberFileMap) totalFiles += files.size();

        int ret = QMessageBox::question(this, "Confirm Upload",
            QString("Upload %1 files to %2 member folders?")
                .arg(totalFiles).arg(m_pendingMemberFileMap.size()),
            QMessageBox::Yes | QMessageBox::No);
        if (ret != QMessageBox::Yes) {
            m_retrySourceJobId.clear();
            return;
        }

        m_currentJobCancelled = false;
        m_currentJobPlannedCount = m_pendingMemberFileMap.size();

        QJsonObject metadata;
        metadata["retryMode"] = "direct_upload";
        metadata["operation"] = "upload";
        metadata["memberFileMap"] = memberFileMapToJson(m_pendingMemberFileMap);
        metadata["memberCount"] = m_pendingMemberFileMap.size();
        metadata["totalFiles"] = totalFiles;
        metadata["source"] = "watermark_auto_upload";
        if (!m_retrySourceJobId.isEmpty()) {
            metadata["retryOfJobId"] = m_retrySourceJobId;
        }
        m_currentJobId = OperationJobStore::instance().createJob(
            OperationJobType::Distribution,
            QString("Upload %1 files to %2 member folders")
                .arg(totalFiles)
                .arg(m_pendingMemberFileMap.size()),
            m_currentJobPlannedCount,
            metadata);
        saveDistributionCheckpoint("created_direct_upload");
        m_retrySourceJobId.clear();

        m_isRunning = true;
        m_startBtn->setEnabled(false);
        m_pauseBtn->setEnabled(true);
        m_stopBtn->setEnabled(true);
        AnimationHelper::smoothShow(m_progressBar);
        m_progressBar->setValue(0);
        m_progressBar->setMaximum(qMax(1, m_currentJobPlannedCount));
        m_successCount = 0;
        m_failCount = 0;
        m_statusLabel->setText(QString("Uploading %1 files to %2 members...")
            .arg(totalFiles).arg(m_pendingMemberFileMap.size()));
        emit distributionStarted();

        m_distController->uploadToMembers(m_pendingMemberFileMap);
        m_pendingMemberFileMap.clear();
        return;
    }

    // Check for a local-only batch: only CloudCopier is optional in that case
    bool allLocal = !m_wmFolders.isEmpty();
    for (const WmFolderInfo& info : m_wmFolders) {
        if (info.selected && !info.isLocalSource) { allLocal = false; break; }
    }

    if (!allLocal && !m_cloudCopier) {
        QMessageBox::warning(this, "Error", "CloudCopier not available. Make sure you're logged in.");
        return;
    }
    if (!m_megaApi) {
        QMessageBox::warning(this, "Error", "MEGA API not available. Make sure you're logged in.");
        return;
    }

    // Validate template first
    QString templatePath = m_destTemplateEdit->text();
    QString validationError;
    if (!TemplateExpander::validateTemplate(templatePath, &validationError)) {
        QMessageBox::warning(this, "Invalid Template",
            QString("Template validation failed:\n%1").arg(validationError));
        return;
    }

    if (!confirmDistributionAudit()) {
        return;
    }

    // Build task list from selected items
    QList<FolderCopyTask> tasks;
    bool copyFolderItself = !m_copyContentsOnlyCheck->isChecked();
    QStringList skippedNoSource;

    // Track table row mapping for smart-routed folders
    int tableRow = 0;

    for (int i = 0; i < m_wmFolders.size(); ++i) {
        const WmFolderInfo& info = m_wmFolders[i];

        if (info.smartRouted) {
            // Smart-routed: build per-child tasks from routes
            tableRow++;  // Skip header row

            if (info.selected) {
                for (int r = 0; r < info.routes.size(); ++r) {
                    const ContentRoute& route = info.routes[r];
                    int childRow = tableRow + r;

                    if (!route.selected) continue;

                    // Read destination from table cell (user may have edited it)
                    QTableWidgetItem* destItem = m_memberTable->item(childRow, COL_DESTINATION);
                    QString cellDest = destItem ? destItem->text().trimmed() : "";
                    QString dest = cellDest.isEmpty() ? route.destinationPath : cellDest;

                    if (dest.isEmpty()) {
                        skippedNoSource.append(QString("%1/%2").arg(info.memberId, route.childName));
                        continue;
                    }

                    FolderCopyTask task;
                    task.index = childRow;
                    task.memberId = route.memberId;
                    task.destPath = dest;
                    task.isSmartRouteChild = true;
                    task.contentTypeLabel = route.contentTypeLabel;
                    task.isLocalSource = route.isLocalSource || info.isLocalSource;

                    if (route.isFolder) {
                        // Subfolder → respect "copy contents only" checkbox
                        task.sourcePath = route.sourcePath;
                        task.copyFolderItself = copyFolderItself;
                    } else {
                        // Root files → copy individual files
                        task.sourcePath = route.sourcePath;
                        task.individualFiles = task.isLocalSource
                            ? route.localFilePaths
                            : route.filePaths;
                        task.copyFolderItself = false;
                    }

                    tasks.append(task);
                }
            }
            tableRow += info.routes.size();
        } else {
            // Legacy mode: one folder → one destination
            if (!info.selected) {
                tableRow++;
                continue;
            }

            if (info.fullPath.isEmpty()) {
                skippedNoSource.append(info.memberId.isEmpty() ? QString("Row %1").arg(i + 1) : info.memberId);
                tableRow++;
                continue;
            }

            bool autoMonth = m_monthCombo->currentText().startsWith("Auto");

            if (autoMonth) {
                // Auto-month: scan source children and split by month
                QMap<QString, QStringList> monthFiles;

                if (info.isLocalSource) {
                    QDir srcDir(info.fullPath);
                    const QFileInfoList entries = srcDir.entryInfoList(QDir::Files);
                    for (const QFileInfo& fi : entries) {
                        QString month = extractMonthFromFilename(fi.fileName());
                        if (month.isEmpty()) month = "Unknown";
                        monthFiles[month].append(fi.absoluteFilePath());
                    }
                } else if (m_megaApi) {
                    std::unique_ptr<mega::MegaNode> srcNode(
                        m_megaApi->getNodeByPath(info.fullPath.toUtf8().constData()));
                    if (srcNode) {
                        std::unique_ptr<mega::MegaNodeList> children(m_megaApi->getChildren(srcNode.get()));
                        if (children) {
                            for (int c = 0; c < children->size(); ++c) {
                                mega::MegaNode* child = children->get(c);
                                if (!child || child->isFolder()) continue;
                                QString name = QString::fromUtf8(child->getName());
                                QString month = extractMonthFromFilename(name);
                                if (month.isEmpty()) month = "Unknown";
                                monthFiles[month].append(info.fullPath + "/" + name);
                            }
                        }
                    }
                }

                for (auto it = monthFiles.constBegin(); it != monthFiles.constEnd(); ++it) {
                    FolderCopyTask task;
                    task.index = tableRow;
                    task.memberId = info.memberId;
                    task.sourcePath = info.fullPath;
                    task.isSmartRouteChild = true;
                    task.individualFiles = it.value();
                    task.contentTypeLabel = it.key();
                    task.destPath = getDestinationPath(info.memberId, it.key());
                    task.copyFolderItself = false;
                    task.isLocalSource = info.isLocalSource;
                    tasks.append(task);
                }
            } else {
                // Standard: single destination for entire folder
                FolderCopyTask task;
                task.index = tableRow;
                task.memberId = info.memberId;
                task.copyFolderItself = copyFolderItself;
                task.sourcePath = info.fullPath;
                task.isLocalSource = info.isLocalSource;

                QTableWidgetItem* destItem = m_memberTable->item(tableRow, COL_DESTINATION);
                QString cellDest = destItem ? destItem->text().trimmed() : "";
                task.destPath = cellDest.isEmpty() ? getDestinationPath(info.memberId) : cellDest;

                if (task.destPath.isEmpty()) {
                    skippedNoSource.append(info.memberId.isEmpty() ? QString("Row %1").arg(i + 1) : info.memberId);
                    tableRow++;
                    continue;
                }

                tasks.append(task);
            }
            tableRow++;
        }
    }

    if (!skippedNoSource.isEmpty()) {
        QMessageBox::warning(this, "Skipped Rows",
            QString("%1 %2 skipped (no source path):\n%3\n\n"
                    "Manual/imported rows require a scanned source folder.")
                .arg(skippedNoSource.size()).arg(skippedNoSource.size() == 1 ? "row" : "rows").arg(skippedNoSource.join(", ")));
    }

    if (tasks.isEmpty()) {
        QMessageBox::warning(this, "Error", "No members selected for distribution.");
        return;
    }

    // Pre-flight destination path validation
    // (Destinations are MEGA paths regardless of source type, so we still validate.)
    if (m_megaApi) {
        QStringList destPaths;
        for (const auto& task : tasks) {
            destPaths.append(task.destPath);
        }
        auto validationResults = CloudPathValidator::validatePaths(m_megaApi, destPaths);
        if (!CloudPathValidator::allValid(validationResults)) {
            auto action = CloudPathValidator::showValidationDialog(this, validationResults, "Distribution");
            if (action == CloudPathValidator::Cancel) {
                return;
            }
            if (action == CloudPathValidator::ProceedValidOnly) {
                QStringList validDests = CloudPathValidator::validPaths(validationResults);
                QList<FolderCopyTask> validTasks;
                for (const auto& task : tasks) {
                    if (validDests.contains(task.destPath)) {
                        validTasks.append(task);
                    }
                }
                tasks = validTasks;
                if (tasks.isEmpty()) {
                    QMessageBox::warning(this, "Error", "No valid destinations remaining after validation.");
                    return;
                }
            }
            // CreateAndProceed: let CloudCopier auto-create as before
        }
    }

    bool moveMode = m_moveFilesCheck->isChecked();
    QString operationType = moveMode ? "MOVE" : "Copy";
    QString copyMode = copyFolderItself ? "entire folder" : "folder contents only";
    QString conflictMode = m_skipExistingCheck->isChecked() ? "skip existing" : "overwrite existing";

    // Log all tasks for debugging
    qDebug() << "Distribution: Building" << tasks.size() << "tasks (" << operationType << "," << copyMode << ")";
    for (int t = 0; t < tasks.size(); ++t) {
        const auto& task = tasks[t];
        qDebug() << "  Task" << t << ":" << task.sourcePath << "->" << task.destPath
                 << (task.isSmartRouteChild ? "[smart]" : "")
                 << (task.copyFolderItself ? "[folder]" : "[contents]")
                 << "files:" << task.individualFiles.size()
                 << "label:" << task.contentTypeLabel;
    }

    // Show extra warning for move mode
    if (moveMode) {
        QMessageBox::StandardButton reply = QMessageBox::warning(
            this,
            "Confirm Move Operation",
            QString("Move mode is enabled. Source files will be DELETED after transfer.\n\n"
                    "This will move content from %1 source folders to their respective destinations.\n\n"
                    "Are you sure you want to continue?").arg(tasks.size()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (reply != QMessageBox::Yes) return;
    } else {
        int ret = QMessageBox::question(this, "Confirm Distribution",
            QString("Start distribution to %1 members?\n\nOperation: %2\nMode: %3\nConflict handling: %4")
                .arg(tasks.size()).arg(operationType).arg(copyMode).arg(conflictMode),
            QMessageBox::Yes | QMessageBox::No);
        if (ret != QMessageBox::Yes) return;
    }

    saveLastJobProfile();

    // Update UI state
    m_isRunning = true;
    m_isPaused = false;
    m_startBtn->setEnabled(false);
    m_pauseBtn->setEnabled(true);
    m_stopBtn->setEnabled(true);
    m_progressBar->setValue(0);
    m_progressBar->setMaximum(tasks.size());
    AnimationHelper::smoothShow(m_progressBar);
    m_successCount = 0;
    m_failCount = 0;
    m_currentJobCancelled = false;
    m_currentJobPlannedCount = tasks.size();

    QJsonObject metadata;
    metadata["retryMode"] = "folder_tasks";
    metadata["operation"] = moveMode ? "move" : "copy";
    metadata["copyFolderItself"] = copyFolderItself;
    metadata["conflictMode"] = m_skipExistingCheck->isChecked() ? "skip_existing" : "overwrite_existing";
    metadata["skipExisting"] = m_skipExistingCheck && m_skipExistingCheck->isChecked();
    metadata["createDestFolder"] = m_createDestFolderCheck && m_createDestFolderCheck->isChecked();
    metadata["copyContentsOnly"] = m_copyContentsOnlyCheck && m_copyContentsOnlyCheck->isChecked();
    metadata["moveFiles"] = m_moveFilesCheck && m_moveFilesCheck->isChecked();
    metadata["removeWatermarkSuffix"] = m_removeWatermarkSuffixCheck && m_removeWatermarkSuffixCheck->isChecked();
    metadata["templatePath"] = templatePath;
    metadata["sourcePath"] = m_wmPathEdit->text();
    metadata["sourceType"] = m_sourceTypeCombo ? m_sourceTypeCombo->currentData().toString() : QString("cloud");
    metadata["month"] = m_monthCombo ? m_monthCombo->currentText() : QString();
    metadata["groupId"] = m_groupCombo ? m_groupCombo->itemData(m_groupCombo->currentIndex()).toString() : QString();
    metadata["broadcast"] = m_broadcastCheck && m_broadcastCheck->isChecked();
    metadata["smartRoute"] = m_smartRouteCheck && m_smartRouteCheck->isChecked();
    metadata["tasks"] = folderCopyTasksToJson(tasks);
    if (!m_retrySourceJobId.isEmpty()) {
        metadata["retryOfJobId"] = m_retrySourceJobId;
    }
    m_currentJobId = OperationJobStore::instance().createJob(
        OperationJobType::Distribution,
        QString("%1 %2 distribution %3")
            .arg(moveMode ? "Move" : "Copy")
            .arg(tasks.size())
            .arg(tasks.size() == 1 ? "task" : "tasks"),
        tasks.size(),
        metadata);
    saveDistributionCheckpoint("created");
    m_retrySourceJobId.clear();

    emit distributionStarted();
    OperationJobStore::instance().markRunning(
        m_currentJobId,
        QString("%1 %2 distribution %3")
            .arg(moveMode ? "Moving" : "Copying")
            .arg(tasks.size())
            .arg(tasks.size() == 1 ? "task" : "tasks"));

    // Start worker thread
    cleanupWorkerThread();

    m_workerThread = new QThread(this);
    m_copyWorker = new FolderCopyWorker();
    m_copyWorker->moveToThread(m_workerThread);

    // Configure worker
    m_copyWorker->setTasks(tasks);
    m_copyWorker->setCloudCopier(m_cloudCopier.get());
    m_copyWorker->setMegaApi(m_megaApi);
    m_copyWorker->setSkipExisting(m_skipExistingCheck->isChecked());
    m_copyWorker->setCreateDestFolder(m_createDestFolderCheck->isChecked());
    m_copyWorker->setMoveMode(m_moveFilesCheck->isChecked());
    m_isMoving = m_moveFilesCheck->isChecked();

    // Connect signals
    connect(m_workerThread, &QThread::started, m_copyWorker, &FolderCopyWorker::process);
    // Cross-thread connections - must use Qt::QueuedConnection for thread safety
    connect(m_copyWorker, &FolderCopyWorker::taskStarted,
            this, &DistributionPanel::onWorkerTaskStarted, Qt::QueuedConnection);
    connect(m_copyWorker, &FolderCopyWorker::taskCompleted,
            this, &DistributionPanel::onWorkerTaskCompleted, Qt::QueuedConnection);
    connect(m_copyWorker, &FolderCopyWorker::destinationFolderCreated,
            this, &DistributionPanel::recordDistributionCreatedFolder, Qt::QueuedConnection);
    connect(m_copyWorker, &FolderCopyWorker::remoteFileUploaded,
            this, &DistributionPanel::recordDistributionUploadedFile, Qt::QueuedConnection);
    connect(m_copyWorker, &FolderCopyWorker::allCompleted,
            this, &DistributionPanel::onWorkerAllCompleted, Qt::QueuedConnection);
    connect(m_copyWorker, &FolderCopyWorker::progress,
            this, &DistributionPanel::onWorkerProgress, Qt::QueuedConnection);

    // Cleanup connections
    connect(m_copyWorker, &FolderCopyWorker::allCompleted,
            m_workerThread, &QThread::quit, Qt::QueuedConnection);
    connect(m_workerThread, &QThread::finished, m_copyWorker, &QObject::deleteLater);

    m_workerThread->start();
}

void DistributionPanel::onStopDistribution() {
    auto reply = QMessageBox::question(this, "Stop Distribution",
        "Are you sure you want to stop? Progress will be lost.",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    m_currentJobCancelled = true;
    saveDistributionCheckpoint("cancel_requested");
    OperationJobStore::instance().markCancelled(
        m_currentJobId,
        "Distribution cancellation requested");

    if (m_controllerActive && m_distController) {
        m_distController->cancel();
    } else if (m_copyWorker) {
        m_copyWorker->cancel();
    }
    m_statusLabel->setText("Stopping...");
    m_progressBar->setProperty("paused", false);
    m_progressBar->style()->polish(m_progressBar);
}

void DistributionPanel::onPauseDistribution() {
    if (m_controllerActive && m_distController) {
        if (m_isPaused) {
            m_distController->resume();
            m_isPaused = false;
            m_pauseBtn->setText("Pause");
            m_statusLabel->setText("Upload resumed");
            m_progressBar->setProperty("paused", false);
            m_progressBar->style()->polish(m_progressBar);
            OperationJobStore::instance().markRunning(m_currentJobId, "Upload resumed");
        } else {
            m_distController->pause();
            m_isPaused = true;
            m_pauseBtn->setText("Resume");
            m_statusLabel->setText("Upload paused");
            m_progressBar->setProperty("paused", true);
            m_progressBar->style()->polish(m_progressBar);
            OperationJobStore::instance().markPaused(m_currentJobId, "Upload paused");
        }
        return;
    }

    if (!m_copyWorker) return;

    if (m_isPaused) {
        m_copyWorker->resume();
        m_isPaused = false;
        m_pauseBtn->setText("Pause");
        m_statusLabel->setText("Distribution resumed");
        m_progressBar->setProperty("paused", false);
        m_progressBar->style()->polish(m_progressBar);
        OperationJobStore::instance().markRunning(m_currentJobId, "Distribution resumed");
    } else {
        m_copyWorker->pause();
        m_isPaused = true;
        m_pauseBtn->setText("Resume");
        m_statusLabel->setText("Distribution paused");
        m_progressBar->setProperty("paused", true);
        m_progressBar->style()->polish(m_progressBar);
        OperationJobStore::instance().markPaused(m_currentJobId, "Distribution paused");
    }
}

// ==================== Worker Thread Slots ====================

void DistributionPanel::onWorkerTaskStarted(int index, const QString& source, const QString& dest) {
    Q_UNUSED(source);

    // index is now the table row (works for both legacy and smart-routed)
    if (index < m_memberTable->rowCount()) {
        QTableWidgetItem* memberItem = m_memberTable->item(index, COL_MATCHED_MEMBER);
        QString memberDisplay = memberItem ? memberItem->text() : "";
        QString verb = m_isMoving ? "Moving" : "Copying";
        m_statusLabel->setText(QString("%1 %2 -> %3")
            .arg(verb)
            .arg(memberDisplay)
            .arg(dest.section('/', -2)));

        QTableWidgetItem* statusItem = m_memberTable->item(index, COL_STATUS);
        if (statusItem) {
            statusItem->setText(verb + "...");
            statusItem->setForeground(ThemeManager::instance().supportWarning());
        }

        updateCurrentJobProgress(QString("%1 %2 -> %3")
            .arg(verb, memberDisplay, dest));
        saveDistributionCheckpoint("task_started");
    }
}

void DistributionPanel::onWorkerTaskCompleted(int index, bool success, const QString& error) {
    if (index >= m_memberTable->rowCount()) return;

    auto& tm = ThemeManager::instance();
    QTableWidgetItem* statusItem = m_memberTable->item(index, COL_STATUS);

    if (success) {
        m_successCount++;
        if (statusItem) {
            statusItem->setText("Done");
            statusItem->setForeground(tm.supportSuccess());
        }

        // Trigger bulk rename if option is checked
        if (m_removeWatermarkSuffixCheck->isChecked()) {
            QTableWidgetItem* destItem = m_memberTable->item(index, COL_DESTINATION);
            if (destItem && !destItem->text().isEmpty()) {
                executeBulkRename(destItem->text());
            }
        }
    } else {
        m_failCount++;
        if (statusItem) {
            statusItem->setText("Failed");
            statusItem->setForeground(tm.supportError());
            statusItem->setToolTip(error);
        }
        QTableWidgetItem* memberItem = m_memberTable->item(index, COL_MATCHED_MEMBER);
        qDebug() << "Task failed:" << (memberItem ? memberItem->text() : "?") << "-" << error;
        OperationJobStore::instance().setLastError(m_currentJobId, error);
    }

    AnimationHelper::animateProgress(m_progressBar, m_successCount + m_failCount);
    updateCurrentJobProgress(success
        ? "Distribution task completed"
        : QString("Distribution task failed: %1").arg(error.left(160)));
    saveDistributionCheckpoint(success ? "task_completed" : "task_failed");
}

void DistributionPanel::onWorkerAllCompleted(int success, int failed) {
    m_isRunning = false;
    m_isPaused = false;
    m_isMoving = false;
    m_startBtn->setEnabled(true);
    m_pauseBtn->setEnabled(false);
    m_pauseBtn->setText("Pause");
    m_stopBtn->setEnabled(false);
    AnimationHelper::smoothHide(m_progressBar);

    m_statusLabel->setText(QString("Distribution complete: %1 succeeded, %2 failed")
        .arg(success).arg(failed));

    if (!m_currentJobId.isEmpty()) {
        saveDistributionCheckpoint(m_currentJobCancelled
            ? "cancelled"
            : (failed > 0 ? "finished_with_errors" : "completed"));
        if (m_currentJobCancelled) {
            OperationJobStore::instance().markCancelled(
                m_currentJobId,
                QString("Distribution cancelled: %1 succeeded, %2 failed")
                    .arg(success).arg(failed));
        } else if (failed > 0) {
            OperationJobStore::instance().markFailed(
                m_currentJobId,
                QString("Distribution completed with %1 failed").arg(failed),
                success,
                failed);
        } else {
            OperationJobStore::instance().markCompleted(
                m_currentJobId,
                success,
                failed,
                0,
                QString("Distribution complete: %1 succeeded").arg(success));
        }
    }

    emit distributionCompleted(success, failed);

    QMessageBox::information(this, "Distribution Complete",
        QString("Distribution finished.\n\nSucceeded: %1\nFailed: %2")
        .arg(success).arg(failed));

    m_currentJobId.clear();
    m_currentJobCancelled = false;
    m_currentJobPlannedCount = 0;

    cleanupWorkerThread();
}

void DistributionPanel::onWorkerProgress(int current, int total, const QString& currentItem) {
    // Update progress bar
    m_progressBar->setMaximum(total);
    AnimationHelper::animateProgress(m_progressBar, current);

    // Update status label with current item
    if (!currentItem.isEmpty()) {
        m_statusLabel->setText(QString("%1: %2").arg(m_isMoving ? "Moving" : "Copying").arg(currentItem));
    }

    updateCurrentJobProgress(currentItem.isEmpty()
        ? QString()
        : QString("%1: %2").arg(m_isMoving ? "Moving" : "Copying", currentItem));

    emit distributionProgress(current, total, currentItem);
}

void DistributionPanel::updateCurrentJobProgress(const QString& summary) {
    if (m_currentJobId.isEmpty()) {
        return;
    }

    OperationJobStore::instance().updateProgress(
        m_currentJobId,
        m_successCount,
        m_failCount,
        0,
        summary);
}

// ==================== Helper Methods ====================

void DistributionPanel::cleanupWorkerThread() {
    if (m_workerThread) {
        if (m_workerThread->isRunning()) {
            if (m_copyWorker) {
                m_copyWorker->cancel();
            }
            m_workerThread->quit();
            m_workerThread->wait(5000);
        }
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
        m_copyWorker = nullptr;
    }
}

// Helper: build a row with SwitchButton + title + description
static QWidget* createSettingRow(const QString& title, const QString& description,
                                  QCheckBox* backingCheck, bool dangerous = false) {
    auto& tm = ThemeManager::instance();
    auto* row = new QWidget();
    row->setToolTip(description);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 8, 0, 8);
    layout->setSpacing(12);

    // Left: title + description (stacked)
    auto* textCol = new QVBoxLayout();
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(2);

    auto* titleLabel = new QLabel(title);
    QColor titleColor = dangerous ? tm.supportError() : tm.textPrimary();
    titleLabel->setStyleSheet(QString("font-weight: 600; font-size: 13px; color: %1;")
        .arg(titleColor.name()));
    textCol->addWidget(titleLabel);

    if (!description.isEmpty()) {
        auto* descLabel = new QLabel(description);
        descLabel->setStyleSheet(QString("font-size: 11px; color: %1;")
            .arg(tm.textSecondary().name()));
        descLabel->setWordWrap(true);
        textCol->addWidget(descLabel);
    }

    layout->addLayout(textCol, 1);

    // Right: SwitchButton tied to backing QCheckBox
    auto* toggle = new SwitchButton();
    toggle->setToolTip(description);
    toggle->setChecked(backingCheck->isChecked());
    QObject::connect(toggle, &SwitchButton::toggled, backingCheck, &QCheckBox::setChecked);
    QObject::connect(backingCheck, &QCheckBox::toggled, toggle, &SwitchButton::setChecked);
    layout->addWidget(toggle, 0, Qt::AlignVCenter);

    return row;
}

// Helper: section header
static QWidget* createSectionHeader(const QString& title) {
    auto& tm = ThemeManager::instance();
    auto* wrapper = new QWidget();
    auto* layout = new QVBoxLayout(wrapper);
    layout->setContentsMargins(0, 12, 0, 4);
    layout->setSpacing(4);

    auto* label = new QLabel(title.toUpper());
    label->setStyleSheet(QString(
        "font-size: 10px; font-weight: 700; letter-spacing: 1px; color: %1;")
        .arg(tm.textSecondary().name()));
    layout->addWidget(label);

    auto* line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(QString("color: %1; background-color: %1;")
        .arg(tm.borderSubtle().name()));
    line->setFixedHeight(1);
    layout->addWidget(line);

    return wrapper;
}

void DistributionPanel::showDistributionSettingsDialog() {
    auto& tm = ThemeManager::instance();
    auto* dialog = new QDialog(this);
    dialog->setWindowTitle("Distribution Settings");
    dialog->setMinimumWidth(DpiScaler::scale(480));
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    auto* mainLayout = new QVBoxLayout(dialog);
    mainLayout->setContentsMargins(20, 16, 20, 16);
    mainLayout->setSpacing(0);

    // --- Copy Behavior section ---
    mainLayout->addWidget(createSectionHeader("Copy Behavior"));
    mainLayout->addWidget(createSettingRow(
        "Copy contents only",
        "Copy files inside the source folder, not the folder itself.",
        m_copyContentsOnlyCheck));
    mainLayout->addWidget(createSettingRow(
        "Skip existing files",
        "Don't overwrite files that already exist at the destination.",
        m_skipExistingCheck));
    mainLayout->addWidget(createSettingRow(
        "Create destination folder if missing",
        "Automatically create destination paths on MEGA if they don't exist.",
        m_createDestFolderCheck));

    // --- File Names section ---
    mainLayout->addWidget(createSectionHeader("File Names"));
    mainLayout->addWidget(createSettingRow(
        "Remove '_watermarked' suffix",
        "Rename files to remove '_watermarked' from filenames after copying.",
        m_removeWatermarkSuffixCheck));

    // --- Danger Zone ---
    mainLayout->addWidget(createSectionHeader("Danger Zone"));
    mainLayout->addWidget(createSettingRow(
        "Move files (delete source)",
        "Server-side move — files will be PERMANENTLY deleted from source after transfer. No bandwidth used.",
        m_moveFilesCheck, true));

    // --- Job Profiles ---
    mainLayout->addWidget(createSectionHeader("Job Profiles"));
    {
        auto* row = new QHBoxLayout();
        row->setContentsMargins(0, 8, 0, 8);
        row->setSpacing(6);

        auto* savedCombo = new QComboBox(dialog);
        savedCombo->setMinimumWidth(DpiScaler::scale(190));
        savedCombo->setToolTip(
            "Saved job profiles include source path, source type, group, destination template, "
            "month, Smart Route, Broadcast, and copy options.");
        auto refreshSavedCombo = [this, savedCombo]() {
            savedCombo->blockSignals(true);
            savedCombo->clear();
            for (int i = 0; i < m_savedTemplateCombo->count(); ++i) {
                savedCombo->addItem(m_savedTemplateCombo->itemText(i),
                                    m_savedTemplateCombo->itemData(i));
            }
            savedCombo->setCurrentIndex(m_savedTemplateCombo->currentIndex());
            savedCombo->blockSignals(false);
        };
        refreshSavedCombo();
        connect(savedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int index) {
            if (m_savedTemplateCombo && index >= 0 && index < m_savedTemplateCombo->count()) {
                m_savedTemplateCombo->setCurrentIndex(index);
            }
        });

        auto* saveBtn = ButtonFactory::createOutline("Save Job", dialog);
        saveBtn->setToolTip("Save the current source, group, destination template, and distribution options as a reusable job profile");
        connect(saveBtn, &QPushButton::clicked, this, [this, refreshSavedCombo]() {
            onSaveTemplate();
            refreshSavedCombo();
        });
        auto* loadBtn = ButtonFactory::createOutline("Load", dialog);
        loadBtn->setToolTip("Load the selected job profile into the distribution panel without scanning");
        connect(loadBtn, &QPushButton::clicked, this, [this, savedCombo]() {
            onLoadTemplate(savedCombo->currentIndex());
        });
        auto* repeatBtn = ButtonFactory::createOutline("Repeat Last", dialog);
        repeatBtn->setToolTip("Load the last confirmed distribution job, close this dialog, and scan immediately");
        connect(repeatBtn, &QPushButton::clicked, this, [this, dialog]() {
            dialog->accept();
            onRepeatLastJob();
        });
        auto* deleteBtn = ButtonFactory::createOutline("Delete", dialog);
        deleteBtn->setToolTip("Delete the selected saved job profile");
        connect(deleteBtn, &QPushButton::clicked, this, [this, savedCombo, refreshSavedCombo]() {
            if (m_savedTemplateCombo && savedCombo->currentIndex() >= 0
                && savedCombo->currentIndex() < m_savedTemplateCombo->count()) {
                m_savedTemplateCombo->setCurrentIndex(savedCombo->currentIndex());
            }
            onDeleteTemplate();
            refreshSavedCombo();
        });
        row->addWidget(savedCombo, 1);
        row->addWidget(saveBtn);
        row->addWidget(loadBtn);
        row->addWidget(repeatBtn);
        row->addWidget(deleteBtn);
        row->addStretch();
        mainLayout->addLayout(row);
    }

    // --- Import / Export ---
    mainLayout->addWidget(createSectionHeader("Import / Export"));
    {
        auto* row = new QHBoxLayout();
        row->setContentsMargins(0, 8, 0, 8);
        row->setSpacing(6);
        auto* importBtn = ButtonFactory::createOutline("Import .txt", dialog);
        importBtn->setToolTip("Import destination paths from a text file into the destination table");
        connect(importBtn, &QPushButton::clicked, this, &DistributionPanel::onImportDestinations);
        auto* exportBtn = ButtonFactory::createOutline("Export .txt", dialog);
        exportBtn->setToolTip("Export the current destination paths to a text file");
        connect(exportBtn, &QPushButton::clicked, this, &DistributionPanel::onExportDestinations);
        row->addWidget(importBtn);
        row->addWidget(exportBtn);
        row->addStretch();
        mainLayout->addLayout(row);
    }

    // --- Tools ---
    mainLayout->addWidget(createSectionHeader("Tools"));
    {
        auto* row = new QHBoxLayout();
        row->setContentsMargins(0, 8, 0, 8);
        row->setSpacing(6);
        auto* previewBtn = ButtonFactory::createOutline("Preview Paths", dialog);
        previewBtn->setToolTip("Preview expanded destination paths for the current template and member selection");
        connect(previewBtn, &QPushButton::clicked, this, &DistributionPanel::onPreviewPathsClicked);
        auto* genBtn = ButtonFactory::createOutline("Generate Destinations", dialog);
        genBtn->setToolTip("Generate destination paths for all active members or a selected member group");
        connect(genBtn, &QPushButton::clicked, this, &DistributionPanel::onGenerateDestinations);
        auto* helpBtn = ButtonFactory::createOutline("Variable Help", dialog);
        helpBtn->setToolTip("Show available destination template variables and quick-template examples");
        connect(helpBtn, &QPushButton::clicked, this, &DistributionPanel::onVariableHelpClicked);
        row->addWidget(previewBtn);
        row->addWidget(genBtn);
        row->addWidget(helpBtn);
        row->addStretch();
        mainLayout->addLayout(row);
    }

    mainLayout->addSpacing(12);

    // --- Close button ---
    auto* buttonRow = new QHBoxLayout();
    buttonRow->addStretch();
    auto* closeBtn = ButtonFactory::createPrimary("Done", dialog);
    closeBtn->setToolTip("Close distribution settings");
    connect(closeBtn, &QPushButton::clicked, dialog, &QDialog::accept);
    buttonRow->addWidget(closeBtn);
    mainLayout->addLayout(buttonRow);

    dialog->exec();
}

void DistributionPanel::executeBulkRename(const QString& folderPath) {
    if (!m_fileController || !m_megaApi) {
        return;
    }

    qDebug() << "DistributionPanel: Executing bulk rename for folder:" << folderPath;

    // Use QtConcurrent to avoid blocking UI
    (void)QtConcurrent::run([this, folderPath]() {
        mega::MegaApi* api = m_megaApi;

        // Get the folder node
        std::unique_ptr<mega::MegaNode> folderNode(api->getNodeByPath(folderPath.toUtf8().constData()));
        if (!folderNode) {
            QMetaObject::invokeMethod(this, [this, folderPath]() {
                qDebug() << "Folder not found:" << folderPath;
                m_statusLabel->setText("Error: Folder not found");
            }, Qt::QueuedConnection);
            return;
        }

        // Get children
        std::unique_ptr<mega::MegaNodeList> children(api->getChildren(folderNode.get()));
        if (!children) {
            QMetaObject::invokeMethod(this, [this]() {
                m_statusLabel->setText("Error: Could not list folder contents");
            }, Qt::QueuedConnection);
            return;
        }

        int renamed = 0;
        int total = children->size();

        for (int i = 0; i < total; ++i) {
            mega::MegaNode* child = children->get(i);
            QString name = QString::fromUtf8(child->getName());

            // Check if name contains "_watermarked"
            if (name.contains("_watermarked")) {
                QString newName = name;
                newName.replace("_watermarked", "");

                // Rename using MEGA SDK
                api->renameNode(child, newName.toUtf8().constData());
                renamed++;

                // Update UI on main thread
                QMetaObject::invokeMethod(this, [this, renamed, total]() {
                    m_statusLabel->setText(QString("Renaming... %1 of %2 files").arg(renamed).arg(total));
                }, Qt::QueuedConnection);
            }
        }

        // Final update on main thread
        QMetaObject::invokeMethod(this, [this, renamed, folderPath]() {
            qDebug() << "Bulk rename completed:" << renamed << "files in" << folderPath;
            m_statusLabel->setText(QString("Renamed %1 files in %2")
                .arg(renamed)
                .arg(folderPath.section('/', -1)));
        }, Qt::QueuedConnection);
    });
}

void DistributionPanel::onBulkRename() {
    if (!m_fileController) {
        QMessageBox::warning(this, "Error", "Not connected to MEGA");
        return;
    }

    // Count selected
    QStringList selectedFolders;
    for (const WmFolderInfo& info : m_wmFolders) {
        if (info.selected) {
            selectedFolders.append(info.fullPath);
        }
    }

    if (selectedFolders.isEmpty()) {
        QMessageBox::warning(this, "Error", "No members selected.");
        return;
    }

    int ret = QMessageBox::question(this, "Bulk Rename",
        QString("This will remove '_watermarked' suffix from all files in %1 selected folders.\n\n"
                "Continue?").arg(selectedFolders.size()),
        QMessageBox::Yes | QMessageBox::No);

    if (ret != QMessageBox::Yes) return;

    m_statusLabel->setText("Bulk rename in progress...");
    m_progressBar->setVisible(true);
    m_progressBar->setMaximum(selectedFolders.size());
    m_progressBar->setValue(0);

    int processed = 0;
    for (const QString& folder : selectedFolders) {
        executeBulkRename(folder);
        processed++;
        m_progressBar->setValue(processed);
        QApplication::processEvents();
    }

    m_progressBar->setVisible(false);
    m_statusLabel->setText(QString("Bulk rename completed for %1 folders").arg(selectedFolders.size()));

    QMessageBox::information(this, "Bulk Rename",
        QString("Bulk rename operation completed for %1 folders.\n\n"
                "Note: Files will be renamed asynchronously.").arg(selectedFolders.size()));
}

void DistributionPanel::onQuickTemplateChanged(int index) {
    QString templateValue = m_quickTemplateCombo->itemData(index).toString();
    if (!templateValue.isEmpty()) {
        m_destTemplateEdit->setText(templateValue);
    }
    // If "Custom" is selected (empty data), leave the edit field as-is
}

void DistributionPanel::onGenerateDestinations() {
    // Get current template
    QString templatePath = m_destTemplateEdit->text();
    QString error;
    if (!TemplateExpander::validateTemplate(templatePath, &error)) {
        QMessageBox::warning(this, "Invalid Template",
            QString("Template validation failed:\n%1").arg(error));
        return;
    }

    // Get active members
    QList<MemberInfo> activeMembers = m_registry->getActiveMembers();
    if (activeMembers.isEmpty()) {
        QMessageBox::information(this, "No Members",
            "No active members found. Add members in the Member Registry.");
        return;
    }

    // Build a dialog with path type selector, month, member group filter, and preview
    QDialog dialog(this);
    dialog.setWindowTitle("Generate Destinations");
    dialog.setMinimumSize(700, 500);
    QVBoxLayout* dlgLayout = new QVBoxLayout(&dialog);

    // Path type selector
    QHBoxLayout* typeRow = new QHBoxLayout();
    typeRow->addWidget(new QLabel("Path type:"));
    QComboBox* pathTypeCombo = new QComboBox();
    pathTypeCombo->setToolTip("Choose which destination template to generate for the selected members");
    pathTypeCombo->addItem("Distribution Folder", "{member}");
    pathTypeCombo->addItem("NHB+ Courses", "{archive_root}/NHB+ Courses");
    pathTypeCombo->addItem("NHB+ Updated Courses", "{archive_root}/NHB+ 2021-2024 - Regularly Updated/NHB+ Courses");
    pathTypeCombo->addItem("FF Courses", "{archive_root}/{fast_forward}/Courses");
    pathTypeCombo->addItem("Hot Seats", "{archive_root}/{fast_forward}/{hot_seats}");
    pathTypeCombo->addItem("Theory Calls", "{archive_root}/{fast_forward}/{theory_calls}");
    pathTypeCombo->addItem("NHB Calls + Month", "{archive_root}/{nhb_calls}/{month}");
    pathTypeCombo->addItem("Current Template", templatePath);
    typeRow->addWidget(pathTypeCombo, 1);
    dlgLayout->addLayout(typeRow);

    // Month selector
    QHBoxLayout* monthRow = new QHBoxLayout();
    monthRow->addWidget(new QLabel("Month:"));
    QComboBox* monthCombo = new QComboBox();
    monthCombo->setToolTip("Month value to use for templates that include {month}");
    monthCombo->addItems({"January", "February", "March", "April", "May", "June",
                          "July", "August", "September", "October", "November", "December"});
    monthCombo->setCurrentIndex(QDate::currentDate().month() - 1);
    monthRow->addWidget(monthCombo, 1);
    dlgLayout->addLayout(monthRow);

    // Members selector
    QHBoxLayout* membersRow = new QHBoxLayout();
    membersRow->addWidget(new QLabel("Members:"));
    QComboBox* membersCombo = new QComboBox();
    membersCombo->setToolTip("Choose all active members or one saved member group for path generation");
    membersCombo->addItem(QString("All Active (%1)").arg(activeMembers.size()), "all");
    QStringList groupNames = m_registry->getGroupNames();
    for (const QString& groupName : groupNames) {
        int count = m_registry->getGroupMemberIds(groupName).size();
        membersCombo->addItem(QString("Group: %1 (%2)").arg(groupName).arg(count),
                              "GROUP:" + groupName);
    }
    membersRow->addWidget(membersCombo, 1);
    dlgLayout->addLayout(membersRow);

    // Preview area
    QLabel* previewLabel = new QLabel("Preview:");
    dlgLayout->addWidget(previewLabel);
    QTextEdit* previewEdit = new QTextEdit();
    previewEdit->setReadOnly(true);
    previewEdit->setFont(QFont("monospace", 9));
    dlgLayout->addWidget(previewEdit, 1);

    // Lambda to refresh preview
    auto refreshPreview = [&]() {
        QString selectedTemplate = pathTypeCombo->currentData().toString();
        QString month = monthCombo->currentText();
        QString memberFilter = membersCombo->currentData().toString();

        // Get filtered member list
        QList<MemberInfo> members;
        if (memberFilter == "all") {
            members = activeMembers;
        } else if (memberFilter.startsWith("GROUP:")) {
            QString groupName = memberFilter.mid(6);
            QStringList groupIds = m_registry->getGroupMemberIds(groupName);
            for (const MemberInfo& m : activeMembers) {
                if (groupIds.contains(m.id)) members.append(m);
            }
        }

        // Expand template for each member
        QStringList paths;
        for (const MemberInfo& member : members) {
            auto vars = TemplateExpander::Variables::fromMember(member);
            vars.month = month;
            vars.monthNum = QString::number(monthCombo->currentIndex() + 1).rightJustified(2, '0');
            QString expanded = TemplateExpander::expand(selectedTemplate, vars);
            paths.append(expanded);
        }

        previewLabel->setText(QString("Preview (%1 paths):").arg(paths.size()));
        previewEdit->setText(paths.join("\n"));
    };

    connect(pathTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, refreshPreview);
    connect(monthCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, refreshPreview);
    connect(membersCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, refreshPreview);
    refreshPreview();

    // Buttons
    QHBoxLayout* btnRow = new QHBoxLayout();
    QPushButton* copyBtn = new QPushButton("Copy to Clipboard");
    QPushButton* exportBtn = new QPushButton("Export to .txt");
    QPushButton* useBtn = new QPushButton("Use as Template");
    useBtn->setObjectName("PanelPrimaryButton");
    QPushButton* closeBtn = new QPushButton("Close");
    copyBtn->setToolTip("Copy the generated destination paths to the clipboard");
    exportBtn->setToolTip("Save the generated destination paths to a text file");
    useBtn->setToolTip("Apply the selected path type as the current destination template");
    closeBtn->setToolTip("Close without changing the current destination template");
    btnRow->addWidget(copyBtn);
    btnRow->addWidget(exportBtn);
    btnRow->addStretch();
    btnRow->addWidget(useBtn);
    btnRow->addWidget(closeBtn);
    dlgLayout->addLayout(btnRow);

    connect(copyBtn, &QPushButton::clicked, &dialog, [&]() {
        QApplication::clipboard()->setText(previewEdit->toPlainText());
        QMessageBox::information(&dialog, "Copied", "Paths copied to clipboard.");
    });

    connect(exportBtn, &QPushButton::clicked, &dialog, [&]() {
        QString filePath = QFileDialog::getSaveFileName(&dialog, "Export Paths",
            QDir::homePath() + "/destinations.txt", "Text Files (*.txt)");
        if (!filePath.isEmpty()) {
            QFile file(filePath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                file.write(previewEdit->toPlainText().toUtf8());
                file.close();
                QMessageBox::information(&dialog, "Exported",
                    QString("Saved %1 paths to %2").arg(previewEdit->toPlainText().split('\n').size()).arg(filePath));
            }
        }
    });

    connect(useBtn, &QPushButton::clicked, &dialog, [&]() {
        m_destTemplateEdit->setText(pathTypeCombo->currentData().toString());
        dialog.accept();
    });

    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    dialog.exec();
}

// ==================== Saved Template Management ====================

QString DistributionPanel::savedTemplatesPath() const {
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    return configDir + "/dist_templates.json";
}

QString DistributionPanel::lastJobProfilePath() const {
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    return configDir + "/dist_last_job.json";
}

QVariantMap DistributionPanel::currentJobProfile(const QString& name) const {
    QVariantMap data;
    if (!name.isEmpty()) data["name"] = name;

    data["text"] = m_destTemplateEdit ? m_destTemplateEdit->text().trimmed() : QString();
    data["quickIdx"] = m_quickTemplateCombo ? m_quickTemplateCombo->currentIndex() : -1;
    data["sourcePath"] = m_wmPathEdit ? m_wmPathEdit->text().trimmed() : QString();
    data["sourceType"] = m_sourceTypeCombo
        ? m_sourceTypeCombo->currentData().toString()
        : QString("cloud");
    data["month"] = m_monthCombo ? m_monthCombo->currentText() : QString();
    data["groupName"] = (m_groupCombo && m_groupCombo->currentIndex() >= 0)
        ? m_groupCombo->itemData(m_groupCombo->currentIndex()).toString()
        : QString();
    data["broadcast"] = m_broadcastCheck && m_broadcastCheck->isChecked();
    data["smartRoute"] = m_smartRouteCheck && m_smartRouteCheck->isChecked();
    data["copyContentsOnly"] = m_copyContentsOnlyCheck && m_copyContentsOnlyCheck->isChecked();
    data["skipExisting"] = m_skipExistingCheck && m_skipExistingCheck->isChecked();
    data["createDestination"] = m_createDestFolderCheck && m_createDestFolderCheck->isChecked();
    data["removeWatermarkSuffix"] = m_removeWatermarkSuffixCheck
        && m_removeWatermarkSuffixCheck->isChecked();
    data["moveFiles"] = m_moveFilesCheck && m_moveFilesCheck->isChecked();
    data["savedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    data["profileVersion"] = 2;
    return data;
}

void DistributionPanel::applyJobProfile(const QVariantMap& data) {
    const QString templateText = data.value("text",
        data.value("templateText").toString()).toString();
    if (!templateText.isEmpty() && m_destTemplateEdit) {
        m_destTemplateEdit->setText(templateText);
    }

    const int quickIdx = data.value("quickIdx",
        data.value("quickTemplateIndex", -1)).toInt();
    if (m_quickTemplateCombo && quickIdx >= 0 && quickIdx < m_quickTemplateCombo->count()) {
        m_quickTemplateCombo->blockSignals(true);
        m_quickTemplateCombo->setCurrentIndex(quickIdx);
        m_quickTemplateCombo->blockSignals(false);
    }

    if (m_sourceTypeCombo && data.contains("sourceType")) {
        const int sourceIdx = m_sourceTypeCombo->findData(data.value("sourceType").toString());
        if (sourceIdx >= 0) m_sourceTypeCombo->setCurrentIndex(sourceIdx);
    }
    if (m_wmPathEdit && data.contains("sourcePath")) {
        m_wmPathEdit->setText(data.value("sourcePath").toString());
    }
    if (m_monthCombo && data.contains("month")) {
        const int monthIdx = m_monthCombo->findText(data.value("month").toString());
        if (monthIdx >= 0) m_monthCombo->setCurrentIndex(monthIdx);
    }
    if (m_groupCombo && data.contains("groupName")) {
        const int groupIdx = m_groupCombo->findData(data.value("groupName").toString());
        m_groupCombo->blockSignals(true);
        m_groupCombo->setCurrentIndex(groupIdx >= 0 ? groupIdx : 0);
        m_groupCombo->blockSignals(false);
    }

    if (m_copyContentsOnlyCheck && data.contains("copyContentsOnly")) {
        m_copyContentsOnlyCheck->setChecked(data.value("copyContentsOnly").toBool());
    }
    if (m_skipExistingCheck && data.contains("skipExisting")) {
        m_skipExistingCheck->setChecked(data.value("skipExisting").toBool());
    }
    if (m_createDestFolderCheck && data.contains("createDestination")) {
        m_createDestFolderCheck->setChecked(data.value("createDestination").toBool());
    }
    if (m_removeWatermarkSuffixCheck && data.contains("removeWatermarkSuffix")) {
        m_removeWatermarkSuffixCheck->setChecked(data.value("removeWatermarkSuffix").toBool());
    }
    if (m_moveFilesCheck && data.contains("moveFiles")) {
        m_moveFilesCheck->setChecked(data.value("moveFiles").toBool());
    }

    const bool broadcast = data.value("broadcast", false).toBool();
    const bool smartRoute = data.value("smartRoute", false).toBool();
    if (m_broadcastCheck) m_broadcastCheck->setChecked(broadcast);
    if (m_smartRouteCheck) m_smartRouteCheck->setChecked(smartRoute && !broadcast);
}

void DistributionPanel::saveLastJobProfile() {
    QFile file(lastJobProfilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "DistributionPanel: Could not save last job profile:" << file.errorString();
        return;
    }
    QJsonObject root = QJsonObject::fromVariantMap(currentJobProfile("Last Job"));
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
}

bool DistributionPanel::loadLastJobProfile(bool scanAfterLoad) {
    QFile file(lastJobProfilePath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) return false;

    applyJobProfile(doc.object().toVariantMap());
    m_statusLabel->setText(scanAfterLoad
        ? "Loaded last job. Scanning..."
        : "Loaded last job.");

    if (scanAfterLoad) {
        onScanWmFolder();
    }
    return true;
}

void DistributionPanel::loadSavedTemplates() {
    m_savedTemplateCombo->blockSignals(true);
    m_savedTemplateCombo->clear();
    m_savedTemplateCombo->addItem("-- Saved Templates --", "");

    QFile file(savedTemplatesPath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        m_savedTemplateCombo->blockSignals(false);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    QJsonArray templates = doc.object().value("templates").toArray();
    for (const QJsonValue& val : templates) {
        QJsonObject tmpl = val.toObject();
        QString name = tmpl.value("name").toString();
        QString text = tmpl.value("templateText").toString(
            tmpl.value("text").toString());
        int quickIdx = tmpl.value("quickTemplateIndex").toInt(
            tmpl.value("quickIdx").toInt(-1));

        QVariantMap itemData = tmpl.toVariantMap();
        itemData["text"] = text;
        itemData["quickIdx"] = quickIdx;
        m_savedTemplateCombo->addItem(name, itemData);
    }

    m_savedTemplateCombo->blockSignals(false);
    qDebug() << "DistributionPanel: Loaded" << (m_savedTemplateCombo->count() - 1) << "saved templates";
}

void DistributionPanel::saveSavedTemplates() {
    QJsonArray templates;
    for (int i = 1; i < m_savedTemplateCombo->count(); ++i) {
        QVariantMap itemData = m_savedTemplateCombo->itemData(i).toMap();
        QString text = itemData.value("text").toString();
        int quickIdx = itemData.value("quickIdx", -1).toInt();

        QJsonObject tmpl = QJsonObject::fromVariantMap(itemData);
        tmpl["name"] = m_savedTemplateCombo->itemText(i);
        tmpl["templateText"] = text;
        tmpl["quickTemplateIndex"] = quickIdx;
        if (!tmpl.contains("created")) {
            tmpl["created"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        }
        tmpl["updated"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        templates.append(tmpl);
    }

    QJsonObject root;
    root["templates"] = templates;

    QFile file(savedTemplatesPath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
    }
}

void DistributionPanel::onSaveTemplate() {
    QString currentText = m_destTemplateEdit->text().trimmed();
    if (currentText.isEmpty()) {
        QMessageBox::warning(this, "Save Template",
            "No template text to save. Enter a destination template first.");
        return;
    }

    bool ok;
    QString name = QInputDialog::getText(this, "Save Job Profile",
        "Profile name:", QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    name = name.trimmed();

    // Check for duplicates
    for (int i = 1; i < m_savedTemplateCombo->count(); ++i) {
        if (m_savedTemplateCombo->itemText(i) == name) {
            auto reply = QMessageBox::question(this, "Overwrite Template",
                QString("A template named '%1' already exists. Overwrite it?").arg(name),
                QMessageBox::Yes | QMessageBox::No);
            if (reply == QMessageBox::No) return;
            // Remove old entry, will re-add below
            m_savedTemplateCombo->removeItem(i);
            break;
        }
    }

    QVariantMap data = currentJobProfile(name);
    data["text"] = currentText;
    data["quickIdx"] = m_quickTemplateCombo ? m_quickTemplateCombo->currentIndex() : -1;
    m_savedTemplateCombo->addItem(name, data);
    m_savedTemplateCombo->setCurrentIndex(m_savedTemplateCombo->count() - 1);
    saveSavedTemplates();

    m_statusLabel->setText(QString("Job profile '%1' saved.").arg(name));
}

void DistributionPanel::onDeleteTemplate() {
    int idx = m_savedTemplateCombo->currentIndex();
    if (idx <= 0) {
        QMessageBox::information(this, "Delete Job Profile",
            "Select a saved job profile to delete.");
        return;
    }

    QString name = m_savedTemplateCombo->itemText(idx);
    auto reply = QMessageBox::question(this, "Delete Job Profile",
        QString("Delete job profile '%1'?").arg(name),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    m_savedTemplateCombo->removeItem(idx);
    m_savedTemplateCombo->setCurrentIndex(0);
    saveSavedTemplates();

    m_statusLabel->setText(QString("Job profile '%1' deleted.").arg(name));
}

void DistributionPanel::onLoadTemplate(int index) {
    if (index <= 0) return;

    QVariantMap data = m_savedTemplateCombo->itemData(index).toMap();
    applyJobProfile(data);

    m_statusLabel->setText(QString("Loaded job profile '%1'.").arg(m_savedTemplateCombo->itemText(index)));
}

void DistributionPanel::onRepeatLastJob() {
    if (!loadLastJobProfile(true)) {
        QMessageBox::information(this, "Repeat Last Job",
            "No previous distribution job was found.");
    }
}

// ==================== Import/Export Destinations ====================

void DistributionPanel::onImportDestinations() {
    QString filePath = QFileDialog::getOpenFileName(this, "Import Destinations",
        QDir::homePath(), "Text Files (*.txt);;All Files (*)");
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Import Error",
            QString("Could not open file: %1").arg(file.errorString()));
        return;
    }

    QStringList lines = QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
    file.close();

    if (lines.isEmpty()) {
        QMessageBox::information(this, "Import Destinations", "File is empty.");
        return;
    }

    // Parse lines: either "memberId: /path" or just "/path"
    int imported = 0;
    for (const QString& rawLine : lines) {
        QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;  // Skip comments

        QString memberId;
        QString destPath;

        // Try "memberId: /path" format
        int colonIdx = line.indexOf(':');
        if (colonIdx > 0 && colonIdx < line.length() - 1) {
            QString beforeColon = line.left(colonIdx).trimmed();
            QString afterColon = line.mid(colonIdx + 1).trimmed();
            if (afterColon.startsWith('/')) {
                memberId = beforeColon;
                destPath = afterColon;
            }
        }

        // Fallback: just a path — try to extract member from path segments
        if (destPath.isEmpty()) {
            if (line.startsWith('/')) {
                destPath = line;
                // Try to match a member from the path
                QList<MemberInfo> members = m_registry->getActiveMembers();
                for (const MemberInfo& m : members) {
                    if (line.contains(m.id, Qt::CaseInsensitive) ||
                        (!m.displayName.isEmpty() && line.contains(m.displayName, Qt::CaseInsensitive))) {
                        memberId = m.id;
                        break;
                    }
                }
            } else {
                continue;  // Not a valid line
            }
        }

        // Add row to table
        int row = m_memberTable->rowCount();
        m_memberTable->insertRow(row);

        // Checkbox
        QWidget* checkWidget = new QWidget();
        QHBoxLayout* checkLayout = new QHBoxLayout(checkWidget);
        checkLayout->setContentsMargins(0, 0, 0, 0);
        checkLayout->setAlignment(Qt::AlignCenter);
        QCheckBox* check = new QCheckBox();
        check->setChecked(true);
        connect(check, &QCheckBox::toggled, this, [this, row](bool checked) {
            if (row < m_wmFolders.size()) m_wmFolders[row].selected = checked;
        });
        checkLayout->addWidget(check);
        m_memberTable->setCellWidget(row, COL_CHECK, checkWidget);

        // Source folder (empty for imported)
        auto* sourceItem = new QTableWidgetItem("(imported)");
        sourceItem->setFlags(sourceItem->flags() & ~Qt::ItemIsEditable);
        m_memberTable->setItem(row, COL_SOURCE_FOLDER, sourceItem);

        // Member
        auto* memberItem = new QTableWidgetItem(memberId.isEmpty() ? "(unmatched)" : memberId);
        memberItem->setFlags(memberItem->flags() & ~Qt::ItemIsEditable);
        m_memberTable->setItem(row, COL_MATCHED_MEMBER, memberItem);

        // Match type
        auto* matchItem = new QTableWidgetItem(memberId.isEmpty() ? "none" : "import");
        matchItem->setFlags(matchItem->flags() & ~Qt::ItemIsEditable);
        m_memberTable->setItem(row, COL_MATCH_TYPE, matchItem);

        // Destination (editable)
        auto* destItem = new QTableWidgetItem(destPath);
        m_memberTable->setItem(row, COL_DESTINATION, destItem);

        // Status
        auto* statusItem = new QTableWidgetItem("Ready");
        statusItem->setFlags(statusItem->flags() & ~Qt::ItemIsEditable);
        m_memberTable->setItem(row, COL_STATUS, statusItem);

        // Track in wmFolders
        WmFolderInfo info;
        info.folderName = "(imported)";
        info.memberId = memberId;
        info.fullPath = m_wmPathEdit->text();
        info.matchType = memberId.isEmpty() ? "none" : "import";
        info.matched = !memberId.isEmpty();
        info.selected = true;
        m_wmFolders.append(info);

        imported++;
    }

    m_statusLabel->setText(QString("Imported %1 %2 from %3")
                          .arg(imported).arg(imported == 1 ? "destination" : "destinations").arg(QFileInfo(filePath).fileName()));
}

void DistributionPanel::onExportDestinations() {
    if (m_memberTable->rowCount() == 0) {
        QMessageBox::information(this, "Export Destinations",
            "No destinations to export. Scan folders or add rows first.");
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(this, "Export Destinations",
        QDir::homePath() + "/destinations.txt", "Text Files (*.txt)");
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Export Error",
            QString("Could not write file: %1").arg(file.errorString()));
        return;
    }

    int exported = 0;
    QTextStream out(&file);
    for (int row = 0; row < m_memberTable->rowCount(); ++row) {
        QTableWidgetItem* memberItem = m_memberTable->item(row, COL_MATCHED_MEMBER);
        QTableWidgetItem* destItem = m_memberTable->item(row, COL_DESTINATION);
        if (!destItem) continue;

        QString dest = destItem->text().trimmed();
        if (dest.isEmpty()) continue;

        QString member = memberItem ? memberItem->text().trimmed() : "";
        if (!member.isEmpty() && member != "(unmatched)") {
            out << member << ": " << dest << "\n";
        } else {
            out << dest << "\n";
        }
        exported++;
    }

    file.close();
    m_statusLabel->setText(QString("Exported %1 %2 to %3")
                          .arg(exported).arg(exported == 1 ? "destination" : "destinations").arg(QFileInfo(filePath).fileName()));
}

// ==================== Manual Destination Management ====================

void DistributionPanel::onAddRow() {
    int row = m_memberTable->rowCount();
    m_memberTable->insertRow(row);

    // Checkbox (checked by default)
    QWidget* checkWidget = new QWidget();
    QHBoxLayout* checkLayout = new QHBoxLayout(checkWidget);
    checkLayout->setContentsMargins(0, 0, 0, 0);
    checkLayout->setAlignment(Qt::AlignCenter);
    QCheckBox* check = new QCheckBox();
    check->setChecked(true);
    connect(check, &QCheckBox::toggled, this, [this, row](bool checked) {
        if (row < m_wmFolders.size()) m_wmFolders[row].selected = checked;
    });
    checkLayout->addWidget(check);
    m_memberTable->setCellWidget(row, COL_CHECK, checkWidget);

    // Source folder (empty for manual)
    auto* sourceItem = new QTableWidgetItem("(manual)");
    sourceItem->setFlags(sourceItem->flags() & ~Qt::ItemIsEditable);
    m_memberTable->setItem(row, COL_SOURCE_FOLDER, sourceItem);

    // Member (editable — user types member ID)
    auto* memberItem = new QTableWidgetItem("");
    memberItem->setToolTip("Enter a member ID");
    m_memberTable->setItem(row, COL_MATCHED_MEMBER, memberItem);

    // Match type
    auto* matchItem = new QTableWidgetItem("manual");
    matchItem->setFlags(matchItem->flags() & ~Qt::ItemIsEditable);
    m_memberTable->setItem(row, COL_MATCH_TYPE, matchItem);

    // Destination (editable)
    auto* destItem = new QTableWidgetItem("");
    destItem->setToolTip("Enter destination cloud path (e.g., /Archive/Members/...)");
    m_memberTable->setItem(row, COL_DESTINATION, destItem);

    // Status
    auto* statusItem = new QTableWidgetItem("Ready");
    statusItem->setFlags(statusItem->flags() & ~Qt::ItemIsEditable);
    m_memberTable->setItem(row, COL_STATUS, statusItem);

    // Track in wmFolders
    WmFolderInfo info;
    info.folderName = "(manual)";
    info.fullPath = m_wmPathEdit->text();
    info.matchType = "manual";
    info.selected = true;
    m_wmFolders.append(info);

    // Focus the member cell for editing
    m_memberTable->scrollToItem(memberItem);
    m_memberTable->editItem(memberItem);
}

void DistributionPanel::onPasteDestinations() {
    QDialog dialog(this);
    dialog.setWindowTitle("Paste Destinations");
    dialog.setMinimumSize(600, 400);
    auto* layout = new QVBoxLayout(&dialog);

    auto* instrLabel = new QLabel(
        "Enter one destination per line. Supported formats:\n"
        "  memberId: /destination/path\n"
        "  /destination/path  (auto-matches member from path)\n\n"
        "Lines starting with '#' are ignored.", &dialog);
    instrLabel->setWordWrap(true);
    layout->addWidget(instrLabel);

    auto* textEdit = new QTextEdit(&dialog);
    textEdit->setToolTip("Paste destination paths here, one per line. Use 'memberId: /path' to force a specific member.");
    textEdit->setPlaceholderText(
        "icekkk: /Archive/Members/Icekkk/HotSeats\n"
        "sp3nc3: /Archive/Members/sp3nc3/HotSeats\n"
        "# Or just paths:\n"
        "/Archive/Members/Icekkk/HotSeats");
    textEdit->setFont(QFont("monospace", 10));

    // Pre-fill from clipboard if it looks like paths
    QString clipboard = QApplication::clipboard()->text();
    if (clipboard.contains('/') && clipboard.contains('\n')) {
        textEdit->setPlainText(clipboard);
    }

    layout->addWidget(textEdit, 1);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto* cancelBtn = new QPushButton("Cancel", &dialog);
    auto* pasteBtn = new QPushButton("Add to Table", &dialog);
    pasteBtn->setObjectName("PanelPrimaryButton");
    cancelBtn->setToolTip("Close without adding pasted destinations");
    pasteBtn->setToolTip("Parse the pasted destinations and add them as manual distribution rows");
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(pasteBtn);
    layout->addLayout(btnLayout);

    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(pasteBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    if (dialog.exec() != QDialog::Accepted) return;

    QString text = textEdit->toPlainText();
    QStringList lines = text.split('\n', Qt::SkipEmptyParts);

    int added = 0;
    for (const QString& rawLine : lines) {
        QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;

        QString memberId;
        QString destPath;

        // Try "memberId: /path" format
        int colonIdx = line.indexOf(':');
        if (colonIdx > 0 && colonIdx < line.length() - 1) {
            QString beforeColon = line.left(colonIdx).trimmed();
            QString afterColon = line.mid(colonIdx + 1).trimmed();
            if (afterColon.startsWith('/')) {
                memberId = beforeColon;
                destPath = afterColon;
            }
        }

        // Fallback: just a path
        if (destPath.isEmpty() && line.startsWith('/')) {
            destPath = line;
            QList<MemberInfo> members = m_registry->getActiveMembers();
            for (const MemberInfo& m : members) {
                if (line.contains(m.id, Qt::CaseInsensitive) ||
                    (!m.displayName.isEmpty() && line.contains(m.displayName, Qt::CaseInsensitive))) {
                    memberId = m.id;
                    break;
                }
            }
        }

        if (destPath.isEmpty()) continue;

        // Check for duplicate destination in existing rows
        bool duplicate = false;
        for (int r = 0; r < m_memberTable->rowCount(); ++r) {
            QTableWidgetItem* existingDest = m_memberTable->item(r, COL_DESTINATION);
            if (existingDest && existingDest->text().trimmed() == destPath) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;

        // Add row
        int row = m_memberTable->rowCount();
        m_memberTable->insertRow(row);

        QWidget* checkWidget = new QWidget();
        QHBoxLayout* checkLayout = new QHBoxLayout(checkWidget);
        checkLayout->setContentsMargins(0, 0, 0, 0);
        checkLayout->setAlignment(Qt::AlignCenter);
        QCheckBox* check = new QCheckBox();
        check->setChecked(true);
        connect(check, &QCheckBox::toggled, this, [this, row](bool checked) {
            if (row < m_wmFolders.size()) m_wmFolders[row].selected = checked;
        });
        checkLayout->addWidget(check);
        m_memberTable->setCellWidget(row, COL_CHECK, checkWidget);

        auto* sourceItem = new QTableWidgetItem("(pasted)");
        sourceItem->setFlags(sourceItem->flags() & ~Qt::ItemIsEditable);
        m_memberTable->setItem(row, COL_SOURCE_FOLDER, sourceItem);

        auto* memberItem = new QTableWidgetItem(memberId.isEmpty() ? "(unmatched)" : memberId);
        memberItem->setFlags(memberItem->flags() & ~Qt::ItemIsEditable);
        m_memberTable->setItem(row, COL_MATCHED_MEMBER, memberItem);

        auto* matchItem = new QTableWidgetItem(memberId.isEmpty() ? "none" : "paste");
        matchItem->setFlags(matchItem->flags() & ~Qt::ItemIsEditable);
        m_memberTable->setItem(row, COL_MATCH_TYPE, matchItem);

        auto* destItem = new QTableWidgetItem(destPath);
        m_memberTable->setItem(row, COL_DESTINATION, destItem);

        auto* statusItem = new QTableWidgetItem("Ready");
        statusItem->setFlags(statusItem->flags() & ~Qt::ItemIsEditable);
        m_memberTable->setItem(row, COL_STATUS, statusItem);

        WmFolderInfo info;
        info.folderName = "(pasted)";
        info.memberId = memberId;
        info.fullPath = m_wmPathEdit->text();
        info.matchType = memberId.isEmpty() ? "none" : "paste";
        info.matched = !memberId.isEmpty();
        info.selected = true;
        m_wmFolders.append(info);

        added++;
    }

    m_statusLabel->setText(QString("Added %1 %2 from paste.").arg(added).arg(added == 1 ? "destination" : "destinations"));
}

void DistributionPanel::onClearAllRows() {
    if (m_memberTable->rowCount() == 0) return;

    auto reply = QMessageBox::question(this, "Clear All Rows",
        QString("Remove all %1 rows from the table?").arg(m_memberTable->rowCount()),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    m_memberTable->setRowCount(0);
    m_wmFolders.clear();
    m_statusLabel->setText("Table cleared.");
    updateEmptyState();
}

void DistributionPanel::onVariableHelpClicked() {
    QString helpText = R"(
<h3>Template Variables</h3>
<p>Use these placeholders in your destination path template:</p>
<h4>Member Variables</h4>
<ul>
<li><b>{member}</b> - Member's distribution folder path</li>
<li><b>{member_id}</b> - Member's unique ID</li>
<li><b>{member_name}</b> - Member's display name</li>
<li><b>{member_email}</b> - Member's email address</li>
</ul>
<h4>Member Path Variables</h4>
<ul>
<li><b>{archive_root}</b> - Archive root path (e.g., /Alen Sultanic - NHB+ - EGBs/3. Icekkk)</li>
<li><b>{nhb_calls}</b> - NHB Calls subpath</li>
<li><b>{fast_forward}</b> - Fast Forward subpath</li>
<li><b>{theory_calls}</b> - Theory Calls subpath</li>
<li><b>{hot_seats}</b> - Hot Seats subpath</li>
</ul>
<h4>Date Variables</h4>
<ul>
<li><b>{month}</b> - Current month name (e.g., December)</li>
<li><b>{month_num}</b> - Month number (01-12)</li>
<li><b>{year}</b> - Current year</li>
<li><b>{date}</b> - Current date (YYYY-MM-DD)</li>
<li><b>{timestamp}</b> - Current timestamp (YYYYMMDD_HHMMSS)</li>
</ul>
<h4>Quick Templates</h4>
<ul>
<li><b>NHB+ Courses:</b> {archive_root}/NHB+ Courses</li>
<li><b>NHB+ Updated Courses:</b> {archive_root}/NHB+ 2021-2024 - Regularly Updated/NHB+ Courses</li>
<li><b>FF Courses:</b> {archive_root}/{fast_forward}/Courses</li>
<li><b>Hot Seats:</b> {archive_root}/{fast_forward}/{hot_seats}</li>
<li><b>Theory Calls:</b> {archive_root}/{fast_forward}/{theory_calls}</li>
<li><b>NHB Calls:</b> {archive_root}/{nhb_calls}/{month}</li>
</ul>
)";

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Template Variables Help");
    msgBox.setTextFormat(Qt::RichText);
    msgBox.setText(helpText);
    msgBox.setIcon(QMessageBox::Information);
    msgBox.exec();
}

void DistributionPanel::onPreviewPathsClicked() {
    // Validate template first
    QString templatePath = m_destTemplateEdit->text();
    QString error;
    if (!TemplateExpander::validateTemplate(templatePath, &error)) {
        QMessageBox::warning(this, "Invalid Template",
            QString("Template validation failed:\n%1").arg(error));
        return;
    }

    // Get selected members
    QStringList selectedIds;
    for (int row = 0; row < m_memberTable->rowCount(); ++row) {
        QWidget* widget = m_memberTable->cellWidget(row, COL_CHECK);
        if (widget) {
            QCheckBox* check = widget->findChild<QCheckBox*>();
            if (check && check->isChecked() && row < m_wmFolders.size()) {
                selectedIds.append(m_wmFolders[row].memberId);
            }
        }
    }

    if (selectedIds.isEmpty()) {
        QMessageBox::information(this, "Preview Paths",
            "No members selected. Select members to preview their destination paths.");
        return;
    }

    // Build preview text
    QString previewText = QString("<h3>Preview for %1 members</h3><table border='1' cellpadding='5'>")
                          .arg(selectedIds.size());
    previewText += "<tr><th>Member ID</th><th>Destination Path</th></tr>";

    for (const QString& memberId : selectedIds) {
        QString destPath = getDestinationPath(memberId);
        previewText += QString("<tr><td>%1</td><td>%2</td></tr>")
                       .arg(memberId.toHtmlEscaped())
                       .arg(destPath.toHtmlEscaped());
    }
    previewText += "</table>";

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Destination Path Preview");
    msgBox.setTextFormat(Qt::RichText);
    msgBox.setText(previewText);
    msgBox.setIcon(QMessageBox::Information);
    msgBox.exec();
}

} // namespace MegaCustom
