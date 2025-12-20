#ifndef MEGACUSTOM_CLOUDCOPIERCONTROLLER_H
#define MEGACUSTOM_CLOUDCOPIERCONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QDateTime>
#include <QMutex>
#include <memory>
#include <atomic>

#include "utils/MemberRegistry.h"  // Need full definition for QList<MemberInfo> in signals

namespace MegaCustom {

// Forward declarations
class CloudCopier;

/**
 * @brief Conflict resolution options
 */
enum class CopyConflictResolution {
    SKIP,           // Skip the item
    OVERWRITE,      // Overwrite existing
    RENAME,         // Rename (add suffix)
    ASK,            // Ask user (default)
    SKIP_ALL,       // Skip all future conflicts
    OVERWRITE_ALL,  // Overwrite all future conflicts
    CANCEL          // Cancel the entire operation
};

/**
 * @brief Information about a conflict for the dialog
 */
struct CopyConflictInfo {
    QString sourcePath;
    QString destinationPath;
    QString existingName;
    qint64 existingSize = 0;
    QDateTime existingModTime;
    qint64 sourceSize = 0;
    QDateTime sourceModTime;
    bool isFolder = false;
};

/**
 * @brief Copy task for display in UI
 */
struct CopyTaskInfo {
    int taskId = 0;
    QString sourcePath;
    QString destinationPath;
    QString status;     // "Pending", "Copying...", "Completed", "Failed", "Skipped"
    int progress = 0;   // 0-100
    QString errorMessage;
};

/**
 * @brief Preview item showing what will be copied where
 */
struct CopyPreviewItem {
    QString sourcePath;      // Full source path
    QString sourceName;      // Just the name being copied
    QString destinationPath; // Full destination path (where it will end up)
    bool isFolder = false;
};

/**
 * @brief Copy template with destinations
 */
struct CopyTemplateInfo {
    QString name;
    QStringList destinations;
    QDateTime created;
    QDateTime lastUsed;
};

/**
 * @brief Path validation result
 */
struct PathValidationResult {
    QString path;
    bool exists = false;
    bool isFolder = false;
    QString errorMessage;  // Empty if valid
};

/**
 * @brief Member copy destination info for UI display
 */
struct MemberDestinationInfo {
    QString memberId;
    QString memberName;
    QString expandedPath;      // The actual destination path after template expansion
    bool isValid = true;       // False if member has no distribution folder
    QString errorMessage;
};

/**
 * @brief Result of template expansion preview
 */
struct TemplateExpansionPreview {
    QString templatePath;                      // Original template with {member} etc.
    QVector<MemberDestinationInfo> members;    // Expanded paths for each member
    int validCount = 0;                        // Number of valid expansions
    int invalidCount = 0;                      // Number of invalid expansions
};

/**
 * Controller for CloudCopier feature
 * Bridges between CloudCopierPanel (GUI) and CloudCopier CLI module
 */
class CloudCopierController : public QObject {
    Q_OBJECT

public:
    explicit CloudCopierController(void* megaApi, QObject* parent = nullptr);
    ~CloudCopierController();

    // State queries
    bool hasActiveCopy() const { return m_isCopying; }
    bool isMoveMode() const { return m_moveMode; }
    int getSourceCount() const { return m_sources.size(); }
    int getDestinationCount() const { return m_destinations.size(); }
    int getPendingTaskCount() const;
    int getCompletedTaskCount() const;

    // Template access
    QStringList getTemplateNames() const;
    CopyTemplateInfo getTemplate(const QString& name) const;

    // === Member Mode Support ===
    bool isMemberModeEnabled() const { return m_memberModeEnabled; }
    bool isAllMembersSelected() const { return m_allMembersSelected; }
    QString getSelectedMemberId() const { return m_selectedMemberId; }
    QString getDestinationTemplate() const { return m_destinationTemplate; }
    int getAvailableMemberCount() const { return m_availableMembers.size(); }
    QList<MemberInfo> getAvailableMembers() const;

signals:
    // State change signals
    void sourcesChanged(const QStringList& sources);
    void destinationsChanged(const QStringList& destinations);

    // Task signals
    void tasksClearing();  // Emitted before tasks are cleared for a new operation
    void taskCreated(int taskId, const QString& source, const QString& destination);
    void taskProgress(int taskId, int progress);
    void taskStatusChanged(int taskId, const QString& status);

    // Copy operation signals
    void copyStarted(int totalTasks);
    void copyProgress(int completed, int total, const QString& currentItem, const QString& currentDest);
    void copyCompleted(int successful, int failed, int skipped);
    void copyPaused();
    void copyCancelled();

    // Move mode signal
    void moveModeChanged(bool moveMode);

    // Conflict signal - emitted when a conflict is detected
    void conflictDetected(const CopyConflictInfo& conflict);

    // Template signals
    void templatesChanged();

    // Error signal
    void error(const QString& operation, const QString& message);

    // Preview signal - shows what will be copied before execution
    void previewReady(const QVector<CopyPreviewItem>& previewItems);

    // Validation signals
    void sourcesValidated(const QVector<PathValidationResult>& results);
    void destinationsValidated(const QVector<PathValidationResult>& results);

    // === Member Mode Signals ===
    void memberModeChanged(bool enabled);
    void availableMembersChanged(const QList<MemberInfo>& members);
    void selectedMemberChanged(const QString& memberId, const QString& memberName);
    void allMembersSelectionChanged(bool allSelected);
    void destinationTemplateChanged(const QString& templatePath);
    void templateExpansionReady(const TemplateExpansionPreview& preview);
    void memberTaskCreated(int taskId, const QString& source, const QString& dest,
                           const QString& memberId, const QString& memberName);

public slots:
    // Source management
    void addSource(const QString& remotePath);
    void addSources(const QStringList& remotePaths);
    void removeSource(const QString& remotePath);
    void clearSources();

    // Destination management
    void addDestination(const QString& remotePath);
    void addDestinations(const QStringList& remotePaths);
    void removeDestination(const QString& remotePath);
    void clearDestinations();

    // Template management
    void saveTemplate(const QString& name);
    void loadTemplate(const QString& name);
    void deleteTemplate(const QString& name);

    // Import/Export
    void importDestinationsFromFile(const QString& filePath);
    void exportDestinationsToFile(const QString& filePath);

    // Copy control
    void previewCopy(bool copyContentsOnly = false);  // Generate preview before starting
    void startCopy(bool copyContentsOnly = false, bool skipExisting = true, bool moveMode = false);
    void pauseCopy();
    void resumeCopy();
    void cancelCopy();
    void clearCompletedTasks();

    // Move mode control
    void setMoveMode(bool moveMode);

    // Conflict resolution (called from conflict dialog)
    void resolveConflict(CopyConflictResolution resolution);

    // Utility
    void verifyDestinations();
    void createMissingDestinations();
    void browseRemoteFolder();  // Request to open folder browser

    // Validation - check if paths exist in MEGA cloud
    void validateSources();
    void validateDestinations();

    // Package files into folders (create folder with same name, put file inside)
    void packageSourcesIntoFolders(const QString& destParentPath);

    // === Member Mode Slots ===
    void setMemberMode(bool enabled);
    void selectMember(const QString& memberId);
    void selectAllMembers(bool selectAll);
    void setDestinationTemplate(const QString& templatePath);
    void refreshAvailableMembers();
    void previewTemplateExpansion();
    void startMemberCopy(bool copyContentsOnly = false, bool skipExisting = true);

private slots:
    void onCopyProgress(int completed, int total, const QString& currentItem);
    void onCopyCompleted(int successful, int failed, int skipped);
    void onCopyError(const QString& taskId, const QString& error);

private:
    void createCopyTasks();
    void processNextConflict();
    int generateTaskId();

private:
    void* m_megaApi;
    std::unique_ptr<CloudCopier> m_cloudCopier;

    std::atomic<bool> m_isCopying{false};
    std::atomic<bool> m_isPaused{false};
    std::atomic<bool> m_cancelRequested{false};
    bool m_moveMode = false;  // Move mode (delete source after transfer)

    QStringList m_sources;
    QStringList m_destinations;

    QVector<CopyTaskInfo> m_tasks;
    int m_nextTaskId = 1;

    // Conflict handling
    CopyConflictInfo m_pendingConflict;
    std::atomic<bool> m_hasApplyToAll{false};
    std::atomic<CopyConflictResolution> m_applyToAllResolution{CopyConflictResolution::ASK};

    // Statistics - protected by m_statsMutex
    mutable QMutex m_statsMutex;
    int m_successCount = 0;
    int m_failCount = 0;
    int m_skipCount = 0;

    // Track task completion to avoid duplicate dialogs
    int m_totalTasksStarted = 0;
    int m_tasksCompleted = 0;

    // Templates (loaded from CloudCopier)
    QMap<QString, CopyTemplateInfo> m_templates;

    // === Member Mode State ===
    bool m_memberModeEnabled = false;
    bool m_allMembersSelected = false;
    QString m_selectedMemberId;
    QString m_destinationTemplate;
    QList<MemberInfo> m_availableMembers;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_CLOUDCOPIERCONTROLLER_H
