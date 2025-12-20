#ifndef MEGACUSTOM_ACCOUNTMODELS_H
#define MEGACUSTOM_ACCOUNTMODELS_H

#include <QString>
#include <QStringList>
#include <QColor>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>

namespace MegaCustom {

/**
 * @brief Status of an account for visual indicator badges
 */
enum class AccountStatus {
    Ready,      // Logged in, not active (gray ○)
    Active,     // Currently selected & logged in (green ●)
    Syncing,    // Sync in progress (blue ↻)
    Expired,    // Session expired, needs re-auth (orange ⚠)
    Offline,    // Login failed or disconnected (red ✕)
    Unknown     // Status not yet determined
};

/**
 * @brief Account group for organizing multiple accounts
 *
 * Groups allow users to categorize accounts (e.g., "Work", "Personal", "Backup")
 * with color coding and collapsible UI sections.
 */
struct AccountGroup {
    QString id;           // UUID (e.g., "grp-a1b2c3d4")
    QString name;         // Display name (e.g., "Work", "Personal")
    QColor color;         // Group color for visual identification
    int sortOrder = 0;    // Custom ordering in UI
    bool collapsed = false; // UI state for collapsible sections

    // Generate a new unique ID
    static QString generateId() {
        return QString("grp-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
    }

    // Serialization
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["name"] = name;
        obj["color"] = color.name();
        obj["sortOrder"] = sortOrder;
        obj["collapsed"] = collapsed;
        return obj;
    }

    static AccountGroup fromJson(const QJsonObject& obj) {
        AccountGroup group;
        group.id = obj["id"].toString();
        group.name = obj["name"].toString();
        group.color = QColor(obj["color"].toString());
        group.sortOrder = obj["sortOrder"].toInt(0);
        group.collapsed = obj["collapsed"].toBool(false);
        return group;
    }

    bool isValid() const {
        return !id.isEmpty() && !name.isEmpty();
    }
};

/**
 * @brief Represents a MEGA account with metadata for multi-account management
 *
 * Stores account information including display settings, labels for search,
 * group membership, and storage statistics. Session tokens are stored
 * separately in the OS keychain for security.
 */
struct MegaAccount {
    QString id;             // UUID (e.g., "acc-a1b2c3d4")
    QString email;          // MEGA account email
    QString displayName;    // User-friendly name (e.g., "Work Account")
    QString groupId;        // References AccountGroup::id
    QStringList labels;     // Searchable tags (e.g., ["Client-X", "Archive"])
    QColor color;           // Override group color (empty = inherit from group)
    QString notes;          // User notes about this account
    bool isDefault = false; // Default account on app startup
    qint64 storageUsed = 0;
    qint64 storageTotal = 0;
    QDateTime lastLogin;
    QDateTime lastSync;

    // Generate a new unique ID
    static QString generateId() {
        return QString("acc-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
    }

    // Get first letter for avatar
    QChar avatarLetter() const {
        if (!displayName.isEmpty()) {
            return displayName.at(0).toUpper();
        }
        if (!email.isEmpty()) {
            return email.at(0).toUpper();
        }
        return 'A';
    }

    // Get storage percentage (0-100)
    int storagePercentage() const {
        if (storageTotal <= 0) return 0;
        return static_cast<int>((storageUsed * 100) / storageTotal);
    }

    // Format storage for display (e.g., "75.2 GB / 100 GB")
    QString storageDisplayText() const {
        auto formatSize = [](qint64 bytes) -> QString {
            if (bytes < 1024) return QString("%1 B").arg(bytes);
            if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
            if (bytes < 1024 * 1024 * 1024) return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
            return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
        };
        return QString("%1 / %2").arg(formatSize(storageUsed), formatSize(storageTotal));
    }

    // Check if search query matches this account
    bool matchesSearch(const QString& query) const {
        QString q = query.toLower();
        if (email.toLower().contains(q)) return true;
        if (displayName.toLower().contains(q)) return true;
        for (const QString& label : labels) {
            if (label.toLower().contains(q)) return true;
        }
        if (notes.toLower().contains(q)) return true;
        return false;
    }

    // Serialization
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["email"] = email;
        obj["displayName"] = displayName;
        obj["groupId"] = groupId;
        obj["labels"] = QJsonArray::fromStringList(labels);
        obj["color"] = color.isValid() ? color.name() : QString();
        obj["notes"] = notes;
        obj["isDefault"] = isDefault;
        obj["storageUsed"] = storageUsed;
        obj["storageTotal"] = storageTotal;
        obj["lastLogin"] = lastLogin.toString(Qt::ISODate);
        obj["lastSync"] = lastSync.toString(Qt::ISODate);
        return obj;
    }

    static MegaAccount fromJson(const QJsonObject& obj) {
        MegaAccount account;
        account.id = obj["id"].toString();
        account.email = obj["email"].toString();
        account.displayName = obj["displayName"].toString();
        account.groupId = obj["groupId"].toString();

        QJsonArray labelsArray = obj["labels"].toArray();
        for (const QJsonValue& v : labelsArray) {
            account.labels.append(v.toString());
        }

        QString colorStr = obj["color"].toString();
        if (!colorStr.isEmpty()) {
            account.color = QColor(colorStr);
        }

        account.notes = obj["notes"].toString();
        account.isDefault = obj["isDefault"].toBool(false);
        account.storageUsed = obj["storageUsed"].toVariant().toLongLong();
        account.storageTotal = obj["storageTotal"].toVariant().toLongLong();
        account.lastLogin = QDateTime::fromString(obj["lastLogin"].toString(), Qt::ISODate);
        account.lastSync = QDateTime::fromString(obj["lastSync"].toString(), Qt::ISODate);
        return account;
    }

    bool isValid() const {
        return !id.isEmpty() && !email.isEmpty();
    }
};

/**
 * @brief Represents a cross-account file transfer operation
 *
 * Tracks copy/move operations between different MEGA accounts,
 * including progress, status, and error information for the transfer log.
 */
struct CrossAccountTransfer {
    QString id;             // UUID (e.g., "xfr-a1b2c3d4")
    QDateTime timestamp;    // When transfer was initiated

    // Source
    QString sourceAccountId;
    QString sourceAccountEmail;  // Cached for display
    QString sourcePath;

    // Target
    QString targetAccountId;
    QString targetAccountEmail;  // Cached for display
    QString targetPath;

    // Operation type
    enum Operation {
        Copy,
        Move
    };
    Operation operation = Copy;

    // Status
    enum Status {
        Pending,
        InProgress,
        Completed,
        Failed,
        Cancelled
    };
    Status status = Pending;

    // Progress
    qint64 bytesTransferred = 0;
    qint64 bytesTotal = 0;
    int filesTransferred = 0;
    int filesTotal = 0;

    // Timing
    QDateTime startTime;
    QDateTime endTime;

    // Error info
    QString errorMessage;
    int errorCode = 0;
    int retryCount = 0;
    bool canRetry = true;

    // Generate a new unique ID
    static QString generateId() {
        return QString("xfr-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
    }

    // Get progress percentage (0-100)
    int progressPercentage() const {
        if (bytesTotal <= 0) return 0;
        return static_cast<int>((bytesTransferred * 100) / bytesTotal);
    }

    // Get status string for display
    QString statusString() const {
        switch (status) {
            case Pending: return "Pending";
            case InProgress: return "In Progress";
            case Completed: return "Completed";
            case Failed: return "Failed";
            case Cancelled: return "Cancelled";
        }
        return "Unknown";
    }

    // Get operation string for display
    QString operationString() const {
        return operation == Copy ? "Copy" : "Move";
    }

    // Get duration in seconds
    qint64 durationSeconds() const {
        if (!startTime.isValid()) return 0;
        QDateTime end = endTime.isValid() ? endTime : QDateTime::currentDateTime();
        return startTime.secsTo(end);
    }

    // Format duration for display
    QString durationString() const {
        qint64 secs = durationSeconds();
        if (secs < 60) return QString("%1s").arg(secs);
        if (secs < 3600) return QString("%1m %2s").arg(secs / 60).arg(secs % 60);
        return QString("%1h %2m").arg(secs / 3600).arg((secs % 3600) / 60);
    }

    // Format bytes transferred for display
    QString progressString() const {
        auto formatSize = [](qint64 bytes) -> QString {
            if (bytes < 1024) return QString("%1 B").arg(bytes);
            if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
            if (bytes < 1024 * 1024 * 1024) return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
            return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
        };
        return QString("%1 / %2").arg(formatSize(bytesTransferred), formatSize(bytesTotal));
    }

    // Serialization for SQLite storage
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["timestamp"] = timestamp.toString(Qt::ISODate);
        obj["sourceAccountId"] = sourceAccountId;
        obj["sourceAccountEmail"] = sourceAccountEmail;
        obj["sourcePath"] = sourcePath;
        obj["targetAccountId"] = targetAccountId;
        obj["targetAccountEmail"] = targetAccountEmail;
        obj["targetPath"] = targetPath;
        obj["operation"] = static_cast<int>(operation);
        obj["status"] = static_cast<int>(status);
        obj["bytesTransferred"] = bytesTransferred;
        obj["bytesTotal"] = bytesTotal;
        obj["filesTransferred"] = filesTransferred;
        obj["filesTotal"] = filesTotal;
        obj["startTime"] = startTime.toString(Qt::ISODate);
        obj["endTime"] = endTime.toString(Qt::ISODate);
        obj["errorMessage"] = errorMessage;
        obj["errorCode"] = errorCode;
        obj["retryCount"] = retryCount;
        obj["canRetry"] = canRetry;
        return obj;
    }

    static CrossAccountTransfer fromJson(const QJsonObject& obj) {
        CrossAccountTransfer transfer;
        transfer.id = obj["id"].toString();
        transfer.timestamp = QDateTime::fromString(obj["timestamp"].toString(), Qt::ISODate);
        transfer.sourceAccountId = obj["sourceAccountId"].toString();
        transfer.sourceAccountEmail = obj["sourceAccountEmail"].toString();
        transfer.sourcePath = obj["sourcePath"].toString();
        transfer.targetAccountId = obj["targetAccountId"].toString();
        transfer.targetAccountEmail = obj["targetAccountEmail"].toString();
        transfer.targetPath = obj["targetPath"].toString();
        transfer.operation = static_cast<Operation>(obj["operation"].toInt(0));
        transfer.status = static_cast<Status>(obj["status"].toInt(0));
        transfer.bytesTransferred = obj["bytesTransferred"].toVariant().toLongLong();
        transfer.bytesTotal = obj["bytesTotal"].toVariant().toLongLong();
        transfer.filesTransferred = obj["filesTransferred"].toInt(0);
        transfer.filesTotal = obj["filesTotal"].toInt(0);
        transfer.startTime = QDateTime::fromString(obj["startTime"].toString(), Qt::ISODate);
        transfer.endTime = QDateTime::fromString(obj["endTime"].toString(), Qt::ISODate);
        transfer.errorMessage = obj["errorMessage"].toString();
        transfer.errorCode = obj["errorCode"].toInt(0);
        transfer.retryCount = obj["retryCount"].toInt(0);
        transfer.canRetry = obj["canRetry"].toBool(true);
        return transfer;
    }

    bool isValid() const {
        return !id.isEmpty() && !sourceAccountId.isEmpty() && !targetAccountId.isEmpty();
    }
};

/**
 * @brief Settings for account management
 */
struct AccountSettings {
    int maxCachedSessions = 5;      // Maximum number of cached MegaApi sessions
    int sessionRefreshInterval = 3600; // Seconds between session refresh checks
    bool autoRestoreSession = true; // Auto-restore last session on startup
    bool showStorageInSwitcher = true; // Show storage bars in account switcher

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["maxCachedSessions"] = maxCachedSessions;
        obj["sessionRefreshInterval"] = sessionRefreshInterval;
        obj["autoRestoreSession"] = autoRestoreSession;
        obj["showStorageInSwitcher"] = showStorageInSwitcher;
        return obj;
    }

    static AccountSettings fromJson(const QJsonObject& obj) {
        AccountSettings settings;
        settings.maxCachedSessions = obj["maxCachedSessions"].toInt(5);
        settings.sessionRefreshInterval = obj["sessionRefreshInterval"].toInt(3600);
        settings.autoRestoreSession = obj["autoRestoreSession"].toBool(true);
        settings.showStorageInSwitcher = obj["showStorageInSwitcher"].toBool(true);
        return settings;
    }
};

} // namespace MegaCustom

#endif // MEGACUSTOM_ACCOUNTMODELS_H
