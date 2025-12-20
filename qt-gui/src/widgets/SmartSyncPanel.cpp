#include "SmartSyncPanel.h"
#include "controllers/SmartSyncController.h"
#include "dialogs/SyncProfileDialog.h"
#include "dialogs/ScheduleSyncDialog.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QDebug>
#include <QScrollArea>
#include <QFrame>

namespace MegaCustom {

SmartSyncPanel::SmartSyncPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    updateButtonStates();
}

SmartSyncPanel::~SmartSyncPanel()
{
}

void SmartSyncPanel::setFileController(FileController* controller)
{
    m_fileController = controller;
}

void SmartSyncPanel::setController(SmartSyncController* controller)
{
    if (m_controller) {
        disconnect(m_controller, nullptr, this, nullptr);
    }

    m_controller = controller;

    if (m_controller) {
        // Connect controller signals to update UI
        connect(m_controller, &SmartSyncController::profilesLoaded,
                this, [this](int count) {
            m_profileTable->setRowCount(0);
            auto profiles = m_controller->getAllProfiles();
            for (const auto& profile : profiles) {
                int row = m_profileTable->rowCount();
                m_profileTable->insertRow(row);
                auto* nameItem = new QTableWidgetItem(profile.name);
                nameItem->setData(Qt::UserRole, profile.id);
                m_profileTable->setItem(row, 0, nameItem);
                m_profileTable->setItem(row, 1, new QTableWidgetItem(profile.localPath));
                m_profileTable->setItem(row, 2, new QTableWidgetItem(profile.remotePath));
                QString dirStr = profile.direction == SyncDirection::BIDIRECTIONAL ? "Bidirectional"
                               : profile.direction == SyncDirection::LOCAL_TO_REMOTE ? "Local->Remote"
                               : "Remote->Local";
                m_profileTable->setItem(row, 3, new QTableWidgetItem(dirStr));
                m_profileTable->setItem(row, 4, new QTableWidgetItem(
                    profile.isActive ? "Active" : (profile.isPaused ? "Paused" : "Ready")));
            }
            Q_UNUSED(count);
            updateButtonStates();
        });

        connect(m_controller, &SmartSyncController::profileCreated,
                this, [this](const QString& id, const QString& name) {
            int row = m_profileTable->rowCount();
            m_profileTable->insertRow(row);
            auto* nameItem = new QTableWidgetItem(name);
            nameItem->setData(Qt::UserRole, id);
            m_profileTable->setItem(row, 0, nameItem);
            m_profileTable->setItem(row, 1, new QTableWidgetItem(""));
            m_profileTable->setItem(row, 2, new QTableWidgetItem(""));
            m_profileTable->setItem(row, 3, new QTableWidgetItem("Bidirectional"));
            m_profileTable->setItem(row, 4, new QTableWidgetItem("Ready"));
            m_profileTable->selectRow(row);
        });

        connect(m_controller, &SmartSyncController::profileDeleted,
                this, [this](const QString& id) {
            for (int row = 0; row < m_profileTable->rowCount(); ++row) {
                if (m_profileTable->item(row, 0)->data(Qt::UserRole).toString() == id) {
                    m_profileTable->removeRow(row);
                    break;
                }
            }
            updateButtonStates();
        });

        connect(m_controller, &SmartSyncController::analysisStarted,
                this, [this](const QString& profileId) {
            Q_UNUSED(profileId);
            m_previewTable->setRowCount(0);
            m_syncStatusLabel->setText("Analyzing...");
            m_detailTabs->setCurrentIndex(0);  // Preview tab
        });

        connect(m_controller, &SmartSyncController::analysisComplete,
                this, [this](const QString& profileId, int uploads, int downloads,
                            int deletions, int conflicts) {
            Q_UNUSED(profileId);
            m_syncStatusLabel->setText(QString("Analysis complete: %1 uploads, %2 downloads, %3 deletions, %4 conflicts")
                .arg(uploads).arg(downloads).arg(deletions).arg(conflicts));
        });

        connect(m_controller, &SmartSyncController::syncStarted,
                this, [this](const QString& profileId) {
            Q_UNUSED(profileId);
            m_isSyncing = true;
            m_detailTabs->setCurrentIndex(2);  // Progress tab
            m_syncProgressBar->setValue(0);
            m_syncStatusLabel->setText("Syncing...");
            updateButtonStates();
        });

        connect(m_controller, &SmartSyncController::syncProgress,
                this, [this](const QString& profileId, const QString& currentFile,
                            int filesCompleted, int totalFiles,
                            qint64 bytesTransferred, qint64 totalBytes) {
            Q_UNUSED(profileId);
            Q_UNUSED(bytesTransferred);
            Q_UNUSED(totalBytes);
            int percent = totalFiles > 0 ? (filesCompleted * 100 / totalFiles) : 0;
            m_syncProgressBar->setValue(percent);
            m_syncStatusLabel->setText(QString("Syncing: %1 (%2/%3)")
                .arg(currentFile).arg(filesCompleted).arg(totalFiles));
        });

        connect(m_controller, &SmartSyncController::syncComplete,
                this, [this](const QString& profileId, bool success,
                            int filesUploaded, int filesDownloaded, int errors) {
            Q_UNUSED(profileId);
            m_isSyncing = false;
            m_syncProgressBar->setValue(100);
            m_syncStatusLabel->setText(QString("Sync %1: %2 uploaded, %3 downloaded, %4 errors")
                .arg(success ? "complete" : "failed")
                .arg(filesUploaded).arg(filesDownloaded).arg(errors));
            updateButtonStates();
        });

        connect(m_controller, &SmartSyncController::error,
                this, [this](const QString& operation, const QString& message) {
            QMessageBox::warning(this, operation, message);
        });

        // Load profiles
        m_controller->loadProfiles();
    }
}

void SmartSyncPanel::setupUI()
{
    setObjectName("SmartSyncPanel");

    // Main layout for the panel
    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // Create scroll area for the content
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // Content widget inside scroll area
    QWidget* contentWidget = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(contentWidget);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    // Header
    QLabel* titleLabel = new QLabel("Smart Sync", contentWidget);
    titleLabel->setObjectName("PanelTitle");
    mainLayout->addWidget(titleLabel);

    QLabel* subtitleLabel = new QLabel("Bidirectional sync between local folders and MEGA cloud with conflict resolution", contentWidget);
    subtitleLabel->setObjectName("PanelSubtitle");
    subtitleLabel->setWordWrap(true);
    mainLayout->addWidget(subtitleLabel);

    mainLayout->addSpacing(8);

    setupProfileSection(mainLayout);
    setupConfigSection(mainLayout);
    setupActionSection(mainLayout);
    setupDetailTabs(mainLayout);

    scrollArea->setWidget(contentWidget);
    outerLayout->addWidget(scrollArea);
}

void SmartSyncPanel::setupProfileSection(QVBoxLayout* mainLayout)
{
    auto* profileGroup = new QGroupBox("Sync Profiles", this);
    auto* profileLayout = new QVBoxLayout(profileGroup);

    // Toolbar
    auto* toolbarLayout = new QHBoxLayout();
    m_newProfileBtn = new QPushButton("New", this);
    m_newProfileBtn->setToolTip("Create new sync profile");
    m_newProfileBtn->setObjectName("PanelPrimaryButton");
    m_editProfileBtn = new QPushButton("Edit", this);
    m_editProfileBtn->setToolTip("Edit selected sync profile");
    m_editProfileBtn->setObjectName("PanelSecondaryButton");
    m_deleteProfileBtn = new QPushButton("Delete", this);
    m_deleteProfileBtn->setToolTip("Delete selected sync profile");
    m_deleteProfileBtn->setObjectName("PanelDangerButton");
    m_importBtn = new QPushButton("Import", this);
    m_importBtn->setToolTip("Import sync profile from file");
    m_importBtn->setObjectName("PanelSecondaryButton");
    m_exportBtn = new QPushButton("Export", this);
    m_exportBtn->setToolTip("Export sync profile to file");
    m_exportBtn->setObjectName("PanelSecondaryButton");

    connect(m_newProfileBtn, &QPushButton::clicked, this, &SmartSyncPanel::onNewProfileClicked);
    connect(m_editProfileBtn, &QPushButton::clicked, this, &SmartSyncPanel::onEditProfileClicked);
    connect(m_deleteProfileBtn, &QPushButton::clicked, this, &SmartSyncPanel::onDeleteProfileClicked);

    toolbarLayout->addWidget(m_newProfileBtn);
    toolbarLayout->addWidget(m_editProfileBtn);
    toolbarLayout->addWidget(m_deleteProfileBtn);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(m_importBtn);
    toolbarLayout->addWidget(m_exportBtn);

    profileLayout->addLayout(toolbarLayout);

    // Profile table
    m_profileTable = new QTableWidget(this);
    m_profileTable->setColumnCount(5);
    m_profileTable->setHorizontalHeaderLabels({"Name", "Local Path", "Remote Path", "Direction", "Status"});
    m_profileTable->horizontalHeader()->setStretchLastSection(true);
    m_profileTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_profileTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_profileTable->setMinimumHeight(100);
    m_profileTable->setMaximumHeight(180);

    connect(m_profileTable, &QTableWidget::itemSelectionChanged,
            this, &SmartSyncPanel::onProfileSelectionChanged);

    profileLayout->addWidget(m_profileTable);

    mainLayout->addWidget(profileGroup);
}

void SmartSyncPanel::setupConfigSection(QVBoxLayout* mainLayout)
{
    auto* configGroup = new QGroupBox("Sync Configuration", this);
    auto* configLayout = new QVBoxLayout(configGroup);

    // Row 1: Direction and Conflict
    auto* row1 = new QHBoxLayout();
    row1->addWidget(new QLabel("Direction:", this));
    m_directionCombo = new QComboBox(this);
    m_directionCombo->addItems({"Bidirectional", "Local to Remote", "Remote to Local",
                                 "Mirror Local", "Mirror Remote"});
    row1->addWidget(m_directionCombo);
    row1->addSpacing(20);

    row1->addWidget(new QLabel("Conflict Resolution:", this));
    m_conflictCombo = new QComboBox(this);
    m_conflictCombo->addItems({"Ask User", "Newer Wins", "Older Wins", "Larger Wins",
                                "Local Wins", "Remote Wins", "Rename Both"});
    row1->addWidget(m_conflictCombo);
    row1->addStretch();

    configLayout->addLayout(row1);

    // Row 2: Filters
    auto* row2 = new QHBoxLayout();
    row2->addWidget(new QLabel("Include:", this));
    m_includePatternEdit = new QLineEdit(this);
    m_includePatternEdit->setPlaceholderText("*.txt, *.doc (comma separated)");
    row2->addWidget(m_includePatternEdit, 1);
    row2->addSpacing(20);

    row2->addWidget(new QLabel("Exclude:", this));
    m_excludePatternEdit = new QLineEdit(this);
    m_excludePatternEdit->setPlaceholderText("*.tmp, .git (comma separated)");
    row2->addWidget(m_excludePatternEdit, 1);

    configLayout->addLayout(row2);

    // Row 3: Options
    auto* row3 = new QHBoxLayout();
    m_syncHiddenCheck = new QCheckBox("Hidden Files", this);
    m_syncTempCheck = new QCheckBox("Temp Files", this);
    m_deleteOrphansCheck = new QCheckBox("Delete Orphans", this);
    m_verifyCheck = new QCheckBox("Verify Transfers", this);

    row3->addWidget(m_syncHiddenCheck);
    row3->addWidget(m_syncTempCheck);
    row3->addWidget(m_deleteOrphansCheck);
    row3->addWidget(m_verifyCheck);
    row3->addStretch();

    // Auto-sync
    m_autoSyncCheck = new QCheckBox("Auto-sync every", this);
    m_autoSyncIntervalSpin = new QSpinBox(this);
    m_autoSyncIntervalSpin->setRange(1, 1440);
    m_autoSyncIntervalSpin->setValue(30);
    m_autoSyncIntervalSpin->setSuffix(" min");
    m_autoSyncIntervalSpin->setEnabled(false);

    connect(m_autoSyncCheck, &QCheckBox::toggled, m_autoSyncIntervalSpin, &QSpinBox::setEnabled);

    row3->addWidget(m_autoSyncCheck);
    row3->addWidget(m_autoSyncIntervalSpin);

    configLayout->addLayout(row3);

    mainLayout->addWidget(configGroup);
}

void SmartSyncPanel::setupActionSection(QVBoxLayout* mainLayout)
{
    auto* actionLayout = new QHBoxLayout();

    m_analyzeBtn = new QPushButton("Analyze", this);
    m_analyzeBtn->setToolTip("Preview changes before syncing");
    m_analyzeBtn->setObjectName("PanelSecondaryButton");
    m_startSyncBtn = new QPushButton("Start Sync", this);
    m_startSyncBtn->setToolTip("Start synchronization");
    m_startSyncBtn->setObjectName("PanelPrimaryButton");
    m_pauseSyncBtn = new QPushButton("Pause", this);
    m_pauseSyncBtn->setToolTip("Pause current sync operation");
    m_pauseSyncBtn->setObjectName("PanelSecondaryButton");
    m_stopSyncBtn = new QPushButton("Stop", this);
    m_stopSyncBtn->setToolTip("Stop and cancel sync operation");
    m_stopSyncBtn->setObjectName("PanelDangerButton");
    m_scheduleBtn = new QPushButton("Schedule...", this);
    m_scheduleBtn->setToolTip("Set up scheduled sync times");
    m_scheduleBtn->setObjectName("PanelSecondaryButton");

    connect(m_analyzeBtn, &QPushButton::clicked, this, &SmartSyncPanel::onAnalyzeClicked);
    connect(m_startSyncBtn, &QPushButton::clicked, this, &SmartSyncPanel::onStartSyncClicked);
    connect(m_pauseSyncBtn, &QPushButton::clicked, this, &SmartSyncPanel::onPauseSyncClicked);
    connect(m_stopSyncBtn, &QPushButton::clicked, this, &SmartSyncPanel::onStopSyncClicked);
    connect(m_scheduleBtn, &QPushButton::clicked, this, &SmartSyncPanel::onScheduleClicked);

    actionLayout->addWidget(m_analyzeBtn);
    actionLayout->addWidget(m_startSyncBtn);
    actionLayout->addWidget(m_pauseSyncBtn);
    actionLayout->addWidget(m_stopSyncBtn);
    actionLayout->addStretch();
    actionLayout->addWidget(m_scheduleBtn);

    mainLayout->addLayout(actionLayout);
}

void SmartSyncPanel::setupDetailTabs(QVBoxLayout* mainLayout)
{
    m_detailTabs = new QTabWidget(this);

    // Preview tab
    m_previewTable = new QTableWidget(this);
    m_previewTable->setColumnCount(4);
    m_previewTable->setHorizontalHeaderLabels({"Action", "File", "Local Info", "Remote Info"});
    m_previewTable->horizontalHeader()->setStretchLastSection(true);
    m_previewTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_previewTable->setAlternatingRowColors(true);
    m_previewTable->verticalHeader()->setVisible(false);
    m_detailTabs->addTab(m_previewTable, "Preview");

    // Add demo data to show action badge styling
    QStringList demoActions = {"Upload", "Download", "Download", "Skip", "Download"};
    QStringList demoFiles = {"Documents Backup", "Documents Backup", "Documents Backup", "Documents Backup", "Documents Backup"};
    QStringList demoStatus = {"Pending", "Ready", "Ready", "Ignored", "Ready"};

    m_previewTable->setRowCount(demoActions.size());
    for (int i = 0; i < demoActions.size(); ++i) {
        m_previewTable->setCellWidget(i, 0, createActionBadge(demoActions[i]));
        m_previewTable->setItem(i, 1, new QTableWidgetItem(demoFiles[i]));
        m_previewTable->setItem(i, 2, new QTableWidgetItem("--"));
        m_previewTable->setItem(i, 3, new QTableWidgetItem(demoStatus[i]));
    }
    m_previewTable->resizeColumnsToContents();

    // Conflicts tab
    m_conflictsTable = new QTableWidget(this);
    m_conflictsTable->setColumnCount(5);
    m_conflictsTable->setHorizontalHeaderLabels({"File", "Local Info", "Remote Info", "Resolution", "Action"});
    m_conflictsTable->horizontalHeader()->setStretchLastSection(true);
    m_detailTabs->addTab(m_conflictsTable, "Conflicts");

    // Progress tab
    m_progressWidget = new QWidget(this);
    auto* progressLayout = new QVBoxLayout(m_progressWidget);
    m_syncStatusLabel = new QLabel("Ready", this);
    m_syncProgressBar = new QProgressBar(this);
    m_syncProgressBar->setRange(0, 100);
    progressLayout->addWidget(m_syncStatusLabel);
    progressLayout->addWidget(m_syncProgressBar);
    progressLayout->addStretch();
    m_detailTabs->addTab(m_progressWidget, "Progress");

    // History tab
    m_historyTable = new QTableWidget(this);
    m_historyTable->setColumnCount(5);
    m_historyTable->setHorizontalHeaderLabels({"Date", "Profile", "Duration", "Files", "Status"});
    m_historyTable->horizontalHeader()->setStretchLastSection(true);
    m_detailTabs->addTab(m_historyTable, "History");

    mainLayout->addWidget(m_detailTabs, 1);
}

void SmartSyncPanel::updateButtonStates()
{
    bool hasSelection = m_profileTable && m_profileTable->currentRow() >= 0;

    if (m_editProfileBtn) m_editProfileBtn->setEnabled(hasSelection);
    if (m_deleteProfileBtn) m_deleteProfileBtn->setEnabled(hasSelection && !m_isSyncing);
    if (m_analyzeBtn) m_analyzeBtn->setEnabled(hasSelection && !m_isSyncing);
    if (m_startSyncBtn) m_startSyncBtn->setEnabled(hasSelection && !m_isSyncing);
    if (m_pauseSyncBtn) m_pauseSyncBtn->setEnabled(m_isSyncing);
    if (m_stopSyncBtn) m_stopSyncBtn->setEnabled(m_isSyncing);
    if (m_scheduleBtn) m_scheduleBtn->setEnabled(hasSelection);
}

void SmartSyncPanel::onNewProfileClicked()
{
    SyncProfileDialog dialog(this);
    dialog.setFileController(m_fileController);

    if (dialog.exec() == QDialog::Accepted) {
        QString name = dialog.profileName();
        QString localPath = dialog.localPath();
        QString remotePath = dialog.remotePath();

        if (m_controller) {
            m_controller->createProfile(name, localPath, remotePath);
        }
        emit createProfileRequested(name, localPath, remotePath);
    }
}

void SmartSyncPanel::onEditProfileClicked()
{
    if (m_profileTable->currentRow() >= 0) {
        emit editProfileRequested(m_currentProfileId);
    }
}

void SmartSyncPanel::onDeleteProfileClicked()
{
    if (m_profileTable->currentRow() >= 0) {
        auto result = QMessageBox::question(this, "Delete Profile",
            "Are you sure you want to delete this sync profile?",
            QMessageBox::Yes | QMessageBox::No);

        if (result == QMessageBox::Yes) {
            if (m_controller) {
                m_controller->deleteProfile(m_currentProfileId);
            }
            emit deleteProfileRequested(m_currentProfileId);
        }
    }
}

void SmartSyncPanel::onAnalyzeClicked()
{
    if (!m_currentProfileId.isEmpty()) {
        if (m_controller) {
            m_controller->analyzeProfile(m_currentProfileId);
        }
        emit analyzeRequested(m_currentProfileId);
    }
}

void SmartSyncPanel::onStartSyncClicked()
{
    if (!m_currentProfileId.isEmpty()) {
        if (m_controller) {
            m_controller->startSync(m_currentProfileId);
        }
        emit startSyncRequested(m_currentProfileId);
    }
}

void SmartSyncPanel::onPauseSyncClicked()
{
    if (!m_currentProfileId.isEmpty()) {
        if (m_controller) {
            if (m_isSyncing) {
                m_controller->pauseSync(m_currentProfileId);
            } else {
                m_controller->resumeSync(m_currentProfileId);
            }
        }
        emit pauseSyncRequested(m_currentProfileId);
    }
}

void SmartSyncPanel::onStopSyncClicked()
{
    if (!m_currentProfileId.isEmpty()) {
        if (m_controller) {
            m_controller->stopSync(m_currentProfileId);
        }
        emit stopSyncRequested(m_currentProfileId);
    }
}

void SmartSyncPanel::onScheduleClicked()
{
    if (m_currentProfileId.isEmpty()) {
        QMessageBox::warning(this, "No Profile Selected",
            "Please select a sync profile first.");
        return;
    }

    // Get current profile to populate dialog with existing schedule settings
    SyncProfile* profile = nullptr;
    if (m_controller) {
        profile = m_controller->getProfile(m_currentProfileId);
    }

    ScheduleSyncDialog dialog(this);

    // If profile has existing schedule settings, populate the dialog
    if (profile) {
        QString taskName = profile->name + " - Auto Sync";
        ScheduleSyncDialog::ScheduleType schedType = ScheduleSyncDialog::ScheduleType::ONCE;

        // Determine schedule type based on interval
        int interval = profile->autoSyncIntervalMinutes;
        if (interval > 0) {
            if (interval < 60) {
                // Less than an hour, treat as one-time
                schedType = ScheduleSyncDialog::ScheduleType::ONCE;
            } else if (interval < 1440) {
                // Less than a day, hourly
                schedType = ScheduleSyncDialog::ScheduleType::HOURLY;
                interval = interval / 60;  // Convert to hours
            } else if (interval < 10080) {
                // Less than a week, daily
                schedType = ScheduleSyncDialog::ScheduleType::DAILY;
                interval = interval / 1440;  // Convert to days
            } else {
                // Weekly
                schedType = ScheduleSyncDialog::ScheduleType::WEEKLY;
                interval = interval / 10080;  // Convert to weeks
            }
        }

        // Use last sync time or current time + 1 hour as start time
        QDateTime startTime = profile->lastSyncTime.isValid() ?
            profile->lastSyncTime.addSecs(profile->autoSyncIntervalMinutes * 60) :
            QDateTime::currentDateTime().addSecs(3600);

        dialog.setScheduleData(taskName, schedType, startTime, qMax(1, interval));
    }

    if (dialog.exec() == QDialog::Accepted) {
        // Convert dialog settings back to profile settings
        bool enabled = dialog.isEnabled();
        int intervalMinutes = 0;

        switch (dialog.scheduleType()) {
            case ScheduleSyncDialog::ScheduleType::ONCE:
                // For one-time, calculate minutes until the scheduled time
                intervalMinutes = static_cast<int>(QDateTime::currentDateTime().secsTo(dialog.startTime()) / 60);
                if (intervalMinutes < 1) intervalMinutes = 1;
                break;
            case ScheduleSyncDialog::ScheduleType::HOURLY:
                intervalMinutes = dialog.repeatInterval() * 60;  // Hours to minutes
                break;
            case ScheduleSyncDialog::ScheduleType::DAILY:
                intervalMinutes = dialog.repeatInterval() * 1440;  // Days to minutes
                break;
            case ScheduleSyncDialog::ScheduleType::WEEKLY:
                intervalMinutes = dialog.repeatInterval() * 10080;  // Weeks to minutes
                break;
        }

        // Update the profile via controller
        if (m_controller) {
            m_controller->setAutoSync(m_currentProfileId, enabled, intervalMinutes);

            // Show confirmation
            QString scheduleDesc;
            if (enabled) {
                switch (dialog.scheduleType()) {
                    case ScheduleSyncDialog::ScheduleType::ONCE:
                        scheduleDesc = QString("Scheduled to run once at %1").arg(dialog.startTime().toString("yyyy-MM-dd hh:mm"));
                        break;
                    case ScheduleSyncDialog::ScheduleType::HOURLY:
                        scheduleDesc = QString("Scheduled to run every %1 hour(s)").arg(dialog.repeatInterval());
                        break;
                    case ScheduleSyncDialog::ScheduleType::DAILY:
                        scheduleDesc = QString("Scheduled to run every %1 day(s)").arg(dialog.repeatInterval());
                        break;
                    case ScheduleSyncDialog::ScheduleType::WEEKLY:
                        scheduleDesc = QString("Scheduled to run every %1 week(s)").arg(dialog.repeatInterval());
                        break;
                }
            } else {
                scheduleDesc = "Schedule disabled";
            }

            QMessageBox::information(this, "Schedule Updated",
                QString("Sync schedule for '%1' has been updated.\n\n%2")
                    .arg(dialog.taskName()).arg(scheduleDesc));

            // Update the auto-sync UI elements to reflect the new settings
            if (m_autoSyncCheck) {
                m_autoSyncCheck->setChecked(enabled);
            }
            if (m_autoSyncIntervalSpin) {
                m_autoSyncIntervalSpin->setValue(intervalMinutes);
            }
        }

        // Emit signal for any external listeners
        emit scheduleRequested(m_currentProfileId);
    }
}

void SmartSyncPanel::onProfileSelectionChanged()
{
    int row = m_profileTable->currentRow();
    if (row >= 0) {
        auto* item = m_profileTable->item(row, 0);
        if (item) {
            m_currentProfileId = item->data(Qt::UserRole).toString();
        }
    } else {
        m_currentProfileId.clear();
    }
    updateButtonStates();
}

QWidget* SmartSyncPanel::createActionBadge(const QString& action)
{
    QLabel* badge = new QLabel(action, this);

    QString color;
    if (action == "Upload") {
        color = "#D90007";      // Red for uploads
    } else if (action == "Download") {
        color = "#0066CC";      // Blue for downloads
    } else {
        color = "#999999";      // Gray for Skip/other
    }

    badge->setStyleSheet(QString(
        "QLabel {"
        "  background-color: %1;"
        "  color: white;"
        "  border-radius: 4px;"
        "  padding: 2px 8px;"
        "  font-size: 11px;"
        "  font-weight: bold;"
        "}"
    ).arg(color));

    badge->setAlignment(Qt::AlignCenter);
    badge->setMinimumWidth(70);

    return badge;
}

} // namespace MegaCustom
