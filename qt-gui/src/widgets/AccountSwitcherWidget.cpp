#include "AccountSwitcherWidget.h"
#include "accounts/AccountManager.h"
#include "styles/ThemeManager.h"
#include "utils/DpiScaler.h"
#include <QProgressBar>
#include <QMouseEvent>
#include <QIcon>
#include <QDebug>
#include <QWindow>

namespace MegaCustom {

// ============================================================================
// AccountSwitcherWidget
// ============================================================================

AccountSwitcherWidget::AccountSwitcherWidget(QWidget* parent)
    : QWidget(parent)
    , m_headerFrame(nullptr)
    , m_avatarLabel(nullptr)
    , m_emailLabel(nullptr)
    , m_nameLabel(nullptr)
    , m_expandButton(nullptr)
    , m_storageBar(nullptr)
    , m_dropdownFrame(nullptr)
    , m_searchBox(nullptr)
    , m_accountList(nullptr)
    , m_addAccountBtn(nullptr)
    , m_manageAccountsBtn(nullptr)
    , m_dropdownAnimation(nullptr)
    , m_expanded(false)
    , m_mainLayout(nullptr)
{
    setupUI();
    connectSignals();
    refresh();
}

void AccountSwitcherWidget::setupUI()
{
    setObjectName("AccountSwitcherWidget");

    // Fixed vertical size policy - prevents layout propagation when dropdown expands
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    setupHeaderSection();
    setupDropdownSection();

    // Initially hide dropdown
    m_dropdownFrame->setMaximumHeight(0);
    m_dropdownFrame->setVisible(false);
}

void AccountSwitcherWidget::setupHeaderSection()
{
    m_headerFrame = new QFrame(this);
    m_headerFrame->setObjectName("AccountHeader");
    m_headerFrame->setCursor(Qt::PointingHandCursor);

    QHBoxLayout* headerLayout = new QHBoxLayout(m_headerFrame);
    headerLayout->setContentsMargins(12, 10, 12, 10);
    headerLayout->setSpacing(10);

    // Simple avatar label (colored circle with letter)
    m_avatarLabel = new QLabel(m_headerFrame);
    m_avatarLabel->setObjectName("AccountAvatar");
    m_avatarLabel->setFixedSize(36, 36);
    m_avatarLabel->setAlignment(Qt::AlignCenter);
    headerLayout->addWidget(m_avatarLabel);

    // Account info column
    QVBoxLayout* infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(2);
    infoLayout->setContentsMargins(0, 0, 0, 0);

    m_emailLabel = new QLabel(m_headerFrame);
    m_emailLabel->setObjectName("AccountEmail");
    infoLayout->addWidget(m_emailLabel);

    m_nameLabel = new QLabel(m_headerFrame);
    m_nameLabel->setObjectName("AccountName");
    infoLayout->addWidget(m_nameLabel);

    // Storage bar for active account
    m_storageBar = new QProgressBar(m_headerFrame);
    m_storageBar->setObjectName("HeaderStorageBar");
    m_storageBar->setFixedHeight(4);
    m_storageBar->setTextVisible(false);
    m_storageBar->setMinimum(0);
    m_storageBar->setMaximum(100);
    m_storageBar->setVisible(false);  // Hidden until storage info available
    infoLayout->addWidget(m_storageBar);

    headerLayout->addLayout(infoLayout, 1);

    // Expand button (chevron)
    m_expandButton = new QPushButton(m_headerFrame);
    m_expandButton->setObjectName("AccountExpandButton");
    m_expandButton->setFixedSize(24, 24);
    m_expandButton->setFlat(true);
    m_expandButton->setCursor(Qt::PointingHandCursor);
    m_expandButton->setIcon(QIcon(":/icons/chevron-down.svg"));
    m_expandButton->setIconSize(QSize(16, 16));
    m_expandButton->setToolTip("Switch accounts");
    headerLayout->addWidget(m_expandButton);

    m_mainLayout->addWidget(m_headerFrame);
}

void AccountSwitcherWidget::setupDropdownSection()
{
    m_dropdownFrame = new QFrame(this);
    m_dropdownFrame->setObjectName("AccountDropdown");

    QVBoxLayout* dropdownLayout = new QVBoxLayout(m_dropdownFrame);
    dropdownLayout->setContentsMargins(12, 8, 12, 12);
    dropdownLayout->setSpacing(8);

    // Search box
    m_searchBox = new QLineEdit(m_dropdownFrame);
    m_searchBox->setObjectName("AccountSearchBox");
    m_searchBox->setPlaceholderText("Search accounts...");
    m_searchBox->setClearButtonEnabled(true);
    m_searchBox->setFixedHeight(32);
    dropdownLayout->addWidget(m_searchBox);

    // Account list
    m_accountList = new QListWidget(m_dropdownFrame);
    m_accountList->setObjectName("AccountList");
    m_accountList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_accountList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_accountList->setMinimumHeight(100);
    m_accountList->setMaximumHeight(250);
    m_accountList->setFrameShape(QFrame::NoFrame);
    dropdownLayout->addWidget(m_accountList);

    // Separator line
    QFrame* separator = new QFrame(m_dropdownFrame);
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet(QString("background-color: %1; max-height: 1px;")
        .arg(ThemeManager::instance().borderSubtle().name()));
    dropdownLayout->addWidget(separator);

    // Buttons row
    QHBoxLayout* buttonsRow = new QHBoxLayout();
    buttonsRow->setSpacing(8);
    buttonsRow->setContentsMargins(0, 4, 0, 0);

    m_addAccountBtn = new QPushButton("+ Add Account", m_dropdownFrame);
    m_addAccountBtn->setObjectName("AddAccountButton");
    m_addAccountBtn->setCursor(Qt::PointingHandCursor);
    m_addAccountBtn->setFlat(true);
    buttonsRow->addWidget(m_addAccountBtn);

    buttonsRow->addStretch();

    m_manageAccountsBtn = new QPushButton("Manage", m_dropdownFrame);
    m_manageAccountsBtn->setObjectName("ManageAccountsButton");
    m_manageAccountsBtn->setCursor(Qt::PointingHandCursor);
    m_manageAccountsBtn->setFlat(true);
    buttonsRow->addWidget(m_manageAccountsBtn);

    dropdownLayout->addLayout(buttonsRow);

    m_mainLayout->addWidget(m_dropdownFrame);

    // Setup animation
    m_dropdownAnimation = new QPropertyAnimation(m_dropdownFrame, "maximumHeight", this);
    m_dropdownAnimation->setDuration(200);
    m_dropdownAnimation->setEasingCurve(QEasingCurve::OutCubic);
}

void AccountSwitcherWidget::connectSignals()
{
    // Header click to expand - both the button and the entire header frame
    connect(m_expandButton, &QPushButton::clicked, this, &AccountSwitcherWidget::onExpandButtonClicked);

    // Make entire header frame clickable
    m_headerFrame->installEventFilter(this);

    // Search
    connect(m_searchBox, &QLineEdit::textChanged, this, &AccountSwitcherWidget::onSearchTextChanged);

    // Account list
    connect(m_accountList, &QListWidget::itemClicked, this, &AccountSwitcherWidget::onAccountItemClicked);

    // Buttons
    connect(m_addAccountBtn, &QPushButton::clicked, this, &AccountSwitcherWidget::addAccountRequested);
    connect(m_manageAccountsBtn, &QPushButton::clicked, this, &AccountSwitcherWidget::manageAccountsRequested);

    // Connect to AccountManager signals
    AccountManager& mgr = AccountManager::instance();
    connect(&mgr, &AccountManager::accountSwitched, this, &AccountSwitcherWidget::onAccountSwitched);
    connect(&mgr, &AccountManager::accountAdded, this, &AccountSwitcherWidget::onAccountAdded);
    connect(&mgr, &AccountManager::accountRemoved, this, &AccountSwitcherWidget::onAccountRemoved);
    connect(&mgr, &AccountManager::accountUpdated, this, &AccountSwitcherWidget::onAccountUpdated);
    connect(&mgr, &AccountManager::storageInfoUpdated, this, &AccountSwitcherWidget::onStorageInfoUpdated);
}

void AccountSwitcherWidget::setExpanded(bool expanded)
{
    if (m_expanded == expanded) return;

    m_expanded = expanded;
    animateDropdown(expanded);

    if (expanded) {
        populateAccountList(m_currentFilter);
        m_searchBox->setFocus();
    }

    // Update expand button
    m_expandButton->setText(expanded ? "▲" : "▼");

    emit expandedChanged(expanded);
}

void AccountSwitcherWidget::toggleExpanded()
{
    setExpanded(!m_expanded);
}

void AccountSwitcherWidget::refresh()
{
    updateActiveAccountDisplay();
    if (m_expanded) {
        populateAccountList(m_currentFilter);
    }
}

void AccountSwitcherWidget::focusSearch()
{
    if (!m_expanded) {
        setExpanded(true);
    }
    m_searchBox->setFocus();
    m_searchBox->selectAll();
}

void AccountSwitcherWidget::onAccountSwitched(const QString& accountId)
{
    Q_UNUSED(accountId)
    refresh();
    setExpanded(false);
}

void AccountSwitcherWidget::onAccountAdded(const MegaAccount& account)
{
    Q_UNUSED(account)
    refresh();
    if (m_expanded) {
        populateAccountList(m_currentFilter);
    }
}

void AccountSwitcherWidget::onAccountRemoved(const QString& accountId)
{
    Q_UNUSED(accountId)
    refresh();
}

void AccountSwitcherWidget::onAccountUpdated(const MegaAccount& account)
{
    Q_UNUSED(account)
    refresh();
}

void AccountSwitcherWidget::onStorageInfoUpdated(const QString& accountId)
{
    if (accountId == AccountManager::instance().activeAccountId()) {
        updateActiveAccountDisplay();
    }
    if (m_expanded) {
        populateAccountList(m_currentFilter);
    }
}

void AccountSwitcherWidget::onSearchTextChanged(const QString& text)
{
    m_currentFilter = text;
    populateAccountList(text);
}

void AccountSwitcherWidget::onAccountItemClicked(QListWidgetItem* item)
{
    if (!item) return;

    QString accountId = item->data(Qt::UserRole).toString();
    if (accountId.isEmpty()) return;

    if (accountId == AccountManager::instance().activeAccountId()) {
        setExpanded(false);
        return;
    }

    emit accountSwitchRequested(accountId);
}

void AccountSwitcherWidget::onExpandButtonClicked()
{
    toggleExpanded();
}

bool AccountSwitcherWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_headerFrame && event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            toggleExpanded();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void AccountSwitcherWidget::populateAccountList(const QString& filter)
{
    m_accountList->clear();

    AccountManager& mgr = AccountManager::instance();
    QList<MegaAccount> accounts = filter.isEmpty()
        ? mgr.allAccounts()
        : mgr.search(filter);

    QString activeId = mgr.activeAccountId();

    for (const MegaAccount& account : accounts) {
        bool isActive = (account.id == activeId);
        QListWidgetItem* item = new QListWidgetItem(m_accountList);
        item->setData(Qt::UserRole, account.id);

        QWidget* itemWidget = createAccountListItem(account, isActive);
        item->setSizeHint(itemWidget->sizeHint());
        m_accountList->setItemWidget(item, itemWidget);
    }

    if (accounts.isEmpty()) {
        QListWidgetItem* item = new QListWidgetItem(m_accountList);
        item->setFlags(Qt::NoItemFlags);
        QLabel* emptyLabel = new QLabel("No accounts found");
        emptyLabel->setStyleSheet(QString("color: %1; padding: 16px;")
            .arg(ThemeManager::instance().textSecondary().name()));
        emptyLabel->setAlignment(Qt::AlignCenter);
        item->setSizeHint(emptyLabel->sizeHint());
        m_accountList->setItemWidget(item, emptyLabel);
    }
}

void AccountSwitcherWidget::updateActiveAccountDisplay()
{
    AccountManager& mgr = AccountManager::instance();
    const MegaAccount* account = mgr.activeAccount();

    if (!account || account->id.isEmpty()) {
        m_avatarLabel->setText("?");
        m_avatarLabel->setStyleSheet(QString(
            "background-color: %1; color: white; font-weight: bold; "
            "font-size: 14px; border-radius: 18px;")
            .arg(ThemeManager::instance().textDisabled().name()));
        m_emailLabel->setText("No account");
        m_nameLabel->setText("Click to add an account");
        return;
    }

    // Avatar with initials
    QString initials = getInitials(account->email, account->displayName);
    m_avatarLabel->setText(initials);

    QColor color = getAccountColor(*account);
    m_avatarLabel->setStyleSheet(QString(
        "background-color: %1; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 18px;").arg(color.name()));

    // Email
    m_emailLabel->setText(account->email);
    m_emailLabel->setToolTip(account->email);

    // Display name or account count
    int totalAccounts = mgr.accountCount();
    if (!account->displayName.isEmpty()) {
        m_nameLabel->setText(account->displayName);
    } else if (totalAccounts > 1) {
        m_nameLabel->setText(QString("%1 accounts").arg(totalAccounts));
    } else {
        m_nameLabel->setText("Active account");
    }

    // Update storage bar with warning colors
    auto& tm = ThemeManager::instance();
    if (account->storageTotal > 0) {
        int percent = account->storagePercentage();
        m_storageBar->setValue(percent);
        m_storageBar->setVisible(true);

        // Color based on usage threshold - use theme-aware status colors
        QString barColor;
        QString tooltipPrefix;
        if (percent >= 95) {
            barColor = tm.supportError().name();  // Red - critical
            tooltipPrefix = "Storage critical";
        } else if (percent >= 80) {
            barColor = tm.supportWarning().name();  // Orange - warning
            tooltipPrefix = "Storage warning";
        } else {
            barColor = tm.supportSuccess().name();  // Green - normal
            tooltipPrefix = "Storage";
        }

        m_storageBar->setStyleSheet(QString(
            "QProgressBar { background-color: %1; border: none; border-radius: 2px; }"
            "QProgressBar::chunk { background-color: %2; border-radius: 2px; }"
        ).arg(tm.borderSubtle().name()).arg(barColor));

        m_storageBar->setToolTip(QString("%1: %2% used (%3)")
            .arg(tooltipPrefix)
            .arg(percent)
            .arg(account->storageDisplayText()));
    } else {
        m_storageBar->setVisible(false);
    }
}

void AccountSwitcherWidget::animateDropdown(bool show)
{
    if (show) {
        // Make dropdown a popup that floats above the layout
        m_dropdownFrame->setParent(nullptr);
        m_dropdownFrame->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);

        // Calculate dropdown height dynamically based on content
        int numAccounts = AccountManager::instance().allAccounts().size();
        int itemHeight = 48;       // Each account item is 48px
        int searchBoxHeight = 40;  // Search box height
        int buttonsHeight = 50;    // Add/Manage buttons
        int margins = 20;          // Padding/margins
        int contentHeight = searchBoxHeight + (numAccounts * itemHeight) + buttonsHeight + margins;
        int targetHeight = qMin(contentHeight, 350);  // Cap at 350px max

        // Position below the header frame - use widget's coordinates for proper alignment
        QPoint globalPos = this->mapToGlobal(QPoint(0, m_headerFrame->height()));
        m_dropdownFrame->setGeometry(globalPos.x(), globalPos.y(), this->width(), targetHeight);
        m_dropdownFrame->show();
        m_dropdownFrame->raise();
    } else {
        m_dropdownFrame->hide();
    }
}

QWidget* AccountSwitcherWidget::createAccountListItem(const MegaAccount& account, bool isActive)
{
    AccountListItemWidget* item = new AccountListItemWidget(account, isActive, nullptr);

    // Connect clicked signal to handle account switching
    connect(item, &AccountListItemWidget::clicked, this, [this, accountId = account.id]() {
        if (accountId == AccountManager::instance().activeAccountId()) {
            setExpanded(false);
            return;
        }
        emit accountSwitchRequested(accountId);
    });

    connect(item, &AccountListItemWidget::quickPeekClicked,
            this, &AccountSwitcherWidget::quickPeekRequested);

    return item;
}

QWidget* AccountSwitcherWidget::createGroupHeader(const QString& groupName, const QColor& color, int accountCount)
{
    QFrame* header = new QFrame();
    header->setObjectName("AccountGroupHeader");

    QHBoxLayout* layout = new QHBoxLayout(header);
    layout->setContentsMargins(8, 4, 8, 4);

    QLabel* colorDot = new QLabel(header);
    colorDot->setFixedSize(8, 8);
    colorDot->setStyleSheet(QString("background-color: %1; border-radius: 4px;").arg(color.name()));
    layout->addWidget(colorDot);

    auto& tm = ThemeManager::instance();
    QLabel* nameLabel = new QLabel(groupName.toUpper(), header);
    nameLabel->setStyleSheet(QString("color: %1; font-size: 10px; font-weight: 600;")
        .arg(tm.textSecondary().name()));
    layout->addWidget(nameLabel);

    QLabel* countLabel = new QLabel(QString("(%1)").arg(accountCount), header);
    countLabel->setStyleSheet(QString("color: %1; font-size: 10px;")
        .arg(tm.textDisabled().name()));
    layout->addWidget(countLabel);

    layout->addStretch();

    return header;
}

QString AccountSwitcherWidget::formatBytes(qint64 bytes) const
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

QString AccountSwitcherWidget::getInitials(const QString& email, const QString& displayName) const
{
    if (!displayName.isEmpty()) {
        QStringList parts = displayName.split(' ', Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            return QString("%1%2").arg(parts[0][0].toUpper()).arg(parts[1][0].toUpper());
        } else if (!parts.isEmpty()) {
            return parts[0].left(2).toUpper();
        }
    }

    if (!email.isEmpty()) {
        int atPos = email.indexOf('@');
        QString localPart = (atPos > 0) ? email.left(atPos) : email;
        return localPart.left(2).toUpper();
    }

    return "??";
}

QColor AccountSwitcherWidget::getAccountColor(const MegaAccount& account) const
{
    if (account.color.isValid()) {
        return account.color;
    }

    if (!account.groupId.isEmpty()) {
        AccountGroup group = AccountManager::instance().getGroup(account.groupId);
        if (group.color.isValid()) {
            return group.color;
        }
    }

    return ThemeManager::instance().brandDefault();
}

// ============================================================================
// AccountListItemWidget
// ============================================================================

// Helper to get account status
static AccountStatus getAccountStatus(const MegaAccount& account, bool isActive)
{
    AccountManager& mgr = AccountManager::instance();

    // Check if this account is syncing (active transfers)
    if (mgr.isAccountSyncing(account.id)) {
        return AccountStatus::Syncing;
    }

    // Check if this is the active account
    if (isActive) {
        return AccountStatus::Active;
    }

    // Check if logged in
    if (mgr.isLoggedIn(account.id)) {
        return AccountStatus::Ready;
    }

    // Check for expired session (lastLogin was valid but no longer logged in)
    if (account.lastLogin.isValid()) {
        return AccountStatus::Expired;
    }

    return AccountStatus::Offline;
}

// Helper to create status badge widget
static QLabel* createStatusBadge(AccountStatus status, QWidget* parent)
{
    QLabel* badge = new QLabel(parent);
    badge->setFixedSize(12, 12);
    badge->setAlignment(Qt::AlignCenter);

    auto& tm = ThemeManager::instance();
    QString symbol;
    QString color;
    QString tooltip;

    switch (status) {
        case AccountStatus::Active:
            symbol = QString::fromUtf8("\u25CF");  // Filled circle
            color = tm.supportSuccess().name();  // Green
            tooltip = "Active account";
            break;
        case AccountStatus::Ready:
            symbol = QString::fromUtf8("\u25CB");  // Empty circle
            color = tm.textDisabled().name();  // Gray
            tooltip = "Ready";
            break;
        case AccountStatus::Syncing:
            symbol = QString::fromUtf8("\u21BB");  // Rotating arrows
            color = tm.supportInfo().name();  // Blue
            tooltip = "Syncing...";
            break;
        case AccountStatus::Expired:
            symbol = QString::fromUtf8("\u26A0");  // Warning
            color = tm.supportWarning().name();  // Orange
            tooltip = "Session expired - click to re-login";
            break;
        case AccountStatus::Offline:
            symbol = QString::fromUtf8("\u2715");  // X
            color = tm.supportError().name();  // Red
            tooltip = "Offline - click to login";
            break;
        default:
            symbol = "?";
            color = tm.textDisabled().name();
            tooltip = "Unknown status";
            break;
    }

    badge->setText(symbol);
    badge->setToolTip(tooltip);
    badge->setStyleSheet(QString(
        "QLabel { color: %1; font-size: 12px; font-weight: bold; }"
    ).arg(color));

    return badge;
}

AccountListItemWidget::AccountListItemWidget(const MegaAccount& account, bool isActive, QWidget* parent)
    : QFrame(parent)
    , m_accountId(account.id)
    , m_peekButton(nullptr)
{
    setupUI(account, isActive);
}

void AccountListItemWidget::setupUI(const MegaAccount& account, bool isActive)
{
    setObjectName("AccountListItem");
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(48);

    if (isActive) {
        setProperty("active", true);
        setStyleSheet("background-color: rgba(221, 20, 5, 0.08); border-radius: 6px;");
    }

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(10);

    // Mini avatar
    QLabel* avatar = new QLabel(this);
    avatar->setFixedSize(32, 32);
    avatar->setAlignment(Qt::AlignCenter);

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
    avatar->setText(initials);

    QColor color = account.color.isValid() ? account.color : ThemeManager::instance().brandDefault();
    avatar->setStyleSheet(QString(
        "background-color: %1; color: white; font-weight: bold; "
        "font-size: 11px; border-radius: 16px;").arg(color.name()));
    layout->addWidget(avatar);

    // Status badge
    AccountStatus status = getAccountStatus(account, isActive);
    QLabel* statusBadge = createStatusBadge(status, this);
    layout->addWidget(statusBadge);

    // Email and storage column
    QVBoxLayout* infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(2);
    infoLayout->setContentsMargins(0, 0, 0, 0);

    auto& tm = ThemeManager::instance();
    QLabel* emailLabel = new QLabel(account.email, this);
    emailLabel->setObjectName("ListItemEmail");
    if (isActive) {
        emailLabel->setStyleSheet(QString("color: %1; font-weight: 600;")
            .arg(tm.textPrimary().name()));
    } else {
        emailLabel->setStyleSheet(QString("color: %1;")
            .arg(tm.textSecondary().name()));
    }
    infoLayout->addWidget(emailLabel);

    // Storage bar with warning colors - use theme-aware status colors
    if (account.storageTotal > 0) {
        QProgressBar* storageBar = new QProgressBar(this);
        storageBar->setObjectName("ListItemStorageBar");
        storageBar->setFixedHeight(4);
        storageBar->setTextVisible(false);
        storageBar->setMinimum(0);
        storageBar->setMaximum(100);

        int percent = account.storagePercentage();
        storageBar->setValue(percent);

        // Color based on usage threshold
        QString barColor;
        QString tooltipPrefix;
        if (percent >= 95) {
            barColor = tm.supportError().name();  // Red - critical
            tooltipPrefix = "Storage critical";
        } else if (percent >= 80) {
            barColor = tm.supportWarning().name();  // Orange - warning
            tooltipPrefix = "Storage warning";
        } else {
            barColor = tm.supportSuccess().name();  // Green - normal
            tooltipPrefix = "Storage";
        }

        storageBar->setStyleSheet(QString(
            "QProgressBar { background-color: %1; border: none; border-radius: 2px; }"
            "QProgressBar::chunk { background-color: %2; border-radius: 2px; }"
        ).arg(tm.borderSubtle().name()).arg(barColor));

        storageBar->setToolTip(QString("%1: %2% used (%3 / %4)")
            .arg(tooltipPrefix)
            .arg(percent)
            .arg(account.storageDisplayText().split('/').value(0).trimmed())
            .arg(account.storageDisplayText().split('/').value(1).trimmed()));

        infoLayout->addWidget(storageBar);
    }

    layout->addLayout(infoLayout, 1);

    // Active checkmark or peek button
    if (isActive) {
        QLabel* checkLabel = new QLabel(this);
        checkLabel->setPixmap(QIcon(":/icons/check.svg").pixmap(DpiScaler::scale(16), DpiScaler::scale(16)));
        checkLabel->setStyleSheet("background: transparent;");
        layout->addWidget(checkLabel);
    } else {
        m_peekButton = new QPushButton(this);
        m_peekButton->setObjectName("PeekButton");
        m_peekButton->setIcon(QIcon(":/icons/eye.svg"));
        m_peekButton->setIconSize(QSize(DpiScaler::scale(18), DpiScaler::scale(18)));
        m_peekButton->setToolTip("Quick peek");
        m_peekButton->setFixedSize(28, 28);
        m_peekButton->setFlat(true);
        m_peekButton->setCursor(Qt::PointingHandCursor);
        m_peekButton->setVisible(false);
        m_peekButton->setStyleSheet(QString("QPushButton { background: transparent; border: none; } "
                                    "QPushButton:hover { background: %1; border-radius: 4px; }")
                                    .arg(tm.borderSubtle().name()));
        connect(m_peekButton, &QPushButton::clicked, [this]() {
            emit quickPeekClicked(m_accountId);
        });
        layout->addWidget(m_peekButton);
    }
}

void AccountListItemWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked();
    }
    QFrame::mousePressEvent(event);
}

void AccountListItemWidget::enterEvent(QEnterEvent* event)
{
    if (m_peekButton) {
        m_peekButton->setVisible(true);
    }
    if (!property("active").toBool()) {
        setStyleSheet(QString("background-color: %1; border-radius: 6px;")
            .arg(ThemeManager::instance().surface2().name()));
    }
    QFrame::enterEvent(event);
}

void AccountListItemWidget::leaveEvent(QEvent* event)
{
    if (m_peekButton) {
        m_peekButton->setVisible(false);
    }
    if (!property("active").toBool()) {
        setStyleSheet("");
    }
    QFrame::leaveEvent(event);
}

} // namespace MegaCustom
