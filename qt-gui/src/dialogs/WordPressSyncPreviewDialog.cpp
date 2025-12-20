#include "WordPressSyncPreviewDialog.h"
#include "integrations/WordPressSync.h"
#include "utils/MemberRegistry.h"
#include "widgets/ButtonFactory.h"
#include "styles/ThemeManager.h"
#include "utils/DpiScaler.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QDateTime>
#include <QDebug>

namespace MegaCustom {

// ============================================================================
// WpFetchWorker
// ============================================================================

WpFetchWorker::WpFetchWorker(QObject* parent)
    : QObject(parent)
{
}

void WpFetchWorker::process()
{
    m_cancelled = false;
    QList<WpUserPreview> users;
    QString error;

    WordPressSync sync;
    WordPressConfig config;
    config.siteUrl = m_siteUrl.toStdString();
    config.username = m_username.toStdString();
    config.applicationPassword = m_password.toStdString();
    config.perPage = m_perPage;
    if (!m_role.isEmpty()) {
        config.roleFilter = m_role.toStdString();
    }
    sync.setConfig(config);

    // Set progress callback
    sync.setProgressCallback([this](const WpSyncProgress& progress) {
        if (!m_cancelled) {
            emit this->progress(progress.currentUser, progress.totalUsers);
        }
    });

    // Fetch users
    std::string fetchError;
    auto wpUsers = sync.fetchAllUsers(fetchError);

    if (m_cancelled) {
        emit finished(users, "Cancelled");
        return;
    }

    if (!fetchError.empty()) {
        emit finished(users, QString::fromStdString(fetchError));
        return;
    }

    // Convert to preview format
    for (const auto& u : wpUsers) {
        WpUserPreview preview;
        preview.wpUserId = u.id;
        preview.username = QString::fromStdString(u.username);
        preview.displayName = QString::fromStdString(u.displayName);
        preview.email = QString::fromStdString(u.email);
        preview.role = QString::fromStdString(u.role);

        // Parse registration date (format: 2024-01-15T10:30:00)
        QString dateStr = QString::fromStdString(u.registeredDate);
        if (dateStr.contains('T')) {
            dateStr = dateStr.left(dateStr.indexOf('T'));
        }
        preview.registeredDate = QDate::fromString(dateStr, "yyyy-MM-dd");
        preview.selected = true;

        users.append(preview);
    }

    emit finished(users, "");
}

void WpFetchWorker::cancel()
{
    m_cancelled = true;
}

// ============================================================================
// WpSyncSelectedWorker
// ============================================================================

WpSyncSelectedWorker::WpSyncSelectedWorker(QObject* parent)
    : QObject(parent)
{
}

void WpSyncSelectedWorker::process()
{
    m_cancelled = false;
    int created = 0, updated = 0, failed = 0;

    MemberRegistry* registry = MemberRegistry::instance();
    if (!registry) {
        emit finished(0, 0, m_users.size(), "Member registry not available");
        return;
    }

    int total = m_users.size();
    for (int i = 0; i < m_users.size(); ++i) {
        if (m_cancelled) break;

        const WpUserPreview& user = m_users[i];
        emit progress(i + 1, total, user.username);

        // Check if member exists by email or username
        QString memberId;
        for (const MemberInfo& member : registry->getAllMembers()) {
            if (!user.email.isEmpty() && member.email == user.email) {
                memberId = member.id;
                break;
            }
            if (member.id == user.username) {
                memberId = member.id;
                break;
            }
        }

        if (memberId.isEmpty()) {
            // Create new member
            MemberInfo newMember;
            newMember.id = user.username.isEmpty() ? QString("wp_%1").arg(user.wpUserId) : user.username;
            newMember.displayName = user.displayName;
            newMember.email = user.email;
            newMember.wpUserId = QString::number(user.wpUserId);
            newMember.active = true;
            newMember.createdAt = QDateTime::currentSecsSinceEpoch();
            newMember.updatedAt = newMember.createdAt;

            registry->addMember(newMember);
            created++;
        } else {
            // Update existing member
            MemberInfo existingMember = registry->getMember(memberId);
            existingMember.displayName = user.displayName;
            existingMember.email = user.email;
            existingMember.wpUserId = QString::number(user.wpUserId);
            existingMember.updatedAt = QDateTime::currentSecsSinceEpoch();

            registry->updateMember(existingMember);
            updated++;
        }
    }

    // Save changes
    registry->save();

    emit finished(created, updated, failed, "");
}

void WpSyncSelectedWorker::cancel()
{
    m_cancelled = true;
}

// ============================================================================
// WordPressSyncPreviewDialog
// ============================================================================

WordPressSyncPreviewDialog::WordPressSyncPreviewDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("WordPress User Preview");
    setMinimumSize(DpiScaler::scale(700), DpiScaler::scale(500));
    resize(DpiScaler::scale(800), DpiScaler::scale(600));

    setupUI();
}

WordPressSyncPreviewDialog::~WordPressSyncPreviewDialog()
{
    cleanupWorker();
}

void WordPressSyncPreviewDialog::setCredentials(const QString& siteUrl, const QString& username, const QString& password)
{
    m_siteUrl = siteUrl;
    m_username = username;
    m_password = password;
}

void WordPressSyncPreviewDialog::setRole(const QString& role)
{
    m_initialRole = role;
}

void WordPressSyncPreviewDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(DpiScaler::scale(10));

    // ========================================
    // Filter Section
    // ========================================
    QGroupBox* filterGroup = new QGroupBox("Filters", this);
    QVBoxLayout* filterLayout = new QVBoxLayout(filterGroup);

    // Date filters row
    QHBoxLayout* dateLayout = new QHBoxLayout();

    m_fromDateCheck = new QCheckBox("From:", this);
    m_fromDate = new QDateEdit(this);
    m_fromDate->setCalendarPopup(true);
    m_fromDate->setDate(QDate::currentDate().addYears(-1));
    m_fromDate->setEnabled(false);
    connect(m_fromDateCheck, &QCheckBox::toggled, m_fromDate, &QDateEdit::setEnabled);
    connect(m_fromDateCheck, &QCheckBox::toggled, this, &WordPressSyncPreviewDialog::onDateFilterChanged);
    connect(m_fromDate, &QDateEdit::dateChanged, this, &WordPressSyncPreviewDialog::onDateFilterChanged);
    dateLayout->addWidget(m_fromDateCheck);
    dateLayout->addWidget(m_fromDate);

    dateLayout->addSpacing(DpiScaler::scale(20));

    m_toDateCheck = new QCheckBox("To:", this);
    m_toDate = new QDateEdit(this);
    m_toDate->setCalendarPopup(true);
    m_toDate->setDate(QDate::currentDate());
    m_toDate->setEnabled(false);
    connect(m_toDateCheck, &QCheckBox::toggled, m_toDate, &QDateEdit::setEnabled);
    connect(m_toDateCheck, &QCheckBox::toggled, this, &WordPressSyncPreviewDialog::onDateFilterChanged);
    connect(m_toDate, &QDateEdit::dateChanged, this, &WordPressSyncPreviewDialog::onDateFilterChanged);
    dateLayout->addWidget(m_toDateCheck);
    dateLayout->addWidget(m_toDate);

    dateLayout->addSpacing(DpiScaler::scale(20));

    dateLayout->addWidget(new QLabel("Role:", this));
    m_roleFilter = new QComboBox(this);
    m_roleFilter->addItem("All Roles", "");
    m_roleFilter->addItem("Administrator", "administrator");
    m_roleFilter->addItem("Editor", "editor");
    m_roleFilter->addItem("Author", "author");
    m_roleFilter->addItem("Contributor", "contributor");
    m_roleFilter->addItem("Subscriber", "subscriber");
    m_roleFilter->addItem("Customer", "customer");
    connect(m_roleFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &WordPressSyncPreviewDialog::onRoleFilterChanged);
    dateLayout->addWidget(m_roleFilter);

    dateLayout->addStretch();
    filterLayout->addLayout(dateLayout);

    // Search row
    QHBoxLayout* searchLayout = new QHBoxLayout();
    searchLayout->addWidget(new QLabel("Search:", this));
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Type to filter by username, email, or name...");
    m_searchEdit->setClearButtonEnabled(true);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &WordPressSyncPreviewDialog::onSearchChanged);
    searchLayout->addWidget(m_searchEdit);
    filterLayout->addLayout(searchLayout);

    mainLayout->addWidget(filterGroup);

    // ========================================
    // User Table Section
    // ========================================
    QHBoxLayout* tableHeaderLayout = new QHBoxLayout();
    m_selectAllCheck = new QCheckBox("Select All", this);
    m_selectAllCheck->setChecked(true);
    connect(m_selectAllCheck, &QCheckBox::stateChanged, this, &WordPressSyncPreviewDialog::onSelectAllChanged);
    tableHeaderLayout->addWidget(m_selectAllCheck);

    tableHeaderLayout->addStretch();

    m_statsLabel = new QLabel("Loading...", this);
    tableHeaderLayout->addWidget(m_statsLabel);
    mainLayout->addLayout(tableHeaderLayout);

    m_userTable = new QTableWidget(this);
    m_userTable->setColumnCount(5);
    m_userTable->setHorizontalHeaderLabels({"Sync", "Username", "Email", "Role", "Registered"});
    m_userTable->horizontalHeader()->setStretchLastSection(true);
    m_userTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_userTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_userTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_userTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    m_userTable->setColumnWidth(0, DpiScaler::scale(50));
    m_userTable->setColumnWidth(1, DpiScaler::scale(120));
    m_userTable->setColumnWidth(3, DpiScaler::scale(100));
    m_userTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_userTable->setAlternatingRowColors(true);
    mainLayout->addWidget(m_userTable, 1);

    // ========================================
    // Progress Section
    // ========================================
    m_progressBar = new QProgressBar(this);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, 0);  // Indeterminate initially
    mainLayout->addWidget(m_progressBar);

    m_statusLabel = new QLabel("Fetching users from WordPress...", this);
    mainLayout->addWidget(m_statusLabel);

    // ========================================
    // Button Section
    // ========================================
    QHBoxLayout* buttonLayout = new QHBoxLayout();

    // Use success-styled primary button for sync
    auto& tm = ThemeManager::instance();
    m_syncBtn = ButtonFactory::createPrimary("Sync Selected", this);
    m_syncBtn->setIcon(QIcon(":/icons/download.svg"));
    m_syncBtn->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: white; border: none; border-radius: 6px; padding: 8px 16px; font-weight: 600; }"
        "QPushButton:hover { background-color: %2; }"
        "QPushButton:pressed { background-color: %3; }"
        "QPushButton:disabled { background-color: %4; color: %5; }")
        .arg(tm.supportSuccess().name())
        .arg(tm.supportSuccess().darker(110).name())
        .arg(tm.supportSuccess().darker(120).name())
        .arg(tm.buttonDisabled().name())
        .arg(tm.textDisabled().name()));
    m_syncBtn->setEnabled(false);
    connect(m_syncBtn, &QPushButton::clicked, this, &WordPressSyncPreviewDialog::onSyncSelected);
    buttonLayout->addWidget(m_syncBtn);

    m_cancelBtn = ButtonFactory::createOutline("Cancel", this);
    m_cancelBtn->setIcon(QIcon(":/icons/x.svg"));
    connect(m_cancelBtn, &QPushButton::clicked, this, &WordPressSyncPreviewDialog::onCancel);
    buttonLayout->addWidget(m_cancelBtn);

    buttonLayout->addStretch();

    m_closeBtn = ButtonFactory::createOutline("Close", this);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(m_closeBtn);

    mainLayout->addLayout(buttonLayout);
}

void WordPressSyncPreviewDialog::startFetch()
{
    cleanupWorker();

    m_isFetching = true;
    m_allUsers.clear();
    m_userTable->setRowCount(0);
    m_syncBtn->setEnabled(false);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, 0);
    m_statusLabel->setText("Fetching users from WordPress...");

    m_workerThread = new QThread(this);
    m_fetchWorker = new WpFetchWorker();
    m_fetchWorker->moveToThread(m_workerThread);

    m_fetchWorker->setSiteUrl(m_siteUrl);
    m_fetchWorker->setUsername(m_username);
    m_fetchWorker->setPassword(m_password);
    m_fetchWorker->setRole(m_initialRole);

    connect(m_workerThread, &QThread::started, m_fetchWorker, &WpFetchWorker::process);
    connect(m_fetchWorker, &WpFetchWorker::progress, this, &WordPressSyncPreviewDialog::onFetchProgress);
    connect(m_fetchWorker, &WpFetchWorker::finished, this, &WordPressSyncPreviewDialog::onFetchFinished);
    connect(m_fetchWorker, &WpFetchWorker::finished, m_workerThread, &QThread::quit);
    connect(m_workerThread, &QThread::finished, m_fetchWorker, &QObject::deleteLater);
    connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater);

    m_workerThread->start();
}

void WordPressSyncPreviewDialog::onFetchProgress(int current, int total)
{
    if (total > 0) {
        m_progressBar->setRange(0, total);
        m_progressBar->setValue(current);
    }
    m_statusLabel->setText(QString("Fetching users... %1/%2").arg(current).arg(total));
}

void WordPressSyncPreviewDialog::onFetchFinished(const QList<WpUserPreview>& users, const QString& error)
{
    m_isFetching = false;
    m_workerThread = nullptr;
    m_fetchWorker = nullptr;
    m_progressBar->setVisible(false);

    auto& tm = ThemeManager::instance();
    if (!error.isEmpty()) {
        m_statusLabel->setText("Error: " + error);
        m_statusLabel->setStyleSheet(QString("color: %1;").arg(tm.supportError().name()));
        return;
    }

    m_allUsers = users;
    m_statusLabel->setText(QString("Found %1 users").arg(users.size()));
    m_statusLabel->setStyleSheet(QString("color: %1;").arg(tm.supportSuccess().name()));

    populateTable();
    m_syncBtn->setEnabled(true);
}

void WordPressSyncPreviewDialog::populateTable()
{
    applyFilters();
}

void WordPressSyncPreviewDialog::applyFilters()
{
    m_visibleIndices.clear();

    for (int i = 0; i < m_allUsers.size(); ++i) {
        if (matchesFilters(m_allUsers[i])) {
            m_visibleIndices.append(i);
        }
    }

    // Populate table with filtered results
    m_userTable->setRowCount(m_visibleIndices.size());

    for (int row = 0; row < m_visibleIndices.size(); ++row) {
        int idx = m_visibleIndices[row];
        const WpUserPreview& user = m_allUsers[idx];

        // Checkbox
        QTableWidgetItem* checkItem = new QTableWidgetItem();
        checkItem->setCheckState(user.selected ? Qt::Checked : Qt::Unchecked);
        checkItem->setFlags(checkItem->flags() | Qt::ItemIsUserCheckable);
        checkItem->setData(Qt::UserRole, idx);  // Store original index
        m_userTable->setItem(row, 0, checkItem);

        // Username
        m_userTable->setItem(row, 1, new QTableWidgetItem(user.username));

        // Email
        m_userTable->setItem(row, 2, new QTableWidgetItem(user.email));

        // Role
        m_userTable->setItem(row, 3, new QTableWidgetItem(user.role));

        // Registered date
        m_userTable->setItem(row, 4, new QTableWidgetItem(user.registeredDate.toString("yyyy-MM-dd")));
    }

    updateStats();
}

bool WordPressSyncPreviewDialog::matchesFilters(const WpUserPreview& user) const
{
    // Date filter - from
    if (m_fromDateCheck->isChecked()) {
        if (user.registeredDate.isValid() && user.registeredDate < m_fromDate->date()) {
            return false;
        }
    }

    // Date filter - to
    if (m_toDateCheck->isChecked()) {
        if (user.registeredDate.isValid() && user.registeredDate > m_toDate->date()) {
            return false;
        }
    }

    // Role filter
    QString roleFilter = m_roleFilter->currentData().toString();
    if (!roleFilter.isEmpty()) {
        if (user.role.toLower() != roleFilter.toLower()) {
            return false;
        }
    }

    // Search filter
    QString search = m_searchEdit->text().trimmed().toLower();
    if (!search.isEmpty()) {
        if (!user.username.toLower().contains(search) &&
            !user.email.toLower().contains(search) &&
            !user.displayName.toLower().contains(search)) {
            return false;
        }
    }

    return true;
}

void WordPressSyncPreviewDialog::updateStats()
{
    int selectedCount = 0;
    for (int row = 0; row < m_userTable->rowCount(); ++row) {
        QTableWidgetItem* item = m_userTable->item(row, 0);
        if (item && item->checkState() == Qt::Checked) {
            selectedCount++;
        }
    }

    m_statsLabel->setText(QString("Showing: %1/%2 users | Selected: %3")
        .arg(m_visibleIndices.size())
        .arg(m_allUsers.size())
        .arg(selectedCount));

    m_syncBtn->setText(QString("Sync Selected (%1)").arg(selectedCount));
    m_syncBtn->setEnabled(selectedCount > 0 && !m_isSyncing);
}

QList<WpUserPreview> WordPressSyncPreviewDialog::getSelectedUsers() const
{
    QList<WpUserPreview> selected;

    for (int row = 0; row < m_userTable->rowCount(); ++row) {
        QTableWidgetItem* item = m_userTable->item(row, 0);
        if (item && item->checkState() == Qt::Checked) {
            int idx = item->data(Qt::UserRole).toInt();
            if (idx >= 0 && idx < m_allUsers.size()) {
                selected.append(m_allUsers[idx]);
            }
        }
    }

    return selected;
}

void WordPressSyncPreviewDialog::onSearchChanged(const QString& text)
{
    Q_UNUSED(text);
    applyFilters();
}

void WordPressSyncPreviewDialog::onDateFilterChanged()
{
    applyFilters();
}

void WordPressSyncPreviewDialog::onRoleFilterChanged(int index)
{
    Q_UNUSED(index);
    applyFilters();
}

void WordPressSyncPreviewDialog::onSelectAllChanged(int state)
{
    bool checked = (state == Qt::Checked);
    for (int row = 0; row < m_userTable->rowCount(); ++row) {
        QTableWidgetItem* item = m_userTable->item(row, 0);
        if (item) {
            item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        }
    }
    updateStats();
}

void WordPressSyncPreviewDialog::onSyncSelected()
{
    QList<WpUserPreview> selected = getSelectedUsers();
    if (selected.isEmpty()) {
        QMessageBox::warning(this, "No Selection", "Please select at least one user to sync.");
        return;
    }

    int result = QMessageBox::question(this, "Confirm Sync",
        QString("Sync %1 selected users to Member Registry?\n\n"
                "New members will be created and existing ones updated.")
            .arg(selected.size()),
        QMessageBox::Yes | QMessageBox::No);

    if (result != QMessageBox::Yes) return;

    cleanupWorker();

    m_isSyncing = true;
    m_syncBtn->setEnabled(false);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, selected.size());
    m_progressBar->setValue(0);
    m_statusLabel->setText("Syncing selected users...");

    m_workerThread = new QThread(this);
    m_syncWorker = new WpSyncSelectedWorker();
    m_syncWorker->moveToThread(m_workerThread);

    m_syncWorker->setSiteUrl(m_siteUrl);
    m_syncWorker->setUsername(m_username);
    m_syncWorker->setPassword(m_password);
    m_syncWorker->setUsers(selected);

    connect(m_workerThread, &QThread::started, m_syncWorker, &WpSyncSelectedWorker::process);
    connect(m_syncWorker, &WpSyncSelectedWorker::progress, this, &WordPressSyncPreviewDialog::onSyncProgress);
    connect(m_syncWorker, &WpSyncSelectedWorker::finished, this, &WordPressSyncPreviewDialog::onSyncFinished);
    connect(m_syncWorker, &WpSyncSelectedWorker::finished, m_workerThread, &QThread::quit);
    connect(m_workerThread, &QThread::finished, m_syncWorker, &QObject::deleteLater);
    connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater);

    m_workerThread->start();
}

void WordPressSyncPreviewDialog::onSyncProgress(int current, int total, const QString& username)
{
    m_progressBar->setValue(current);
    m_statusLabel->setText(QString("Syncing %1/%2: %3").arg(current).arg(total).arg(username));
}

void WordPressSyncPreviewDialog::onSyncFinished(int created, int updated, int failed, const QString& error)
{
    m_isSyncing = false;
    m_workerThread = nullptr;
    m_syncWorker = nullptr;
    m_progressBar->setVisible(false);

    auto& tm = ThemeManager::instance();
    if (!error.isEmpty()) {
        m_statusLabel->setText("Error: " + error);
        m_statusLabel->setStyleSheet(QString("color: %1;").arg(tm.supportError().name()));
        QMessageBox::warning(this, "Sync Error", error);
        m_syncBtn->setEnabled(true);
        return;
    }

    QString msg = QString("Sync completed!\nCreated: %1\nUpdated: %2\nFailed: %3")
        .arg(created).arg(updated).arg(failed);

    m_statusLabel->setText(QString("Sync complete: %1 created, %2 updated").arg(created).arg(updated));
    m_statusLabel->setStyleSheet(QString("color: %1;").arg(tm.supportSuccess().name()));

    QMessageBox::information(this, "Sync Complete", msg);

    emit syncCompleted(created, updated);
    updateStats();
}

void WordPressSyncPreviewDialog::onCancel()
{
    if (m_fetchWorker) {
        m_fetchWorker->cancel();
    }
    if (m_syncWorker) {
        m_syncWorker->cancel();
    }
    m_statusLabel->setText("Cancelled");
}

void WordPressSyncPreviewDialog::cleanupWorker()
{
    if (m_workerThread) {
        if (m_workerThread->isRunning()) {
            if (m_fetchWorker) m_fetchWorker->cancel();
            if (m_syncWorker) m_syncWorker->cancel();
            m_workerThread->quit();
            m_workerThread->wait(3000);
        }
        m_workerThread = nullptr;
        m_fetchWorker = nullptr;
        m_syncWorker = nullptr;
    }
}

} // namespace MegaCustom
