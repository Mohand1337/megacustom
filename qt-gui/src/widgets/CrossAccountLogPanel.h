#ifndef MEGACUSTOM_CROSSACCOUNTLOGPANEL_H
#define MEGACUSTOM_CROSSACCOUNTLOGPANEL_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QDateEdit>
#include <QLineEdit>
#include <QProgressBar>
#include "accounts/AccountModels.h"

namespace MegaCustom {

class CrossAccountTransferManager;
class TransferLogStore;

/**
 * @brief Panel displaying cross-account transfer history and active transfers
 *
 * Shows:
 * - Active transfers with progress
 * - Completed/failed transfer history
 * - Filtering by status, date, account
 * - Retry and cancel actions
 */
class CrossAccountLogPanel : public QWidget
{
    Q_OBJECT

public:
    explicit CrossAccountLogPanel(QWidget* parent = nullptr);
    ~CrossAccountLogPanel() override = default;

    /**
     * @brief Set the transfer manager
     */
    void setTransferManager(CrossAccountTransferManager* manager);

    /**
     * @brief Refresh the transfer list
     */
    void refresh();

signals:
    void navigateToPath(const QString& accountId, const QString& path);

private slots:
    void onStatusFilterChanged(int index);
    void onDateFilterChanged();
    void onSearchChanged(const QString& text);
    void onClearLogClicked();
    void onRefreshClicked();

    void onTransferItemClicked(QListWidgetItem* item);
    void onRetryClicked();
    void onCancelClicked();

    // Transfer manager signals
    void onTransferStarted(const CrossAccountTransfer& transfer);
    void onTransferProgress(const QString& transferId, int percent, qint64 bytesTransferred, qint64 bytesTotal);
    void onTransferCompleted(const CrossAccountTransfer& transfer);
    void onTransferFailed(const CrossAccountTransfer& transfer);
    void onTransferCancelled(const QString& transferId);

private:
    void setupUI();
    void connectSignals();
    void populateList();
    void updateStatusCounts();

    QWidget* createTransferItemWidget(const CrossAccountTransfer& transfer);
    QString formatBytes(qint64 bytes) const;
    QString formatDuration(qint64 seconds) const;
    QString getStatusText(CrossAccountTransfer::Status status) const;
    QString getStatusColor(CrossAccountTransfer::Status status) const;
    QString getAccountEmail(const QString& accountId) const;

    // Header
    QLabel* m_titleLabel;
    QLabel* m_countLabel;

    // Filters
    QComboBox* m_statusFilter;
    QDateEdit* m_fromDate;
    QDateEdit* m_toDate;
    QLineEdit* m_searchEdit;
    QPushButton* m_refreshBtn;
    QPushButton* m_clearBtn;

    // Transfer list
    QListWidget* m_transferList;

    // Action buttons
    QPushButton* m_retryBtn;
    QPushButton* m_cancelBtn;

    // Status bar
    QLabel* m_statusLabel;

    // State
    CrossAccountTransferManager* m_transferManager;
    QString m_selectedTransferId;

    // Cache for item widgets (to update progress)
    QMap<QString, QWidget*> m_itemWidgets;
    QMap<QString, QProgressBar*> m_progressBars;
};

/**
 * @brief Widget for a single transfer item in the list
 */
class TransferLogItemWidget : public QFrame
{
    Q_OBJECT

public:
    explicit TransferLogItemWidget(const CrossAccountTransfer& transfer, QWidget* parent = nullptr);

    QString transferId() const { return m_transferId; }
    void updateProgress(int percent, qint64 bytesTransferred, qint64 bytesTotal);
    void updateStatus(CrossAccountTransfer::Status status, const QString& errorMessage = QString());

signals:
    void retryClicked(const QString& transferId);
    void cancelClicked(const QString& transferId);

private:
    void setupUI(const CrossAccountTransfer& transfer);
    QString formatBytes(qint64 bytes) const;
    QString getStatusIconPath(CrossAccountTransfer::Status status) const;
    QString getStatusColor(CrossAccountTransfer::Status status) const;

    QString m_transferId;
    CrossAccountTransfer::Status m_status;

    QLabel* m_statusIcon;
    QLabel* m_timeLabel;
    QLabel* m_fileLabel;
    QLabel* m_accountsLabel;
    QLabel* m_pathLabel;
    QLabel* m_errorLabel;
    QProgressBar* m_progressBar;
    QLabel* m_progressLabel;
    QPushButton* m_retryBtn;
    QPushButton* m_cancelBtn;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_CROSSACCOUNTLOGPANEL_H
