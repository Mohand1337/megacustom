#include "AccountManager.h"
#include "CredentialStore.h"
#include "SessionPool.h"
#include "../utils/Constants.h"
#include <megaapi.h>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QCoreApplication>
#include <QThread>
#include <QEventLoop>
#include <QTimer>

namespace MegaCustom {

/**
 * @brief Worker class for async login operations
 *
 * Runs MEGA login, fetchNodes, and getAccountDetails in a separate thread
 * to avoid blocking the UI.
 */
class LoginWorker : public QObject {
    Q_OBJECT

public:
    LoginWorker(const QString& email, const QString& password, QObject* parent = nullptr)
        : QObject(parent), m_email(email), m_password(password) {}

signals:
    void progress(int percent, const QString& status);
    void success(const QString& sessionKey, qint64 storageUsed, qint64 storageTotal);
    void failed(const QString& error);

public slots:
    void doLogin() {
        emit progress(0, "Connecting to MEGA...");

        // Create temporary API for login
        mega::MegaApi* api = new mega::MegaApi(
            Constants::MEGA_API_KEY,
            (const char*)nullptr,
            "MegaCustomApp/1.0"
        );

        // === Step 1: Login ===
        emit progress(10, "Authenticating...");

        class LoginListener : public mega::MegaRequestListener {
        public:
            bool finished = false;
            bool success = false;
            QString error;
            QString sessionKey;

            void onRequestFinish(mega::MegaApi* api, mega::MegaRequest*, mega::MegaError* e) override {
                finished = true;
                if (e->getErrorCode() == mega::MegaError::API_OK) {
                    success = true;
                    sessionKey = QString::fromUtf8(api->dumpSession());
                } else {
                    success = false;
                    error = QString::fromUtf8(e->getErrorString());
                }
            }
        };

        LoginListener loginListener;
        api->login(m_email.toUtf8().constData(), m_password.toUtf8().constData(), &loginListener);

        // Wait with event loop (non-blocking for the worker thread)
        if (!waitForListener(loginListener.finished, 120000)) {
            delete api;
            emit failed("Login timeout - server may be slow");
            return;
        }

        if (!loginListener.success) {
            delete api;
            emit failed(loginListener.error);
            return;
        }

        QString sessionKey = loginListener.sessionKey;

        // === Step 2: Fetch Nodes ===
        emit progress(40, "Loading account data...");

        LoginListener fetchListener;
        api->fetchNodes(&fetchListener);

        if (!waitForListener(fetchListener.finished, 180000)) {
            api->localLogout();
            delete api;
            emit failed("Timeout loading account data");
            return;
        }

        if (!fetchListener.success) {
            api->localLogout();
            delete api;
            emit failed("Failed to load account: " + fetchListener.error);
            return;
        }

        emit progress(70, "Getting storage info...");

        // === Step 3: Get Account Details ===
        qint64 storageUsed = 0;
        qint64 storageTotal = 0;

        class AccountListener : public mega::MegaRequestListener {
        public:
            bool finished = false;
            qint64 used = 0;
            qint64 total = 0;

            void onRequestFinish(mega::MegaApi*, mega::MegaRequest* request, mega::MegaError* e) override {
                finished = true;
                if (e->getErrorCode() == mega::MegaError::API_OK) {
                    used = request->getNumber();
                    total = request->getTotalBytes();
                }
            }
        };

        AccountListener accListener;
        api->getAccountDetails(&accListener);

        if (waitForListener(accListener.finished, 10000)) {
            storageUsed = accListener.used;
            storageTotal = accListener.total;
        }
        // Don't fail if account details timeout - not critical

        emit progress(90, "Finalizing...");

        // Cleanup temporary API
        api->localLogout();
        delete api;

        emit progress(100, "Complete");
        emit success(sessionKey, storageUsed, storageTotal);
    }

private:
    bool waitForListener(volatile bool& finished, int timeoutMs) {
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);

        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(timeoutMs);

        // Check every 100ms
        while (!finished && timer.isActive()) {
            loop.processEvents(QEventLoop::AllEvents, 100);
            QThread::msleep(50);
        }

        timer.stop();
        return finished;
    }

    QString m_email;
    QString m_password;
};

// Include the moc file for LoginWorker since it's defined in the cpp
#include "AccountManager.moc"

AccountManager* AccountManager::s_instance = nullptr;

AccountManager& AccountManager::instance()
{
    if (!s_instance) {
        qFatal("AccountManager::instance() called before initialize()");
    }
    return *s_instance;
}

void AccountManager::initialize(QObject* parent)
{
    if (s_instance) {
        qWarning() << "AccountManager already initialized";
        return;
    }
    s_instance = new AccountManager(parent);
    s_instance->loadAccounts();
    qDebug() << "AccountManager initialized";
}

void AccountManager::shutdown()
{
    if (s_instance) {
        s_instance->saveAccounts();
        delete s_instance;
        s_instance = nullptr;
        qDebug() << "AccountManager shutdown";
    }
}

AccountManager::AccountManager(QObject* parent)
    : QObject(parent)
    , m_credentialStore(new CredentialStore(this))
    , m_sessionPool(new SessionPool(m_credentialStore, this))
    , m_initialized(false)
    , m_dirty(false)
{
    setupConnections();
}

AccountManager::~AccountManager()
{
    saveAccounts();
}

void AccountManager::setupConnections()
{
    connect(m_sessionPool, &SessionPool::sessionReady,
            this, &AccountManager::onSessionReady);
    connect(m_sessionPool, &SessionPool::sessionError,
            this, &AccountManager::onSessionError);
    connect(m_sessionPool, &SessionPool::sessionExpired,
            this, &AccountManager::onSessionExpired);
    connect(m_sessionPool, &SessionPool::loginRequired,
            this, &AccountManager::onLoginRequired);
}

void AccountManager::createDefaultGroup()
{
    if (m_groups.isEmpty()) {
        AccountGroup defaultGroup;
        defaultGroup.id = AccountGroup::generateId();
        defaultGroup.name = "Default";
        defaultGroup.color = QColor("#2196F3");  // Blue
        defaultGroup.sortOrder = 0;
        m_groups[defaultGroup.id] = defaultGroup;
        m_dirty = true;
    }
}

QString AccountManager::generateAccountId() const
{
    return MegaAccount::generateId();
}

// ============================================================================
// Account Management
// ============================================================================

void AccountManager::addAccount(const QString& email, const QString& password)
{
    if (email.isEmpty() || password.isEmpty()) {
        emit accountAddFailed(email, "Email and password are required");
        return;
    }

    // Check if account already exists
    for (const MegaAccount& acc : m_accounts) {
        if (acc.email.toLower() == email.toLower()) {
            emit accountAddFailed(email, "Account already exists");
            return;
        }
    }

    performLogin(email, password);
}

void AccountManager::performLogin(const QString& email, const QString& password)
{
    qDebug() << "AccountManager: Starting async login for:" << email;

    // Check if already logging in
    if (m_loginThread && m_loginThread->isRunning()) {
        emit accountAddFailed(email, "Another login is in progress");
        return;
    }

    // Store pending email for use in success handler
    m_pendingLoginEmail = email;

    // Clean up previous thread/worker if any
    if (m_loginThread) {
        m_loginThread->quit();
        m_loginThread->wait();
        delete m_loginThread;
        m_loginThread = nullptr;
    }
    if (m_loginWorker) {
        delete m_loginWorker;
        m_loginWorker = nullptr;
    }

    // Create worker and thread
    m_loginThread = new QThread(this);
    m_loginWorker = new LoginWorker(email, password);
    m_loginWorker->moveToThread(m_loginThread);

    // Connect signals
    connect(m_loginThread, &QThread::started, m_loginWorker, &LoginWorker::doLogin);
    connect(m_loginWorker, &LoginWorker::progress, this, &AccountManager::onLoginWorkerProgress);
    connect(m_loginWorker, &LoginWorker::success, this, &AccountManager::onLoginWorkerSuccess);
    connect(m_loginWorker, &LoginWorker::failed, this, &AccountManager::onLoginWorkerFailed);

    // Clean up when done
    connect(m_loginWorker, &LoginWorker::success, m_loginThread, &QThread::quit);
    connect(m_loginWorker, &LoginWorker::failed, m_loginThread, &QThread::quit);

    // Start the login process
    emit loginProgress(email, 0, "Starting login...");
    m_loginThread->start();
}

void AccountManager::onLoginWorkerProgress(int progress, const QString& status)
{
    emit loginProgress(m_pendingLoginEmail, progress, status);
}

void AccountManager::onLoginWorkerSuccess(const QString& sessionKey, qint64 storageUsed, qint64 storageTotal)
{
    qDebug() << "AccountManager: Login successful for:" << m_pendingLoginEmail;

    // Create account entry
    MegaAccount account;
    account.id = generateAccountId();
    account.email = m_pendingLoginEmail;
    account.displayName = m_pendingLoginEmail.split('@').first();
    account.storageUsed = storageUsed;
    account.storageTotal = storageTotal;
    account.lastLogin = QDateTime::currentDateTime();
    account.isDefault = m_accounts.isEmpty();

    // Assign to first group
    if (!m_groups.isEmpty()) {
        account.groupId = m_groups.first().id;
    }

    // Store account
    m_accounts[account.id] = account;
    m_dirty = true;

    // Store session in credential store
    m_credentialStore->saveSession(account.id, sessionKey);

    // Save to disk
    saveAccounts();

    // Clear pending state
    m_pendingLoginEmail.clear();

    emit accountAdded(account);

    // Switch to new account
    switchToAccount(account.id);
}

void AccountManager::onLoginWorkerFailed(const QString& error)
{
    qWarning() << "AccountManager: Login failed for:" << m_pendingLoginEmail << "-" << error;

    QString email = m_pendingLoginEmail;
    m_pendingLoginEmail.clear();

    emit accountAddFailed(email, error);
}

void AccountManager::addAccountWithSession(const QString& email, const QString& sessionToken)
{
    if (email.isEmpty() || sessionToken.isEmpty()) {
        emit accountAddFailed(email, "Email and session token are required");
        return;
    }

    // Check if account already exists
    for (const MegaAccount& acc : m_accounts) {
        if (acc.email.toLower() == email.toLower()) {
            emit accountAddFailed(email, "Account already exists");
            return;
        }
    }

    // Create account entry
    MegaAccount account;
    account.id = generateAccountId();
    account.email = email;
    account.displayName = email.split('@').first();
    account.lastLogin = QDateTime::currentDateTime();
    account.isDefault = m_accounts.isEmpty();

    if (!m_groups.isEmpty()) {
        account.groupId = m_groups.first().id;
    }

    // Store account
    m_accounts[account.id] = account;
    m_dirty = true;

    // Store session
    m_credentialStore->saveSession(account.id, sessionToken);

    saveAccounts();
    emit accountAdded(account);

    // Switch to new account
    switchToAccount(account.id);
}

void AccountManager::registerExistingSession(const QString& email, mega::MegaApi* api)
{
    if (email.isEmpty() || !api) {
        qWarning() << "AccountManager::registerExistingSession: Invalid parameters";
        return;
    }

    qDebug() << "AccountManager: Registering existing session for" << email;

    // Check if account already exists
    for (const MegaAccount& acc : m_accounts) {
        if (acc.email.toLower() == email.toLower()) {
            qDebug() << "AccountManager: Account already exists, switching to it";
            switchToAccount(acc.id);
            return;
        }
    }

    // Get storage info from the existing API
    qint64 storageUsed = 0;
    qint64 storageTotal = 0;

    mega::MegaNode* root = api->getRootNode();
    if (root) {
        storageUsed = api->getSize(root);
        delete root;
    }

    // Try to get total storage from account details
    // For now, use a reasonable default - will be updated later
    storageTotal = 2199023255552; // 2 TB default

    // Create account entry
    MegaAccount account;
    account.id = generateAccountId();
    account.email = email;
    account.displayName = email.split('@').first();
    account.storageUsed = storageUsed;
    account.storageTotal = storageTotal;
    account.lastLogin = QDateTime::currentDateTime();
    account.isDefault = m_accounts.isEmpty();

    // Assign to first group or create default
    createDefaultGroup();
    if (!m_groups.isEmpty()) {
        account.groupId = m_groups.first().id;
    }

    // Store account
    m_accounts[account.id] = account;
    m_activeAccountId = account.id;
    m_dirty = true;

    // Get session token and store it
    const char* session = api->dumpSession();
    if (session) {
        m_credentialStore->saveSession(account.id, QString::fromUtf8(session));
        delete[] session;
    }

    // Save to disk
    saveAccounts();

    qDebug() << "AccountManager: Registered account" << account.id << "for" << email;

    emit accountAdded(account);
    emit accountSwitched(account.id);
}

void AccountManager::removeAccount(const QString& accountId, bool deleteSession)
{
    if (!m_accounts.contains(accountId)) {
        return;
    }

    qDebug() << "AccountManager: Removing account" << accountId;

    // Release session if active
    m_sessionPool->releaseSession(accountId, !deleteSession);

    // Remove from accounts
    m_accounts.remove(accountId);
    m_dirty = true;

    // If this was the active account, switch to another
    if (m_activeAccountId == accountId) {
        m_activeAccountId.clear();
        if (!m_accounts.isEmpty()) {
            // Find default or first account
            for (const MegaAccount& acc : m_accounts) {
                if (acc.isDefault) {
                    switchToAccount(acc.id);
                    break;
                }
            }
            if (m_activeAccountId.isEmpty()) {
                switchToAccount(m_accounts.first().id);
            }
        }
    }

    saveAccounts();
    emit accountRemoved(accountId);
}

void AccountManager::updateAccount(const MegaAccount& account)
{
    if (!m_accounts.contains(account.id)) {
        return;
    }

    m_accounts[account.id] = account;
    m_dirty = true;
    saveAccounts();
    emit accountUpdated(account);
}

MegaAccount AccountManager::getAccount(const QString& accountId) const
{
    return m_accounts.value(accountId);
}

MegaAccount AccountManager::getAccountByEmail(const QString& email) const
{
    for (const MegaAccount& acc : m_accounts) {
        if (acc.email.toLower() == email.toLower()) {
            return acc;
        }
    }
    return MegaAccount();
}

QList<MegaAccount> AccountManager::allAccounts() const
{
    return m_accounts.values();
}

int AccountManager::accountCount() const
{
    return m_accounts.size();
}

// ============================================================================
// Group Management
// ============================================================================

void AccountManager::addGroup(const AccountGroup& group)
{
    if (group.id.isEmpty() || group.name.isEmpty()) {
        return;
    }

    m_groups[group.id] = group;
    m_dirty = true;
    saveAccounts();
    emit groupAdded(group);
}

void AccountManager::removeGroup(const QString& groupId, bool moveAccountsToDefault)
{
    if (!m_groups.contains(groupId)) {
        return;
    }

    // Don't remove the last group
    if (m_groups.size() <= 1) {
        qWarning() << "Cannot remove last group";
        return;
    }

    // Move accounts to first remaining group
    if (moveAccountsToDefault) {
        QString newGroupId;
        for (const AccountGroup& g : m_groups) {
            if (g.id != groupId) {
                newGroupId = g.id;
                break;
            }
        }

        for (auto it = m_accounts.begin(); it != m_accounts.end(); ++it) {
            if (it.value().groupId == groupId) {
                it.value().groupId = newGroupId;
            }
        }
    }

    m_groups.remove(groupId);
    m_dirty = true;
    saveAccounts();
    emit groupRemoved(groupId);
}

void AccountManager::updateGroup(const AccountGroup& group)
{
    if (!m_groups.contains(group.id)) {
        return;
    }

    m_groups[group.id] = group;
    m_dirty = true;
    saveAccounts();
    emit groupUpdated(group);
}

AccountGroup AccountManager::getGroup(const QString& groupId) const
{
    return m_groups.value(groupId);
}

QList<AccountGroup> AccountManager::allGroups() const
{
    QList<AccountGroup> groups = m_groups.values();

    // Sort by sortOrder
    std::sort(groups.begin(), groups.end(), [](const AccountGroup& a, const AccountGroup& b) {
        return a.sortOrder < b.sortOrder;
    });

    return groups;
}

QList<MegaAccount> AccountManager::accountsInGroup(const QString& groupId) const
{
    QList<MegaAccount> result;
    for (const MegaAccount& acc : m_accounts) {
        if (acc.groupId == groupId) {
            result.append(acc);
        }
    }
    return result;
}

// ============================================================================
// Session Management
// ============================================================================

void AccountManager::switchToAccount(const QString& accountId)
{
    if (!m_accounts.contains(accountId)) {
        emit sessionError(accountId, "Account not found");
        return;
    }

    qDebug() << "AccountManager: Switching to account" << accountId;

    m_activeAccountId = accountId;

    // Always emit accountSwitched so the UI updates to show the new active account
    // Even if login is required, the account is now "active" from UI perspective
    emit accountSwitched(accountId);

    // Check if session is already active - if not, try to get/create one
    if (!m_sessionPool->isSessionActive(accountId)) {
        // Get or create session - will emit sessionReady or loginRequired
        m_sessionPool->getSession(accountId);
    }
}

QString AccountManager::activeAccountId() const
{
    return m_activeAccountId;
}

MegaAccount* AccountManager::activeAccount()
{
    if (m_activeAccountId.isEmpty() || !m_accounts.contains(m_activeAccountId)) {
        return nullptr;
    }
    return &m_accounts[m_activeAccountId];
}

const MegaAccount* AccountManager::activeAccount() const
{
    if (m_activeAccountId.isEmpty() || !m_accounts.contains(m_activeAccountId)) {
        return nullptr;
    }
    auto it = m_accounts.constFind(m_activeAccountId);
    if (it != m_accounts.constEnd()) {
        return &it.value();
    }
    return nullptr;
}

mega::MegaApi* AccountManager::activeApi() const
{
    if (m_activeAccountId.isEmpty()) {
        return nullptr;
    }
    return m_sessionPool->getSession(m_activeAccountId);
}

mega::MegaApi* AccountManager::getApi(const QString& accountId) const
{
    return m_sessionPool->getSession(accountId);
}

void AccountManager::refreshStorageInfo()
{
    if (m_activeAccountId.isEmpty()) {
        return;
    }

    mega::MegaApi* api = activeApi();
    if (!api) {
        return;
    }

    // Get account details asynchronously
    // For now, use synchronous for simplicity
    class AccountListener : public mega::MegaRequestListener {
    public:
        bool finished = false;
        qint64 used = 0;
        qint64 total = 0;

        void onRequestFinish(mega::MegaApi*, mega::MegaRequest* request, mega::MegaError* e) override {
            finished = true;
            if (e->getErrorCode() == mega::MegaError::API_OK) {
                used = request->getNumber();
                total = request->getTotalBytes();
            }
        }
    };

    AccountListener listener;
    api->getAccountDetails(&listener);

    int waited = 0;
    while (!listener.finished && waited < 10000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        QThread::msleep(100);
        waited += 100;
    }

    if (listener.finished && m_accounts.contains(m_activeAccountId)) {
        m_accounts[m_activeAccountId].storageUsed = listener.used;
        m_accounts[m_activeAccountId].storageTotal = listener.total;
        m_dirty = true;
        emit storageInfoUpdated(m_activeAccountId);
    }
}

void AccountManager::updateAccountSession(const QString& accountId, mega::MegaApi* api)
{
    if (!m_accounts.contains(accountId) || !api) {
        qWarning() << "AccountManager: Cannot update session - account not found or api null";
        return;
    }

    // Get session token and store it
    const char* session = api->dumpSession();
    if (session) {
        m_credentialStore->saveSession(accountId, QString::fromUtf8(session));
        delete[] session;
        qDebug() << "AccountManager: Updated session for account" << accountId;
    }

    // Update last login time
    m_accounts[accountId].lastLogin = QDateTime::currentDateTime();
    m_dirty = true;
}

bool AccountManager::isLoggedIn(const QString& accountId) const
{
    return m_sessionPool->isSessionActive(accountId);
}

bool AccountManager::isAccountSyncing(const QString& accountId) const
{
    return m_syncingAccounts.contains(accountId);
}

void AccountManager::setAccountSyncing(const QString& accountId, bool syncing)
{
    bool wasChanged = false;
    if (syncing) {
        if (!m_syncingAccounts.contains(accountId)) {
            m_syncingAccounts.insert(accountId);
            wasChanged = true;
        }
    } else {
        if (m_syncingAccounts.remove(accountId)) {
            wasChanged = true;
        }
    }

    if (wasChanged) {
        emit syncStatusChanged(accountId, syncing);
    }
}

SessionPool* AccountManager::sessionPool() const
{
    return m_sessionPool;
}

// ============================================================================
// Search & Filter
// ============================================================================

QList<MegaAccount> AccountManager::search(const QString& query) const
{
    if (query.isEmpty()) {
        return allAccounts();
    }

    QList<MegaAccount> results;
    for (const MegaAccount& acc : m_accounts) {
        if (acc.matchesSearch(query)) {
            results.append(acc);
        }
    }
    return results;
}

QList<MegaAccount> AccountManager::findByLabel(const QString& label) const
{
    QList<MegaAccount> results;
    QString lowerLabel = label.toLower();
    for (const MegaAccount& acc : m_accounts) {
        for (const QString& l : acc.labels) {
            if (l.toLower() == lowerLabel) {
                results.append(acc);
                break;
            }
        }
    }
    return results;
}

// ============================================================================
// Settings
// ============================================================================

AccountSettings AccountManager::settings() const
{
    return m_settings;
}

void AccountManager::setSettings(const AccountSettings& settings)
{
    m_settings = settings;
    m_sessionPool->setMaxSessions(settings.maxCachedSessions);
    m_dirty = true;
    saveAccounts();
}

// ============================================================================
// Persistence
// ============================================================================

QString AccountManager::configFilePath() const
{
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return configPath + "/MegaCustom/accounts.json";
}

void AccountManager::saveAccounts()
{
    if (!m_dirty && m_initialized) {
        return;
    }

    QString filePath = configFilePath();
    QDir dir = QFileInfo(filePath).dir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QJsonObject root;
    root["version"] = 1;
    root["activeAccountId"] = m_activeAccountId;

    // Save groups
    QJsonArray groupsArray;
    for (const AccountGroup& group : m_groups) {
        groupsArray.append(group.toJson());
    }
    root["groups"] = groupsArray;

    // Save accounts (without session tokens)
    QJsonArray accountsArray;
    for (const MegaAccount& account : m_accounts) {
        accountsArray.append(account.toJson());
    }
    root["accounts"] = accountsArray;

    // Save settings
    root["settings"] = m_settings.toJson();

    QJsonDocument doc(root);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "AccountManager: Cannot save accounts:" << file.errorString();
        return;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    m_dirty = false;
    qDebug() << "AccountManager: Saved" << m_accounts.size() << "accounts to" << filePath;
}

void AccountManager::loadAccounts()
{
    QString filePath = configFilePath();
    QFile file(filePath);

    if (!file.exists()) {
        qDebug() << "AccountManager: No accounts file found, creating default";
        createDefaultGroup();
        m_initialized = true;
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "AccountManager: Cannot read accounts:" << file.errorString();
        createDefaultGroup();
        m_initialized = true;
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "AccountManager: JSON parse error:" << parseError.errorString();
        createDefaultGroup();
        m_initialized = true;
        return;
    }

    QJsonObject root = doc.object();

    // Load groups
    m_groups.clear();
    QJsonArray groupsArray = root["groups"].toArray();
    for (const QJsonValue& v : groupsArray) {
        AccountGroup group = AccountGroup::fromJson(v.toObject());
        if (group.isValid()) {
            m_groups[group.id] = group;
        }
    }

    // Ensure at least one group exists
    if (m_groups.isEmpty()) {
        createDefaultGroup();
    }

    // Load accounts
    m_accounts.clear();
    QJsonArray accountsArray = root["accounts"].toArray();
    for (const QJsonValue& v : accountsArray) {
        MegaAccount account = MegaAccount::fromJson(v.toObject());
        if (account.isValid()) {
            m_accounts[account.id] = account;
        }
    }

    // Load settings
    if (root.contains("settings")) {
        m_settings = AccountSettings::fromJson(root["settings"].toObject());
        m_sessionPool->setMaxSessions(m_settings.maxCachedSessions);
    }

    // Load active account
    m_activeAccountId = root["activeAccountId"].toString();

    m_initialized = true;
    m_dirty = false;

    qDebug() << "AccountManager: Loaded" << m_accounts.size() << "accounts,"
             << m_groups.size() << "groups";

    // Auto-restore session for active account if enabled
    if (m_settings.autoRestoreSession && !m_activeAccountId.isEmpty()) {
        if (m_accounts.contains(m_activeAccountId)) {
            qDebug() << "AccountManager: Restoring session for" << m_activeAccountId;
            m_sessionPool->getSession(m_activeAccountId);
        }
    }
}

// ============================================================================
// Session Slots
// ============================================================================

void AccountManager::onSessionReady(const QString& accountId)
{
    qDebug() << "AccountManager: Session ready for" << accountId;

    if (m_accounts.contains(accountId)) {
        m_accounts[accountId].lastLogin = QDateTime::currentDateTime();
        m_dirty = true;
    }

    emit sessionReady(accountId);

    if (accountId == m_activeAccountId) {
        emit accountSwitched(accountId);
    }
}

void AccountManager::onSessionError(const QString& accountId, const QString& error)
{
    qDebug() << "AccountManager: Session error for" << accountId << "-" << error;
    emit sessionError(accountId, error);
}

void AccountManager::onSessionExpired(const QString& accountId)
{
    qDebug() << "AccountManager: Session expired for" << accountId;
    emit sessionExpired(accountId);
}

void AccountManager::onLoginRequired(const QString& accountId)
{
    qDebug() << "AccountManager: Login required for" << accountId;
    emit loginRequired(accountId);
}

} // namespace MegaCustom
