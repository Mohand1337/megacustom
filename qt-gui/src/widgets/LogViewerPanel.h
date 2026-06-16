#ifndef MEGACUSTOM_LOGVIEWERPANEL_H
#define MEGACUSTOM_LOGVIEWERPANEL_H

#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QTabWidget>
#include <QTimer>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QFutureWatcher>
#include <QPlainTextEdit>
#include "utils/OperationJobStore.h"
#include <vector>

class EmptyStateWidget;

namespace MegaCustom {

struct LogEntry;
struct DistributionRecord;
struct LogStats;
struct LogFilter;

/**
 * Panel for viewing activity logs and distribution history
 * Provides filtering, search, and export capabilities
 */
class LogViewerPanel : public QWidget {
    Q_OBJECT

public:
    explicit LogViewerPanel(QWidget* parent = nullptr);
    ~LogViewerPanel() = default;

signals:
    void logEntrySelected(const QString& details);
    void openRelatedPanelRequested(const QString& panelKey, const QString& jobId);
    void retryJobRequested(const QString& jobId);
    void resumeJobRequested(const QString& jobId);
    void cleanupJobRequested(const QString& jobId);

public slots:
    void refresh();
    void refreshJobs();
    void refreshActivityLog();
    void refreshDistributionHistory();
    void refreshStats();

private slots:
    void onJobTypeFilterChanged(int index);
    void onJobStatusFilterChanged(int index);
    void onSearchChanged(const QString& text);
    void onLevelFilterChanged(int index);
    void onCategoryFilterChanged(int index);
    void onMemberFilterChanged(int index);
    void onStatusFilterChanged(int index);
    void onDateRangeChanged();
    void onRefreshClicked();
    void onExportClicked();
    void onClearClicked();
    void onQuickFilterChanged(int index);
    void onCopyDetailsClicked();
    void onCopyReportClicked();
    void onCopyJobIdClicked();
    void onShowJobActivityClicked();
    void onRetryJobClicked();
    void onResumeJobClicked();
    void onCleanupJobClicked();
    void onOpenRelatedPanelClicked();
    void onAutoRefreshToggled(bool enabled);
    void onJobsTableSelectionChanged();
    void onActivityTableSelectionChanged();
    void onDistributionTableSelectionChanged();
    void onTabChanged(int index);
    void onActivityLogLoaded();
    void onDistributionHistoryLoaded();

private:
    void setupUI();
    void populateJobsTable();
    void populateJobsTableFromRecords(const QList<OperationJobRecord>& records);
    void populateActivityTable();
    void populateActivityTableFromEntries(const std::vector<LogEntry>& entries);
    void populateDistributionTable();
    void populateDistributionTableFromRecords(const std::vector<DistributionRecord>& records);
    void updateStatsDisplay();
    void setLoadingState(bool loading);
    void updateEmptyState();
    void updateCopyButtonStates();
    void updateJobActionStates();
    void updateLastRefreshedLabel();
    QTableWidget* currentVisibleTable() const;
    LogFilter buildActivityFilter(int limit) const;
    QString buildTableReport(QTableWidget* table) const;
    QString formatTimestamp(qint64 timestamp) const;
    QString formatFileSize(qint64 bytes) const;
    QString formatDuration(qint64 ms) const;
    QString formatJobRecordDetails(const OperationJobRecord& record) const;
    QString formatJobProgress(const OperationJobRecord& record) const;
    QString formatJobType(OperationJobType type) const;
    QString formatJobStatus(OperationJobStatus status) const;
    QString panelKeyForJobType(OperationJobType type) const;
    QString formatLogEntryDetails(const LogEntry& entry) const;
    QString formatDistributionRecordDetails(const DistributionRecord& record) const;
    QColor getLevelColor(int level) const;
    QColor getStatusColor(int status) const;
    QColor getJobStatusColor(OperationJobStatus status) const;

    // UI Components - Jobs Tab
    QTableWidget* m_jobsTable = nullptr;
    QComboBox* m_jobTypeCombo = nullptr;
    QComboBox* m_jobStatusCombo = nullptr;
    QPlainTextEdit* m_jobDetailsText = nullptr;
    QPushButton* m_copyJobIdBtn = nullptr;
    QPushButton* m_showJobActivityBtn = nullptr;
    QPushButton* m_retryJobBtn = nullptr;
    QPushButton* m_resumeJobBtn = nullptr;
    QPushButton* m_cleanupJobBtn = nullptr;
    QPushButton* m_openRelatedPanelBtn = nullptr;

    // UI Components - Activity Log Tab
    QTableWidget* m_activityTable = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QComboBox* m_levelCombo = nullptr;
    QComboBox* m_categoryCombo = nullptr;
    QComboBox* m_quickFilterCombo = nullptr;
    QDateTimeEdit* m_fromDateEdit = nullptr;
    QDateTimeEdit* m_toDateEdit = nullptr;
    QCheckBox* m_dateFilterCheck = nullptr;
    QPlainTextEdit* m_activityDetailsText = nullptr;

    // UI Components - Distribution History Tab
    QTableWidget* m_distributionTable = nullptr;
    QComboBox* m_memberFilterCombo = nullptr;
    QComboBox* m_statusFilterCombo = nullptr;
    QLineEdit* m_distributionJobFilterEdit = nullptr;

    // Empty state
    EmptyStateWidget* m_emptyState = nullptr;

    // UI Components - Common
    QTabWidget* m_tabWidget = nullptr;
    QPushButton* m_refreshBtn = nullptr;
    QPushButton* m_exportBtn = nullptr;
    QPushButton* m_copyDetailsBtn = nullptr;
    QPushButton* m_copyReportBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QCheckBox* m_autoRefreshCheck = nullptr;
    QLabel* m_statsLabel = nullptr;
    QLabel* m_countLabel = nullptr;

    // Auto-refresh timer
    QTimer* m_refreshTimer = nullptr;

    // Loading indicator
    QLabel* m_loadingLabel = nullptr;
    QLabel* m_lastRefreshedLabel = nullptr;
    bool m_isLoading = false;

    // Async loading
    QFutureWatcher<std::vector<LogEntry>>* m_activityWatcher = nullptr;
    QFutureWatcher<std::vector<DistributionRecord>>* m_distributionWatcher = nullptr;

    // Current filters
    QString m_searchText;
    QString m_selectedJobId;
    OperationJobType m_selectedJobType = OperationJobType::Unknown;
    OperationJobStatus m_selectedJobStatus = OperationJobStatus::Queued;
    int m_levelFilter = -1;      // -1 = all
    int m_categoryFilter = -1;   // -1 = all
    QString m_memberFilter;
    int m_statusFilter = -1;     // -1 = all
};

} // namespace MegaCustom

#endif // MEGACUSTOM_LOGVIEWERPANEL_H
