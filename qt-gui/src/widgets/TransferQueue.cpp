#include "TransferQueue.h"
#include "controllers/TransferController.h"
#include "utils/Constants.h"
#include "utils/DpiScaler.h"
#include "styles/ThemeManager.h"
#include <QHeaderView>
#include <QFileInfo>
#include <QDebug>
#include <QBrush>

namespace MegaCustom {

// Column indices
enum TransferColumns {
    COL_TYPE = 0,
    COL_FILENAME,
    COL_SIZE,
    COL_PROGRESS,
    COL_SPEED,
    COL_ETA,
    COL_STATUS,
    COL_COUNT
};

TransferQueue::TransferQueue(QWidget* parent)
    : QWidget(parent)
    , m_controller(nullptr) {

    setupUi();
    qDebug() << "TransferQueue constructed (with real progress tracking)";
}

void TransferQueue::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Header with title and badges
    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(12);

    // Title
    m_titleLabel = new QLabel("Transfers", this);
    m_titleLabel->setStyleSheet(QString("font-size: %1px; font-weight: bold; color: %2;")
        .arg(DpiScaler::scale(16))
        .arg(ThemeManager::instance().textPrimary().name()));
    headerLayout->addWidget(m_titleLabel);

    // Status badges
    m_activeBadge = createBadge("0 Active", Constants::Colors::TRANSFER_ACTIVE);
    m_pendingBadge = createBadge("0 Pending", Constants::Colors::TRANSFER_PENDING);
    m_completedBadge = createBadge("0 Completed", Constants::Colors::TRANSFER_COMPLETED);

    headerLayout->addWidget(m_activeBadge);
    headerLayout->addWidget(m_pendingBadge);
    headerLayout->addWidget(m_completedBadge);
    headerLayout->addStretch();

    // Action buttons
    m_cancelAllButton = new QPushButton("Cancel All", this);
    m_cancelAllButton->setObjectName("TransferActionButton");
    m_cancelAllButton->setEnabled(false);
    connect(m_cancelAllButton, &QPushButton::clicked, this, &TransferQueue::onCancelAllClicked);
    headerLayout->addWidget(m_cancelAllButton);

    m_clearCompletedButton = new QPushButton("Clear Completed", this);
    m_clearCompletedButton->setObjectName("TransferActionButton");
    m_clearCompletedButton->setEnabled(false);
    connect(m_clearCompletedButton, &QPushButton::clicked, this, &TransferQueue::onClearCompletedClicked);
    headerLayout->addWidget(m_clearCompletedButton);

    mainLayout->addLayout(headerLayout);

    // Transfer table
    m_transferTable = new QTableWidget(this);
    m_transferTable->setColumnCount(COL_COUNT);
    m_transferTable->setHorizontalHeaderLabels({
        "Type", "File", "Size", "Progress", "Speed", "ETA", "Status"
    });

    // Configure table
    m_transferTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_transferTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_transferTable->setAlternatingRowColors(true);
    m_transferTable->verticalHeader()->setVisible(false);
    m_transferTable->horizontalHeader()->setStretchLastSection(true);
    m_transferTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // Set column widths
    m_transferTable->setColumnWidth(COL_TYPE, 80);
    m_transferTable->setColumnWidth(COL_FILENAME, 200);
    m_transferTable->setColumnWidth(COL_SIZE, 80);
    m_transferTable->setColumnWidth(COL_PROGRESS, 150);
    m_transferTable->setColumnWidth(COL_SPEED, 100);
    m_transferTable->setColumnWidth(COL_ETA, 80);
    m_transferTable->setColumnWidth(COL_STATUS, 100);

    mainLayout->addWidget(m_transferTable);
    setLayout(mainLayout);
}

void TransferQueue::setTransferController(TransferController* controller) {
    // Disconnect existing connections to avoid duplicates if called multiple times
    if (m_controller) {
        disconnect(m_controller, nullptr, this, nullptr);
    }

    m_controller = controller;

    if (m_controller) {
        // Connect to controller signals
        connect(m_controller, &TransferController::addTransfer,
                this, &TransferQueue::onTransferAdded);
        connect(m_controller, &TransferController::transferProgress,
                this, &TransferQueue::onTransferProgress);
        connect(m_controller, &TransferController::transferComplete,
                this, &TransferQueue::onTransferComplete);
        connect(m_controller, &TransferController::transferFailed,
                this, &TransferQueue::onTransferFailed);
        connect(m_controller, &TransferController::queueStatusChanged,
                this, &TransferQueue::onQueueStatusChanged);

        qDebug() << "TransferQueue: controller connected";
    }
}

void TransferQueue::onTransferAdded(const QString& type, const QString& sourcePath,
                                    const QString& destPath, qint64 size) {
    int row = m_transferTable->rowCount();
    m_transferTable->insertRow(row);

    // Get filename from path
    QFileInfo fileInfo(sourcePath);
    QString fileName = fileInfo.fileName();

    // Type icon/text
    QTableWidgetItem* typeItem = new QTableWidgetItem(type == "upload" ? "Upload" : "Download");
    typeItem->setIcon(QIcon::fromTheme(type == "upload" ? "go-up" : "go-down"));
    m_transferTable->setItem(row, COL_TYPE, typeItem);

    // Filename
    QTableWidgetItem* fileItem = new QTableWidgetItem(fileName);
    fileItem->setToolTip(sourcePath);
    fileItem->setData(Qt::UserRole, sourcePath);  // Store full path
    m_transferTable->setItem(row, COL_FILENAME, fileItem);

    // Size
    QTableWidgetItem* sizeItem = new QTableWidgetItem(formatSize(size));
    sizeItem->setData(Qt::UserRole, size);
    m_transferTable->setItem(row, COL_SIZE, sizeItem);

    // Progress bar
    QProgressBar* progressBar = new QProgressBar();
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setTextVisible(true);
    progressBar->setFormat("%p%");
    m_transferTable->setCellWidget(row, COL_PROGRESS, progressBar);

    // Speed
    m_transferTable->setItem(row, COL_SPEED, new QTableWidgetItem("--"));

    // ETA
    m_transferTable->setItem(row, COL_ETA, new QTableWidgetItem("--"));

    // Status
    QTableWidgetItem* statusItem = new QTableWidgetItem("Starting...");
    statusItem->setForeground(Qt::blue);
    m_transferTable->setItem(row, COL_STATUS, statusItem);

    // Track this transfer
    // Note: We don't have the transfer ID here yet, so we'll match by path
    m_transferRows[sourcePath] = row;

    m_cancelAllButton->setEnabled(true);

    qDebug() << "Transfer added to queue:" << fileName;
}

void TransferQueue::onTransferProgress(const QString& transferId, qint64 transferred,
                                       qint64 total, qint64 speed, int timeRemaining) {
    // Find the row for this transfer
    int row = findRowByTransferId(transferId);
    if (row < 0) {
        // Try to find by matching existing rows
        for (int r = 0; r < m_transferTable->rowCount(); ++r) {
            QTableWidgetItem* statusItem = m_transferTable->item(r, COL_STATUS);
            if (statusItem && (statusItem->text() == "Starting..." ||
                              statusItem->text() == "Transferring...")) {
                row = r;
                break;
            }
        }
    }

    if (row < 0 || row >= m_transferTable->rowCount()) {
        return;
    }

    // Update progress bar with smooth animation
    QProgressBar* progressBar = qobject_cast<QProgressBar*>(
        m_transferTable->cellWidget(row, COL_PROGRESS));
    if (progressBar && total > 0) {
        int percent = static_cast<int>((transferred * 100) / total);
        animateProgressBar(progressBar, percent);
    }

    // Update speed
    QTableWidgetItem* speedItem = m_transferTable->item(row, COL_SPEED);
    if (speedItem) {
        speedItem->setText(formatSpeed(speed));
    }

    // Update ETA
    QTableWidgetItem* etaItem = m_transferTable->item(row, COL_ETA);
    if (etaItem) {
        etaItem->setText(formatTime(timeRemaining));
    }

    // Update status
    QTableWidgetItem* statusItem = m_transferTable->item(row, COL_STATUS);
    if (statusItem) {
        statusItem->setText("Uploading");
        statusItem->setForeground(ThemeManager::instance().brandDefault());
    }

    // Highlight active row with brand tinted background
    QColor activeRowBg = ThemeManager::instance().brandDefault();
    activeRowBg.setAlpha(30);  // Light tint of brand color
    for (int col = 0; col < m_transferTable->columnCount(); ++col) {
        QTableWidgetItem* item = m_transferTable->item(row, col);
        if (item) {
            item->setBackground(activeRowBg);
        }
    }
}

void TransferQueue::onTransferComplete(const QString& transferId) {
    int row = findRowByTransferId(transferId);
    if (row < 0) {
        // Find by status
        for (int r = 0; r < m_transferTable->rowCount(); ++r) {
            QTableWidgetItem* statusItem = m_transferTable->item(r, COL_STATUS);
            if (statusItem && statusItem->text() == "Transferring...") {
                row = r;
                break;
            }
        }
    }

    if (row < 0 || row >= m_transferTable->rowCount()) {
        return;
    }

    // Update progress to 100%
    QProgressBar* progressBar = qobject_cast<QProgressBar*>(
        m_transferTable->cellWidget(row, COL_PROGRESS));
    if (progressBar) {
        progressBar->setValue(100);
        progressBar->setStyleSheet(QString("QProgressBar::chunk { background-color: %1; }")
            .arg(ThemeManager::instance().supportSuccess().name()));
    }

    // Clear speed and ETA
    if (auto* item = m_transferTable->item(row, COL_SPEED)) {
        item->setText("--");
    }
    if (auto* item = m_transferTable->item(row, COL_ETA)) {
        item->setText("--");
    }

    // Update status
    QTableWidgetItem* statusItem = m_transferTable->item(row, COL_STATUS);
    if (statusItem) {
        statusItem->setText("Completed");
        statusItem->setForeground(ThemeManager::instance().supportSuccess());
    }

    // Clear row highlighting
    for (int col = 0; col < m_transferTable->columnCount(); ++col) {
        QTableWidgetItem* item = m_transferTable->item(row, col);
        if (item) {
            item->setBackground(QBrush());  // Reset to default
        }
    }

    m_clearCompletedButton->setEnabled(true);

    qDebug() << "Transfer completed:" << transferId;
}

void TransferQueue::onTransferFailed(const QString& path, const QString& error) {
    int row = findRowByPath(path);
    if (row < 0) {
        // Find by status
        for (int r = 0; r < m_transferTable->rowCount(); ++r) {
            QTableWidgetItem* statusItem = m_transferTable->item(r, COL_STATUS);
            if (statusItem && (statusItem->text() == "Starting..." ||
                              statusItem->text() == "Transferring...")) {
                row = r;
                break;
            }
        }
    }

    if (row < 0 || row >= m_transferTable->rowCount()) {
        return;
    }

    // Update progress bar style
    QProgressBar* progressBar = qobject_cast<QProgressBar*>(
        m_transferTable->cellWidget(row, COL_PROGRESS));
    if (progressBar) {
        progressBar->setStyleSheet(QString("QProgressBar::chunk { background-color: %1; }")
            .arg(ThemeManager::instance().supportError().name()));
    }

    // Update status
    QTableWidgetItem* statusItem = m_transferTable->item(row, COL_STATUS);
    if (statusItem) {
        statusItem->setText("Failed");
        statusItem->setForeground(ThemeManager::instance().supportError());
        statusItem->setToolTip(error);
    }

    // Clear row highlighting
    for (int col = 0; col < m_transferTable->columnCount(); ++col) {
        QTableWidgetItem* item = m_transferTable->item(row, col);
        if (item) {
            item->setBackground(QBrush());  // Reset to default
        }
    }

    m_clearCompletedButton->setEnabled(true);

    qDebug() << "Transfer failed:" << path << "-" << error;
}

void TransferQueue::onQueueStatusChanged(int active, int pending, int completed, int failed) {
    m_activeCount = active;
    m_pendingCount = pending;
    m_completedCount = completed;
    m_failedCount = failed;

    updateStatusLabel();

    // Enable/disable buttons
    m_cancelAllButton->setEnabled(active > 0);
    m_clearCompletedButton->setEnabled(completed > 0 || failed > 0);
}

void TransferQueue::onCancelAllClicked() {
    if (m_controller) {
        m_controller->cancelAllTransfers();

        // Mark all active transfers as cancelled
        for (int row = 0; row < m_transferTable->rowCount(); ++row) {
            QTableWidgetItem* statusItem = m_transferTable->item(row, COL_STATUS);
            if (statusItem && (statusItem->text() == "Starting..." ||
                              statusItem->text() == "Transferring...")) {
                statusItem->setText("Cancelled");
                statusItem->setForeground(Qt::gray);
            }
        }
    }
}

void TransferQueue::onClearCompletedClicked() {
    // Remove completed and failed transfers from table
    for (int row = m_transferTable->rowCount() - 1; row >= 0; --row) {
        QTableWidgetItem* statusItem = m_transferTable->item(row, COL_STATUS);
        if (statusItem) {
            QString status = statusItem->text();
            if (status == "Completed" || status == "Failed" || status == "Cancelled") {
                m_transferTable->removeRow(row);
            }
        }
    }

    // Rebuild the transfer rows map
    m_transferRows.clear();
    for (int row = 0; row < m_transferTable->rowCount(); ++row) {
        QTableWidgetItem* fileItem = m_transferTable->item(row, COL_FILENAME);
        if (fileItem) {
            QString path = fileItem->data(Qt::UserRole).toString();
            m_transferRows[path] = row;
        }
    }

    m_completedCount = 0;
    m_failedCount = 0;
    updateStatusLabel();

    m_clearCompletedButton->setEnabled(false);
}

void TransferQueue::updateStatusLabel() {
    // Update badge counts
    updateBadge(m_activeBadge, m_activeCount, "Active");
    updateBadge(m_pendingBadge, m_pendingCount, "Pending");
    updateBadge(m_completedBadge, m_completedCount + m_failedCount, "Completed");

    // Show/hide badges based on counts
    m_activeBadge->setVisible(m_activeCount > 0);
    m_pendingBadge->setVisible(m_pendingCount > 0);
    m_completedBadge->setVisible(m_completedCount > 0 || m_failedCount > 0);
}

QLabel* TransferQueue::createBadge(const QString& text, const QString& color) {
    QLabel* badge = new QLabel(text, this);
    badge->setStyleSheet(QString(
        "QLabel {"
        "  background-color: %1;"
        "  color: white;"
        "  border-radius: 10px;"
        "  padding: 4px 12px;"
        "  font-size: 12px;"
        "  font-weight: bold;"
        "}"
    ).arg(color));
    badge->setVisible(false);
    return badge;
}

void TransferQueue::updateBadge(QLabel* badge, int count, const QString& label) {
    if (badge) {
        badge->setText(QString("%1 %2").arg(count).arg(label));
    }
}

QString TransferQueue::formatSize(qint64 bytes) {
    if (bytes < 1024) {
        return QString("%1 B").arg(bytes);
    } else if (bytes < 1024 * 1024) {
        return QString("%1 KB").arg(bytes / 1024);
    } else if (bytes < 1024 * 1024 * 1024) {
        return QString("%1 MB").arg(bytes / (1024 * 1024));
    } else {
        return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
    }
}

QString TransferQueue::formatSpeed(qint64 bytesPerSecond) {
    if (bytesPerSecond < 1024) {
        return QString("%1 B/s").arg(bytesPerSecond);
    } else if (bytesPerSecond < 1024 * 1024) {
        return QString("%1 KB/s").arg(bytesPerSecond / 1024);
    } else {
        return QString("%1 MB/s").arg(bytesPerSecond / (1024.0 * 1024.0), 0, 'f', 1);
    }
}

QString TransferQueue::formatTime(int seconds) {
    if (seconds < 0 || seconds > 86400) {
        return "--";
    } else if (seconds < 60) {
        return QString("%1s").arg(seconds);
    } else if (seconds < 3600) {
        return QString("%1m %2s").arg(seconds / 60).arg(seconds % 60);
    } else {
        return QString("%1h %2m").arg(seconds / 3600).arg((seconds % 3600) / 60);
    }
}

int TransferQueue::findRowByTransferId(const QString& transferId) {
    // Since we don't store transfer IDs directly, return -1
    // The caller will need to match by other criteria
    Q_UNUSED(transferId);
    return -1;
}

int TransferQueue::findRowByPath(const QString& path) {
    for (int row = 0; row < m_transferTable->rowCount(); ++row) {
        QTableWidgetItem* fileItem = m_transferTable->item(row, COL_FILENAME);
        if (fileItem) {
            QString rowPath = fileItem->data(Qt::UserRole).toString();
            if (rowPath == path || path.endsWith(fileItem->text())) {
                return row;
            }
        }
    }
    return -1;
}

void TransferQueue::animateProgressBar(QProgressBar* progressBar, int targetValue) {
    if (!progressBar) return;

    // Don't animate if we're already at the target
    if (progressBar->value() == targetValue) return;

    // Check if there's an existing animation for this progress bar
    QPropertyAnimation* existingAnim = m_progressAnimations.value(progressBar, nullptr);

    // If there's an existing animation and it's running, update its end value
    if (existingAnim && existingAnim->state() == QAbstractAnimation::Running) {
        existingAnim->stop();
        existingAnim->setStartValue(progressBar->value());
        existingAnim->setEndValue(targetValue);
        existingAnim->start();
        return;
    }

    // Create new animation
    QPropertyAnimation* anim = new QPropertyAnimation(progressBar, "value", this);
    anim->setDuration(150);  // 150ms for smooth but responsive animation
    anim->setStartValue(progressBar->value());
    anim->setEndValue(targetValue);
    anim->setEasingCurve(QEasingCurve::OutQuad);

    // Store the animation
    m_progressAnimations[progressBar] = anim;

    // Clean up when animation finishes
    connect(anim, &QPropertyAnimation::finished, this, [this, progressBar, anim]() {
        if (m_progressAnimations.value(progressBar) == anim) {
            m_progressAnimations.remove(progressBar);
        }
        anim->deleteLater();
    });

    anim->start();
}

} // namespace MegaCustom

#include "moc_TransferQueue.cpp"
