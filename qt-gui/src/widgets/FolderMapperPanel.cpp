#include "FolderMapperPanel.h"
#include "controllers/FileController.h"
#include "dialogs/RemoteFolderBrowserDialog.h"
#include "utils/PathUtils.h"
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QTimer>
#include <QDebug>
#include <QScrollArea>
#include <QFrame>

namespace MegaCustom {

FolderMapperPanel::FolderMapperPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    updateButtonStates();
}

FolderMapperPanel::~FolderMapperPanel()
{
}

void FolderMapperPanel::setController(FolderMapperController* controller)
{
    m_controller = controller;
    // Controller connections will be set up in MainWindow
}

void FolderMapperPanel::setFileController(FileController* controller)
{
    m_fileController = controller;
}

void FolderMapperPanel::setupUI()
{
    setObjectName("FolderMapperPanel");

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
    QLabel* titleLabel = new QLabel("Folder Mapper", contentWidget);
    titleLabel->setObjectName("PanelTitle");
    mainLayout->addWidget(titleLabel);

    QLabel* subtitleLabel = new QLabel("Map local folders to MEGA cloud destinations for automated uploads", contentWidget);
    subtitleLabel->setObjectName("PanelSubtitle");
    subtitleLabel->setWordWrap(true);
    mainLayout->addWidget(subtitleLabel);

    mainLayout->addSpacing(8);

    setupInputSection(mainLayout);
    setupToolbar(mainLayout);
    setupMappingTable(mainLayout);
    setupProgressSection(mainLayout);
    setupSettingsSection(mainLayout);

    mainLayout->addStretch();

    scrollArea->setWidget(contentWidget);
    outerLayout->addWidget(scrollArea);
}

void FolderMapperPanel::setupInputSection(QVBoxLayout* mainLayout)
{
    QGroupBox* inputGroup = new QGroupBox("Add/Edit Mapping", this);
    QGridLayout* inputLayout = new QGridLayout(inputGroup);

    // Name field
    inputLayout->addWidget(new QLabel("Name:"), 0, 0);
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText("Unique mapping name (e.g., 'documents')");
    inputLayout->addWidget(m_nameEdit, 0, 1, 1, 2);

    // Local path field
    inputLayout->addWidget(new QLabel("Local Path:"), 1, 0);
    m_localPathEdit = new QLineEdit(this);
    m_localPathEdit->setPlaceholderText("/path/to/local/folder");
    inputLayout->addWidget(m_localPathEdit, 1, 1);
    m_browseLocalBtn = new QPushButton("Browse...", this);
    m_browseLocalBtn->setToolTip("Browse for local folder");
    connect(m_browseLocalBtn, &QPushButton::clicked, this, &FolderMapperPanel::onBrowseLocalClicked);
    inputLayout->addWidget(m_browseLocalBtn, 1, 2);

    // Remote path field
    inputLayout->addWidget(new QLabel("Remote Path:"), 2, 0);
    m_remotePathEdit = new QLineEdit(this);
    m_remotePathEdit->setPlaceholderText("/CloudFolder (e.g., /Backup/Documents)");
    inputLayout->addWidget(m_remotePathEdit, 2, 1);
    m_browseRemoteBtn = new QPushButton("Browse...", this);
    m_browseRemoteBtn->setToolTip("Browse MEGA cloud folders");
    connect(m_browseRemoteBtn, &QPushButton::clicked, this, &FolderMapperPanel::onBrowseRemoteClicked);
    inputLayout->addWidget(m_browseRemoteBtn, 2, 2);

    // Action buttons row
    QHBoxLayout* actionLayout = new QHBoxLayout();

    m_addButton = new QPushButton("+ Add New", this);
    m_addButton->setToolTip("Add a new folder mapping");
    connect(m_addButton, &QPushButton::clicked, this, &FolderMapperPanel::onAddClicked);
    actionLayout->addWidget(m_addButton);

    m_updateButton = new QPushButton("Save Changes", this);
    m_updateButton->setToolTip("Update the selected mapping with new values");
    m_updateButton->setEnabled(false);
    m_updateButton->setVisible(false);
    connect(m_updateButton, &QPushButton::clicked, this, &FolderMapperPanel::onUpdateClicked);
    actionLayout->addWidget(m_updateButton);

    QPushButton* clearBtn = new QPushButton("Clear", this);
    clearBtn->setToolTip("Clear input fields");
    connect(clearBtn, &QPushButton::clicked, this, &FolderMapperPanel::onClearEditClicked);
    actionLayout->addWidget(clearBtn);

    actionLayout->addStretch();

    inputLayout->addLayout(actionLayout, 3, 0, 1, 3);

    inputGroup->setLayout(inputLayout);
    mainLayout->addWidget(inputGroup);
}

void FolderMapperPanel::setupToolbar(QVBoxLayout* mainLayout)
{
    QHBoxLayout* toolbarLayout = new QHBoxLayout();

    m_editButton = new QPushButton("Edit Selected", this);
    m_editButton->setToolTip("Edit selected mapping (loads into form above)");
    connect(m_editButton, &QPushButton::clicked, this, &FolderMapperPanel::onEditClicked);
    toolbarLayout->addWidget(m_editButton);

    m_removeButton = new QPushButton("- Remove", this);
    m_removeButton->setToolTip("Remove selected mapping");
    connect(m_removeButton, &QPushButton::clicked, this, &FolderMapperPanel::onRemoveClicked);
    toolbarLayout->addWidget(m_removeButton);

    toolbarLayout->addSpacing(20);

    m_previewButton = new QPushButton("Preview", this);
    m_previewButton->setToolTip("Preview what would be uploaded (dry run)");
    connect(m_previewButton, &QPushButton::clicked, this, &FolderMapperPanel::onPreviewClicked);
    toolbarLayout->addWidget(m_previewButton);

    m_uploadSelectedButton = new QPushButton("Upload Selected", this);
    m_uploadSelectedButton->setObjectName("PanelPrimaryButton");
    m_uploadSelectedButton->setToolTip("Upload selected mapping");
    connect(m_uploadSelectedButton, &QPushButton::clicked, this, &FolderMapperPanel::onUploadSelectedClicked);
    toolbarLayout->addWidget(m_uploadSelectedButton);

    m_uploadAllButton = new QPushButton("Upload All", this);
    m_uploadAllButton->setObjectName("PanelSecondaryButton");
    m_uploadAllButton->setToolTip("Upload all enabled mappings");
    connect(m_uploadAllButton, &QPushButton::clicked, this, &FolderMapperPanel::onUploadAllClicked);
    toolbarLayout->addWidget(m_uploadAllButton);

    m_cancelButton = new QPushButton("Cancel", this);
    m_cancelButton->setObjectName("PanelDangerButton");
    m_cancelButton->setToolTip("Cancel current upload");
    m_cancelButton->setEnabled(false);
    connect(m_cancelButton, &QPushButton::clicked, this, &FolderMapperPanel::onCancelClicked);
    toolbarLayout->addWidget(m_cancelButton);

    toolbarLayout->addStretch();

    m_refreshButton = new QPushButton("Refresh", this);
    m_refreshButton->setToolTip("Reload mappings from config");
    connect(m_refreshButton, &QPushButton::clicked, this, [this]() {
        emit refreshMappingsRequested();
    });
    toolbarLayout->addWidget(m_refreshButton);

    mainLayout->addLayout(toolbarLayout);
}

void FolderMapperPanel::setupMappingTable(QVBoxLayout* mainLayout)
{
    m_mappingTable = new QTableWidget(this);
    m_mappingTable->setColumnCount(COL_COUNT);
    m_mappingTable->setHorizontalHeaderLabels(
        {"Name", "Local Path", "Remote Path", "Status", "Enabled"});
    m_mappingTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_mappingTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_mappingTable->setAlternatingRowColors(true);
    m_mappingTable->horizontalHeader()->setStretchLastSection(false);
    m_mappingTable->horizontalHeader()->setSectionResizeMode(COL_NAME, QHeaderView::ResizeToContents);
    m_mappingTable->horizontalHeader()->setSectionResizeMode(COL_LOCAL_PATH, QHeaderView::Stretch);
    m_mappingTable->horizontalHeader()->setSectionResizeMode(COL_REMOTE_PATH, QHeaderView::Stretch);
    m_mappingTable->horizontalHeader()->setSectionResizeMode(COL_STATUS, QHeaderView::ResizeToContents);
    m_mappingTable->horizontalHeader()->setSectionResizeMode(COL_ENABLED, QHeaderView::ResizeToContents);
    m_mappingTable->verticalHeader()->setVisible(false);

    connect(m_mappingTable, &QTableWidget::itemSelectionChanged,
            this, &FolderMapperPanel::onMappingSelectionChanged);
    connect(m_mappingTable, &QTableWidget::cellDoubleClicked,
            this, &FolderMapperPanel::onMappingDoubleClicked);

    mainLayout->addWidget(m_mappingTable, 1); // stretch factor 1
}

void FolderMapperPanel::setupProgressSection(QVBoxLayout* mainLayout)
{
    m_progressGroup = new QGroupBox("Progress", this);
    m_progressGroup->setVisible(false);
    QVBoxLayout* progressLayout = new QVBoxLayout(m_progressGroup);

    m_currentFileLabel = new QLabel("Ready", this);
    m_currentFileLabel->setWordWrap(true);
    progressLayout->addWidget(m_currentFileLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setMinimum(0);
    m_progressBar->setMaximum(100);
    m_progressBar->setValue(0);
    progressLayout->addWidget(m_progressBar);

    m_statsLabel = new QLabel("Files: 0/0 | Uploaded: 0 B", this);
    progressLayout->addWidget(m_statsLabel);

    m_progressGroup->setLayout(progressLayout);
    mainLayout->addWidget(m_progressGroup);
}

void FolderMapperPanel::setupSettingsSection(QVBoxLayout* mainLayout)
{
    QGroupBox* settingsGroup = new QGroupBox("Upload Options", this);
    QHBoxLayout* settingsLayout = new QHBoxLayout(settingsGroup);

    m_incrementalCheckbox = new QCheckBox("Incremental (only new/changed)", this);
    m_incrementalCheckbox->setChecked(true);
    m_incrementalCheckbox->setToolTip("Only upload files that are new or have changed");
    settingsLayout->addWidget(m_incrementalCheckbox);

    m_recursiveCheckbox = new QCheckBox("Recursive", this);
    m_recursiveCheckbox->setChecked(true);
    m_recursiveCheckbox->setToolTip("Include subdirectories");
    settingsLayout->addWidget(m_recursiveCheckbox);

    settingsLayout->addSpacing(20);

    settingsLayout->addWidget(new QLabel("Concurrent:"));
    m_concurrentSpinBox = new QSpinBox(this);
    m_concurrentSpinBox->setRange(1, 8);
    m_concurrentSpinBox->setValue(4);
    m_concurrentSpinBox->setToolTip("Number of simultaneous uploads");
    settingsLayout->addWidget(m_concurrentSpinBox);

    settingsLayout->addStretch();

    settingsLayout->addWidget(new QLabel("Exclude:"));
    m_excludePatternsEdit = new QLineEdit(this);
    m_excludePatternsEdit->setPlaceholderText("*.tmp, *.log, .git/*");
    m_excludePatternsEdit->setToolTip("Comma-separated patterns to exclude");
    m_excludePatternsEdit->setMinimumWidth(200);
    settingsLayout->addWidget(m_excludePatternsEdit);

    settingsGroup->setLayout(settingsLayout);
    mainLayout->addWidget(settingsGroup);
}

void FolderMapperPanel::updateButtonStates()
{
    bool hasSelection = m_mappingTable && m_mappingTable->currentRow() >= 0;
    bool hasMappings = m_mappingTable && m_mappingTable->rowCount() > 0;

    m_removeButton->setEnabled(hasSelection && !m_isUploading);
    m_editButton->setEnabled(hasSelection && !m_isUploading);
    m_previewButton->setEnabled(hasSelection && !m_isUploading);
    m_uploadSelectedButton->setEnabled(hasSelection && !m_isUploading);
    m_uploadAllButton->setEnabled(hasMappings && !m_isUploading);
    m_cancelButton->setEnabled(m_isUploading);
    m_addButton->setEnabled(!m_isUploading);
    m_refreshButton->setEnabled(!m_isUploading);
}

void FolderMapperPanel::clearInputFields()
{
    m_nameEdit->clear();
    m_localPathEdit->clear();
    m_remotePathEdit->clear();
}

int FolderMapperPanel::findRowByName(const QString& name)
{
    for (int i = 0; i < m_mappingTable->rowCount(); ++i) {
        QTableWidgetItem* item = m_mappingTable->item(i, COL_NAME);
        if (item && item->text() == name) {
            return i;
        }
    }
    return -1;
}

QString FolderMapperPanel::formatSize(qint64 bytes)
{
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024 * 1024 * 1024) return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

QString FolderMapperPanel::formatSpeed(double bytesPerSec)
{
    if (bytesPerSec < 1024) return QString("%1 B/s").arg(bytesPerSec, 0, 'f', 0);
    if (bytesPerSec < 1024 * 1024) return QString("%1 KB/s").arg(bytesPerSec / 1024.0, 0, 'f', 1);
    return QString("%1 MB/s").arg(bytesPerSec / (1024.0 * 1024.0), 0, 'f', 1);
}

// Slots for user actions
void FolderMapperPanel::onAddClicked()
{
    QString name = m_nameEdit->text().trimmed();
    QString localPath = PathUtils::normalizeLocalPath(m_localPathEdit->text());
    QString remotePath = PathUtils::normalizeRemotePath(m_remotePathEdit->text());

    if (name.isEmpty() || localPath.isEmpty() || PathUtils::isPathEmpty(m_remotePathEdit->text())) {
        QMessageBox::warning(this, "Incomplete Input",
            "Please fill in all fields: Name, Local Path, and Remote Path.");
        return;
    }

    // Check if name already exists
    if (findRowByName(name) >= 0) {
        QMessageBox::warning(this, "Duplicate Name",
            QString("A mapping with the name '%1' already exists.").arg(name));
        return;
    }

    emit addMappingRequested(name, localPath, remotePath);
    clearInputFields();
}

void FolderMapperPanel::onRemoveClicked()
{
    int row = m_mappingTable->currentRow();
    if (row < 0) return;

    QTableWidgetItem* nameItem = m_mappingTable->item(row, COL_NAME);
    if (!nameItem) return;

    QString name = nameItem->text();
    int ret = QMessageBox::question(this, "Confirm Removal",
        QString("Remove mapping '%1'?").arg(name),
        QMessageBox::Yes | QMessageBox::No);

    if (ret == QMessageBox::Yes) {
        emit removeMappingRequested(name);
    }
}

void FolderMapperPanel::onEditClicked()
{
    int row = m_mappingTable->currentRow();
    if (row < 0) return;

    // Enter edit mode - populate input fields with selected row data
    QString name = m_mappingTable->item(row, COL_NAME)->text();
    m_editingMappingName = name;

    m_nameEdit->setText(name);
    m_nameEdit->setReadOnly(true);  // Can't change the name while editing
    m_localPathEdit->setText(m_mappingTable->item(row, COL_LOCAL_PATH)->text());
    m_remotePathEdit->setText(m_mappingTable->item(row, COL_REMOTE_PATH)->text());

    // Show update button, hide add button
    m_addButton->setVisible(false);
    m_updateButton->setVisible(true);
    m_updateButton->setEnabled(true);
}

void FolderMapperPanel::onUpdateClicked()
{
    if (m_editingMappingName.isEmpty()) return;

    QString localPath = PathUtils::normalizeLocalPath(m_localPathEdit->text());
    QString remotePath = PathUtils::normalizeRemotePath(m_remotePathEdit->text());

    if (localPath.isEmpty() || PathUtils::isPathEmpty(m_remotePathEdit->text())) {
        QMessageBox::warning(this, "Incomplete Input",
            "Please fill in both Local Path and Remote Path.");
        return;
    }

    emit editMappingRequested(m_editingMappingName, localPath, remotePath);

    // Exit edit mode
    onClearEditClicked();
}

void FolderMapperPanel::onClearEditClicked()
{
    // Clear input fields and exit edit mode
    m_editingMappingName.clear();
    m_nameEdit->clear();
    m_nameEdit->setReadOnly(false);
    m_localPathEdit->clear();
    m_remotePathEdit->clear();

    // Show add button, hide update button
    m_addButton->setVisible(true);
    m_updateButton->setVisible(false);
    m_updateButton->setEnabled(false);
}

void FolderMapperPanel::onUploadSelectedClicked()
{
    int row = m_mappingTable->currentRow();
    if (row < 0) return;

    QTableWidgetItem* nameItem = m_mappingTable->item(row, COL_NAME);
    if (!nameItem) return;

    QString name = nameItem->text();
    bool incremental = m_incrementalCheckbox->isChecked();

    emit uploadMappingRequested(name, false, incremental);
}

void FolderMapperPanel::onUploadAllClicked()
{
    if (m_mappingTable->rowCount() == 0) {
        QMessageBox::information(this, "No Mappings", "No folder mappings defined.");
        return;
    }

    bool incremental = m_incrementalCheckbox->isChecked();
    emit uploadAllRequested(false, incremental);
}

void FolderMapperPanel::onPreviewClicked()
{
    int row = m_mappingTable->currentRow();
    if (row < 0) return;

    QTableWidgetItem* nameItem = m_mappingTable->item(row, COL_NAME);
    if (!nameItem) return;

    emit previewUploadRequested(nameItem->text());
}

void FolderMapperPanel::onCancelClicked()
{
    emit cancelUploadRequested();
}

void FolderMapperPanel::onMappingSelectionChanged()
{
    updateButtonStates();
}

void FolderMapperPanel::onMappingDoubleClicked(int row, int column)
{
    Q_UNUSED(column);
    if (row >= 0) {
        onEditClicked();
    }
}

void FolderMapperPanel::onEnabledCheckboxChanged(int state)
{
    QCheckBox* checkbox = qobject_cast<QCheckBox*>(sender());
    if (!checkbox) return;

    // Find the row for this checkbox
    for (int row = 0; row < m_mappingTable->rowCount(); ++row) {
        QWidget* widget = m_mappingTable->cellWidget(row, COL_ENABLED);
        if (widget) {
            QCheckBox* cb = widget->findChild<QCheckBox*>();
            if (cb == checkbox) {
                QTableWidgetItem* nameItem = m_mappingTable->item(row, COL_NAME);
                if (nameItem) {
                    emit toggleMappingEnabled(nameItem->text(), state == Qt::Checked);
                }
                break;
            }
        }
    }
}

void FolderMapperPanel::onBrowseLocalClicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Select Local Folder",
        m_localPathEdit->text().isEmpty() ? QDir::homePath() : m_localPathEdit->text());
    if (!dir.isEmpty()) {
        m_localPathEdit->setText(dir);
    }
}

void FolderMapperPanel::onBrowseRemoteClicked()
{
    if (!m_fileController) {
        QMessageBox::warning(this, "Not Connected",
            "Please log in to MEGA first to browse cloud folders.");
        return;
    }

    QString currentPath = m_remotePathEdit->text();
    if (currentPath.isEmpty()) {
        currentPath = "/";
    }

    RemoteFolderBrowserDialog dialog(this);
    dialog.setFileController(m_fileController);
    dialog.setSelectionMode(RemoteFolderBrowserDialog::SingleFolder);
    dialog.setInitialPath(currentPath);
    dialog.setTitle("Select Remote Folder");
    dialog.refresh();

    if (dialog.exec() == QDialog::Accepted) {
        QString path = dialog.selectedPath();
        if (!path.isEmpty()) {
            m_remotePathEdit->setText(path);
        }
    }
}

// Slots for controller signals
void FolderMapperPanel::onMappingsLoaded(int count)
{
    qDebug() << "FolderMapperPanel: Loaded" << count << "mappings";
    // Clear the table BEFORE this signal - mappings were already added via mappingAdded signals
    // So we actually need to clear BEFORE loadMappings is called
    // This signal comes AFTER all mappingAdded signals, so just update state
    updateButtonStates();
}

void FolderMapperPanel::clearMappingsTable()
{
    // Clear all rows from the table
    while (m_mappingTable->rowCount() > 0) {
        m_mappingTable->removeRow(0);
    }
}

void FolderMapperPanel::onMappingAdded(const QString& name, const QString& localPath,
                                        const QString& remotePath, bool enabled)
{
    int row = m_mappingTable->rowCount();
    m_mappingTable->insertRow(row);

    m_mappingTable->setItem(row, COL_NAME, new QTableWidgetItem(name));
    m_mappingTable->setItem(row, COL_LOCAL_PATH, new QTableWidgetItem(localPath));
    m_mappingTable->setItem(row, COL_REMOTE_PATH, new QTableWidgetItem(remotePath));
    m_mappingTable->setItem(row, COL_STATUS, new QTableWidgetItem("Ready"));

    // Enabled checkbox
    QWidget* checkboxWidget = new QWidget();
    QHBoxLayout* checkboxLayout = new QHBoxLayout(checkboxWidget);
    checkboxLayout->setContentsMargins(0, 0, 0, 0);
    checkboxLayout->setAlignment(Qt::AlignCenter);
    QCheckBox* checkbox = new QCheckBox();
    checkbox->setChecked(enabled);
    connect(checkbox, &QCheckBox::stateChanged, this, &FolderMapperPanel::onEnabledCheckboxChanged);
    checkboxLayout->addWidget(checkbox);
    m_mappingTable->setCellWidget(row, COL_ENABLED, checkboxWidget);

    updateButtonStates();
}

void FolderMapperPanel::onMappingRemoved(const QString& name)
{
    int row = findRowByName(name);
    if (row >= 0) {
        m_mappingTable->removeRow(row);
    }
    updateButtonStates();
}

void FolderMapperPanel::onMappingUpdated(const QString& name)
{
    // Will be called when a mapping is updated
    // For now, just update the status
    int row = findRowByName(name);
    if (row >= 0) {
        m_mappingTable->item(row, COL_STATUS)->setText("Updated");
    }
}

void FolderMapperPanel::onUploadStarted(const QString& mappingName)
{
    m_isUploading = true;
    m_currentMappingName = mappingName;
    m_progressGroup->setVisible(true);
    m_currentFileLabel->setText(QString("Starting upload for '%1'...").arg(mappingName));
    m_progressBar->setValue(0);
    m_statsLabel->setText("Files: 0/0 | Uploaded: 0 B");

    // Update status in table
    int row = findRowByName(mappingName);
    if (row >= 0) {
        m_mappingTable->item(row, COL_STATUS)->setText("Uploading...");
        m_mappingTable->item(row, COL_STATUS)->setForeground(Qt::blue);
    }

    updateButtonStates();
}

void FolderMapperPanel::onUploadProgress(const QString& mappingName, const QString& currentFile,
                                          int filesCompleted, int totalFiles,
                                          qint64 bytesUploaded, qint64 totalBytes,
                                          double speedBytesPerSec)
{
    m_currentFileLabel->setText(QString("Uploading: %1").arg(currentFile));

    int percent = (totalFiles > 0) ? (filesCompleted * 100 / totalFiles) : 0;
    m_progressBar->setValue(percent);

    m_statsLabel->setText(QString("Files: %1/%2 | Uploaded: %3 / %4 | Speed: %5")
        .arg(filesCompleted)
        .arg(totalFiles)
        .arg(formatSize(bytesUploaded))
        .arg(formatSize(totalBytes))
        .arg(formatSpeed(speedBytesPerSec)));

    // Update status in table
    int row = findRowByName(mappingName);
    if (row >= 0) {
        m_mappingTable->item(row, COL_STATUS)->setText(
            QString("%1% (%2/%3)").arg(percent).arg(filesCompleted).arg(totalFiles));
    }
}

void FolderMapperPanel::onUploadComplete(const QString& mappingName, bool success,
                                          int filesUploaded, int filesSkipped, int filesFailed)
{
    m_isUploading = false;
    m_currentMappingName.clear();

    QString statusText;
    QColor statusColor;

    if (success) {
        statusText = QString("Done (%1 uploaded, %2 skipped)")
            .arg(filesUploaded).arg(filesSkipped);
        statusColor = Qt::darkGreen;
        m_currentFileLabel->setText(QString("Upload complete for '%1'").arg(mappingName));
    } else {
        statusText = QString("Failed (%1 errors)").arg(filesFailed);
        statusColor = Qt::red;
        m_currentFileLabel->setText(QString("Upload failed for '%1'").arg(mappingName));
    }

    m_progressBar->setValue(success ? 100 : 0);

    // Update status in table
    int row = findRowByName(mappingName);
    if (row >= 0) {
        m_mappingTable->item(row, COL_STATUS)->setText(statusText);
        m_mappingTable->item(row, COL_STATUS)->setForeground(statusColor);
    }

    // Hide progress after a delay
    QTimer::singleShot(5000, this, [this]() {
        if (!m_isUploading) {
            m_progressGroup->setVisible(false);
        }
    });

    updateButtonStates();
}

void FolderMapperPanel::onPreviewReady(const QString& mappingName, int filesToUpload,
                                        int filesToSkip, qint64 totalBytes)
{
    QString message = QString("Preview for '%1':\n\n"
                              "Files to upload: %2\n"
                              "Files to skip: %3\n"
                              "Total size: %4")
        .arg(mappingName)
        .arg(filesToUpload)
        .arg(filesToSkip)
        .arg(formatSize(totalBytes));

    QMessageBox::information(this, "Upload Preview", message);
}

void FolderMapperPanel::onError(const QString& operation, const QString& message)
{
    QMessageBox::critical(this, QString("Error: %1").arg(operation), message);

    if (m_isUploading) {
        m_isUploading = false;
        m_currentFileLabel->setText(QString("Error: %1").arg(message));
        m_progressGroup->setVisible(true);
        updateButtonStates();
    }
}

} // namespace MegaCustom
