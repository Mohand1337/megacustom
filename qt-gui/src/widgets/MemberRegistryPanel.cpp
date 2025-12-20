#include "MemberRegistryPanel.h"
#include "utils/MemberRegistry.h"
#include "controllers/FileController.h"
#include "dialogs/RemoteFolderBrowserDialog.h"
#include "dialogs/WordPressConfigDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
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

namespace MegaCustom {

MemberRegistryPanel::MemberRegistryPanel(QWidget* parent)
    : QWidget(parent)
    , m_registry(MemberRegistry::instance())
{
    setupUI();
    refresh();

    // Connect registry signals
    connect(m_registry, &MemberRegistry::membersReloaded, this, &MemberRegistryPanel::refresh);
    connect(m_registry, &MemberRegistry::memberAdded, this, &MemberRegistryPanel::refresh);
    connect(m_registry, &MemberRegistry::memberUpdated, this, &MemberRegistryPanel::refresh);
    connect(m_registry, &MemberRegistry::memberRemoved, this, &MemberRegistryPanel::refresh);
    connect(m_registry, &MemberRegistry::templateChanged, this, &MemberRegistryPanel::refreshTemplate);
}

void MemberRegistryPanel::setFileController(FileController* controller) {
    m_fileController = controller;
}

void MemberRegistryPanel::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(16);

    // Title
    QLabel* titleLabel = new QLabel("Member Registry");
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #e0e0e0;");
    mainLayout->addWidget(titleLabel);

    // Description
    QLabel* descLabel = new QLabel("Manage members with distribution folders, watermark settings, and contact info for personalized file distribution.");
    descLabel->setStyleSheet("color: #888; margin-bottom: 8px;");
    descLabel->setWordWrap(true);
    mainLayout->addWidget(descLabel);

    // Search and Filter bar
    QHBoxLayout* filterLayout = new QHBoxLayout();
    filterLayout->setSpacing(12);

    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("Search members...");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setMinimumWidth(200);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MemberRegistryPanel::onSearchChanged);
    filterLayout->addWidget(m_searchEdit);

    m_activeOnlyCheck = new QCheckBox("Active only");
    m_activeOnlyCheck->setChecked(false);
    connect(m_activeOnlyCheck, &QCheckBox::toggled, this, &MemberRegistryPanel::onFilterChanged);
    filterLayout->addWidget(m_activeOnlyCheck);

    m_withFolderOnlyCheck = new QCheckBox("With folder bound");
    m_withFolderOnlyCheck->setChecked(false);
    connect(m_withFolderOnlyCheck, &QCheckBox::toggled, this, &MemberRegistryPanel::onFilterChanged);
    filterLayout->addWidget(m_withFolderOnlyCheck);

    filterLayout->addStretch();
    mainLayout->addLayout(filterLayout);

    // Tab widget for members table and template config
    QTabWidget* tabWidget = new QTabWidget();
    tabWidget->setStyleSheet(R"(
        QTabWidget::pane {
            border: 1px solid #444;
            border-radius: 4px;
            background-color: #1e1e1e;
        }
        QTabBar::tab {
            background-color: #2a2a2a;
            color: #888;
            padding: 8px 16px;
            border: 1px solid #444;
            border-bottom: none;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
        }
        QTabBar::tab:selected {
            background-color: #1e1e1e;
            color: #e0e0e0;
        }
    )");

    // === Members Tab ===
    QWidget* membersTab = new QWidget();
    QVBoxLayout* membersLayout = new QVBoxLayout(membersTab);
    membersLayout->setContentsMargins(8, 8, 8, 8);

    // Members table with extended columns
    m_memberTable = new QTableWidget();
    m_memberTable->setColumnCount(7);
    m_memberTable->setHorizontalHeaderLabels({
        "#", "ID", "Display Name", "Email", "Distribution Folder", "WM Fields", "Active"
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
    m_memberTable->setColumnWidth(0, 40);
    m_memberTable->setColumnWidth(1, 100);
    m_memberTable->setColumnWidth(2, 120);
    m_memberTable->setColumnWidth(3, 160);
    m_memberTable->setColumnWidth(5, 100);
    m_memberTable->setColumnWidth(6, 60);

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

    connect(m_memberTable, &QTableWidget::itemSelectionChanged,
            this, &MemberRegistryPanel::onTableSelectionChanged);
    connect(m_memberTable, &QTableWidget::cellDoubleClicked,
            this, &MemberRegistryPanel::onTableDoubleClicked);
    connect(m_memberTable, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QString memberId = getSelectedMemberId();
        if (memberId.isEmpty()) return;

        QMenu menu(this);
        menu.addAction("Edit", this, &MemberRegistryPanel::onEditMember);
        menu.addAction("Bind Folder...", this, &MemberRegistryPanel::onBindFolder);
        menu.addAction("Unbind Folder", this, &MemberRegistryPanel::onUnbindFolder);
        menu.addSeparator();
        menu.addAction("Remove", this, &MemberRegistryPanel::onRemoveMember);
        menu.exec(m_memberTable->viewport()->mapToGlobal(pos));
    });

    membersLayout->addWidget(m_memberTable, 1);

    // Action buttons row 1 - Main actions
    QHBoxLayout* actionsLayout1 = new QHBoxLayout();
    actionsLayout1->setSpacing(8);

    m_addBtn = new QPushButton("Add Member");
    m_addBtn->setIcon(QIcon(":/icons/plus.svg"));
    connect(m_addBtn, &QPushButton::clicked, this, &MemberRegistryPanel::onAddMember);

    m_editBtn = new QPushButton("Edit");
    m_editBtn->setIcon(QIcon(":/icons/edit.svg"));
    m_editBtn->setEnabled(false);
    connect(m_editBtn, &QPushButton::clicked, this, &MemberRegistryPanel::onEditMember);

    m_removeBtn = new QPushButton("Remove");
    m_removeBtn->setIcon(QIcon(":/icons/trash-2.svg"));
    m_removeBtn->setEnabled(false);
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

    QLabel* templateDescLabel = new QLabel("Configure default path types for new members. Enable/disable path types to customize which paths are available.");
    templateDescLabel->setStyleSheet("color: #888;");
    templateDescLabel->setWordWrap(true);
    templateMainLayout->addWidget(templateDescLabel);

    // Path types grid with checkboxes and edit fields
    m_pathTypesWidget = new QWidget();
    QGridLayout* pathTypesGrid = new QGridLayout(m_pathTypesWidget);
    pathTypesGrid->setSpacing(8);

    // Headers
    QLabel* enabledHeader = new QLabel("Enabled");
    enabledHeader->setStyleSheet("font-weight: bold; color: #888;");
    QLabel* typeHeader = new QLabel("Path Type");
    typeHeader->setStyleSheet("font-weight: bold; color: #888;");
    QLabel* defaultHeader = new QLabel("Default Value");
    defaultHeader->setStyleSheet("font-weight: bold; color: #888;");

    pathTypesGrid->addWidget(enabledHeader, 0, 0);
    pathTypesGrid->addWidget(typeHeader, 0, 1);
    pathTypesGrid->addWidget(defaultHeader, 0, 2);

    // Create rows for each path type
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

        // Connect checkbox to enable/disable field
        connect(enableCheck, &QCheckBox::toggled, valueEdit, &QLineEdit::setEnabled);

        m_pathTypeChecks[pt.key] = enableCheck;
        m_pathTypeEdits[pt.key] = valueEdit;

        row++;
    }

    pathTypesGrid->setColumnStretch(2, 1);
    templateMainLayout->addWidget(m_pathTypesWidget);

    // Save Template button
    QHBoxLayout* templateBtnLayout = new QHBoxLayout();
    QPushButton* saveTemplateBtn = new QPushButton("Save Template");
    saveTemplateBtn->setIcon(QIcon(":/icons/check.svg"));
    saveTemplateBtn->setToolTip("Save changes to the global template");
    connect(saveTemplateBtn, &QPushButton::clicked, this, &MemberRegistryPanel::onSaveTemplate);
    templateBtnLayout->addStretch();
    templateBtnLayout->addWidget(saveTemplateBtn);
    templateMainLayout->addLayout(templateBtnLayout);

    templateMainLayout->addStretch();

    tabWidget->addTab(templateTab, "Global Template");

    mainLayout->addWidget(tabWidget, 1);

    // Stats
    m_statsLabel = new QLabel();
    m_statsLabel->setStyleSheet("color: #888;");
    mainLayout->addWidget(m_statsLabel);
}

void MemberRegistryPanel::refresh() {
    populateTable();

    // Update stats
    QList<MemberInfo> all = m_registry->getAllMembers();
    QList<MemberInfo> active = m_registry->getActiveMembers();
    QList<MemberInfo> withFolder = m_registry->getMembersWithDistributionFolders();
    m_statsLabel->setText(QString("Total: %1 members | %2 active | %3 with distribution folders")
        .arg(all.size()).arg(active.size()).arg(withFolder.size()));
}

void MemberRegistryPanel::refreshTemplate() {
    // Update template UI from registry
    MemberTemplate tmpl = m_registry->getTemplate();
    for (const PathType& pt : tmpl.pathTypes) {
        if (m_pathTypeChecks.contains(pt.key)) {
            m_pathTypeChecks[pt.key]->setChecked(pt.enabled);
        }
        if (m_pathTypeEdits.contains(pt.key)) {
            m_pathTypeEdits[pt.key]->setText(pt.defaultValue);
            m_pathTypeEdits[pt.key]->setEnabled(pt.enabled);
        }
    }
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

    QList<MemberInfo> members = m_registry->filterMembers(searchText, activeOnly, withFolderOnly);
    m_memberTable->setRowCount(members.size());

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
            emailItem->setForeground(QColor("#666"));
            emailItem->setText("-");
        }
        m_memberTable->setItem(row, 3, emailItem);

        // Distribution Folder
        QTableWidgetItem* folderItem = new QTableWidgetItem();
        if (m.hasDistributionFolder()) {
            folderItem->setText(m.distributionFolder);
            folderItem->setIcon(QIcon(":/icons/folder.svg"));
            folderItem->setForeground(QColor("#4ade80")); // Green
        } else {
            folderItem->setText("Not bound");
            folderItem->setForeground(QColor("#666"));
        }
        m_memberTable->setItem(row, 4, folderItem);

        // Watermark Fields
        QTableWidgetItem* wmItem = new QTableWidgetItem();
        if (m.useGlobalWatermark) {
            wmItem->setText("Global");
            wmItem->setForeground(QColor("#fbbf24")); // Yellow
        } else if (!m.watermarkFields.isEmpty()) {
            wmItem->setText(m.watermarkFields.join(", "));
        } else {
            wmItem->setText("Default");
            wmItem->setForeground(QColor("#888"));
        }
        m_memberTable->setItem(row, 5, wmItem);

        // Active
        QTableWidgetItem* activeItem = new QTableWidgetItem(m.active ? "Yes" : "No");
        activeItem->setTextAlignment(Qt::AlignCenter);
        if (!m.active) {
            activeItem->setForeground(QColor("#888"));
        }
        m_memberTable->setItem(row, 6, activeItem);
    }
}

QString MemberRegistryPanel::getSelectedMemberId() const {
    int row = m_memberTable->currentRow();
    if (row < 0) return QString();
    QTableWidgetItem* item = m_memberTable->item(row, 0);
    if (!item) return QString();
    return item->data(Qt::UserRole).toString();
}

void MemberRegistryPanel::onTableSelectionChanged() {
    QString memberId = getSelectedMemberId();
    bool hasSelection = !memberId.isEmpty();

    m_editBtn->setEnabled(hasSelection);
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

void MemberRegistryPanel::onSearchChanged(const QString& text) {
    Q_UNUSED(text);
    populateTable();
}

void MemberRegistryPanel::onFilterChanged() {
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
    wmFieldsLabel->setStyleSheet("color: #888;");
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
    previewLabel->setStyleSheet("font-family: monospace; color: #d4a760; padding: 8px; background: #2a2a2a; border-radius: 4px;");
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
    distLabel->setStyleSheet("color: #888;");
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
    lastSyncLabel->setStyleSheet("color: #888;");
    wpForm->addRow("Last Synced:", lastSyncLabel);

    distLayout->addWidget(wpGroup);
    distLayout->addStretch();

    tabs->addTab(distTab, "Distribution");

    // === Paths Tab (existing functionality) ===
    QWidget* pathsTab = new QWidget();
    QVBoxLayout* pathsLayout = new QVBoxLayout(pathsTab);

    QLabel* pathsLabel = new QLabel("Legacy path configuration (for archive-based distribution):");
    pathsLabel->setStyleSheet("color: #888;");
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

void MemberRegistryPanel::onRemoveMember() {
    QString memberId = getSelectedMemberId();
    if (memberId.isEmpty()) return;

    MemberInfo info = m_registry->getMember(memberId);

    int ret = QMessageBox::question(this, "Remove Member",
        QString("Are you sure you want to remove '%1'?").arg(info.displayName),
        QMessageBox::Yes | QMessageBox::No);

    if (ret == QMessageBox::Yes) {
        m_registry->removeMember(memberId);
    }
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

    if (m_registry->importFromFile(filePath)) {
        refreshTemplate();
        QMessageBox::information(this, "Import", "Members imported successfully");
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

} // namespace MegaCustom
