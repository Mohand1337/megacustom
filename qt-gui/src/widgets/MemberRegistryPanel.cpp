#include "MemberRegistryPanel.h"
#include "EmptyStateWidget.h"
#include "utils/MemberRegistry.h"
#include "controllers/FileController.h"
#include "utils/CopyHelper.h"
#include "dialogs/RemoteFolderBrowserDialog.h"
#include "dialogs/WordPressConfigDialog.h"
#include "styles/ThemeManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFormLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QTextEdit>
#include <QScrollArea>
#include <QGroupBox>
#include <QSplitter>
#include <QTabWidget>
#include <QListWidget>
#include <QComboBox>
#include <QDateTime>
#include <QMenu>
#include <QApplication>
#include <QClipboard>

namespace MegaCustom {

MemberRegistryPanel::MemberRegistryPanel(QWidget* parent)
    : QWidget(parent)
    , m_registry(MemberRegistry::instance())
{
    setObjectName("MemberRegistryPanel");
    setupUI();
    refresh();

    // Connect registry signals
    connect(m_registry, &MemberRegistry::membersReloaded, this, &MemberRegistryPanel::refresh);
    connect(m_registry, &MemberRegistry::memberAdded, this, &MemberRegistryPanel::refresh);
    connect(m_registry, &MemberRegistry::memberUpdated, this, &MemberRegistryPanel::refresh);
    connect(m_registry, &MemberRegistry::memberRemoved, this, &MemberRegistryPanel::refresh);
    connect(m_registry, &MemberRegistry::templateChanged, this, &MemberRegistryPanel::refreshTemplate);
    connect(m_registry, &MemberRegistry::groupAdded, this, &MemberRegistryPanel::refresh);
    connect(m_registry, &MemberRegistry::groupUpdated, this, [this]() {
        if (!m_suppressGroupRefresh) refresh();
    });
    connect(m_registry, &MemberRegistry::groupRemoved, this, &MemberRegistryPanel::refresh);
}

void MemberRegistryPanel::setFileController(FileController* controller) {
    m_fileController = controller;
}

void MemberRegistryPanel::setupUI() {
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
    QLabel* titleLabel = new QLabel("Member Registry");
    titleLabel->setObjectName("PanelTitle");
    mainLayout->addWidget(titleLabel);

    // Description
    QLabel* descLabel = new QLabel("Manage members with distribution folders, watermark settings, and contact info for personalized file distribution.");
    descLabel->setObjectName("PanelSubtitle");
    descLabel->setWordWrap(true);
    mainLayout->addWidget(descLabel);

    // Search and Filter bar
    QHBoxLayout* filterLayout = new QHBoxLayout();
    filterLayout->setSpacing(12);

    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("Search members...");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setMinimumWidth(200);
    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(300);
    connect(m_searchDebounce, &QTimer::timeout, this, [this]() {
        onSearchChanged(m_searchEdit->text());
    });
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() { m_searchDebounce->start(); });
    filterLayout->addWidget(m_searchEdit);

    m_activeOnlyCheck = new QCheckBox("Active only");
    m_activeOnlyCheck->setChecked(false);
    connect(m_activeOnlyCheck, &QCheckBox::toggled, this, &MemberRegistryPanel::onFilterChanged);
    filterLayout->addWidget(m_activeOnlyCheck);

    m_withFolderOnlyCheck = new QCheckBox("With folder bound");
    m_withFolderOnlyCheck->setChecked(false);
    connect(m_withFolderOnlyCheck, &QCheckBox::toggled, this, &MemberRegistryPanel::onFilterChanged);
    filterLayout->addWidget(m_withFolderOnlyCheck);

    m_withEmailCheck = new QCheckBox("With email");
    m_withEmailCheck->setChecked(false);
    connect(m_withEmailCheck, &QCheckBox::toggled, this, &MemberRegistryPanel::onFilterChanged);
    filterLayout->addWidget(m_withEmailCheck);

    m_withIpCheck = new QCheckBox("With IP");
    m_withIpCheck->setChecked(false);
    connect(m_withIpCheck, &QCheckBox::toggled, this, &MemberRegistryPanel::onFilterChanged);
    filterLayout->addWidget(m_withIpCheck);

    m_missingWmInfoCheck = new QCheckBox("Missing WM info");
    m_missingWmInfoCheck->setChecked(false);
    m_missingWmInfoCheck->setToolTip("Show members missing email or IP address");
    connect(m_missingWmInfoCheck, &QCheckBox::toggled, this, &MemberRegistryPanel::onFilterChanged);
    filterLayout->addWidget(m_missingWmInfoCheck);

    filterLayout->addStretch();
    mainLayout->addLayout(filterLayout);

    // Tab widget for members table and template config
    QTabWidget* tabWidget = new QTabWidget();

    // === Members Tab ===
    QWidget* membersTab = new QWidget();
    QVBoxLayout* membersLayout = new QVBoxLayout(membersTab);
    membersLayout->setContentsMargins(8, 8, 8, 8);

    // Empty state (shown when no members exist)
    m_emptyState = new EmptyStateWidget(
        ":/icons/users.svg",
        "No members registered",
        "Add members to distribute files, apply watermarks, and manage cloud folders.",
        "Add Member",
        membersTab);
    connect(m_emptyState, &EmptyStateWidget::actionClicked, this, &MemberRegistryPanel::onAddMember);
    membersLayout->addWidget(m_emptyState);

    // Members table with extended columns
    m_memberTable = new QTableWidget();
    m_memberTable->setObjectName("MemberRegistryTable");
    m_memberTable->setColumnCount(9);
    m_memberTable->setHorizontalHeaderLabels({
        "#", "ID", "Display Name", "Email", "Distribution Folder", "WM Fields", "Active", "Groups", "Last Activity"
    });
    m_memberTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_memberTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_memberTable->setAlternatingRowColors(true);
    m_memberTable->verticalHeader()->setVisible(false);
    m_memberTable->setContextMenuPolicy(Qt::CustomContextMenu);

    // Column sizing
    m_memberTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);    // #
    m_memberTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive); // ID
    m_memberTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive); // Name
    m_memberTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive); // Email
    m_memberTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);    // Folder
    m_memberTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Interactive); // WM Fields
    m_memberTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Fixed);      // Active
    m_memberTable->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Interactive); // Groups
    m_memberTable->horizontalHeader()->setSectionResizeMode(8, QHeaderView::Interactive); // Last Activity
    m_memberTable->setColumnWidth(0, 40);
    m_memberTable->setColumnWidth(1, 100);
    m_memberTable->setColumnWidth(2, 120);
    m_memberTable->setColumnWidth(3, 160);
    m_memberTable->setColumnWidth(5, 100);
    m_memberTable->setColumnWidth(6, 60);
    m_memberTable->setColumnWidth(7, 120);
    m_memberTable->setColumnWidth(8, 130);


    // Column header click sorting
    m_memberTable->horizontalHeader()->setSortIndicatorShown(true);
    m_memberTable->horizontalHeader()->setSortIndicator(0, Qt::AscendingOrder);
    connect(m_memberTable->horizontalHeader(), &QHeaderView::sectionClicked,
            this, &MemberRegistryPanel::onSortByColumn);

    connect(m_memberTable, &QTableWidget::itemSelectionChanged,
            this, &MemberRegistryPanel::onTableSelectionChanged);
    connect(m_memberTable, &QTableWidget::cellDoubleClicked,
            this, &MemberRegistryPanel::onTableDoubleClicked);
    connect(m_memberTable, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QString memberId = getSelectedMemberId();
        if (memberId.isEmpty()) return;

        QMenu menu(this);

        // Copy actions
        QTableWidgetItem* clickedItem = m_memberTable->itemAt(pos);
        if (clickedItem) {
            QAction* copyCellAction = menu.addAction("Copy Cell");
            connect(copyCellAction, &QAction::triggered, this, [this, clickedItem]() {
                QApplication::clipboard()->setText(clickedItem->text());
            });
            QAction* copyRowAction = menu.addAction("Copy Row");
            connect(copyRowAction, &QAction::triggered, this, [this, clickedItem]() {
                CopyHelper::copyRow(m_memberTable, clickedItem->row());
            });
            menu.addSeparator();
        }

        menu.addAction("Edit", this, &MemberRegistryPanel::onEditMember);
        menu.addAction("Duplicate...", this, &MemberRegistryPanel::onDuplicateMember);
        menu.addAction("Bind Folder...", this, &MemberRegistryPanel::onBindFolder);
        menu.addAction("Unbind Folder", this, &MemberRegistryPanel::onUnbindFolder);
        menu.addSeparator();

        // Add to Group submenu
        QMenu* groupMenu = menu.addMenu("Add to Group...");
        QStringList groupNames = m_registry->getGroupNames();
        if (groupNames.isEmpty()) {
            groupMenu->addAction("(No groups created)")->setEnabled(false);
        } else {
            for (const QString& gn : groupNames) {
                MemberGroup group = m_registry->getGroup(gn);
                bool inGroup = group.memberIds.contains(memberId);
                QAction* action = groupMenu->addAction(
                    (inGroup ? QString::fromUtf8("\xe2\x9c\x93 ") : QString("  ")) + gn);
                connect(action, &QAction::triggered, this, [this, gn, memberId]() {
                    MemberGroup g = m_registry->getGroup(gn);
                    if (g.memberIds.contains(memberId)) {
                        g.memberIds.removeAll(memberId);
                    } else {
                        g.memberIds.append(memberId);
                    }
                    g.updatedAt = QDateTime::currentSecsSinceEpoch();
                    m_registry->updateGroup(g);
                });
            }
        }

        menu.addSeparator();
        menu.addAction("Delete", this, &MemberRegistryPanel::onRemoveMember);
        menu.exec(m_memberTable->viewport()->mapToGlobal(pos));
    });

    membersLayout->addWidget(m_memberTable, 1);

    // Action buttons row 1 - Main actions
    QHBoxLayout* actionsLayout1 = new QHBoxLayout();
    actionsLayout1->setSpacing(8);

    m_addBtn = new QPushButton("Add Member");
    m_addBtn->setObjectName("PanelPrimaryButton");
    m_addBtn->setIcon(QIcon(":/icons/plus.svg"));
    connect(m_addBtn, &QPushButton::clicked, this, &MemberRegistryPanel::onAddMember);

    m_editBtn = new QPushButton("Edit");
    m_editBtn->setIcon(QIcon(":/icons/edit.svg"));
    m_editBtn->setEnabled(false);
    m_editBtn->setToolTip("Edit the selected member");
    connect(m_editBtn, &QPushButton::clicked, this, &MemberRegistryPanel::onEditMember);

    m_duplicateBtn = new QPushButton("Duplicate");
    m_duplicateBtn->setIcon(QIcon(":/icons/copy.svg"));
    m_duplicateBtn->setEnabled(false);
    m_duplicateBtn->setToolTip("Create a new member from the selected member, with options for copying paths, contact info, watermark settings, and groups");
    connect(m_duplicateBtn, &QPushButton::clicked, this, &MemberRegistryPanel::onDuplicateMember);

    m_removeBtn = new QPushButton("Delete");
    m_removeBtn->setObjectName("PanelDangerButton");
    m_removeBtn->setIcon(QIcon(":/icons/trash-2.svg"));
    m_removeBtn->setEnabled(false);
    m_removeBtn->setToolTip("Delete the selected member from the registry");
    connect(m_removeBtn, &QPushButton::clicked, this, &MemberRegistryPanel::onRemoveMember);

    m_bindFolderBtn = new QPushButton("Bind Folder");
    m_bindFolderBtn->setIcon(QIcon(":/icons/folder.svg"));
    m_bindFolderBtn->setToolTip("Bind a MEGA folder for file distribution");
    m_bindFolderBtn->setEnabled(false);
    connect(m_bindFolderBtn, &QPushButton::clicked, this, &MemberRegistryPanel::onBindFolder);

    m_unbindFolderBtn = new QPushButton("Unbind");
    m_unbindFolderBtn->setToolTip("Remove folder binding");
    m_unbindFolderBtn->setEnabled(false);
    connect(m_unbindFolderBtn, &QPushButton::clicked, this, &MemberRegistryPanel::onUnbindFolder);

    actionsLayout1->addWidget(m_addBtn);
    actionsLayout1->addWidget(m_editBtn);
    actionsLayout1->addWidget(m_duplicateBtn);
    actionsLayout1->addWidget(m_removeBtn);
    actionsLayout1->addWidget(m_bindFolderBtn);
    actionsLayout1->addWidget(m_unbindFolderBtn);
    actionsLayout1->addStretch();

    membersLayout->addLayout(actionsLayout1);

    // Action buttons row 2 - Import/Export
    QHBoxLayout* actionsLayout2 = new QHBoxLayout();
    actionsLayout2->setSpacing(8);

    m_populateBtn = new QPushButton("Populate Defaults");
    m_populateBtn->setToolTip("Populate with default members");
    connect(m_populateBtn, &QPushButton::clicked, this, &MemberRegistryPanel::onPopulateDefaults);

    m_auditBtn = new QPushButton("Audit Members");
    m_auditBtn->setIcon(QIcon(":/icons/search.svg"));
    m_auditBtn->setToolTip("Check the member registry for duplicate identities, missing contact/path data, group issues, and routing risks");
    connect(m_auditBtn, &QPushButton::clicked, this, &MemberRegistryPanel::onAuditMembers);

    m_wpSyncBtn = new QPushButton("WordPress Sync");
    m_wpSyncBtn->setToolTip("Sync members from WordPress via REST API");
    m_wpSyncBtn->setIcon(QIcon(":/icons/cloud.svg"));
    connect(m_wpSyncBtn, &QPushButton::clicked, this, &MemberRegistryPanel::onWordPressSync);

    m_importBtn = new QPushButton("Import JSON");
    m_importBtn->setToolTip("Import members from JSON file");
    connect(m_importBtn, &QPushButton::clicked, this, &MemberRegistryPanel::onImportMembers);

    m_exportBtn = new QPushButton("Export JSON");
    m_exportBtn->setToolTip("Export members to JSON file");
    connect(m_exportBtn, &QPushButton::clicked, this, &MemberRegistryPanel::onExportMembers);

    m_importCsvBtn = new QPushButton("Import CSV");
    m_importCsvBtn->setToolTip("Import members from CSV file");
    connect(m_importCsvBtn, &QPushButton::clicked, this, &MemberRegistryPanel::onImportCsv);

    m_exportCsvBtn = new QPushButton("Export CSV");
    m_exportCsvBtn->setToolTip("Export members to CSV file");
    connect(m_exportCsvBtn, &QPushButton::clicked, this, &MemberRegistryPanel::onExportCsv);

    actionsLayout2->addWidget(m_populateBtn);
    actionsLayout2->addWidget(m_auditBtn);
    actionsLayout2->addWidget(m_wpSyncBtn);
    actionsLayout2->addStretch();
    actionsLayout2->addWidget(m_importCsvBtn);
    actionsLayout2->addWidget(m_exportCsvBtn);
    actionsLayout2->addWidget(m_importBtn);
    actionsLayout2->addWidget(m_exportBtn);

    membersLayout->addLayout(actionsLayout2);

    tabWidget->addTab(membersTab, "Members");

    // === Global Template Tab ===
    QWidget* templateTab = new QWidget();
    QVBoxLayout* templateMainLayout = new QVBoxLayout(templateTab);
    templateMainLayout->setContentsMargins(8, 8, 8, 8);

    QLabel* templateDescLabel = new QLabel("Configure default path types for new members. "
        "Enable/disable path types, edit defaults, or add custom path types.");
    templateDescLabel->setObjectName("PanelSubtitle");
    templateDescLabel->setWordWrap(true);
    templateMainLayout->addWidget(templateDescLabel);

    // Path types grid — will be rebuilt by rebuildPathTypesGrid()
    m_pathTypesWidget = new QWidget();
    templateMainLayout->addWidget(m_pathTypesWidget);
    rebuildPathTypesGrid();

    // Add Path Type + Save Template buttons
    QHBoxLayout* templateBtnLayout = new QHBoxLayout();
    QPushButton* addPathTypeBtn = new QPushButton("Add Path Type");
    addPathTypeBtn->setIcon(QIcon(":/icons/plus.svg"));
    addPathTypeBtn->setToolTip("Add a custom path type");
    connect(addPathTypeBtn, &QPushButton::clicked, this, [this]() {
        // Dialog to add custom path type
        QDialog dialog(this);
        dialog.setWindowTitle("Add Path Type");
        dialog.setMinimumWidth(400);
        QVBoxLayout* dlgLayout = new QVBoxLayout(&dialog);

        QGridLayout* formLayout = new QGridLayout();
        formLayout->addWidget(new QLabel("Key:"), 0, 0);
        QLineEdit* keyEdit = new QLineEdit();
        keyEdit->setPlaceholderText("e.g., customPath");
        formLayout->addWidget(keyEdit, 0, 1);

        formLayout->addWidget(new QLabel("Label:"), 1, 0);
        QLineEdit* labelEdit = new QLineEdit();
        labelEdit->setPlaceholderText("e.g., Custom Path");
        formLayout->addWidget(labelEdit, 1, 1);

        formLayout->addWidget(new QLabel("Default Value:"), 2, 0);
        QLineEdit* valueEdit = new QLineEdit();
        valueEdit->setPlaceholderText("e.g., /some/default/path");
        formLayout->addWidget(valueEdit, 2, 1);

        formLayout->addWidget(new QLabel("Description:"), 3, 0);
        QLineEdit* descEdit = new QLineEdit();
        descEdit->setPlaceholderText("Brief description of this path type");
        formLayout->addWidget(descEdit, 3, 1);

        dlgLayout->addLayout(formLayout);

        QHBoxLayout* btnLayout = new QHBoxLayout();
        btnLayout->addStretch();
        QPushButton* okBtn = new QPushButton("Add");
        QPushButton* cancelBtn = new QPushButton("Cancel");
        btnLayout->addWidget(okBtn);
        btnLayout->addWidget(cancelBtn);
        dlgLayout->addLayout(btnLayout);

        connect(okBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
        connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

        if (dialog.exec() == QDialog::Accepted) {
            QString key = keyEdit->text().trimmed();
            QString label = labelEdit->text().trimmed();
            if (key.isEmpty() || label.isEmpty()) {
                QMessageBox::warning(this, "Error", "Key and Label are required.");
                return;
            }

            // Check for duplicate key
            MemberTemplate tmpl = m_registry->getTemplate();
            for (const PathType& pt : tmpl.pathTypes) {
                if (pt.key == key) {
                    QMessageBox::warning(this, "Error",
                        QString("Path type with key '%1' already exists.").arg(key));
                    return;
                }
            }

            // Add the new path type
            PathType newType;
            newType.key = key;
            newType.label = label;
            newType.defaultValue = valueEdit->text().trimmed();
            newType.description = descEdit->text().trimmed();
            newType.enabled = true;
            tmpl.pathTypes.append(newType);
            m_registry->setTemplate(tmpl);
            rebuildPathTypesGrid();
        }
    });

    QPushButton* saveTemplateBtn = new QPushButton("Save Template");
    saveTemplateBtn->setIcon(QIcon(":/icons/check.svg"));
    saveTemplateBtn->setToolTip("Save changes to the global template");
    connect(saveTemplateBtn, &QPushButton::clicked, this, &MemberRegistryPanel::onSaveTemplate);
    templateBtnLayout->addWidget(addPathTypeBtn);
    templateBtnLayout->addStretch();
    templateBtnLayout->addWidget(saveTemplateBtn);
    templateMainLayout->addLayout(templateBtnLayout);

    templateMainLayout->addStretch();

    tabWidget->addTab(templateTab, "Global Template");

    // === Groups Tab ===
    QWidget* groupsTab = new QWidget();
    QVBoxLayout* groupsLayout = new QVBoxLayout(groupsTab);
    groupsLayout->setContentsMargins(8, 8, 8, 8);

    QLabel* groupsDescLabel = new QLabel(
        "Create named groups of members for quick selection in Watermark and Distribution panels.");
    groupsDescLabel->setObjectName("PanelSubtitle");
    groupsDescLabel->setWordWrap(true);
    groupsLayout->addWidget(groupsDescLabel);

    QSplitter* groupSplitter = new QSplitter(Qt::Horizontal);

    // Left: group list
    QWidget* groupListWidget = new QWidget();
    QVBoxLayout* groupListLayout = new QVBoxLayout(groupListWidget);
    groupListLayout->setContentsMargins(0, 0, 0, 0);

    QLabel* groupListLabel = new QLabel("Groups");
    groupListLabel->setObjectName("SectionHeader");
    groupListLayout->addWidget(groupListLabel);

    m_groupList = new QListWidget();
    m_groupList->setObjectName("GroupList");
    m_groupList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_groupList, &QListWidget::currentRowChanged,
            this, &MemberRegistryPanel::onGroupSelectionChanged);
    connect(m_groupList, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (m_groupList->currentRow() < 0) return;
        QMenu menu(this);
        menu.addAction("Rename", this, &MemberRegistryPanel::onRenameGroup);
        menu.addAction("Duplicate", this, &MemberRegistryPanel::onDuplicateGroup);
        menu.addSeparator();
        menu.addAction("Delete", this, &MemberRegistryPanel::onDeleteGroup);
        menu.exec(m_groupList->viewport()->mapToGlobal(pos));
    });
    groupListLayout->addWidget(m_groupList, 1);

    QHBoxLayout* groupBtnsLayout = new QHBoxLayout();
    groupBtnsLayout->setSpacing(4);

    m_addGroupBtn = new QPushButton("+ New");
    connect(m_addGroupBtn, &QPushButton::clicked, this, &MemberRegistryPanel::onAddGroup);

    m_renameGroupBtn = new QPushButton("Rename");
    m_renameGroupBtn->setEnabled(false);
    connect(m_renameGroupBtn, &QPushButton::clicked, this, &MemberRegistryPanel::onRenameGroup);

    m_deleteGroupBtn = new QPushButton("Delete");
    m_deleteGroupBtn->setObjectName("PanelDangerButton");
    m_deleteGroupBtn->setEnabled(false);
    connect(m_deleteGroupBtn, &QPushButton::clicked, this, &MemberRegistryPanel::onDeleteGroup);

    m_duplicateGroupBtn = new QPushButton("Duplicate");
    m_duplicateGroupBtn->setEnabled(false);
    connect(m_duplicateGroupBtn, &QPushButton::clicked, this, &MemberRegistryPanel::onDuplicateGroup);

    groupBtnsLayout->addWidget(m_addGroupBtn);
    groupBtnsLayout->addWidget(m_renameGroupBtn);
    groupBtnsLayout->addWidget(m_duplicateGroupBtn);
    groupBtnsLayout->addWidget(m_deleteGroupBtn);
    groupListLayout->addLayout(groupBtnsLayout);

    groupSplitter->addWidget(groupListWidget);

    // Right: members in group
    QWidget* groupMemberWidget = new QWidget();
    QVBoxLayout* groupMemberLayout = new QVBoxLayout(groupMemberWidget);
    groupMemberLayout->setContentsMargins(0, 0, 0, 0);

    QLabel* groupMemberLabel = new QLabel("Members in group:");
    groupMemberLabel->setObjectName("SectionHeader");
    groupMemberLayout->addWidget(groupMemberLabel);

    m_groupSearchEdit = new QLineEdit();
    m_groupSearchEdit->setPlaceholderText("Search members...");
    m_groupSearchEdit->setClearButtonEnabled(true);
    m_groupSearchDebounce = new QTimer(this);
    m_groupSearchDebounce->setSingleShot(true);
    m_groupSearchDebounce->setInterval(300);
    connect(m_groupSearchDebounce, &QTimer::timeout, this, [this]() {
        onGroupSearchChanged(m_groupSearchEdit->text());
    });
    connect(m_groupSearchEdit, &QLineEdit::textChanged,
            this, [this]() { m_groupSearchDebounce->start(); });
    groupMemberLayout->addWidget(m_groupSearchEdit);

    m_groupMemberList = new QListWidget();
    m_groupMemberList->setObjectName("GroupMemberList");
    connect(m_groupMemberList, &QListWidget::itemChanged,
            this, &MemberRegistryPanel::onGroupMemberToggled);
    groupMemberLayout->addWidget(m_groupMemberList, 1);

    QHBoxLayout* groupMemberBtnsLayout = new QHBoxLayout();
    groupMemberBtnsLayout->setSpacing(4);

    m_groupSelectAllBtn = new QPushButton("Select All");
    m_groupSelectAllBtn->setEnabled(false);
    connect(m_groupSelectAllBtn, &QPushButton::clicked, this, &MemberRegistryPanel::onGroupSelectAll);

    m_groupDeselectAllBtn = new QPushButton("Deselect All");
    m_groupDeselectAllBtn->setEnabled(false);
    connect(m_groupDeselectAllBtn, &QPushButton::clicked, this, &MemberRegistryPanel::onGroupDeselectAll);

    groupMemberBtnsLayout->addWidget(m_groupSelectAllBtn);
    groupMemberBtnsLayout->addWidget(m_groupDeselectAllBtn);
    groupMemberBtnsLayout->addStretch();
    groupMemberLayout->addLayout(groupMemberBtnsLayout);

    groupSplitter->addWidget(groupMemberWidget);
    groupSplitter->setSizes({250, 400});

    groupsLayout->addWidget(groupSplitter, 1);

    m_groupStatsLabel = new QLabel();
    m_groupStatsLabel->setProperty("type", "secondary");
    groupsLayout->addWidget(m_groupStatsLabel);

    tabWidget->addTab(groupsTab, "Groups");

    mainLayout->addWidget(tabWidget, 1);

    // Stats
    m_statsLabel = new QLabel();
    m_statsLabel->setProperty("type", "secondary");
    CopyHelper::makeSelectable(m_statsLabel);
    mainLayout->addWidget(m_statsLabel);

    scrollArea->setWidget(contentWidget);
    outerLayout->addWidget(scrollArea);
}

void MemberRegistryPanel::refresh() {
    populateTable();
    updateEmptyState();
    refreshGroups();

    // Update stats
    QList<MemberInfo> all = m_registry->getAllMembers();
    QList<MemberInfo> active = m_registry->getActiveMembers();
    QList<MemberInfo> withFolder = m_registry->getMembersWithDistributionFolders();
    int withEmailCount = 0, withIpCount = 0;
    for (const MemberInfo& m : all) {
        if (!m.email.isEmpty()) withEmailCount++;
        if (!m.ipAddress.isEmpty()) withIpCount++;
    }
    m_statsLabel->setText(
        QString("Total: %1 | %2 active | %3 with folders | %4 with email | %5 with IP")
            .arg(all.size()).arg(active.size()).arg(withFolder.size())
            .arg(withEmailCount).arg(withIpCount));
}

void MemberRegistryPanel::refreshTemplate() {
    rebuildPathTypesGrid();
}

void MemberRegistryPanel::rebuildPathTypesGrid() {
    // Clear existing layout
    QLayout* oldLayout = m_pathTypesWidget->layout();
    if (oldLayout) {
        QLayoutItem* item;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
        delete oldLayout;
    }
    m_pathTypeChecks.clear();
    m_pathTypeEdits.clear();

    QGridLayout* pathTypesGrid = new QGridLayout(m_pathTypesWidget);
    pathTypesGrid->setSpacing(8);

    // Headers
    QLabel* enabledHeader = new QLabel("Enabled");
    enabledHeader->setObjectName("SectionHeader");
    QLabel* typeHeader = new QLabel("Path Type");
    typeHeader->setObjectName("SectionHeader");
    QLabel* defaultHeader = new QLabel("Default Value");
    defaultHeader->setObjectName("SectionHeader");
    QLabel* actionHeader = new QLabel("");

    pathTypesGrid->addWidget(enabledHeader, 0, 0);
    pathTypesGrid->addWidget(typeHeader, 0, 1);
    pathTypesGrid->addWidget(defaultHeader, 0, 2);
    pathTypesGrid->addWidget(actionHeader, 0, 3);

    // Predefined keys that cannot be deleted
    static const QStringList predefinedKeys = {
        "archiveRoot", "nhbCallsPath", "fastForwardPath", "theoryCallsPath", "hotSeatsPath"
    };

    MemberTemplate tmpl = m_registry->getTemplate();

    int row = 1;
    for (const PathType& pt : tmpl.pathTypes) {
        QCheckBox* enableCheck = new QCheckBox();
        enableCheck->setChecked(pt.enabled);
        enableCheck->setProperty("pathKey", pt.key);
        pathTypesGrid->addWidget(enableCheck, row, 0, Qt::AlignCenter);

        QLabel* typeLabel = new QLabel(pt.label);
        typeLabel->setToolTip(pt.description);
        pathTypesGrid->addWidget(typeLabel, row, 1);

        QLineEdit* valueEdit = new QLineEdit(pt.defaultValue);
        valueEdit->setProperty("pathKey", pt.key);
        valueEdit->setEnabled(pt.enabled);
        pathTypesGrid->addWidget(valueEdit, row, 2);

        connect(enableCheck, &QCheckBox::toggled, valueEdit, &QLineEdit::setEnabled);

        m_pathTypeChecks[pt.key] = enableCheck;
        m_pathTypeEdits[pt.key] = valueEdit;

        // Delete button for custom (non-predefined) types only
        if (!predefinedKeys.contains(pt.key)) {
            QPushButton* deleteBtn = new QPushButton();
            deleteBtn->setIcon(QIcon(":/icons/x.svg"));
            deleteBtn->setFixedSize(24, 24);
            deleteBtn->setToolTip(QString("Delete '%1' path type").arg(pt.label));
            deleteBtn->setProperty("pathKey", pt.key);
            connect(deleteBtn, &QPushButton::clicked, this, [this, key = pt.key, label = pt.label]() {
                int ret = QMessageBox::question(this, "Delete Path Type",
                    QString("Delete custom path type '%1'?").arg(label),
                    QMessageBox::Yes | QMessageBox::No);
                if (ret == QMessageBox::Yes) {
                    MemberTemplate tmpl = m_registry->getTemplate();
                    for (int i = 0; i < tmpl.pathTypes.size(); ++i) {
                        if (tmpl.pathTypes[i].key == key) {
                            tmpl.pathTypes.removeAt(i);
                            break;
                        }
                    }
                    m_registry->setTemplate(tmpl);
                    rebuildPathTypesGrid();
                }
            });
            pathTypesGrid->addWidget(deleteBtn, row, 3, Qt::AlignCenter);
        }

        row++;
    }

    pathTypesGrid->setColumnStretch(2, 1);
}

void MemberRegistryPanel::onSaveTemplate() {
    MemberTemplate tmpl = m_registry->getTemplate();

    // Update path types from UI
    for (PathType& pt : tmpl.pathTypes) {
        if (m_pathTypeChecks.contains(pt.key)) {
            pt.enabled = m_pathTypeChecks[pt.key]->isChecked();
        }
        if (m_pathTypeEdits.contains(pt.key)) {
            pt.defaultValue = m_pathTypeEdits[pt.key]->text();
        }
    }

    m_registry->setTemplate(tmpl);
    QMessageBox::information(this, "Template Saved", "Global template has been saved.");
}

void MemberRegistryPanel::populateTable() {
    m_memberTable->setRowCount(0);

    // Get filtered members
    QString searchText = m_searchEdit->text();
    bool activeOnly = m_activeOnlyCheck->isChecked();
    bool withFolderOnly = m_withFolderOnlyCheck->isChecked();
    bool withEmail = m_withEmailCheck->isChecked();
    bool withIp = m_withIpCheck->isChecked();
    bool missingWmInfo = m_missingWmInfoCheck->isChecked();

    QList<MemberInfo> members = m_registry->filterMembers(
        searchText, activeOnly, withFolderOnly, withEmail, withIp, missingWmInfo);

    // Apply column sorting
    if (m_sortColumn >= 0) {
        auto* registry = m_registry;
        std::sort(members.begin(), members.end(),
            [this, registry](const MemberInfo& a, const MemberInfo& b) {
                int cmp = 0;
                switch (m_sortColumn) {
                    case 0: cmp = a.sortOrder - b.sortOrder; break;
                    case 1: cmp = a.id.compare(b.id, Qt::CaseInsensitive); break;
                    case 2: cmp = a.displayName.compare(b.displayName, Qt::CaseInsensitive); break;
                    case 3: cmp = a.email.compare(b.email, Qt::CaseInsensitive); break;
                    case 4: cmp = a.distributionFolder.compare(b.distributionFolder, Qt::CaseInsensitive); break;
                    case 5: cmp = a.watermarkFields.join(",").compare(
                                b.watermarkFields.join(","), Qt::CaseInsensitive); break;
                    case 6: cmp = (a.active ? 1 : 0) - (b.active ? 1 : 0); break;
                    case 7: {
                        QString ga = registry->getGroupsForMember(a.id).join(",");
                        QString gb = registry->getGroupsForMember(b.id).join(",");
                        cmp = ga.compare(gb, Qt::CaseInsensitive);
                        break;
                    }
                    case 8: {
                        // Sort by most recent activity date
                        QString da = a.pipelineStatus.lastWatermarkDate > a.pipelineStatus.lastDistributionDate
                                     ? a.pipelineStatus.lastWatermarkDate : a.pipelineStatus.lastDistributionDate;
                        QString db = b.pipelineStatus.lastWatermarkDate > b.pipelineStatus.lastDistributionDate
                                     ? b.pipelineStatus.lastWatermarkDate : b.pipelineStatus.lastDistributionDate;
                        cmp = da.compare(db, Qt::CaseInsensitive);
                        break;
                    }
                    default: cmp = a.sortOrder - b.sortOrder; break;
                }
                return m_sortAscending ? cmp < 0 : cmp > 0;
            });
    }

    m_memberTable->setRowCount(members.size());

    auto& tm = ThemeManager::instance();

    for (int row = 0; row < members.size(); ++row) {
        const MemberInfo& m = members[row];

        // Sort order
        QTableWidgetItem* orderItem = new QTableWidgetItem(QString::number(m.sortOrder));
        orderItem->setData(Qt::UserRole, m.id);
        orderItem->setTextAlignment(Qt::AlignCenter);
        m_memberTable->setItem(row, 0, orderItem);

        // ID
        QTableWidgetItem* idItem = new QTableWidgetItem(m.id);
        m_memberTable->setItem(row, 1, idItem);

        // Display Name
        QTableWidgetItem* nameItem = new QTableWidgetItem(m.displayName);
        m_memberTable->setItem(row, 2, nameItem);

        // Email
        QTableWidgetItem* emailItem = new QTableWidgetItem(m.email);
        if (m.email.isEmpty()) {
            emailItem->setForeground(tm.textSecondary());
            emailItem->setText("-");
        }
        m_memberTable->setItem(row, 3, emailItem);

        // Distribution Folder
        QTableWidgetItem* folderItem = new QTableWidgetItem();
        if (m.hasDistributionFolder()) {
            folderItem->setText(m.distributionFolder);
            folderItem->setIcon(QIcon(":/icons/folder.svg"));
            folderItem->setForeground(tm.supportSuccess());
        } else {
            folderItem->setText("Not bound");
            folderItem->setForeground(tm.textSecondary());
        }
        m_memberTable->setItem(row, 4, folderItem);

        // Watermark Fields
        QTableWidgetItem* wmItem = new QTableWidgetItem();
        if (m.useGlobalWatermark) {
            wmItem->setText("Global");
            wmItem->setForeground(tm.supportWarning());
        } else if (!m.watermarkFields.isEmpty()) {
            wmItem->setText(m.watermarkFields.join(", "));
        } else {
            wmItem->setText("Default");
            wmItem->setForeground(tm.textSecondary());
        }
        m_memberTable->setItem(row, 5, wmItem);

        // Active
        QTableWidgetItem* activeItem = new QTableWidgetItem(m.active ? "Yes" : "No");
        activeItem->setTextAlignment(Qt::AlignCenter);
        if (!m.active) {
            activeItem->setForeground(tm.textSecondary());
        }
        m_memberTable->setItem(row, 6, activeItem);

        // Groups
        QTableWidgetItem* groupsItem = new QTableWidgetItem();
        QStringList memberGroups = m_registry->getGroupsForMember(m.id);
        if (memberGroups.isEmpty()) {
            groupsItem->setText("-");
            groupsItem->setForeground(tm.textSecondary());
        } else {
            groupsItem->setText(memberGroups.join(", "));
            groupsItem->setForeground(tm.supportInfo());
        }
        m_memberTable->setItem(row, 7, groupsItem);

        // Last Activity
        QTableWidgetItem* activityItem = new QTableWidgetItem();
        const auto& ps = m.pipelineStatus;
        // Show whichever is more recent
        QString lastDate = ps.lastDistributionDate.isEmpty() ? ps.lastWatermarkDate
            : (ps.lastWatermarkDate.isEmpty() ? ps.lastDistributionDate
               : (ps.lastDistributionDate > ps.lastWatermarkDate ? ps.lastDistributionDate : ps.lastWatermarkDate));
        if (lastDate.isEmpty()) {
            activityItem->setText("-");
            activityItem->setForeground(tm.textSecondary());
        } else {
            activityItem->setText(lastDate);
            activityItem->setToolTip(
                QString("WM: %1 (%2 files)\nDist: %3 (%4 files)")
                    .arg(ps.lastWatermarkDate.isEmpty() ? "never" : ps.lastWatermarkDate)
                    .arg(ps.watermarkCount)
                    .arg(ps.lastDistributionDate.isEmpty() ? "never" : ps.lastDistributionDate)
                    .arg(ps.distributionCount));
        }
        m_memberTable->setItem(row, 8, activityItem);
    }
}

void MemberRegistryPanel::updateEmptyState() {
    bool empty = m_registry->getAllMembers().isEmpty();
    m_emptyState->setVisible(empty);
    m_memberTable->setVisible(!empty);
}

QString MemberRegistryPanel::getSelectedMemberId() const {
    int row = m_memberTable->currentRow();
    if (row < 0) return QString();
    QTableWidgetItem* item = m_memberTable->item(row, 0);
    if (!item) return QString();
    return item->data(Qt::UserRole).toString();
}

void MemberRegistryPanel::selectMemberById(const QString& memberId) {
    for (int row = 0; row < m_memberTable->rowCount(); ++row) {
        QTableWidgetItem* item = m_memberTable->item(row, 0);
        if (item && item->data(Qt::UserRole).toString() == memberId) {
            m_memberTable->selectRow(row);
            m_memberTable->scrollToItem(item, QAbstractItemView::PositionAtCenter);
            return;
        }
    }
}

QList<QStringList> MemberRegistryPanel::buildMemberAuditRows(int* errorCount,
                                                             int* warningCount,
                                                             int* infoCount) const {
    int errors = 0;
    int warnings = 0;
    int infos = 0;
    QList<QStringList> rows;

    auto addFinding = [&](const QString& severity,
                          const QString& area,
                          const QString& subject,
                          const QString& problem,
                          const QString& fix) {
        rows.append({severity, area, subject, problem, fix});
        if (severity == "Error") errors++;
        else if (severity == "Warning") warnings++;
        else infos++;
    };

    auto cleanKey = [](const QString& value) {
        return value.trimmed().toLower();
    };
    auto pathKey = [](QString value) {
        value = value.trimmed();
        while (value.endsWith('/') && value.size() > 1) value.chop(1);
        return value.toLower();
    };
    auto addDuplicateFindings = [&](const QString& area,
                                    const QMap<QString, QStringList>& buckets,
                                    const QString& problemPrefix,
                                    const QString& fix,
                                    const QString& severity = "Warning") {
        for (auto it = buckets.constBegin(); it != buckets.constEnd(); ++it) {
            QStringList ids = it.value();
            ids.removeDuplicates();
            if (ids.size() > 1) {
                addFinding(severity, area, ids.join(", "),
                    QString("%1: %2").arg(problemPrefix, it.key()), fix);
            }
        }
    };

    const QList<MemberInfo> members = m_registry->getAllMembers();
    const QList<MemberGroup> groups = m_registry->getAllGroups();

    QMap<QString, QStringList> idCaseBuckets;
    QMap<QString, QStringList> displayNameBuckets;
    QMap<QString, QStringList> emailBuckets;
    QMap<QString, QStringList> emailPrefixBuckets;
    QMap<QString, QStringList> wpBuckets;
    QMap<QString, QStringList> directFolderBuckets;
    QMap<QString, QStringList> archiveRootBuckets;
    QMap<QString, QStringList> wmPatternBuckets;
    QMap<int, QStringList> sortOrderBuckets;
    struct MatchKey {
        QString memberId;
        QString key;
        QString label;
    };
    QList<MatchKey> matchKeys;

    auto addMatchKey = [&](const QString& memberId, const QString& key, const QString& label) {
        const QString normalized = cleanKey(key);
        if (normalized.length() >= 3) {
            matchKeys.append({memberId, normalized, label});
        }
    };

    for (const MemberInfo& member : members) {
        const QString subject = member.id;
        idCaseBuckets[cleanKey(member.id)].append(member.id);
        addMatchKey(member.id, member.id, "member ID");
        if (member.sortOrder > 0) sortOrderBuckets[member.sortOrder].append(member.id);

        const QString nameKey = cleanKey(member.displayName);
        if (!nameKey.isEmpty()) {
            displayNameBuckets[nameKey].append(member.id);
            addMatchKey(member.id, nameKey, "display name");
        }
        const QString emailKey = cleanKey(member.email);
        if (!emailKey.isEmpty()) {
            emailBuckets[emailKey].append(member.id);
            const QString emailPrefix = emailKey.section('@', 0, 0);
            emailPrefixBuckets[emailPrefix].append(member.id);
            addMatchKey(member.id, emailPrefix, "email prefix");
        }
        const QString wpKey = cleanKey(member.wpUserId);
        if (!wpKey.isEmpty()) wpBuckets[wpKey].append(member.id);
        const QString folderKey = pathKey(member.distributionFolder);
        if (!folderKey.isEmpty()) directFolderBuckets[folderKey].append(member.id);
        const QString archiveKey = pathKey(member.paths.archiveRoot);
        if (!archiveKey.isEmpty()) archiveRootBuckets[archiveKey].append(member.id);
        const QString wmKey = cleanKey(member.wmFolderPattern);
        if (!wmKey.isEmpty()) wmPatternBuckets[wmKey].append(member.id);

        if (member.id.trimmed().isEmpty()) {
            addFinding("Error", "Identity", subject, "Member ID is empty", "Set a unique member ID.");
        }
        if (member.displayName.trimmed().isEmpty()) {
            addFinding("Warning", "Identity", subject, "Display name is empty", "Add a readable display name.");
        }
        if (member.active && !member.hasDistributionFolder() && member.paths.archiveRoot.isEmpty()) {
            addFinding("Error", "Paths", subject,
                "Active member has no direct distribution folder and no archive root",
                "Bind a folder or set archive paths before distribution.");
        }
        if (!member.distributionFolder.isEmpty() && !member.distributionFolder.startsWith('/')) {
            addFinding("Warning", "Paths", subject,
                "Direct distribution folder does not start with /",
                "Use an absolute MEGA path.");
        }
        if (!member.paths.archiveRoot.isEmpty() && !member.paths.archiveRoot.startsWith('/')) {
            addFinding("Warning", "Paths", subject,
                "Archive root does not start with /",
                "Use an absolute MEGA path.");
        }
        if (!member.paths.hotSeatsPath.isEmpty() && member.paths.fastForwardPath.isEmpty()) {
            addFinding("Warning", "Paths", subject,
                "Hot Seats path is set but Fast Forward path is empty",
                "Set Fast Forward path or clear Hot Seats path.");
        }
        if (!member.paths.theoryCallsPath.isEmpty() && member.paths.fastForwardPath.isEmpty()) {
            addFinding("Warning", "Paths", subject,
                "Theory Calls path is set but Fast Forward path is empty",
                "Set Fast Forward path or clear Theory Calls path.");
        }
        if (member.active && member.email.trimmed().isEmpty() && !member.useGlobalWatermark) {
            addFinding("Warning", "Watermark", subject,
                "Active member has no email for personalized watermarking",
                "Add email or enable global watermark only.");
        }
        if (member.active && member.ipAddress.trimmed().isEmpty() && !member.useGlobalWatermark) {
            addFinding("Warning", "Watermark", subject,
                "Active member has no IP address for personalized watermarking",
                "Add IP address or enable global watermark only.");
        }
        if (member.useGlobalWatermark && (!member.watermarkFields.isEmpty())) {
            addFinding("Info", "Watermark", subject,
                "Global watermark is enabled, so personalized watermark fields are ignored",
                "Clear fields or disable global watermark if personalization is expected.");
        }
        if (!member.active && member.hasDistributionFolder()) {
            addFinding("Info", "Status", subject,
                "Inactive member still has a bound distribution folder",
                "This is okay if intentional; otherwise unbind or reactivate.");
        }
        if (member.active && member.wmFolderPattern.trimmed().isEmpty()) {
            addFinding("Info", "Matching", subject,
                "Active member has no explicit WM folder pattern",
                "Add a pattern when folder names differ from member ID/display name.");
        }
        if (member.active && m_registry->getGroupsForMember(member.id).isEmpty()) {
            addFinding("Info", "Groups", subject,
                "Active member is not in any saved group",
                "Add to NHB+/FF/etc. groups if group-based workflows should include this member.");
        }
    }

    int collisionWarnings = 0;
    for (int i = 0; i < matchKeys.size(); ++i) {
        for (int j = i + 1; j < matchKeys.size(); ++j) {
            const MatchKey& a = matchKeys[i];
            const MatchKey& b = matchKeys[j];
            if (a.memberId == b.memberId || a.key == b.key) continue;

            const bool aInsideB = b.key.contains(a.key);
            const bool bInsideA = a.key.contains(b.key);
            if (!aInsideB && !bInsideA) continue;

            const MatchKey& shorter = a.key.length() <= b.key.length() ? a : b;
            const MatchKey& longer = a.key.length() <= b.key.length() ? b : a;
            addFinding("Warning", "Matching",
                QString("%1, %2").arg(shorter.memberId, longer.memberId),
                QString("%1 '%2' is contained in %3 '%4'")
                    .arg(shorter.label, shorter.key, longer.label, longer.key),
                "Use explicit WM folder patterns to prevent fuzzy matching the wrong member.");
            if (++collisionWarnings >= 40) {
                addFinding("Info", "Matching", "Audit",
                    "Additional fuzzy-match collision warnings were suppressed",
                    "Fix the listed patterns first, then run audit again.");
                i = matchKeys.size();
                break;
            }
        }
    }

    addDuplicateFindings("Identity", idCaseBuckets,
        "Member IDs differ only by case", "Rename one member ID to avoid ambiguous matching.", "Error");
    addDuplicateFindings("Identity", displayNameBuckets,
        "Duplicate display name", "Use distinct display names or add notes explaining the duplicate.");
    addDuplicateFindings("Identity", emailBuckets,
        "Duplicate email", "Confirm whether these are the same person or split the contact details.", "Error");
    addDuplicateFindings("Matching", emailPrefixBuckets,
        "Duplicate email username prefix used for folder matching",
        "Use explicit WM folder patterns to avoid matching the wrong member.");
    addDuplicateFindings("WordPress", wpBuckets,
        "Duplicate WordPress user ID", "Keep one registry member per WordPress user ID unless intentional.", "Error");
    addDuplicateFindings("Paths", directFolderBuckets,
        "Duplicate direct distribution folder", "Bind each member to a unique destination folder.", "Error");
    addDuplicateFindings("Paths", archiveRootBuckets,
        "Duplicate archive root", "Set a unique archive root for each member.", "Error");
    addDuplicateFindings("Matching", wmPatternBuckets,
        "Duplicate WM folder pattern", "Use distinct patterns so watermark folders match one member.");

    for (auto it = sortOrderBuckets.constBegin(); it != sortOrderBuckets.constEnd(); ++it) {
        if (it.value().size() > 1) {
            addFinding("Info", "Ordering", it.value().join(", "),
                QString("Members share sort order %1").arg(it.key()),
                "Adjust sort order if display order matters.");
        }
    }

    for (const MemberGroup& group : groups) {
        QStringList seen;
        QStringList duplicateIds;
        QStringList missingIds;
        QStringList inactiveIds;
        for (const QString& memberId : group.memberIds) {
            if (seen.contains(memberId)) duplicateIds.append(memberId);
            seen.append(memberId);
            if (!m_registry->hasMember(memberId)) {
                missingIds.append(memberId);
                continue;
            }
            if (!m_registry->getMember(memberId).active) {
                inactiveIds.append(memberId);
            }
        }
        duplicateIds.removeDuplicates();
        missingIds.removeDuplicates();
        inactiveIds.removeDuplicates();

        if (group.name.trimmed().isEmpty()) {
            addFinding("Error", "Groups", "(blank group)", "Group name is empty", "Rename or delete the group.");
        }
        if (group.memberIds.isEmpty()) {
            addFinding("Info", "Groups", group.name, "Group has no members", "Add members or delete the group.");
        }
        if (!duplicateIds.isEmpty()) {
            addFinding("Warning", "Groups", group.name,
                QString("Group contains duplicate member IDs: %1").arg(duplicateIds.join(", ")),
                "Open the group and re-save membership.");
        }
        if (!missingIds.isEmpty()) {
            addFinding("Error", "Groups", group.name,
                QString("Group references missing members: %1").arg(missingIds.join(", ")),
                "Remove missing IDs or recreate those members.");
        }
        if (!inactiveIds.isEmpty()) {
            addFinding("Info", "Groups", group.name,
                QString("Group contains inactive members: %1").arg(inactiveIds.join(", ")),
                "Remove them from the group if they should not receive files.");
        }
    }

    if (errorCount) *errorCount = errors;
    if (warningCount) *warningCount = warnings;
    if (infoCount) *infoCount = infos;
    return rows;
}

void MemberRegistryPanel::onTableSelectionChanged() {
    QString memberId = getSelectedMemberId();
    bool hasSelection = !memberId.isEmpty();

    m_editBtn->setEnabled(hasSelection);
    m_duplicateBtn->setEnabled(hasSelection);
    m_removeBtn->setEnabled(hasSelection);
    m_bindFolderBtn->setEnabled(hasSelection);

    if (hasSelection) {
        MemberInfo member = m_registry->getMember(memberId);
        m_unbindFolderBtn->setEnabled(member.hasDistributionFolder());
        emit memberSelected(memberId);
    } else {
        m_unbindFolderBtn->setEnabled(false);
    }
}

void MemberRegistryPanel::onTableDoubleClicked(int row, int column) {
    Q_UNUSED(column);
    if (row >= 0) {
        onEditMember();
    }
}

void MemberRegistryPanel::onSearchChanged(const QString& /*text*/) {
    populateTable();
}

void MemberRegistryPanel::onFilterChanged() {
    populateTable();
}

void MemberRegistryPanel::onSortByColumn(int column) {
    if (m_sortColumn == column) {
        m_sortAscending = !m_sortAscending;
    } else {
        m_sortColumn = column;
        m_sortAscending = true;
    }
    m_memberTable->horizontalHeader()->setSortIndicator(
        column, m_sortAscending ? Qt::AscendingOrder : Qt::DescendingOrder);
    populateTable();
}

void MemberRegistryPanel::showMemberEditDialog(const MemberInfo& member, bool isNew) {
    QDialog dialog(this);
    dialog.setWindowTitle(isNew ? "Add Member" : QString("Edit Member: %1").arg(member.displayName));
    dialog.setMinimumWidth(700);
    dialog.setMinimumHeight(600);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    // Use tabs for organization
    QTabWidget* tabs = new QTabWidget();

    // === Basic Info Tab ===
    QWidget* basicTab = new QWidget();
    QFormLayout* basicForm = new QFormLayout(basicTab);
    basicForm->setSpacing(8);

    QLineEdit* idEdit = new QLineEdit(member.id);
    idEdit->setPlaceholderText("e.g., EGB001 or icekkk");
    idEdit->setEnabled(isNew); // Can't edit ID after creation
    basicForm->addRow("Member ID:", idEdit);

    QLineEdit* nameEdit = new QLineEdit(member.displayName);
    nameEdit->setPlaceholderText("e.g., John Smith");
    basicForm->addRow("Display Name:", nameEdit);

    QSpinBox* orderSpin = new QSpinBox();
    orderSpin->setRange(1, 999);
    orderSpin->setValue(member.sortOrder > 0 ? member.sortOrder : m_registry->getAllMembers().size() + 1);
    basicForm->addRow("Sort Order:", orderSpin);

    QCheckBox* activeCheck = new QCheckBox();
    activeCheck->setChecked(member.active);
    basicForm->addRow("Active:", activeCheck);

    QTextEdit* notesEdit = new QTextEdit();
    notesEdit->setMaximumHeight(60);
    notesEdit->setText(member.notes);
    notesEdit->setPlaceholderText("Optional notes about this member...");
    basicForm->addRow("Notes:", notesEdit);

    tabs->addTab(basicTab, "Basic Info");

    // === Contact Info Tab ===
    QWidget* contactTab = new QWidget();
    QFormLayout* contactForm = new QFormLayout(contactTab);
    contactForm->setSpacing(8);

    QLineEdit* emailEdit = new QLineEdit(member.email);
    emailEdit->setPlaceholderText("member@example.com");
    contactForm->addRow("Email:", emailEdit);

    QLineEdit* ipEdit = new QLineEdit(member.ipAddress);
    ipEdit->setPlaceholderText("192.168.1.1");
    contactForm->addRow("IP Address:", ipEdit);

    QLineEdit* macEdit = new QLineEdit(member.macAddress);
    macEdit->setPlaceholderText("AA:BB:CC:DD:EE:FF");
    contactForm->addRow("MAC Address:", macEdit);

    QLineEdit* socialEdit = new QLineEdit(member.socialHandle);
    socialEdit->setPlaceholderText("@username");
    contactForm->addRow("Social Handle:", socialEdit);

    tabs->addTab(contactTab, "Contact Info");

    // === Watermark Config Tab ===
    QWidget* wmTab = new QWidget();
    QVBoxLayout* wmLayout = new QVBoxLayout(wmTab);

    QCheckBox* useGlobalWmCheck = new QCheckBox("Use global watermark only (no personalization)");
    useGlobalWmCheck->setChecked(member.useGlobalWatermark);
    wmLayout->addWidget(useGlobalWmCheck);

    QGroupBox* wmFieldsGroup = new QGroupBox("Watermark Fields");
    QVBoxLayout* wmFieldsLayout = new QVBoxLayout(wmFieldsGroup);

    QLabel* wmFieldsLabel = new QLabel("Select which fields to include in personalized watermarks:");
    wmFieldsLabel->setProperty("type", "secondary");
    wmFieldsLayout->addWidget(wmFieldsLabel);

    QMap<QString, QCheckBox*> wmFieldChecks;
    QStringList availableFields = MemberRegistry::availableWatermarkFields();
    for (const QString& field : availableFields) {
        QCheckBox* check = new QCheckBox(field);
        check->setChecked(member.watermarkFields.contains(field));
        wmFieldsLayout->addWidget(check);
        wmFieldChecks[field] = check;

        // Disable when using global watermark
        connect(useGlobalWmCheck, &QCheckBox::toggled, check, [check](bool useGlobal) {
            check->setEnabled(!useGlobal);
        });
        check->setEnabled(!member.useGlobalWatermark);
    }

    wmFieldsLayout->addStretch();
    wmLayout->addWidget(wmFieldsGroup);

    // Preview watermark text
    QGroupBox* previewGroup = new QGroupBox("Watermark Preview");
    QVBoxLayout* previewLayout = new QVBoxLayout(previewGroup);
    QLabel* previewLabel = new QLabel();
    previewLabel->setObjectName("WatermarkPreviewLabel");
    previewLabel->setWordWrap(true);

    auto updatePreview = [=]() {
        if (useGlobalWmCheck->isChecked()) {
            previewLabel->setText("[Global watermark - brand only]");
        } else {
            QStringList selectedFields;
            for (auto it = wmFieldChecks.begin(); it != wmFieldChecks.end(); ++it) {
                if (it.value()->isChecked()) {
                    selectedFields << it.key();
                }
            }
            if (selectedFields.isEmpty()) {
                previewLabel->setText("[Default: name, email, ip]");
            } else {
                QString preview;
                if (selectedFields.contains("name")) preview += nameEdit->text().isEmpty() ? "Name" : nameEdit->text();
                if (selectedFields.contains("email")) preview += (preview.isEmpty() ? "" : " - ") + (emailEdit->text().isEmpty() ? "email@example.com" : emailEdit->text());
                if (selectedFields.contains("ip")) preview += (preview.isEmpty() ? "" : " - IP: ") + (ipEdit->text().isEmpty() ? "1.2.3.4" : ipEdit->text());
                if (selectedFields.contains("mac")) preview += (preview.isEmpty() ? "" : " - MAC: ") + (macEdit->text().isEmpty() ? "AA:BB:CC:DD:EE:FF" : macEdit->text());
                if (selectedFields.contains("social")) preview += (preview.isEmpty() ? "" : " - ") + (socialEdit->text().isEmpty() ? "@handle" : socialEdit->text());
                previewLabel->setText(preview);
            }
        }
    };

    connect(useGlobalWmCheck, &QCheckBox::toggled, this, updatePreview);
    for (auto it = wmFieldChecks.begin(); it != wmFieldChecks.end(); ++it) {
        connect(it.value(), &QCheckBox::toggled, this, updatePreview);
    }
    connect(nameEdit, &QLineEdit::textChanged, this, updatePreview);
    connect(emailEdit, &QLineEdit::textChanged, this, updatePreview);
    connect(ipEdit, &QLineEdit::textChanged, this, updatePreview);
    updatePreview();

    previewLayout->addWidget(previewLabel);
    wmLayout->addWidget(previewGroup);

    tabs->addTab(wmTab, "Watermark");

    // === Distribution Folder Tab ===
    QWidget* distTab = new QWidget();
    QVBoxLayout* distLayout = new QVBoxLayout(distTab);

    QLabel* distLabel = new QLabel("MEGA folder where distributed files will be uploaded for this member:");
    distLabel->setProperty("type", "secondary");
    distLabel->setWordWrap(true);
    distLayout->addWidget(distLabel);

    QHBoxLayout* folderLayout = new QHBoxLayout();
    QLineEdit* folderEdit = new QLineEdit(member.distributionFolder);
    folderEdit->setPlaceholderText("e.g., /Members/John_EGB001/");
    folderEdit->setReadOnly(true);
    folderLayout->addWidget(folderEdit, 1);

    QPushButton* browseFolderBtn = new QPushButton("Browse...");
    browseFolderBtn->setIcon(QIcon(":/icons/folder.svg"));
    connect(browseFolderBtn, &QPushButton::clicked, this, [=]() {
        RemoteFolderBrowserDialog browser(this);
        browser.setTitle("Select Distribution Folder");
        browser.setSelectionMode(RemoteFolderBrowserDialog::SingleFolder);
        if (m_fileController) {
            browser.setFileController(m_fileController);
        }
        if (!member.distributionFolder.isEmpty()) {
            browser.setInitialPath(member.distributionFolder);
        }
        if (browser.exec() == QDialog::Accepted) {
            folderEdit->setText(browser.selectedPath());
        }
    });
    folderLayout->addWidget(browseFolderBtn);

    QPushButton* clearFolderBtn = new QPushButton("Clear");
    connect(clearFolderBtn, &QPushButton::clicked, this, [=]() {
        folderEdit->clear();
    });
    folderLayout->addWidget(clearFolderBtn);

    distLayout->addLayout(folderLayout);

    // WordPress sync info
    QGroupBox* wpGroup = new QGroupBox("WordPress Sync");
    QFormLayout* wpForm = new QFormLayout(wpGroup);

    QLineEdit* wpUserIdEdit = new QLineEdit(member.wpUserId);
    wpUserIdEdit->setPlaceholderText("WordPress User ID");
    wpForm->addRow("WP User ID:", wpUserIdEdit);

    QString lastSyncText = member.lastWpSync > 0
        ? QDateTime::fromSecsSinceEpoch(member.lastWpSync).toString("yyyy-MM-dd hh:mm:ss")
        : "Never";
    QLabel* lastSyncLabel = new QLabel(lastSyncText);
    lastSyncLabel->setProperty("type", "secondary");
    wpForm->addRow("Last Synced:", lastSyncLabel);

    distLayout->addWidget(wpGroup);
    distLayout->addStretch();

    tabs->addTab(distTab, "Distribution");

    // === Paths Tab (existing functionality) ===
    QWidget* pathsTab = new QWidget();
    QVBoxLayout* pathsLayout = new QVBoxLayout(pathsTab);

    QLabel* pathsLabel = new QLabel("Legacy path configuration (for archive-based distribution):");
    pathsLabel->setProperty("type", "secondary");
    pathsLabel->setWordWrap(true);
    pathsLayout->addWidget(pathsLabel);

    QLineEdit* wmPatternEdit = new QLineEdit(member.wmFolderPattern);
    wmPatternEdit->setPlaceholderText("e.g., MemberName_*");
    QFormLayout* pathsForm = new QFormLayout();
    pathsForm->addRow("WM Folder Pattern:", wmPatternEdit);

    MemberTemplate tmpl = m_registry->getTemplate();
    QMap<QString, QCheckBox*> memberPathChecks;
    QMap<QString, QLineEdit*> memberPathEdits;

    // Get current member values
    QMap<QString, QString> currentValues;
    currentValues["archiveRoot"] = member.paths.archiveRoot;
    currentValues["nhbCallsPath"] = member.paths.nhbCallsPath;
    currentValues["fastForwardPath"] = member.paths.fastForwardPath;
    currentValues["theoryCallsPath"] = member.paths.theoryCallsPath;
    currentValues["hotSeatsPath"] = member.paths.hotSeatsPath;

    QGridLayout* pathsGrid = new QGridLayout();
    pathsGrid->setSpacing(8);

    int prow = 0;
    for (const PathType& pt : tmpl.pathTypes) {
        QString currentVal = currentValues.value(pt.key);
        bool hasValue = !currentVal.isEmpty();

        QCheckBox* includeCheck = new QCheckBox(pt.label);
        includeCheck->setChecked(hasValue);
        includeCheck->setToolTip(pt.description);
        pathsGrid->addWidget(includeCheck, prow, 0);

        QLineEdit* valueEdit = new QLineEdit(hasValue ? currentVal : pt.defaultValue);
        valueEdit->setEnabled(hasValue);
        pathsGrid->addWidget(valueEdit, prow, 1);

        connect(includeCheck, &QCheckBox::toggled, valueEdit, &QLineEdit::setEnabled);

        memberPathChecks[pt.key] = includeCheck;
        memberPathEdits[pt.key] = valueEdit;

        prow++;
    }
    pathsGrid->setColumnStretch(1, 1);

    pathsLayout->addLayout(pathsForm);
    pathsLayout->addLayout(pathsGrid);
    pathsLayout->addStretch();

    tabs->addTab(pathsTab, "Paths");

    layout->addWidget(tabs);

    // Dialog buttons
    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() == QDialog::Accepted) {
        if (idEdit->text().isEmpty()) {
            QMessageBox::warning(this, "Error", "Member ID is required");
            return;
        }
        if (isNew && m_registry->hasMember(idEdit->text().toLower())) {
            QMessageBox::warning(this, "Error", "A member with this ID already exists");
            return;
        }

        MemberInfo info = member;
        if (isNew) {
            info.id = idEdit->text().toLower();
        }
        info.displayName = nameEdit->text();
        info.sortOrder = orderSpin->value();
        info.active = activeCheck->isChecked();
        info.notes = notesEdit->toPlainText();
        info.wmFolderPattern = wmPatternEdit->text();

        // Contact info
        info.email = emailEdit->text();
        info.ipAddress = ipEdit->text();
        info.macAddress = macEdit->text();
        info.socialHandle = socialEdit->text();

        // Watermark config
        info.useGlobalWatermark = useGlobalWmCheck->isChecked();
        info.watermarkFields.clear();
        for (auto it = wmFieldChecks.begin(); it != wmFieldChecks.end(); ++it) {
            if (it.value()->isChecked()) {
                info.watermarkFields << it.key();
            }
        }

        // Distribution folder
        info.distributionFolder = folderEdit->text();
        info.wpUserId = wpUserIdEdit->text();

        // Legacy paths
        info.paths.archiveRoot = memberPathChecks["archiveRoot"]->isChecked()
            ? memberPathEdits["archiveRoot"]->text() : QString();
        info.paths.nhbCallsPath = memberPathChecks["nhbCallsPath"]->isChecked()
            ? memberPathEdits["nhbCallsPath"]->text() : QString();
        info.paths.fastForwardPath = memberPathChecks["fastForwardPath"]->isChecked()
            ? memberPathEdits["fastForwardPath"]->text() : QString();
        info.paths.theoryCallsPath = memberPathChecks["theoryCallsPath"]->isChecked()
            ? memberPathEdits["theoryCallsPath"]->text() : QString();
        info.paths.hotSeatsPath = memberPathChecks["hotSeatsPath"]->isChecked()
            ? memberPathEdits["hotSeatsPath"]->text() : QString();

        // Update timestamps
        qint64 now = QDateTime::currentSecsSinceEpoch();
        if (isNew) {
            info.createdAt = now;
        }
        info.updatedAt = now;

        if (isNew) {
            m_registry->addMember(info);
        } else {
            m_registry->updateMember(info);
        }
    }
}

void MemberRegistryPanel::onAddMember() {
    MemberInfo newMember;
    newMember.active = true;
    newMember.sortOrder = m_registry->getAllMembers().size() + 1;
    showMemberEditDialog(newMember, true);
}

void MemberRegistryPanel::onEditMember() {
    QString memberId = getSelectedMemberId();
    if (memberId.isEmpty()) return;

    MemberInfo info = m_registry->getMember(memberId);
    showMemberEditDialog(info, false);
}

void MemberRegistryPanel::onDuplicateMember() {
    const QString sourceId = getSelectedMemberId();
    if (sourceId.isEmpty()) return;

    const MemberInfo source = m_registry->getMember(sourceId);
    QString baseId = source.id + "_copy";
    QString newId = baseId;
    int suffix = 2;
    while (m_registry->hasMember(newId)) {
        newId = baseId + QString::number(suffix++);
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QString("Duplicate Member: %1").arg(source.displayName.isEmpty() ? source.id : source.displayName));
    dialog.setMinimumWidth(520);

    auto* layout = new QVBoxLayout(&dialog);
    auto* infoLabel = new QLabel(
        "Create a new registry member from the selected member. The new member gets fresh activity/WordPress sync state.");
    infoLabel->setWordWrap(true);
    infoLabel->setProperty("type", "secondary");
    layout->addWidget(infoLabel);

    auto* form = new QFormLayout();
    auto* idEdit = new QLineEdit(newId);
    idEdit->setToolTip("Unique member ID for the duplicate. Existing members will not be overwritten.");
    auto* nameEdit = new QLineEdit(source.displayName + " (copy)");
    nameEdit->setToolTip("Display name for the duplicated member.");
    auto* orderSpin = new QSpinBox();
    orderSpin->setRange(1, 9999);
    orderSpin->setValue(m_registry->getAllMembers().size() + 1);
    orderSpin->setToolTip("Sort order for the duplicated member.");
    form->addRow("New member ID:", idEdit);
    form->addRow("Display name:", nameEdit);
    form->addRow("Sort order:", orderSpin);
    layout->addLayout(form);

    auto* copyPathsCheck = new QCheckBox("Copy archive paths");
    copyPathsCheck->setChecked(true);
    copyPathsCheck->setToolTip("Copy archive root, NHB calls, Fast Forward, theory calls, and hot seats paths.");
    auto* copyFolderCheck = new QCheckBox("Copy direct distribution folder");
    copyFolderCheck->setChecked(false);
    copyFolderCheck->setToolTip("Copy the direct distribution folder binding. Leave off when the duplicate should get its own folder.");
    auto* copyContactCheck = new QCheckBox("Copy contact fields");
    copyContactCheck->setChecked(true);
    copyContactCheck->setToolTip("Copy email, IP, MAC, and social handle.");
    auto* copyWatermarkCheck = new QCheckBox("Copy watermark settings");
    copyWatermarkCheck->setChecked(true);
    copyWatermarkCheck->setToolTip("Copy personalized watermark field selection and global-watermark override.");
    auto* copyGroupsCheck = new QCheckBox("Copy group membership");
    copyGroupsCheck->setChecked(true);
    copyGroupsCheck->setToolTip("Add the duplicated member to the same saved groups as the source member.");
    auto* activeCheck = new QCheckBox("Mark duplicate active");
    activeCheck->setChecked(source.active);
    activeCheck->setToolTip("Set whether the duplicated member should be active immediately.");

    layout->addWidget(copyPathsCheck);
    layout->addWidget(copyFolderCheck);
    layout->addWidget(copyContactCheck);
    layout->addWidget(copyWatermarkCheck);
    layout->addWidget(copyGroupsCheck);
    layout->addWidget(activeCheck);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Duplicate");
    buttons->button(QDialogButtonBox::Ok)->setToolTip("Create the duplicated member with these options");
    buttons->button(QDialogButtonBox::Cancel)->setToolTip("Close without creating a duplicate");
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) return;

    newId = idEdit->text().trimmed().toLower();
    if (newId.isEmpty()) {
        QMessageBox::warning(this, "Duplicate Member", "New member ID is required.");
        return;
    }
    if (m_registry->hasMember(newId)) {
        QMessageBox::warning(this, "Duplicate Member",
            QString("A member with ID '%1' already exists.").arg(newId));
        return;
    }

    MemberInfo copy = source;
    copy.id = newId;
    copy.displayName = nameEdit->text().trimmed().isEmpty()
        ? newId
        : nameEdit->text().trimmed();
    copy.sortOrder = orderSpin->value();
    copy.active = activeCheck->isChecked();
    copy.pipelineStatus = MemberStatusInfo();
    copy.wpUserId.clear();
    copy.lastWpSync = 0;

    if (!copyPathsCheck->isChecked()) {
        copy.paths = MemberPaths();
        copy.wmFolderPattern.clear();
    }
    if (!copyFolderCheck->isChecked()) {
        copy.distributionFolder.clear();
    }
    if (!copyContactCheck->isChecked()) {
        copy.email.clear();
        copy.ipAddress.clear();
        copy.macAddress.clear();
        copy.socialHandle.clear();
    }
    if (!copyWatermarkCheck->isChecked()) {
        copy.watermarkFields.clear();
        copy.useGlobalWatermark = false;
    }

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    copy.createdAt = now;
    copy.updatedAt = now;

    m_registry->addMember(copy);

    if (copyGroupsCheck->isChecked()) {
        const QStringList groups = m_registry->getGroupsForMember(source.id);
        for (const QString& groupName : groups) {
            MemberGroup group = m_registry->getGroup(groupName);
            if (!group.memberIds.contains(copy.id)) {
                group.memberIds.append(copy.id);
                group.updatedAt = now;
                m_registry->updateGroup(group);
            }
        }
    }

    refresh();
    selectMemberById(copy.id);
    QMessageBox::information(this, "Duplicate Member",
        QString("Created member '%1' from '%2'.").arg(copy.id, source.id));
}

void MemberRegistryPanel::onRemoveMember() {
    QString memberId = getSelectedMemberId();
    if (memberId.isEmpty()) return;

    MemberInfo info = m_registry->getMember(memberId);

    int ret = QMessageBox::question(this, "Delete Member",
        QString("Are you sure you want to delete '%1'? This cannot be undone.").arg(info.displayName),
        QMessageBox::Yes | QMessageBox::No);

    if (ret == QMessageBox::Yes) {
        m_registry->removeMember(memberId);
    }
}

void MemberRegistryPanel::onAuditMembers() {
    int errors = 0;
    int warnings = 0;
    int infos = 0;
    const QList<QStringList> findings = buildMemberAuditRows(&errors, &warnings, &infos);

    QDialog dialog(this);
    dialog.setWindowTitle("Member Registry Audit");
    dialog.setMinimumSize(900, 560);

    auto* layout = new QVBoxLayout(&dialog);
    auto* summary = new QLabel(QString("Audit complete: %1 errors, %2 warnings, %3 info across %4 members and %5 groups.")
        .arg(errors)
        .arg(warnings)
        .arg(infos)
        .arg(m_registry->getAllMembers().size())
        .arg(m_registry->getAllGroups().size()));
    summary->setObjectName("PanelSubtitle");
    summary->setWordWrap(true);
    layout->addWidget(summary);

    auto* table = new QTableWidget(&dialog);
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels({"Severity", "Area", "Subject", "Problem", "Suggested Fix"});
    table->setRowCount(findings.size());
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->setSortingEnabled(false);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    table->setColumnWidth(0, 90);
    table->setColumnWidth(1, 110);
    table->setColumnWidth(2, 180);

    auto& tm = ThemeManager::instance();
    for (int row = 0; row < findings.size(); ++row) {
        const QStringList finding = findings[row];
        for (int col = 0; col < finding.size(); ++col) {
            auto* item = new QTableWidgetItem(finding[col]);
            item->setToolTip(finding[col]);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            if (col == 0) {
                item->setTextAlignment(Qt::AlignCenter);
                if (finding[col] == "Error") item->setForeground(tm.supportError());
                else if (finding[col] == "Warning") item->setForeground(tm.supportWarning());
                else item->setForeground(tm.supportInfo());
            }
            table->setItem(row, col, item);
        }
    }
    table->setSortingEnabled(true);
    layout->addWidget(table, 1);

    auto buildReport = [&]() {
        QStringList lines;
        lines.append(summary->text());
        lines.append("Severity\tArea\tSubject\tProblem\tSuggested Fix");
        for (const QStringList& finding : findings) {
            lines.append(finding.join("\t"));
        }
        return lines.join("\n");
    };

    auto* buttonRow = new QHBoxLayout();
    auto* copyBtn = new QPushButton("Copy Report", &dialog);
    copyBtn->setToolTip("Copy the full audit report to the clipboard");
    connect(copyBtn, &QPushButton::clicked, &dialog, [&]() {
        QApplication::clipboard()->setText(buildReport());
        QMessageBox::information(&dialog, "Audit Report", "Audit report copied to clipboard.");
    });
    auto* closeBtn = new QPushButton("Close", &dialog);
    closeBtn->setToolTip("Close the member audit report");
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttonRow->addWidget(copyBtn);
    buttonRow->addStretch();
    buttonRow->addWidget(closeBtn);
    layout->addLayout(buttonRow);

    if (findings.isEmpty()) {
        QMessageBox::information(this, "Member Registry Audit",
            "No member registry issues found.");
        return;
    }

    dialog.exec();
}

void MemberRegistryPanel::onBindFolder() {
    QString memberId = getSelectedMemberId();
    if (memberId.isEmpty()) return;

    MemberInfo info = m_registry->getMember(memberId);

    RemoteFolderBrowserDialog browser(this);
    browser.setTitle(QString("Select Distribution Folder for %1").arg(info.displayName));
    browser.setSelectionMode(RemoteFolderBrowserDialog::SingleFolder);
    if (m_fileController) {
        browser.setFileController(m_fileController);
    }
    if (!info.distributionFolder.isEmpty()) {
        browser.setInitialPath(info.distributionFolder);
    }

    if (browser.exec() == QDialog::Accepted) {
        QString selectedPath = browser.selectedPath();
        if (!selectedPath.isEmpty()) {
            m_registry->setDistributionFolder(memberId, selectedPath);
            QMessageBox::information(this, "Folder Bound",
                QString("Distribution folder for %1 set to:\n%2")
                    .arg(info.displayName).arg(selectedPath));
        }
    }
}

void MemberRegistryPanel::onUnbindFolder() {
    QString memberId = getSelectedMemberId();
    if (memberId.isEmpty()) return;

    MemberInfo info = m_registry->getMember(memberId);

    int ret = QMessageBox::question(this, "Unbind Folder",
        QString("Remove distribution folder binding for '%1'?").arg(info.displayName),
        QMessageBox::Yes | QMessageBox::No);

    if (ret == QMessageBox::Yes) {
        m_registry->clearDistributionFolder(memberId);
    }
}

void MemberRegistryPanel::onEditTemplate() {
    // Now handled in the tab
}

void MemberRegistryPanel::onImportMembers() {
    QString filePath = QFileDialog::getOpenFileName(this,
        "Import Members", QString(), "JSON Files (*.json)");
    if (filePath.isEmpty()) return;

    // Count members in the import file
    QFile file(filePath);
    int importCount = 0;
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (doc.isObject() && doc.object().contains("members")) {
            importCount = doc.object()["members"].toArray().size();
        }
    }

    int existingCount = m_registry->getAllMembers().size();

    // Ask user: merge or replace?
    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Import Members");
    msgBox.setText(QString("File contains %1 members.\nYou currently have %2 members.")
        .arg(importCount).arg(existingCount));
    msgBox.setInformativeText("How would you like to import?");
    QPushButton* mergeBtn = msgBox.addButton("Merge (add/update)", QMessageBox::AcceptRole);
    QPushButton* replaceBtn = msgBox.addButton("Replace all", QMessageBox::DestructiveRole);
    msgBox.addButton(QMessageBox::Cancel);
    msgBox.setDefaultButton(mergeBtn);
    msgBox.exec();

    if (msgBox.clickedButton() == msgBox.button(QMessageBox::Cancel)) return;

    bool mergeMode = (msgBox.clickedButton() == mergeBtn);

    if (m_registry->importFromFile(filePath, mergeMode)) {
        refreshTemplate();
        QString mode = mergeMode ? "merged" : "replaced";
        QMessageBox::information(this, "Import",
            QString("Members %1 successfully. Now have %2 members.")
                .arg(mode).arg(m_registry->getAllMembers().size()));
    } else {
        QMessageBox::warning(this, "Import Failed", "Failed to import members from file");
    }
}

void MemberRegistryPanel::onExportMembers() {
    QString filePath = QFileDialog::getSaveFileName(this,
        "Export Members", "members.json", "JSON Files (*.json)");
    if (filePath.isEmpty()) return;

    if (m_registry->exportToFile(filePath)) {
        QMessageBox::information(this, "Export", "Members exported successfully");
    } else {
        QMessageBox::warning(this, "Export Failed", "Failed to export members to file");
    }
}

void MemberRegistryPanel::onImportCsv() {
    QString filePath = QFileDialog::getOpenFileName(this,
        "Import Members from CSV", QString(), "CSV Files (*.csv)");
    if (filePath.isEmpty()) return;

    if (m_registry->importFromCsv(filePath, true)) {
        QMessageBox::information(this, "Import", "Members imported from CSV successfully");
    } else {
        QMessageBox::warning(this, "Import Failed", "Failed to import members from CSV file");
    }
}

void MemberRegistryPanel::onExportCsv() {
    QString filePath = QFileDialog::getSaveFileName(this,
        "Export Members to CSV", "members.csv", "CSV Files (*.csv)");
    if (filePath.isEmpty()) return;

    if (m_registry->exportToCsv(filePath)) {
        QMessageBox::information(this, "Export", "Members exported to CSV successfully");
    } else {
        QMessageBox::warning(this, "Export Failed", "Failed to export members to CSV file");
    }
}

void MemberRegistryPanel::onPopulateDefaults() {
    if (!m_registry->getAllMembers().isEmpty()) {
        int ret = QMessageBox::question(this, "Populate Defaults",
            "This will replace all existing members with the default 14 members.\n"
            "Are you sure?",
            QMessageBox::Yes | QMessageBox::No);
        if (ret != QMessageBox::Yes) return;
    }

    // Your 14 default members
    QList<MemberInfo> defaults = {
        {"icekkk", "Icekkk", 3, "Icekkk_*",
            {"/Alen Sultanic - NHB+ - EGBs/3. Icekkk",
             "NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025",
             "Fast Forward", "2- Theory Calls", "3- Hotseats"}, true, ""},
        {"nekondarun", "nekondarun", 5, "nekondarun_*",
            {"/Alen Sultanic - NHB+ - EGBs/5. nekondarun",
             "NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025",
             "Fast Forward", "2- Theory Calls", "3- Hotseats"}, true, ""},
        {"sp3nc3", "sp3nc3", 7, "sp3nc3_*",
            {"/Alen Sultanic - NHB+ - EGBs/7. sp3nc3",
             "NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025",
             "Fast Forward", "2- Theory Calls", "3- Hotseats"}, true, ""},
        {"mehulthakkar", "mehulthakkar", 9, "mehtha_*",
            {"/Alen Sultanic - NHB+ - EGBs/9. mehulthakkar",
             "NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025",
             "Fast Forward", "2- Theory Calls", "3- Hotseats"}, true, ""},
        {"maxbooks", "maxbooks", 10, "maxbooks_*",
            {"/Alen Sultanic - NHB+ - EGBs/10. maxbooks",
             "NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025",
             "Fast Forward", "2- Theory Calls", "3- Hotseats"}, true, ""},
        {"mars", "mars", 11, "mars_*",
            {"/Alen Sultanic - NHB+ - EGBs/11. mars",
             "NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025",
             "Fast Forward", "2- Theory Calls", "3- Hotseats"}, true, ""},
        {"alfie", "alfie - MM2024", 13, "mm2024_*",
            {"/Alen Sultanic - NHB+ - EGBs/13. alfie - MM2024",
             "NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025",
             "Fast Forward", "2- Theory Calls", "3- Hotseats"}, true, ""},
        {"peterpette", "peterpette", 14, "jpegcollector_*",
            {"/Alen Sultanic - NHB+ - EGBs/14. peterpette",
             "NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025",
             "Fast Forward", "2- Theory Calls", "3- Hotseats"}, true, ""},
        {"danki", "danki", 17, "danki_*",
            {"/Alen Sultanic - NHB+ - EGBs/17. danki",
             "NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025",
             "Fast Forward", "2- Theory Calls", "3- Hotseats"}, true, ""},
        {"marvizta", "marvizta", 20, "slayer_*",
            {"/Alen Sultanic - NHB+ - EGBs/20. marvizta",
             "NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025",
             "Fast Forward", "2- Theory Calls", "3- Hotseats"}, true, ""},
        {"jkalam", "jkalam", 21, "jkalam_*",
            {"/Alen Sultanic - NHB+ - EGBs/21. jkalam",
             "NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025",
             "Fast Forward", "2- Theory Calls", "3- Hotseats"}, true, ""},
        {"cmex", "CMex", 23, "CMex_*",
            {"/Alen Sultanic - NHB+ - EGBs/23. CMex",
             "NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025",
             "Fast Forward", "2- Theory Calls", "3- Hotseats"}, true, ""},
        {"downdogcatsup", "downdogcatsup", 24, "downdogcatsup_*",
            {"/Alen Sultanic - NHB+ - EGBs/24. downdogcatsup",
             "NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025",
             "Fast Forward", "2- Theory Calls", "3- Hotseats"}, true, ""},
        {"boris", "Boris", 25, "boris_*",
            {"/Alen Sultanic - NHB+ - EGBs/25. Boris",
             "NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025",
             "Fast Forward", "2- Theory Calls", "3- Hotseats"}, true, ""}
    };

    m_registry->setMembers(defaults);
    QMessageBox::information(this, "Done", "Populated with 14 default members");
}

void MemberRegistryPanel::onWordPressSync() {
    WordPressConfigDialog dialog(this);
    connect(&dialog, &WordPressConfigDialog::syncCompleted,
            this, &MemberRegistryPanel::onWpSyncCompleted);
    dialog.exec();
}

void MemberRegistryPanel::onWpSyncCompleted(int created, int updated) {
    // Refresh the member table to show newly synced members
    refresh();

    // Show summary message
    if (created > 0 || updated > 0) {
        QMessageBox::information(this, "WordPress Sync Complete",
            QString("Sync completed:\n- %1 new members created\n- %2 existing members updated")
                .arg(created).arg(updated));
    }
}

// ==================== Groups Tab ====================

void MemberRegistryPanel::refreshGroups() {
    if (!m_groupList) return;

    QString currentGroup;
    if (m_groupList->currentItem()) {
        currentGroup = m_groupList->currentItem()->data(Qt::UserRole).toString();
    }

    m_groupList->clear();
    QStringList groupNames = m_registry->getGroupNames();
    int selectRow = -1;
    for (int i = 0; i < groupNames.size(); ++i) {
        const QString& name = groupNames[i];
        MemberGroup group = m_registry->getGroup(name);
        int activeCount = m_registry->getGroupMemberIds(name).size();
        QListWidgetItem* item = new QListWidgetItem(
            QString("%1 (%2 members)").arg(name).arg(activeCount));
        item->setData(Qt::UserRole, name);
        m_groupList->addItem(item);
        if (name == currentGroup) selectRow = i;
    }

    if (selectRow >= 0) {
        m_groupList->setCurrentRow(selectRow);
    }

    m_groupStatsLabel->setText(QString("%1 %2").arg(groupNames.size()).arg(groupNames.size() == 1 ? "group" : "groups"));
}

void MemberRegistryPanel::onAddGroup() {
    bool ok;
    QString name = QInputDialog::getText(this, "New Group",
        "Group name:", QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    name = name.trimmed();
    if (m_registry->hasGroup(name)) {
        QMessageBox::warning(this, "Error", "A group with this name already exists.");
        return;
    }
    MemberGroup group;
    group.name = name;
    group.createdAt = QDateTime::currentSecsSinceEpoch();
    group.updatedAt = group.createdAt;
    m_registry->addGroup(group);

    // Select the new group
    for (int i = 0; i < m_groupList->count(); ++i) {
        if (m_groupList->item(i)->data(Qt::UserRole).toString() == name) {
            m_groupList->setCurrentRow(i);
            break;
        }
    }
}

void MemberRegistryPanel::onRenameGroup() {
    if (m_groupList->currentRow() < 0) return;
    QString oldName = m_groupList->currentItem()->data(Qt::UserRole).toString();

    bool ok;
    QString newName = QInputDialog::getText(this, "Rename Group",
        "New name:", QLineEdit::Normal, oldName, &ok);
    if (!ok || newName.trimmed().isEmpty()) return;
    newName = newName.trimmed();
    if (newName == oldName) return;
    if (m_registry->hasGroup(newName)) {
        QMessageBox::warning(this, "Error", "A group with this name already exists.");
        return;
    }

    MemberGroup group = m_registry->getGroup(oldName);
    m_registry->removeGroup(oldName);
    group.name = newName;
    group.updatedAt = QDateTime::currentSecsSinceEpoch();
    m_registry->addGroup(group);
}

void MemberRegistryPanel::onDeleteGroup() {
    if (m_groupList->currentRow() < 0) return;
    QString name = m_groupList->currentItem()->data(Qt::UserRole).toString();

    int ret = QMessageBox::question(this, "Delete Group",
        QString("Delete group '%1'?\n\nThis will NOT delete the members, only the group.").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    m_registry->removeGroup(name);
}

void MemberRegistryPanel::onDuplicateGroup() {
    if (m_groupList->currentRow() < 0) return;
    QString name = m_groupList->currentItem()->data(Qt::UserRole).toString();
    MemberGroup original = m_registry->getGroup(name);

    QString newName = name + " (copy)";
    int suffix = 2;
    while (m_registry->hasGroup(newName)) {
        newName = name + QString(" (copy %1)").arg(suffix++);
    }

    MemberGroup copy;
    copy.name = newName;
    copy.description = original.description;
    copy.memberIds = original.memberIds;
    copy.createdAt = QDateTime::currentSecsSinceEpoch();
    copy.updatedAt = copy.createdAt;
    m_registry->addGroup(copy);

    // Select the new group
    for (int i = 0; i < m_groupList->count(); ++i) {
        if (m_groupList->item(i)->data(Qt::UserRole).toString() == newName) {
            m_groupList->setCurrentRow(i);
            break;
        }
    }
}

void MemberRegistryPanel::onGroupSelectionChanged() {
    bool hasSelection = m_groupList->currentRow() >= 0;
    m_renameGroupBtn->setEnabled(hasSelection);
    m_deleteGroupBtn->setEnabled(hasSelection);
    m_duplicateGroupBtn->setEnabled(hasSelection);
    m_groupSelectAllBtn->setEnabled(hasSelection);
    m_groupDeselectAllBtn->setEnabled(hasSelection);

    // Block signals while populating to avoid triggering onGroupMemberToggled
    m_groupMemberList->blockSignals(true);
    m_groupMemberList->clear();

    if (!hasSelection) {
        m_groupMemberList->blockSignals(false);
        return;
    }

    QString groupName = m_groupList->currentItem()->data(Qt::UserRole).toString();
    MemberGroup group = m_registry->getGroup(groupName);
    QList<MemberInfo> activeMembers = m_registry->getActiveMembers();
    QString searchFilter = m_groupSearchEdit->text().trimmed();

    for (const MemberInfo& m : activeMembers) {
        // Apply search filter
        if (!searchFilter.isEmpty()) {
            bool match = m.id.contains(searchFilter, Qt::CaseInsensitive) ||
                         m.displayName.contains(searchFilter, Qt::CaseInsensitive);
            if (!match) continue;
        }

        QListWidgetItem* item = new QListWidgetItem(
            QString("%1 (%2)").arg(m.displayName).arg(m.id));
        item->setData(Qt::UserRole, m.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(group.memberIds.contains(m.id) ? Qt::Checked : Qt::Unchecked);
        m_groupMemberList->addItem(item);
    }

    m_groupMemberList->blockSignals(false);
}

void MemberRegistryPanel::onGroupMemberToggled(QListWidgetItem* item) {
    if (m_groupList->currentRow() < 0) return;

    QString groupName = m_groupList->currentItem()->data(Qt::UserRole).toString();
    QString memberId = item->data(Qt::UserRole).toString();
    MemberGroup group = m_registry->getGroup(groupName);

    if (item->checkState() == Qt::Checked) {
        if (!group.memberIds.contains(memberId)) {
            group.memberIds.append(memberId);
        }
    } else {
        group.memberIds.removeAll(memberId);
    }

    group.updatedAt = QDateTime::currentSecsSinceEpoch();

    // Suppress refresh to avoid rebuilding member list (losing scroll position)
    m_suppressGroupRefresh = true;
    m_registry->updateGroup(group);
    m_suppressGroupRefresh = false;

    // Update the group list count display without losing selection
    int row = m_groupList->currentRow();
    int activeCount = m_registry->getGroupMemberIds(groupName).size();
    m_groupList->item(row)->setText(
        QString("%1 (%2 members)").arg(groupName).arg(activeCount));
}

void MemberRegistryPanel::onGroupSelectAll() {
    m_groupMemberList->blockSignals(true);
    for (int i = 0; i < m_groupMemberList->count(); ++i) {
        QListWidgetItem* item = m_groupMemberList->item(i);
        if (!item->isHidden()) {
            item->setCheckState(Qt::Checked);
        }
    }
    m_groupMemberList->blockSignals(false);

    // Save all visible members to group
    if (m_groupList->currentRow() < 0) return;
    QString groupName = m_groupList->currentItem()->data(Qt::UserRole).toString();
    MemberGroup group = m_registry->getGroup(groupName);
    for (int i = 0; i < m_groupMemberList->count(); ++i) {
        QListWidgetItem* item = m_groupMemberList->item(i);
        if (!item->isHidden()) {
            QString memberId = item->data(Qt::UserRole).toString();
            if (!group.memberIds.contains(memberId)) {
                group.memberIds.append(memberId);
            }
        }
    }
    group.updatedAt = QDateTime::currentSecsSinceEpoch();
    m_suppressGroupRefresh = true;
    m_registry->updateGroup(group);
    m_suppressGroupRefresh = false;

    // Update count display
    int activeCount = m_registry->getGroupMemberIds(groupName).size();
    m_groupList->currentItem()->setText(
        QString("%1 (%2 members)").arg(groupName).arg(activeCount));
}

void MemberRegistryPanel::onGroupDeselectAll() {
    m_groupMemberList->blockSignals(true);
    for (int i = 0; i < m_groupMemberList->count(); ++i) {
        QListWidgetItem* item = m_groupMemberList->item(i);
        if (!item->isHidden()) {
            item->setCheckState(Qt::Unchecked);
        }
    }
    m_groupMemberList->blockSignals(false);

    // Remove all visible members from group
    if (m_groupList->currentRow() < 0) return;
    QString groupName = m_groupList->currentItem()->data(Qt::UserRole).toString();
    MemberGroup group = m_registry->getGroup(groupName);
    for (int i = 0; i < m_groupMemberList->count(); ++i) {
        QListWidgetItem* item = m_groupMemberList->item(i);
        if (!item->isHidden()) {
            QString memberId = item->data(Qt::UserRole).toString();
            group.memberIds.removeAll(memberId);
        }
    }
    group.updatedAt = QDateTime::currentSecsSinceEpoch();
    m_suppressGroupRefresh = true;
    m_registry->updateGroup(group);
    m_suppressGroupRefresh = false;

    // Update count display
    int activeCount = m_registry->getGroupMemberIds(groupName).size();
    m_groupList->currentItem()->setText(
        QString("%1 (%2 members)").arg(groupName).arg(activeCount));
}

void MemberRegistryPanel::onGroupSearchChanged(const QString& text) {
    Q_UNUSED(text);
    // Re-populate the member list with filter applied
    onGroupSelectionChanged();
}

} // namespace MegaCustom
