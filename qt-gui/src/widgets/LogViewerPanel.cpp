#include "LogViewerPanel.h"
#include "core/LogManager.h"
#include "utils/MemberRegistry.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QCheckBox>
#include <QSplitter>
#include <QTextEdit>
#include <QMenu>
#include <QtConcurrent/QtConcurrent>

namespace MegaCustom {

LogViewerPanel::LogViewerPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();

    // Setup auto-refresh timer
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(5000); // 5 seconds
    connect(m_refreshTimer, &QTimer::timeout, this, &LogViewerPanel::refresh);

    // Setup async watchers
    m_activityWatcher = new QFutureWatcher<std::vector<LogEntry>>(this);
    connect(m_activityWatcher, &QFutureWatcher<std::vector<LogEntry>>::finished,
            this, &LogViewerPanel::onActivityLogLoaded);

    m_distributionWatcher = new QFutureWatcher<std::vector<DistributionRecord>>(this);
    connect(m_distributionWatcher, &QFutureWatcher<std::vector<DistributionRecord>>::finished,
            this, &LogViewerPanel::onDistributionHistoryLoaded);

    // Delay initial load to avoid calling LogManager during startup
    QTimer::singleShot(500, this, &LogViewerPanel::refresh);
}

void LogViewerPanel::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // Title
    QLabel* titleLabel = new QLabel("Activity Logs");
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #e0e0e0;");
    mainLayout->addWidget(titleLabel);

    // Description
    QLabel* descLabel = new QLabel("View activity logs, errors, and distribution history for all operations.");
    descLabel->setStyleSheet("color: #888; margin-bottom: 8px;");
    descLabel->setWordWrap(true);
    mainLayout->addWidget(descLabel);

    // Tab widget
    m_tabWidget = new QTabWidget();
    m_tabWidget->setStyleSheet(R"(
        QTabWidget::pane {
            border: 1px solid #444;
            border-radius: 4px;
            background-color: #1e1e1e;
        }
        QTabBar::tab {
            background-color: #2a2a2a;
            color: #888;
            padding: 8px 16px;
            border: 1px solid #444;
            border-bottom: none;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
        }
        QTabBar::tab:selected {
            background-color: #1e1e1e;
            color: #e0e0e0;
        }
    )");
    connect(m_tabWidget, &QTabWidget::currentChanged, this, &LogViewerPanel::onTabChanged);

    // =====================================================
    // Activity Log Tab
    // =====================================================
    QWidget* activityTab = new QWidget();
    QVBoxLayout* activityLayout = new QVBoxLayout(activityTab);
    activityLayout->setContentsMargins(8, 8, 8, 8);
    activityLayout->setSpacing(8);

    // Filter row
    QHBoxLayout* filterLayout = new QHBoxLayout();
    filterLayout->setSpacing(8);

    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("Search logs...");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setMinimumWidth(200);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &LogViewerPanel::onSearchChanged);
    filterLayout->addWidget(m_searchEdit);

    QLabel* levelLabel = new QLabel("Level:");
    filterLayout->addWidget(levelLabel);
    m_levelCombo = new QComboBox();
    m_levelCombo->addItem("All Levels", -1);
    m_levelCombo->addItem("Debug", 0);
    m_levelCombo->addItem("Info", 1);
    m_levelCombo->addItem("Warning", 2);
    m_levelCombo->addItem("Error", 3);
    m_levelCombo->setCurrentIndex(0);
    connect(m_levelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LogViewerPanel::onLevelFilterChanged);
    filterLayout->addWidget(m_levelCombo);

    QLabel* catLabel = new QLabel("Category:");
    filterLayout->addWidget(catLabel);
    m_categoryCombo = new QComboBox();
    m_categoryCombo->addItem("All Categories", -1);
    m_categoryCombo->addItem("General", 0);
    m_categoryCombo->addItem("Auth", 1);
    m_categoryCombo->addItem("Upload", 2);
    m_categoryCombo->addItem("Download", 3);
    m_categoryCombo->addItem("Sync", 4);
    m_categoryCombo->addItem("Watermark", 5);
    m_categoryCombo->addItem("Distribution", 6);
    m_categoryCombo->addItem("Member", 7);
    m_categoryCombo->addItem("WordPress", 8);
    m_categoryCombo->addItem("Folder", 9);
    m_categoryCombo->addItem("System", 10);
    m_categoryCombo->setCurrentIndex(0);
    connect(m_categoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LogViewerPanel::onCategoryFilterChanged);
    filterLayout->addWidget(m_categoryCombo);

    filterLayout->addStretch();
    activityLayout->addLayout(filterLayout);

    // Date range filter row
    QHBoxLayout* dateFilterLayout = new QHBoxLayout();
    dateFilterLayout->setSpacing(8);

    m_dateFilterCheck = new QCheckBox("Date Range:");
    m_dateFilterCheck->setChecked(false);
    connect(m_dateFilterCheck, &QCheckBox::toggled, this, &LogViewerPanel::onDateRangeChanged);
    dateFilterLayout->addWidget(m_dateFilterCheck);

    dateFilterLayout->addWidget(new QLabel("From:"));
    m_fromDateEdit = new QDateTimeEdit();
    m_fromDateEdit->setDisplayFormat("yyyy-MM-dd hh:mm");
    m_fromDateEdit->setCalendarPopup(true);
    m_fromDateEdit->setDateTime(QDateTime::currentDateTime().addDays(-7));  // Default: 1 week ago
    m_fromDateEdit->setEnabled(false);  // Disabled until checkbox is checked
    connect(m_fromDateEdit, &QDateTimeEdit::dateTimeChanged, this, &LogViewerPanel::onDateRangeChanged);
    dateFilterLayout->addWidget(m_fromDateEdit);

    dateFilterLayout->addWidget(new QLabel("To:"));
    m_toDateEdit = new QDateTimeEdit();
    m_toDateEdit->setDisplayFormat("yyyy-MM-dd hh:mm");
    m_toDateEdit->setCalendarPopup(true);
    m_toDateEdit->setDateTime(QDateTime::currentDateTime());  // Default: now
    m_toDateEdit->setEnabled(false);  // Disabled until checkbox is checked
    connect(m_toDateEdit, &QDateTimeEdit::dateTimeChanged, this, &LogViewerPanel::onDateRangeChanged);
    dateFilterLayout->addWidget(m_toDateEdit);

    dateFilterLayout->addStretch();
    activityLayout->addLayout(dateFilterLayout);

    // Activity table
    m_activityTable = new QTableWidget();
    m_activityTable->setColumnCount(6);
    m_activityTable->setHorizontalHeaderLabels({
        "Time", "Level", "Category", "Action", "Message", "Details"
    });
    m_activityTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_activityTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_activityTable->setAlternatingRowColors(true);
    m_activityTable->verticalHeader()->setVisible(false);
    m_activityTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // Column sizing
    m_activityTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);     // Time
    m_activityTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);     // Level
    m_activityTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);     // Category
    m_activityTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive); // Action
    m_activityTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);   // Message
    m_activityTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Interactive); // Details
    m_activityTable->setColumnWidth(0, 140);
    m_activityTable->setColumnWidth(1, 70);
    m_activityTable->setColumnWidth(2, 90);
    m_activityTable->setColumnWidth(3, 120);
    m_activityTable->setColumnWidth(5, 150);

    m_activityTable->setStyleSheet(R"(
        QTableWidget {
            background-color: #1e1e1e;
            border: 1px solid #444;
            border-radius: 4px;
            gridline-color: #333;
        }
        QTableWidget::item {
            padding: 4px;
        }
        QTableWidget::item:selected {
            background-color: #0d6efd;
        }
        QHeaderView::section {
            background-color: #2a2a2a;
            color: #e0e0e0;
            padding: 6px;
            border: none;
            border-bottom: 1px solid #444;
        }
    )");

    connect(m_activityTable, &QTableWidget::itemSelectionChanged,
            this, &LogViewerPanel::onActivityTableSelectionChanged);
    activityLayout->addWidget(m_activityTable, 1);

    m_tabWidget->addTab(activityTab, "Activity Log");

    // =====================================================
    // Distribution History Tab
    // =====================================================
    QWidget* distributionTab = new QWidget();
    QVBoxLayout* distLayout = new QVBoxLayout(distributionTab);
    distLayout->setContentsMargins(8, 8, 8, 8);
    distLayout->setSpacing(8);

    // Distribution filter row
    QHBoxLayout* distFilterLayout = new QHBoxLayout();
    distFilterLayout->setSpacing(8);

    distFilterLayout->addWidget(new QLabel("Member:"));
    m_memberFilterCombo = new QComboBox();
    m_memberFilterCombo->addItem("All Members", "");
    // Populate with members from registry
    MemberRegistry* registry = MemberRegistry::instance();
    if (registry) {
        for (const MemberInfo& m : registry->getAllMembers()) {
            m_memberFilterCombo->addItem(m.displayName, m.id);
        }
    }
    connect(m_memberFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LogViewerPanel::onMemberFilterChanged);
    distFilterLayout->addWidget(m_memberFilterCombo);

    distFilterLayout->addWidget(new QLabel("Status:"));
    m_statusFilterCombo = new QComboBox();
    m_statusFilterCombo->addItem("All", -1);
    m_statusFilterCombo->addItem("Pending", 0);
    m_statusFilterCombo->addItem("Watermarking", 1);
    m_statusFilterCombo->addItem("Uploading", 2);
    m_statusFilterCombo->addItem("Completed", 3);
    m_statusFilterCombo->addItem("Failed", 4);
    connect(m_statusFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LogViewerPanel::onStatusFilterChanged);
    distFilterLayout->addWidget(m_statusFilterCombo);

    distFilterLayout->addStretch();
    distLayout->addLayout(distFilterLayout);

    // Distribution table
    m_distributionTable = new QTableWidget();
    m_distributionTable->setColumnCount(8);
    m_distributionTable->setHorizontalHeaderLabels({
        "Time", "Member", "Source File", "Destination", "Status", "Size", "WM Time", "Upload Time"
    });
    m_distributionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_distributionTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_distributionTable->setAlternatingRowColors(true);
    m_distributionTable->verticalHeader()->setVisible(false);
    m_distributionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // Column sizing
    m_distributionTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);     // Time
    m_distributionTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive); // Member
    m_distributionTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);   // Source
    m_distributionTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive); // Dest
    m_distributionTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);     // Status
    m_distributionTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);     // Size
    m_distributionTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Fixed);     // WM Time
    m_distributionTable->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Fixed);     // Upload
    m_distributionTable->setColumnWidth(0, 140);
    m_distributionTable->setColumnWidth(1, 100);
    m_distributionTable->setColumnWidth(3, 150);
    m_distributionTable->setColumnWidth(4, 90);
    m_distributionTable->setColumnWidth(5, 80);
    m_distributionTable->setColumnWidth(6, 80);
    m_distributionTable->setColumnWidth(7, 80);

    m_distributionTable->setStyleSheet(m_activityTable->styleSheet());

    connect(m_distributionTable, &QTableWidget::itemSelectionChanged,
            this, &LogViewerPanel::onDistributionTableSelectionChanged);
    distLayout->addWidget(m_distributionTable, 1);

    m_tabWidget->addTab(distributionTab, "Distribution History");

    mainLayout->addWidget(m_tabWidget, 1);

    // =====================================================
    // Bottom bar - Actions and Stats
    // =====================================================
    QHBoxLayout* bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(12);

    m_autoRefreshCheck = new QCheckBox("Auto-refresh");
    m_autoRefreshCheck->setChecked(false);
    connect(m_autoRefreshCheck, &QCheckBox::toggled, this, &LogViewerPanel::onAutoRefreshToggled);
    bottomLayout->addWidget(m_autoRefreshCheck);

    m_refreshBtn = new QPushButton("Refresh");
    m_refreshBtn->setIcon(QIcon(":/icons/refresh-cw.svg"));
    connect(m_refreshBtn, &QPushButton::clicked, this, &LogViewerPanel::onRefreshClicked);
    bottomLayout->addWidget(m_refreshBtn);

    m_exportBtn = new QPushButton("Export");
    m_exportBtn->setIcon(QIcon(":/icons/download.svg"));
    connect(m_exportBtn, &QPushButton::clicked, this, &LogViewerPanel::onExportClicked);
    bottomLayout->addWidget(m_exportBtn);

    m_clearBtn = new QPushButton("Clear Logs");
    m_clearBtn->setIcon(QIcon(":/icons/trash-2.svg"));
    connect(m_clearBtn, &QPushButton::clicked, this, &LogViewerPanel::onClearClicked);
    bottomLayout->addWidget(m_clearBtn);

    // Loading indicator
    m_loadingLabel = new QLabel();
    m_loadingLabel->setStyleSheet("color: #60a5fa; font-weight: bold;");
    m_loadingLabel->setVisible(false);
    bottomLayout->addWidget(m_loadingLabel);

    bottomLayout->addStretch();

    // Last refreshed timestamp
    m_lastRefreshedLabel = new QLabel();
    m_lastRefreshedLabel->setStyleSheet("color: #666; font-size: 11px;");
    bottomLayout->addWidget(m_lastRefreshedLabel);

    m_countLabel = new QLabel();
    m_countLabel->setStyleSheet("color: #888;");
    bottomLayout->addWidget(m_countLabel);

    mainLayout->addLayout(bottomLayout);

    // Stats bar
    m_statsLabel = new QLabel();
    m_statsLabel->setStyleSheet("color: #888; padding-top: 4px; border-top: 1px solid #333;");
    mainLayout->addWidget(m_statsLabel);
}

void LogViewerPanel::refresh() {
    // Guard against calls before widgets are fully initialized
    if (!m_activityTable || !m_distributionTable || !m_statsLabel) return;

    // Don't start new refresh if already loading
    if (m_isLoading) return;

    setLoadingState(true);
    refreshActivityLog();
    refreshDistributionHistory();
    refreshStats();
}

void LogViewerPanel::refreshActivityLog() {
    if (!m_activityWatcher) {
        populateActivityTable();  // Fallback to sync if watcher not ready
        return;
    }

    // Capture filter values for the async task
    int levelFilter = m_levelFilter;
    int categoryFilter = m_categoryFilter;
    QString searchText = m_searchText;

    // Capture date filter values
    bool dateFilterEnabled = m_dateFilterCheck ? m_dateFilterCheck->isChecked() : false;
    qint64 startTime = 0;
    qint64 endTime = 0;
    if (dateFilterEnabled && m_fromDateEdit && m_toDateEdit) {
        startTime = m_fromDateEdit->dateTime().toMSecsSinceEpoch();
        endTime = m_toDateEdit->dateTime().toMSecsSinceEpoch();
    }

    // Run the log retrieval in a background thread
    QFuture<std::vector<LogEntry>> future = QtConcurrent::run([levelFilter, categoryFilter, searchText, dateFilterEnabled, startTime, endTime]() {
        LogManager& logMgr = LogManager::instance();
        LogFilter filter;
        filter.limit = 500;

        if (levelFilter >= 0) {
            filter.minLevel = static_cast<LogLevel>(levelFilter);
        }
        if (categoryFilter >= 0) {
            filter.categories.push_back(static_cast<LogCategory>(categoryFilter));
        }
        if (!searchText.isEmpty()) {
            filter.searchText = searchText.toStdString();
        }
        if (dateFilterEnabled && startTime > 0) {
            filter.startTime = startTime;
            filter.endTime = endTime;
        }

        return logMgr.getEntries(filter);
    });

    m_activityWatcher->setFuture(future);
}

void LogViewerPanel::refreshDistributionHistory() {
    if (!m_distributionWatcher || !m_memberFilterCombo) {
        populateDistributionTable();  // Fallback to sync if not ready
        return;
    }

    // Capture filter values for the async task
    QString memberFilter = m_memberFilterCombo->currentData().toString();

    // Run the history retrieval in a background thread
    QFuture<std::vector<DistributionRecord>> future = QtConcurrent::run([memberFilter]() {
        LogManager& logMgr = LogManager::instance();
        return logMgr.getDistributionHistory(memberFilter.toStdString(), 500);
    });

    m_distributionWatcher->setFuture(future);
}

void LogViewerPanel::refreshStats() {
    updateStatsDisplay();
}

void LogViewerPanel::onActivityLogLoaded() {
    if (!m_activityWatcher || !m_activityTable) return;

    std::vector<LogEntry> entries = m_activityWatcher->result();
    populateActivityTableFromEntries(entries);

    // Check if distribution loading is also done
    if (!m_distributionWatcher->isRunning()) {
        setLoadingState(false);
        updateLastRefreshedLabel();
    }
}

void LogViewerPanel::onDistributionHistoryLoaded() {
    if (!m_distributionWatcher || !m_distributionTable) return;

    std::vector<DistributionRecord> records = m_distributionWatcher->result();
    populateDistributionTableFromRecords(records);

    // Check if activity loading is also done
    if (!m_activityWatcher->isRunning()) {
        setLoadingState(false);
        updateLastRefreshedLabel();
    }
}

void LogViewerPanel::populateActivityTable() {
    if (!m_activityTable) return;  // Guard against early calls

    LogManager& logMgr = LogManager::instance();

    // Build filter
    LogFilter filter;
    filter.limit = 500;

    if (m_levelFilter >= 0) {
        filter.minLevel = static_cast<LogLevel>(m_levelFilter);
    }

    if (m_categoryFilter >= 0) {
        filter.categories.push_back(static_cast<LogCategory>(m_categoryFilter));
    }

    if (!m_searchText.isEmpty()) {
        filter.searchText = m_searchText.toStdString();
    }

    std::vector<LogEntry> entries = logMgr.getEntries(filter);
    populateActivityTableFromEntries(entries);
}

void LogViewerPanel::populateActivityTableFromEntries(const std::vector<LogEntry>& entries) {
    if (!m_activityTable) return;
    m_activityTable->setRowCount(0);

    // Show empty state if no entries
    if (entries.empty()) {
        showEmptyState(m_activityTable, "No log entries found.\nTry adjusting your filters or wait for activity.");
        if (m_countLabel) m_countLabel->setText("Showing 0 entries");
        return;
    }

    m_activityTable->setRowCount(entries.size());

    for (int row = 0; row < static_cast<int>(entries.size()); ++row) {
        const LogEntry& entry = entries[row];

        // Time
        QTableWidgetItem* timeItem = new QTableWidgetItem(formatTimestamp(entry.timestamp));
        m_activityTable->setItem(row, 0, timeItem);

        // Level
        QString levelStr = QString::fromStdString(LogManager::levelToString(entry.level));
        QTableWidgetItem* levelItem = new QTableWidgetItem(levelStr);
        levelItem->setForeground(getLevelColor(static_cast<int>(entry.level)));
        m_activityTable->setItem(row, 1, levelItem);

        // Category
        QString catStr = QString::fromStdString(LogManager::categoryToString(entry.category));
        QTableWidgetItem* catItem = new QTableWidgetItem(catStr);
        m_activityTable->setItem(row, 2, catItem);

        // Action
        QTableWidgetItem* actionItem = new QTableWidgetItem(QString::fromStdString(entry.action));
        m_activityTable->setItem(row, 3, actionItem);

        // Message
        QTableWidgetItem* msgItem = new QTableWidgetItem(QString::fromStdString(entry.message));
        m_activityTable->setItem(row, 4, msgItem);

        // Details (truncated)
        QString details = QString::fromStdString(entry.details);
        if (details.length() > 50) {
            details = details.left(47) + "...";
        }
        QTableWidgetItem* detailsItem = new QTableWidgetItem(details);
        detailsItem->setToolTip(QString::fromStdString(entry.details));
        m_activityTable->setItem(row, 5, detailsItem);
    }

    if (m_countLabel) m_countLabel->setText(QString("Showing %1 entries").arg(entries.size()));
}

void LogViewerPanel::populateDistributionTable() {
    // Guard against early calls before widgets are fully initialized
    if (!m_distributionTable || !m_memberFilterCombo || !m_statusFilterCombo) return;

    LogManager& logMgr = LogManager::instance();

    QString memberFilter = m_memberFilterCombo->currentData().toString();
    std::vector<DistributionRecord> records = logMgr.getDistributionHistory(
        memberFilter.toStdString(), 500);

    populateDistributionTableFromRecords(records);
}

void LogViewerPanel::populateDistributionTableFromRecords(const std::vector<DistributionRecord>& records) {
    if (!m_distributionTable) return;
    m_distributionTable->setRowCount(0);

    // Apply status filter
    int statusFilter = m_statusFilterCombo ? m_statusFilterCombo->currentData().toInt() : -1;

    int visibleCount = 0;
    for (const DistributionRecord& record : records) {
        if (statusFilter >= 0 && static_cast<int>(record.status) != statusFilter) {
            continue;
        }

        int row = m_distributionTable->rowCount();
        m_distributionTable->insertRow(row);

        // Time
        QTableWidgetItem* timeItem = new QTableWidgetItem(formatTimestamp(record.timestamp));
        m_distributionTable->setItem(row, 0, timeItem);

        // Member
        QTableWidgetItem* memberItem = new QTableWidgetItem(QString::fromStdString(record.memberName));
        m_distributionTable->setItem(row, 1, memberItem);

        // Source file (just filename)
        QString sourcePath = QString::fromStdString(record.sourceFile);
        QString fileName = sourcePath.mid(sourcePath.lastIndexOf('/') + 1);
        QTableWidgetItem* sourceItem = new QTableWidgetItem(fileName);
        sourceItem->setToolTip(sourcePath);
        m_distributionTable->setItem(row, 2, sourceItem);

        // Destination
        QTableWidgetItem* destItem = new QTableWidgetItem(QString::fromStdString(record.megaFolder));
        m_distributionTable->setItem(row, 3, destItem);

        // Status
        QString statusStr;
        switch (record.status) {
            case DistributionRecord::Status::Pending: statusStr = "Pending"; break;
            case DistributionRecord::Status::Watermarking: statusStr = "Watermarking"; break;
            case DistributionRecord::Status::Uploading: statusStr = "Uploading"; break;
            case DistributionRecord::Status::Completed: statusStr = "Completed"; break;
            case DistributionRecord::Status::Failed: statusStr = "Failed"; break;
        }
        QTableWidgetItem* statusItem = new QTableWidgetItem(statusStr);
        statusItem->setForeground(getStatusColor(static_cast<int>(record.status)));
        if (!record.errorMessage.empty()) {
            statusItem->setToolTip(QString::fromStdString(record.errorMessage));
        }
        m_distributionTable->setItem(row, 4, statusItem);

        // Size
        QTableWidgetItem* sizeItem = new QTableWidgetItem(formatFileSize(record.fileSizeBytes));
        m_distributionTable->setItem(row, 5, sizeItem);

        // Watermark time
        QTableWidgetItem* wmTimeItem = new QTableWidgetItem(formatDuration(record.watermarkTimeMs));
        m_distributionTable->setItem(row, 6, wmTimeItem);

        // Upload time
        QTableWidgetItem* uploadTimeItem = new QTableWidgetItem(formatDuration(record.uploadTimeMs));
        m_distributionTable->setItem(row, 7, uploadTimeItem);

        visibleCount++;
    }

    // Show empty state if no visible records
    if (visibleCount == 0) {
        m_distributionTable->setRowCount(0);
        showEmptyState(m_distributionTable, "No distribution history found.\nDistributed files will appear here.");
    }

    if (m_tabWidget && m_tabWidget->currentIndex() == 1 && m_countLabel) {
        m_countLabel->setText(QString("Showing %1 distributions").arg(visibleCount));
    }
}

void LogViewerPanel::updateStatsDisplay() {
    LogManager& logMgr = LogManager::instance();
    LogStats stats = logMgr.getStats();

    QString statsText = QString(
        "Total: %1 entries | Errors: %2 | Warnings: %3 | "
        "Distributions: %4 total (%5 successful, %6 failed)")
        .arg(stats.totalEntries)
        .arg(stats.errorCount)
        .arg(stats.warningCount)
        .arg(stats.totalDistributions)
        .arg(stats.successfulDistributions)
        .arg(stats.failedDistributions);

    if (m_statsLabel) m_statsLabel->setText(statsText);
}

void LogViewerPanel::setLoadingState(bool loading) {
    m_isLoading = loading;

    if (m_loadingLabel) {
        m_loadingLabel->setText(loading ? "Loading..." : "");
        m_loadingLabel->setVisible(loading);
    }

    if (m_refreshBtn) {
        m_refreshBtn->setEnabled(!loading);
    }
}

void LogViewerPanel::showEmptyState(QTableWidget* table, const QString& message) {
    if (!table) return;

    table->setRowCount(1);
    table->setSpan(0, 0, 1, table->columnCount());

    QTableWidgetItem* item = new QTableWidgetItem(message);
    item->setTextAlignment(Qt::AlignCenter);
    item->setForeground(QColor("#666"));
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);  // Make non-selectable
    table->setItem(0, 0, item);

    // Set minimum row height for the empty state
    table->setRowHeight(0, 80);
}

void LogViewerPanel::updateLastRefreshedLabel() {
    if (!m_lastRefreshedLabel) return;

    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    m_lastRefreshedLabel->setText(QString("Last refreshed: %1").arg(timestamp));
}

void LogViewerPanel::onSearchChanged(const QString& text) {
    if (!m_activityTable) return;  // Guard against early calls
    m_searchText = text;
    refreshActivityLog();
}

void LogViewerPanel::onLevelFilterChanged(int index) {
    Q_UNUSED(index);
    if (!m_levelCombo || !m_activityTable) return;  // Guard against early calls
    m_levelFilter = m_levelCombo->currentData().toInt();
    refreshActivityLog();
}

void LogViewerPanel::onCategoryFilterChanged(int index) {
    Q_UNUSED(index);
    if (!m_categoryCombo || !m_activityTable) return;  // Guard against early calls
    m_categoryFilter = m_categoryCombo->currentData().toInt();
    refreshActivityLog();
}

void LogViewerPanel::onMemberFilterChanged(int index) {
    Q_UNUSED(index);
    if (!m_memberFilterCombo || !m_distributionTable) return;  // Guard against early calls
    refreshDistributionHistory();
}

void LogViewerPanel::onStatusFilterChanged(int index) {
    Q_UNUSED(index);
    if (!m_statusFilterCombo || !m_distributionTable) return;  // Guard against early calls
    refreshDistributionHistory();
}

void LogViewerPanel::onDateRangeChanged() {
    // Guard against early calls
    if (!m_dateFilterCheck || !m_fromDateEdit || !m_toDateEdit || !m_activityTable) return;

    // Enable/disable date edit widgets based on checkbox
    bool enabled = m_dateFilterCheck->isChecked();
    m_fromDateEdit->setEnabled(enabled);
    m_toDateEdit->setEnabled(enabled);

    // Validate date range (from should be before to)
    if (enabled && m_fromDateEdit->dateTime() > m_toDateEdit->dateTime()) {
        // Swap if invalid
        QDateTime temp = m_fromDateEdit->dateTime();
        m_fromDateEdit->setDateTime(m_toDateEdit->dateTime());
        m_toDateEdit->setDateTime(temp);
    }

    // Refresh the activity log with new date filter
    refreshActivityLog();
}

void LogViewerPanel::onRefreshClicked() {
    refresh();
}

void LogViewerPanel::onExportClicked() {
    QString filePath = QFileDialog::getSaveFileName(this,
        "Export Logs",
        "megacustom_logs.txt",
        "Text Files (*.txt);;JSON Files (*.json);;All Files (*)");

    if (filePath.isEmpty()) return;

    LogManager& logMgr = LogManager::instance();

    LogFilter filter;
    filter.limit = 10000;
    if (m_levelFilter >= 0) {
        filter.minLevel = static_cast<LogLevel>(m_levelFilter);
    }
    if (m_categoryFilter >= 0) {
        filter.categories.push_back(static_cast<LogCategory>(m_categoryFilter));
    }

    if (logMgr.exportLogs(filePath.toStdString(), filter)) {
        QMessageBox::information(this, "Export Complete",
            QString("Logs exported to:\n%1").arg(filePath));
    } else {
        QMessageBox::warning(this, "Export Failed",
            "Failed to export logs to file.");
    }
}

void LogViewerPanel::onClearClicked() {
    int ret = QMessageBox::question(this, "Clear Logs",
        "Are you sure you want to clear all logs?\n\n"
        "This will delete activity logs and distribution history.\n"
        "This action cannot be undone.",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (ret == QMessageBox::Yes) {
        LogManager::instance().clearAll();
        refresh();
        QMessageBox::information(this, "Cleared", "All logs have been cleared.");
    }
}

void LogViewerPanel::onAutoRefreshToggled(bool enabled) {
    if (!m_refreshTimer) return;  // Guard against early calls
    if (enabled) {
        m_refreshTimer->start();
    } else {
        m_refreshTimer->stop();
    }
}

void LogViewerPanel::onActivityTableSelectionChanged() {
    if (!m_activityTable) return;  // Guard against early calls
    int row = m_activityTable->currentRow();
    if (row < 0) return;

    QTableWidgetItem* detailsItem = m_activityTable->item(row, 5);
    if (detailsItem) {
        emit logEntrySelected(detailsItem->toolTip());
    }
}

void LogViewerPanel::onDistributionTableSelectionChanged() {
    if (!m_distributionTable) return;  // Guard against early calls
    int row = m_distributionTable->currentRow();
    if (row < 0) return;

    QTableWidgetItem* statusItem = m_distributionTable->item(row, 4);
    if (statusItem && !statusItem->toolTip().isEmpty()) {
        emit logEntrySelected(statusItem->toolTip());
    }
}

void LogViewerPanel::onTabChanged(int index) {
    // Guard against early calls before widgets are fully initialized
    if (!m_countLabel || !m_activityTable || !m_distributionTable) return;

    if (index == 0) {
        m_countLabel->setText(QString("Showing %1 entries").arg(m_activityTable->rowCount()));
    } else {
        m_countLabel->setText(QString("Showing %1 distributions").arg(m_distributionTable->rowCount()));
    }
}

QString LogViewerPanel::formatTimestamp(qint64 timestamp) const {
    QDateTime dt = QDateTime::fromMSecsSinceEpoch(timestamp);
    return dt.toString("yyyy-MM-dd hh:mm:ss");
}

QString LogViewerPanel::formatFileSize(qint64 bytes) const {
    if (bytes <= 0) return "-";
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024);
    if (bytes < 1024 * 1024 * 1024) return QString("%1 MB").arg(bytes / (1024 * 1024));
    return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
}

QString LogViewerPanel::formatDuration(qint64 ms) const {
    if (ms <= 0) return "-";
    if (ms < 1000) return QString("%1 ms").arg(ms);
    if (ms < 60000) return QString("%1 s").arg(ms / 1000.0, 0, 'f', 1);
    return QString("%1 min").arg(ms / 60000.0, 0, 'f', 1);
}

QColor LogViewerPanel::getLevelColor(int level) const {
    switch (level) {
        case 0: return QColor("#888");      // Debug - gray
        case 1: return QColor("#e0e0e0");   // Info - white
        case 2: return QColor("#fbbf24");   // Warning - yellow
        case 3: return QColor("#f87171");   // Error - red
        default: return QColor("#e0e0e0");
    }
}

QColor LogViewerPanel::getStatusColor(int status) const {
    switch (status) {
        case 0: return QColor("#888");      // Pending - gray
        case 1: return QColor("#60a5fa");   // Watermarking - blue
        case 2: return QColor("#818cf8");   // Uploading - purple
        case 3: return QColor("#4ade80");   // Completed - green
        case 4: return QColor("#f87171");   // Failed - red
        default: return QColor("#e0e0e0");
    }
}

} // namespace MegaCustom
