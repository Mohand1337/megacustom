#include "DistributionPanel.h"
#include "EmptyStateWidget.h"
#include "utils/MemberRegistry.h"
#include "utils/TemplateExpander.h"
#include "utils/CopyHelper.h"
#include "utils/CloudPathValidator.h"
#include "utils/ContentRouter.h"
#include "utils/MegaUploadUtils.h"
#include "utils/DpiScaler.h"
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
                if (task.isLocalSource) {
                    // megaApiUpload ensures the folder exists, but do it upfront so
                    // empty uploads still create the expected destination.
                    std::unique_ptr<mega::MegaNode> created(
                        ensureFolderExists(m_megaApi, task.destPath.toStdString()));
                } else if (m_cloudCopier) {
                    m_cloudCopier->createDestinations({task.destPath.toStdString()});
                }
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
    void allCompleted(int success, int failed);
    void progress(int current, int total, const QString& currentItem);
    void error(const QString& message);

private:
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

            // Use native separators on Windows — the MEGA SDK's Windows
            // FileSystemAccess is picky about forward vs backward slashes
            // on some code paths. On Linux toNativeSeparators is a no-op.
            const QString nativeLocal = QDir::toNativeSeparators(item.localPath);

            std::string err;
            bool ok = megaApiUpload(m_megaApi,
                                    nativeLocal.toStdString(),
                                    destFolder.toStdString(),
                                    err);
            if (ok) {
                success++;
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
            Q_UNUSED(jobId);
            if (!m_controllerActive) return;
            m_isRunning = true;
            m_statusLabel->setText("Upload starting...");
        });

        // Progress updates — update status label with current file info
        connect(m_distController, &DistributionController::distributionProgress,
                this, [this](const QtDistributionProgress& progress) {
            if (!m_controllerActive) return;
            m_statusLabel->setText(QString("Uploading: %1 - %2")
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

            QMessageBox::information(this, "Upload Complete",
                QString("Upload finished.\n\n"
                        "Members: %1 succeeded, %2 failed, %3 skipped\n"
                        "Files: %4 uploaded, %5 failed")
                .arg(result.membersCompleted).arg(result.membersFailed).arg(result.membersSkipped)
                .arg(result.filesUploaded).arg(result.filesFailed));
        });

        // Error handling
        connect(m_distController, &DistributionController::distributionError,
                this, [this](const QString& error) {
            m_statusLabel->setText(QString("Error: %1").arg(error));
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
    m_smartRouteCheck->setToolTip("Auto-detect content types and route to correct destinations");
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
    m_destTemplateEdit->setToolTip("Destination path template. Variables: {member}, {archive_root}, {nhb_calls}, {month}, etc.");
    row2->addWidget(m_destTemplateEdit, 1);

    // Quick template combo (compact)
    m_quickTemplateCombo = new QComboBox();
    m_quickTemplateCombo->addItem("Distribution Folder", "{member}");
    m_quickTemplateCombo->addItem("Hot Seats", "{archive_root}/{fast_forward}/{hot_seats}");
    m_quickTemplateCombo->addItem("Theory Calls", "{archive_root}/{fast_forward}/{theory_calls}");
    m_quickTemplateCombo->addItem("NHB Calls + Month", "{archive_root}/{nhb_calls}/{month}");
    m_quickTemplateCombo->addItem("Custom", "");
    m_quickTemplateCombo->setFixedWidth(s*140);
    m_quickTemplateCombo->setToolTip("Quick preset for destination template");
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
            ? "Destination path template. Use {member}, {archive_root}, {hot_seats}, etc."
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
    // Popup opens upward since the combo is near the screen bottom
    m_groupCombo->view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_groupCombo->addItem("-- Group --", "");
    connect(m_groupCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        QString groupName = m_groupCombo->itemData(index).toString();
        if (groupName.isEmpty()) return;
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
    connect(m_bulkRenameBtn, &QPushButton::clicked, this, &DistributionPanel::onBulkRename);
    actionBar->addWidget(m_bulkRenameBtn);

    actionBar->addStretch();

    m_previewBtn = ButtonFactory::createOutline("Preview", this);
    m_previewBtn->setIcon(QIcon(":/icons/eye.svg"));
    connect(m_previewBtn, &QPushButton::clicked, this, &DistributionPanel::onPreviewDistribution);
    actionBar->addWidget(m_previewBtn);

    m_startBtn = ButtonFactory::createPrimary("Start Distribution", this);
    m_startBtn->setIcon(QIcon(":/icons/play.svg"));
    connect(m_startBtn, &QPushButton::clicked, this, &DistributionPanel::onStartDistribution);
    actionBar->addWidget(m_startBtn);

    m_pauseBtn = new QPushButton("Pause");
    m_pauseBtn->setProperty("type", "warning");
    m_pauseBtn->setIcon(QIcon(":/icons/pause.svg"));
    m_pauseBtn->setEnabled(false);
    m_pauseBtn->setVisible(false);
    connect(m_pauseBtn, &QPushButton::clicked, this, &DistributionPanel::onPauseDistribution);
    actionBar->addWidget(m_pauseBtn);

    m_stopBtn = new QPushButton("Stop");
    m_stopBtn->setObjectName("PanelDangerButton");
    m_stopBtn->setIcon(QIcon(":/icons/x.svg"));
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

void DistributionPanel::prepareForUpload(const QMap<QString, QStringList>& memberFileMap) {
    m_controllerActive = true;
    m_modeIndicator->setText("Mode: Auto-Upload (watermark -> member folders)");
    m_modeIndicator->setProperty("mode", "active");
    m_modeIndicator->style()->polish(m_modeIndicator);
    m_memberRowMap.clear();
    m_routeMap.clear();
    m_wmFolders.clear();
    m_memberTable->setRowCount(0);
    m_memberTable->setRowCount(memberFileMap.size());

    int totalFiles = 0;
    for (const auto& files : memberFileMap) totalFiles += files.size();

    // Show upload banner
    m_uploadBannerLabel->setText(QString("Upload Mode -- %1 files for %2 members. "
        "Files received from Watermark panel.").arg(totalFiles).arg(memberFileMap.size()));
    m_uploadBanner->setVisible(true);

    int row = 0;
    for (auto it = memberFileMap.constBegin(); it != memberFileMap.constEnd(); ++it) {
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
    m_pendingMemberFileMap = memberFileMap;

    // Set UI to ready state — user must click Start
    m_startBtn->setEnabled(true);
    m_pauseBtn->setEnabled(false);
    m_stopBtn->setEnabled(false);
    m_progressBar->setValue(0);
    m_progressBar->setMaximum(memberFileMap.size());
    m_successCount = 0;
    m_failCount = 0;

    m_statusLabel->setText(QString("Ready to upload %1 files to %2 members. Click Start to begin.")
        .arg(totalFiles).arg(memberFileMap.size()));
    m_statsLabel->setText(QString("Members: %1 | Files: %2")
        .arg(memberFileMap.size()).arg(totalFiles));

    updateEmptyState();
}

void DistributionPanel::onScanWmFolder() {
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
    m_statusLabel->setText("Scanning " + wmPath + "...");
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
    m_statusLabel->setText("Scanning local folder " + localPath + "...");

    // Broadcast mode for Local: single local source → all active members
    if (m_broadcastCheck->isChecked()) {
        QList<MemberInfo> allMembers = m_registry->getAllMembers();
        for (const MemberInfo& member : allMembers) {
            if (!member.active) continue;
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
        m_statusLabel->setText(QString("Broadcast (local): %1 members ready. Source: %2")
            .arg(m_wmFolders.size()).arg(localPath));
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
                info.fullPath, member, month, fallbackDest);
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
    m_statsLabel->setText(statsText);
    m_statusLabel->setText("Local scan complete");

    populateTable();
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

    QList<MemberInfo> allMembers = m_registry->getAllMembers();
    for (const MemberInfo& member : allMembers) {
        if (!member.active) continue;

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

    m_statusLabel->setText(QString("Broadcast mode: %1 members ready. Source: %2")
        .arg(m_wmFolders.size()).arg(sourcePath));
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
                m_megaApi, info.fullPath, member, month, fallbackDest);
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
    m_statsLabel->setText(statsText);
    m_statusLabel->setText("Scan complete");

    populateTable();
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
                case ContentType::HOT_SEATS:      cType->setForeground(tm.supportSuccess()); break;
                case ContentType::THEORY_CALLS:   cType->setForeground(tm.supportSuccess()); break;
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

    // Controller-active mode: upload pending member files via DistributionController
    if (m_controllerActive && m_distController && !m_pendingMemberFileMap.isEmpty()) {
        int totalFiles = 0;
        for (const auto& files : m_pendingMemberFileMap) totalFiles += files.size();

        int ret = QMessageBox::question(this, "Confirm Upload",
            QString("Upload %1 files to %2 member folders?")
                .arg(totalFiles).arg(m_pendingMemberFileMap.size()),
            QMessageBox::Yes | QMessageBox::No);
        if (ret != QMessageBox::Yes) return;

        m_isRunning = true;
        m_startBtn->setEnabled(false);
        m_pauseBtn->setEnabled(true);
        m_stopBtn->setEnabled(true);
        AnimationHelper::smoothShow(m_progressBar);
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

    emit distributionStarted();

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
        } else {
            m_distController->pause();
            m_isPaused = true;
            m_pauseBtn->setText("Resume");
            m_statusLabel->setText("Upload paused");
            m_progressBar->setProperty("paused", true);
            m_progressBar->style()->polish(m_progressBar);
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
    } else {
        m_copyWorker->pause();
        m_isPaused = true;
        m_pauseBtn->setText("Resume");
        m_statusLabel->setText("Distribution paused");
        m_progressBar->setProperty("paused", true);
        m_progressBar->style()->polish(m_progressBar);
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
    }

    AnimationHelper::animateProgress(m_progressBar, m_successCount + m_failCount);
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

    emit distributionCompleted(success, failed);

    QMessageBox::information(this, "Distribution Complete",
        QString("Distribution finished.\n\nSucceeded: %1\nFailed: %2")
        .arg(success).arg(failed));

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

    emit distributionProgress(current, total, currentItem);
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

    // --- Templates ---
    mainLayout->addWidget(createSectionHeader("Templates"));
    {
        auto* row = new QHBoxLayout();
        row->setContentsMargins(0, 8, 0, 8);
        row->setSpacing(6);
        auto* saveBtn = ButtonFactory::createOutline("Save...", dialog);
        connect(saveBtn, &QPushButton::clicked, this, &DistributionPanel::onSaveTemplate);
        auto* loadBtn = ButtonFactory::createOutline("Load...", dialog);
        connect(loadBtn, &QPushButton::clicked, this, &DistributionPanel::onLoadTemplate);
        auto* deleteBtn = ButtonFactory::createOutline("Delete...", dialog);
        connect(deleteBtn, &QPushButton::clicked, this, &DistributionPanel::onDeleteTemplate);
        row->addWidget(saveBtn);
        row->addWidget(loadBtn);
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
        connect(importBtn, &QPushButton::clicked, this, &DistributionPanel::onImportDestinations);
        auto* exportBtn = ButtonFactory::createOutline("Export .txt", dialog);
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
        connect(previewBtn, &QPushButton::clicked, this, &DistributionPanel::onPreviewPathsClicked);
        auto* genBtn = ButtonFactory::createOutline("Generate Destinations", dialog);
        connect(genBtn, &QPushButton::clicked, this, &DistributionPanel::onGenerateDestinations);
        auto* helpBtn = ButtonFactory::createOutline("Variable Help", dialog);
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
    pathTypeCombo->addItem("Distribution Folder", "{member}");
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
    monthCombo->addItems({"January", "February", "March", "April", "May", "June",
                          "July", "August", "September", "October", "November", "December"});
    monthCombo->setCurrentIndex(QDate::currentDate().month() - 1);
    monthRow->addWidget(monthCombo, 1);
    dlgLayout->addLayout(monthRow);

    // Members selector
    QHBoxLayout* membersRow = new QHBoxLayout();
    membersRow->addWidget(new QLabel("Members:"));
    QComboBox* membersCombo = new QComboBox();
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
        QString text = tmpl.value("templateText").toString();
        int quickIdx = tmpl.value("quickTemplateIndex").toInt(-1);

        // Store template text and quick index as QVariantMap in itemData
        QVariantMap itemData;
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

        QJsonObject tmpl;
        tmpl["name"] = m_savedTemplateCombo->itemText(i);
        tmpl["templateText"] = text;
        tmpl["quickTemplateIndex"] = quickIdx;
        tmpl["created"] = QDateTime::currentDateTime().toString(Qt::ISODate);
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
    QString name = QInputDialog::getText(this, "Save Template",
        "Template name:", QLineEdit::Normal, "", &ok);
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

    int quickIdx = m_quickTemplateCombo->currentIndex();
    QVariantMap data;
    data["text"] = currentText;
    data["quickIdx"] = quickIdx;
    m_savedTemplateCombo->addItem(name, data);
    m_savedTemplateCombo->setCurrentIndex(m_savedTemplateCombo->count() - 1);
    saveSavedTemplates();

    m_statusLabel->setText(QString("Template '%1' saved.").arg(name));
}

void DistributionPanel::onDeleteTemplate() {
    int idx = m_savedTemplateCombo->currentIndex();
    if (idx <= 0) {
        QMessageBox::information(this, "Delete Template",
            "Select a saved template to delete.");
        return;
    }

    QString name = m_savedTemplateCombo->itemText(idx);
    auto reply = QMessageBox::question(this, "Delete Template",
        QString("Delete template '%1'?").arg(name),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    m_savedTemplateCombo->removeItem(idx);
    m_savedTemplateCombo->setCurrentIndex(0);
    saveSavedTemplates();

    m_statusLabel->setText(QString("Template '%1' deleted.").arg(name));
}

void DistributionPanel::onLoadTemplate(int index) {
    if (index <= 0) return;

    QVariantMap data = m_savedTemplateCombo->itemData(index).toMap();
    QString templateText = data.value("text").toString();
    int quickIdx = data.value("quickIdx", -1).toInt();

    if (!templateText.isEmpty()) {
        m_destTemplateEdit->setText(templateText);
    }

    if (quickIdx >= 0 && quickIdx < m_quickTemplateCombo->count()) {
        m_quickTemplateCombo->blockSignals(true);
        m_quickTemplateCombo->setCurrentIndex(quickIdx);
        m_quickTemplateCombo->blockSignals(false);
    }

    m_statusLabel->setText(QString("Loaded template '%1'.").arg(m_savedTemplateCombo->itemText(index)));
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
