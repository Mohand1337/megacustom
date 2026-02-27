#include "WatermarkPanel.h"
#include "utils/MemberRegistry.h"
#include "utils/TemplateExpander.h"
#include "utils/CopyHelper.h"
#include "utils/Constants.h"
#include "utils/MetricsStore.h"
#include "utils/MegaUploadUtils.h"
#include "features/Watermarker.h"
#include "controllers/WatermarkerController.h"
#include "dialogs/WatermarkSettingsDialog.h"
#include "styles/ThemeManager.h"
#include "utils/AnimationHelper.h"
#include <QSettings>
#include <QInputDialog>
#include <QStorageInfo>
#include <QElapsedTimer>
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

void WatermarkWorker::setMetricsStore(MetricsStore* store) {
    m_metricsStore = store;
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

    // All-members mode: iterate members x files (finish all files for one member before next)
    if (!m_memberIds.isEmpty()) {
        int total = m_files.size() * m_memberIds.size();
        int idx = 0;
        QMap<QString, QStringList> memberFileMap;

        for (int j = 0; j < m_memberIds.size() && !m_cancelled; ++j) {
            QString memberId = m_memberIds[j];
            QStringList memberOutputFiles;  // Track this member's outputs for auto-upload

            for (int i = 0; i < m_files.size() && !m_cancelled; ++i) {
                QString inputPath = m_files[i];
                std::string inputStd = inputPath.toStdString();
                QString label = QString("%1 [%2]").arg(QFileInfo(inputPath).fileName()).arg(memberId);
                emit progress(idx, total, label, 0);

                // Set FFmpeg progress callback with captured idx for correct row update
                int currentIdx = idx;
                watermarker.setProgressCallback([this, total, currentIdx](const WatermarkProgress& progress) {
                    emit this->progress(currentIdx, total,
                                       QString::fromStdString(progress.currentFile),
                                       static_cast<int>(progress.percentComplete));
                });

                // Expand templates with this member's data (Qt layer — single source of truth)
                applyMemberTemplates(watermarker, baseConfig, memberId,
                                     m_rawPrimaryTemplate, m_rawSecondaryTemplate);

                // Build member output path and watermark directly
                std::string outPath = watermarker.buildMemberOutputPath(inputStd, outputDir, memberId.toStdString());
                WatermarkResult result;
                if (Watermarker::isVideoFile(inputStd)) {
                    result = watermarker.watermarkVideo(inputStd, outPath);
                } else if (Watermarker::isPdfFile(inputStd)) {
                    result = watermarker.watermarkPdf(inputStd, outPath);
                } else {
                    result.success = false;
                    result.error = "Unsupported file type";
                }

                // Record metrics (Smart Engine learning)
                if (m_metricsStore) {
                    QString ext = QFileInfo(inputPath).suffix().toLower();
                    m_metricsStore->recordWatermark(
                        ext, memberId,
                        result.inputSizeBytes, result.outputSizeBytes,
                        result.processingTimeMs, result.success,
                        QString::fromStdString(result.error));
                }

                if (result.success) {
                    successCount++;
                    QString outFile = QString::fromStdString(result.outputFile);
                    memberFileMap[memberId].append(outFile);
                    memberOutputFiles.append(outFile);
                } else {
                    failCount++;
                }

                emit fileCompleted(idx, result.success,
                                  QString::fromStdString(result.outputFile),
                                  QString::fromStdString(result.error));
                idx++;
            }

            // === Auto-upload & cleanup after this member's batch ===
            if (m_autoUpload && m_megaApi && !m_cancelled && !memberOutputFiles.isEmpty()) {
                mega::MegaApi* api = static_cast<mega::MegaApi*>(m_megaApi);
                MemberInfo memberInfo = MemberRegistry::instance()->getMember(memberId);

                if (!memberInfo.distributionFolder.isEmpty()) {
                    int uploadOk = 0, uploadFail = 0, deleted = 0;

                    for (int f = 0; f < memberOutputFiles.size() && !m_cancelled; ++f) {
                        emit memberBatchUploading(memberId, f + 1,
                            memberOutputFiles.size(), QFileInfo(memberOutputFiles[f]).fileName());

                        QElapsedTimer uploadTimer;
                        uploadTimer.start();
                        std::string error;
                        qint64 fileSize = QFileInfo(memberOutputFiles[f]).size();

                        bool ok = megaApiUpload(api, memberOutputFiles[f].toStdString(),
                                                memberInfo.distributionFolder.toStdString(), error);

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
                        }
                    }
                } else {
                    qWarning() << "Auto-upload: No distribution folder for member" << memberId;
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

        WatermarkResult result;
        std::string inputStd = inputPath.toStdString();

        if (!m_memberId.isEmpty()) {
            // Single-member: expand templates with member data
            applyMemberTemplates(watermarker, baseConfig, m_memberId,
                                 m_rawPrimaryTemplate, m_rawSecondaryTemplate);

            std::string outPath = watermarker.buildMemberOutputPath(inputStd, outputDir, m_memberId.toStdString());
            if (Watermarker::isVideoFile(inputStd)) {
                result = watermarker.watermarkVideo(inputStd, outPath);
            } else if (Watermarker::isPdfFile(inputStd)) {
                result = watermarker.watermarkPdf(inputStd, outPath);
            } else {
                result.success = false;
                result.error = "Unsupported file type";
            }
        } else {
            // Global mode: text already expanded in config
            std::string outputPath = "";
            if (!outputDir.empty()) {
                outputPath = watermarker.generateOutputPath(inputStd, outputDir);
            }
            result = watermarker.watermarkFile(inputStd, outputPath);
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
    m_groupQuickSelectCombo->setToolTip("Add all members from a group (additive)");
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
        m_statusLabel->setText(QString("Received %1 file(s) from Downloader").arg(filePaths.size()));
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
        item->setForeground(QColor("#2196F3"));
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
        "Supported Files (*.mp4 *.mkv *.avi *.mov *.wmv *.flv *.webm *.pdf);;Videos (*.mp4 *.mkv *.avi *.mov *.wmv *.flv *.webm);;PDFs (*.pdf);;All Files (*)");

    for (const QString& file : files) {
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
}

void WatermarkPanel::onAddFolder() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Folder to Watermark");
    if (dir.isEmpty()) return;

    QStringList filters = {"*.mp4", "*.mkv", "*.avi", "*.mov", "*.wmv", "*.flv", "*.webm", "*.pdf"};
    QDirIterator it(dir, filters, QDir::Files, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QString file = it.next();

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
    if (!allMemberIds.isEmpty()) {
        QList<WatermarkFileInfo> expanded;
        expanded.reserve(baseFiles.size() * allMemberIds.size());
        for (const WatermarkFileInfo& base : baseFiles) {
            for (const QString& mid : allMemberIds) {
                WatermarkFileInfo entry = base;
                entry.memberId = mid;
                MemberInfo mi = m_registry->getMember(mid);
                entry.memberName = mi.displayName.isEmpty() ? mid : mi.displayName;
                entry.status = "pending";
                entry.outputPath.clear();
                entry.error.clear();
                entry.progressPercent = 0;
                expanded.append(entry);
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

    // Create worker thread
    m_workerThread = new QThread();
    m_worker = new WatermarkWorker();
    m_worker->moveToThread(m_workerThread);

    m_worker->setFiles(filePaths);
    m_worker->setOutputDir(outputDir);
    m_worker->setConfig(config);
    m_worker->setMemberId(memberId);
    m_worker->setMemberIds(allMemberIds);
    m_worker->setRawTemplates(m_primaryTextEdit->text(), m_secondaryTextEdit->text());

    // Smart Engine: pass auto-upload and metrics to worker
    if (m_autoUploadCheck->isChecked() && m_megaApi) {
        m_worker->setAutoUpload(true, m_megaApi);
    }
    if (m_metricsStore) {
        m_worker->setMetricsStore(m_metricsStore);
    }

    connect(m_workerThread, &QThread::started, m_worker, &WatermarkWorker::process);
    connect(m_worker, &WatermarkWorker::progress, this, &WatermarkPanel::onWorkerProgress);
    connect(m_worker, &WatermarkWorker::fileCompleted, this, &WatermarkPanel::onWorkerFileCompleted);
    connect(m_worker, &WatermarkWorker::finished, this, &WatermarkPanel::onWorkerFinished);
    connect(m_worker, &WatermarkWorker::finishedWithMapping, this,
            [this](int, int, const QMap<QString, QStringList>& memberFileMap) {
                if (!memberFileMap.isEmpty()) {
                    // Store the map for manual send — do NOT auto-emit
                    m_lastMemberFileMap = memberFileMap;
                    m_sendToDistBtn->setEnabled(true);
                    m_statusLabel->setText(QString("Watermarked files for %1 members. Click 'Send to Distribution' to upload.")
                        .arg(memberFileMap.size()));
                }
            });

    // Smart Engine: auto-upload progress signals
    connect(m_worker, &WatermarkWorker::memberBatchUploading, this,
        [this](const QString& memberId, int fileIdx, int totalFiles, const QString& fileName) {
            m_statusLabel->setText(QString("Uploading %1 to %2 (%3/%4)...")
                .arg(fileName).arg(memberId).arg(fileIdx).arg(totalFiles));
        });
    connect(m_worker, &WatermarkWorker::memberBatchCleanedUp, this,
        [this](const QString& memberId, int uploaded, int failed, int deleted) {
            qDebug() << "Smart pipeline:" << memberId
                     << "— uploaded:" << uploaded << "failed:" << failed << "cleaned:" << deleted;
        });
    connect(m_worker, &WatermarkWorker::diskSpaceWarning, this,
        [this](qint64 available, qint64 needed) {
            qWarning() << "Low disk space! Available:" << available << "Needed:" << needed;
            m_statusLabel->setText(QString("Warning: Low disk (%1 free)")
                .arg(formatFileSize(available)));
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
    m_workerThread->start();
}

void WatermarkPanel::onStopWatermark() {
    if (m_worker) {
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
        if (!item->isHidden()) {
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

    m_memberListWidget->blockSignals(true);
    for (int i = 0; i < m_memberListWidget->count(); ++i) {
        QListWidgetItem* item = m_memberListWidget->item(i);
        QString data = item->data(Qt::UserRole).toString();
        if (data == "GROUP:" + groupName) {
            item->setCheckState(Qt::Checked);
        }
        if (groupMemberIds.contains(data)) {
            item->setCheckState(Qt::Checked);
        }
    }
    m_memberListWidget->blockSignals(false);

    // Reset combo to prompt
    m_groupQuickSelectCombo->blockSignals(true);
    m_groupQuickSelectCombo->setCurrentIndex(0);
    m_groupQuickSelectCombo->blockSignals(false);

    onMemberSelectionChanged();
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
    if (fileIndex >= 0 && fileIndex < m_files.size()) {
        m_files[fileIndex].status = "processing";
        m_files[fileIndex].progressPercent = percent;
        populateTable();
    }

    int overallPercent = (fileIndex * 100 + percent) / totalFiles;
    AnimationHelper::animateProgress(m_progressBar, overallPercent);

    QString fileName = QFileInfo(currentFile).fileName();
    if (fileIndex >= 0 && fileIndex < m_files.size() && !m_files[fileIndex].memberName.isEmpty()) {
        m_statusLabel->setText(QString("Processing %1 for %2 (%3%)")
            .arg(fileName).arg(m_files[fileIndex].memberName).arg(percent));
    } else {
        m_statusLabel->setText(QString("Processing %1 (%2%)").arg(fileName).arg(percent));
    }

    emit watermarkProgress(fileIndex + 1, totalFiles, currentFile);
}

void WatermarkPanel::onWorkerFileCompleted(int fileIndex, bool success, const QString& outputPath, const QString& error) {
    if (fileIndex >= 0 && fileIndex < m_files.size()) {
        m_files[fileIndex].status = success ? "complete" : "error";
        m_files[fileIndex].outputPath = outputPath;
        m_files[fileIndex].error = error;
        m_files[fileIndex].progressPercent = 100;
        populateTable();
    }
}

void WatermarkPanel::onWorkerFinished(int successCount, int failCount) {
    m_isRunning = false;
    updateButtonStates();

    AnimationHelper::animateProgress(m_progressBar, 100);
    m_statusLabel->setText(QString("Completed: %1 success, %2 failed").arg(successCount).arg(failCount));

    emit watermarkCompleted(successCount, failCount);

    if (failCount == 0) {
        QMessageBox::information(this, "Complete",
            QString("Successfully watermarked %1 file(s).").arg(successCount));
    } else {
        QMessageBox::warning(this, "Complete with Errors",
            QString("Completed: %1 success, %2 failed.\n\nCheck the table for error details.")
                .arg(successCount).arg(failCount));
    }
}

void WatermarkPanel::populateTable() {
    auto& tm = ThemeManager::instance();
    m_fileTable->setRowCount(m_files.size());

    for (int row = 0; row < m_files.size(); ++row) {
        const WatermarkFileInfo& info = m_files[row];

        // File name (col 0)
        QTableWidgetItem* nameItem = new QTableWidgetItem(info.fileName);
        nameItem->setToolTip(info.filePath);
        m_fileTable->setItem(row, 0, nameItem);

        // Member (col 1)
        QTableWidgetItem* memberItem = new QTableWidgetItem(info.memberName);
        if (!info.memberId.isEmpty()) {
            memberItem->setToolTip(info.memberId);
            memberItem->setForeground(tm.supportInfo()); // Blue
        }
        m_fileTable->setItem(row, 1, memberItem);

        // Type (col 2)
        QTableWidgetItem* typeItem = new QTableWidgetItem(info.fileType.toUpper());
        typeItem->setTextAlignment(Qt::AlignCenter);
        if (info.fileType == "video") {
            typeItem->setForeground(tm.supportInfo()); // Blue
        } else {
            typeItem->setForeground(tm.supportError()); // Red for PDF
        }
        m_fileTable->setItem(row, 2, typeItem);

        // Size (col 3)
        QTableWidgetItem* sizeItem = new QTableWidgetItem(formatFileSize(info.fileSize));
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_fileTable->setItem(row, 3, sizeItem);

        // Status (col 4)
        QTableWidgetItem* statusItem = new QTableWidgetItem();
        if (info.status == "pending") {
            statusItem->setText("Pending");
            statusItem->setForeground(tm.textSecondary());
        } else if (info.status == "processing") {
            statusItem->setText(QString("Processing %1%").arg(info.progressPercent));
            statusItem->setForeground(tm.supportWarning()); // Yellow
        } else if (info.status == "complete") {
            statusItem->setText("Complete");
            statusItem->setForeground(tm.supportSuccess()); // Green
        } else if (info.status == "error") {
            statusItem->setText("Error");
            statusItem->setForeground(tm.supportError()); // Red
            statusItem->setToolTip(info.error);
        }
        statusItem->setTextAlignment(Qt::AlignCenter);
        m_fileTable->setItem(row, 4, statusItem);

        // Output (col 5)
        QTableWidgetItem* outputItem = new QTableWidgetItem(info.outputPath);
        if (info.status == "error" && !info.error.isEmpty()) {
            outputItem->setText(info.error);
            outputItem->setForeground(tm.supportError());
        }
        m_fileTable->setItem(row, 5, outputItem);

        // Highlight entire row for errors with light red background
        if (info.status == "error") {
            QColor errorBg(255, 240, 240);  // Light red background
            nameItem->setBackground(errorBg);
            memberItem->setBackground(errorBg);
            typeItem->setBackground(errorBg);
            sizeItem->setBackground(errorBg);
            statusItem->setBackground(errorBg);
            outputItem->setBackground(errorBg);
        } else if (info.status == "complete") {
            QColor successBg(240, 255, 240);  // Light green background
            nameItem->setBackground(successBg);
            memberItem->setBackground(successBg);
            typeItem->setBackground(successBg);
            sizeItem->setBackground(successBg);
            statusItem->setBackground(successBg);
            outputItem->setBackground(successBg);
        }
    }
}

void WatermarkPanel::updateStats() {
    int videoCount = 0, pdfCount = 0, errorCount = 0, completeCount = 0;
    qint64 totalSize = 0;
    QSet<QString> uniquePaths;

    for (const WatermarkFileInfo& info : m_files) {
        if (!uniquePaths.contains(info.filePath)) {
            uniquePaths.insert(info.filePath);
            if (info.fileType == "video") videoCount++;
            else pdfCount++;
            totalSize += info.fileSize;
        }
        if (info.status == "error") errorCount++;
        else if (info.status == "complete") completeCount++;
    }

    int uniqueFileCount = uniquePaths.size();
    int totalOps = m_files.size();

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

    if (errorCount > 0) {
        statsText += QString(" | <span style='color: #D90007; font-weight: bold;'>%1 error(s)</span>").arg(errorCount);
    }

    m_statsLabel->setTextFormat(Qt::RichText);
    m_statsLabel->setText(statsText);

    // Update smart estimate when file list changes
    updateSmartEstimate();
}

void WatermarkPanel::updateButtonStates() {
    bool hasFiles = !m_files.isEmpty();
    bool hasSelection = m_fileTable->selectionModel()->hasSelection();

    // Count completed files for distribution button
    int completedCount = 0;
    for (const WatermarkFileInfo& info : m_files) {
        if (info.status == "complete" && !info.outputPath.isEmpty()) {
            completedCount++;
        }
    }

    m_removeBtn->setEnabled(hasSelection && !m_isRunning);
    m_clearBtn->setEnabled(hasFiles && !m_isRunning);
    m_startBtn->setEnabled(hasFiles && !m_isRunning);
    m_stopBtn->setEnabled(m_isRunning);
    m_sendToDistBtn->setEnabled(completedCount > 0 && !m_isRunning);

    m_addFilesBtn->setEnabled(!m_isRunning);
    m_addFolderBtn->setEnabled(!m_isRunning);
    m_modeCombo->setEnabled(!m_isRunning);
    m_memberListWidget->setEnabled(!m_isRunning);
    m_selectAllMembersBtn->setEnabled(!m_isRunning);
    m_deselectAllMembersBtn->setEnabled(!m_isRunning);
    m_groupQuickSelectCombo->setEnabled(!m_isRunning);
    m_memberSearchEdit->setEnabled(!m_isRunning);
    m_primaryTextEdit->setEnabled(!m_isRunning);
    m_secondaryTextEdit->setEnabled(!m_isRunning);
    m_presetCombo->setEnabled(!m_isRunning);
    m_crfSpin->setEnabled(!m_isRunning);
    m_intervalSpin->setEnabled(!m_isRunning);
    m_durationSpin->setEnabled(!m_isRunning);
    m_embedMetadataCheck->setEnabled(!m_isRunning);
    bool metaEnabled = !m_isRunning && m_embedMetadataCheck->isChecked();
    m_metaTitleEdit->setEnabled(metaEnabled);
    m_metaAuthorEdit->setEnabled(metaEnabled);
    m_metaCommentEdit->setEnabled(metaEnabled);
    m_metaKeywordsEdit->setEnabled(metaEnabled);
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
    // If we have a member file map from multi-member watermarking, send that
    if (!m_lastMemberFileMap.isEmpty()) {
        m_statusLabel->setText(QString("Sending files for %1 members to Distribution...")
            .arg(m_lastMemberFileMap.size()));
        emit sendToDistributionMapped(m_lastMemberFileMap);
        m_lastMemberFileMap.clear();
        m_sendToDistBtn->setEnabled(false);
        return;
    }

    // Otherwise send flat file list (global/single-member mode)
    QStringList completedFiles;
    for (const WatermarkFileInfo& info : m_files) {
        if (info.status == "complete" && !info.outputPath.isEmpty()) {
            completedFiles.append(info.outputPath);
        }
    }

    if (completedFiles.isEmpty()) {
        QMessageBox::information(this, "No Files",
            "No completed watermarked files to send to Distribution.");
        return;
    }

    m_statusLabel->setText(QString("Sending %1 file(s) to Distribution...").arg(completedFiles.size()));
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
    QString previewText = QString(R"(
<h3>Watermark Preview</h3>
<p><b>Mode:</b> %1</p>
<p>%2</p>
<hr>
<table style="width: 100%;">
<tr>
    <td style="width: 100px;"><b>Primary Text:</b></td>
    <td style="background: #f0f0f0; padding: 8px; border-radius: 4px;">
        <code>%3</code>
    </td>
</tr>
<tr><td colspan="2" style="height: 8px;"></td></tr>
<tr>
    <td><b>Template:</b></td>
    <td style="color: #666;"><i>%4</i></td>
</tr>
<tr><td colspan="2" style="height: 16px;"></td></tr>
<tr>
    <td><b>Secondary Text:</b></td>
    <td style="background: #f0f0f0; padding: 8px; border-radius: 4px;">
        <code>%5</code>
    </td>
</tr>
<tr><td colspan="2" style="height: 8px;"></td></tr>
<tr>
    <td><b>Template:</b></td>
    <td style="color: #666;"><i>%6</i></td>
</tr>
</table>
)")
        .arg(mode)
        .arg(memberInfo)
        .arg(expandedPrimary.isEmpty() ? "<i>(empty)</i>" : expandedPrimary.toHtmlEscaped())
        .arg(primaryText.isEmpty() ? "<i>(empty)</i>" : primaryText.toHtmlEscaped())
        .arg(expandedSecondary.isEmpty() ? "<i>(empty)</i>" : expandedSecondary.toHtmlEscaped())
        .arg(secondaryText.isEmpty() ? "<i>(empty)</i>" : secondaryText.toHtmlEscaped());

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
