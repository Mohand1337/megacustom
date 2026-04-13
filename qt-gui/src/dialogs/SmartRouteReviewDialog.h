#ifndef MEGACUSTOM_SMARTROUTEREVIEWDIALOG_H
#define MEGACUSTOM_SMARTROUTEREVIEWDIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QList>
#include <QMap>

namespace MegaCustom {

struct WmFolderInfo;
struct ContentRoute;
enum class ContentType;
class MemberRegistry;

/**
 * @brief Dialog for reviewing and editing smart route classifications
 *
 * Shows detected content types (Hot Seats, Theory Calls, NHB Files, etc.)
 * grouped by member, with editable destination paths and bulk actions.
 *
 * Opened after scan when Smart Route is enabled. Returns edited routes
 * to populate the distribution table.
 */
class SmartRouteReviewDialog : public QDialog {
    Q_OBJECT

public:
    explicit SmartRouteReviewDialog(QWidget* parent = nullptr);

    /**
     * Set the routes to review. Only smart-routed folders are shown.
     * Makes a working copy — original data is not modified until Apply.
     */
    void setRoutes(const QList<WmFolderInfo>& folders, MemberRegistry* registry);

    /**
     * Get the reviewed/edited folders back (includes non-smart-routed unchanged).
     */
    QList<WmFolderInfo> getReviewedFolders() const;

private:
    void setupUI();
    void populateTable();
    void updateSummary();
    void readBackEdits();
    void onBulkSetDestination(ContentType type);
    void onSelectAll();
    void onDeselectAll();

    // Table column indices
    enum Column {
        COL_CHECK = 0,
        COL_ITEM,
        COL_TYPE,
        COL_SOURCE,
        COL_DESTINATION,
        COL_COUNT
    };

    QTableWidget* m_table = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QLabel* m_warningLabel = nullptr;
    QPushButton* m_applyBtn = nullptr;

    QList<WmFolderInfo> m_folders;       // Working copy of all folders
    MemberRegistry* m_registry = nullptr;

    // Maps table row → (folderIdx, routeIdx). routeIdx = -1 for header rows.
    struct RowMapping {
        int folderIdx = -1;
        int routeIdx = -1;
    };
    QMap<int, RowMapping> m_rowMap;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_SMARTROUTEREVIEWDIALOG_H
