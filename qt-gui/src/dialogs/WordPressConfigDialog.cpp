#include "WordPressConfigDialog.h"
#include "WordPressSyncPreviewDialog.h"
#include "integrations/WordPressSync.h"
#include "widgets/ButtonFactory.h"
#include "styles/ThemeManager.h"
#include "utils/DpiScaler.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QMessageBox>
#include <QTimer>
#include <QDebug>

namespace MegaCustom {

// ============================================================================
// WpSyncWorker
// ============================================================================

WpSyncWorker::WpSyncWorker(QObject* parent)
    : QObject(parent)
{
}

void WpSyncWorker::process()
{
    m_cancelled = false;

    WordPressSync sync;
    WordPressConfig config;
    config.siteUrl = m_siteUrl.toStdString();
    config.username = m_username.toStdString();
    config.applicationPassword = m_password.toStdString();
    sync.setConfig(config);

    switch (m_operation) {
    case Operation::TestConnection: {
        std::string error;
        bool success = sync.testConnection(error);
        QString siteName;
        if (success) {
            std::string siteError;
            auto info = sync.getSiteInfo(siteError);
            if (info.count("name")) {
                siteName = QString::fromStdString(info["name"]);
            }
        }
        emit testResult(success, QString::fromStdString(error), siteName);
        break;
    }

    case Operation::GetFields: {
        std::string error;
        auto fields = sync.getAvailableFields(error);
        QStringList fieldList;
        for (const auto& f : fields) {
            fieldList << QString::fromStdString(f);
        }
        emit fieldsResult(!fields.empty(), fieldList, QString::fromStdString(error));
        break;
    }

    case Operation::SyncAll: {
        sync.setProgressCallback([this](const WpSyncProgress& progress) {
            if (m_cancelled) return;
            emit syncProgress(progress.currentUser, progress.totalUsers,
                            QString::fromStdString(progress.currentUsername));
        });

        SyncResult result = sync.syncAll();
        emit syncResult(result.success,
                       result.usersCreated,
                       result.usersUpdated,
                       result.usersSkipped,
                       result.usersFailed,
                       QString::fromStdString(result.error));
        break;
    }

    case Operation::SyncPreview: {
        sync.setProgressCallback([this](const WpSyncProgress& progress) {
            if (m_cancelled) return;
            emit syncProgress(progress.currentUser, progress.totalUsers,
                            QString::fromStdString(progress.currentUsername));
        });

        SyncResult result = sync.previewSync();
        emit syncResult(result.success,
                       result.usersCreated,
                       result.usersUpdated,
                       result.usersSkipped,
                       result.usersFailed,
                       QString::fromStdString(result.error));
        break;
    }
    }

    emit finished();
}

void WpSyncWorker::cancel()
{
    m_cancelled = true;
}

// ============================================================================
// WordPressConfigDialog
// ============================================================================

WordPressConfigDialog::WordPressConfigDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("WordPress Sync Configuration");
    setMinimumSize(DpiScaler::scale(550), DpiScaler::scale(500));
    resize(DpiScaler::scale(600), DpiScaler::scale(550));

    setupUI();
    loadConfig();
}

WordPressConfigDialog::~WordPressConfigDialog()
{
    if (m_workerThread) {
        if (m_worker) {
            m_worker->cancel();
        }
        m_workerThread->quit();
        m_workerThread->wait();
    }
}

void WordPressConfigDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(DpiScaler::scale(12));

    // ========================================
    // Connection Settings Group
    // ========================================
    QGroupBox* connGroup = new QGroupBox("WordPress Connection", this);
    QFormLayout* connLayout = new QFormLayout(connGroup);
    connLayout->setSpacing(DpiScaler::scale(8));

    m_urlEdit = new QLineEdit(this);
    m_urlEdit->setPlaceholderText("https://yoursite.com");
    connect(m_urlEdit, &QLineEdit::textChanged, this, &WordPressConfigDialog::onUrlChanged);
    connLayout->addRow("Site URL:", m_urlEdit);

    m_usernameEdit = new QLineEdit(this);
    m_usernameEdit->setPlaceholderText("WordPress username");
    connLayout->addRow("Username:", m_usernameEdit);

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText("Application Password (not your login password)");
    connLayout->addRow("App Password:", m_passwordEdit);

    // Test connection button and status
    QHBoxLayout* testLayout = new QHBoxLayout();
    m_testBtn = ButtonFactory::createSecondary("Test Connection", this);
    m_testBtn->setIcon(QIcon(":/icons/zap.svg"));
    connect(m_testBtn, &QPushButton::clicked, this, &WordPressConfigDialog::onTestConnection);
    testLayout->addWidget(m_testBtn);

    m_connectionStatus = new QLabel(this);
    m_connectionStatus->setWordWrap(true);
    testLayout->addWidget(m_connectionStatus, 1);
    connLayout->addRow("", testLayout);

    // Help text
    QLabel* helpLabel = new QLabel(
        "<small>Use WordPress Application Passwords (Users > Profile > Application Passwords). "
        "Requires WordPress 5.6+</small>", this);
    helpLabel->setWordWrap(true);
    helpLabel->setStyleSheet("color: #666;");
    connLayout->addRow("", helpLabel);

    mainLayout->addWidget(connGroup);

    // ========================================
    // Sync Options Group
    // ========================================
    QGroupBox* optionsGroup = new QGroupBox("Sync Options", this);
    QFormLayout* optionsLayout = new QFormLayout(optionsGroup);
    optionsLayout->setSpacing(DpiScaler::scale(8));

    m_createNewCheck = new QCheckBox("Create new members for WordPress users not in registry", this);
    m_createNewCheck->setChecked(true);
    optionsLayout->addRow(m_createNewCheck);

    m_updateExistingCheck = new QCheckBox("Update existing members with WordPress data", this);
    m_updateExistingCheck->setChecked(true);
    optionsLayout->addRow(m_updateExistingCheck);

    QHBoxLayout* spinLayout = new QHBoxLayout();
    m_perPageSpin = new QSpinBox(this);
    m_perPageSpin->setRange(10, 100);
    m_perPageSpin->setValue(100);
    m_perPageSpin->setSuffix(" users/page");
    spinLayout->addWidget(new QLabel("Per page:", this));
    spinLayout->addWidget(m_perPageSpin);
    spinLayout->addSpacing(DpiScaler::scale(20));

    m_timeoutSpin = new QSpinBox(this);
    m_timeoutSpin->setRange(10, 120);
    m_timeoutSpin->setValue(30);
    m_timeoutSpin->setSuffix(" seconds");
    spinLayout->addWidget(new QLabel("Timeout:", this));
    spinLayout->addWidget(m_timeoutSpin);
    spinLayout->addStretch();
    optionsLayout->addRow(spinLayout);

    // Role filter
    QHBoxLayout* roleLayout = new QHBoxLayout();
    m_roleCombo = new QComboBox(this);
    m_roleCombo->addItem("All Roles", "");
    m_roleCombo->addItem("Administrator", "administrator");
    m_roleCombo->addItem("Editor", "editor");
    m_roleCombo->addItem("Author", "author");
    m_roleCombo->addItem("Contributor", "contributor");
    m_roleCombo->addItem("Subscriber", "subscriber");
    m_roleCombo->addItem("Customer (WooCommerce)", "customer");
    roleLayout->addWidget(new QLabel("Filter by role:", this));
    roleLayout->addWidget(m_roleCombo);
    roleLayout->addStretch();
    optionsLayout->addRow(roleLayout);

    mainLayout->addWidget(optionsGroup);

    // ========================================
    // Field Mappings Group
    // ========================================
    QGroupBox* fieldsGroup = new QGroupBox("Field Mappings", this);
    QVBoxLayout* fieldsLayout = new QVBoxLayout(fieldsGroup);

    QHBoxLayout* fieldsTopLayout = new QHBoxLayout();
    m_getFieldsBtn = ButtonFactory::createSecondary("Fetch Available Fields", this);
    m_getFieldsBtn->setIcon(QIcon(":/icons/download.svg"));
    connect(m_getFieldsBtn, &QPushButton::clicked, this, &WordPressConfigDialog::onGetFields);
    fieldsTopLayout->addWidget(m_getFieldsBtn);
    fieldsTopLayout->addStretch();
    fieldsLayout->addLayout(fieldsTopLayout);

    m_fieldTable = new QTableWidget(this);
    m_fieldTable->setColumnCount(2);
    m_fieldTable->setHorizontalHeaderLabels({"WordPress Field", "Member Field"});
    m_fieldTable->horizontalHeader()->setStretchLastSection(true);
    m_fieldTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_fieldTable->setMinimumHeight(DpiScaler::scale(120));
    m_fieldTable->setMaximumHeight(DpiScaler::scale(150));
    // Make table editable on double-click or key press
    m_fieldTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);

    // Add default mappings
    QStringList defaultMappings = {
        "user_email|email",
        "display_name|displayName",
        "user_login|id",
        "meta.ip_address|ipAddress",
        "meta.social_handle|socialHandle"
    };
    m_fieldTable->setRowCount(defaultMappings.size());
    for (int i = 0; i < defaultMappings.size(); ++i) {
        QStringList parts = defaultMappings[i].split("|");
        m_fieldTable->setItem(i, 0, new QTableWidgetItem(parts[0]));
        m_fieldTable->setItem(i, 1, new QTableWidgetItem(parts[1]));
    }

    fieldsLayout->addWidget(m_fieldTable);

    // Add/Remove field row buttons
    QHBoxLayout* fieldBtnLayout = new QHBoxLayout();
    m_addFieldBtn = ButtonFactory::createSecondary("Add Row", this);
    m_addFieldBtn->setIcon(QIcon(":/icons/plus.svg"));
    connect(m_addFieldBtn, &QPushButton::clicked, this, &WordPressConfigDialog::onAddFieldRow);
    fieldBtnLayout->addWidget(m_addFieldBtn);

    m_removeFieldBtn = ButtonFactory::createSecondary("Remove Row", this);
    m_removeFieldBtn->setIcon(QIcon(":/icons/minus.svg"));
    connect(m_removeFieldBtn, &QPushButton::clicked, this, &WordPressConfigDialog::onRemoveFieldRow);
    fieldBtnLayout->addWidget(m_removeFieldBtn);
    fieldBtnLayout->addStretch();
    fieldsLayout->addLayout(fieldBtnLayout);

    mainLayout->addWidget(fieldsGroup);

    // ========================================
    // Progress
    // ========================================
    m_progressBar = new QProgressBar(this);
    m_progressBar->setVisible(false);
    mainLayout->addWidget(m_progressBar);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel);

    // ========================================
    // Buttons
    // ========================================
    mainLayout->addStretch();

    QHBoxLayout* buttonLayout = new QHBoxLayout();

    m_previewBtn = ButtonFactory::createSecondary("Preview Sync", this);
    m_previewBtn->setIcon(QIcon(":/icons/eye.svg"));
    connect(m_previewBtn, &QPushButton::clicked, this, &WordPressConfigDialog::onPreviewSync);
    buttonLayout->addWidget(m_previewBtn);

    // Use success-styled primary button for sync
    auto& tm = ThemeManager::instance();
    m_syncBtn = ButtonFactory::createPrimary("Sync Now", this);
    m_syncBtn->setIcon(QIcon(":/icons/refresh-cw.svg"));
    m_syncBtn->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: white; border: none; border-radius: 6px; padding: 8px 16px; font-weight: 600; }"
        "QPushButton:hover { background-color: %2; }"
        "QPushButton:pressed { background-color: %3; }")
        .arg(tm.supportSuccess().name())
        .arg(tm.supportSuccess().darker(110).name())
        .arg(tm.supportSuccess().darker(120).name()));
    connect(m_syncBtn, &QPushButton::clicked, this, &WordPressConfigDialog::onSyncNow);
    buttonLayout->addWidget(m_syncBtn);

    m_cancelBtn = ButtonFactory::createDestructive("Cancel", this);
    m_cancelBtn->setIcon(QIcon(":/icons/x.svg"));
    m_cancelBtn->setVisible(false);  // Hidden initially, shown during sync
    connect(m_cancelBtn, &QPushButton::clicked, this, &WordPressConfigDialog::onCancelSync);
    buttonLayout->addWidget(m_cancelBtn);

    buttonLayout->addStretch();

    m_saveBtn = ButtonFactory::createPrimary("Save Config", this);
    m_saveBtn->setIcon(QIcon(":/icons/save.svg"));
    connect(m_saveBtn, &QPushButton::clicked, this, &WordPressConfigDialog::onSaveConfig);
    buttonLayout->addWidget(m_saveBtn);

    m_closeBtn = ButtonFactory::createOutline("Close", this);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(m_closeBtn);

    mainLayout->addLayout(buttonLayout);
}

void WordPressConfigDialog::loadConfig()
{
    WordPressSync sync;
    if (sync.loadConfig()) {
        WordPressConfig config = sync.getConfig();
        m_urlEdit->setText(QString::fromStdString(config.siteUrl));
        m_usernameEdit->setText(QString::fromStdString(config.username));
        m_passwordEdit->setText(QString::fromStdString(config.applicationPassword));
        m_createNewCheck->setChecked(config.createNewMembers);
        m_updateExistingCheck->setChecked(config.updateExisting);
        m_perPageSpin->setValue(config.perPage);
        m_timeoutSpin->setValue(config.timeout);

        // Load field mappings
        if (!config.fieldMappings.empty()) {
            m_fieldTable->setRowCount(static_cast<int>(config.fieldMappings.size()));
            int row = 0;
            for (const auto& [wpField, memberField] : config.fieldMappings) {
                m_fieldTable->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(wpField)));
                m_fieldTable->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(memberField)));
                ++row;
            }
        }

        updateStatus("Configuration loaded from ~/.megacustom/wordpress.json");
    }
}

void WordPressConfigDialog::saveConfig()
{
    WordPressSync sync;
    WordPressConfig config;

    config.siteUrl = m_urlEdit->text().trimmed().toStdString();
    config.username = m_usernameEdit->text().trimmed().toStdString();
    config.applicationPassword = m_passwordEdit->text().toStdString();
    config.createNewMembers = m_createNewCheck->isChecked();
    config.updateExisting = m_updateExistingCheck->isChecked();
    config.perPage = m_perPageSpin->value();
    config.timeout = m_timeoutSpin->value();

    // Save field mappings
    config.fieldMappings.clear();
    for (int row = 0; row < m_fieldTable->rowCount(); ++row) {
        QTableWidgetItem* wpItem = m_fieldTable->item(row, 0);
        QTableWidgetItem* memberItem = m_fieldTable->item(row, 1);
        if (wpItem && memberItem && !wpItem->text().isEmpty() && !memberItem->text().isEmpty()) {
            config.fieldMappings[wpItem->text().toStdString()] = memberItem->text().toStdString();
        }
    }

    sync.setConfig(config);
    if (sync.saveConfig()) {
        updateStatus("Configuration saved successfully");
    } else {
        updateStatus("Failed to save configuration: " + QString::fromStdString(sync.getLastError()), true);
    }
}

void WordPressConfigDialog::setControlsEnabled(bool enabled)
{
    m_urlEdit->setEnabled(enabled);
    m_usernameEdit->setEnabled(enabled);
    m_passwordEdit->setEnabled(enabled);
    m_testBtn->setEnabled(enabled);
    m_createNewCheck->setEnabled(enabled);
    m_updateExistingCheck->setEnabled(enabled);
    m_perPageSpin->setEnabled(enabled);
    m_timeoutSpin->setEnabled(enabled);
    m_roleCombo->setEnabled(enabled);
    m_getFieldsBtn->setEnabled(enabled);
    m_fieldTable->setEnabled(enabled);
    m_addFieldBtn->setEnabled(enabled);
    m_removeFieldBtn->setEnabled(enabled);
    m_previewBtn->setEnabled(enabled);
    m_syncBtn->setEnabled(enabled);
    m_saveBtn->setEnabled(enabled);

    // Show cancel button when controls are disabled (syncing), hide when enabled
    m_cancelBtn->setVisible(!enabled);
}

void WordPressConfigDialog::updateStatus(const QString& message, bool isError)
{
    auto& tm = ThemeManager::instance();
    m_statusLabel->setText(message);
    if (isError) {
        m_statusLabel->setStyleSheet(QString("color: %1;").arg(tm.supportError().name()));
    } else {
        m_statusLabel->setStyleSheet(QString("color: %1;").arg(tm.supportSuccess().name()));
    }
}

void WordPressConfigDialog::onUrlChanged()
{
    m_connectionStatus->clear();
}

void WordPressConfigDialog::onTestConnection()
{
    if (m_urlEdit->text().trimmed().isEmpty()) {
        updateStatus("Please enter a WordPress site URL", true);
        return;
    }

    if (m_isWorking) return;
    m_isWorking = true;
    setControlsEnabled(false);
    m_connectionStatus->setText("Testing connection...");
    m_connectionStatus->setStyleSheet("color: #666;");

    // Create worker thread
    m_workerThread = new QThread(this);
    m_worker = new WpSyncWorker();
    m_worker->moveToThread(m_workerThread);

    m_worker->setOperation(WpSyncWorker::Operation::TestConnection);
    m_worker->setSiteUrl(m_urlEdit->text().trimmed());
    m_worker->setUsername(m_usernameEdit->text().trimmed());
    m_worker->setPassword(m_passwordEdit->text());

    connect(m_workerThread, &QThread::started, m_worker, &WpSyncWorker::process);
    connect(m_worker, &WpSyncWorker::testResult, this, &WordPressConfigDialog::onTestResult);
    connect(m_worker, &WpSyncWorker::finished, this, &WordPressConfigDialog::onWorkerFinished);
    connect(m_worker, &WpSyncWorker::finished, m_workerThread, &QThread::quit);
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater);

    m_workerThread->start();
}

void WordPressConfigDialog::onGetFields()
{
    if (m_urlEdit->text().trimmed().isEmpty()) {
        updateStatus("Please enter a WordPress site URL first", true);
        return;
    }

    if (m_isWorking) return;
    m_isWorking = true;
    setControlsEnabled(false);
    updateStatus("Fetching available fields...");

    m_workerThread = new QThread(this);
    m_worker = new WpSyncWorker();
    m_worker->moveToThread(m_workerThread);

    m_worker->setOperation(WpSyncWorker::Operation::GetFields);
    m_worker->setSiteUrl(m_urlEdit->text().trimmed());
    m_worker->setUsername(m_usernameEdit->text().trimmed());
    m_worker->setPassword(m_passwordEdit->text());

    connect(m_workerThread, &QThread::started, m_worker, &WpSyncWorker::process);
    connect(m_worker, &WpSyncWorker::fieldsResult, this, &WordPressConfigDialog::onFieldsResult);
    connect(m_worker, &WpSyncWorker::finished, this, &WordPressConfigDialog::onWorkerFinished);
    connect(m_worker, &WpSyncWorker::finished, m_workerThread, &QThread::quit);
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater);

    m_workerThread->start();
}

void WordPressConfigDialog::onSyncNow()
{
    if (m_urlEdit->text().trimmed().isEmpty()) {
        updateStatus("Please configure WordPress connection first", true);
        return;
    }

    int result = QMessageBox::question(this, "Sync Members",
        "This will sync member data from WordPress.\n\n"
        "New members will be created and existing members updated.\n\n"
        "Continue?",
        QMessageBox::Yes | QMessageBox::No);

    if (result != QMessageBox::Yes) return;

    if (m_isWorking) return;
    m_isWorking = true;
    setControlsEnabled(false);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, 0);  // Indeterminate
    updateStatus("Syncing members from WordPress...");

    // Save config first
    saveConfig();

    m_workerThread = new QThread(this);
    m_worker = new WpSyncWorker();
    m_worker->moveToThread(m_workerThread);

    m_worker->setOperation(WpSyncWorker::Operation::SyncAll);
    m_worker->setSiteUrl(m_urlEdit->text().trimmed());
    m_worker->setUsername(m_usernameEdit->text().trimmed());
    m_worker->setPassword(m_passwordEdit->text());

    connect(m_workerThread, &QThread::started, m_worker, &WpSyncWorker::process);
    connect(m_worker, &WpSyncWorker::syncProgress, this, &WordPressConfigDialog::onSyncProgress);
    connect(m_worker, &WpSyncWorker::syncResult, this, &WordPressConfigDialog::onSyncResult);
    connect(m_worker, &WpSyncWorker::finished, this, &WordPressConfigDialog::onWorkerFinished);
    connect(m_worker, &WpSyncWorker::finished, m_workerThread, &QThread::quit);
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater);

    m_workerThread->start();
}

void WordPressConfigDialog::onPreviewSync()
{
    if (m_urlEdit->text().trimmed().isEmpty()) {
        updateStatus("Please configure WordPress connection first", true);
        return;
    }

    // Save config first
    saveConfig();

    // Open the preview dialog
    WordPressSyncPreviewDialog* previewDialog = new WordPressSyncPreviewDialog(this);
    previewDialog->setCredentials(
        m_urlEdit->text().trimmed(),
        m_usernameEdit->text().trimmed(),
        m_passwordEdit->text()
    );

    // Pass the role filter if set
    QString role = m_roleCombo->currentData().toString();
    if (!role.isEmpty()) {
        previewDialog->setRole(role);
    }

    // Connect sync completed signal
    connect(previewDialog, &WordPressSyncPreviewDialog::syncCompleted,
            this, &WordPressConfigDialog::syncCompleted);

    // Start fetching users
    previewDialog->startFetch();

    // Show the dialog
    previewDialog->exec();

    // Clean up
    previewDialog->deleteLater();

    updateStatus("Preview dialog closed");
}

void WordPressConfigDialog::onSaveConfig()
{
    saveConfig();
}

void WordPressConfigDialog::onTestResult(bool success, const QString& error, const QString& siteName)
{
    auto& tm = ThemeManager::instance();
    if (success) {
        QString msg = "Connected successfully!";
        if (!siteName.isEmpty()) {
            msg += " Site: " + siteName;
        }
        m_connectionStatus->setText(msg);
        m_connectionStatus->setStyleSheet(QString("color: %1;").arg(tm.supportSuccess().name()));
    } else {
        m_connectionStatus->setText("Connection failed: " + error);
        m_connectionStatus->setStyleSheet(QString("color: %1;").arg(tm.supportError().name()));
    }
}

void WordPressConfigDialog::onFieldsResult(bool success, const QStringList& fields, const QString& error)
{
    if (success && !fields.isEmpty()) {
        // Show fields in a message box
        QString fieldList = fields.join("\n");
        QMessageBox::information(this, "Available WordPress Fields",
            "The following fields are available from WordPress:\n\n" + fieldList +
            "\n\nYou can map these to member fields in the table above.");
        updateStatus(QString("Found %1 available fields").arg(fields.size()));
    } else {
        updateStatus("Failed to fetch fields: " + error, true);
    }
}

void WordPressConfigDialog::onSyncProgress(int current, int total, const QString& username)
{
    if (total > 0) {
        m_progressBar->setRange(0, total);
        m_progressBar->setValue(current);
    }
    updateStatus(QString("Syncing %1 of %2: %3").arg(current).arg(total).arg(username));
}

void WordPressConfigDialog::onSyncResult(bool success, int created, int updated, int skipped, int failed, const QString& error)
{
    m_progressBar->setVisible(false);

    if (success) {
        QString msg = QString("Sync completed:\n"
                             "  Created: %1\n"
                             "  Updated: %2\n"
                             "  Skipped: %3\n"
                             "  Failed: %4")
            .arg(created).arg(updated).arg(skipped).arg(failed);

        QMessageBox::information(this, "Sync Complete", msg);
        updateStatus(QString("Sync complete: %1 created, %2 updated").arg(created).arg(updated));

        emit syncCompleted(created, updated);
    } else {
        QMessageBox::warning(this, "Sync Failed",
            "WordPress sync failed:\n" + error);
        updateStatus("Sync failed: " + error, true);
    }
}

void WordPressConfigDialog::onWorkerFinished()
{
    m_isWorking = false;
    m_workerThread = nullptr;
    m_worker = nullptr;
    setControlsEnabled(true);
    m_progressBar->setVisible(false);
}

void WordPressConfigDialog::onCancelSync()
{
    if (m_worker) {
        m_worker->cancel();
        updateStatus("Cancelling sync...");
    }
}

void WordPressConfigDialog::onAddFieldRow()
{
    int row = m_fieldTable->rowCount();
    m_fieldTable->insertRow(row);
    m_fieldTable->setItem(row, 0, new QTableWidgetItem(""));
    m_fieldTable->setItem(row, 1, new QTableWidgetItem(""));
    m_fieldTable->editItem(m_fieldTable->item(row, 0));
}

void WordPressConfigDialog::onRemoveFieldRow()
{
    int currentRow = m_fieldTable->currentRow();
    if (currentRow >= 0) {
        m_fieldTable->removeRow(currentRow);
    } else {
        updateStatus("Select a row to remove", true);
    }
}

} // namespace MegaCustom
