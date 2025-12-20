#include "widgets/CloudCopierPanel.h"
#include "controllers/CloudCopierController.h"
#include "controllers/FileController.h"
#include "dialogs/RemoteFolderBrowserDialog.h"
#include "dialogs/BulkPathEditorDialog.h"
#include "utils/PathUtils.h"
#include <QScrollArea>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QTextEdit>
#include <QDialog>
#include <QApplication>
#include <QShortcut>
#include <QTime>

namespace MegaCustom {

CloudCopierPanel::CloudCopierPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    updateButtonStates();
}

CloudCopierPanel::~CloudCopierPanel() = default;

void CloudCopierPanel::setFileController(FileController* fileController) {
    m_fileController = fileController;
}

void CloudCopierPanel::setController(CloudCopierController* controller) {
    if (m_controller) {
        disconnect(m_controller, nullptr, this, nullptr);
    }

    m_controller = controller;

    if (m_controller) {
        // Connect controller signals to panel slots
        connect(m_controller, &CloudCopierController::sourcesChanged,
                this, &CloudCopierPanel::onSourcesChanged);
        connect(m_controller, &CloudCopierController::destinationsChanged,
                this, &CloudCopierPanel::onDestinationsChanged);
        connect(m_controller, &CloudCopierController::templatesChanged,
                this, &CloudCopierPanel::onTemplatesChanged);
        connect(m_controller, &CloudCopierController::tasksClearing,
                this, &CloudCopierPanel::onTasksClearing);
        connect(m_controller, &CloudCopierController::taskCreated,
                this, &CloudCopierPanel::onTaskCreated);
        connect(m_controller, &CloudCopierController::taskProgress,
                this, &CloudCopierPanel::onTaskProgress);
        connect(m_controller, &CloudCopierController::taskStatusChanged,
                this, &CloudCopierPanel::onTaskStatusChanged);
        connect(m_controller, &CloudCopierController::copyStarted,
                this, &CloudCopierPanel::onCopyStarted);
        connect(m_controller, &CloudCopierController::copyProgress,
                this, &CloudCopierPanel::onCopyProgress);
        connect(m_controller, &CloudCopierController::copyCompleted,
                this, &CloudCopierPanel::onCopyCompleted);
        connect(m_controller, &CloudCopierController::copyPaused,
                this, &CloudCopierPanel::onCopyPaused);
        connect(m_controller, &CloudCopierController::copyCancelled,
                this, &CloudCopierPanel::onCopyCancelled);
        connect(m_controller, &CloudCopierController::error,
                this, &CloudCopierPanel::onError);
        connect(m_controller, &CloudCopierController::previewReady,
                this, &CloudCopierPanel::onPreviewReady);

        // Member mode signals
        connect(m_controller, &CloudCopierController::memberModeChanged,
                this, &CloudCopierPanel::onMemberModeChanged);
        connect(m_controller, &CloudCopierController::availableMembersChanged,
                this, &CloudCopierPanel::onAvailableMembersChanged);
        connect(m_controller, &CloudCopierController::selectedMemberChanged,
                this, &CloudCopierPanel::onSelectedMemberChanged);
        connect(m_controller, &CloudCopierController::allMembersSelectionChanged,
                this, &CloudCopierPanel::onAllMembersSelectionChanged);
        connect(m_controller, &CloudCopierController::destinationTemplateChanged,
                this, &CloudCopierPanel::onDestinationTemplateChanged);
        connect(m_controller, &CloudCopierController::templateExpansionReady,
                this, &CloudCopierPanel::onTemplateExpansionReady);
        connect(m_controller, &CloudCopierController::memberTaskCreated,
                this, &CloudCopierPanel::onMemberTaskCreated);

        // Load templates
        updateTemplateCombo();
    }
}

void CloudCopierPanel::setupUI() {
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    QWidget* contentWidget = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(contentWidget);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    // Title
    QLabel* titleLabel = new QLabel("CLOUD COPIER", this);
    titleLabel->setObjectName("PanelTitle");
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #333;");
    mainLayout->addWidget(titleLabel);

    QLabel* subtitleLabel = new QLabel(
        "Copy files and folders within MEGA to multiple destinations", this);
    subtitleLabel->setObjectName("PanelSubtitle");
    subtitleLabel->setStyleSheet("color: #666; margin-bottom: 8px;");
    subtitleLabel->setWordWrap(true);
    mainLayout->addWidget(subtitleLabel);

    mainLayout->addSpacing(8);

    setupSourceSection(mainLayout);
    setupDestinationSection(mainLayout);
    setupMemberSection(mainLayout);
    setupTemplateSection(mainLayout);
    setupTaskTable(mainLayout);
    setupProgressSection(mainLayout);
    setupControlButtons(mainLayout);
    setupErrorLogSection(mainLayout);

    mainLayout->addStretch();

    scrollArea->setWidget(contentWidget);

    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);

    setupShortcuts();
}

void CloudCopierPanel::setupShortcuts() {
    // Ctrl+V - Paste paths to focused list
    QShortcut* pasteShortcut = new QShortcut(QKeySequence::Paste, this);
    connect(pasteShortcut, &QShortcut::activated, this, &CloudCopierPanel::onPasteShortcut);

    // Delete - Remove selected items from focused list
    QShortcut* deleteShortcut = new QShortcut(QKeySequence::Delete, this);
    connect(deleteShortcut, &QShortcut::activated, this, &CloudCopierPanel::onDeleteShortcut);

    // Ctrl+A - Select all in focused list
    QShortcut* selectAllShortcut = new QShortcut(QKeySequence::SelectAll, this);
    connect(selectAllShortcut, &QShortcut::activated, this, &CloudCopierPanel::onSelectAllShortcut);

    // Ctrl+Enter - Start copy
    QShortcut* startShortcut = new QShortcut(QKeySequence("Ctrl+Return"), this);
    connect(startShortcut, &QShortcut::activated, this, &CloudCopierPanel::onStartCopyClicked);

    // Escape - Cancel operation
    QShortcut* cancelShortcut = new QShortcut(QKeySequence("Escape"), this);
    connect(cancelShortcut, &QShortcut::activated, [this]() {
        if (m_isCopying) {
            onCancelCopyClicked();
        }
    });

    // F5 - Validate destinations
    QShortcut* validateShortcut = new QShortcut(QKeySequence("F5"), this);
    connect(validateShortcut, &QShortcut::activated, this, &CloudCopierPanel::onValidateDestinationsClicked);
}

void CloudCopierPanel::onPasteShortcut() {
    if (m_isCopying) return;

    // Determine which list has focus
    QWidget* focused = QApplication::focusWidget();

    if (focused == m_sourceList || m_sourceList->hasFocus()) {
        onPasteSourcesClicked();
    } else if (focused == m_destinationList || m_destinationList->hasFocus()) {
        onPasteDestinationsClicked();
    } else {
        // Default to sources if no list is focused
        onPasteSourcesClicked();
    }
}

void CloudCopierPanel::onDeleteShortcut() {
    if (m_isCopying) return;

    QWidget* focused = QApplication::focusWidget();

    if (focused == m_sourceList || m_sourceList->hasFocus()) {
        if (!m_sourceList->selectedItems().isEmpty()) {
            onRemoveSourceClicked();
        }
    } else if (focused == m_destinationList || m_destinationList->hasFocus()) {
        if (!m_destinationList->selectedItems().isEmpty()) {
            onRemoveDestinationClicked();
        }
    }
}

void CloudCopierPanel::onSelectAllShortcut() {
    QWidget* focused = QApplication::focusWidget();

    if (focused == m_sourceList || m_sourceList->hasFocus()) {
        m_sourceList->selectAll();
    } else if (focused == m_destinationList || m_destinationList->hasFocus()) {
        m_destinationList->selectAll();
    }
}

void CloudCopierPanel::setupSourceSection(QVBoxLayout* mainLayout) {
    QGroupBox* sourceGroup = new QGroupBox("SOURCE", this);
    sourceGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; border: 1px solid #E0E0E0; "
        "border-radius: 6px; margin-top: 12px; padding-top: 16px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; }");

    QVBoxLayout* sourceLayout = new QVBoxLayout(sourceGroup);

    // Source list
    m_sourceList = new QListWidget(this);
    m_sourceList->setMaximumHeight(120);
    m_sourceList->setAlternatingRowColors(true);
    m_sourceList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_sourceList, &QListWidget::itemSelectionChanged,
            this, &CloudCopierPanel::onSourceSelectionChanged);
    sourceLayout->addWidget(m_sourceList);

    // Source summary
    m_sourceSummaryLabel = new QLabel("0 items selected", this);
    m_sourceSummaryLabel->setStyleSheet("color: #666;");
    sourceLayout->addWidget(m_sourceSummaryLabel);

    // Source buttons
    QHBoxLayout* srcBtnLayout = new QHBoxLayout();

    m_addSourceBtn = new QPushButton("+ Add", this);
    m_addSourceBtn->setToolTip("Add source files/folders from MEGA cloud");
    m_addSourceBtn->setObjectName("PanelSecondaryButton");
    connect(m_addSourceBtn, &QPushButton::clicked, this, &CloudCopierPanel::onAddSourceClicked);
    srcBtnLayout->addWidget(m_addSourceBtn);

    m_pasteSourcesBtn = new QPushButton("Paste Multiple", this);
    m_pasteSourcesBtn->setToolTip("Paste multiple source paths (one per line)");
    m_pasteSourcesBtn->setObjectName("PanelSecondaryButton");
    connect(m_pasteSourcesBtn, &QPushButton::clicked, this, &CloudCopierPanel::onPasteSourcesClicked);
    srcBtnLayout->addWidget(m_pasteSourcesBtn);

    m_editSourcesBtn = new QPushButton("Edit All", this);
    m_editSourcesBtn->setToolTip("Smart bulk edit - change common path segments while keeping unique parts");
    m_editSourcesBtn->setStyleSheet(
        "QPushButton { background-color: #FF9800; color: white; "
        "border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold; } "
        "QPushButton:hover { background-color: #F57C00; } "
        "QPushButton:disabled { background-color: #AAAAAA; }");
    connect(m_editSourcesBtn, &QPushButton::clicked, this, &CloudCopierPanel::onEditSourcesClicked);
    srcBtnLayout->addWidget(m_editSourcesBtn);

    m_removeSourceBtn = new QPushButton("Remove", this);
    m_removeSourceBtn->setToolTip("Remove selected source");
    m_removeSourceBtn->setEnabled(false);
    connect(m_removeSourceBtn, &QPushButton::clicked, this, &CloudCopierPanel::onRemoveSourceClicked);
    srcBtnLayout->addWidget(m_removeSourceBtn);

    m_clearSourcesBtn = new QPushButton("Clear All", this);
    m_clearSourcesBtn->setToolTip("Remove all sources from list");
    m_clearSourcesBtn->setObjectName("PanelDangerButton");
    connect(m_clearSourcesBtn, &QPushButton::clicked, this, &CloudCopierPanel::onClearSourcesClicked);
    srcBtnLayout->addWidget(m_clearSourcesBtn);

    srcBtnLayout->addStretch();
    sourceLayout->addLayout(srcBtnLayout);

    mainLayout->addWidget(sourceGroup);
}

void CloudCopierPanel::setupDestinationSection(QVBoxLayout* mainLayout) {
    QGroupBox* destGroup = new QGroupBox("DESTINATIONS", this);
    destGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; border: 1px solid #E0E0E0; "
        "border-radius: 6px; margin-top: 12px; padding-top: 16px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; }");

    QVBoxLayout* destLayout = new QVBoxLayout(destGroup);

    // Destination list
    m_destinationList = new QListWidget(this);
    m_destinationList->setMaximumHeight(150);
    m_destinationList->setAlternatingRowColors(true);
    m_destinationList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_destinationList, &QListWidget::itemSelectionChanged,
            this, &CloudCopierPanel::onDestinationSelectionChanged);
    destLayout->addWidget(m_destinationList);

    // Destination summary
    m_destSummaryLabel = new QLabel("0 destinations", this);
    m_destSummaryLabel->setStyleSheet("color: #666;");
    destLayout->addWidget(m_destSummaryLabel);

    // Destination buttons
    QHBoxLayout* destBtnLayout = new QHBoxLayout();

    m_addDestBtn = new QPushButton("+ Add", this);
    m_addDestBtn->setToolTip("Add destination folder in MEGA cloud");
    m_addDestBtn->setObjectName("PanelSecondaryButton");
    connect(m_addDestBtn, &QPushButton::clicked, this, &CloudCopierPanel::onAddDestinationClicked);
    destBtnLayout->addWidget(m_addDestBtn);

    m_pasteDestsBtn = new QPushButton("Paste Multiple", this);
    m_pasteDestsBtn->setToolTip("Paste multiple destination paths (one per line)");
    m_pasteDestsBtn->setObjectName("PanelSecondaryButton");
    connect(m_pasteDestsBtn, &QPushButton::clicked, this, &CloudCopierPanel::onPasteDestinationsClicked);
    destBtnLayout->addWidget(m_pasteDestsBtn);

    m_editDestsBtn = new QPushButton("Edit All", this);
    m_editDestsBtn->setToolTip("Smart bulk edit - change common path segments (e.g., month, year) while keeping unique parts");
    m_editDestsBtn->setStyleSheet(
        "QPushButton { background-color: #FF9800; color: white; "
        "border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold; } "
        "QPushButton:hover { background-color: #F57C00; } "
        "QPushButton:disabled { background-color: #AAAAAA; }");
    connect(m_editDestsBtn, &QPushButton::clicked, this, &CloudCopierPanel::onEditDestinationsClicked);
    destBtnLayout->addWidget(m_editDestsBtn);

    m_removeDestBtn = new QPushButton("Remove", this);
    m_removeDestBtn->setToolTip("Remove selected destination");
    m_removeDestBtn->setEnabled(false);
    connect(m_removeDestBtn, &QPushButton::clicked, this, &CloudCopierPanel::onRemoveDestinationClicked);
    destBtnLayout->addWidget(m_removeDestBtn);

    m_clearDestsBtn = new QPushButton("Clear All", this);
    m_clearDestsBtn->setToolTip("Remove all destinations from list");
    m_clearDestsBtn->setObjectName("PanelDangerButton");
    connect(m_clearDestsBtn, &QPushButton::clicked, this, &CloudCopierPanel::onClearDestinationsClicked);
    destBtnLayout->addWidget(m_clearDestsBtn);

    m_validateDestsBtn = new QPushButton("Validate", this);
    m_validateDestsBtn->setToolTip("Check which destinations exist in MEGA cloud");
    m_validateDestsBtn->setStyleSheet(
        "QPushButton { background-color: #6A5ACD; color: white; "
        "border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold; } "
        "QPushButton:hover { background-color: #5A4ABD; } "
        "QPushButton:disabled { background-color: #AAAAAA; }");
    connect(m_validateDestsBtn, &QPushButton::clicked, this, &CloudCopierPanel::onValidateDestinationsClicked);
    destBtnLayout->addWidget(m_validateDestsBtn);

    destBtnLayout->addStretch();
    destLayout->addLayout(destBtnLayout);

    mainLayout->addWidget(destGroup);
}

void CloudCopierPanel::setupMemberSection(QVBoxLayout* mainLayout) {
    m_memberGroup = new QGroupBox("MEMBER MODE", this);
    m_memberGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; border: 1px solid #E0E0E0; "
        "border-radius: 6px; margin-top: 12px; padding-top: 16px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; }");

    QVBoxLayout* memberLayout = new QVBoxLayout(m_memberGroup);

    // Mode selection row - radio buttons to switch between manual/member destinations
    QHBoxLayout* modeRow = new QHBoxLayout();
    m_destModeGroup = new QButtonGroup(this);

    m_manualDestRadio = new QRadioButton("Manual destinations", this);
    m_manualDestRadio->setToolTip("Use the destinations list above");
    m_manualDestRadio->setChecked(true);
    m_destModeGroup->addButton(m_manualDestRadio, 0);
    modeRow->addWidget(m_manualDestRadio);

    m_memberDestRadio = new QRadioButton("Copy to members", this);
    m_memberDestRadio->setToolTip("Copy to member distribution folders using template");
    m_destModeGroup->addButton(m_memberDestRadio, 1);
    modeRow->addWidget(m_memberDestRadio);

    modeRow->addStretch();
    memberLayout->addLayout(modeRow);

    // Member selection container (shown when member mode is active)
    m_memberSelectionWidget = new QWidget(this);
    QVBoxLayout* memberSelLayout = new QVBoxLayout(m_memberSelectionWidget);
    memberSelLayout->setContentsMargins(0, 8, 0, 0);

    // Member dropdown row
    QHBoxLayout* memberRow = new QHBoxLayout();
    QLabel* memberLabel = new QLabel("Member:", this);
    memberRow->addWidget(memberLabel);

    m_memberCombo = new QComboBox(this);
    m_memberCombo->setMinimumWidth(200);
    m_memberCombo->addItem("-- Select Member --");
    connect(m_memberCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CloudCopierPanel::onMemberComboChanged);
    memberRow->addWidget(m_memberCombo);

    m_allMembersCheck = new QCheckBox("Copy to ALL members", this);
    m_allMembersCheck->setToolTip("Copy to all members with distribution folders");
    connect(m_allMembersCheck, &QCheckBox::toggled,
            this, &CloudCopierPanel::onAllMembersCheckChanged);
    memberRow->addWidget(m_allMembersCheck);

    m_memberCountLabel = new QLabel("(0 available)", this);
    m_memberCountLabel->setStyleSheet("color: #666;");
    memberRow->addWidget(m_memberCountLabel);

    memberRow->addStretch();
    memberSelLayout->addLayout(memberRow);

    // Template path row
    QHBoxLayout* templateRow = new QHBoxLayout();
    QLabel* pathLabel = new QLabel("Path template:", this);
    templateRow->addWidget(pathLabel);

    m_templatePathEdit = new QLineEdit(this);
    m_templatePathEdit->setPlaceholderText("e.g., /Archive/{member}/Content/{month}/");
    m_templatePathEdit->setMinimumWidth(350);
    connect(m_templatePathEdit, &QLineEdit::textChanged,
            this, &CloudCopierPanel::onTemplatePathChanged);
    templateRow->addWidget(m_templatePathEdit);

    m_variableHelpBtn = new QPushButton("?", this);
    m_variableHelpBtn->setFixedSize(24, 24);
    m_variableHelpBtn->setToolTip("Show available template variables");
    connect(m_variableHelpBtn, &QPushButton::clicked,
            this, &CloudCopierPanel::onVariableHelpClicked);
    templateRow->addWidget(m_variableHelpBtn);

    templateRow->addStretch();
    memberSelLayout->addLayout(templateRow);

    // Preview and action buttons row
    QHBoxLayout* actionRow = new QHBoxLayout();

    m_previewExpansionBtn = new QPushButton("Preview Paths", this);
    m_previewExpansionBtn->setToolTip("Preview expanded paths for selected members");
    m_previewExpansionBtn->setStyleSheet(
        "QPushButton { background-color: #2196F3; color: white; "
        "border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold; } "
        "QPushButton:hover { background-color: #1976D2; } "
        "QPushButton:disabled { background-color: #AAAAAA; }");
    connect(m_previewExpansionBtn, &QPushButton::clicked,
            this, &CloudCopierPanel::onPreviewExpansionClicked);
    actionRow->addWidget(m_previewExpansionBtn);

    m_manageMembersBtn = new QPushButton("Manage Members...", this);
    m_manageMembersBtn->setToolTip("Open Member Registry to manage members");
    connect(m_manageMembersBtn, &QPushButton::clicked,
            this, &CloudCopierPanel::onManageMembersClicked);
    actionRow->addWidget(m_manageMembersBtn);

    actionRow->addStretch();
    memberSelLayout->addLayout(actionRow);

    // Expansion preview label
    m_expansionPreviewLabel = new QLabel(this);
    m_expansionPreviewLabel->setStyleSheet("color: #666; font-style: italic;");
    m_expansionPreviewLabel->setWordWrap(true);
    m_expansionPreviewLabel->hide();
    memberSelLayout->addWidget(m_expansionPreviewLabel);

    memberLayout->addWidget(m_memberSelectionWidget);

    // Initially hide member selection (manual mode is default)
    m_memberSelectionWidget->setVisible(false);

    // Connect mode change
    connect(m_destModeGroup, QOverload<int>::of(&QButtonGroup::idClicked),
            this, &CloudCopierPanel::onDestinationModeChanged);

    mainLayout->addWidget(m_memberGroup);
}

void CloudCopierPanel::setupTemplateSection(QVBoxLayout* mainLayout) {
    QGroupBox* templateGroup = new QGroupBox("TEMPLATES & IMPORT", this);
    templateGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; border: 1px solid #E0E0E0; "
        "border-radius: 6px; margin-top: 12px; padding-top: 16px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; }");

    QVBoxLayout* templateLayout = new QVBoxLayout(templateGroup);

    // Template row
    QHBoxLayout* templateRow = new QHBoxLayout();

    QLabel* templateLabel = new QLabel("Template:", this);
    templateRow->addWidget(templateLabel);

    m_templateCombo = new QComboBox(this);
    m_templateCombo->setMinimumWidth(200);
    m_templateCombo->addItem("-- Select Template --");
    connect(m_templateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CloudCopierPanel::onTemplateComboChanged);
    templateRow->addWidget(m_templateCombo);

    m_loadTemplateBtn = new QPushButton("Load", this);
    m_loadTemplateBtn->setToolTip("Load selected template configuration");
    m_loadTemplateBtn->setEnabled(false);
    connect(m_loadTemplateBtn, &QPushButton::clicked, this, &CloudCopierPanel::onLoadTemplateClicked);
    templateRow->addWidget(m_loadTemplateBtn);

    m_saveTemplateBtn = new QPushButton("Save As...", this);
    m_saveTemplateBtn->setToolTip("Save current configuration as template");
    connect(m_saveTemplateBtn, &QPushButton::clicked, this, &CloudCopierPanel::onSaveTemplateClicked);
    templateRow->addWidget(m_saveTemplateBtn);

    m_deleteTemplateBtn = new QPushButton("Delete", this);
    m_deleteTemplateBtn->setToolTip("Delete selected template");
    m_deleteTemplateBtn->setObjectName("PanelDangerButton");
    m_deleteTemplateBtn->setEnabled(false);
    connect(m_deleteTemplateBtn, &QPushButton::clicked, this, &CloudCopierPanel::onDeleteTemplateClicked);
    templateRow->addWidget(m_deleteTemplateBtn);

    templateRow->addStretch();
    templateLayout->addLayout(templateRow);

    // Import/Export row
    QHBoxLayout* importRow = new QHBoxLayout();

    m_importBtn = new QPushButton("Import from File...", this);
    m_importBtn->setToolTip("Import configuration from JSON file");
    connect(m_importBtn, &QPushButton::clicked, this, &CloudCopierPanel::onImportClicked);
    importRow->addWidget(m_importBtn);

    m_exportBtn = new QPushButton("Export to File...", this);
    m_exportBtn->setToolTip("Export configuration to JSON file");
    connect(m_exportBtn, &QPushButton::clicked, this, &CloudCopierPanel::onExportClicked);
    importRow->addWidget(m_exportBtn);

    importRow->addStretch();
    templateLayout->addLayout(importRow);

    mainLayout->addWidget(templateGroup);
}

void CloudCopierPanel::setupTaskTable(QVBoxLayout* mainLayout) {
    QGroupBox* taskGroup = new QGroupBox("COPY TASKS", this);
    taskGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; border: 1px solid #E0E0E0; "
        "border-radius: 6px; margin-top: 12px; padding-top: 16px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; }");

    QVBoxLayout* taskLayout = new QVBoxLayout(taskGroup);

    // Filter row
    QHBoxLayout* filterLayout = new QHBoxLayout();
    QLabel* filterLabel = new QLabel("Filter:", this);
    filterLayout->addWidget(filterLabel);

    m_taskFilterCombo = new QComboBox(this);
    m_taskFilterCombo->addItem("All Tasks", "all");
    m_taskFilterCombo->addItem("Pending", "pending");
    m_taskFilterCombo->addItem("Copying", "copying");
    m_taskFilterCombo->addItem("Completed", "completed");
    m_taskFilterCombo->addItem("Failed", "failed");
    m_taskFilterCombo->addItem("Skipped", "skipped");
    m_taskFilterCombo->setMinimumWidth(120);
    connect(m_taskFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int) {
        filterTasks(m_taskFilterCombo->currentData().toString());
    });
    filterLayout->addWidget(m_taskFilterCombo);

    filterLayout->addStretch();

    m_taskCountLabel = new QLabel("0 tasks", this);
    m_taskCountLabel->setStyleSheet("color: #666;");
    filterLayout->addWidget(m_taskCountLabel);

    taskLayout->addLayout(filterLayout);

    // Task table
    m_taskTable = new QTableWidget(this);
    m_taskTable->setColumnCount(COL_COUNT);
    m_taskTable->setHorizontalHeaderLabels({"Source", "Destination", "Status", "Progress"});
    m_taskTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_taskTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_taskTable->setAlternatingRowColors(true);
    m_taskTable->setMaximumHeight(200);
    m_taskTable->horizontalHeader()->setStretchLastSection(true);
    m_taskTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_taskTable->verticalHeader()->setVisible(false);
    m_taskTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(m_taskTable, &QTableWidget::itemSelectionChanged,
            this, &CloudCopierPanel::onTaskSelectionChanged);

    taskLayout->addWidget(m_taskTable);

    mainLayout->addWidget(taskGroup);
}

void CloudCopierPanel::setupProgressSection(QVBoxLayout* mainLayout) {
    m_progressGroup = new QGroupBox("PROGRESS", this);
    m_progressGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; border: 1px solid #E0E0E0; "
        "border-radius: 6px; margin-top: 12px; padding-top: 16px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; }");
    m_progressGroup->setVisible(false);

    QVBoxLayout* progressLayout = new QVBoxLayout(m_progressGroup);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_progressBar->setStyleSheet(
        "QProgressBar { border: 1px solid #E0E0E0; border-radius: 4px; "
        "background-color: #E8E8E8; height: 20px; text-align: center; } "
        "QProgressBar::chunk { background-color: #D90007; border-radius: 3px; }");
    progressLayout->addWidget(m_progressBar);

    m_currentItemLabel = new QLabel("", this);
    m_currentItemLabel->setStyleSheet("color: #666;");
    progressLayout->addWidget(m_currentItemLabel);

    m_statsLabel = new QLabel("", this);
    m_statsLabel->setStyleSheet("color: #666;");
    progressLayout->addWidget(m_statsLabel);

    mainLayout->addWidget(m_progressGroup);
}

void CloudCopierPanel::setupControlButtons(QVBoxLayout* mainLayout) {
    // Operation mode row (Copy vs Move)
    m_operationModeGroup = new QGroupBox("Operation Mode", this);
    m_operationModeGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; border: 1px solid #E0E0E0; "
        "border-radius: 6px; margin-top: 6px; padding: 8px; padding-top: 16px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");

    QHBoxLayout* modeLayout = new QHBoxLayout(m_operationModeGroup);

    m_operationModeButtonGroup = new QButtonGroup(this);

    m_copyModeRadio = new QRadioButton("Copy files (keep originals)", this);
    m_copyModeRadio->setChecked(true);  // Default: copy mode
    m_copyModeRadio->setToolTip("Copy files to destinations. Source files remain in place.");
    m_operationModeButtonGroup->addButton(m_copyModeRadio, 0);
    modeLayout->addWidget(m_copyModeRadio);

    m_moveModeRadio = new QRadioButton("Move files (delete source after transfer)", this);
    m_moveModeRadio->setToolTip("Move files to destination. Source files will be DELETED after successful transfer.\n"
                                "This is a server-side operation - no bandwidth is used.\n\n"
                                "WARNING: For multiple destinations, files are MOVED to the first destination,\n"
                                "then COPIED to the remaining destinations.");
    m_moveModeRadio->setStyleSheet("QRadioButton { color: #D90007; }");  // Red to indicate danger
    m_operationModeButtonGroup->addButton(m_moveModeRadio, 1);
    modeLayout->addWidget(m_moveModeRadio);

    modeLayout->addStretch();
    connect(m_operationModeButtonGroup, &QButtonGroup::idClicked,
            this, &CloudCopierPanel::onOperationModeChanged);

    mainLayout->addWidget(m_operationModeGroup);

    // Options row
    QHBoxLayout* optionsLayout = new QHBoxLayout();

    m_copyContentsOnlyCheck = new QCheckBox("Copy folder contents only (not the folder itself)", this);
    m_copyContentsOnlyCheck->setChecked(true);  // Default: copy contents only
    m_copyContentsOnlyCheck->setToolTip("When checked, copies only the files/folders INSIDE the source folder.\n"
                                         "When unchecked, copies the source folder itself into the destination.");
    optionsLayout->addWidget(m_copyContentsOnlyCheck);

    m_skipExistingCheck = new QCheckBox("Skip existing files", this);
    m_skipExistingCheck->setChecked(true);  // Default: skip existing
    m_skipExistingCheck->setToolTip("When checked, skips files that already exist at destination.\n"
                                     "When unchecked, overwrites existing files.");
    optionsLayout->addWidget(m_skipExistingCheck);

    optionsLayout->addStretch();
    mainLayout->addLayout(optionsLayout);

    // Control buttons row
    QHBoxLayout* controlLayout = new QHBoxLayout();

    m_previewBtn = new QPushButton("Preview", this);
    m_previewBtn->setToolTip("Show what will be copied and where BEFORE starting");
    m_previewBtn->setStyleSheet(
        "QPushButton { background-color: #4A90D9; color: white; "
        "border: none; border-radius: 6px; padding: 10px 20px; font-weight: bold; } "
        "QPushButton:hover { background-color: #3A80C9; } "
        "QPushButton:disabled { background-color: #AAAAAA; }");
    connect(m_previewBtn, &QPushButton::clicked, this, &CloudCopierPanel::onPreviewCopyClicked);
    controlLayout->addWidget(m_previewBtn);

    m_startBtn = new QPushButton("Start Copy", this);
    m_startBtn->setToolTip("Start copying files to destinations");
    m_startBtn->setObjectName("PanelPrimaryButton");
    m_startBtn->setStyleSheet(
        "QPushButton { background-color: #D90007; color: white; "
        "border: none; border-radius: 6px; padding: 10px 24px; font-weight: bold; } "
        "QPushButton:hover { background-color: #C00006; } "
        "QPushButton:disabled { background-color: #AAAAAA; }");
    connect(m_startBtn, &QPushButton::clicked, this, &CloudCopierPanel::onStartCopyClicked);
    controlLayout->addWidget(m_startBtn);

    m_pauseBtn = new QPushButton("Pause", this);
    m_pauseBtn->setToolTip("Pause copy operation");
    m_pauseBtn->setEnabled(false);
    connect(m_pauseBtn, &QPushButton::clicked, this, &CloudCopierPanel::onPauseCopyClicked);
    controlLayout->addWidget(m_pauseBtn);

    m_cancelBtn = new QPushButton("Cancel", this);
    m_cancelBtn->setToolTip("Cancel copy operation");
    m_cancelBtn->setObjectName("PanelDangerButton");
    m_cancelBtn->setEnabled(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &CloudCopierPanel::onCancelCopyClicked);
    controlLayout->addWidget(m_cancelBtn);

    controlLayout->addStretch();

    m_clearCompletedBtn = new QPushButton("Clear Completed", this);
    m_clearCompletedBtn->setToolTip("Remove completed tasks from list");
    connect(m_clearCompletedBtn, &QPushButton::clicked, this, &CloudCopierPanel::onClearCompletedClicked);
    controlLayout->addWidget(m_clearCompletedBtn);

    m_clearAllTasksBtn = new QPushButton("Clear All Tasks", this);
    m_clearAllTasksBtn->setToolTip("Remove all tasks from list (completed, failed, and pending)");
    m_clearAllTasksBtn->setObjectName("PanelDangerButton");
    connect(m_clearAllTasksBtn, &QPushButton::clicked, this, &CloudCopierPanel::onClearAllTasksClicked);
    controlLayout->addWidget(m_clearAllTasksBtn);

    mainLayout->addLayout(controlLayout);
}

void CloudCopierPanel::setupErrorLogSection(QVBoxLayout* mainLayout) {
    m_errorLogGroup = new QGroupBox("Error Log (0)", this);
    m_errorLogGroup->setCheckable(true);
    m_errorLogGroup->setChecked(false);  // Collapsed by default
    m_errorLogGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; border: 1px solid #E0E0E0; "
        "border-radius: 6px; margin-top: 12px; padding-top: 16px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; } "
        "QGroupBox::indicator { width: 13px; height: 13px; } ");

    QVBoxLayout* errorLayout = new QVBoxLayout(m_errorLogGroup);

    m_errorLogEdit = new QTextEdit(this);
    m_errorLogEdit->setReadOnly(true);
    m_errorLogEdit->setMaximumHeight(150);
    m_errorLogEdit->setPlaceholderText("Errors and warnings will appear here...");
    m_errorLogEdit->setStyleSheet(
        "QTextEdit { background-color: #FFF8F8; border: 1px solid #FFCCCC; border-radius: 4px; "
        "font-family: monospace; font-size: 11px; }");
    errorLayout->addWidget(m_errorLogEdit);

    QHBoxLayout* errorBtnLayout = new QHBoxLayout();
    errorBtnLayout->addStretch();

    m_clearErrorLogBtn = new QPushButton("Clear Log", this);
    m_clearErrorLogBtn->setToolTip("Clear all error messages");
    connect(m_clearErrorLogBtn, &QPushButton::clicked, this, &CloudCopierPanel::clearErrorLog);
    errorBtnLayout->addWidget(m_clearErrorLogBtn);

    errorLayout->addLayout(errorBtnLayout);

    // Connect the checkbox toggle to show/hide content
    connect(m_errorLogGroup, &QGroupBox::toggled, [this](bool checked) {
        m_errorLogEdit->setVisible(checked);
        m_clearErrorLogBtn->setVisible(checked);
    });

    // Initially hide content since collapsed
    m_errorLogEdit->setVisible(false);
    m_clearErrorLogBtn->setVisible(false);

    mainLayout->addWidget(m_errorLogGroup);
}

void CloudCopierPanel::logError(const QString& message, const QString& details) {
    QString timestamp = QTime::currentTime().toString("HH:mm:ss");
    QString logEntry = QString("<span style='color: #C00000;'>[%1] <b>ERROR:</b> %2</span>")
        .arg(timestamp).arg(message.toHtmlEscaped());

    if (!details.isEmpty()) {
        logEntry += QString("<br>&nbsp;&nbsp;&nbsp;&nbsp;<span style='color: #666;'>%1</span>")
            .arg(details.toHtmlEscaped());
    }

    m_errorLogEdit->append(logEntry);
    m_errorCount++;
    m_errorLogGroup->setTitle(QString("Error Log (%1)").arg(m_errorCount));

    // Auto-expand on first error
    if (m_errorCount == 1) {
        m_errorLogGroup->setChecked(true);
    }
}

void CloudCopierPanel::logWarning(const QString& message, const QString& details) {
    QString timestamp = QTime::currentTime().toString("HH:mm:ss");
    QString logEntry = QString("<span style='color: #CC7000;'>[%1] <b>WARNING:</b> %2</span>")
        .arg(timestamp).arg(message.toHtmlEscaped());

    if (!details.isEmpty()) {
        logEntry += QString("<br>&nbsp;&nbsp;&nbsp;&nbsp;<span style='color: #666;'>%1</span>")
            .arg(details.toHtmlEscaped());
    }

    m_errorLogEdit->append(logEntry);
}

void CloudCopierPanel::clearErrorLog() {
    m_errorLogEdit->clear();
    m_errorCount = 0;
    m_errorLogGroup->setTitle("Error Log (0)");
}

void CloudCopierPanel::filterTasks(const QString& status) {
    for (int row = 0; row < m_taskTable->rowCount(); ++row) {
        QTableWidgetItem* statusItem = m_taskTable->item(row, COL_STATUS);
        if (!statusItem) continue;

        QString taskStatus = statusItem->text().toLower();
        bool show = true;

        if (status != "all") {
            if (status == "pending") {
                show = taskStatus == "pending" || taskStatus == "queued";
            } else if (status == "copying") {
                show = taskStatus == "copying" || taskStatus == "in progress";
            } else if (status == "completed") {
                show = taskStatus == "completed" || taskStatus == "done";
            } else if (status == "failed") {
                show = taskStatus == "failed" || taskStatus == "error";
            } else if (status == "skipped") {
                show = taskStatus == "skipped";
            }
        }

        m_taskTable->setRowHidden(row, !show);
    }
    updateTaskCounts();
}

void CloudCopierPanel::updateTaskCounts() {
    int total = m_taskTable->rowCount();
    int visible = 0;
    int pending = 0, copying = 0, completed = 0, failed = 0, skipped = 0;

    for (int row = 0; row < total; ++row) {
        if (!m_taskTable->isRowHidden(row)) {
            visible++;
        }

        QTableWidgetItem* statusItem = m_taskTable->item(row, COL_STATUS);
        if (!statusItem) continue;

        QString status = statusItem->text().toLower();
        if (status == "pending" || status == "queued") pending++;
        else if (status == "copying" || status == "in progress") copying++;
        else if (status == "completed" || status == "done") completed++;
        else if (status == "failed" || status == "error") failed++;
        else if (status == "skipped") skipped++;
    }

    QString currentFilter = m_taskFilterCombo->currentData().toString();
    QString countText;

    if (currentFilter == "all") {
        countText = QString("%1 tasks").arg(total);
    } else {
        countText = QString("Showing %1 of %2 tasks").arg(visible).arg(total);
    }

    // Update filter combo item text with counts
    m_taskFilterCombo->setItemText(0, QString("All Tasks (%1)").arg(total));
    m_taskFilterCombo->setItemText(1, QString("Pending (%1)").arg(pending));
    m_taskFilterCombo->setItemText(2, QString("Copying (%1)").arg(copying));
    m_taskFilterCombo->setItemText(3, QString("Completed (%1)").arg(completed));
    m_taskFilterCombo->setItemText(4, QString("Failed (%1)").arg(failed));
    m_taskFilterCombo->setItemText(5, QString("Skipped (%1)").arg(skipped));

    m_taskCountLabel->setText(countText);
}

void CloudCopierPanel::updateButtonStates() {
    bool hasSources = m_sourceList->count() > 0;
    bool hasDestinations = m_destinationList->count() > 0;
    bool hasSourceSelection = !m_sourceList->selectedItems().isEmpty();
    bool hasDestSelection = !m_destinationList->selectedItems().isEmpty();
    bool hasTemplateSelected = m_templateCombo->currentIndex() > 0;

    m_removeSourceBtn->setEnabled(hasSourceSelection && !m_isCopying);
    m_clearSourcesBtn->setEnabled(hasSources && !m_isCopying);
    m_editDestsBtn->setEnabled(hasDestinations && !m_isCopying);
    m_removeDestBtn->setEnabled(hasDestSelection && !m_isCopying);
    m_clearDestsBtn->setEnabled(hasDestinations && !m_isCopying);

    m_loadTemplateBtn->setEnabled(hasTemplateSelected && !m_isCopying);
    m_deleteTemplateBtn->setEnabled(hasTemplateSelected && !m_isCopying);
    m_saveTemplateBtn->setEnabled(hasDestinations && !m_isCopying);
    m_exportBtn->setEnabled(hasDestinations);

    m_previewBtn->setEnabled(hasSources && hasDestinations && !m_isCopying);
    m_startBtn->setEnabled(hasSources && hasDestinations && !m_isCopying);
    m_pauseBtn->setEnabled(m_isCopying);
    m_cancelBtn->setEnabled(m_isCopying);
    m_clearCompletedBtn->setEnabled(m_taskTable->rowCount() > 0 && !m_isCopying);
    m_clearAllTasksBtn->setEnabled(m_taskTable->rowCount() > 0 && !m_isCopying);

    m_addSourceBtn->setEnabled(!m_isCopying);
    m_addDestBtn->setEnabled(!m_isCopying);
    m_pasteDestsBtn->setEnabled(!m_isCopying);
    m_importBtn->setEnabled(!m_isCopying);
}

void CloudCopierPanel::updateTemplateCombo() {
    m_templateCombo->clear();
    m_templateCombo->addItem("-- Select Template --");

    if (m_controller) {
        QStringList templates = m_controller->getTemplateNames();
        for (const auto& name : templates) {
            m_templateCombo->addItem(name);
        }
    }
}

int CloudCopierPanel::findTaskRow(int taskId) {
    for (int row = 0; row < m_taskTable->rowCount(); ++row) {
        QTableWidgetItem* item = m_taskTable->item(row, 0);
        if (item && item->data(Qt::UserRole).toInt() == taskId) {
            return row;
        }
    }
    return -1;
}

QString CloudCopierPanel::shortenPath(const QString& path, int maxLength) {
    if (path.length() <= maxLength) {
        return path;
    }

    // Show first 15 chars + ... + last (maxLength - 18) chars
    return path.left(15) + "..." + path.right(maxLength - 18);
}

QStringList CloudCopierPanel::showPastePathsDialog(const QString& title, const QString& instruction,
                                                    const QString& placeholder, const QString& buttonText) {
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.setMinimumSize(500, 400);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    QLabel* instructionLabel = new QLabel(instruction, &dialog);
    instructionLabel->setStyleSheet("color: #666; margin-bottom: 8px;");
    layout->addWidget(instructionLabel);

    QTextEdit* textEdit = new QTextEdit(&dialog);
    textEdit->setPlaceholderText(placeholder);
    layout->addWidget(textEdit);

    QLabel* countLabel = new QLabel("0 paths entered", &dialog);
    countLabel->setStyleSheet("color: #888;");
    layout->addWidget(countLabel);

    // Update count as user types
    connect(textEdit, &QTextEdit::textChanged, [textEdit, countLabel]() {
        QString text = textEdit->toPlainText();
        QStringList lines = text.split('\n');
        int count = 0;
        for (const QString& line : lines) {
            if (!PathUtils::isPathEmpty(line)) {
                count++;
            }
        }
        countLabel->setText(QString("%1 path(s) entered").arg(count));
    });

    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    QPushButton* cancelBtn = new QPushButton("Cancel", &dialog);
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);

    QPushButton* addBtn = new QPushButton(buttonText, &dialog);
    addBtn->setStyleSheet(
        "QPushButton { background-color: #D90007; color: white; "
        "border: none; border-radius: 4px; padding: 8px 16px; font-weight: bold; } "
        "QPushButton:hover { background-color: #C00006; }");
    connect(addBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    btnLayout->addWidget(addBtn);

    layout->addLayout(btnLayout);

    QStringList result;
    if (dialog.exec() == QDialog::Accepted) {
        QString text = textEdit->toPlainText();
        QStringList lines = text.split('\n');
        for (const QString& line : lines) {
            QString path = PathUtils::normalizeRemotePath(line);
            if (!path.isEmpty() && path != "/") {
                result.append(path);
            }
        }
    }
    return result;
}

// Slots - Data updates
void CloudCopierPanel::onSourcesChanged(const QStringList& sources) {
    m_sourceList->clear();
    for (const auto& source : sources) {
        m_sourceList->addItem(source);
    }
    m_sourceSummaryLabel->setText(QString("%1 item(s) selected").arg(sources.size()));
    updateButtonStates();
}

void CloudCopierPanel::onDestinationsChanged(const QStringList& destinations) {
    m_destinationList->clear();
    for (const auto& dest : destinations) {
        m_destinationList->addItem(dest);
    }
    m_destSummaryLabel->setText(QString("%1 destination(s)").arg(destinations.size()));
    updateButtonStates();
}

void CloudCopierPanel::onTemplatesChanged() {
    updateTemplateCombo();
}

void CloudCopierPanel::onTasksClearing() {
    // Clear the task table before new tasks are added
    qDebug() << "CloudCopierPanel: Clearing task table (had" << m_taskTable->rowCount() << "rows)";
    m_taskTable->setRowCount(0);
}

void CloudCopierPanel::onTaskCreated(int taskId, const QString& source, const QString& destination) {
    // Prevent duplicates - check if task already exists
    if (findTaskRow(taskId) >= 0) {
        qDebug() << "CloudCopierPanel: Task" << taskId << "already exists, skipping duplicate";
        return;
    }

    int row = m_taskTable->rowCount();
    m_taskTable->insertRow(row);

    QTableWidgetItem* srcItem = new QTableWidgetItem(shortenPath(source));
    srcItem->setData(Qt::UserRole, taskId);
    srcItem->setToolTip(source);
    m_taskTable->setItem(row, COL_SOURCE, srcItem);

    QTableWidgetItem* destItem = new QTableWidgetItem(shortenPath(destination));
    destItem->setToolTip(destination);
    m_taskTable->setItem(row, COL_DESTINATION, destItem);

    QTableWidgetItem* statusItem = new QTableWidgetItem("Pending");
    m_taskTable->setItem(row, COL_STATUS, statusItem);

    QTableWidgetItem* progressItem = new QTableWidgetItem("0%");
    m_taskTable->setItem(row, COL_PROGRESS, progressItem);
}

void CloudCopierPanel::onTaskProgress(int taskId, int progress) {
    int row = findTaskRow(taskId);
    if (row >= 0) {
        m_taskTable->item(row, COL_PROGRESS)->setText(QString("%1%").arg(progress));
        m_taskTable->item(row, COL_STATUS)->setText("Copying...");
    }
}

void CloudCopierPanel::onTaskStatusChanged(int taskId, const QString& status) {
    int row = findTaskRow(taskId);
    if (row >= 0) {
        m_taskTable->item(row, COL_STATUS)->setText(status);

        // Color-code status
        QColor bgColor = Qt::white;
        if (status == "Completed") {
            bgColor = QColor("#E8F5E9");  // Light green
            m_taskTable->item(row, COL_PROGRESS)->setText("100%");
        } else if (status == "Failed") {
            bgColor = QColor("#FFEBEE");  // Light red
        } else if (status == "Skipped") {
            bgColor = QColor("#FFF3E0");  // Light orange
        } else if (status == "Copying...") {
            bgColor = QColor("#FFE6E7");  // Light MEGA red
        }

        for (int col = 0; col < COL_COUNT; ++col) {
            if (m_taskTable->item(row, col)) {
                m_taskTable->item(row, col)->setBackground(bgColor);
            }
        }
    }
}

void CloudCopierPanel::onCopyStarted(int totalTasks) {
    m_isCopying = true;
    m_progressGroup->setVisible(true);
    m_progressBar->setValue(0);
    m_currentItemLabel->setText("Starting copy operation...");
    m_statsLabel->setText(QString("0 / %1 tasks").arg(totalTasks));
    updateButtonStates();
}

void CloudCopierPanel::onCopyProgress(int completed, int total, const QString& currentItem, const QString& currentDest) {
    int progress = (total > 0) ? (completed * 100 / total) : 0;
    m_progressBar->setValue(progress);
    m_currentItemLabel->setText(QString("Copying: %1").arg(shortenPath(currentItem, 50)));
    m_statsLabel->setText(QString("%1 / %2 tasks").arg(completed).arg(total));
}

void CloudCopierPanel::onCopyCompleted(int successful, int failed, int skipped) {
    m_isCopying = false;
    m_progressBar->setValue(100);
    m_currentItemLabel->setText("Copy operation completed");
    m_statsLabel->setText(QString("Completed: %1 | Failed: %2 | Skipped: %3")
                         .arg(successful).arg(failed).arg(skipped));
    updateButtonStates();

    // Show summary message
    QString message = QString("Copy operation completed.\n\n"
                             "Successful: %1\n"
                             "Failed: %2\n"
                             "Skipped: %3")
                     .arg(successful).arg(failed).arg(skipped);

    if (failed > 0) {
        QMessageBox::warning(this, "Copy Completed with Errors", message);
    } else {
        QMessageBox::information(this, "Copy Completed", message);
    }
}

void CloudCopierPanel::onCopyPaused() {
    m_currentItemLabel->setText("Copy operation paused");
    m_pauseBtn->setText("Resume");
}

void CloudCopierPanel::onCopyCancelled() {
    m_isCopying = false;
    m_currentItemLabel->setText("Copy operation cancelled");
    updateButtonStates();
}

void CloudCopierPanel::onError(const QString& operation, const QString& message) {
    QMessageBox::warning(this, "Error - " + operation, message);
}

// Slots - Source section
void CloudCopierPanel::onAddSourceClicked() {
    if (!m_fileController) {
        QMessageBox::warning(this, "Error", "File controller not available. Please login first.");
        return;
    }

    RemoteFolderBrowserDialog dialog(this);
    dialog.setFileController(m_fileController);
    dialog.setSelectionMode(RemoteFolderBrowserDialog::MultipleItems);
    dialog.setTitle("Select Source Files/Folders");
    dialog.setInitialPath("/");
    dialog.refresh();

    if (dialog.exec() == QDialog::Accepted) {
        QStringList selected = dialog.selectedPaths();
        for (const QString& path : selected) {
            emit addSourceRequested(path);
        }
    }
}

void CloudCopierPanel::onPasteSourcesClicked() {
    QStringList paths = showPastePathsDialog(
        "Paste Multiple Sources",
        "Paste source paths below (one per line).\n"
        "Paths should be MEGA cloud paths starting with /",
        "Example:\n"
        "/Alen Sultanic - NHB+ - EGBs/0. Nothing Held Back+/November\n"
        "/Alen Sultanic - NHB+ - EGBs/3. Icekkk/November\n"
        "/Alen Sultanic - NHB+ - EGBs/5. David/November",
        "Add Sources"
    );

    for (const QString& path : paths) {
        emit addSourceRequested(path);
    }
}

void CloudCopierPanel::onEditSourcesClicked() {
    // Gather current sources
    QStringList currentPaths;
    for (int i = 0; i < m_sourceList->count(); ++i) {
        currentPaths.append(m_sourceList->item(i)->text());
    }

    if (currentPaths.isEmpty()) {
        QMessageBox::information(this, "No Sources", "Add some sources first before editing.");
        return;
    }

    BulkPathEditorDialog dialog(this);
    dialog.setWindowTitle("Edit Sources");
    dialog.setPaths(currentPaths);

    if (dialog.exec() == QDialog::Accepted) {
        QStringList modifiedPaths = dialog.getModifiedPaths();

        // Clear existing sources
        emit clearSourcesRequested();

        // Add modified paths
        for (const QString& path : modifiedPaths) {
            if (!path.isEmpty()) {
                emit addSourceRequested(path);
            }
        }
    }
}

void CloudCopierPanel::onRemoveSourceClicked() {
    // Collect paths first to avoid iterator invalidation
    // (the signal triggers onSourcesChanged which clears the list)
    QStringList pathsToRemove;
    for (auto* item : m_sourceList->selectedItems()) {
        pathsToRemove.append(item->text());
    }

    // Now emit signals after collecting all paths
    for (const QString& path : pathsToRemove) {
        emit removeSourceRequested(path);
    }
}

void CloudCopierPanel::onClearSourcesClicked() {
    if (QMessageBox::question(this, "Clear Sources",
                             "Remove all sources?",
                             QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        emit clearSourcesRequested();
    }
}

void CloudCopierPanel::onSourceSelectionChanged() {
    updateButtonStates();
}

// Slots - Destination section
void CloudCopierPanel::onAddDestinationClicked() {
    if (!m_fileController) {
        QMessageBox::warning(this, "Error", "File controller not available. Please login first.");
        return;
    }

    RemoteFolderBrowserDialog dialog(this);
    dialog.setFileController(m_fileController);
    dialog.setSelectionMode(RemoteFolderBrowserDialog::MultipleFolders);
    dialog.setTitle("Select Destination Folders");
    dialog.setInitialPath("/");
    dialog.refresh();

    if (dialog.exec() == QDialog::Accepted) {
        QStringList selected = dialog.selectedPaths();
        for (const QString& path : selected) {
            emit addDestinationRequested(path);
        }
    }
}

void CloudCopierPanel::onPasteDestinationsClicked() {
    QStringList paths = showPastePathsDialog(
        "Paste Multiple Destinations",
        "Paste destination paths below (one per line).\n"
        "Paths should be MEGA cloud paths starting with /",
        "Example:\n"
        "/Alen Sultanic - NHB+ - EGBs/0. Nothing Held Back+/November. \n"
        "/Alen Sultanic - NHB+ - EGBs/3. Icekkk/November. \n"
        "/Alen Sultanic - NHB+ - EGBs/5. David/November. ",
        "Add Destinations"
    );

    if (paths.isEmpty()) return;

    int addedCount = 0;
    int skippedCount = 0;

    for (const QString& path : paths) {
        // Check if already in list
        bool exists = false;
        for (int i = 0; i < m_destinationList->count(); ++i) {
            if (m_destinationList->item(i)->text() == path) {
                exists = true;
                break;
            }
        }

        if (!exists) {
            emit addDestinationRequested(path);
            addedCount++;
        } else {
            skippedCount++;
        }
    }

    if (addedCount > 0 || skippedCount > 0) {
        QString message = QString("Added %1 destination(s)").arg(addedCount);
        if (skippedCount > 0) {
            message += QString("\nSkipped %1 duplicate(s)").arg(skippedCount);
        }
        QMessageBox::information(this, "Destinations Added", message);
    }
}

void CloudCopierPanel::onEditDestinationsClicked() {
    if (m_destinationList->count() == 0) {
        QMessageBox::information(this, "No Destinations",
            "Please add destinations first before using the bulk editor.");
        return;
    }

    if (m_destinationList->count() == 1) {
        QMessageBox::information(this, "Single Destination",
            "Bulk editor works best with multiple destinations. "
            "For a single path, you can remove and re-add it.");
        return;
    }

    // Gather current destinations
    QStringList currentPaths;
    for (int i = 0; i < m_destinationList->count(); ++i) {
        currentPaths.append(m_destinationList->item(i)->text());
    }

    // Open the bulk editor dialog
    BulkPathEditorDialog dialog(this);
    dialog.setPaths(currentPaths);

    if (dialog.exec() == QDialog::Accepted) {
        QStringList modifiedPaths = dialog.getModifiedPaths();

        // Check if anything actually changed
        bool hasChanges = false;
        for (int i = 0; i < currentPaths.size() && i < modifiedPaths.size(); ++i) {
            if (currentPaths[i] != modifiedPaths[i]) {
                hasChanges = true;
                break;
            }
        }

        if (!hasChanges) {
            QMessageBox::information(this, "No Changes", "No paths were modified.");
            return;
        }

        // Clear current destinations and add the modified ones
        emit clearDestinationsRequested();

        // Add all modified destinations
        for (const QString& path : modifiedPaths) {
            emit addDestinationRequested(path);
        }

        QMessageBox::information(this, "Paths Updated",
            QString("Successfully updated %1 destination path(s).").arg(modifiedPaths.size()));
    }
}

void CloudCopierPanel::onRemoveDestinationClicked() {
    // Collect paths first to avoid iterator invalidation
    // (the signal triggers onDestinationsChanged which clears the list)
    QStringList pathsToRemove;
    for (auto* item : m_destinationList->selectedItems()) {
        pathsToRemove.append(item->text());
    }
    // Now emit signals after collecting all paths
    for (const QString& path : pathsToRemove) {
        emit removeDestinationRequested(path);
    }
}

void CloudCopierPanel::onClearDestinationsClicked() {
    if (QMessageBox::question(this, "Clear Destinations",
                             "Remove all destinations?",
                             QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        emit clearDestinationsRequested();
    }
}

void CloudCopierPanel::onDestinationSelectionChanged() {
    updateButtonStates();
}

// Slots - Template section
void CloudCopierPanel::onSaveTemplateClicked() {
    QString name = QInputDialog::getText(this, "Save Template",
                                         "Enter template name:");
    if (!name.isEmpty()) {
        emit saveTemplateRequested(name);
    }
}

void CloudCopierPanel::onLoadTemplateClicked() {
    if (m_templateCombo->currentIndex() > 0) {
        emit loadTemplateRequested(m_templateCombo->currentText());
    }
}

void CloudCopierPanel::onDeleteTemplateClicked() {
    if (m_templateCombo->currentIndex() > 0) {
        QString name = m_templateCombo->currentText();
        if (QMessageBox::question(this, "Delete Template",
                                 QString("Delete template '%1'?").arg(name),
                                 QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
            emit deleteTemplateRequested(name);
        }
    }
}

void CloudCopierPanel::onTemplateComboChanged(int index) {
    updateButtonStates();
}

// Slots - Import/Export
void CloudCopierPanel::onImportClicked() {
    QString filePath = QFileDialog::getOpenFileName(this, "Import Destinations",
                                                    QString(), "Text Files (*.txt);;All Files (*)");
    if (!filePath.isEmpty()) {
        emit importDestinationsRequested(filePath);
    }
}

void CloudCopierPanel::onExportClicked() {
    QString filePath = QFileDialog::getSaveFileName(this, "Export Destinations",
                                                    "destinations.txt", "Text Files (*.txt);;All Files (*)");
    if (!filePath.isEmpty()) {
        emit exportDestinationsRequested(filePath);
    }
}

// Slots - Copy control
void CloudCopierPanel::onPreviewCopyClicked() {
    bool copyContentsOnly = m_copyContentsOnlyCheck->isChecked();
    emit previewCopyRequested(copyContentsOnly);
}

void CloudCopierPanel::onOperationModeChanged() {
    bool moveMode = m_moveModeRadio->isChecked();

    // Update button labels based on mode
    if (moveMode) {
        m_startBtn->setText("Start Move");
        m_previewBtn->setText("Preview Move");
        m_startBtn->setToolTip("Start MOVING files to destinations (source will be deleted)");
    } else {
        m_startBtn->setText("Start Copy");
        m_previewBtn->setText("Preview");
        m_startBtn->setToolTip("Start copying files to destinations");
    }

    // Notify controller of mode change
    if (m_controller) {
        m_controller->setMoveMode(moveMode);
    }
}

void CloudCopierPanel::onStartCopyClicked() {
    bool copyContentsOnly = m_copyContentsOnlyCheck->isChecked();
    bool skipExisting = m_skipExistingCheck->isChecked();
    bool moveMode = m_moveModeRadio->isChecked();

    // Show confirmation for move mode
    if (moveMode) {
        QMessageBox::StandardButton reply = QMessageBox::warning(
            this,
            "Confirm Move Operation",
            "Move mode is enabled. Source files will be DELETED after successful transfer.\n\n"
            "This action cannot be undone. Are you sure you want to continue?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );

        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    if (m_memberModeEnabled && m_controller) {
        // Use member copy when member mode is enabled
        m_controller->startMemberCopy(copyContentsOnly, skipExisting);
    } else {
        // Use regular copy
        emit startCopyRequested(copyContentsOnly, skipExisting, moveMode);
    }
}

void CloudCopierPanel::onPauseCopyClicked() {
    emit pauseCopyRequested();
}

void CloudCopierPanel::onCancelCopyClicked() {
    if (QMessageBox::question(this, "Cancel Copy",
                             "Cancel the copy operation?",
                             QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        emit cancelCopyRequested();
    }
}

void CloudCopierPanel::onClearCompletedClicked() {
    qDebug() << "CloudCopierPanel::onClearCompletedClicked() - Starting";
    qDebug() << "  m_isCopying:" << m_isCopying << "rowCount:" << m_taskTable->rowCount();

    // Safety check - don't clear while copying is in progress
    if (m_isCopying) {
        qDebug() << "  Aborting - copy still in progress";
        return;
    }

    // Remove completed rows from table first (before emitting signal)
    // Iterate backwards to avoid index shifting issues
    QList<int> rowsToRemove;
    int rowCount = m_taskTable->rowCount();  // Cache the count
    qDebug() << "  Scanning" << rowCount << "rows for completed tasks";

    for (int row = 0; row < rowCount; ++row) {
        QTableWidgetItem* statusItem = m_taskTable->item(row, COL_STATUS);
        if (statusItem) {
            QString status = statusItem->text();
            if (status == "Completed" || status == "Failed" || status == "Skipped") {
                rowsToRemove.prepend(row);  // Add to front so we remove from end first
            }
        }
    }

    qDebug() << "  Found" << rowsToRemove.size() << "rows to remove";

    // Remove rows in reverse order
    for (int row : rowsToRemove) {
        if (row < m_taskTable->rowCount()) {  // Safety check
            m_taskTable->removeRow(row);
        }
    }

    qDebug() << "  After removal:" << m_taskTable->rowCount() << "rows remain";

    // Notify controller
    emit clearCompletedRequested();
    qDebug() << "CloudCopierPanel::onClearCompletedClicked() - Done";
}

void CloudCopierPanel::onClearAllTasksClicked() {
    if (m_taskTable->rowCount() == 0) {
        return;
    }

    if (QMessageBox::question(this, "Clear All Tasks",
                             QString("Remove all %1 task(s) from the list?").arg(m_taskTable->rowCount()),
                             QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        // Clear all rows from the task table
        m_taskTable->setRowCount(0);

        // Hide progress section since there are no tasks
        m_progressGroup->setVisible(false);

        // Update button states
        updateButtonStates();
    }
}

void CloudCopierPanel::onTaskSelectionChanged() {
    // Could enable task-specific actions here
}

void CloudCopierPanel::onPreviewReady(const QVector<CopyPreviewItem>& previewItems) {
    // Create preview dialog
    QDialog dialog(this);
    dialog.setWindowTitle("Copy Preview");
    dialog.setMinimumSize(700, 500);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    // Summary
    QLabel* summaryLabel = new QLabel(
        QString("<b>%1 copy operation(s) will be performed:</b>").arg(previewItems.size()),
        &dialog);
    layout->addWidget(summaryLabel);

    // Preview text
    QTextEdit* previewText = new QTextEdit(&dialog);
    previewText->setReadOnly(true);
    previewText->setFont(QFont("Courier New", 9));

    QString previewContent;
    QString currentDest;
    int destCount = 0;

    for (const CopyPreviewItem& item : previewItems) {
        // Group by destination for cleaner display
        if (item.destinationPath != currentDest) {
            if (!currentDest.isEmpty()) {
                previewContent += "\n";
            }
            currentDest = item.destinationPath;
            destCount++;
        }

        QString typeIcon = item.isFolder ? "[FOLDER]" : "[FILE]";
        previewContent += QString("%1 %2\n    -> %3\n")
            .arg(typeIcon)
            .arg(item.sourceName)
            .arg(item.destinationPath);
    }

    previewText->setPlainText(previewContent);
    layout->addWidget(previewText);

    // Options reminder
    QLabel* optionsLabel = new QLabel(&dialog);
    QString skipMode = m_skipExistingCheck->isChecked() ? "SKIP existing files" : "OVERWRITE existing files";
    optionsLabel->setText(QString("<i>Conflict handling: %1</i>").arg(skipMode));
    optionsLabel->setStyleSheet("color: #666;");
    layout->addWidget(optionsLabel);

    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    QPushButton* cancelBtn = new QPushButton("Cancel", &dialog);
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);

    QPushButton* proceedBtn = new QPushButton("Proceed with Copy", &dialog);
    proceedBtn->setStyleSheet(
        "QPushButton { background-color: #D90007; color: white; "
        "border: none; border-radius: 4px; padding: 10px 20px; font-weight: bold; } "
        "QPushButton:hover { background-color: #C00006; }");
    connect(proceedBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    btnLayout->addWidget(proceedBtn);

    layout->addLayout(btnLayout);

    // If user accepts, start the copy
    if (dialog.exec() == QDialog::Accepted) {
        bool copyContentsOnly = m_copyContentsOnlyCheck->isChecked();
        bool skipExisting = m_skipExistingCheck->isChecked();
        bool moveMode = m_moveModeRadio->isChecked();
        emit startCopyRequested(copyContentsOnly, skipExisting, moveMode);
    }
}

void CloudCopierPanel::onValidateDestinationsClicked() {
    emit validateDestinationsRequested();
}

void CloudCopierPanel::onSourcesValidated(const QVector<PathValidationResult>& results) {
    // Show validation results dialog
    QDialog dialog(this);
    dialog.setWindowTitle("Source Validation Results");
    dialog.setMinimumSize(600, 400);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    // Count valid/invalid
    int validCount = 0;
    int invalidCount = 0;
    for (const auto& result : results) {
        if (result.exists) validCount++;
        else invalidCount++;
    }

    QLabel* summaryLabel = new QLabel(
        QString("<b>%1 sources checked:</b> %2 valid, %3 invalid")
            .arg(results.size()).arg(validCount).arg(invalidCount),
        &dialog);
    if (invalidCount > 0) {
        summaryLabel->setStyleSheet("color: #C00; font-weight: bold;");
    } else {
        summaryLabel->setStyleSheet("color: #060; font-weight: bold;");
    }
    layout->addWidget(summaryLabel);

    QTextEdit* resultText = new QTextEdit(&dialog);
    resultText->setReadOnly(true);
    resultText->setFont(QFont("Courier New", 9));

    QString content;
    for (const auto& result : results) {
        QString status = result.exists ? "OK" : "NOT FOUND";
        QString type = result.isFolder ? "[FOLDER]" : "[FILE]";
        QString line = QString("[%1] %2 %3\n").arg(status, -9).arg(type).arg(result.path);
        if (!result.exists) {
            line = QString("<span style='color:red;'>%1</span>").arg(line.toHtmlEscaped().replace("\n", "<br>"));
        }
        content += line;
    }
    resultText->setHtml(QString("<pre>%1</pre>").arg(content));
    layout->addWidget(resultText);

    QPushButton* closeBtn = new QPushButton("Close", &dialog);
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    layout->addWidget(closeBtn);

    dialog.exec();
}

void CloudCopierPanel::onDestinationsValidated(const QVector<PathValidationResult>& results) {
    // Show validation results dialog
    QDialog dialog(this);
    dialog.setWindowTitle("Destination Validation Results");
    dialog.setMinimumSize(600, 400);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    // Count valid/invalid
    int validCount = 0;
    int invalidCount = 0;
    for (const auto& result : results) {
        if (result.exists && result.errorMessage.isEmpty()) validCount++;
        else invalidCount++;
    }

    QLabel* summaryLabel = new QLabel(
        QString("<b>%1 destinations checked:</b> %2 valid, %3 invalid/missing")
            .arg(results.size()).arg(validCount).arg(invalidCount),
        &dialog);
    if (invalidCount > 0) {
        summaryLabel->setStyleSheet("color: #C00; font-weight: bold;");
    } else {
        summaryLabel->setStyleSheet("color: #060; font-weight: bold;");
    }
    layout->addWidget(summaryLabel);

    QTextEdit* resultText = new QTextEdit(&dialog);
    resultText->setReadOnly(true);
    resultText->setFont(QFont("Courier New", 9));

    QString content;
    for (const auto& result : results) {
        QString status;
        if (!result.exists) {
            status = "NOT FOUND";
        } else if (!result.errorMessage.isEmpty()) {
            status = "ERROR";
        } else {
            status = "OK";
        }

        QString line = QString("[%1] %2").arg(status, -9).arg(result.path);
        if (!result.errorMessage.isEmpty()) {
            line += QString(" (%1)").arg(result.errorMessage);
        }
        line += "\n";

        if (!result.exists || !result.errorMessage.isEmpty()) {
            content += QString("<span style='color:red;'>%1</span>").arg(line.toHtmlEscaped().replace("\n", "<br>"));
        } else {
            content += line.toHtmlEscaped().replace("\n", "<br>");
        }
    }
    resultText->setHtml(QString("<pre style='white-space: pre-wrap;'>%1</pre>").arg(content));
    layout->addWidget(resultText);

    QPushButton* closeBtn = new QPushButton("Close", &dialog);
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    layout->addWidget(closeBtn);

    dialog.exec();
}

// === Member Mode Implementation ===

void CloudCopierPanel::onDestinationModeChanged() {
    bool memberMode = m_memberDestRadio->isChecked();
    m_memberModeEnabled = memberMode;

    // Show/hide member selection UI
    m_memberSelectionWidget->setVisible(memberMode);

    // Enable/disable manual destination controls
    m_destinationList->setEnabled(!memberMode);
    m_addDestBtn->setEnabled(!memberMode);
    m_pasteDestsBtn->setEnabled(!memberMode);
    m_editDestsBtn->setEnabled(!memberMode);
    m_removeDestBtn->setEnabled(!memberMode);
    m_clearDestsBtn->setEnabled(!memberMode);
    m_validateDestsBtn->setEnabled(!memberMode);

    // Notify controller
    if (m_controller) {
        m_controller->setMemberMode(memberMode);
    }

    updateButtonStates();
}

void CloudCopierPanel::onMemberComboChanged(int index) {
    if (index <= 0 || !m_controller) return;

    // Get member ID from combo data
    QString memberId = m_memberCombo->currentData().toString();
    if (!memberId.isEmpty()) {
        m_allMembersCheck->setChecked(false);
        m_controller->selectMember(memberId);
    }
}

void CloudCopierPanel::onAllMembersCheckChanged(bool checked) {
    if (!m_controller) return;

    m_memberCombo->setEnabled(!checked);
    m_controller->selectAllMembers(checked);
}

void CloudCopierPanel::onTemplatePathChanged() {
    if (!m_controller) return;

    QString templatePath = m_templatePathEdit->text().trimmed();
    m_controller->setDestinationTemplate(templatePath);

    // Clear previous preview
    m_expansionPreviewLabel->hide();
}

void CloudCopierPanel::onPreviewExpansionClicked() {
    if (!m_controller) return;

    // Ensure template is set
    QString templatePath = m_templatePathEdit->text().trimmed();
    if (templatePath.isEmpty()) {
        QMessageBox::warning(this, "Preview", "Please enter a path template first.");
        return;
    }

    m_controller->setDestinationTemplate(templatePath);
    m_controller->previewTemplateExpansion();
}

void CloudCopierPanel::onManageMembersClicked() {
    // Signal to main window to switch to Member Registry panel
    // For now, just show a message
    QMessageBox::information(this, "Manage Members",
        "To manage members, please switch to the Member Registry panel using the sidebar.");
}

void CloudCopierPanel::onVariableHelpClicked() {
    QString helpText = R"(
<h3>Template Variables</h3>
<p>Use these placeholders in your path template:</p>
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
<pre>/Archive/{member}/Updates/{month}/</pre>
<p>For member "Alice" with folder "/Members/Alice":</p>
<pre>/Archive/Members/Alice/Updates/December/</pre>
)";

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Template Variables Help");
    msgBox.setTextFormat(Qt::RichText);
    msgBox.setText(helpText);
    msgBox.setIcon(QMessageBox::Information);
    msgBox.exec();
}

// Member mode controller callbacks

void CloudCopierPanel::onMemberModeChanged(bool enabled) {
    if (enabled) {
        m_memberDestRadio->setChecked(true);
    } else {
        m_manualDestRadio->setChecked(true);
    }
    m_memberModeEnabled = enabled;
    m_memberSelectionWidget->setVisible(enabled);
}

void CloudCopierPanel::onAvailableMembersChanged(const QList<MemberInfo>& members) {
    updateMemberCombo();
    m_memberCountLabel->setText(QString("(%1 available)").arg(members.size()));
}

void CloudCopierPanel::onSelectedMemberChanged(const QString& memberId, const QString& memberName) {
    // Find and select the member in combo
    for (int i = 1; i < m_memberCombo->count(); ++i) {
        if (m_memberCombo->itemData(i).toString() == memberId) {
            m_memberCombo->blockSignals(true);
            m_memberCombo->setCurrentIndex(i);
            m_memberCombo->blockSignals(false);
            break;
        }
    }
}

void CloudCopierPanel::onAllMembersSelectionChanged(bool allSelected) {
    m_allMembersCheck->blockSignals(true);
    m_allMembersCheck->setChecked(allSelected);
    m_allMembersCheck->blockSignals(false);
    m_memberCombo->setEnabled(!allSelected);
}

void CloudCopierPanel::onDestinationTemplateChanged(const QString& templatePath) {
    if (m_templatePathEdit->text() != templatePath) {
        m_templatePathEdit->blockSignals(true);
        m_templatePathEdit->setText(templatePath);
        m_templatePathEdit->blockSignals(false);
    }
}

void CloudCopierPanel::onTemplateExpansionReady(const TemplateExpansionPreview& preview) {
    // Show preview in dialog
    QDialog dialog(this);
    dialog.setWindowTitle("Template Expansion Preview");
    dialog.setMinimumSize(600, 400);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    // Summary
    QLabel* summaryLabel = new QLabel(
        QString("<b>Template:</b> %1<br><b>Results:</b> %2 valid, %3 invalid")
            .arg(preview.templatePath.toHtmlEscaped())
            .arg(preview.validCount)
            .arg(preview.invalidCount),
        &dialog);
    layout->addWidget(summaryLabel);

    // Results list
    QTextEdit* resultText = new QTextEdit(&dialog);
    resultText->setReadOnly(true);
    resultText->setFont(QFont("Courier New", 9));

    QString content;
    for (const auto& member : preview.members) {
        QString status = member.isValid ? "OK" : "ERROR";
        QString line = QString("[%1] %2\n    -> %3\n")
            .arg(status, -5)
            .arg(member.memberName)
            .arg(member.expandedPath);

        if (!member.isValid) {
            line += QString("    Error: %1\n").arg(member.errorMessage);
            content += QString("<span style='color:red;'>%1</span>").arg(line.toHtmlEscaped().replace("\n", "<br>"));
        } else {
            content += line.toHtmlEscaped().replace("\n", "<br>");
        }
    }
    resultText->setHtml(QString("<pre style='white-space: pre-wrap;'>%1</pre>").arg(content));
    layout->addWidget(resultText);

    QPushButton* closeBtn = new QPushButton("Close", &dialog);
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    layout->addWidget(closeBtn);

    dialog.exec();

    // Update preview label with short summary
    if (preview.validCount > 0) {
        m_expansionPreviewLabel->setText(
            QString("Preview: %1 destinations ready").arg(preview.validCount));
        m_expansionPreviewLabel->setStyleSheet("color: #060; font-style: italic;");
    } else {
        m_expansionPreviewLabel->setText("No valid destinations");
        m_expansionPreviewLabel->setStyleSheet("color: #C00; font-style: italic;");
    }
    m_expansionPreviewLabel->show();
}

void CloudCopierPanel::onMemberTaskCreated(int taskId, const QString& source, const QString& dest,
                                            const QString& memberId, const QString& memberName) {
    // Find the task row and add member info to tooltip
    int row = findTaskRow(taskId);
    if (row >= 0) {
        QTableWidgetItem* destItem = m_taskTable->item(row, COL_DESTINATION);
        if (destItem) {
            destItem->setToolTip(QString("Member: %1\nPath: %2").arg(memberName).arg(dest));
        }
    }
}

void CloudCopierPanel::updateMemberCombo() {
    if (!m_controller) return;

    m_memberCombo->blockSignals(true);
    m_memberCombo->clear();
    m_memberCombo->addItem("-- Select Member --");

    QList<MemberInfo> members = m_controller->getAvailableMembers();
    for (const MemberInfo& member : members) {
        m_memberCombo->addItem(member.displayName, member.id);
    }

    m_memberCombo->blockSignals(false);
    m_memberCountLabel->setText(QString("(%1 available)").arg(members.size()));
}

void CloudCopierPanel::updateMemberModeUI() {
    bool enabled = m_memberModeEnabled && !m_isCopying;

    m_memberCombo->setEnabled(enabled && !m_allMembersCheck->isChecked());
    m_allMembersCheck->setEnabled(enabled);
    m_templatePathEdit->setEnabled(enabled);
    m_previewExpansionBtn->setEnabled(enabled);
    m_manageMembersBtn->setEnabled(enabled);
}

} // namespace MegaCustom
