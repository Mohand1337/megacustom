#ifndef MEGACUSTOM_MEMBERREGISTRY_H
#define MEGACUSTOM_MEMBERREGISTRY_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>

namespace MegaCustom {

/**
 * Member destination paths configuration
 */
struct MemberPaths {
    QString archiveRoot;      // e.g., "/Alen Sultanic - NHB+ - EGBs/3. Icekkk"
    QString nhbCallsPath;     // e.g., "NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025"
    QString fastForwardPath;  // e.g., "Fast Forward⏩"
    QString theoryCallsPath;  // e.g., "2- Theory Calls"
    QString hotSeatsPath;     // e.g., "3- Hotseats"

    // Full paths (computed)
    QString getMonthPath(const QString& month) const {
        return archiveRoot + "/" + nhbCallsPath + "/" + month;
    }
    QString getTheoryCallsFullPath() const {
        return archiveRoot + "/" + fastForwardPath + "/" + theoryCallsPath;
    }
    QString getHotSeatsFullPath() const {
        return archiveRoot + "/" + fastForwardPath + "/" + hotSeatsPath;
    }
};

/**
 * Lightweight pipeline activity tracking per member
 */
struct MemberStatusInfo {
    QString lastWatermarkDate;     // e.g., "2026-02-24 14:30"
    QString lastDistributionDate;  // e.g., "2026-02-24 14:35"
    int watermarkCount = 0;        // Total files watermarked
    int distributionCount = 0;     // Total files distributed

    QJsonObject toJson() const;
    static MemberStatusInfo fromJson(const QJsonObject& obj);
};

/**
 * Member info with all relevant data
 * Extended for Phase 2: watermarking, distribution, WordPress sync
 */
struct MemberInfo {
    QString id;               // Unique identifier (e.g., "EGB001" or "icekkk")
    QString displayName;      // Display name (e.g., "Icekkk")
    int sortOrder = 0;        // Order in list (e.g., 3)
    QString wmFolderPattern;  // Watermark folder pattern (e.g., "Icekkk_*")
    MemberPaths paths;
    bool active = true;       // Whether member is active
    QString notes;            // Optional notes

    // === Phase 2: Contact & Watermark Info ===
    QString email;            // Email address for watermark
    QString ipAddress;        // IP address for watermark
    QString macAddress;       // MAC address for watermark
    QString socialHandle;     // Social media handle

    // === Phase 2: Watermark Configuration ===
    QStringList watermarkFields;     // Fields to include: {"name", "email", "ip"}
    bool useGlobalWatermark = false; // Override with global watermark only

    // === Phase 2: WordPress Integration ===
    QString wpUserId;         // WordPress user ID for sync
    qint64 lastWpSync = 0;    // Unix timestamp of last WP sync

    // === Phase 2: Distribution Folder (direct binding) ===
    QString distributionFolder;      // Direct MEGA folder for distributions (alternative to paths)

    // === Pipeline Status Tracking ===
    MemberStatusInfo pipelineStatus;

    // Timestamps
    qint64 createdAt = 0;
    qint64 updatedAt = 0;

    // Helper: Check if member has distribution folder set
    bool hasDistributionFolder() const { return !distributionFolder.isEmpty(); }

    QJsonObject toJson() const;
    static MemberInfo fromJson(const QJsonObject& obj);
};

/**
 * Path type definition for the global template
 */
struct PathType {
    QString key;          // Internal key (e.g., "archiveRoot")
    QString label;        // Display label (e.g., "Archive Root")
    QString description;  // Description (e.g., "Main member folder")
    QString defaultValue; // Default path value
    bool enabled = true;  // Whether this path type is enabled in template

    QJsonObject toJson() const;
    static PathType fromJson(const QJsonObject& obj);
};

/**
 * Global template for default paths
 * Used when adding new members - they inherit these defaults
 * Now supports dynamic path types that can be enabled/disabled
 */
struct MemberTemplate {
    QString archiveRootPrefix;   // e.g., "/Alen Sultanic - NHB+ - EGBs/"
    QString nhbCallsPath;        // Default NHB path
    QString fastForwardPath;     // Default FF path
    QString theoryCallsPath;     // Default theory calls subpath
    QString hotSeatsPath;        // Default hotseats subpath
    QString wmRootPath;          // e.g., "/latest-wm/"

    // Path types with enable/disable flags
    QList<PathType> pathTypes;

    // Initialize with default path types
    void initDefaultPathTypes();

    // Check if a path type is enabled
    bool isPathTypeEnabled(const QString& key) const;

    // Set path type enabled state
    void setPathTypeEnabled(const QString& key, bool enabled);

    // Get path type by key
    PathType* getPathType(const QString& key);
    const PathType* getPathType(const QString& key) const;

    QJsonObject toJson() const;
    static MemberTemplate fromJson(const QJsonObject& obj);
};

/**
 * Named group of members for quick selection
 */
struct MemberGroup {
    QString name;              // e.g., "NHB2026"
    QStringList memberIds;     // Member IDs in this group
    QString description;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;

    QJsonObject toJson() const;
    static MemberGroup fromJson(const QJsonObject& obj);
};

/**
 * MemberRegistry - manages all members and their paths
 * Stores data in JSON config file
 */
class MemberRegistry : public QObject {
    Q_OBJECT

public:
    static MemberRegistry* instance();

    // Template management
    MemberTemplate getTemplate() const { return m_template; }
    void setTemplate(const MemberTemplate& tmpl);

    // Member management
    QList<MemberInfo> getAllMembers() const;
    QList<MemberInfo> getActiveMembers() const;
    MemberInfo getMember(const QString& id) const;
    bool hasMember(const QString& id) const;

    void addMember(const MemberInfo& member);
    void updateMember(const MemberInfo& member);
    void removeMember(const QString& id);

    // Bulk operations
    void setMembers(const QList<MemberInfo>& members);

    // Watermark folder discovery
    QString findWmFolder(const QString& memberId) const;
    QMap<QString, QString> findAllWmFolders() const;

    // Path helpers
    QString getMonthPath(const QString& memberId, const QString& month) const;
    QString getTheoryCallsPath(const QString& memberId) const;
    QString getHotSeatsPath(const QString& memberId) const;

    // Persistence
    bool load();
    bool save();
    QString configPath() const;

    // Import/Export
    bool exportToFile(const QString& filePath);
    bool importFromFile(const QString& filePath, bool mergeMode = true);

    // === Phase 2: Distribution folder management ===
    void setDistributionFolder(const QString& memberId, const QString& folderPath);
    void clearDistributionFolder(const QString& memberId);
    QList<MemberInfo> getMembersWithDistributionFolders() const;

    // === Phase 2: Watermark configuration ===
    void setWatermarkFields(const QString& memberId, const QStringList& fields);
    void setUseGlobalWatermark(const QString& memberId, bool useGlobal);
    static QStringList availableWatermarkFields();

    // === Phase 2: WordPress sync tracking ===
    void markWordPressSynced(const QString& memberId, const QString& wpUserId = QString());
    QList<MemberInfo> getUnsyncedMembers() const;

    // === Phase 2: CSV Import/Export with extended fields ===
    bool exportToCsv(const QString& filePath);
    bool importFromCsv(const QString& filePath, bool skipHeader = true);

    // === Phase 2: Filter/search ===
    QList<MemberInfo> filterMembers(const QString& searchText,
                                    bool activeOnly = false,
                                    bool withDistributionFolder = false,
                                    bool withEmail = false,
                                    bool withIp = false,
                                    bool missingWmInfo = false) const;

    // === Smart Folder Matching ===
    struct FolderMatch {
        QString folderName;
        QString fullPath;
        QString matchedMemberId;
        QString matchType;  // "pattern", "id", "email", "name", "fuzzy", "manual", "none"
        int confidence;     // 1-5 (5 = highest)
    };

    FolderMatch matchFolderToMember(const QString& folderName) const;
    QList<FolderMatch> matchFoldersToMembers(const QStringList& folderNames) const;

    // === Pipeline Status ===
    void recordWatermark(const QString& memberId, int fileCount);
    void recordDistribution(const QString& memberId, int fileCount);

    // === Member Groups ===
    QList<MemberGroup> getAllGroups() const;
    MemberGroup getGroup(const QString& name) const;
    bool hasGroup(const QString& name) const;
    QStringList getGroupNames() const;
    void addGroup(const MemberGroup& group);
    void updateGroup(const MemberGroup& group);
    void removeGroup(const QString& name);
    QStringList getGroupMemberIds(const QString& groupName) const;
    QStringList getGroupsForMember(const QString& memberId) const;

signals:
    void templateChanged();
    void memberAdded(const QString& id);
    void memberUpdated(const QString& id);
    void memberRemoved(const QString& id);
    void membersReloaded();
    void groupAdded(const QString& name);
    void groupUpdated(const QString& name);
    void groupRemoved(const QString& name);

private:
    explicit MemberRegistry(QObject* parent = nullptr);
    ~MemberRegistry() = default;

    static MemberRegistry* s_instance;

    MemberTemplate m_template;
    QMap<QString, MemberInfo> m_members;
    QMap<QString, MemberGroup> m_groups;

    void initDefaults();
};

} // namespace MegaCustom

#endif // MEGACUSTOM_MEMBERREGISTRY_H
