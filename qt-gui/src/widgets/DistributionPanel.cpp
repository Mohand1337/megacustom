#include "DistributionPanel.h"
#include "utils/MemberRegistry.h"
#include "utils/TemplateExpander.h"
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
#include <QTimer>
#include <QMutex>
#include <QWaitCondition>
#include <QDebug>

namespace MegaCustom {

// ==================== Copy Task Structure ====================

struct FolderCopyTask {
    int index;
    QString sourcePath;
    QString destPath;
    QString memberId;
    bool copyFolderItself;
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
        if (!m_cloudCopier || !m_megaApi) {
            emit error("CloudCopier or MegaApi not available");
            emit allCompleted(0, m_tasks.size());
            return;
        }

        // Set conflict resolution
        m_cloudCopier->setDefaultConflictResolution(
            m_skipExisting ? ConflictResolution::SKIP : ConflictResolution::OVERWRITE);

        // Set operation mode (copy or move)
        m_cloudCopier->setOperationMode(
            m_moveMode ? OperationMode::MOVE : OperationMode::COPY);

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
            emit taskStarted(task.index, task.sourcePath, task.destPath);
            emit progress(i + 1, m_tasks.size(), task.memberId);

            bool taskSuccess = false;
            QString errorMsg;

            // Create destination folder if needed
            if (m_createDestFolder) {
                m_cloudCopier->createDestinations({task.destPath.toStdString()});
            }

            if (task.copyFolderItself) {
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
            } else {
                failed++;
            }

            emit taskCompleted(task.index, taskSuccess, errorMsg);
        }

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
    setupUI();
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
        // Connect controller signals to panel slots
        connect(m_distController, &DistributionController::distributionStarted,
                this, [this](const QString& jobId) {
            m_statusLabel->setText(QString("Distribution started (Job: %1)").arg(jobId));
        });

        connect(m_distController, &DistributionController::distributionProgress,
                this, [this](const QtDistributionProgress& progress) {
            m_progressBar->setValue(static_cast<int>(progress.overallPercent));
            m_statusLabel->setText(QString("%1: %2 - %3")
                .arg(progress.phase)
                .arg(progress.currentMember)
                .arg(progress.currentFile));
        });

        connect(m_distController, &DistributionController::memberCompleted,
                this, [this](const QtMemberStatus& status) {
            qDebug() << "Member completed:" << status.memberId << "state:" << status.state;
        });

        connect(m_distController, &DistributionController::distributionFinished,
                this, [this](const QtDistributionResult& result) {
            m_isRunning = false;
            m_startBtn->setEnabled(true);
            m_stopBtn->setEnabled(false);
            m_progressBar->setVisible(false);

            m_statusLabel->setText(QString("Distribution complete: %1/%2 members succeeded")
                .arg(result.membersCompleted).arg(result.totalMembers));
        });

        connect(m_distController, &DistributionController::distributionError,
                this, [this](const QString& error) {
            m_statusLabel->setText(QString("Error: %1").arg(error));
        });

        qDebug() << "DistributionPanel: DistributionController connected";
    }
}

void DistributionPanel::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // Title
    QLabel* titleLabel = new QLabel("Content Distribution");
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #e0e0e0;");
    mainLayout->addWidget(titleLabel);

    QLabel* descLabel = new QLabel(
        "Distribute watermarked content from /latest-wm/ to registered members. "
        "Scans for timestamped folders, matches them to members, and copies to destinations.");
    descLabel->setStyleSheet("color: #888; margin-bottom: 8px;");
    descLabel->setWordWrap(true);
    mainLayout->addWidget(descLabel);

    // Source/Destination Config
    QGroupBox* configGroup = new QGroupBox("CONFIGURATION");
    configGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; border: 1px solid #444; "
        "border-radius: 6px; margin-top: 12px; padding-top: 16px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; color: #e0e0e0; }");
    QGridLayout* configLayout = new QGridLayout(configGroup);
    configLayout->setSpacing(8);

    configLayout->addWidget(new QLabel("WM Source Path:"), 0, 0);
    m_wmPathEdit = new QLineEdit("/latest-wm");
    m_wmPathEdit->setToolTip("Path to scan for watermarked member folders");
    configLayout->addWidget(m_wmPathEdit, 0, 1);

    m_scanBtn = new QPushButton("Scan");
    m_scanBtn->setIcon(QIcon(":/icons/search.svg"));
    m_scanBtn->setToolTip("Scan for watermarked folders");
    connect(m_scanBtn, &QPushButton::clicked, this, &DistributionPanel::onScanWmFolder);
    configLayout->addWidget(m_scanBtn, 0, 2);

    configLayout->addWidget(new QLabel("Dest Template:"), 1, 0);
    m_destTemplateEdit = new QLineEdit("{member}/{year}/{month}/");
    m_destTemplateEdit->setToolTip("Destination path template. Use {member}, {member_id}, {year}, {month}, etc.");
    configLayout->addWidget(m_destTemplateEdit, 1, 1);

    // Template help button and month selector in a horizontal layout
    QWidget* templateBtnWidget = new QWidget();
    QHBoxLayout* templateBtnLayout = new QHBoxLayout(templateBtnWidget);
    templateBtnLayout->setContentsMargins(0, 0, 0, 0);
    templateBtnLayout->setSpacing(4);

    m_monthCombo = new QComboBox();
    m_monthCombo->addItems({"January", "February", "March", "April", "May", "June",
                           "July", "August", "September", "October", "November", "December"});
    m_monthCombo->setCurrentIndex(QDate::currentDate().month() - 1); // Current month
    m_monthCombo->setToolTip("Month for {month} variable");
    templateBtnLayout->addWidget(m_monthCombo);

    m_variableHelpBtn = new QPushButton("?");
    m_variableHelpBtn->setFixedSize(24, 24);
    m_variableHelpBtn->setToolTip("Show available template variables");
    connect(m_variableHelpBtn, &QPushButton::clicked, this, &DistributionPanel::onVariableHelpClicked);
    templateBtnLayout->addWidget(m_variableHelpBtn);

    configLayout->addWidget(templateBtnWidget, 1, 2);

    // Preview paths button row
    QHBoxLayout* previewRow = new QHBoxLayout();
    m_previewPathsBtn = new QPushButton("Preview Paths");
    m_previewPathsBtn->setToolTip("Preview expanded destination paths for selected members");
    m_previewPathsBtn->setStyleSheet(
        "QPushButton { background-color: #2196F3; color: white; "
        "border: none; border-radius: 4px; padding: 6px 12px; } "
        "QPushButton:hover { background-color: #1976D2; }");
    connect(m_previewPathsBtn, &QPushButton::clicked, this, &DistributionPanel::onPreviewPathsClicked);
    previewRow->addWidget(m_previewPathsBtn);
    previewRow->addStretch();
    configLayout->addLayout(previewRow, 2, 1, 1, 2);

    mainLayout->addWidget(configGroup);

    // Options Group
    QGroupBox* optionsGroup = new QGroupBox("OPTIONS");
    optionsGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; border: 1px solid #444; "
        "border-radius: 6px; margin-top: 12px; padding-top: 16px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; color: #e0e0e0; }");
    QGridLayout* optionsLayout = new QGridLayout(optionsGroup);
    optionsLayout->setSpacing(8);

    m_removeWatermarkSuffixCheck = new QCheckBox("Remove '_watermarked' from filenames");
    m_removeWatermarkSuffixCheck->setChecked(true);
    m_removeWatermarkSuffixCheck->setToolTip("Rename files to remove '_watermarked' suffix after copying");
    optionsLayout->addWidget(m_removeWatermarkSuffixCheck, 0, 0);

    m_createDestFolderCheck = new QCheckBox("Create destination folder if missing");
    m_createDestFolderCheck->setChecked(true);
    m_createDestFolderCheck->setToolTip("Automatically create the destination folder if it doesn't exist");
    optionsLayout->addWidget(m_createDestFolderCheck, 0, 1);

    m_copyFolderItselfCheck = new QCheckBox("Copy folder itself (not just contents)");
    m_copyFolderItselfCheck->setChecked(false);
    m_copyFolderItselfCheck->setToolTip("If checked, copies the entire folder. If unchecked, copies only the folder's contents.");
    optionsLayout->addWidget(m_copyFolderItselfCheck, 1, 0);

    m_skipExistingCheck = new QCheckBox("Skip existing files");
    m_skipExistingCheck->setChecked(true);
    m_skipExistingCheck->setToolTip("If checked, skips files/folders that already exist at destination. If unchecked, overwrites them.");
    optionsLayout->addWidget(m_skipExistingCheck, 1, 1);

    m_moveFilesCheck = new QCheckBox("Move files (delete source after distribution)");
    m_moveFilesCheck->setChecked(false);
    m_moveFilesCheck->setToolTip("If checked, files will be MOVED (source deleted after transfer).\n"
                                 "This is a server-side operation - no bandwidth is used.\n\n"
                                 "WARNING: Source files will be permanently deleted after successful transfer!");
    m_moveFilesCheck->setStyleSheet("QCheckBox { color: #D90007; }");  // Red text for warning
    optionsLayout->addWidget(m_moveFilesCheck, 2, 0, 1, 2);  // Span both columns

    mainLayout->addWidget(optionsGroup);

    // Members Table Group
    QGroupBox* tableGroup = new QGroupBox("DETECTED FOLDERS");
    tableGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; border: 1px solid #444; "
        "border-radius: 6px; margin-top: 12px; padding-top: 16px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; color: #e0e0e0; }");
    QVBoxLayout* tableLayout = new QVBoxLayout(tableGroup);

    m_memberTable = new QTableWidget();
    m_memberTable->setColumnCount(6);
    m_memberTable->setHorizontalHeaderLabels({"", "Member ID", "Timestamp", "WM Folder", "Destination", "Status"});
    m_memberTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_memberTable->setAlternatingRowColors(true);
    m_memberTable->verticalHeader()->setVisible(false);
    m_memberTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_memberTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_memberTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_memberTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_memberTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_memberTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    m_memberTable->setColumnWidth(0, 30);
    m_memberTable->setColumnWidth(1, 200);  // Wider for member dropdown
    m_memberTable->setColumnWidth(2, 140);
    m_memberTable->setColumnWidth(5, 100);  // Wider for status text
    m_memberTable->setStyleSheet(R"(
        QTableWidget {
            background-color: #1e1e1e;
            border: 1px solid #444;
            border-radius: 4px;
            gridline-color: #333;
        }
        QTableWidget::item {
            padding: 4px;
        }
        QTableWidget::item:selected {
            background-color: #0d6efd;
        }
        QHeaderView::section {
            background-color: #2a2a2a;
            color: #e0e0e0;
            padding: 6px;
            border: none;
            border-bottom: 1px solid #444;
        }
    )");
    tableLayout->addWidget(m_memberTable, 1);
    mainLayout->addWidget(tableGroup, 1);

    // Action Buttons
    QHBoxLayout* actionsLayout = new QHBoxLayout();

    // Secondary action buttons styling
    QString secondaryBtnStyle =
        "QPushButton { background-color: #444; color: white; "
        "border: none; border-radius: 4px; padding: 6px 12px; } "
        "QPushButton:hover { background-color: #555; } "
        "QPushButton:disabled { background-color: #333; color: #666; }";

    m_selectAllBtn = new QPushButton("Select All");
    m_selectAllBtn->setToolTip("Select all members for distribution");
    m_selectAllBtn->setStyleSheet(secondaryBtnStyle);
    connect(m_selectAllBtn, &QPushButton::clicked, this, &DistributionPanel::onSelectAll);

    m_deselectAllBtn = new QPushButton("Deselect All");
    m_deselectAllBtn->setToolTip("Deselect all members");
    m_deselectAllBtn->setStyleSheet(secondaryBtnStyle);
    connect(m_deselectAllBtn, &QPushButton::clicked, this, &DistributionPanel::onDeselectAll);

    m_bulkRenameBtn = new QPushButton("Bulk Rename");
    m_bulkRenameBtn->setIcon(QIcon(":/icons/edit.svg"));
    m_bulkRenameBtn->setToolTip("Remove '_watermarked' suffix from files in selected folders");
    m_bulkRenameBtn->setStyleSheet(
        "QPushButton { background-color: #FF9800; color: white; "
        "border: none; border-radius: 4px; padding: 6px 12px; } "
        "QPushButton:hover { background-color: #F57C00; }");
    connect(m_bulkRenameBtn, &QPushButton::clicked, this, &DistributionPanel::onBulkRename);

    actionsLayout->addWidget(m_selectAllBtn);
    actionsLayout->addWidget(m_deselectAllBtn);
    actionsLayout->addWidget(m_bulkRenameBtn);
    actionsLayout->addStretch();

    m_previewBtn = new QPushButton("Preview");
    m_previewBtn->setIcon(QIcon(":/icons/eye.svg"));
    m_previewBtn->setToolTip("Preview what will be copied");
    m_previewBtn->setStyleSheet(
        "QPushButton { background-color: #2196F3; color: white; "
        "border: none; border-radius: 4px; padding: 6px 12px; } "
        "QPushButton:hover { background-color: #1976D2; }");
    connect(m_previewBtn, &QPushButton::clicked, this, &DistributionPanel::onPreviewDistribution);

    m_startBtn = new QPushButton("Start Distribution");
    m_startBtn->setIcon(QIcon(":/icons/play.svg"));
    m_startBtn->setToolTip("Start copying to all selected members");
    m_startBtn->setStyleSheet(
        "QPushButton { background-color: #198754; color: white; "
        "border: none; border-radius: 4px; padding: 8px 16px; font-weight: bold; } "
        "QPushButton:hover { background-color: #157347; } "
        "QPushButton:disabled { background-color: #333; color: #666; }");
    connect(m_startBtn, &QPushButton::clicked, this, &DistributionPanel::onStartDistribution);

    m_pauseBtn = new QPushButton("Pause");
    m_pauseBtn->setIcon(QIcon(":/icons/pause.svg"));
    m_pauseBtn->setToolTip("Pause/Resume distribution");
    m_pauseBtn->setStyleSheet(
        "QPushButton { background-color: #FFC107; color: #333; "
        "border: none; border-radius: 4px; padding: 6px 12px; } "
        "QPushButton:hover { background-color: #FFB300; } "
        "QPushButton:disabled { background-color: #333; color: #666; }");
    m_pauseBtn->setEnabled(false);
    connect(m_pauseBtn, &QPushButton::clicked, this, &DistributionPanel::onPauseDistribution);

    m_stopBtn = new QPushButton("Stop");
    m_stopBtn->setIcon(QIcon(":/icons/x.svg"));
    m_stopBtn->setToolTip("Cancel distribution");
    m_stopBtn->setStyleSheet(
        "QPushButton { background-color: #DC3545; color: white; "
        "border: none; border-radius: 4px; padding: 6px 12px; } "
        "QPushButton:hover { background-color: #C82333; } "
        "QPushButton:disabled { background-color: #333; color: #666; }");
    m_stopBtn->setEnabled(false);
    connect(m_stopBtn, &QPushButton::clicked, this, &DistributionPanel::onStopDistribution);

    actionsLayout->addWidget(m_previewBtn);
    actionsLayout->addWidget(m_startBtn);
    actionsLayout->addWidget(m_pauseBtn);
    actionsLayout->addWidget(m_stopBtn);

    mainLayout->addLayout(actionsLayout);

    // Progress
    m_progressBar = new QProgressBar();
    m_progressBar->setVisible(false);
    mainLayout->addWidget(m_progressBar);

    // Status
    QHBoxLayout* statusLayout = new QHBoxLayout();
    m_statusLabel = new QLabel("Click 'Scan' to detect watermarked folders");
    m_statusLabel->setStyleSheet("color: #888;");
    statusLayout->addWidget(m_statusLabel);

    m_statsLabel = new QLabel();
    m_statsLabel->setStyleSheet("color: #888;");
    statusLayout->addWidget(m_statsLabel);
    statusLayout->addStretch();

    mainLayout->addLayout(statusLayout);
}

void DistributionPanel::refresh() {
    onScanWmFolder();
}

void DistributionPanel::addFilesFromWatermark(const QStringList& filePaths) {
    if (filePaths.isEmpty()) {
        return;
    }

    // Add received files to the distribution list
    // These files should be in /latest-wm/ format from WatermarkPanel
    m_statusLabel->setText(QString("Received %1 file(s) from Watermark panel").arg(filePaths.size()));

    // Log received files
    qDebug() << "DistributionPanel: Received" << filePaths.size() << "files from Watermark:";
    for (const QString& path : filePaths) {
        qDebug() << "  -" << path;
    }

    // Trigger a scan to pick up any new files in /latest-wm/
    // The watermarked files should already be in the /latest-wm/ folder
    onScanWmFolder();
}

void DistributionPanel::onScanWmFolder() {
    if (!m_fileController) {
        m_statusLabel->setText("Error: Not connected to MEGA");
        return;
    }

    // Reload member registry to pick up any new members
    m_registry->load();
    qDebug() << "DistributionPanel: Reloaded member registry, count:" << m_registry->getAllMembers().size();

    QString wmPath = m_wmPathEdit->text();
    m_statusLabel->setText("Scanning " + wmPath + "...");
    m_scanBtn->setEnabled(false);
    m_wmFolders.clear();
    m_memberTable->setRowCount(0);

    // Request folder listing via FileController
    // The result will come back via onFileListReceived slot
    m_fileController->refreshRemote(wmPath);
}

void DistributionPanel::onFileListReceived(const QVariantList& files) {
    m_scanBtn->setEnabled(true);
    m_wmFolders.clear();

    QString wmBasePath = m_wmPathEdit->text();
    if (!wmBasePath.endsWith("/")) wmBasePath += "/";

    // Pattern: memberId_YYYYMMDD_HHMMSS
    QRegularExpression re("^(.+)_(\\d{8}_\\d{6})$");

    // Get all members for fuzzy matching
    QList<MemberInfo> allMembers = m_registry->getAllMembers();

    qDebug() << "DistributionPanel: Received" << files.size() << "items";
    qDebug() << "DistributionPanel: Registry has" << allMembers.size() << "members";

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

        // Step 1: Try regex pattern (memberId_YYYYMMDD_HHMMSS)
        QRegularExpressionMatch match = re.match(folderName);
        if (match.hasMatch()) {
            info.memberId = match.captured(1);
            info.timestamp = match.captured(2);
            info.matched = m_registry->hasMember(info.memberId);
        }

        // Step 2: If no regex match, try exact member ID lookup
        if (!info.matched && info.memberId.isEmpty()) {
            if (m_registry->hasMember(folderName)) {
                info.memberId = folderName;
                info.timestamp = "N/A";
                info.matched = true;
            }
        }

        // Step 3: Try fuzzy matching - check if folder name contains any member ID
        if (!info.matched) {
            QString folderLower = folderName.toLower();
            for (const MemberInfo& member : allMembers) {
                QString idLower = member.id.toLower();
                QString nameLower = member.displayName.toLower();

                // Check if folder name contains member ID or display name
                if (folderLower.contains(idLower) || (!nameLower.isEmpty() && folderLower.contains(nameLower))) {
                    info.memberId = member.id;
                    info.timestamp = "N/A";
                    info.matched = true;
                    qDebug() << "  Fuzzy match:" << folderName << "->" << member.id;
                    break;
                }
                // Check if member ID or name contains folder name
                if (idLower.contains(folderLower) || (!nameLower.isEmpty() && nameLower.contains(folderLower))) {
                    info.memberId = member.id;
                    info.timestamp = "N/A";
                    info.matched = true;
                    qDebug() << "  Reverse fuzzy match:" << folderName << "->" << member.id;
                    break;
                }
            }
        }

        // Step 4: If still no match, keep folder name as potential member ID for manual override
        if (!info.matched) {
            if (info.memberId.isEmpty()) {
                info.memberId = folderName;  // Default to folder name
            }
            info.timestamp = "N/A";
        }

        info.selected = info.matched; // Auto-select if member exists
        m_wmFolders.append(info);

        qDebug() << "  Found folder:" << info.folderName
                 << "member:" << info.memberId
                 << "matched:" << info.matched;
    }

    // Update stats
    int matched = 0, unmatched = 0;
    for (const WmFolderInfo& info : m_wmFolders) {
        if (info.matched) matched++;
        else unmatched++;
    }

    m_statsLabel->setText(QString("Found: %1 folders (%2 matched, %3 unmatched)")
        .arg(m_wmFolders.size()).arg(matched).arg(unmatched));
    m_statusLabel->setText("Scan complete");

    populateTable();
}

void DistributionPanel::populateTable() {
    m_memberTable->setRowCount(0);
    m_memberTable->setRowCount(m_wmFolders.size());

    // Get all members for dropdown
    QList<MemberInfo> allMembers = m_registry->getAllMembers();

    for (int row = 0; row < m_wmFolders.size(); ++row) {
        WmFolderInfo& info = m_wmFolders[row];

        // Checkbox
        QCheckBox* check = new QCheckBox();
        check->setChecked(info.selected);
        connect(check, &QCheckBox::toggled, [this, row](bool checked) {
            if (row < m_wmFolders.size()) {
                m_wmFolders[row].selected = checked;
            }
        });
        QWidget* checkWidget = new QWidget();
        QHBoxLayout* checkLayout = new QHBoxLayout(checkWidget);
        checkLayout->addWidget(check);
        checkLayout->setAlignment(Qt::AlignCenter);
        checkLayout->setContentsMargins(0, 0, 0, 0);
        m_memberTable->setCellWidget(row, 0, checkWidget);

        // Member ID - use combo for unmatched, plain text for matched
        if (!info.matched && !allMembers.isEmpty()) {
            // Create combo for manual member selection
            QComboBox* memberCombo = new QComboBox();
            memberCombo->addItem("-- Select Member --", QString());
            for (const MemberInfo& member : allMembers) {
                QString display = member.displayName.isEmpty() ? member.id
                    : QString("%1 (%2)").arg(member.displayName, member.id);
                memberCombo->addItem(display, member.id);
            }

            // Set current selection if memberId partially matches
            for (int i = 0; i < memberCombo->count(); ++i) {
                if (memberCombo->itemData(i).toString() == info.memberId) {
                    memberCombo->setCurrentIndex(i);
                    break;
                }
            }

            memberCombo->setStyleSheet("QComboBox { background-color: #3d3d3d; color: #ff6b6b; }");
            memberCombo->setToolTip("Select member for this folder");
            connect(memberCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    [this, row, memberCombo]() {
                if (row < m_wmFolders.size()) {
                    QString selectedId = memberCombo->currentData().toString();
                    if (!selectedId.isEmpty()) {
                        m_wmFolders[row].memberId = selectedId;
                        m_wmFolders[row].matched = true;
                        m_wmFolders[row].selected = true;
                        // Update destination column
                        QString dest = getDestinationPath(selectedId);
                        if (m_memberTable->item(row, 4)) {
                            m_memberTable->item(row, 4)->setText(dest);
                        }
                        // Update status
                        if (m_memberTable->item(row, 5)) {
                            m_memberTable->item(row, 5)->setText("Ready");
                            m_memberTable->item(row, 5)->setForeground(QColor("#69db7c"));
                        }
                        // Check the checkbox
                        QWidget* w = m_memberTable->cellWidget(row, 0);
                        if (w) {
                            QCheckBox* c = w->findChild<QCheckBox*>();
                            if (c) c->setChecked(true);
                        }
                        // Update combo style
                        memberCombo->setStyleSheet("QComboBox { background-color: #3d3d3d; color: #69db7c; }");
                    }
                }
            });
            m_memberTable->setCellWidget(row, 1, memberCombo);
        } else {
            // Plain text for matched members
            QTableWidgetItem* idItem = new QTableWidgetItem(info.memberId);
            if (!info.matched) {
                idItem->setForeground(QColor("#ff6b6b"));
                idItem->setToolTip("Member not found in registry (no members available)");
            } else {
                idItem->setForeground(QColor("#69db7c"));
                // Show display name if available
                MemberInfo memberInfo = m_registry->getMember(info.memberId);
                if (!memberInfo.displayName.isEmpty() && memberInfo.displayName != info.memberId) {
                    idItem->setText(QString("%1 (%2)").arg(memberInfo.displayName, info.memberId));
                }
            }
            m_memberTable->setItem(row, 1, idItem);
        }

        // Timestamp
        QTableWidgetItem* tsItem = new QTableWidgetItem(info.timestamp);
        m_memberTable->setItem(row, 2, tsItem);

        // WM Folder
        QTableWidgetItem* wmItem = new QTableWidgetItem(info.fullPath);
        wmItem->setToolTip(info.fullPath);
        m_memberTable->setItem(row, 3, wmItem);

        // Destination - use TemplateExpander
        QString dest = getDestinationPath(info.memberId);
        QTableWidgetItem* destItem = new QTableWidgetItem(dest);
        destItem->setToolTip(dest);
        m_memberTable->setItem(row, 4, destItem);

        // Status
        QString status = info.matched ? "Ready" : "Select Member";
        QTableWidgetItem* statusItem = new QTableWidgetItem(status);
        statusItem->setTextAlignment(Qt::AlignCenter);
        if (!info.matched) {
            statusItem->setForeground(QColor("#ff6b6b"));
        } else {
            statusItem->setForeground(QColor("#69db7c"));
        }
        m_memberTable->setItem(row, 5, statusItem);
    }
}

QString DistributionPanel::getDestinationPath(const QString& memberId) {
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

    if (result.isValid) {
        return result.expandedPath;
    }

    // Fallback: simple replacement if expansion fails
    QString dest = templatePath;
    dest.replace("{member}", memberInfo.distributionFolder.isEmpty() ? memberId : memberInfo.distributionFolder);
    dest.replace("{member_id}", memberId);
    dest.replace("{member_name}", memberInfo.displayName.isEmpty() ? memberId : memberInfo.displayName);
    dest.replace("{month}", m_monthCombo->currentText());
    dest.replace("{year}", QString::number(QDate::currentDate().year()));
    return dest;
}

void DistributionPanel::onSelectAll() {
    for (int row = 0; row < m_memberTable->rowCount(); ++row) {
        QWidget* widget = m_memberTable->cellWidget(row, 0);
        if (widget) {
            QCheckBox* check = widget->findChild<QCheckBox*>();
            if (check) check->setChecked(true);
        }
        if (row < m_wmFolders.size()) {
            m_wmFolders[row].selected = true;
        }
    }
}

void DistributionPanel::onDeselectAll() {
    for (int row = 0; row < m_memberTable->rowCount(); ++row) {
        QWidget* widget = m_memberTable->cellWidget(row, 0);
        if (widget) {
            QCheckBox* check = widget->findChild<QCheckBox*>();
            if (check) check->setChecked(false);
        }
        if (row < m_wmFolders.size()) {
            m_wmFolders[row].selected = false;
        }
    }
}

void DistributionPanel::onPreviewDistribution() {
    QStringList preview;
    int selectedCount = 0;

    bool copyFolder = m_copyFolderItselfCheck->isChecked();

    for (const WmFolderInfo& info : m_wmFolders) {
        if (info.selected) {
            QString dest = getDestinationPath(info.memberId);
            if (copyFolder) {
                preview.append(QString("%1 -> %2%3")
                    .arg(info.fullPath)
                    .arg(dest)
                    .arg(info.folderName));
            } else {
                preview.append(QString("%1/* -> %2").arg(info.fullPath).arg(dest));
            }
            selectedCount++;
        }
    }

    if (selectedCount == 0) {
        QMessageBox::information(this, "Preview", "No members selected for distribution.");
        return;
    }

    QString msg = QString("Will copy %1 to %2 member folders:\n\n")
        .arg(copyFolder ? "folders" : "folder contents")
        .arg(selectedCount);
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
    if (!m_cloudCopier) {
        QMessageBox::warning(this, "Error", "CloudCopier not available. Make sure you're logged in.");
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
    bool copyFolderItself = m_copyFolderItselfCheck->isChecked();

    for (int i = 0; i < m_wmFolders.size(); ++i) {
        const WmFolderInfo& info = m_wmFolders[i];
        if (info.selected) {
            FolderCopyTask task;
            task.index = i;
            task.sourcePath = info.fullPath;
            task.destPath = getDestinationPath(info.memberId);
            task.memberId = info.memberId;
            task.copyFolderItself = copyFolderItself;
            tasks.append(task);
        }
    }

    if (tasks.isEmpty()) {
        QMessageBox::warning(this, "Error", "No members selected for distribution.");
        return;
    }

    bool moveMode = m_moveFilesCheck->isChecked();
    QString operationType = moveMode ? "MOVE" : "Copy";
    QString copyMode = copyFolderItself ? "entire folder" : "folder contents only";
    QString conflictMode = m_skipExistingCheck->isChecked() ? "skip existing" : "overwrite existing";

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
    m_progressBar->setVisible(true);
    m_progressBar->setMaximum(tasks.size());
    m_progressBar->setValue(0);
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
    if (m_copyWorker) {
        m_copyWorker->cancel();
    }
    m_statusLabel->setText("Stopping distribution...");
}

void DistributionPanel::onPauseDistribution() {
    if (!m_copyWorker) return;

    if (m_isPaused) {
        // Resume
        m_copyWorker->resume();
        m_isPaused = false;
        m_pauseBtn->setText("Pause");
        m_statusLabel->setText("Distribution resumed");
    } else {
        // Pause
        m_copyWorker->pause();
        m_isPaused = true;
        m_pauseBtn->setText("Resume");
        m_statusLabel->setText("Distribution paused");
    }
}

// ==================== Worker Thread Slots ====================

void DistributionPanel::onWorkerTaskStarted(int index, const QString& source, const QString& dest) {
    Q_UNUSED(source);

    if (index < m_wmFolders.size()) {
        m_statusLabel->setText(QString("Copying %1 -> %2")
            .arg(m_wmFolders[index].memberId)
            .arg(dest.section('/', -2)));

        QTableWidgetItem* statusItem = m_memberTable->item(index, 5);
        if (statusItem) {
            statusItem->setText("Copying...");
            statusItem->setForeground(QColor("#ffd43b"));
        }
    }
}

void DistributionPanel::onWorkerTaskCompleted(int index, bool success, const QString& error) {
    if (index >= m_wmFolders.size()) return;

    QTableWidgetItem* statusItem = m_memberTable->item(index, 5);

    if (success) {
        m_successCount++;
        if (statusItem) {
            statusItem->setText("Done");
            statusItem->setForeground(QColor("#69db7c"));
        }

        // Trigger bulk rename if option is checked
        if (m_removeWatermarkSuffixCheck->isChecked()) {
            QString dest = getDestinationPath(m_wmFolders[index].memberId);
            executeBulkRename(dest);
        }
    } else {
        m_failCount++;
        if (statusItem) {
            statusItem->setText("Failed");
            statusItem->setForeground(QColor("#ff6b6b"));
            statusItem->setToolTip(error);
        }
        qDebug() << "Task failed:" << m_wmFolders[index].memberId << "-" << error;
    }

    m_progressBar->setValue(m_successCount + m_failCount);
}

void DistributionPanel::onWorkerAllCompleted(int success, int failed) {
    m_isRunning = false;
    m_isPaused = false;
    m_startBtn->setEnabled(true);
    m_pauseBtn->setEnabled(false);
    m_pauseBtn->setText("Pause");
    m_stopBtn->setEnabled(false);
    m_progressBar->setVisible(false);

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
    m_progressBar->setValue(current);

    // Update status label with current item
    if (!currentItem.isEmpty()) {
        m_statusLabel->setText(QString("Copying: %1").arg(currentItem));
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

void DistributionPanel::onTableSelectionChanged() {
    // Nothing specific needed
}

void DistributionPanel::onVariableHelpClicked() {
    QString helpText = R"(
<h3>Template Variables</h3>
<p>Use these placeholders in your destination path template:</p>
<ul>
<li><b>{member}</b> - Member's distribution folder path</li>
<li><b>{member_id}</b> - Member's unique ID</li>
<li><b>{member_name}</b> - Member's display name</li>
<li><b>{month}</b> - Current month name (e.g., December)</li>
<li><b>{month_num}</b> - Current month number (01-12)</li>
<li><b>{year}</b> - Current year (e.g., 2025)</li>
<li><b>{date}</b> - Current date (YYYY-MM-DD)</li>
<li><b>{timestamp}</b> - Current timestamp (YYYYMMDD_HHMMSS)</li>
</ul>
<p><b>Example:</b></p>
<pre>{member}/{year}/{month}/</pre>
<p>For member "Alice" with folder "/Members/Alice":</p>
<pre>/Members/Alice/2025/December/</pre>
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
        QWidget* widget = m_memberTable->cellWidget(row, 0);
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
