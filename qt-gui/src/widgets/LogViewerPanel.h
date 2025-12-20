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
#include <vector>

namespace MegaCustom {

struct LogEntry;
struct DistributionRecord;
struct LogStats;

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

public slots:
    void refresh();
    void refreshActivityLog();
    void refreshDistributionHistory();
    void refreshStats();

private slots:
    void onSearchChanged(const QString& text);
    void onLevelFilterChanged(int index);
    void onCategoryFilterChanged(int index);
    void onMemberFilterChanged(int index);
    void onStatusFilterChanged(int index);
    void onDateRangeChanged();
    void onRefreshClicked();
    void onExportClicked();
    void onClearClicked();
    void onAutoRefreshToggled(bool enabled);
    void onActivityTableSelectionChanged();
    void onDistributionTableSelectionChanged();
    void onTabChanged(int index);
    void onActivityLogLoaded();
    void onDistributionHistoryLoaded();

private:
    void setupUI();
    void populateActivityTable();
    void populateActivityTableFromEntries(const std::vector<LogEntry>& entries);
    void populateDistributionTable();
    void populateDistributionTableFromRecords(const std::vector<DistributionRecord>& records);
    void updateStatsDisplay();
    void setLoadingState(bool loading);
    void showEmptyState(QTableWidget* table, const QString& message);
    void updateLastRefreshedLabel();
    QString formatTimestamp(qint64 timestamp) const;
    QString formatFileSize(qint64 bytes) const;
    QString formatDuration(qint64 ms) const;
    QColor getLevelColor(int level) const;
    QColor getStatusColor(int status) const;

    // UI Components - Activity Log Tab
    QTableWidget* m_activityTable = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QComboBox* m_levelCombo = nullptr;
    QComboBox* m_categoryCombo = nullptr;
    QDateTimeEdit* m_fromDateEdit = nullptr;
    QDateTimeEdit* m_toDateEdit = nullptr;
    QCheckBox* m_dateFilterCheck = nullptr;

    // UI Components - Distribution History Tab
    QTableWidget* m_distributionTable = nullptr;
    QComboBox* m_memberFilterCombo = nullptr;
    QComboBox* m_statusFilterCombo = nullptr;

    // UI Components - Common
    QTabWidget* m_tabWidget = nullptr;
    QPushButton* m_refreshBtn = nullptr;
    QPushButton* m_exportBtn = nullptr;
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
    int m_levelFilter = -1;      // -1 = all
    int m_categoryFilter = -1;   // -1 = all
    QString m_memberFilter;
    int m_statusFilter = -1;     // -1 = all
};

} // namespace MegaCustom

#endif // MEGACUSTOM_LOGVIEWERPANEL_H
