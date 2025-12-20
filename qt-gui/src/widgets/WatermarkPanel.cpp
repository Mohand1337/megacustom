#include "WatermarkPanel.h"
#include "utils/MemberRegistry.h"
#include "utils/TemplateExpander.h"
#include "features/Watermarker.h"
#include "controllers/WatermarkerController.h"
#include "dialogs/WatermarkSettingsDialog.h"
#include "styles/ThemeManager.h"
#include <QSettings>
#include <QInputDialog>
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

void WatermarkWorker::cancel() {
    m_cancelled = true;
}

void WatermarkWorker::process() {
    emit started();

    m_cancelled = false;
    int successCount = 0;
    int failCount = 0;
    int total = m_files.size();

    Watermarker watermarker;
    if (m_config) {
        watermarker.setConfig(*m_config);
    }

    // Set progress callback
    watermarker.setProgressCallback([this, total](const WatermarkProgress& progress) {
        emit this->progress(progress.currentIndex, total,
                           QString::fromStdString(progress.currentFile),
                           static_cast<int>(progress.percentComplete));
    });

    for (int i = 0; i < m_files.size() && !m_cancelled; ++i) {
        QString inputPath = m_files[i];
        emit progress(i, total, QFileInfo(inputPath).fileName(), 0);

        WatermarkResult result;
        std::string inputStd = inputPath.toStdString();
        std::string outputDir = m_outputDir.isEmpty() ? "" : m_outputDir.toStdString();

        if (!m_memberId.isEmpty()) {
            // Per-member watermarking
            if (Watermarker::isVideoFile(inputStd)) {
                result = watermarker.watermarkVideoForMember(inputStd, m_memberId.toStdString(), outputDir);
            } else if (Watermarker::isPdfFile(inputStd)) {
                result = watermarker.watermarkPdfForMember(inputStd, m_memberId.toStdString(), outputDir);
            } else {
                result.success = false;
                result.error = "Unsupported file type";
            }
        } else {
            // Global watermarking
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
    setupUI();
    loadMembers();
    updateButtonStates();

    // Connect registry signals
    connect(m_registry, &MemberRegistry::membersReloaded, this, &WatermarkPanel::loadMembers);
    connect(m_registry, &MemberRegistry::memberAdded, this, &WatermarkPanel::loadMembers);
    connect(m_registry, &MemberRegistry::memberRemoved, this, &WatermarkPanel::loadMembers);
}

WatermarkPanel::~WatermarkPanel() {
    if (m_workerThread) {
        if (m_worker) {
            m_worker->cancel();
        }
        m_workerThread->quit();
        m_workerThread->wait();
        delete m_workerThread;
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
            m_progressBar->setValue(progress.currentIndex);
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
            m_progressBar->setVisible(false);

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
    auto& tm = ThemeManager::instance();
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(16);

    // Title
    QLabel* titleLabel = new QLabel("Watermark Tool");
    titleLabel->setStyleSheet(QString("font-size: 18px; font-weight: bold; color: %1;")
        .arg(tm.textPrimary().name()));
    mainLayout->addWidget(titleLabel);

    // Description
    QLabel* descLabel = new QLabel("Add watermarks to videos (FFmpeg) and PDFs (Python). Select files, configure settings, and process.");
    descLabel->setStyleSheet(QString("color: %1; margin-bottom: 8px;").arg(tm.textSecondary().name()));
    descLabel->setWordWrap(true);
    mainLayout->addWidget(descLabel);

    // === File Selection Section ===
    QGroupBox* fileGroup = new QGroupBox("Source Files");
    QVBoxLayout* fileLayout = new QVBoxLayout(fileGroup);

    // File table
    m_fileTable = new QTableWidget();
    m_fileTable->setColumnCount(5);
    m_fileTable->setHorizontalHeaderLabels({"File Name", "Type", "Size", "Status", "Output"});
    m_fileTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fileTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileTable->setAlternatingRowColors(true);
    m_fileTable->verticalHeader()->setVisible(false);
    m_fileTable->setContextMenuPolicy(Qt::CustomContextMenu);

    m_fileTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_fileTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_fileTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_fileTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_fileTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_fileTable->setColumnWidth(1, 60);
    m_fileTable->setColumnWidth(2, 80);
    m_fileTable->setColumnWidth(3, 100);

    m_fileTable->setStyleSheet(QString(R"(
        QTableWidget {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 4px;
            gridline-color: %3;
        }
        QTableWidget::item {
            padding: 4px;
        }
        QTableWidget::item:selected {
            background-color: %4;
        }
        QHeaderView::section {
            background-color: %5;
            color: %6;
            padding: 6px;
            border: none;
            border-bottom: 1px solid %2;
        }
    )")
        .arg(tm.surfacePrimary().name())
        .arg(tm.borderSubtle().name())
        .arg(tm.borderSubtle().darker(120).name())
        .arg(tm.brandDefault().name())
        .arg(tm.surface2().name())
        .arg(tm.textPrimary().name()));

    connect(m_fileTable, &QTableWidget::itemSelectionChanged,
            this, &WatermarkPanel::onTableSelectionChanged);
    connect(m_fileTable, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
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

    // Member selection (hidden by default)
    m_memberWidget = new QWidget();
    QHBoxLayout* memberLayout = new QHBoxLayout(m_memberWidget);
    memberLayout->setContentsMargins(0, 0, 0, 0);
    memberLayout->addWidget(new QLabel("Member:"));
    m_memberCombo = new QComboBox();
    m_memberCombo->setMinimumWidth(200);
    connect(m_memberCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &WatermarkPanel::onMemberChanged);
    memberLayout->addWidget(m_memberCombo);
    m_memberWidget->setVisible(false);

    modeLayout->addWidget(m_memberWidget);
    modeLayout->addStretch();
    settingsLayout->addLayout(modeLayout);

    // Watermark text (for global mode)
    QGridLayout* textGrid = new QGridLayout();
    textGrid->setSpacing(8);

    QLabel* primaryLabel = new QLabel("Primary Text:");
    textGrid->addWidget(primaryLabel, 0, 0);
    m_primaryTextEdit = new QLineEdit();
    m_primaryTextEdit->setPlaceholderText("e.g., {member_name} - {date} or custom text");
    m_primaryTextEdit->setToolTip("Use template variables like {member_name}, {date}, {timestamp}. Click ? for help.");
    textGrid->addWidget(m_primaryTextEdit, 0, 1);

    QLabel* secondaryLabel = new QLabel("Secondary Text:");
    textGrid->addWidget(secondaryLabel, 1, 0);
    m_secondaryTextEdit = new QLineEdit();
    m_secondaryTextEdit->setPlaceholderText("e.g., {member_id} - {timestamp}");
    m_secondaryTextEdit->setToolTip("Use template variables like {member_id}, {month}, {year}. Click ? for help.");
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
    m_statusLabel->setStyleSheet("color: #888;");
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
    m_startBtn->setStyleSheet(QString("QPushButton { background-color: %1; } QPushButton:hover { background-color: %2; }")
        .arg(tm.supportSuccess().name())
        .arg(tm.supportSuccess().darker(110).name()));
    connect(m_startBtn, &QPushButton::clicked, this, &WatermarkPanel::onStartWatermark);
    actionsLayout->addWidget(m_startBtn);

    m_stopBtn = new QPushButton("Stop");
    m_stopBtn->setObjectName("PanelDangerButton");
    m_stopBtn->setIcon(QIcon(":/icons/stop.svg"));
    m_stopBtn->setEnabled(false);
    m_stopBtn->setStyleSheet(QString("QPushButton { background-color: %1; } QPushButton:hover { background-color: %2; }")
        .arg(tm.supportError().name())
        .arg(tm.supportError().darker(110).name()));
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
    m_statsLabel->setStyleSheet(QString("color: %1;").arg(tm.textSecondary().name()));
    mainLayout->addWidget(m_statsLabel);

    updateStats();
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
    int perMemberIndex = m_modeCombo->findText("Per-Member");
    if (perMemberIndex >= 0 && m_modeCombo->currentIndex() != perMemberIndex) {
        m_modeCombo->setCurrentIndex(perMemberIndex);
    }

    // Find and select the member in the combo box
    for (int i = 0; i < m_memberCombo->count(); ++i) {
        if (m_memberCombo->itemData(i).toString() == memberId) {
            m_memberCombo->setCurrentIndex(i);
            m_statusLabel->setText(QString("Selected member: %1").arg(m_memberCombo->currentText()));
            break;
        }
    }
}

void WatermarkPanel::loadMembers() {
    m_memberCombo->clear();
    m_memberCombo->addItem("-- Select Member --", "");

    QList<MemberInfo> members = m_registry->getActiveMembers();
    for (const MemberInfo& m : members) {
        m_memberCombo->addItem(QString("%1 (%2)").arg(m.displayName).arg(m.id), m.id);
    }
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
        if (m_memberCombo->currentData().toString().isEmpty()) {
            QMessageBox::warning(this, "No Member", "Please select a member.");
            return;
        }
    }

    // Collect file paths
    QStringList filePaths;
    for (const WatermarkFileInfo& info : m_files) {
        filePaths.append(info.filePath);
    }

    // Build config
    WatermarkConfig config = buildConfig();

    // Get output directory
    QString outputDir;
    if (!m_sameAsInputCheck->isChecked() && !m_outputDirEdit->text().isEmpty()) {
        outputDir = m_outputDirEdit->text();
    }

    // Get member ID (if per-member mode)
    QString memberId;
    if (m_modeCombo->currentData().toString() == "member") {
        memberId = m_memberCombo->currentData().toString();
    }

    // Reset file statuses
    for (int i = 0; i < m_files.size(); ++i) {
        m_files[i].status = "pending";
        m_files[i].outputPath.clear();
        m_files[i].error.clear();
        m_files[i].progressPercent = 0;
    }
    populateTable();

    // Create worker thread
    m_workerThread = new QThread();
    m_worker = new WatermarkWorker();
    m_worker->moveToThread(m_workerThread);

    m_worker->setFiles(filePaths);
    m_worker->setOutputDir(outputDir);
    m_worker->setConfig(config);
    m_worker->setMemberId(memberId);

    connect(m_workerThread, &QThread::started, m_worker, &WatermarkWorker::process);
    connect(m_worker, &WatermarkWorker::progress, this, &WatermarkPanel::onWorkerProgress);
    connect(m_worker, &WatermarkWorker::fileCompleted, this, &WatermarkPanel::onWorkerFileCompleted);
    connect(m_worker, &WatermarkWorker::finished, this, &WatermarkPanel::onWorkerFinished);
    connect(m_worker, &WatermarkWorker::finished, m_workerThread, &QThread::quit);
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater);
    connect(m_workerThread, &QThread::finished, this, [this]() {
        m_workerThread = nullptr;
        m_worker = nullptr;
    });

    m_isRunning = true;
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
    m_primaryTextEdit->setEnabled(isGlobal);
    m_secondaryTextEdit->setEnabled(isGlobal);
}

void WatermarkPanel::onMemberChanged(int index) {
    Q_UNUSED(index);
    // Could preview member watermark text here
}

void WatermarkPanel::onWorkerProgress(int fileIndex, int totalFiles, const QString& currentFile, int percent) {
    if (fileIndex >= 0 && fileIndex < m_files.size()) {
        m_files[fileIndex].status = "processing";
        m_files[fileIndex].progressPercent = percent;
        populateTable();
    }

    int overallPercent = (fileIndex * 100 + percent) / totalFiles;
    m_progressBar->setValue(overallPercent);
    m_statusLabel->setText(QString("Processing %1 (%2%)").arg(currentFile).arg(percent));

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

    m_progressBar->setValue(100);
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

        // File name
        QTableWidgetItem* nameItem = new QTableWidgetItem(info.fileName);
        nameItem->setToolTip(info.filePath);
        m_fileTable->setItem(row, 0, nameItem);

        // Type
        QTableWidgetItem* typeItem = new QTableWidgetItem(info.fileType.toUpper());
        typeItem->setTextAlignment(Qt::AlignCenter);
        if (info.fileType == "video") {
            typeItem->setForeground(tm.supportInfo()); // Blue
        } else {
            typeItem->setForeground(tm.supportError()); // Red for PDF
        }
        m_fileTable->setItem(row, 1, typeItem);

        // Size
        QTableWidgetItem* sizeItem = new QTableWidgetItem(formatFileSize(info.fileSize));
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_fileTable->setItem(row, 2, sizeItem);

        // Status
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
        m_fileTable->setItem(row, 3, statusItem);

        // Output
        QTableWidgetItem* outputItem = new QTableWidgetItem(info.outputPath);
        if (info.status == "error" && !info.error.isEmpty()) {
            outputItem->setText(info.error);
            outputItem->setForeground(tm.supportError());
        }
        m_fileTable->setItem(row, 4, outputItem);

        // Highlight entire row for errors with light red background
        if (info.status == "error") {
            QColor errorBg(255, 240, 240);  // Light red background
            nameItem->setBackground(errorBg);
            typeItem->setBackground(errorBg);
            sizeItem->setBackground(errorBg);
            statusItem->setBackground(errorBg);
            outputItem->setBackground(errorBg);
        } else if (info.status == "complete") {
            QColor successBg(240, 255, 240);  // Light green background
            nameItem->setBackground(successBg);
            typeItem->setBackground(successBg);
            sizeItem->setBackground(successBg);
            statusItem->setBackground(successBg);
            outputItem->setBackground(successBg);
        }
    }
}

void WatermarkPanel::updateStats() {
    int videoCount = 0;
    int pdfCount = 0;
    int errorCount = 0;
    int completeCount = 0;
    qint64 totalSize = 0;

    for (const WatermarkFileInfo& info : m_files) {
        if (info.fileType == "video") {
            videoCount++;
        } else {
            pdfCount++;
        }
        totalSize += info.fileSize;

        if (info.status == "error") {
            errorCount++;
        } else if (info.status == "complete") {
            completeCount++;
        }
    }

    QString statsText = QString("Files: %1 (%2 videos, %3 PDFs) | Total size: %4")
        .arg(m_files.size())
        .arg(videoCount)
        .arg(pdfCount)
        .arg(formatFileSize(totalSize));

    if (completeCount > 0) {
        statsText += QString(" | <span style='color: green;'>%1 completed</span>").arg(completeCount);
    }

    if (errorCount > 0) {
        statsText += QString(" | <span style='color: #D90007; font-weight: bold;'>%1 error(s)</span>").arg(errorCount);
    }

    m_statsLabel->setTextFormat(Qt::RichText);
    m_statsLabel->setText(statsText);
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
    m_memberCombo->setEnabled(!m_isRunning);
    m_primaryTextEdit->setEnabled(!m_isRunning && m_modeCombo->currentData().toString() == "global");
    m_secondaryTextEdit->setEnabled(!m_isRunning && m_modeCombo->currentData().toString() == "global");
    m_presetCombo->setEnabled(!m_isRunning);
    m_crfSpin->setEnabled(!m_isRunning);
    m_intervalSpin->setEnabled(!m_isRunning);
    m_durationSpin->setEnabled(!m_isRunning);
}

WatermarkConfig WatermarkPanel::buildConfig() const {
    WatermarkConfig config;

    // Get raw text from UI
    QString primaryText = m_primaryTextEdit->text();
    QString secondaryText = m_secondaryTextEdit->text();

    // Expand template variables if present
    if (TemplateExpander::hasVariables(primaryText) || TemplateExpander::hasVariables(secondaryText)) {
        TemplateExpander::Variables vars;

        // Check if per-member mode with a selected member
        if (m_modeCombo->currentData().toString() == "member" && m_memberCombo->currentIndex() > 0) {
            QString memberId = m_memberCombo->currentData().toString();
            MemberInfo member = m_registry->getMember(memberId);
            if (!member.id.isEmpty()) {
                vars = TemplateExpander::Variables::fromMember(member);
            } else {
                vars = TemplateExpander::Variables::withCurrentDateTime();
            }
        } else {
            // Global mode - just use date/time variables
            vars = TemplateExpander::Variables::withCurrentDateTime();
        }

        primaryText = TemplateExpander::expand(primaryText, vars);
        secondaryText = TemplateExpander::expand(secondaryText, vars);
    }

    config.primaryText = primaryText.toStdString();
    config.secondaryText = secondaryText.toStdString();
    config.preset = m_presetCombo->currentText().toStdString();
    config.crf = m_crfSpin->value();
    config.intervalSeconds = m_intervalSpin->value();
    config.durationSeconds = m_durationSpin->value();

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
<tr><td><b>{member}</b></td><td>Member's distribution folder path</td></tr>
<tr><td><b>{member_id}</b></td><td>Member's unique ID</td></tr>
<tr><td><b>{member_name}</b></td><td>Member's display name</td></tr>
<tr><td><b>{month}</b></td><td>Current month name (e.g., December)</td></tr>
<tr><td><b>{month_num}</b></td><td>Current month number (01-12)</td></tr>
<tr><td><b>{year}</b></td><td>Current year (e.g., 2025)</td></tr>
<tr><td><b>{date}</b></td><td>Current date (YYYY-MM-DD)</td></tr>
<tr><td><b>{timestamp}</b></td><td>Current timestamp (YYYYMMDD_HHMMSS)</td></tr>
</table>
<br>
<p><b>Examples:</b></p>
<p><i>Primary:</i> <code>EasyGroupBuys - {member_name}</code></p>
<p><i>Secondary:</i> <code>{member_id} - {date}</code></p>
<br>
<p><b>Note:</b> Member variables ({member}, {member_id}, {member_name}) are only
expanded in Per-Member mode with a selected member. In Global mode, only date/time
variables are expanded.</p>
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

void WatermarkPanel::onLoadPreset() {
    QString presetName = m_presetNameCombo->currentData().toString();
    if (!presetName.isEmpty()) {
        applyPreset(presetName);
    }
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

    if (m_modeCombo->currentData().toString() == "member" && m_memberCombo->currentIndex() > 0) {
        QString memberId = m_memberCombo->currentData().toString();
        MemberInfo member = m_registry->getMember(memberId);
        if (!member.id.isEmpty()) {
            vars = TemplateExpander::Variables::fromMember(member);
            memberInfo = QString("<b>Member:</b> %1 (%2)").arg(member.displayName).arg(member.id);
            mode = "Per-Member Mode";
        } else {
            vars = TemplateExpander::Variables::withCurrentDateTime();
            memberInfo = "<i>Member not found - using date/time only</i>";
            mode = "Per-Member Mode (member not found)";
        }
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

} // namespace MegaCustom
