#ifndef MEGACUSTOM_WORDPRESSCONFIGDIALOG_H
#define MEGACUSTOM_WORDPRESSCONFIGDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QTableWidget>
#include <QGroupBox>
#include <QThread>

namespace MegaCustom {

class WordPressSync;
struct WordPressConfig;
struct SyncResult;

/**
 * Worker thread for WordPress operations
 */
class WpSyncWorker : public QObject {
    Q_OBJECT
public:
    enum class Operation {
        TestConnection,
        GetFields,
        SyncAll,
        SyncPreview
    };

    explicit WpSyncWorker(QObject* parent = nullptr);

    void setOperation(Operation op) { m_operation = op; }
    void setSiteUrl(const QString& url) { m_siteUrl = url; }
    void setUsername(const QString& user) { m_username = user; }
    void setPassword(const QString& pass) { m_password = pass; }

public slots:
    void process();
    void cancel();

signals:
    void testResult(bool success, const QString& error, const QString& siteName);
    void fieldsResult(bool success, const QStringList& fields, const QString& error);
    void syncProgress(int current, int total, const QString& username);
    void syncResult(bool success, int created, int updated, int skipped, int failed, const QString& error);
    void finished();

private:
    Operation m_operation = Operation::TestConnection;
    QString m_siteUrl;
    QString m_username;
    QString m_password;
    bool m_cancelled = false;
};

/**
 * Dialog for configuring WordPress REST API connection
 * and syncing member data from WordPress
 */
class WordPressConfigDialog : public QDialog {
    Q_OBJECT

public:
    explicit WordPressConfigDialog(QWidget* parent = nullptr);
    ~WordPressConfigDialog();

signals:
    void syncCompleted(int membersCreated, int membersUpdated);

private slots:
    void onTestConnection();
    void onGetFields();
    void onSyncNow();
    void onPreviewSync();
    void onSaveConfig();
    void onUrlChanged();
    void onCancelSync();
    void onAddFieldRow();
    void onRemoveFieldRow();

    // Worker signals
    void onTestResult(bool success, const QString& error, const QString& siteName);
    void onFieldsResult(bool success, const QStringList& fields, const QString& error);
    void onSyncProgress(int current, int total, const QString& username);
    void onSyncResult(bool success, int created, int updated, int skipped, int failed, const QString& error);
    void onWorkerFinished();

private:
    void setupUI();
    void loadConfig();
    void saveConfig();
    void setControlsEnabled(bool enabled);
    void updateStatus(const QString& message, bool isError = false);

    // Connection settings
    QLineEdit* m_urlEdit;
    QLineEdit* m_usernameEdit;
    QLineEdit* m_passwordEdit;
    QPushButton* m_testBtn;
    QLabel* m_connectionStatus;

    // Sync options
    QCheckBox* m_createNewCheck;
    QCheckBox* m_updateExistingCheck;
    QSpinBox* m_perPageSpin;
    QSpinBox* m_timeoutSpin;

    // Field mappings
    QTableWidget* m_fieldTable;
    QPushButton* m_getFieldsBtn;
    QPushButton* m_addFieldBtn;
    QPushButton* m_removeFieldBtn;

    // Role filter
    QComboBox* m_roleCombo;

    // Actions
    QPushButton* m_cancelBtn;
    QPushButton* m_previewBtn;
    QPushButton* m_syncBtn;
    QPushButton* m_saveBtn;
    QPushButton* m_closeBtn;

    // Progress
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;

    // Worker thread
    QThread* m_workerThread = nullptr;
    WpSyncWorker* m_worker = nullptr;
    bool m_isWorking = false;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_WORDPRESSCONFIGDIALOG_H
