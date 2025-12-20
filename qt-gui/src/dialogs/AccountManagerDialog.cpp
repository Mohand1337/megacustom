#include "AccountManagerDialog.h"
#include "accounts/AccountManager.h"
#include "widgets/ButtonFactory.h"
#include "styles/ThemeManager.h"
#include "utils/DpiScaler.h"
#include <QMessageBox>
#include <QInputDialog>
#include <QUuid>
#include <QDateTime>
#include <QDebug>

namespace MegaCustom {

// ============================================================================
// AccountManagerDialog
// ============================================================================

AccountManagerDialog::AccountManagerDialog(QWidget* parent)
    : QDialog(parent)
    , m_splitter(nullptr)
    , m_listPanel(nullptr)
    , m_filterEdit(nullptr)
    , m_accountTree(nullptr)
    , m_addAccountBtn(nullptr)
    , m_removeAccountBtn(nullptr)
    , m_detailsStack(nullptr)
    , m_detailsPage(nullptr)
    , m_avatarLabel(nullptr)
    , m_emailLabel(nullptr)
    , m_statusLabel(nullptr)
    , m_storageValueLabel(nullptr)
    , m_storageBar(nullptr)
    , m_lastLoginLabel(nullptr)
    , m_displayNameEdit(nullptr)
    , m_groupCombo(nullptr)
    , m_colorButton(nullptr)
    , m_clearColorBtn(nullptr)
    , m_labelsList(nullptr)
    , m_newLabelEdit(nullptr)
    , m_addLabelBtn(nullptr)
    , m_removeLabelBtn(nullptr)
    , m_notesEdit(nullptr)
    , m_reauthBtn(nullptr)
    , m_setDefaultBtn(nullptr)
    , m_emptyPage(nullptr)
    , m_groupsPanel(nullptr)
    , m_groupsList(nullptr)
    , m_addGroupBtn(nullptr)
    , m_editGroupBtn(nullptr)
    , m_deleteGroupBtn(nullptr)
    , m_closeBtn(nullptr)
    , m_ignoreChanges(false)
{
    setupUI();
    connectSignals();
    refresh();
}

void AccountManagerDialog::setupUI()
{
    setWindowTitle("Account Manager");
    setMinimumSize(DpiScaler::scale(800), DpiScaler::scale(550));
    resize(DpiScaler::scale(900), DpiScaler::scale(600));

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(DpiScaler::scale(16), DpiScaler::scale(16),
                                   DpiScaler::scale(16), DpiScaler::scale(16));
    mainLayout->setSpacing(DpiScaler::scale(16));

    // Title
    QLabel* titleLabel = new QLabel("Account Manager", this);
    titleLabel->setObjectName("DialogTitle");
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    mainLayout->addWidget(titleLabel);

    // Splitter for list and details
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);

    setupAccountListPanel();
    setupAccountDetailsPanel();
    setupGroupsPanel();

    m_splitter->addWidget(m_listPanel);
    m_splitter->addWidget(m_detailsStack);
    m_splitter->addWidget(m_groupsPanel);
    m_splitter->setSizes({280, 380, 200});

    mainLayout->addWidget(m_splitter, 1);

    // Close button
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    m_closeBtn = ButtonFactory::createOutline("Close", this);
    m_closeBtn->setDefault(true);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(m_closeBtn);
    mainLayout->addLayout(buttonLayout);
}

void AccountManagerDialog::setupAccountListPanel()
{
    m_listPanel = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(m_listPanel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(DpiScaler::scale(8));

    // Section header
    QLabel* headerLabel = new QLabel("Accounts", m_listPanel);
    headerLabel->setObjectName("SectionHeader");
    QFont headerFont = headerLabel->font();
    headerFont.setPointSize(12);
    headerFont.setBold(true);
    headerLabel->setFont(headerFont);
    layout->addWidget(headerLabel);

    // Filter
    m_filterEdit = new QLineEdit(m_listPanel);
    m_filterEdit->setPlaceholderText("Filter accounts...");
    m_filterEdit->setClearButtonEnabled(true);
    layout->addWidget(m_filterEdit);

    // Account tree
    m_accountTree = new QTreeWidget(m_listPanel);
    m_accountTree->setHeaderHidden(true);
    m_accountTree->setRootIsDecorated(true);
    m_accountTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_accountTree->setExpandsOnDoubleClick(false);
    layout->addWidget(m_accountTree, 1);

    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(DpiScaler::scale(8));

    m_addAccountBtn = ButtonFactory::createPrimary("+ Add", m_listPanel);
    m_addAccountBtn->setToolTip("Add a new MEGA account");
    btnLayout->addWidget(m_addAccountBtn);

    m_removeAccountBtn = ButtonFactory::createDestructive("Remove", m_listPanel);
    m_removeAccountBtn->setToolTip("Remove selected account");
    m_removeAccountBtn->setEnabled(false);
    btnLayout->addWidget(m_removeAccountBtn);

    btnLayout->addStretch();
    layout->addLayout(btnLayout);
}

void AccountManagerDialog::setupAccountDetailsPanel()
{
    m_detailsStack = new QStackedWidget(this);

    // Empty state page
    m_emptyPage = new QWidget(this);
    QVBoxLayout* emptyLayout = new QVBoxLayout(m_emptyPage);
    QLabel* emptyLabel = new QLabel("Select an account to view details", m_emptyPage);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setStyleSheet(QString("color: %1;").arg(ThemeManager::instance().textDisabled().name()));
    emptyLayout->addStretch();
    emptyLayout->addWidget(emptyLabel);
    emptyLayout->addStretch();
    m_detailsStack->addWidget(m_emptyPage);

    // Details page
    m_detailsPage = new QWidget(this);
    QVBoxLayout* detailsLayout = new QVBoxLayout(m_detailsPage);
    detailsLayout->setContentsMargins(DpiScaler::scale(16), 0, 0, 0);
    detailsLayout->setSpacing(DpiScaler::scale(12));

    // Header section (avatar, email, status)
    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(DpiScaler::scale(12));

    m_avatarLabel = new QLabel(m_detailsPage);
    m_avatarLabel->setFixedSize(DpiScaler::scale(64), DpiScaler::scale(64));
    m_avatarLabel->setAlignment(Qt::AlignCenter);
    QFont avatarFont = m_avatarLabel->font();
    avatarFont.setPointSize(24);
    avatarFont.setBold(true);
    m_avatarLabel->setFont(avatarFont);
    headerLayout->addWidget(m_avatarLabel);

    QVBoxLayout* headerInfoLayout = new QVBoxLayout();
    headerInfoLayout->setSpacing(DpiScaler::scale(4));

    m_emailLabel = new QLabel(m_detailsPage);
    m_emailLabel->setObjectName("AccountEmailLabel");
    QFont emailFont = m_emailLabel->font();
    emailFont.setPointSize(14);
    emailFont.setBold(true);
    m_emailLabel->setFont(emailFont);
    headerInfoLayout->addWidget(m_emailLabel);

    m_statusLabel = new QLabel(m_detailsPage);
    m_statusLabel->setObjectName("AccountStatusLabel");
    headerInfoLayout->addWidget(m_statusLabel);

    m_lastLoginLabel = new QLabel(m_detailsPage);
    m_lastLoginLabel->setObjectName("LastLoginLabel");
    QFont smallFont = m_lastLoginLabel->font();
    smallFont.setPointSize(9);
    m_lastLoginLabel->setFont(smallFont);
    headerInfoLayout->addWidget(m_lastLoginLabel);

    headerLayout->addLayout(headerInfoLayout, 1);
    detailsLayout->addLayout(headerLayout);

    // Storage section
    QHBoxLayout* storageLayout = new QHBoxLayout();
    QLabel* storageLabel = new QLabel("Storage:", m_detailsPage);
    storageLayout->addWidget(storageLabel);
    m_storageBar = new QProgressBar(m_detailsPage);
    m_storageBar->setMinimum(0);
    m_storageBar->setMaximum(100);
    m_storageBar->setTextVisible(false);
    m_storageBar->setFixedHeight(DpiScaler::scale(8));
    storageLayout->addWidget(m_storageBar, 1);
    m_storageValueLabel = new QLabel(m_detailsPage);
    storageLayout->addWidget(m_storageValueLabel);
    detailsLayout->addLayout(storageLayout);

    // Separator
    QFrame* sep1 = new QFrame(m_detailsPage);
    sep1->setFrameShape(QFrame::HLine);
    detailsLayout->addWidget(sep1);

    // Display name
    QHBoxLayout* nameLayout = new QHBoxLayout();
    QLabel* nameLabel = new QLabel("Display Name:", m_detailsPage);
    nameLabel->setFixedWidth(DpiScaler::scale(100));
    nameLayout->addWidget(nameLabel);
    m_displayNameEdit = new QLineEdit(m_detailsPage);
    m_displayNameEdit->setPlaceholderText("Optional friendly name");
    nameLayout->addWidget(m_displayNameEdit, 1);
    detailsLayout->addLayout(nameLayout);

    // Group
    QHBoxLayout* groupLayout = new QHBoxLayout();
    QLabel* groupLabel = new QLabel("Group:", m_detailsPage);
    groupLabel->setFixedWidth(DpiScaler::scale(100));
    groupLayout->addWidget(groupLabel);
    m_groupCombo = new QComboBox(m_detailsPage);
    groupLayout->addWidget(m_groupCombo, 1);
    detailsLayout->addLayout(groupLayout);

    // Color - color button is special (not using ButtonFactory)
    QHBoxLayout* colorLayout = new QHBoxLayout();
    QLabel* colorLabel = new QLabel("Color:", m_detailsPage);
    colorLabel->setFixedWidth(DpiScaler::scale(100));
    colorLayout->addWidget(colorLabel);
    m_colorButton = new QPushButton(m_detailsPage);
    m_colorButton->setFixedSize(DpiScaler::scale(32), DpiScaler::scale(24));
    m_colorButton->setToolTip("Choose custom color");
    colorLayout->addWidget(m_colorButton);
    m_clearColorBtn = ButtonFactory::createSecondary("Clear", m_detailsPage);
    m_clearColorBtn->setToolTip("Use group color");
    colorLayout->addWidget(m_clearColorBtn);
    colorLayout->addStretch();
    detailsLayout->addLayout(colorLayout);

    // Labels section
    QLabel* labelsHeader = new QLabel("Labels:", m_detailsPage);
    detailsLayout->addWidget(labelsHeader);

    m_labelsList = new QListWidget(m_detailsPage);
    m_labelsList->setMaximumHeight(DpiScaler::scale(80));
    m_labelsList->setSelectionMode(QAbstractItemView::SingleSelection);
    detailsLayout->addWidget(m_labelsList);

    QHBoxLayout* labelBtnLayout = new QHBoxLayout();
    m_newLabelEdit = new QLineEdit(m_detailsPage);
    m_newLabelEdit->setPlaceholderText("New label...");
    labelBtnLayout->addWidget(m_newLabelEdit, 1);
    m_addLabelBtn = ButtonFactory::createSecondary("+", m_detailsPage);
    m_addLabelBtn->setFixedWidth(DpiScaler::scale(30));
    labelBtnLayout->addWidget(m_addLabelBtn);
    m_removeLabelBtn = ButtonFactory::createSecondary("-", m_detailsPage);
    m_removeLabelBtn->setFixedWidth(DpiScaler::scale(30));
    m_removeLabelBtn->setEnabled(false);
    labelBtnLayout->addWidget(m_removeLabelBtn);
    detailsLayout->addLayout(labelBtnLayout);

    // Notes
    QLabel* notesLabel = new QLabel("Notes:", m_detailsPage);
    detailsLayout->addWidget(notesLabel);
    m_notesEdit = new QTextEdit(m_detailsPage);
    m_notesEdit->setMaximumHeight(DpiScaler::scale(60));
    m_notesEdit->setPlaceholderText("Optional notes about this account...");
    detailsLayout->addWidget(m_notesEdit);

    // Action buttons
    QHBoxLayout* actionLayout = new QHBoxLayout();
    m_reauthBtn = ButtonFactory::createSecondary("Re-authenticate", m_detailsPage);
    m_reauthBtn->setToolTip("Log in again if session expired");
    actionLayout->addWidget(m_reauthBtn);
    m_setDefaultBtn = ButtonFactory::createPrimary("Set as Default", m_detailsPage);
    m_setDefaultBtn->setToolTip("Use this account on startup");
    actionLayout->addWidget(m_setDefaultBtn);
    actionLayout->addStretch();
    detailsLayout->addLayout(actionLayout);

    detailsLayout->addStretch();

    m_detailsStack->addWidget(m_detailsPage);
    m_detailsStack->setCurrentWidget(m_emptyPage);
}

void AccountManagerDialog::setupGroupsPanel()
{
    m_groupsPanel = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(m_groupsPanel);
    layout->setContentsMargins(DpiScaler::scale(16), 0, 0, 0);
    layout->setSpacing(DpiScaler::scale(8));

    // Section header
    QLabel* headerLabel = new QLabel("Groups", m_groupsPanel);
    headerLabel->setObjectName("SectionHeader");
    QFont headerFont = headerLabel->font();
    headerFont.setPointSize(12);
    headerFont.setBold(true);
    headerLabel->setFont(headerFont);
    layout->addWidget(headerLabel);

    // Groups list
    m_groupsList = new QListWidget(m_groupsPanel);
    m_groupsList->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_groupsList, 1);

    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(DpiScaler::scale(4));

    m_addGroupBtn = ButtonFactory::createSecondary("+", m_groupsPanel);
    m_addGroupBtn->setFixedWidth(DpiScaler::scale(30));
    m_addGroupBtn->setToolTip("Add group");
    btnLayout->addWidget(m_addGroupBtn);

    m_editGroupBtn = ButtonFactory::createSecondary("Edit", m_groupsPanel);
    m_editGroupBtn->setEnabled(false);
    btnLayout->addWidget(m_editGroupBtn);

    m_deleteGroupBtn = ButtonFactory::createDestructive("Delete", m_groupsPanel);
    m_deleteGroupBtn->setEnabled(false);
    btnLayout->addWidget(m_deleteGroupBtn);

    btnLayout->addStretch();
    layout->addLayout(btnLayout);
}

void AccountManagerDialog::connectSignals()
{
    // Account list
    connect(m_filterEdit, &QLineEdit::textChanged,
            this, &AccountManagerDialog::onAccountFilterChanged);
    connect(m_accountTree, &QTreeWidget::itemClicked,
            this, &AccountManagerDialog::onAccountItemClicked);
    connect(m_addAccountBtn, &QPushButton::clicked,
            this, &AccountManagerDialog::onAddAccountClicked);
    connect(m_removeAccountBtn, &QPushButton::clicked,
            this, &AccountManagerDialog::onRemoveAccountClicked);

    // Account details
    connect(m_displayNameEdit, &QLineEdit::editingFinished,
            this, [this]() { onDisplayNameChanged(m_displayNameEdit->text()); });
    connect(m_groupCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AccountManagerDialog::onGroupChanged);
    connect(m_colorButton, &QPushButton::clicked,
            this, &AccountManagerDialog::onColorButtonClicked);
    connect(m_clearColorBtn, &QPushButton::clicked,
            this, &AccountManagerDialog::onClearColorClicked);
    connect(m_notesEdit, &QTextEdit::textChanged,
            this, &AccountManagerDialog::onNotesChanged);
    connect(m_reauthBtn, &QPushButton::clicked,
            this, &AccountManagerDialog::onReauthenticateClicked);
    connect(m_setDefaultBtn, &QPushButton::clicked,
            this, &AccountManagerDialog::onSetDefaultClicked);

    // Labels
    connect(m_addLabelBtn, &QPushButton::clicked,
            this, &AccountManagerDialog::onAddLabelClicked);
    connect(m_removeLabelBtn, &QPushButton::clicked,
            this, &AccountManagerDialog::onRemoveLabelClicked);
    connect(m_labelsList, &QListWidget::itemSelectionChanged, this, [this]() {
        m_removeLabelBtn->setEnabled(m_labelsList->currentItem() != nullptr);
    });
    connect(m_newLabelEdit, &QLineEdit::returnPressed,
            this, &AccountManagerDialog::onAddLabelClicked);

    // Groups
    connect(m_addGroupBtn, &QPushButton::clicked,
            this, &AccountManagerDialog::onAddGroupClicked);
    connect(m_editGroupBtn, &QPushButton::clicked,
            this, &AccountManagerDialog::onEditGroupClicked);
    connect(m_deleteGroupBtn, &QPushButton::clicked,
            this, &AccountManagerDialog::onDeleteGroupClicked);
    connect(m_groupsList, &QListWidget::itemSelectionChanged, this, [this]() {
        bool hasSelection = m_groupsList->currentItem() != nullptr;
        m_editGroupBtn->setEnabled(hasSelection);
        // Don't allow deleting the default group
        if (hasSelection) {
            QString groupId = m_groupsList->currentItem()->data(Qt::UserRole).toString();
            m_deleteGroupBtn->setEnabled(groupId != "default");
        } else {
            m_deleteGroupBtn->setEnabled(false);
        }
    });
    connect(m_groupsList, &QListWidget::itemDoubleClicked,
            this, &AccountManagerDialog::onEditGroupClicked);

    // AccountManager signals
    AccountManager& mgr = AccountManager::instance();
    connect(&mgr, &AccountManager::accountAdded,
            this, &AccountManagerDialog::onAccountAdded);
    connect(&mgr, &AccountManager::accountRemoved,
            this, &AccountManagerDialog::onAccountRemoved);
    connect(&mgr, &AccountManager::accountUpdated,
            this, &AccountManagerDialog::onAccountUpdated);
    connect(&mgr, &AccountManager::sessionReady,
            this, &AccountManagerDialog::onSessionReady);
    connect(&mgr, &AccountManager::sessionError,
            this, &AccountManagerDialog::onSessionError);
}

void AccountManagerDialog::refresh()
{
    populateAccountTree();
    populateGroupCombo();

    // Refresh groups list
    m_groupsList->clear();
    for (const AccountGroup& group : AccountManager::instance().allGroups()) {
        QListWidgetItem* item = new QListWidgetItem(m_groupsList);
        item->setText(group.name);
        item->setData(Qt::UserRole, group.id);

        // Color indicator
        QPixmap colorPixmap(DpiScaler::scale(12), DpiScaler::scale(12));
        colorPixmap.fill(group.color.isValid() ? group.color : ThemeManager::instance().brandDefault());
        item->setIcon(QIcon(colorPixmap));
    }
}

void AccountManagerDialog::populateAccountTree(const QString& filter)
{
    m_accountTree->clear();

    AccountManager& mgr = AccountManager::instance();
    QList<AccountGroup> groups = mgr.allGroups();
    QString activeId = mgr.activeAccountId();

    for (const AccountGroup& group : groups) {
        QList<MegaAccount> accounts = mgr.accountsInGroup(group.id);

        // Filter accounts if needed
        if (!filter.isEmpty()) {
            QList<MegaAccount> filtered;
            for (const MegaAccount& acc : accounts) {
                if (acc.matchesSearch(filter)) {
                    filtered.append(acc);
                }
            }
            accounts = filtered;
        }

        if (accounts.isEmpty() && !filter.isEmpty()) {
            continue; // Skip empty groups when filtering
        }

        // Create group item
        QTreeWidgetItem* groupItem = createGroupItem(group);
        m_accountTree->addTopLevelItem(groupItem);

        // Add accounts
        for (const MegaAccount& account : accounts) {
            QTreeWidgetItem* accountItem = createAccountItem(account);
            groupItem->addChild(accountItem);

            // Mark active account
            if (account.id == activeId) {
                accountItem->setIcon(0, QIcon(":/icons/check.svg"));
            }
        }

        groupItem->setExpanded(true);
    }
}

void AccountManagerDialog::populateGroupCombo()
{
    m_groupCombo->clear();

    for (const AccountGroup& group : AccountManager::instance().allGroups()) {
        m_groupCombo->addItem(group.name, group.id);
    }
}

QTreeWidgetItem* AccountManagerDialog::createAccountItem(const MegaAccount& account)
{
    QTreeWidgetItem* item = new QTreeWidgetItem();

    QString displayText = account.email;
    if (!account.displayName.isEmpty()) {
        displayText = QString("%1 (%2)").arg(account.displayName, account.email);
    }
    item->setText(0, displayText);
    item->setData(0, Qt::UserRole, account.id);
    item->setData(0, Qt::UserRole + 1, "account");

    // Status color
    QColor statusColor = getStatusColor(account.id);
    item->setForeground(0, statusColor);

    return item;
}

QTreeWidgetItem* AccountManagerDialog::createGroupItem(const AccountGroup& group)
{
    QTreeWidgetItem* item = new QTreeWidgetItem();
    item->setText(0, group.name);
    item->setData(0, Qt::UserRole, group.id);
    item->setData(0, Qt::UserRole + 1, "group");

    // Color indicator
    QPixmap colorPixmap(DpiScaler::scale(12), DpiScaler::scale(12));
    colorPixmap.fill(group.color.isValid() ? group.color : ThemeManager::instance().brandDefault());
    item->setIcon(0, QIcon(colorPixmap));

    // Bold font for groups
    QFont font = item->font(0);
    font.setBold(true);
    item->setFont(0, font);

    return item;
}

QColor AccountManagerDialog::getStatusColor(const QString& accountId) const
{
    auto& tm = ThemeManager::instance();
    AccountManager& mgr = AccountManager::instance();
    if (mgr.isLoggedIn(accountId)) {
        return tm.supportSuccess(); // Green - connected
    }
    return tm.textDisabled(); // Gray - disconnected
}

void AccountManagerDialog::showAccountDetails(const QString& accountId)
{
    m_ignoreChanges = true;
    m_currentAccountId = accountId;

    MegaAccount account = AccountManager::instance().getAccount(accountId);
    if (account.id.isEmpty()) {
        clearAccountDetails();
        return;
    }

    // Avatar
    QString initials;
    if (!account.displayName.isEmpty()) {
        QStringList parts = account.displayName.split(' ', Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            initials = QString("%1%2").arg(parts[0][0].toUpper()).arg(parts[1][0].toUpper());
        } else if (!parts.isEmpty()) {
            initials = parts[0].left(2).toUpper();
        }
    }
    if (initials.isEmpty()) {
        int atPos = account.email.indexOf('@');
        QString localPart = (atPos > 0) ? account.email.left(atPos) : account.email;
        initials = localPart.left(2).toUpper();
    }
    m_avatarLabel->setText(initials);

    QColor avatarColor = account.color.isValid() ? account.color : ThemeManager::instance().brandDefault();
    m_avatarLabel->setStyleSheet(QString(
        "background-color: %1; color: white; border-radius: %2px;")
        .arg(avatarColor.name())
        .arg(DpiScaler::scale(32)));

    // Email and status
    m_emailLabel->setText(account.email);

    bool isLoggedIn = AccountManager::instance().isLoggedIn(accountId);
    if (isLoggedIn) {
        m_statusLabel->setText("Connected");
        m_statusLabel->setStyleSheet(QString("color: %1;").arg(ThemeManager::instance().supportSuccess().name()));
    } else {
        m_statusLabel->setText("Disconnected");
        m_statusLabel->setStyleSheet(QString("color: %1;").arg(ThemeManager::instance().textDisabled().name()));
    }

    // Last login
    if (account.lastLogin.isValid()) {
        m_lastLoginLabel->setText("Last login: " + account.lastLogin.toString("MMM d, yyyy h:mm AP"));
    } else {
        m_lastLoginLabel->setText("Last login: Never");
    }

    // Storage
    if (account.storageTotal > 0) {
        int percent = static_cast<int>((account.storageUsed * 100) / account.storageTotal);
        m_storageBar->setValue(percent);
        m_storageValueLabel->setText(QString("%1 / %2 (%3%)")
            .arg(formatBytes(account.storageUsed))
            .arg(formatBytes(account.storageTotal))
            .arg(percent));
    } else {
        m_storageBar->setValue(0);
        m_storageValueLabel->setText("Unknown");
    }

    // Details fields
    m_displayNameEdit->setText(account.displayName);

    // Group
    int groupIndex = m_groupCombo->findData(account.groupId);
    if (groupIndex >= 0) {
        m_groupCombo->setCurrentIndex(groupIndex);
    }

    // Color
    m_selectedColor = account.color;
    if (m_selectedColor.isValid()) {
        m_colorButton->setStyleSheet(QString("background-color: %1;").arg(m_selectedColor.name()));
    } else {
        m_colorButton->setStyleSheet("");
    }

    // Labels
    m_labelsList->clear();
    for (const QString& label : account.labels) {
        m_labelsList->addItem(label);
    }

    // Notes
    m_notesEdit->setPlainText(account.notes);

    // Default button state
    m_setDefaultBtn->setEnabled(!account.isDefault);
    if (account.isDefault) {
        m_setDefaultBtn->setText("Default Account");
    } else {
        m_setDefaultBtn->setText("Set as Default");
    }

    m_detailsStack->setCurrentWidget(m_detailsPage);
    m_removeAccountBtn->setEnabled(true);

    m_ignoreChanges = false;
}

void AccountManagerDialog::clearAccountDetails()
{
    m_currentAccountId.clear();
    m_detailsStack->setCurrentWidget(m_emptyPage);
    m_removeAccountBtn->setEnabled(false);
}

void AccountManagerDialog::saveCurrentAccountChanges()
{
    if (m_currentAccountId.isEmpty() || m_ignoreChanges) {
        return;
    }

    MegaAccount account = AccountManager::instance().getAccount(m_currentAccountId);
    if (account.id.isEmpty()) {
        return;
    }

    account.displayName = m_displayNameEdit->text().trimmed();
    account.groupId = m_groupCombo->currentData().toString();
    account.color = m_selectedColor;
    account.notes = m_notesEdit->toPlainText();

    // Collect labels
    account.labels.clear();
    for (int i = 0; i < m_labelsList->count(); ++i) {
        account.labels.append(m_labelsList->item(i)->text());
    }

    AccountManager::instance().updateAccount(account);
}

QString AccountManagerDialog::formatBytes(qint64 bytes) const
{
    const qint64 KB = 1024;
    const qint64 MB = KB * 1024;
    const qint64 GB = MB * 1024;
    const qint64 TB = GB * 1024;

    if (bytes >= TB) {
        return QString::number(bytes / static_cast<double>(TB), 'f', 1) + " TB";
    } else if (bytes >= GB) {
        return QString::number(bytes / static_cast<double>(GB), 'f', 1) + " GB";
    } else if (bytes >= MB) {
        return QString::number(bytes / static_cast<double>(MB), 'f', 1) + " MB";
    } else if (bytes >= KB) {
        return QString::number(bytes / static_cast<double>(KB), 'f', 0) + " KB";
    } else {
        return QString::number(bytes) + " B";
    }
}

// ============================================================================
// Slots - Account List
// ============================================================================

void AccountManagerDialog::onAccountItemClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)

    if (!item) {
        clearAccountDetails();
        return;
    }

    QString type = item->data(0, Qt::UserRole + 1).toString();
    if (type == "account") {
        QString accountId = item->data(0, Qt::UserRole).toString();
        showAccountDetails(accountId);
        emit accountSelected(accountId);
    } else {
        clearAccountDetails();
    }
}

void AccountManagerDialog::onAccountFilterChanged(const QString& text)
{
    populateAccountTree(text);
}

void AccountManagerDialog::onAddAccountClicked()
{
    // This would typically open a login dialog
    // For now, emit a signal or show the main login dialog
    QMessageBox::information(this, "Add Account",
        "To add a new account, use the Login dialog from the main window.\n\n"
        "Close this dialog and click 'Add Account' from the account switcher.");
}

void AccountManagerDialog::onRemoveAccountClicked()
{
    if (m_currentAccountId.isEmpty()) {
        return;
    }

    MegaAccount account = AccountManager::instance().getAccount(m_currentAccountId);
    if (account.id.isEmpty()) {
        return;
    }

    QString message = QString("Are you sure you want to remove the account '%1'?\n\n"
                              "This will log out and remove stored credentials.")
                          .arg(account.email);

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Remove Account", message,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        AccountManager::instance().removeAccount(m_currentAccountId, true);
        clearAccountDetails();
    }
}

// ============================================================================
// Slots - Account Details
// ============================================================================

void AccountManagerDialog::onDisplayNameChanged(const QString& text)
{
    Q_UNUSED(text)
    saveCurrentAccountChanges();
}

void AccountManagerDialog::onGroupChanged(int index)
{
    Q_UNUSED(index)
    saveCurrentAccountChanges();
    populateAccountTree(m_filterEdit->text());
}

void AccountManagerDialog::onColorButtonClicked()
{
    QColor color = QColorDialog::getColor(
        m_selectedColor.isValid() ? m_selectedColor : ThemeManager::instance().brandDefault(),
        this, "Choose Account Color");

    if (color.isValid()) {
        m_selectedColor = color;
        m_colorButton->setStyleSheet(QString("background-color: %1;").arg(color.name()));
        saveCurrentAccountChanges();

        // Refresh avatar
        m_avatarLabel->setStyleSheet(QString(
            "background-color: %1; color: white; border-radius: %2px;")
            .arg(color.name())
            .arg(DpiScaler::scale(32)));
    }
}

void AccountManagerDialog::onClearColorClicked()
{
    m_selectedColor = QColor();
    m_colorButton->setStyleSheet("");
    saveCurrentAccountChanges();

    // Refresh avatar with group color
    MegaAccount account = AccountManager::instance().getAccount(m_currentAccountId);
    AccountGroup group = AccountManager::instance().getGroup(account.groupId);
    QColor color = group.color.isValid() ? group.color : ThemeManager::instance().brandDefault();
    m_avatarLabel->setStyleSheet(QString(
        "background-color: %1; color: white; border-radius: %2px;")
        .arg(color.name())
        .arg(DpiScaler::scale(32)));
}

void AccountManagerDialog::onNotesChanged()
{
    // Debounce this - don't save on every keystroke
    // For simplicity, we save when focus is lost (handled elsewhere)
}

void AccountManagerDialog::onReauthenticateClicked()
{
    QMessageBox::information(this, "Re-authenticate",
        "To re-authenticate, close this dialog and use the Login dialog.\n\n"
        "The existing session will be refreshed with new credentials.");
}

void AccountManagerDialog::onSetDefaultClicked()
{
    if (m_currentAccountId.isEmpty()) {
        return;
    }

    // Clear default from all accounts
    for (const MegaAccount& acc : AccountManager::instance().allAccounts()) {
        if (acc.isDefault && acc.id != m_currentAccountId) {
            MegaAccount updated = acc;
            updated.isDefault = false;
            AccountManager::instance().updateAccount(updated);
        }
    }

    // Set current as default
    MegaAccount account = AccountManager::instance().getAccount(m_currentAccountId);
    account.isDefault = true;
    AccountManager::instance().updateAccount(account);

    m_setDefaultBtn->setEnabled(false);
    m_setDefaultBtn->setText("Default Account");
}

// ============================================================================
// Slots - Labels
// ============================================================================

void AccountManagerDialog::onAddLabelClicked()
{
    QString label = m_newLabelEdit->text().trimmed();
    if (label.isEmpty()) {
        return;
    }

    // Check for duplicates
    for (int i = 0; i < m_labelsList->count(); ++i) {
        if (m_labelsList->item(i)->text().compare(label, Qt::CaseInsensitive) == 0) {
            return;
        }
    }

    m_labelsList->addItem(label);
    m_newLabelEdit->clear();
    saveCurrentAccountChanges();
}

void AccountManagerDialog::onRemoveLabelClicked()
{
    QListWidgetItem* item = m_labelsList->currentItem();
    if (item) {
        delete item;
        saveCurrentAccountChanges();
    }
}

// ============================================================================
// Slots - Groups
// ============================================================================

void AccountManagerDialog::onAddGroupClicked()
{
    GroupEditDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        AccountGroup group = dialog.getGroup();
        AccountManager::instance().addGroup(group);
        refresh();
    }
}

void AccountManagerDialog::onEditGroupClicked()
{
    QListWidgetItem* item = m_groupsList->currentItem();
    if (!item) {
        return;
    }

    QString groupId = item->data(Qt::UserRole).toString();
    AccountGroup group = AccountManager::instance().getGroup(groupId);

    GroupEditDialog dialog(group, this);
    if (dialog.exec() == QDialog::Accepted) {
        AccountGroup updated = dialog.getGroup();
        AccountManager::instance().updateGroup(updated);
        refresh();
    }
}

void AccountManagerDialog::onDeleteGroupClicked()
{
    QListWidgetItem* item = m_groupsList->currentItem();
    if (!item) {
        return;
    }

    QString groupId = item->data(Qt::UserRole).toString();
    if (groupId == "default") {
        QMessageBox::warning(this, "Cannot Delete",
            "The default group cannot be deleted.");
        return;
    }

    AccountGroup group = AccountManager::instance().getGroup(groupId);
    QList<MegaAccount> accounts = AccountManager::instance().accountsInGroup(groupId);

    QString message = QString("Delete group '%1'?").arg(group.name);
    if (!accounts.isEmpty()) {
        message += QString("\n\n%1 account(s) will be moved to the default group.")
                       .arg(accounts.size());
    }

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Delete Group", message,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        AccountManager::instance().removeGroup(groupId, true);
        refresh();
    }
}

// ============================================================================
// Slots - AccountManager Signals
// ============================================================================

void AccountManagerDialog::onAccountAdded(const MegaAccount& account)
{
    Q_UNUSED(account)
    refresh();
}

void AccountManagerDialog::onAccountRemoved(const QString& accountId)
{
    if (accountId == m_currentAccountId) {
        clearAccountDetails();
    }
    refresh();
}

void AccountManagerDialog::onAccountUpdated(const MegaAccount& account)
{
    if (account.id == m_currentAccountId) {
        showAccountDetails(m_currentAccountId);
    }
    populateAccountTree(m_filterEdit->text());
}

void AccountManagerDialog::onSessionReady(const QString& accountId)
{
    if (accountId == m_currentAccountId) {
        m_statusLabel->setText("Connected");
        m_statusLabel->setStyleSheet(QString("color: %1;").arg(ThemeManager::instance().supportSuccess().name()));
    }
    populateAccountTree(m_filterEdit->text());
}

void AccountManagerDialog::onSessionError(const QString& accountId, const QString& error)
{
    if (accountId == m_currentAccountId) {
        m_statusLabel->setText("Error: " + error);
        m_statusLabel->setStyleSheet(QString("color: %1;").arg(ThemeManager::instance().supportError().name()));
    }
}

// ============================================================================
// GroupEditDialog
// ============================================================================

GroupEditDialog::GroupEditDialog(QWidget* parent)
    : QDialog(parent)
    , m_nameEdit(nullptr)
    , m_colorButton(nullptr)
    , m_selectedColor(ThemeManager::instance().brandDefault())
    , m_okButton(nullptr)
    , m_cancelButton(nullptr)
{
    setupUI();
    setWindowTitle("New Group");
}

GroupEditDialog::GroupEditDialog(const AccountGroup& group, QWidget* parent)
    : QDialog(parent)
    , m_nameEdit(nullptr)
    , m_colorButton(nullptr)
    , m_selectedColor(group.color.isValid() ? group.color : ThemeManager::instance().brandDefault())
    , m_okButton(nullptr)
    , m_cancelButton(nullptr)
    , m_groupId(group.id)
{
    setupUI();
    setWindowTitle("Edit Group");
    m_nameEdit->setText(group.name);
    m_colorButton->setStyleSheet(QString("background-color: %1;").arg(m_selectedColor.name()));
}

void GroupEditDialog::setupUI()
{
    setFixedSize(DpiScaler::scale(300), DpiScaler::scale(150));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setSpacing(DpiScaler::scale(12));

    // Name
    QHBoxLayout* nameLayout = new QHBoxLayout();
    QLabel* nameLabel = new QLabel("Name:", this);
    nameLabel->setFixedWidth(DpiScaler::scale(60));
    nameLayout->addWidget(nameLabel);
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText("Group name");
    nameLayout->addWidget(m_nameEdit);
    layout->addLayout(nameLayout);

    // Color
    QHBoxLayout* colorLayout = new QHBoxLayout();
    QLabel* colorLabel = new QLabel("Color:", this);
    colorLabel->setFixedWidth(DpiScaler::scale(60));
    colorLayout->addWidget(colorLabel);
    m_colorButton = new QPushButton(this);
    m_colorButton->setFixedSize(DpiScaler::scale(60), DpiScaler::scale(24));
    m_colorButton->setStyleSheet(QString("background-color: %1;").arg(m_selectedColor.name()));
    connect(m_colorButton, &QPushButton::clicked, this, &GroupEditDialog::onColorButtonClicked);
    colorLayout->addWidget(m_colorButton);
    colorLayout->addStretch();
    layout->addLayout(colorLayout);

    layout->addStretch();

    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_cancelButton = ButtonFactory::createOutline("Cancel", this);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(m_cancelButton);
    m_okButton = ButtonFactory::createPrimary("OK", this);
    m_okButton->setDefault(true);
    m_okButton->setEnabled(false);
    connect(m_okButton, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(m_okButton);
    layout->addLayout(btnLayout);

    // Validation
    connect(m_nameEdit, &QLineEdit::textChanged, this, &GroupEditDialog::validate);
}

void GroupEditDialog::onColorButtonClicked()
{
    QColor color = QColorDialog::getColor(m_selectedColor, this, "Choose Group Color");
    if (color.isValid()) {
        m_selectedColor = color;
        m_colorButton->setStyleSheet(QString("background-color: %1;").arg(color.name()));
    }
}

void GroupEditDialog::validate()
{
    m_okButton->setEnabled(!m_nameEdit->text().trimmed().isEmpty());
}

AccountGroup GroupEditDialog::getGroup() const
{
    AccountGroup group;
    group.id = m_groupId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : m_groupId;
    group.name = m_nameEdit->text().trimmed();
    group.color = m_selectedColor;
    group.sortOrder = 0;
    group.collapsed = false;
    return group;
}

} // namespace MegaCustom
