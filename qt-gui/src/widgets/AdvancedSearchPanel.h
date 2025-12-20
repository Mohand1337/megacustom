#ifndef ADVANCEDSEARCHPANEL_H
#define ADVANCEDSEARCHPANEL_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QDateEdit>
#include <QSpinBox>
#include <QListView>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QGroupBox>
#include <QScrollArea>
#include <QTimer>
#include <QClipboard>
#include <QMenu>

#include "CloudSearchIndex.h"

namespace MegaCustom {

class CloudSearchIndex;
class LoadingSpinner;

/**
 * Custom delegate for drawing search result items with checkboxes
 */
class AdvancedSearchResultDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit AdvancedSearchResultDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
    bool editorEvent(QEvent* event, QAbstractItemModel* model,
                     const QStyleOptionViewItem& option, const QModelIndex& index) override;

    // Data roles
    enum {
        NameRole = Qt::UserRole + 1,
        PathRole,
        SizeRole,
        DateRole,
        HandleRole,
        IsFolderRole,
        ExtensionRole,
        RelevanceRole,
        CheckedRole,
        NameMatchesRole  // QVariantList of match spans for highlighting
    };

private:
    QString formatSize(qint64 bytes) const;
    QString formatDate(qint64 timestamp) const;
    QRect checkboxRect(const QStyleOptionViewItem& option) const;
};

/**
 * Advanced Search Panel - Everything-style search tool
 *
 * Provides full-featured cloud file search with:
 * - Real-time search as you type
 * - Filters: type, extension, size, date, path, regex
 * - Sorting by relevance, name, size, date, type
 * - Multi-selection with checkboxes
 * - Bulk operations: copy paths, bulk rename, go to location
 */
class AdvancedSearchPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AdvancedSearchPanel(QWidget* parent = nullptr);
    ~AdvancedSearchPanel();

    // Set the search index to use
    void setSearchIndex(CloudSearchIndex* index);

signals:
    // Navigate to a specific file/folder in CloudDrive
    void navigateToPath(const QString& handle, const QString& path, bool isFolder);

    // Request bulk rename dialog
    void bulkRenameRequested(const QStringList& paths);

    // Request actual file rename via MEGA API
    void renameRequested(const QString& path, const QString& newName);

    // Batch rename completed
    void batchRenameCompleted(int successCount, int failCount);

private slots:
    // Search
    void onSearchTextChanged(const QString& text);
    void executeSearch();
    void onSearchButtonClicked();

    // Filters
    void onTypeFilterChanged();
    void onExtensionFilterChanged();
    void onSizeFilterChanged();
    void onDateFilterChanged();
    void onPathFilterChanged();
    void onRegexToggled(bool checked);

    // Sorting
    void onSortFieldChanged(int index);
    void onSortOrderToggled();

    // Results
    void onResultDoubleClicked(const QModelIndex& index);
    void onResultContextMenu(const QPoint& pos);
    void onSelectionChanged();

    // Actions
    void onSelectAll();
    void onDeselectAll();
    void onCopyPaths();
    void onBulkRename();
    void onGoToLocation();

    // Index status
    void updateIndexStatus();

private:
    void setupUI();
    void setupSearchSection();
    void setupFiltersSection();
    void setupSortSection();
    void setupResultsSection();
    void setupActionsSection();
    void setupStatusSection();

    void applyStyles();
    void populateResults(const QVector<SearchResult>& results);
    void clearResults();
    void updateActionButtons();
    QString buildQueryString() const;
    QStringList getSelectedPaths() const;
    QStringList getSelectedHandles() const;

private:
    // Main layout
    QVBoxLayout* m_mainLayout;

    // Search section
    QLineEdit* m_searchEdit;
    QPushButton* m_searchBtn;

    // Filters section
    QGroupBox* m_filtersGroup;
    QButtonGroup* m_typeFilterGroup;
    QRadioButton* m_typeAllRadio;
    QRadioButton* m_typeFilesRadio;
    QRadioButton* m_typeFoldersRadio;
    QLineEdit* m_extensionEdit;
    QComboBox* m_sizeMinUnitCombo;
    QSpinBox* m_sizeMinSpin;
    QComboBox* m_sizeMaxUnitCombo;
    QSpinBox* m_sizeMaxSpin;
    QComboBox* m_datePresetCombo;
    QDateEdit* m_dateFromEdit;
    QDateEdit* m_dateToEdit;
    QLineEdit* m_pathEdit;
    QCheckBox* m_regexCheck;

    // Sort section
    QComboBox* m_sortCombo;
    QPushButton* m_sortOrderBtn;
    QLabel* m_resultsCountLabel;

    // Results section
    QListView* m_resultsList;
    QStandardItemModel* m_model;
    AdvancedSearchResultDelegate* m_delegate;

    // Actions section
    QPushButton* m_selectAllBtn;
    QPushButton* m_deselectAllBtn;
    QPushButton* m_copyPathsBtn;
    QPushButton* m_bulkRenameBtn;
    QPushButton* m_goToLocationBtn;

    // Status section
    QLabel* m_indexStatusLabel;
    LoadingSpinner* m_indexingSpinner;

    // Search state
    CloudSearchIndex* m_searchIndex;
    QString m_currentQuery;
    SortField m_sortField;
    SortOrder m_sortOrder;
    QTimer* m_searchTimer;

    // Constants
    static constexpr int SEARCH_DEBOUNCE_MS = 150;
    static constexpr int MAX_RESULTS = 500;
};

} // namespace MegaCustom

#endif // ADVANCEDSEARCHPANEL_H
