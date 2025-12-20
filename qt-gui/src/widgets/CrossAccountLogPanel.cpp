#include "CrossAccountLogPanel.h"
#include "accounts/CrossAccountTransferManager.h"
#include "accounts/TransferLogStore.h"
#include "accounts/AccountManager.h"
#include "utils/DpiScaler.h"
#include <QMessageBox>
#include <QFileInfo>
#include <QDebug>

namespace MegaCustom {

// ============================================================================
// CrossAccountLogPanel
// ============================================================================

CrossAccountLogPanel::CrossAccountLogPanel(QWidget* parent)
    : QWidget(parent)
    , m_titleLabel(nullptr)
    , m_countLabel(nullptr)
    , m_statusFilter(nullptr)
    , m_fromDate(nullptr)
    , m_toDate(nullptr)
    , m_searchEdit(nullptr)
    , m_refreshBtn(nullptr)
    , m_clearBtn(nullptr)
    , m_transferList(nullptr)
    , m_retryBtn(nullptr)
    , m_cancelBtn(nullptr)
    , m_statusLabel(nullptr)
    , m_transferManager(nullptr)
{
    setupUI();
    connectSignals();
}

void CrossAccountLogPanel::setupUI()
{
    setObjectName("CrossAccountLogPanel");

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(DpiScaler::scale(16), DpiScaler::scale(16),
                                   DpiScaler::scale(16), DpiScaler::scale(16));
    mainLayout->setSpacing(DpiScaler::scale(12));

    // Header
    QHBoxLayout* headerLayout = new QHBoxLayout();

    m_titleLabel = new QLabel("Cross-Account Transfers", this);
    m_titleLabel->setObjectName("PanelTitle");
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    headerLayout->addWidget(m_titleLabel);

    m_countLabel = new QLabel("0 transfers", this);
    m_countLabel->setStyleSheet("color: #888;");
    headerLayout->addWidget(m_countLabel);

    headerLayout->addStretch();

    m_refreshBtn = new QPushButton("Refresh", this);
    m_refreshBtn->setIcon(QIcon(":/icons/refresh-cw.svg"));
    headerLayout->addWidget(m_refreshBtn);

    m_clearBtn = new QPushButton("Clear Log", this);
    m_clearBtn->setToolTip("Clear completed transfers from history");
    headerLayout->addWidget(m_clearBtn);

    mainLayout->addLayout(headerLayout);

    // Filters row
    QHBoxLayout* filterLayout = new QHBoxLayout();
    filterLayout->setSpacing(DpiScaler::scale(8));

    QLabel* filterLabel = new QLabel("Filter:", this);
    filterLayout->addWidget(filterLabel);

    m_statusFilter = new QComboBox(this);
    m_statusFilter->addItem("All", -1);
    m_statusFilter->addItem("Active", static_cast<int>(CrossAccountTransfer::InProgress));
    m_statusFilter->addItem("Completed", static_cast<int>(CrossAccountTransfer::Completed));
    m_statusFilter->addItem("Failed", static_cast<int>(CrossAccountTransfer::Failed));
    m_statusFilter->addItem("Cancelled", static_cast<int>(CrossAccountTransfer::Cancelled));
    m_statusFilter->setFixedWidth(DpiScaler::scale(100));
    filterLayout->addWidget(m_statusFilter);

    filterLayout->addWidget(new QLabel("From:", this));
    m_fromDate = new QDateEdit(this);
    m_fromDate->setCalendarPopup(true);
    m_fromDate->setDate(QDate::currentDate().addDays(-7));
    m_fromDate->setFixedWidth(DpiScaler::scale(110));
    filterLayout->addWidget(m_fromDate);

    filterLayout->addWidget(new QLabel("To:", this));
    m_toDate = new QDateEdit(this);
    m_toDate->setCalendarPopup(true);
    m_toDate->setDate(QDate::currentDate());
    m_toDate->setFixedWidth(DpiScaler::scale(110));
    filterLayout->addWidget(m_toDate);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Search paths...");
    m_searchEdit->setClearButtonEnabled(true);
    filterLayout->addWidget(m_searchEdit, 1);

    mainLayout->addLayout(filterLayout);

    // Transfer list
    m_transferList = new QListWidget(this);
    m_transferList->setObjectName("TransferLogList");
    m_transferList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_transferList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_transferList->setSpacing(DpiScaler::scale(4));
    mainLayout->addWidget(m_transferList, 1);

    // Action buttons
    QHBoxLayout* actionLayout = new QHBoxLayout();

    m_retryBtn = new QPushButton("Retry Selected", this);
    m_retryBtn->setIcon(QIcon(":/icons/refresh-cw.svg"));
    m_retryBtn->setEnabled(false);
    actionLayout->addWidget(m_retryBtn);

    m_cancelBtn = new QPushButton("Cancel Selected", this);
    m_cancelBtn->setIcon(QIcon(":/icons/x.svg"));
    m_cancelBtn->setEnabled(false);
    actionLayout->addWidget(m_cancelBtn);

    actionLayout->addStretch();

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color: #888;");
    actionLayout->addWidget(m_statusLabel);

    mainLayout->addLayout(actionLayout);
}

void CrossAccountLogPanel::connectSignals()
{
    connect(m_statusFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CrossAccountLogPanel::onStatusFilterChanged);
    connect(m_fromDate, &QDateEdit::dateChanged,
            this, &CrossAccountLogPanel::onDateFilterChanged);
    connect(m_toDate, &QDateEdit::dateChanged,
            this, &CrossAccountLogPanel::onDateFilterChanged);
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &CrossAccountLogPanel::onSearchChanged);
    connect(m_refreshBtn, &QPushButton::clicked,
            this, &CrossAccountLogPanel::onRefreshClicked);
    connect(m_clearBtn, &QPushButton::clicked,
            this, &CrossAccountLogPanel::onClearLogClicked);
    connect(m_transferList, &QListWidget::itemClicked,
            this, &CrossAccountLogPanel::onTransferItemClicked);
    connect(m_retryBtn, &QPushButton::clicked,
            this, &CrossAccountLogPanel::onRetryClicked);
    connect(m_cancelBtn, &QPushButton::clicked,
            this, &CrossAccountLogPanel::onCancelClicked);
}

void CrossAccountLogPanel::setTransferManager(CrossAccountTransferManager* manager)
{
    if (m_transferManager) {
        disconnect(m_transferManager, nullptr, this, nullptr);
    }

    m_transferManager = manager;

    if (m_transferManager) {
        connect(m_transferManager, &CrossAccountTransferManager::transferStarted,
                this, &CrossAccountLogPanel::onTransferStarted);
        connect(m_transferManager, &CrossAccountTransferManager::transferProgress,
                this, &CrossAccountLogPanel::onTransferProgress);
        connect(m_transferManager, &CrossAccountTransferManager::transferCompleted,
                this, &CrossAccountLogPanel::onTransferCompleted);
        connect(m_transferManager, &CrossAccountTransferManager::transferFailed,
                this, &CrossAccountLogPanel::onTransferFailed);
        connect(m_transferManager, &CrossAccountTransferManager::transferCancelled,
                this, &CrossAccountLogPanel::onTransferCancelled);
    }

    refresh();
}

void CrossAccountLogPanel::refresh()
{
    populateList();
    updateStatusCounts();
}

void CrossAccountLogPanel::populateList()
{
    m_transferList->clear();
    m_itemWidgets.clear();
    m_progressBars.clear();

    if (!m_transferManager) {
        return;
    }

    // Get filters
    int statusValue = m_statusFilter->currentData().toInt();
    QDateTime fromDt(m_fromDate->date(), QTime(0, 0, 0));
    QDateTime toDt(m_toDate->date(), QTime(23, 59, 59));
    QString search = m_searchEdit->text().trimmed();

    // Get transfers
    QList<CrossAccountTransfer> transfers = m_transferManager->getHistory(200);

    // Apply filters
    for (const CrossAccountTransfer& transfer : transfers) {
        // Status filter
        if (statusValue >= 0 && static_cast<int>(transfer.status) != statusValue) {
            continue;
        }

        // Date filter
        if (transfer.timestamp < fromDt || transfer.timestamp > toDt) {
            continue;
        }

        // Search filter
        if (!search.isEmpty()) {
            bool matches = transfer.sourcePath.contains(search, Qt::CaseInsensitive) ||
                           transfer.targetPath.contains(search, Qt::CaseInsensitive);
            if (!matches) {
                continue;
            }
        }

        // Create item
        QListWidgetItem* item = new QListWidgetItem(m_transferList);
        item->setData(Qt::UserRole, transfer.id);

        TransferLogItemWidget* widget = new TransferLogItemWidget(transfer, nullptr);
        item->setSizeHint(widget->sizeHint());
        m_transferList->setItemWidget(item, widget);

        m_itemWidgets[transfer.id] = widget;

        // Connect signals
        connect(widget, &TransferLogItemWidget::retryClicked, this, [this](const QString& id) {
            m_selectedTransferId = id;
            onRetryClicked();
        });
        connect(widget, &TransferLogItemWidget::cancelClicked, this, [this](const QString& id) {
            m_selectedTransferId = id;
            onCancelClicked();
        });
    }

    m_countLabel->setText(QString("%1 transfers").arg(m_transferList->count()));
}

void CrossAccountLogPanel::updateStatusCounts()
{
    if (!m_transferManager) {
        m_statusLabel->clear();
        return;
    }

    int active = m_transferManager->activeTransferCount();
    if (active > 0) {
        m_statusLabel->setText(QString("%1 active transfer(s)").arg(active));
    } else {
        m_statusLabel->setText("No active transfers");
    }
}

QWidget* CrossAccountLogPanel::createTransferItemWidget(const CrossAccountTransfer& transfer)
{
    return new TransferLogItemWidget(transfer, nullptr);
}

QString CrossAccountLogPanel::formatBytes(qint64 bytes) const
{
    const qint64 KB = 1024;
    const qint64 MB = KB * 1024;
    const qint64 GB = MB * 1024;

    if (bytes >= GB) {
        return QString::number(bytes / static_cast<double>(GB), 'f', 1) + " GB";
    } else if (bytes >= MB) {
        return QString::number(bytes / static_cast<double>(MB), 'f', 1) + " MB";
    } else if (bytes >= KB) {
        return QString::number(bytes / static_cast<double>(KB), 'f', 0) + " KB";
    }
    return QString::number(bytes) + " B";
}

QString CrossAccountLogPanel::formatDuration(qint64 seconds) const
{
    if (seconds < 60) {
        return QString("%1s").arg(seconds);
    } else if (seconds < 3600) {
        return QString("%1m %2s").arg(seconds / 60).arg(seconds % 60);
    }
    return QString("%1h %2m").arg(seconds / 3600).arg((seconds % 3600) / 60);
}

QString CrossAccountLogPanel::getStatusText(CrossAccountTransfer::Status status) const
{
    switch (status) {
    case CrossAccountTransfer::Pending: return "Pending";
    case CrossAccountTransfer::InProgress: return "In Progress";
    case CrossAccountTransfer::Completed: return "Completed";
    case CrossAccountTransfer::Failed: return "Failed";
    case CrossAccountTransfer::Cancelled: return "Cancelled";
    }
    return "Unknown";
}

QString CrossAccountLogPanel::getStatusColor(CrossAccountTransfer::Status status) const
{
    switch (status) {
    case CrossAccountTransfer::Pending: return "#888888";
    case CrossAccountTransfer::InProgress: return "#2196F3";
    case CrossAccountTransfer::Completed: return "#4CAF50";
    case CrossAccountTransfer::Failed: return "#EF4444";
    case CrossAccountTransfer::Cancelled: return "#888888";
    }
    return "#888888";
}

QString CrossAccountLogPanel::getAccountEmail(const QString& accountId) const
{
    MegaAccount account = AccountManager::instance().getAccount(accountId);
    return account.email.isEmpty() ? accountId : account.email;
}

// Slots
void CrossAccountLogPanel::onStatusFilterChanged(int index)
{
    Q_UNUSED(index)
    populateList();
}

void CrossAccountLogPanel::onDateFilterChanged()
{
    populateList();
}

void CrossAccountLogPanel::onSearchChanged(const QString& text)
{
    Q_UNUSED(text)
    populateList();
}

void CrossAccountLogPanel::onClearLogClicked()
{
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Clear Log",
        "Clear all completed transfers from the log?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        // Would call m_logStore->clearCompleted() if we had access
        // For now, refresh will show updated list
        refresh();
    }
}

void CrossAccountLogPanel::onRefreshClicked()
{
    refresh();
}

void CrossAccountLogPanel::onTransferItemClicked(QListWidgetItem* item)
{
    if (!item) {
        m_selectedTransferId.clear();
        m_retryBtn->setEnabled(false);
        m_cancelBtn->setEnabled(false);
        return;
    }

    m_selectedTransferId = item->data(Qt::UserRole).toString();

    // Get transfer status to enable appropriate buttons
    QList<CrossAccountTransfer> transfers = m_transferManager->getHistory(200);
    for (const CrossAccountTransfer& t : transfers) {
        if (t.id == m_selectedTransferId) {
            m_retryBtn->setEnabled(t.status == CrossAccountTransfer::Failed && t.canRetry);
            m_cancelBtn->setEnabled(t.status == CrossAccountTransfer::InProgress ||
                                    t.status == CrossAccountTransfer::Pending);
            break;
        }
    }
}

void CrossAccountLogPanel::onRetryClicked()
{
    if (m_selectedTransferId.isEmpty() || !m_transferManager) {
        return;
    }

    QString newId = m_transferManager->retryTransfer(m_selectedTransferId);
    if (!newId.isEmpty()) {
        refresh();
    }
}

void CrossAccountLogPanel::onCancelClicked()
{
    if (m_selectedTransferId.isEmpty() || !m_transferManager) {
        return;
    }

    m_transferManager->cancelTransfer(m_selectedTransferId);
    refresh();
}

void CrossAccountLogPanel::onTransferStarted(const CrossAccountTransfer& transfer)
{
    Q_UNUSED(transfer)
    refresh();
}

void CrossAccountLogPanel::onTransferProgress(const QString& transferId, int percent,
                                               qint64 bytesTransferred, qint64 bytesTotal)
{
    if (m_itemWidgets.contains(transferId)) {
        TransferLogItemWidget* widget = qobject_cast<TransferLogItemWidget*>(m_itemWidgets[transferId]);
        if (widget) {
            widget->updateProgress(percent, bytesTransferred, bytesTotal);
        }
    }
}

void CrossAccountLogPanel::onTransferCompleted(const CrossAccountTransfer& transfer)
{
    if (m_itemWidgets.contains(transfer.id)) {
        TransferLogItemWidget* widget = qobject_cast<TransferLogItemWidget*>(m_itemWidgets[transfer.id]);
        if (widget) {
            widget->updateStatus(CrossAccountTransfer::Completed);
        }
    }
    updateStatusCounts();
}

void CrossAccountLogPanel::onTransferFailed(const CrossAccountTransfer& transfer)
{
    if (m_itemWidgets.contains(transfer.id)) {
        TransferLogItemWidget* widget = qobject_cast<TransferLogItemWidget*>(m_itemWidgets[transfer.id]);
        if (widget) {
            widget->updateStatus(CrossAccountTransfer::Failed, transfer.errorMessage);
        }
    }
    updateStatusCounts();
}

void CrossAccountLogPanel::onTransferCancelled(const QString& transferId)
{
    if (m_itemWidgets.contains(transferId)) {
        TransferLogItemWidget* widget = qobject_cast<TransferLogItemWidget*>(m_itemWidgets[transferId]);
        if (widget) {
            widget->updateStatus(CrossAccountTransfer::Cancelled);
        }
    }
    updateStatusCounts();
}

// ============================================================================
// TransferLogItemWidget
// ============================================================================

TransferLogItemWidget::TransferLogItemWidget(const CrossAccountTransfer& transfer, QWidget* parent)
    : QFrame(parent)
    , m_transferId(transfer.id)
    , m_status(transfer.status)
    , m_statusIcon(nullptr)
    , m_timeLabel(nullptr)
    , m_fileLabel(nullptr)
    , m_accountsLabel(nullptr)
    , m_pathLabel(nullptr)
    , m_errorLabel(nullptr)
    , m_progressBar(nullptr)
    , m_progressLabel(nullptr)
    , m_retryBtn(nullptr)
    , m_cancelBtn(nullptr)
{
    setupUI(transfer);
}

void TransferLogItemWidget::setupUI(const CrossAccountTransfer& transfer)
{
    setObjectName("TransferLogItem");
    setFrameShape(QFrame::StyledPanel);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(DpiScaler::scale(12), DpiScaler::scale(8),
                                   DpiScaler::scale(12), DpiScaler::scale(8));
    mainLayout->setSpacing(DpiScaler::scale(4));

    // Top row: status icon, time, file name, action buttons
    QHBoxLayout* topRow = new QHBoxLayout();
    topRow->setSpacing(DpiScaler::scale(8));

    m_statusIcon = new QLabel(this);
    m_statusIcon->setPixmap(QIcon(getStatusIconPath(transfer.status)).pixmap(DpiScaler::scale(16), DpiScaler::scale(16)));
    m_statusIcon->setFixedSize(DpiScaler::scale(20), DpiScaler::scale(20));
    m_statusIcon->setAlignment(Qt::AlignCenter);
    topRow->addWidget(m_statusIcon);

    m_timeLabel = new QLabel(transfer.timestamp.toString("HH:mm"), this);
    m_timeLabel->setStyleSheet("color: #888;");
    m_timeLabel->setFixedWidth(DpiScaler::scale(40));
    topRow->addWidget(m_timeLabel);

    // Extract filename from path
    QStringList paths = transfer.sourcePath.split(";", Qt::SkipEmptyParts);
    QString fileName = paths.isEmpty() ? "Unknown" : QFileInfo(paths.first()).fileName();
    if (paths.size() > 1) {
        fileName += QString(" (+%1 more)").arg(paths.size() - 1);
    }

    m_fileLabel = new QLabel(fileName, this);
    m_fileLabel->setObjectName("TransferFileName");
    QFont fileFont = m_fileLabel->font();
    fileFont.setBold(true);
    m_fileLabel->setFont(fileFont);
    topRow->addWidget(m_fileLabel, 1);

    // Action buttons
    m_retryBtn = new QPushButton("Retry", this);
    m_retryBtn->setObjectName("RetryButton");
    m_retryBtn->setFixedWidth(DpiScaler::scale(60));
    m_retryBtn->setVisible(transfer.status == CrossAccountTransfer::Failed && transfer.canRetry);
    connect(m_retryBtn, &QPushButton::clicked, [this]() { emit retryClicked(m_transferId); });
    topRow->addWidget(m_retryBtn);

    m_cancelBtn = new QPushButton("Cancel", this);
    m_cancelBtn->setObjectName("CancelButton");
    m_cancelBtn->setFixedWidth(DpiScaler::scale(60));
    m_cancelBtn->setVisible(transfer.status == CrossAccountTransfer::InProgress ||
                           transfer.status == CrossAccountTransfer::Pending);
    connect(m_cancelBtn, &QPushButton::clicked, [this]() { emit cancelClicked(m_transferId); });
    topRow->addWidget(m_cancelBtn);

    mainLayout->addLayout(topRow);

    // Second row: accounts
    QString sourceEmail = AccountManager::instance().getAccount(transfer.sourceAccountId).email;
    QString targetEmail = AccountManager::instance().getAccount(transfer.targetAccountId).email;
    if (sourceEmail.isEmpty()) sourceEmail = transfer.sourceAccountId;
    if (targetEmail.isEmpty()) targetEmail = transfer.targetAccountId;

    QString opSymbol = (transfer.operation == CrossAccountTransfer::Move) ? " -> " : " => ";
    m_accountsLabel = new QLabel(sourceEmail + opSymbol + targetEmail, this);
    m_accountsLabel->setStyleSheet("color: #666;");
    mainLayout->addWidget(m_accountsLabel);

    // Third row: paths
    m_pathLabel = new QLabel(this);
    m_pathLabel->setText(QString("%1 -> %2").arg(transfer.sourcePath).arg(transfer.targetPath));
    m_pathLabel->setStyleSheet("color: #888; font-size: 10px;");
    m_pathLabel->setWordWrap(true);
    mainLayout->addWidget(m_pathLabel);

    // Progress row (for in-progress transfers)
    if (transfer.status == CrossAccountTransfer::InProgress) {
        QHBoxLayout* progressRow = new QHBoxLayout();

        m_progressBar = new QProgressBar(this);
        m_progressBar->setMinimum(0);
        m_progressBar->setMaximum(100);
        m_progressBar->setValue(0);
        if (transfer.bytesTotal > 0) {
            int percent = static_cast<int>((transfer.bytesTransferred * 100) / transfer.bytesTotal);
            m_progressBar->setValue(percent);
        }
        m_progressBar->setTextVisible(false);
        m_progressBar->setFixedHeight(DpiScaler::scale(6));
        progressRow->addWidget(m_progressBar, 1);

        m_progressLabel = new QLabel(this);
        if (transfer.bytesTotal > 0) {
            m_progressLabel->setText(QString("%1 / %2")
                .arg(formatBytes(transfer.bytesTransferred))
                .arg(formatBytes(transfer.bytesTotal)));
        } else {
            m_progressLabel->setText("Calculating...");
        }
        m_progressLabel->setStyleSheet("color: #888;");
        progressRow->addWidget(m_progressLabel);

        mainLayout->addLayout(progressRow);
    }

    // Error row (for failed transfers)
    if (transfer.status == CrossAccountTransfer::Failed && !transfer.errorMessage.isEmpty()) {
        m_errorLabel = new QLabel("Error: " + transfer.errorMessage, this);
        m_errorLabel->setStyleSheet("color: #EF4444;");
        m_errorLabel->setWordWrap(true);
        mainLayout->addWidget(m_errorLabel);
    }

    // Set frame style based on status
    QString borderColor = getStatusColor(transfer.status);
    setStyleSheet(QString("TransferLogItemWidget { border-left: 3px solid %1; }").arg(borderColor));
}

void TransferLogItemWidget::updateProgress(int percent, qint64 bytesTransferred, qint64 bytesTotal)
{
    if (m_progressBar) {
        m_progressBar->setValue(percent);
    }
    if (m_progressLabel) {
        m_progressLabel->setText(QString("%1 / %2 (%3%)")
            .arg(formatBytes(bytesTransferred))
            .arg(formatBytes(bytesTotal))
            .arg(percent));
    }
}

void TransferLogItemWidget::updateStatus(CrossAccountTransfer::Status status, const QString& errorMessage)
{
    m_status = status;

    if (m_statusIcon) {
        m_statusIcon->setPixmap(QIcon(getStatusIconPath(status)).pixmap(DpiScaler::scale(16), DpiScaler::scale(16)));
    }

    // Update button visibility
    if (m_retryBtn) {
        m_retryBtn->setVisible(status == CrossAccountTransfer::Failed);
    }
    if (m_cancelBtn) {
        m_cancelBtn->setVisible(status == CrossAccountTransfer::InProgress ||
                               status == CrossAccountTransfer::Pending);
    }

    // Hide progress for completed/failed
    if (m_progressBar && (status == CrossAccountTransfer::Completed ||
                          status == CrossAccountTransfer::Failed ||
                          status == CrossAccountTransfer::Cancelled)) {
        m_progressBar->hide();
        if (m_progressLabel) m_progressLabel->hide();
    }

    // Show error message
    if (!errorMessage.isEmpty() && m_errorLabel) {
        m_errorLabel->setText("Error: " + errorMessage);
        m_errorLabel->show();
    }

    // Update border color
    QString borderColor = getStatusColor(status);
    setStyleSheet(QString("TransferLogItemWidget { border-left: 3px solid %1; }").arg(borderColor));
}

QString TransferLogItemWidget::formatBytes(qint64 bytes) const
{
    const qint64 KB = 1024;
    const qint64 MB = KB * 1024;
    const qint64 GB = MB * 1024;

    if (bytes >= GB) {
        return QString::number(bytes / static_cast<double>(GB), 'f', 1) + " GB";
    } else if (bytes >= MB) {
        return QString::number(bytes / static_cast<double>(MB), 'f', 1) + " MB";
    } else if (bytes >= KB) {
        return QString::number(bytes / static_cast<double>(KB), 'f', 0) + " KB";
    }
    return QString::number(bytes) + " B";
}

QString TransferLogItemWidget::getStatusIconPath(CrossAccountTransfer::Status status) const
{
    switch (status) {
    case CrossAccountTransfer::Pending: return ":/icons/clock.svg";
    case CrossAccountTransfer::InProgress: return ":/icons/play.svg";
    case CrossAccountTransfer::Completed: return ":/icons/check.svg";
    case CrossAccountTransfer::Failed: return ":/icons/x.svg";
    case CrossAccountTransfer::Cancelled: return ":/icons/stop.svg";
    }
    return ":/icons/info.svg";
}

QString TransferLogItemWidget::getStatusColor(CrossAccountTransfer::Status status) const
{
    switch (status) {
    case CrossAccountTransfer::Pending: return "#888888";
    case CrossAccountTransfer::InProgress: return "#2196F3";
    case CrossAccountTransfer::Completed: return "#4CAF50";
    case CrossAccountTransfer::Failed: return "#EF4444";
    case CrossAccountTransfer::Cancelled: return "#888888";
    }
    return "#888888";
}

} // namespace MegaCustom
