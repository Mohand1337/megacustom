#ifndef MEGACUSTOM_CONTENTMANAGERPANEL_H
#define MEGACUSTOM_CONTENTMANAGERPANEL_H

#include <QWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QMap>
#include <QList>
#include <QThread>

class EmptyStateWidget;

namespace mega {
    class MegaApi;
}

namespace MegaCustom {

class MemberRegistry;
class FileController;
class CloudCopier;

// ==================== Data Structures ====================

/**
 * Content status for a single call within a member folder
 */
struct CallInfo {
    QString date;              // "03-25-2026"
    QString callName;          // Full base name without extension
    bool hasVideo = false;     // .mp4
    bool hasAudio = false;     // .mp3
    bool hasDoc = false;       // .pdf (not AI Summary)
    bool hasAiSummary = false; // "AI Summary.pdf"
    QStringList actualFiles;   // All files found for this call
    QStringList oddFiles;      // Oddly-named files that might belong here
};

/**
 * Audit result for one member
 */
struct MemberAudit {
    QString memberId;
    QString displayName;
    QString folderPath;
    QMap<QString, CallInfo> calls;  // callName -> CallInfo
    int totalFiles = 0;
    int missingCount = 0;
    int oddNameCount = 0;
};

/**
 * A pending rename operation
 */
struct RenameItem {
    QString memberId;
    QString currentPath;       // Full MEGA path
    QString currentName;       // Current filename
    QString newName;           // Proposed new filename
    QString targetFolder;      // If non-empty, move to this subfolder after rename
    bool selected = true;
};

/**
 * A pending reorganize operation (move files into subfolder)
 */
struct ReorganizeItem {
    QString memberId;
    QString baseName;          // e.g., "03-31-2026 FF Hot Seats"
    QStringList filePaths;     // Files to move
    QString targetFolder;      // Subfolder to create/move into
    bool selected = true;
};

// ==================== Panel ====================

/**
 * Content Manager panel — audit member content and organize files
 *
 * Tab 1 (Audit): Scan member folders, compare against reference,
 *   show missing VIDEO/DOC/AI SUMMARY per call per member
 *
 * Tab 2 (Organize): Detect oddly-named files, rename to convention,
 *   move FF content into subfolders
 */
class ContentManagerPanel : public QWidget {
    Q_OBJECT

public:
    explicit ContentManagerPanel(QWidget* parent = nullptr);
    ~ContentManagerPanel();

    void setMegaApi(mega::MegaApi* api);
    void setMemberRegistry(MemberRegistry* registry);
    void setCloudCopier(CloudCopier* copier);

signals:
    void scanStarted();
    void scanProgress(const QString& member, int current, int total);
    void scanCompleted(int totalMissing);

private slots:
    // Audit tab
    void onScanClicked();
    void onExportMissing();
    void onScanBasePath();

    // Organize tab
    void onApplyRenames();
    void onApplyReorganize();

private:
    void setupUI();
    void setupAuditTab(QWidget* tab);
    void setupOrganizeTab(QWidget* tab);

    // Audit logic
    void runAudit();
    QMap<QString, CallInfo> scanMemberFolder(const QString& folderPath);
    void populateAuditTable();
    void updateAuditSummary();

    // Organize logic
    void detectOddNames();
    void detectFFToReorganize();
    void populateRenameTable();
    void populateReorganizeTable();

    // Helpers
    QString extractCallBaseName(const QString& filename) const;
    bool isOddlyNamed(const QString& filename) const;
    QString suggestProperName(const QString& oddName, const MemberAudit& memberAudit) const;

    // UI - Main
    QTabWidget* m_tabWidget = nullptr;

    // UI - Audit tab
    QLineEdit* m_basePathEdit = nullptr;
    QPushButton* m_browseBtn = nullptr;
    QComboBox* m_referenceMemberCombo = nullptr;
    QComboBox* m_groupFilterCombo = nullptr;
    QPushButton* m_scanBtn = nullptr;
    QTableWidget* m_auditTable = nullptr;
    QLabel* m_auditSummaryLabel = nullptr;
    QProgressBar* m_auditProgressBar = nullptr;
    QPushButton* m_exportBtn = nullptr;

    // UI - Organize tab
    QTableWidget* m_renameTable = nullptr;
    QTableWidget* m_reorganizeTable = nullptr;
    QLabel* m_renameSummaryLabel = nullptr;
    QLabel* m_reorganizeSummaryLabel = nullptr;
    QPushButton* m_applyRenameBtn = nullptr;
    QPushButton* m_applyReorganizeBtn = nullptr;
    QProgressBar* m_organizeProgressBar = nullptr;
    QLabel* m_organizeStatusLabel = nullptr;

    // Data
    mega::MegaApi* m_megaApi = nullptr;
    MemberRegistry* m_registry = nullptr;
    CloudCopier* m_cloudCopier = nullptr;

    QList<MemberAudit> m_auditResults;
    QStringList m_masterCallList;          // Reference member's call names
    QList<RenameItem> m_renameQueue;
    QList<ReorganizeItem> m_reorganizeQueue;

    // State
    bool m_isScanning = false;
    QString m_referenceMemberId;

    // Worker
    QThread* m_workerThread = nullptr;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_CONTENTMANAGERPANEL_H
