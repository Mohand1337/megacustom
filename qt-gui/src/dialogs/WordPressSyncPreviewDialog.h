#ifndef MEGACUSTOM_WORDPRESSSYNCPREVIEWDIALOG_H
#define MEGACUSTOM_WORDPRESSSYNCPREVIEWDIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QLineEdit>
#include <QDateEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QThread>
#include <vector>

namespace MegaCustom {

/**
 * WordPress user data for preview
 */
struct WpUserPreview {
    int wpUserId = 0;
    QString username;
    QString displayName;
    QString email;
    QString role;
    QDate registeredDate;
    bool selected = true;  // Selected for sync by default
};

/**
 * Worker thread for fetching WordPress users
 */
class WpFetchWorker : public QObject {
    Q_OBJECT
public:
    explicit WpFetchWorker(QObject* parent = nullptr);

    void setSiteUrl(const QString& url) { m_siteUrl = url; }
    void setUsername(const QString& user) { m_username = user; }
    void setPassword(const QString& pass) { m_password = pass; }
    void setRole(const QString& role) { m_role = role; }
    void setPerPage(int perPage) { m_perPage = perPage; }

public slots:
    void process();
    void cancel();

signals:
    void progress(int current, int total);
    void finished(const QList<WpUserPreview>& users, const QString& error);

private:
    QString m_siteUrl;
    QString m_username;
    QString m_password;
    QString m_role;
    int m_perPage = 100;
    bool m_cancelled = false;
};

/**
 * Worker thread for syncing selected WordPress users
 */
class WpSyncSelectedWorker : public QObject {
    Q_OBJECT
public:
    explicit WpSyncSelectedWorker(QObject* parent = nullptr);

    void setSiteUrl(const QString& url) { m_siteUrl = url; }
    void setUsername(const QString& user) { m_username = user; }
    void setPassword(const QString& pass) { m_password = pass; }
    void setUsers(const QList<WpUserPreview>& users) { m_users = users; }

public slots:
    void process();
    void cancel();

signals:
    void progress(int current, int total, const QString& username);
    void finished(int created, int updated, int failed, const QString& error);

private:
    QString m_siteUrl;
    QString m_username;
    QString m_password;
    QList<WpUserPreview> m_users;
    bool m_cancelled = false;
};

/**
 * Dialog for previewing and selecting WordPress users to sync
 */
class WordPressSyncPreviewDialog : public QDialog {
    Q_OBJECT

public:
    explicit WordPressSyncPreviewDialog(QWidget* parent = nullptr);
    ~WordPressSyncPreviewDialog();

    void setCredentials(const QString& siteUrl, const QString& username, const QString& password);
    void setRole(const QString& role);
    void startFetch();

signals:
    void syncCompleted(int created, int updated);

private slots:
    void onFetchProgress(int current, int total);
    void onFetchFinished(const QList<WpUserPreview>& users, const QString& error);
    void onSyncProgress(int current, int total, const QString& username);
    void onSyncFinished(int created, int updated, int failed, const QString& error);

    void onSearchChanged(const QString& text);
    void onDateFilterChanged();
    void onRoleFilterChanged(int index);
    void onSelectAllChanged(int state);
    void onSyncSelected();
    void onCancel();

private:
    void setupUI();
    void populateTable();
    void applyFilters();
    void updateStats();
    bool matchesFilters(const WpUserPreview& user) const;
    QList<WpUserPreview> getSelectedUsers() const;
    void cleanupWorker();

    // Credentials
    QString m_siteUrl;
    QString m_username;
    QString m_password;
    QString m_initialRole;

    // Filter controls
    QDateEdit* m_fromDate;
    QDateEdit* m_toDate;
    QCheckBox* m_fromDateCheck;
    QCheckBox* m_toDateCheck;
    QComboBox* m_roleFilter;
    QLineEdit* m_searchEdit;

    // Table
    QCheckBox* m_selectAllCheck;
    QTableWidget* m_userTable;
    QLabel* m_statsLabel;

    // Actions
    QPushButton* m_syncBtn;
    QPushButton* m_cancelBtn;
    QPushButton* m_closeBtn;

    // Progress
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;

    // Data
    QList<WpUserPreview> m_allUsers;
    QList<int> m_visibleIndices;  // Indices into m_allUsers for filtered view

    // Worker threads
    QThread* m_workerThread = nullptr;
    WpFetchWorker* m_fetchWorker = nullptr;
    WpSyncSelectedWorker* m_syncWorker = nullptr;
    bool m_isFetching = false;
    bool m_isSyncing = false;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_WORDPRESSSYNCPREVIEWDIALOG_H
