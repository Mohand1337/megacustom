#include "LogViewerPanel.h"
#include "EmptyStateWidget.h"
#include "core/LogManager.h"
#include "utils/MemberRegistry.h"
#include "utils/CopyHelper.h"
#include "styles/ThemeManager.h"
#include <QApplication>
#include <QClipboard>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QCheckBox>
#include <QScrollArea>
#include <QSplitter>
#include <QTextEdit>
#include <QMenu>
#include <QTextStream>
#include <QTime>
#include <QtConcurrent/QtConcurrent>

namespace MegaCustom {

LogViewerPanel::LogViewerPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("LogViewerPanel");
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
    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    QWidget* contentWidget = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(contentWidget);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(16);

    // Title
    QLabel* titleLabel = new QLabel("Activity Logs");
    titleLabel->setObjectName("PanelTitle");
    mainLayout->addWidget(titleLabel);

    // Description
    QLabel* descLabel = new QLabel("View activity logs, errors, and distribution history for all operations.");
    descLabel->setObjectName("PanelSubtitle");
    descLabel->setWordWrap(true);
    mainLayout->addWidget(descLabel);

    // Empty state (shown when both tables are empty)
    m_emptyState = new EmptyStateWidget(
        ":/icons/clipboard.svg",
        "No activity logged",
        "Activity and distribution logs will appear here as you use the app.",
        QString(),
        this);
    mainLayout->addWidget(m_emptyState);

    // Tab widget
    m_tabWidget = new QTabWidget();
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
    m_searchEdit->setToolTip("Search action, message, details, member ID, file path, job ID, level, or category.");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setMinimumWidth(200);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &LogViewerPanel::onSearchChanged);
    filterLayout->addWidget(m_searchEdit);

    QLabel* levelLabel = new QLabel("Level:");
    filterLayout->addWidget(levelLabel);
    m_levelCombo = new QComboBox();
    m_levelCombo->setToolTip("Show entries at this severity or higher.");
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
    m_categoryCombo->setToolTip("Limit activity logs to one operation category.");
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

    QLabel* quickLabel = new QLabel("Quick:");
    filterLayout->addWidget(quickLabel);
    m_quickFilterCombo = new QComboBox();
    m_quickFilterCombo->setToolTip("Apply a common operational filter without changing the saved level/category choices.");
    m_quickFilterCombo->addItem("All Activity", "all");
    m_quickFilterCombo->addItem("Needs Attention", "issues");
    m_quickFilterCombo->addItem("Errors Only", "errors");
    m_quickFilterCombo->addItem("Today", "today");
    m_quickFilterCombo->addItem("Debug/System", "debug_system");
    connect(m_quickFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LogViewerPanel::onQuickFilterChanged);
    filterLayout->addWidget(m_quickFilterCombo);

    filterLayout->addStretch();
    activityLayout->addLayout(filterLayout);

    // Date range filter row
    QHBoxLayout* dateFilterLayout = new QHBoxLayout();
    dateFilterLayout->setSpacing(8);

    m_dateFilterCheck = new QCheckBox("Date Range:");
    m_dateFilterCheck->setToolTip("Limit activity logs to entries inside the selected time range.");
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
    m_activityTable->setObjectName("ActivityLogTable");
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

    CopyHelper::installTableCopyMenu(m_activityTable);

    connect(m_activityTable, &QTableWidget::itemSelectionChanged,
            this, &LogViewerPanel::onActivityTableSelectionChanged);
    activityLayout->addWidget(m_activityTable, 1);

    QGroupBox* detailsBox = new QGroupBox("Selected Log Details");
    QVBoxLayout* detailsLayout = new QVBoxLayout(detailsBox);
    detailsLayout->setContentsMargins(10, 8, 10, 10);
    m_activityDetailsText = new QPlainTextEdit();
    m_activityDetailsText->setReadOnly(true);
    m_activityDetailsText->setMaximumHeight(150);
    m_activityDetailsText->setPlaceholderText("Select an activity row to inspect full context.");
    m_activityDetailsText->setToolTip("Full selected log context. Use the standard copy shortcut to copy it.");
    detailsLayout->addWidget(m_activityDetailsText);
    activityLayout->addWidget(detailsBox);

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
    m_memberFilterCombo->setToolTip("Show distribution history for one member.");
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
    m_statusFilterCombo->setToolTip("Show distribution history by delivery status.");
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
    m_distributionTable->setObjectName("DistributionLogTable");
    m_distributionTable->setColumnCount(9);
    m_distributionTable->setHorizontalHeaderLabels({
        "Time", "Job", "Member", "Source File", "Destination", "Status", "Size", "WM Time", "Upload Time"
    });
    m_distributionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_distributionTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_distributionTable->setAlternatingRowColors(true);
    m_distributionTable->verticalHeader()->setVisible(false);
    m_distributionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // Column sizing
    m_distributionTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);     // Time
    m_distributionTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive); // Job
    m_distributionTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive); // Member
    m_distributionTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);   // Source
    m_distributionTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Interactive); // Dest
    m_distributionTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);     // Status
    m_distributionTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Fixed);     // Size
    m_distributionTable->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Fixed);     // WM Time
    m_distributionTable->horizontalHeader()->setSectionResizeMode(8, QHeaderView::Fixed);     // Upload
    m_distributionTable->setColumnWidth(0, 140);
    m_distributionTable->setColumnWidth(1, 120);
    m_distributionTable->setColumnWidth(2, 100);
    m_distributionTable->setColumnWidth(4, 150);
    m_distributionTable->setColumnWidth(5, 90);
    m_distributionTable->setColumnWidth(6, 80);
    m_distributionTable->setColumnWidth(7, 80);
    m_distributionTable->setColumnWidth(8, 80);

    CopyHelper::installTableCopyMenu(m_distributionTable);

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
    m_autoRefreshCheck->setToolTip("Refresh logs every 5 seconds while this panel is open.");
    m_autoRefreshCheck->setChecked(false);
    connect(m_autoRefreshCheck, &QCheckBox::toggled, this, &LogViewerPanel::onAutoRefreshToggled);
    bottomLayout->addWidget(m_autoRefreshCheck);

    m_refreshBtn = new QPushButton("Refresh");
    m_refreshBtn->setIcon(QIcon(":/icons/refresh-cw.svg"));
    m_refreshBtn->setToolTip("Reload activity logs, distribution history, and summary counts.");
    connect(m_refreshBtn, &QPushButton::clicked, this, &LogViewerPanel::onRefreshClicked);
    bottomLayout->addWidget(m_refreshBtn);

    m_exportBtn = new QPushButton("Export");
    m_exportBtn->setIcon(QIcon(":/icons/download.svg"));
    m_exportBtn->setToolTip("Export the currently filtered activity logs.");
    connect(m_exportBtn, &QPushButton::clicked, this, &LogViewerPanel::onExportClicked);
    bottomLayout->addWidget(m_exportBtn);

    m_copyDetailsBtn = new QPushButton("Copy Details");
    m_copyDetailsBtn->setIcon(QIcon(":/icons/copy.svg"));
    m_copyDetailsBtn->setToolTip("Copy the full selected activity or distribution row details.");
    m_copyDetailsBtn->setEnabled(false);
    connect(m_copyDetailsBtn, &QPushButton::clicked, this, &LogViewerPanel::onCopyDetailsClicked);
    bottomLayout->addWidget(m_copyDetailsBtn);

    m_copyReportBtn = new QPushButton("Copy Report");
    m_copyReportBtn->setIcon(QIcon(":/icons/clipboard.svg"));
    m_copyReportBtn->setToolTip("Copy the currently visible table as a tab-separated report.");
    m_copyReportBtn->setEnabled(false);
    connect(m_copyReportBtn, &QPushButton::clicked, this, &LogViewerPanel::onCopyReportClicked);
    bottomLayout->addWidget(m_copyReportBtn);

    m_clearBtn = new QPushButton("Clear Logs");
    m_clearBtn->setObjectName("PanelDangerButton");
    m_clearBtn->setIcon(QIcon(":/icons/trash-2.svg"));
    m_clearBtn->setToolTip("Clear persisted activity logs, error logs, and distribution history.");
    connect(m_clearBtn, &QPushButton::clicked, this, &LogViewerPanel::onClearClicked);
    bottomLayout->addWidget(m_clearBtn);

    // Loading indicator
    m_loadingLabel = new QLabel();
    m_loadingLabel->setProperty("type", "secondary");
    m_loadingLabel->setVisible(false);
    bottomLayout->addWidget(m_loadingLabel);

    bottomLayout->addStretch();

    // Last refreshed timestamp
    m_lastRefreshedLabel = new QLabel();
    m_lastRefreshedLabel->setProperty("type", "secondary");
    bottomLayout->addWidget(m_lastRefreshedLabel);

    m_countLabel = new QLabel();
    m_countLabel->setProperty("type", "secondary");
    CopyHelper::makeSelectable(m_countLabel);
    bottomLayout->addWidget(m_countLabel);

    mainLayout->addLayout(bottomLayout);

    // Stats bar
    m_statsLabel = new QLabel();
    m_statsLabel->setProperty("type", "secondary");
    mainLayout->addWidget(m_statsLabel);

    scrollArea->setWidget(contentWidget);
    outerLayout->addWidget(scrollArea);
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

    LogFilter filter = buildActivityFilter(500);

    // Run the log retrieval in a background thread
    QFuture<std::vector<LogEntry>> future = QtConcurrent::run([filter]() {
        LogManager& logMgr = LogManager::instance();
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

    LogFilter filter = buildActivityFilter(500);
    std::vector<LogEntry> entries = logMgr.getEntries(filter);
    populateActivityTableFromEntries(entries);
}

void LogViewerPanel::populateActivityTableFromEntries(const std::vector<LogEntry>& entries) {
    if (!m_activityTable) return;
    m_activityTable->setRowCount(0);

    // Show empty state if no entries
    if (entries.empty()) {
        if (m_countLabel) m_countLabel->setText("Showing 0 entries");
        updateEmptyState();
        updateCopyButtonStates();
        return;
    }

    m_activityTable->setRowCount(entries.size());

    for (int row = 0; row < static_cast<int>(entries.size()); ++row) {
        const LogEntry& entry = entries[row];

        // Time
        QTableWidgetItem* timeItem = new QTableWidgetItem(formatTimestamp(entry.timestamp));
        timeItem->setData(Qt::UserRole, formatLogEntryDetails(entry));
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
        QString detailsTooltip = QString::fromStdString(entry.details);
        if (details.isEmpty()) {
            QStringList context;
            if (!entry.memberId.empty()) context << "Member: " + QString::fromStdString(entry.memberId);
            if (!entry.filePath.empty()) context << "File: " + QString::fromStdString(entry.filePath);
            if (!entry.jobId.empty()) context << "Job: " + QString::fromStdString(entry.jobId);
            details = context.join(" | ");
            detailsTooltip = details;
        }
        if (details.length() > 50) {
            details = details.left(47) + "...";
        }
        QTableWidgetItem* detailsItem = new QTableWidgetItem(details);
        detailsItem->setToolTip(detailsTooltip);
        m_activityTable->setItem(row, 5, detailsItem);
    }

    if (m_countLabel) m_countLabel->setText(QString("Showing %1 entries").arg(entries.size()));
    updateEmptyState();
    updateCopyButtonStates();
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
        timeItem->setData(Qt::UserRole, formatDistributionRecordDetails(record));
        m_distributionTable->setItem(row, 0, timeItem);

        // Job
        QString jobId = QString::fromStdString(record.jobId);
        QString jobDisplay = jobId;
        if (jobDisplay.length() > 18) {
            jobDisplay = jobDisplay.left(15) + "...";
        }
        QTableWidgetItem* jobItem = new QTableWidgetItem(jobDisplay.isEmpty() ? "-" : jobDisplay);
        jobItem->setToolTip(jobId);
        m_distributionTable->setItem(row, 1, jobItem);

        // Member
        QTableWidgetItem* memberItem = new QTableWidgetItem(QString::fromStdString(record.memberName));
        memberItem->setToolTip(QString::fromStdString(record.memberId));
        m_distributionTable->setItem(row, 2, memberItem);

        // Source file (just filename)
        QString sourcePath = QString::fromStdString(record.sourceFile);
        QString fileName = sourcePath.mid(sourcePath.lastIndexOf('/') + 1);
        QTableWidgetItem* sourceItem = new QTableWidgetItem(fileName);
        sourceItem->setToolTip(sourcePath);
        m_distributionTable->setItem(row, 3, sourceItem);

        // Destination
        QTableWidgetItem* destItem = new QTableWidgetItem(QString::fromStdString(record.megaFolder));
        m_distributionTable->setItem(row, 4, destItem);

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
        m_distributionTable->setItem(row, 5, statusItem);

        // Size
        QTableWidgetItem* sizeItem = new QTableWidgetItem(formatFileSize(record.fileSizeBytes));
        m_distributionTable->setItem(row, 6, sizeItem);

        // Watermark time
        QTableWidgetItem* wmTimeItem = new QTableWidgetItem(formatDuration(record.watermarkTimeMs));
        m_distributionTable->setItem(row, 7, wmTimeItem);

        // Upload time
        QTableWidgetItem* uploadTimeItem = new QTableWidgetItem(formatDuration(record.uploadTimeMs));
        m_distributionTable->setItem(row, 8, uploadTimeItem);

        visibleCount++;
    }

    // Show empty state if no visible records
    if (visibleCount == 0) {
        m_distributionTable->setRowCount(0);
    }

    if (m_tabWidget && m_tabWidget->currentIndex() == 1 && m_countLabel) {
        m_countLabel->setText(QString("Showing %1 distributions").arg(visibleCount));
    }
    updateEmptyState();
    updateCopyButtonStates();
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

void LogViewerPanel::updateEmptyState() {
    if (!m_emptyState || !m_activityTable || !m_distributionTable || !m_tabWidget) return;
    bool empty = m_activityTable->rowCount() == 0 && m_distributionTable->rowCount() == 0;
    m_emptyState->setVisible(empty);
    m_tabWidget->setVisible(!empty);
}

void LogViewerPanel::updateCopyButtonStates() {
    if (!m_copyDetailsBtn || !m_copyReportBtn || !m_tabWidget) return;

    const bool activityActive = m_tabWidget->currentIndex() == 0;
    QTableWidget* table = activityActive ? m_activityTable : m_distributionTable;
    const bool hasRows = table && table->rowCount() > 0;
    const bool hasSelection = table && table->currentRow() >= 0;

    m_copyDetailsBtn->setEnabled(hasSelection);
    m_copyReportBtn->setEnabled(hasRows);

    if (!hasSelection) {
        m_copyDetailsBtn->setToolTip(activityActive
            ? "Select an activity row to copy its full details."
            : "Select a distribution row to copy its full details.");
    } else {
        m_copyDetailsBtn->setToolTip("Copy the full selected activity or distribution row details.");
    }

    if (!hasRows) {
        m_copyReportBtn->setToolTip(activityActive
            ? "No visible activity rows to copy."
            : "No visible distribution rows to copy.");
    } else {
        m_copyReportBtn->setToolTip("Copy the currently visible table as a tab-separated report.");
    }
}

void LogViewerPanel::updateLastRefreshedLabel() {
    if (!m_lastRefreshedLabel) return;

    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    m_lastRefreshedLabel->setText(QString("Last refreshed: %1").arg(timestamp));
}

LogFilter LogViewerPanel::buildActivityFilter(int limit) const {
    LogFilter filter;
    filter.limit = limit;

    QString quickFilter = m_quickFilterCombo
        ? m_quickFilterCombo->currentData().toString()
        : QStringLiteral("all");

    int effectiveLevel = m_levelFilter;
    if (quickFilter == "issues") {
        effectiveLevel = qMax(effectiveLevel, static_cast<int>(LogLevel::Warning));
    } else if (quickFilter == "errors") {
        effectiveLevel = qMax(effectiveLevel, static_cast<int>(LogLevel::Error));
    } else if (quickFilter == "debug_system") {
        effectiveLevel = static_cast<int>(LogLevel::Debug);
    }

    if (effectiveLevel >= 0) {
        filter.minLevel = static_cast<LogLevel>(effectiveLevel);
    }

    if (m_categoryFilter >= 0) {
        filter.categories.push_back(static_cast<LogCategory>(m_categoryFilter));
    }

    if (quickFilter == "debug_system") {
        filter.categories.clear();
        filter.categories.push_back(LogCategory::System);
    }

    if (!m_searchText.isEmpty()) {
        filter.searchText = m_searchText.toStdString();
    }

    if (quickFilter == "today") {
        QDateTime startOfDay(QDate::currentDate(), QTime(0, 0));
        filter.startTime = startOfDay.toMSecsSinceEpoch();
        filter.endTime = QDateTime::currentDateTime().toMSecsSinceEpoch();
    } else if (m_dateFilterCheck && m_dateFilterCheck->isChecked() && m_fromDateEdit && m_toDateEdit) {
        filter.startTime = m_fromDateEdit->dateTime().toMSecsSinceEpoch();
        filter.endTime = m_toDateEdit->dateTime().toMSecsSinceEpoch();
    }

    return filter;
}

QString LogViewerPanel::buildTableReport(QTableWidget* table) const {
    if (!table || table->rowCount() == 0) return {};

    QStringList lines;
    QStringList headers;
    for (int col = 0; col < table->columnCount(); ++col) {
        if (table->isColumnHidden(col)) continue;
        QTableWidgetItem* header = table->horizontalHeaderItem(col);
        headers << (header ? header->text() : QString("Column %1").arg(col + 1));
    }
    lines << headers.join("\t");

    for (int row = 0; row < table->rowCount(); ++row) {
        QStringList cells;
        for (int col = 0; col < table->columnCount(); ++col) {
            if (table->isColumnHidden(col)) continue;
            QTableWidgetItem* item = table->item(row, col);
            cells << (item ? item->text() : QString());
        }
        lines << cells.join("\t");
    }

    return lines.join("\n");
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

    LogFilter filter = buildActivityFilter(10000);

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
        "This will clear persisted activity logs, error logs, and distribution history.\n"
        "This action cannot be undone.",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (ret == QMessageBox::Yes) {
        LogManager::instance().clearAll();
        refresh();
        QMessageBox::information(this, "Cleared", "All logs have been cleared.");
    }
}

void LogViewerPanel::onQuickFilterChanged(int index) {
    Q_UNUSED(index);
    if (!m_activityTable) return;
    refreshActivityLog();
}

void LogViewerPanel::onCopyDetailsClicked() {
    if (!m_tabWidget) return;

    QString details;
    if (m_tabWidget->currentIndex() == 0) {
        details = m_activityDetailsText ? m_activityDetailsText->toPlainText() : QString();
    } else if (m_distributionTable && m_distributionTable->currentRow() >= 0) {
        QTableWidgetItem* item = m_distributionTable->item(m_distributionTable->currentRow(), 0);
        details = item ? item->data(Qt::UserRole).toString() : QString();
    }

    if (details.trimmed().isEmpty()) return;
    QApplication::clipboard()->setText(details);
}

void LogViewerPanel::onCopyReportClicked() {
    if (!m_tabWidget) return;

    QTableWidget* table = (m_tabWidget->currentIndex() == 0)
        ? m_activityTable
        : m_distributionTable;

    const QString report = buildTableReport(table);
    if (report.trimmed().isEmpty()) return;
    QApplication::clipboard()->setText(report);
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
    if (row < 0) {
        if (m_activityDetailsText) m_activityDetailsText->clear();
        updateCopyButtonStates();
        return;
    }

    QTableWidgetItem* timeItem = m_activityTable->item(row, 0);
    QString fullDetails = timeItem ? timeItem->data(Qt::UserRole).toString() : QString();
    if (m_activityDetailsText) {
        m_activityDetailsText->setPlainText(fullDetails);
    }
    if (!fullDetails.isEmpty()) {
        emit logEntrySelected(fullDetails);
    }
    updateCopyButtonStates();
}

void LogViewerPanel::onDistributionTableSelectionChanged() {
    if (!m_distributionTable) return;  // Guard against early calls
    int row = m_distributionTable->currentRow();
    if (row < 0) {
        updateCopyButtonStates();
        return;
    }

    QTableWidgetItem* timeItem = m_distributionTable->item(row, 0);
    QString fullDetails = timeItem ? timeItem->data(Qt::UserRole).toString() : QString();
    if (!fullDetails.isEmpty()) {
        emit logEntrySelected(fullDetails);
    }
    updateCopyButtonStates();
}

void LogViewerPanel::onTabChanged(int index) {
    // Guard against early calls before widgets are fully initialized
    if (!m_countLabel || !m_activityTable || !m_distributionTable) return;

    if (index == 0) {
        m_countLabel->setText(QString("Showing %1 entries").arg(m_activityTable->rowCount()));
    } else {
        m_countLabel->setText(QString("Showing %1 distributions").arg(m_distributionTable->rowCount()));
    }
    updateCopyButtonStates();
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

QString LogViewerPanel::formatLogEntryDetails(const LogEntry& entry) const {
    QString output;
    QTextStream stream(&output);
    stream << "Time: " << formatTimestamp(entry.timestamp) << "\n";
    stream << "Level: " << QString::fromStdString(LogManager::levelToString(entry.level)) << "\n";
    stream << "Category: " << QString::fromStdString(LogManager::categoryToString(entry.category)) << "\n";
    stream << "Action: " << QString::fromStdString(entry.action) << "\n";
    stream << "Message: " << QString::fromStdString(entry.message) << "\n";
    if (!entry.details.empty()) {
        stream << "Details: " << QString::fromStdString(entry.details) << "\n";
    }
    if (!entry.memberId.empty()) {
        stream << "Member ID: " << QString::fromStdString(entry.memberId) << "\n";
    }
    if (!entry.filePath.empty()) {
        stream << "File Path: " << QString::fromStdString(entry.filePath) << "\n";
    }
    if (!entry.jobId.empty()) {
        stream << "Job ID: " << QString::fromStdString(entry.jobId) << "\n";
    }
    return output.trimmed();
}

QString LogViewerPanel::formatDistributionRecordDetails(const DistributionRecord& record) const {
    QString statusStr;
    switch (record.status) {
        case DistributionRecord::Status::Pending: statusStr = "Pending"; break;
        case DistributionRecord::Status::Watermarking: statusStr = "Watermarking"; break;
        case DistributionRecord::Status::Uploading: statusStr = "Uploading"; break;
        case DistributionRecord::Status::Completed: statusStr = "Completed"; break;
        case DistributionRecord::Status::Failed: statusStr = "Failed"; break;
    }

    QString output;
    QTextStream stream(&output);
    stream << "Time: " << formatTimestamp(record.timestamp) << "\n";
    if (!record.jobId.empty()) {
        stream << "Job ID: " << QString::fromStdString(record.jobId) << "\n";
    }
    stream << "Member: " << QString::fromStdString(record.memberName) << "\n";
    if (!record.memberId.empty()) {
        stream << "Member ID: " << QString::fromStdString(record.memberId) << "\n";
    }
    stream << "Status: " << statusStr << "\n";
    if (!record.sourceFile.empty()) {
        stream << "Source: " << QString::fromStdString(record.sourceFile) << "\n";
    }
    if (!record.outputFile.empty()) {
        stream << "Output: " << QString::fromStdString(record.outputFile) << "\n";
    }
    if (!record.megaFolder.empty()) {
        stream << "Destination: " << QString::fromStdString(record.megaFolder) << "\n";
    }
    if (!record.megaLink.empty()) {
        stream << "MEGA Link: " << QString::fromStdString(record.megaLink) << "\n";
    }
    stream << "Size: " << formatFileSize(record.fileSizeBytes) << "\n";
    stream << "Watermark Time: " << formatDuration(record.watermarkTimeMs) << "\n";
    stream << "Upload Time: " << formatDuration(record.uploadTimeMs) << "\n";
    if (!record.errorMessage.empty()) {
        stream << "Error: " << QString::fromStdString(record.errorMessage) << "\n";
    }
    return output.trimmed();
}

QColor LogViewerPanel::getLevelColor(int level) const {
    auto& tm = ThemeManager::instance();
    switch (level) {
        case 0: return tm.textSecondary();   // Debug - gray
        case 1: return tm.textPrimary();     // Info - primary text
        case 2: return tm.supportWarning();  // Warning - amber
        case 3: return tm.supportError();    // Error - red
        default: return tm.textPrimary();
    }
}

QColor LogViewerPanel::getStatusColor(int status) const {
    auto& tm = ThemeManager::instance();
    switch (status) {
        case 0: return tm.textSecondary();   // Pending - gray
        case 1: return tm.supportInfo();     // Watermarking - blue
        case 2: return tm.supportInfo();     // Uploading - purple (closest: info)
        case 3: return tm.supportSuccess();  // Completed - green
        case 4: return tm.supportError();    // Failed - red
        default: return tm.textPrimary();
    }
}

} // namespace MegaCustom
