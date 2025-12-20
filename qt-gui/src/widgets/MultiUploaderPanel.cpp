#include "MultiUploaderPanel.h"
#include "controllers/MultiUploaderController.h"
#include "controllers/FileController.h"
#include "dialogs/RemoteFolderBrowserDialog.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QHeaderView>
#include <QDebug>
#include <QScrollArea>
#include <QFrame>

namespace MegaCustom {

MultiUploaderPanel::MultiUploaderPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    updateButtonStates();
}

MultiUploaderPanel::~MultiUploaderPanel()
{
}

void MultiUploaderPanel::setController(MultiUploaderController* controller)
{
    if (m_controller) {
        disconnect(m_controller, nullptr, this, nullptr);
    }

    m_controller = controller;

    if (m_controller) {
        // Connect controller signals to update UI
        connect(m_controller, &MultiUploaderController::sourceFilesChanged,
                this, [this](int count, qint64 totalBytes) {
            QString sizeStr = totalBytes > 1024*1024
                ? QString::number(totalBytes / (1024*1024)) + " MB"
                : QString::number(totalBytes / 1024) + " KB";
            m_sourceSummaryLabel->setText(QString("%1 files (%2)").arg(count).arg(sizeStr));
            updateButtonStates();
        });

        connect(m_controller, &MultiUploaderController::destinationsChanged,
                this, [this](const QStringList& destinations) {
            m_destinationList->clear();
            m_destinationList->addItems(destinations);
            updateButtonStates();
        });

        connect(m_controller, &MultiUploaderController::taskCreated,
                this, [this](int taskId, const QString& fileName, const QString& destination) {
            int row = m_taskTable->rowCount();
            m_taskTable->insertRow(row);
            m_taskTable->setItem(row, 0, new QTableWidgetItem(QString::number(taskId)));
            m_taskTable->setItem(row, 1, new QTableWidgetItem("Pending"));
            m_taskTable->setItem(row, 2, new QTableWidgetItem(fileName));
            m_taskTable->setItem(row, 3, new QTableWidgetItem(destination));
            m_taskTable->setItem(row, 4, new QTableWidgetItem("0%"));
        });

        connect(m_controller, &MultiUploaderController::taskProgress,
                this, [this](int taskId, qint64 bytesUploaded, qint64 totalBytes, double speed) {
            Q_UNUSED(speed);
            int percent = totalBytes > 0 ? (bytesUploaded * 100 / totalBytes) : 0;
            for (int row = 0; row < m_taskTable->rowCount(); ++row) {
                if (m_taskTable->item(row, 0)->text().toInt() == taskId) {
                    m_taskTable->item(row, 4)->setText(QString("%1%").arg(percent));
                    break;
                }
            }
        });

        connect(m_controller, &MultiUploaderController::taskStatusChanged,
                this, [this](int taskId, const QString& status) {
            for (int row = 0; row < m_taskTable->rowCount(); ++row) {
                if (m_taskTable->item(row, 0)->text().toInt() == taskId) {
                    m_taskTable->item(row, 1)->setText(status);
                    break;
                }
            }
        });

        connect(m_controller, &MultiUploaderController::uploadStarted,
                this, [this](int totalTasks) {
            Q_UNUSED(totalTasks);
            m_isUploading = true;
            updateButtonStates();
        });

        connect(m_controller, &MultiUploaderController::uploadComplete,
                this, [this](int successful, int failed, int skipped) {
            m_isUploading = false;
            updateButtonStates();
            QMessageBox::information(this, "Upload Complete",
                QString("Upload finished.\nSuccessful: %1\nFailed: %2\nSkipped: %3")
                .arg(successful).arg(failed).arg(skipped));
        });

        connect(m_controller, &MultiUploaderController::error,
                this, [this](const QString& operation, const QString& message) {
            QMessageBox::warning(this, operation, message);
        });
    }
}

void MultiUploaderPanel::setFileController(FileController* controller)
{
    m_fileController = controller;
}

void MultiUploaderPanel::setupUI()
{
    setObjectName("MultiUploaderPanel");

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
    QLabel* titleLabel = new QLabel("Multi Uploader", contentWidget);
    titleLabel->setObjectName("PanelTitle");
    mainLayout->addWidget(titleLabel);

    QLabel* subtitleLabel = new QLabel("Upload files to multiple MEGA cloud destinations with distribution rules", contentWidget);
    subtitleLabel->setObjectName("PanelSubtitle");
    subtitleLabel->setWordWrap(true);
    mainLayout->addWidget(subtitleLabel);

    mainLayout->addSpacing(8);

    setupSourceSection(mainLayout);
    setupDestinationSection(mainLayout);
    setupRulesSection(mainLayout);
    setupTaskSection(mainLayout);

    mainLayout->addStretch();

    scrollArea->setWidget(contentWidget);
    outerLayout->addWidget(scrollArea);
}

void MultiUploaderPanel::setupSourceSection(QVBoxLayout* mainLayout)
{
    auto* sourceGroup = new QGroupBox("Source Files", this);
    auto* sourceLayout = new QVBoxLayout(sourceGroup);

    // Toolbar
    auto* toolbarLayout = new QHBoxLayout();
    m_addFilesBtn = new QPushButton("Add Files", this);
    m_addFilesBtn->setToolTip("Select files to upload");
    m_addFolderBtn = new QPushButton("Add Folder", this);
    m_addFolderBtn->setToolTip("Add entire folder for upload");
    m_clearFilesBtn = new QPushButton("Clear All", this);
    m_clearFilesBtn->setToolTip("Remove all source files from list");
    m_clearFilesBtn->setObjectName("PanelDangerButton");
    m_sourceSummaryLabel = new QLabel("No files selected", this);
    m_sourceSummaryLabel->setObjectName("SummaryLabel");

    connect(m_addFilesBtn, &QPushButton::clicked, this, &MultiUploaderPanel::onAddFilesClicked);
    connect(m_addFolderBtn, &QPushButton::clicked, this, &MultiUploaderPanel::onAddFolderClicked);
    connect(m_clearFilesBtn, &QPushButton::clicked, this, &MultiUploaderPanel::onClearFilesClicked);

    toolbarLayout->addWidget(m_addFilesBtn);
    toolbarLayout->addWidget(m_addFolderBtn);
    toolbarLayout->addWidget(m_clearFilesBtn);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(m_sourceSummaryLabel);

    sourceLayout->addLayout(toolbarLayout);

    // File list
    m_sourceList = new QListWidget(this);
    m_sourceList->setMinimumHeight(80);
    m_sourceList->setMaximumHeight(150);
    sourceLayout->addWidget(m_sourceList);

    mainLayout->addWidget(sourceGroup);
}

void MultiUploaderPanel::setupDestinationSection(QVBoxLayout* mainLayout)
{
    auto* destGroup = new QGroupBox("Destinations", this);
    auto* destLayout = new QHBoxLayout(destGroup);

    m_destinationList = new QListWidget(this);
    m_destinationList->setMinimumHeight(60);
    m_destinationList->setMaximumHeight(120);
    destLayout->addWidget(m_destinationList, 1);

    auto* btnLayout = new QVBoxLayout();
    m_addDestBtn = new QPushButton("Add", this);
    m_addDestBtn->setToolTip("Add MEGA cloud destination folder");
    m_addDestBtn->setObjectName("PanelSecondaryButton");
    m_removeDestBtn = new QPushButton("Remove", this);
    m_removeDestBtn->setToolTip("Remove selected destination");
    m_removeDestBtn->setObjectName("PanelDangerButton");

    connect(m_addDestBtn, &QPushButton::clicked, this, &MultiUploaderPanel::onAddDestinationClicked);
    connect(m_removeDestBtn, &QPushButton::clicked, this, &MultiUploaderPanel::onRemoveDestinationClicked);

    btnLayout->addWidget(m_addDestBtn);
    btnLayout->addWidget(m_removeDestBtn);
    btnLayout->addStretch();

    destLayout->addLayout(btnLayout);

    mainLayout->addWidget(destGroup);
}

void MultiUploaderPanel::setupRulesSection(QVBoxLayout* mainLayout)
{
    auto* rulesGroup = new QGroupBox("Distribution Rules", this);
    auto* rulesLayout = new QVBoxLayout(rulesGroup);

    // Rule type selector
    auto* ruleTypeLayout = new QHBoxLayout();
    ruleTypeLayout->addWidget(new QLabel("Rule Type:", this));
    m_ruleTypeCombo = new QComboBox(this);
    m_ruleTypeCombo->addItems({"By Extension", "By Size", "By Date", "By Regex", "Round Robin", "Random"});
    ruleTypeLayout->addWidget(m_ruleTypeCombo);
    ruleTypeLayout->addStretch();

    m_addRuleBtn = new QPushButton("Add Rule", this);
    m_addRuleBtn->setToolTip("Add distribution rule for file routing");
    m_addRuleBtn->setObjectName("PanelSecondaryButton");
    m_removeRuleBtn = new QPushButton("Remove", this);
    m_removeRuleBtn->setToolTip("Remove selected rule");
    m_removeRuleBtn->setObjectName("PanelDangerButton");
    ruleTypeLayout->addWidget(m_addRuleBtn);
    ruleTypeLayout->addWidget(m_removeRuleBtn);

    rulesLayout->addLayout(ruleTypeLayout);

    // Rules table
    m_rulesTable = new QTableWidget(this);
    m_rulesTable->setColumnCount(3);
    m_rulesTable->setHorizontalHeaderLabels({"Pattern", "Destination", "Priority"});
    m_rulesTable->horizontalHeader()->setStretchLastSection(true);
    m_rulesTable->setMinimumHeight(80);
    m_rulesTable->setMaximumHeight(150);
    rulesLayout->addWidget(m_rulesTable);

    mainLayout->addWidget(rulesGroup);
}

void MultiUploaderPanel::setupTaskSection(QVBoxLayout* mainLayout)
{
    auto* taskGroup = new QGroupBox("Upload Tasks", this);
    auto* taskLayout = new QVBoxLayout(taskGroup);

    // Toolbar
    auto* toolbarLayout = new QHBoxLayout();
    m_startBtn = new QPushButton("Start", this);
    m_startBtn->setToolTip("Start uploading files to destinations");
    m_startBtn->setObjectName("PanelPrimaryButton");
    m_pauseBtn = new QPushButton("Pause All", this);
    m_pauseBtn->setToolTip("Pause all active uploads");
    m_pauseBtn->setObjectName("PanelSecondaryButton");
    m_cancelBtn = new QPushButton("Cancel All", this);
    m_cancelBtn->setToolTip("Cancel all uploads");
    m_cancelBtn->setObjectName("PanelDangerButton");
    m_clearCompletedBtn = new QPushButton("Clear Completed", this);
    m_clearCompletedBtn->setToolTip("Remove completed tasks from list");
    m_clearCompletedBtn->setObjectName("PanelSecondaryButton");

    connect(m_startBtn, &QPushButton::clicked, this, &MultiUploaderPanel::onStartClicked);
    connect(m_pauseBtn, &QPushButton::clicked, this, &MultiUploaderPanel::onPauseClicked);
    connect(m_cancelBtn, &QPushButton::clicked, this, &MultiUploaderPanel::onCancelClicked);

    toolbarLayout->addWidget(m_startBtn);
    toolbarLayout->addWidget(m_pauseBtn);
    toolbarLayout->addWidget(m_cancelBtn);
    toolbarLayout->addWidget(m_clearCompletedBtn);
    toolbarLayout->addStretch();

    taskLayout->addLayout(toolbarLayout);

    // Task table
    m_taskTable = new QTableWidget(this);
    m_taskTable->setColumnCount(5);
    m_taskTable->setHorizontalHeaderLabels({"ID", "Status", "Progress", "Speed", "ETA"});
    m_taskTable->horizontalHeader()->setStretchLastSection(true);
    taskLayout->addWidget(m_taskTable);

    mainLayout->addWidget(taskGroup);
}

void MultiUploaderPanel::updateButtonStates()
{
    bool hasFiles = m_sourceList && m_sourceList->count() > 0;
    bool hasDestinations = m_destinationList && m_destinationList->count() > 0;

    if (m_startBtn) m_startBtn->setEnabled(hasFiles && hasDestinations && !m_isUploading);
    if (m_pauseBtn) m_pauseBtn->setEnabled(m_isUploading);
    if (m_cancelBtn) m_cancelBtn->setEnabled(m_isUploading);
    if (m_clearFilesBtn) m_clearFilesBtn->setEnabled(hasFiles && !m_isUploading);
}

void MultiUploaderPanel::onAddFilesClicked()
{
    QStringList files = QFileDialog::getOpenFileNames(this, "Select Files to Upload");
    if (!files.isEmpty()) {
        m_sourceList->addItems(files);
        if (m_controller) {
            m_controller->addFiles(files);
        } else {
            m_sourceSummaryLabel->setText(QString("%1 files selected").arg(m_sourceList->count()));
            updateButtonStates();
        }
        emit addFilesRequested();
    }
}

void MultiUploaderPanel::onAddFolderClicked()
{
    QString folder = QFileDialog::getExistingDirectory(this, "Select Folder to Upload");
    if (!folder.isEmpty()) {
        m_sourceList->addItem(folder + " (folder)");
        if (m_controller) {
            m_controller->addFolder(folder, true);  // recursive
        } else {
            m_sourceSummaryLabel->setText(QString("%1 items selected").arg(m_sourceList->count()));
            updateButtonStates();
        }
        emit addFolderRequested();
    }
}

void MultiUploaderPanel::onClearFilesClicked()
{
    m_sourceList->clear();
    if (m_controller) {
        m_controller->clearFiles();
    } else {
        m_sourceSummaryLabel->setText("No files selected");
        updateButtonStates();
    }
    emit clearFilesRequested();
}

void MultiUploaderPanel::onAddDestinationClicked()
{
    if (!m_fileController) {
        QMessageBox::warning(this, "Not Connected",
            "Please log in to MEGA first to browse cloud folders.");
        return;
    }

    RemoteFolderBrowserDialog dialog(this);
    dialog.setFileController(m_fileController);
    dialog.setSelectionMode(RemoteFolderBrowserDialog::SingleFolder);
    dialog.setInitialPath("/");
    dialog.setTitle("Select Destination Folder");
    dialog.refresh();

    if (dialog.exec() == QDialog::Accepted) {
        QString remotePath = dialog.selectedPath();
        if (!remotePath.isEmpty()) {
            if (m_controller) {
                m_controller->addDestination(remotePath);
            } else {
                m_destinationList->addItem(remotePath);
                updateButtonStates();
            }
            emit addDestinationRequested(remotePath);
        }
    }
}

void MultiUploaderPanel::onRemoveDestinationClicked()
{
    auto* item = m_destinationList->currentItem();
    if (item) {
        QString remotePath = item->text();
        if (m_controller) {
            m_controller->removeDestination(remotePath);
        } else {
            delete m_destinationList->takeItem(m_destinationList->row(item));
            updateButtonStates();
        }
        emit removeDestinationRequested(remotePath);
    }
}

void MultiUploaderPanel::onStartClicked()
{
    if (m_controller) {
        m_controller->startUpload();
    }
    emit startUploadRequested();
}

void MultiUploaderPanel::onPauseClicked()
{
    if (m_controller) {
        if (m_controller->hasActiveUpload()) {
            m_controller->pauseUpload();
        } else {
            m_controller->resumeUpload();
        }
    }
    emit pauseUploadRequested();
}

void MultiUploaderPanel::onCancelClicked()
{
    if (m_controller) {
        m_controller->cancelUpload();
    }
    emit cancelUploadRequested();
}

} // namespace MegaCustom
