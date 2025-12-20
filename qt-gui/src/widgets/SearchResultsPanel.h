#ifndef SEARCHRESULTSPANEL_H
#define SEARCHRESULTSPANEL_H

#include <QWidget>
#include <QListView>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "CloudSearchIndex.h"

namespace MegaCustom {

class CloudSearchIndex;

/**
 * Custom delegate for drawing search result items
 */
class SearchResultDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit SearchResultDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;

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
        NameMatchesRole  // QVariantList of match spans for highlighting
    };

private:
    QString formatSize(qint64 bytes) const;
    QString formatDate(qint64 timestamp) const;
};

/**
 * Dropdown panel showing instant search results
 *
 * This panel appears below the search field and displays
 * matching results as the user types (like Spotlight/Alfred)
 */
class SearchResultsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SearchResultsPanel(QWidget* parent = nullptr);
    ~SearchResultsPanel();

    // Set the search index to use
    void setSearchIndex(CloudSearchIndex* index);

    // Keyboard navigation
    void selectNext();
    void selectPrevious();
    void activateSelected();

    // Visibility control
    void showAtPosition(const QPoint& pos, int width);
    void updatePosition(const QPoint& pos, int width);

public slots:
    // Set search query (called on each keystroke, debounced internally)
    void setQuery(const QString& query);

    // Clear results and hide
    void clearResults();

    // Sorting
    void setSortField(SortField field);
    void setSortOrder(SortOrder order);

signals:
    // Result was activated (double-click or Enter)
    void resultActivated(const QString& handle, const QString& path, bool isFolder);

    // Bulk rename requested for selected items
    void bulkRenameRequested(const QStringList& handles);

    // Panel visibility changed
    void visibilityChanged(bool visible);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void executeSearch();
    void onItemActivated(const QModelIndex& index);
    void onSortFieldChanged(int index);
    void onSortOrderToggled();
    void updateStatusBar();

private:
    void setupUI();
    void populateResults(const QVector<SearchResult>& results);
    void updateSortButton();

private:
    // UI components
    QVBoxLayout* m_mainLayout;
    QListView* m_resultsList;
    QStandardItemModel* m_model;
    SearchResultDelegate* m_delegate;

    // Header bar
    QWidget* m_headerBar;
    QLabel* m_queryLabel;
    QComboBox* m_sortCombo;
    QPushButton* m_sortOrderBtn;

    // Status bar
    QWidget* m_statusBar;
    QLabel* m_statusLabel;
    QLabel* m_indexStatusLabel;
    QPushButton* m_bulkRenameBtn;

    // Search state
    CloudSearchIndex* m_searchIndex;
    QString m_currentQuery;
    SortField m_sortField;
    SortOrder m_sortOrder;

    // Debounce timer
    QTimer* m_searchTimer;
    static constexpr int SEARCH_DEBOUNCE_MS = 100;

    // Settings
    static constexpr int MAX_VISIBLE_RESULTS = 100;
    static constexpr int PANEL_MAX_HEIGHT = 500;
};

} // namespace MegaCustom

#endif // SEARCHRESULTSPANEL_H
