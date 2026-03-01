#ifndef MEGACUSTOM_MEMBERREGISTRYPANEL_H
#define MEGACUSTOM_MEMBERREGISTRYPANEL_H

#include <QWidget>
#include <QTableWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QGroupBox>
#include <QCheckBox>
#include <QMap>
#include <QTabWidget>
#include <QTimer>

class EmptyStateWidget;

namespace MegaCustom {

class MemberRegistry;
class FileController;
struct MemberInfo;
struct MemberTemplate;

/**
 * Panel for managing the member registry
 * Provides UI to add, edit, remove members and configure the global template
 * Phase 2: Extended with distribution folder binding, watermark config, contact info
 */
class MemberRegistryPanel : public QWidget {
    Q_OBJECT

public:
    explicit MemberRegistryPanel(QWidget* parent = nullptr);
    ~MemberRegistryPanel() = default;

    /**
     * Set file controller for folder browsing
     */
    void setFileController(FileController* controller);

signals:
    void memberSelected(const QString& memberId);

public slots:
    void refresh();
    void refreshTemplate();

private slots:
    void onAddMember();
    void onEditMember();
    void onRemoveMember();
    void onEditTemplate();
    void onSaveTemplate();
    void onImportMembers();
    void onExportMembers();
    void onExportCsv();
    void onImportCsv();
    void onTableSelectionChanged();
    void onTableDoubleClicked(int row, int column);
    void onPopulateDefaults();
    void onBindFolder();
    void onUnbindFolder();
    void onSearchChanged(const QString& text);
    void onFilterChanged();
    void onSortByColumn(int column);
    void onWordPressSync();
    void onWpSyncCompleted(int created, int updated);

    // Groups tab
    void onAddGroup();
    void onRenameGroup();
    void onDeleteGroup();
    void onDuplicateGroup();
    void onGroupSelectionChanged();
    void onGroupMemberToggled(QListWidgetItem* item);
    void onGroupSelectAll();
    void onGroupDeselectAll();
    void onGroupSearchChanged(const QString& text);

private:
    void setupUI();
    void populateTable();
    void updateEmptyState();
    void refreshGroups();
    QString getSelectedMemberId() const;
    void showMemberEditDialog(const MemberInfo& member, bool isNew);
    void rebuildPathTypesGrid();

    // Global template path types
    QWidget* m_pathTypesWidget;
    QMap<QString, QCheckBox*> m_pathTypeChecks;
    QMap<QString, QLineEdit*> m_pathTypeEdits;

    // Empty state
    EmptyStateWidget* m_emptyState = nullptr;

    // Member table
    QTableWidget* m_memberTable;

    // Search/filter
    QLineEdit* m_searchEdit;
    QCheckBox* m_activeOnlyCheck;
    QCheckBox* m_withFolderOnlyCheck;
    QCheckBox* m_withEmailCheck;
    QCheckBox* m_withIpCheck;
    QCheckBox* m_missingWmInfoCheck;

    // Column sorting
    int m_sortColumn = 0;        // Current sort column
    bool m_sortAscending = true;  // Sort direction

    // Actions
    QPushButton* m_addBtn;
    QPushButton* m_editBtn;
    QPushButton* m_removeBtn;
    QPushButton* m_bindFolderBtn;
    QPushButton* m_unbindFolderBtn;
    QPushButton* m_importBtn;
    QPushButton* m_exportBtn;
    QPushButton* m_importCsvBtn;
    QPushButton* m_exportCsvBtn;
    QPushButton* m_populateBtn;
    QPushButton* m_wpSyncBtn;

    // Stats
    QLabel* m_statsLabel;

    // Groups tab
    QListWidget* m_groupList = nullptr;
    QListWidget* m_groupMemberList = nullptr;
    QLineEdit* m_groupSearchEdit = nullptr;
    QPushButton* m_addGroupBtn = nullptr;
    QPushButton* m_renameGroupBtn = nullptr;
    QPushButton* m_deleteGroupBtn = nullptr;
    QPushButton* m_duplicateGroupBtn = nullptr;
    QPushButton* m_groupSelectAllBtn = nullptr;
    QPushButton* m_groupDeselectAllBtn = nullptr;
    QLabel* m_groupStatsLabel = nullptr;
    bool m_suppressGroupRefresh = false;  // Guard to prevent rebuild during checkbox toggles

    // Debounce timers
    QTimer* m_searchDebounce = nullptr;
    QTimer* m_groupSearchDebounce = nullptr;

    // Controllers
    MemberRegistry* m_registry;
    FileController* m_fileController = nullptr;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_MEMBERREGISTRYPANEL_H
