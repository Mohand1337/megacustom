#ifndef MEGACUSTOM_ACCOUNTMANAGER_H
#define MEGACUSTOM_ACCOUNTMANAGER_H

#include <QObject>
#include <QMap>
#include <QList>
#include <QSet>
#include <QThread>
#include "AccountModels.h"

// Forward declaration for MEGA SDK
namespace mega {
class MegaApi;
}

namespace MegaCustom {

class CredentialStore;
class SessionPool;
class LoginWorker;

/**
 * @brief Central coordinator for multi-account management
 *
 * AccountManager is a singleton that handles:
 * - Account CRUD operations
 * - Group management
 * - Session switching
 * - Persistence to accounts.json
 * - Coordination between CredentialStore and SessionPool
 */
class AccountManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Get the singleton instance
     */
    static AccountManager& instance();

    /**
     * @brief Initialize the account manager
     * @param parent Parent QObject (typically QApplication)
     *
     * Must be called once at application startup.
     */
    static void initialize(QObject* parent = nullptr);

    /**
     * @brief Shutdown the account manager
     *
     * Saves state and releases resources. Call at application exit.
     */
    static void shutdown();

    // ========================================================================
    // Account Management
    // ========================================================================

    /**
     * @brief Add a new account via email/password login
     * @param email MEGA account email
     * @param password MEGA account password
     *
     * Emits accountAdded() on success, accountAddFailed() on failure.
     * The account is automatically set as active after successful login.
     */
    void addAccount(const QString& email, const QString& password);

    /**
     * @brief Add a new account via session token
     * @param email MEGA account email
     * @param sessionToken Existing session token
     *
     * Used for restoring sessions or importing accounts.
     */
    void addAccountWithSession(const QString& email, const QString& sessionToken);

    /**
     * @brief Register an existing MegaApi session as an account
     * @param email MEGA account email
     * @param api The already-logged-in MegaApi instance
     *
     * Used when login happens through old AuthController and needs to
     * be registered with AccountManager for multi-account support.
     */
    void registerExistingSession(const QString& email, mega::MegaApi* api);

    /**
     * @brief Remove an account
     * @param accountId The account identifier
     * @param deleteSession If true, also removes stored session credentials
     */
    void removeAccount(const QString& accountId, bool deleteSession = true);

    /**
     * @brief Update account metadata
     * @param account Updated account data
     */
    void updateAccount(const MegaAccount& account);

    /**
     * @brief Get an account by ID
     * @param accountId The account identifier
     * @return The account, or invalid account if not found
     */
    MegaAccount getAccount(const QString& accountId) const;

    /**
     * @brief Get an account by email
     * @param email The account email
     * @return The account, or invalid account if not found
     */
    MegaAccount getAccountByEmail(const QString& email) const;

    /**
     * @brief Get all accounts
     * @return List of all accounts
     */
    QList<MegaAccount> allAccounts() const;

    /**
     * @brief Get account count
     */
    int accountCount() const;

    // ========================================================================
    // Group Management
    // ========================================================================

    /**
     * @brief Add a new group
     * @param group The group to add
     */
    void addGroup(const AccountGroup& group);

    /**
     * @brief Remove a group
     * @param groupId The group identifier
     * @param moveAccountsToDefault If true, moves accounts to default group
     */
    void removeGroup(const QString& groupId, bool moveAccountsToDefault = true);

    /**
     * @brief Update a group
     * @param group Updated group data
     */
    void updateGroup(const AccountGroup& group);

    /**
     * @brief Get a group by ID
     * @param groupId The group identifier
     * @return The group, or invalid group if not found
     */
    AccountGroup getGroup(const QString& groupId) const;

    /**
     * @brief Get all groups
     * @return List of all groups, sorted by sortOrder
     */
    QList<AccountGroup> allGroups() const;

    /**
     * @brief Get accounts in a specific group
     * @param groupId The group identifier
     * @return List of accounts in the group
     */
    QList<MegaAccount> accountsInGroup(const QString& groupId) const;

    // ========================================================================
    // Session Management
    // ========================================================================

    /**
     * @brief Switch to a different account
     * @param accountId The account identifier
     *
     * Emits accountSwitched() when the switch is complete.
     */
    void switchToAccount(const QString& accountId);

    /**
     * @brief Get the active account ID
     * @return The currently active account ID, or empty string
     */
    QString activeAccountId() const;

    /**
     * @brief Get the active account
     * @return Pointer to active account, or nullptr
     */
    MegaAccount* activeAccount();
    const MegaAccount* activeAccount() const;

    /**
     * @brief Get the MegaApi for the active account
     * @return The active MegaApi instance, or nullptr
     */
    mega::MegaApi* activeApi() const;

    /**
     * @brief Get MegaApi for a specific account
     * @param accountId The account identifier
     * @return The MegaApi instance, or nullptr
     */
    mega::MegaApi* getApi(const QString& accountId) const;

    /**
     * @brief Refresh storage info for active account
     */
    void refreshStorageInfo();

    /**
     * @brief Update session token for an existing account
     * @param accountId The account identifier
     * @param api The MegaApi with the active session
     */
    void updateAccountSession(const QString& accountId, mega::MegaApi* api);

    /**
     * @brief Check if an account is logged in
     * @param accountId The account identifier
     */
    bool isLoggedIn(const QString& accountId) const;

    /**
     * @brief Check if an account has active sync/transfer operations
     * @param accountId The account identifier
     * @return true if account is currently syncing/transferring
     */
    bool isAccountSyncing(const QString& accountId) const;

    /**
     * @brief Set sync status for an account (called by transfer managers)
     * @param accountId The account identifier
     * @param syncing Whether the account is syncing
     */
    void setAccountSyncing(const QString& accountId, bool syncing);

    /**
     * @brief Get the session pool
     * @return The SessionPool instance
     */
    SessionPool* sessionPool() const;

    // ========================================================================
    // Search & Filter
    // ========================================================================

    /**
     * @brief Search accounts by query
     * @param query Search string (matches email, display name, labels, notes)
     * @return Matching accounts
     */
    QList<MegaAccount> search(const QString& query) const;

    /**
     * @brief Find accounts by label
     * @param label The label to search for
     * @return Accounts with the specified label
     */
    QList<MegaAccount> findByLabel(const QString& label) const;

    // ========================================================================
    // Settings
    // ========================================================================

    /**
     * @brief Get account settings
     */
    AccountSettings settings() const;

    /**
     * @brief Update account settings
     */
    void setSettings(const AccountSettings& settings);

    // ========================================================================
    // Persistence
    // ========================================================================

    /**
     * @brief Save all account data to disk
     */
    void saveAccounts();

    /**
     * @brief Load account data from disk
     */
    void loadAccounts();

    /**
     * @brief Get the accounts config file path
     */
    QString configFilePath() const;

signals:
    // Account signals
    void accountAdded(const MegaAccount& account);
    void accountAddFailed(const QString& email, const QString& error);
    void accountRemoved(const QString& accountId);
    void loginProgress(const QString& email, int progress, const QString& status);
    void accountUpdated(const MegaAccount& account);
    void accountSwitched(const QString& accountId);

    // Group signals
    void groupAdded(const AccountGroup& group);
    void groupRemoved(const QString& groupId);
    void groupUpdated(const AccountGroup& group);

    // Session signals
    void sessionReady(const QString& accountId);
    void sessionError(const QString& accountId, const QString& error);
    void sessionExpired(const QString& accountId);
    void loginRequired(const QString& accountId);

    // Storage signals
    void storageInfoUpdated(const QString& accountId);

    // Sync status signals
    void syncStatusChanged(const QString& accountId, bool syncing);

private:
    explicit AccountManager(QObject* parent = nullptr);
    ~AccountManager();

    // Prevent copying
    AccountManager(const AccountManager&) = delete;
    AccountManager& operator=(const AccountManager&) = delete;

    void setupConnections();
    void createDefaultGroup();
    QString generateAccountId() const;
    void performLogin(const QString& email, const QString& password);

private slots:
    void onSessionReady(const QString& accountId);
    void onSessionError(const QString& accountId, const QString& error);
    void onSessionExpired(const QString& accountId);
    void onLoginRequired(const QString& accountId);

    // Login worker slots
    void onLoginWorkerProgress(int progress, const QString& status);
    void onLoginWorkerSuccess(const QString& sessionKey, qint64 storageUsed, qint64 storageTotal);
    void onLoginWorkerFailed(const QString& error);

private:
    static AccountManager* s_instance;

    QMap<QString, MegaAccount> m_accounts;
    QMap<QString, AccountGroup> m_groups;
    QString m_activeAccountId;
    AccountSettings m_settings;

    CredentialStore* m_credentialStore;
    SessionPool* m_sessionPool;

    bool m_initialized;
    bool m_dirty;  // True if unsaved changes exist

    // Track syncing accounts
    QSet<QString> m_syncingAccounts;

    // Async login state
    QThread* m_loginThread = nullptr;
    LoginWorker* m_loginWorker = nullptr;
    QString m_pendingLoginEmail;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_ACCOUNTMANAGER_H
