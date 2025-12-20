#ifndef MEGACUSTOM_SMARTSYNCCONTROLLER_H
#define MEGACUSTOM_SMARTSYNCCONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QVector>
#include <QMutex>
#include <memory>
#include <atomic>

namespace MegaCustom {

/**
 * @brief Sync direction configuration
 */
enum class SyncDirection {
    BIDIRECTIONAL,      // Sync changes in both directions
    LOCAL_TO_REMOTE,    // Upload only
    REMOTE_TO_LOCAL     // Download only
};

/**
 * @brief Conflict resolution strategy
 */
enum class ConflictResolution {
    ASK_USER,           // Always ask user
    KEEP_NEWER,         // Keep file with newer timestamp
    KEEP_LARGER,        // Keep larger file
    KEEP_LOCAL,         // Always prefer local
    KEEP_REMOTE,        // Always prefer remote
    KEEP_BOTH           // Rename and keep both versions
};

/**
 * @brief Represents a sync profile configuration
 */
struct SyncProfile {
    QString id;
    QString name;
    QString localPath;
    QString remotePath;
    SyncDirection direction = SyncDirection::BIDIRECTIONAL;
    ConflictResolution conflictResolution = ConflictResolution::ASK_USER;

    // Filters
    QString includePatterns;
    QString excludePatterns;
    bool syncHiddenFiles = false;
    bool syncTempFiles = false;
    bool deleteOrphans = false;
    bool verifyAfterSync = true;

    // Schedule
    bool autoSyncEnabled = false;
    int autoSyncIntervalMinutes = 60;
    QDateTime lastSyncTime;

    // Status
    bool isActive = false;
    bool isPaused = false;
};

/**
 * @brief Represents a file that needs to be synced
 */
struct SyncAction {
    enum class ActionType {
        UPLOAD,         // Upload local to remote
        DOWNLOAD,       // Download remote to local
        DELETE_LOCAL,   // Delete local file
        DELETE_REMOTE,  // Delete remote file
        CONFLICT,       // Conflict needs resolution
        SKIP            // Skip (no change needed)
    };

    int id = 0;
    QString filePath;
    QString localPath;
    QString remotePath;
    ActionType actionType = ActionType::SKIP;
    qint64 localSize = 0;
    qint64 remoteSize = 0;
    QDateTime localModTime;
    QDateTime remoteModTime;
    QString status;
};

/**
 * @brief Represents a conflict that needs resolution
 */
struct SyncConflict {
    int id = 0;
    QString filePath;
    QString localPath;
    QString remotePath;
    qint64 localSize = 0;
    qint64 remoteSize = 0;
    QDateTime localModTime;
    QDateTime remoteModTime;
    QString reason;
    bool resolved = false;
    QString resolution;
};

/**
 * @brief Sync history entry
 */
struct SyncHistoryEntry {
    QDateTime timestamp;
    QString profileName;
    int filesUploaded = 0;
    int filesDownloaded = 0;
    int filesDeleted = 0;
    int conflicts = 0;
    int errors = 0;
    QString status;
};

/**
 * Controller for SmartSync feature
 * Bridges between SmartSyncPanel (GUI) and sync operations
 */
class SmartSyncController : public QObject {
    Q_OBJECT

public:
    explicit SmartSyncController(void* megaApi, QObject* parent = nullptr);
    ~SmartSyncController();

    // State queries
    bool isSyncing() const { return m_isSyncing; }
    int getProfileCount() const { return m_profiles.size(); }

signals:
    // Profile signals
    void profilesLoaded(int count);
    void profileCreated(const QString& id, const QString& name);
    void profileUpdated(const QString& id);
    void profileDeleted(const QString& id);

    // Analysis signals
    void analysisStarted(const QString& profileId);
    void analysisProgress(const QString& profileId, int current, int total);
    void analysisComplete(const QString& profileId, int uploads, int downloads,
                          int deletions, int conflicts);

    // Sync signals
    void syncStarted(const QString& profileId);
    void syncProgress(const QString& profileId, const QString& currentFile,
                      int filesCompleted, int totalFiles,
                      qint64 bytesTransferred, qint64 totalBytes);
    void syncComplete(const QString& profileId, bool success,
                      int filesUploaded, int filesDownloaded, int errors);
    void syncPaused(const QString& profileId);
    void syncResumed(const QString& profileId);
    void syncCancelled(const QString& profileId);

    // Conflict signals
    void conflictDetected(const SyncConflict& conflict);
    void conflictResolved(int conflictId, const QString& resolution);
    void conflictsCleared(const QString& profileId);

    // Preview/actions signals
    void actionsReady(const QString& profileId, const QVector<SyncAction>& actions);

    // Error signal
    void error(const QString& operation, const QString& message);

public slots:
    // Profile management
    void loadProfiles();
    void saveProfiles();
    void createProfile(const QString& name, const QString& localPath,
                       const QString& remotePath);
    void updateProfile(const QString& profileId, const SyncProfile& profile);
    void deleteProfile(const QString& profileId);
    SyncProfile* getProfile(const QString& profileId);
    QVector<SyncProfile> getAllProfiles() const;

    // Profile configuration
    void setDirection(const QString& profileId, SyncDirection direction);
    void setConflictResolution(const QString& profileId, ConflictResolution resolution);
    void setFilters(const QString& profileId, const QString& include, const QString& exclude);
    void setAutoSync(const QString& profileId, bool enabled, int intervalMinutes);

    // Sync operations
    void analyzeProfile(const QString& profileId);
    void startSync(const QString& profileId);
    void pauseSync(const QString& profileId);
    void resumeSync(const QString& profileId);
    void stopSync(const QString& profileId);

    // Conflict handling
    void resolveConflict(int conflictId, const QString& resolution);
    void resolveAllConflicts(const QString& profileId, ConflictResolution strategy);
    QVector<SyncConflict> getConflicts(const QString& profileId) const;

    // History
    QVector<SyncHistoryEntry> getHistory(const QString& profileId, int maxEntries = 50) const;

    // Import/Export
    void exportProfile(const QString& profileId, const QString& filePath);
    void importProfile(const QString& filePath);

private:
    void performSync(SyncProfile& profile);
    void addHistoryEntry(const QString& profileId, const SyncHistoryEntry& entry);
    QString generateProfileId();

private:
    void* m_megaApi;
    std::atomic<bool> m_isSyncing{false};
    std::atomic<bool> m_isPaused{false};
    std::atomic<bool> m_cancelRequested{false};
    QString m_currentSyncProfileId;

    QVector<SyncProfile> m_profiles;
    QVector<SyncAction> m_pendingActions;
    QVector<SyncConflict> m_conflicts;
    QHash<QString, QVector<SyncHistoryEntry>> m_history;

    mutable QMutex m_dataMutex;  // Protects m_profiles, m_pendingActions, m_conflicts

    int m_nextConflictId = 1;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_SMARTSYNCCONTROLLER_H
