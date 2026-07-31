#include "MemberRegistry.h"
#include "Settings.h"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStandardPaths>
#include <QDebug>
#include <QDateTime>
#include <QTextStream>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QLockFile>
#include <QSet>

namespace MegaCustom {

namespace {

constexpr int kRegistryMigrationVersion = 2;
constexpr int kBackupGenerations = 5;

QString cleanAbsolutePath(const QString& path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QByteArray fileHash(const QByteArray& bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

bool readRegistryObject(const QString& path, QJsonObject* root, QByteArray* bytes,
                        QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QString("Could not read %1: %2").arg(path, file.errorString());
        }
        return false;
    }

    const QByteArray contents = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        if (error) {
            *error = QString("Could not finish reading %1: %2")
                .arg(path, file.errorString());
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(contents, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) {
            *error = QString("Invalid member registry %1 at byte %2: %3")
                .arg(path)
                .arg(parseError.offset)
                .arg(parseError.errorString());
        }
        return false;
    }
    if (!document.isObject()) {
        if (error) *error = QString("Invalid member registry %1: the root must be a JSON object.").arg(path);
        return false;
    }

    const QJsonObject object = document.object();
    if ((object.contains("members") && !object.value("members").isArray())
        || (object.contains("groups") && !object.value("groups").isArray())
        || (object.contains("template") && !object.value("template").isObject())) {
        if (error) {
            *error = QString("Member registry %1 has an invalid members, groups, or template field.")
                .arg(path);
        }
        return false;
    }

    QSet<QString> memberIds;
    for (const QJsonValue& value : object.value("members").toArray()) {
        const QString id = value.toObject().value("id").toString().trimmed();
        if (!value.isObject() || id.isEmpty()) {
            if (error) {
                *error = QString("Member registry %1 contains a member without a valid ID.")
                    .arg(path);
            }
            return false;
        }
        if (memberIds.contains(id)) {
            if (error) {
                *error = QString("Member registry %1 contains the duplicate member ID '%2'.")
                    .arg(path, id);
            }
            return false;
        }
        memberIds.insert(id);
    }
    QSet<QString> groupNames;
    for (const QJsonValue& value : object.value("groups").toArray()) {
        const QJsonObject group = value.toObject();
        const QString name = group.value("name").toString().trimmed();
        if (!value.isObject() || name.isEmpty()) {
            if (error) {
                *error = QString("Member registry %1 contains a group without a valid name.")
                    .arg(path);
            }
            return false;
        }
        if (groupNames.contains(name)) {
            if (error) {
                *error = QString("Member registry %1 contains the duplicate group name '%2'.")
                    .arg(path, name);
            }
            return false;
        }
        if (group.contains("memberIds") && !group.value("memberIds").isArray()) {
            if (error) {
                *error = QString("Member registry %1 has an invalid memberIds field in group '%2'.")
                    .arg(path, name);
            }
            return false;
        }
        groupNames.insert(name);
    }

    if (root) *root = object;
    if (bytes) *bytes = contents;
    return true;
}

bool writeBytesAtomically(const QString& path, const QByteArray& bytes, QString* error) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error) *error = QString("Could not create the registry directory for %1.").arg(path);
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QString("Could not open %1 for writing: %2").arg(path, file.errorString());
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        if (error) *error = QString("Could not write all data to %1: %2").arg(path, file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error) *error = QString("Could not commit %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

QMap<QString, QJsonObject> objectsByKey(const QJsonArray& values, const QString& key) {
    QMap<QString, QJsonObject> result;
    for (const QJsonValue& value : values) {
        const QJsonObject object = value.toObject();
        const QString objectKey = object.value(key).toString();
        if (!objectKey.isEmpty()) result[objectKey] = object;
    }
    return result;
}

qint64 updatedAt(const QJsonObject& object) {
    return object.value("updatedAt").toInteger(0);
}

qint64 latestRegistryTimestamp(const QJsonObject& root) {
    qint64 latest = 0;
    for (const QJsonValue& value : root.value("members").toArray()) {
        const QJsonObject object = value.toObject();
        latest = qMax(latest, qMax(updatedAt(object), object.value("createdAt").toInteger(0)));
    }
    for (const QJsonValue& value : root.value("groups").toArray()) {
        const QJsonObject object = value.toObject();
        latest = qMax(latest, qMax(updatedAt(object), object.value("createdAt").toInteger(0)));
    }
    return latest;
}

QJsonArray mergeMembers(const QJsonArray& activeValues, const QJsonArray& legacyValues) {
    QMap<QString, QJsonObject> merged = objectsByKey(activeValues, "id");
    const QMap<QString, QJsonObject> legacy = objectsByKey(legacyValues, "id");
    for (auto it = legacy.constBegin(); it != legacy.constEnd(); ++it) {
        if (!merged.contains(it.key()) || updatedAt(it.value()) > updatedAt(merged.value(it.key()))) {
            merged[it.key()] = it.value();
        }
    }

    QJsonArray result;
    for (const QJsonObject& object : merged) result.append(object);
    return result;
}

QJsonArray mergeGroups(const QJsonArray& activeValues, const QJsonArray& legacyValues) {
    QMap<QString, QJsonObject> active = objectsByKey(activeValues, "name");
    const QMap<QString, QJsonObject> legacy = objectsByKey(legacyValues, "name");
    for (auto it = legacy.constBegin(); it != legacy.constEnd(); ++it) {
        if (!active.contains(it.key())) {
            active[it.key()] = it.value();
            continue;
        }

        const QJsonObject activeObject = active.value(it.key());
        const qint64 activeUpdatedAt = updatedAt(activeObject);
        const qint64 legacyUpdatedAt = updatedAt(it.value());
        QJsonObject winner = legacyUpdatedAt > activeUpdatedAt ? it.value() : activeObject;
        if (activeUpdatedAt == legacyUpdatedAt) {
            QStringList memberIds;
            for (const QJsonValue& value : activeObject.value("memberIds").toArray()) {
                const QString id = value.toString();
                if (!id.isEmpty() && !memberIds.contains(id)) memberIds.append(id);
            }
            for (const QJsonValue& value : it.value().value("memberIds").toArray()) {
                const QString id = value.toString();
                if (!id.isEmpty() && !memberIds.contains(id)) memberIds.append(id);
            }
            QJsonArray ids;
            for (const QString& id : memberIds) ids.append(id);
            winner["memberIds"] = ids;
        }
        active[it.key()] = winner;
    }

    QJsonArray result;
    for (const QJsonObject& object : active) result.append(object);
    return result;
}

QJsonObject mergeRegistryRoots(const QJsonObject& active, const QJsonObject& legacy) {
    QJsonObject merged = active;
    for (auto it = legacy.constBegin(); it != legacy.constEnd(); ++it) {
        if (!merged.contains(it.key())) merged.insert(it.key(), it.value());
    }
    merged["members"] = mergeMembers(active.value("members").toArray(),
                                      legacy.value("members").toArray());
    merged["groups"] = mergeGroups(active.value("groups").toArray(),
                                    legacy.value("groups").toArray());
    if (legacy.contains("template")
        && latestRegistryTimestamp(legacy) > latestRegistryTimestamp(active)) {
        merged["template"] = legacy.value("template");
    }
    return merged;
}

QJsonObject replaceKnownFields(QJsonObject existing, const QJsonObject& current,
                               const QStringList& knownFields) {
    for (const QString& field : knownFields) existing.remove(field);
    for (auto it = current.constBegin(); it != current.constEnd(); ++it) {
        existing.insert(it.key(), it.value());
    }
    return existing;
}

} // namespace

MemberRegistry* MemberRegistry::s_instance = nullptr;

// MemberStatusInfo JSON serialization
QJsonObject MemberStatusInfo::toJson() const {
    QJsonObject obj;
    if (!lastWatermarkDate.isEmpty()) obj["lastWatermarkDate"] = lastWatermarkDate;
    if (!lastDistributionDate.isEmpty()) obj["lastDistributionDate"] = lastDistributionDate;
    if (watermarkCount > 0) obj["watermarkCount"] = watermarkCount;
    if (distributionCount > 0) obj["distributionCount"] = distributionCount;
    return obj;
}

MemberStatusInfo MemberStatusInfo::fromJson(const QJsonObject& obj) {
    MemberStatusInfo s;
    s.lastWatermarkDate = obj["lastWatermarkDate"].toString();
    s.lastDistributionDate = obj["lastDistributionDate"].toString();
    s.watermarkCount = obj["watermarkCount"].toInt(0);
    s.distributionCount = obj["distributionCount"].toInt(0);
    return s;
}

// MemberInfo JSON serialization
QJsonObject MemberInfo::toJson() const {
    QJsonObject obj;
    obj["id"] = id;
    obj["displayName"] = displayName;
    obj["sortOrder"] = sortOrder;
    obj["wmFolderPattern"] = wmFolderPattern;
    obj["active"] = active;
    obj["notes"] = notes;

    QJsonObject pathsObj;
    pathsObj["archiveRoot"] = paths.archiveRoot;
    pathsObj["nhbCallsPath"] = paths.nhbCallsPath;
    pathsObj["fastForwardPath"] = paths.fastForwardPath;
    pathsObj["theoryCallsPath"] = paths.theoryCallsPath;
    pathsObj["hotSeatsPath"] = paths.hotSeatsPath;
    obj["paths"] = pathsObj;

    // Phase 2: Contact & watermark info
    if (!email.isEmpty()) obj["email"] = email;
    if (!ipAddress.isEmpty()) obj["ipAddress"] = ipAddress;
    if (!macAddress.isEmpty()) obj["macAddress"] = macAddress;
    if (!socialHandle.isEmpty()) obj["socialHandle"] = socialHandle;

    // Phase 2: Watermark configuration
    if (!watermarkFields.isEmpty()) {
        QJsonArray wmFields;
        for (const QString& f : watermarkFields) wmFields.append(f);
        obj["watermarkFields"] = wmFields;
    }
    if (useGlobalWatermark) obj["useGlobalWatermark"] = true;

    // Phase 2: WordPress integration
    if (!wpUserId.isEmpty()) obj["wpUserId"] = wpUserId;
    if (lastWpSync > 0) obj["lastWpSync"] = lastWpSync;

    // Phase 2: Distribution folder
    if (!distributionFolder.isEmpty()) obj["distributionFolder"] = distributionFolder;

    // Pipeline status
    QJsonObject statusObj = pipelineStatus.toJson();
    if (!statusObj.isEmpty()) obj["pipelineStatus"] = statusObj;

    // Timestamps
    if (createdAt > 0) obj["createdAt"] = createdAt;
    if (updatedAt > 0) obj["updatedAt"] = updatedAt;

    return obj;
}

MemberInfo MemberInfo::fromJson(const QJsonObject& obj) {
    MemberInfo info;
    info.id = obj["id"].toString();
    info.displayName = obj["displayName"].toString();
    info.sortOrder = obj["sortOrder"].toInt();
    info.wmFolderPattern = obj["wmFolderPattern"].toString();
    info.active = obj["active"].toBool(true);
    info.notes = obj["notes"].toString();

    QJsonObject pathsObj = obj["paths"].toObject();
    info.paths.archiveRoot = pathsObj["archiveRoot"].toString();
    info.paths.nhbCallsPath = pathsObj["nhbCallsPath"].toString();
    info.paths.fastForwardPath = pathsObj["fastForwardPath"].toString();
    info.paths.theoryCallsPath = pathsObj["theoryCallsPath"].toString();
    info.paths.hotSeatsPath = pathsObj["hotSeatsPath"].toString();

    // Phase 2: Contact & watermark info
    info.email = obj["email"].toString();
    info.ipAddress = obj["ipAddress"].toString();
    info.macAddress = obj["macAddress"].toString();
    info.socialHandle = obj["socialHandle"].toString();

    // Phase 2: Watermark configuration
    QJsonArray wmFields = obj["watermarkFields"].toArray();
    for (const QJsonValue& v : wmFields) {
        info.watermarkFields.append(v.toString());
    }
    info.useGlobalWatermark = obj["useGlobalWatermark"].toBool(false);

    // Phase 2: WordPress integration
    info.wpUserId = obj["wpUserId"].toString();
    info.lastWpSync = obj["lastWpSync"].toInteger(0);

    // Phase 2: Distribution folder
    info.distributionFolder = obj["distributionFolder"].toString();

    // Pipeline status
    if (obj.contains("pipelineStatus")) {
        info.pipelineStatus = MemberStatusInfo::fromJson(obj["pipelineStatus"].toObject());
    }

    // Timestamps
    info.createdAt = obj["createdAt"].toInteger(0);
    info.updatedAt = obj["updatedAt"].toInteger(0);

    return info;
}

// PathType JSON serialization
QJsonObject PathType::toJson() const {
    QJsonObject obj;
    obj["key"] = key;
    obj["label"] = label;
    obj["description"] = description;
    obj["defaultValue"] = defaultValue;
    obj["enabled"] = enabled;
    return obj;
}

PathType PathType::fromJson(const QJsonObject& obj) {
    PathType pt;
    pt.key = obj["key"].toString();
    pt.label = obj["label"].toString();
    pt.description = obj["description"].toString();
    pt.defaultValue = obj["defaultValue"].toString();
    pt.enabled = obj["enabled"].toBool(true);
    return pt;
}

// MemberGroup JSON serialization
QJsonObject MemberGroup::toJson() const {
    QJsonObject obj;
    obj["name"] = name;
    obj["description"] = description;
    QJsonArray ids;
    for (const QString& id : memberIds) ids.append(id);
    obj["memberIds"] = ids;
    if (createdAt > 0) obj["createdAt"] = createdAt;
    if (updatedAt > 0) obj["updatedAt"] = updatedAt;
    return obj;
}

MemberGroup MemberGroup::fromJson(const QJsonObject& obj) {
    MemberGroup group;
    group.name = obj["name"].toString();
    group.description = obj["description"].toString();
    QJsonArray ids = obj["memberIds"].toArray();
    for (const QJsonValue& v : ids) {
        group.memberIds.append(v.toString());
    }
    group.createdAt = obj["createdAt"].toInteger(0);
    group.updatedAt = obj["updatedAt"].toInteger(0);
    return group;
}

// MemberTemplate methods
void MemberTemplate::initDefaultPathTypes() {
    pathTypes.clear();
    pathTypes.append({"archiveRoot", "Archive Root", "Main member folder", "/Alen Sultanic - NHB+ - EGBs/X. MemberName", true});
    pathTypes.append({"nhbCallsPath", "NHB Calls Path", "Monthly calls archive", "NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025", true});
    pathTypes.append({"fastForwardPath", "Fast Forward Path", "FF content folder", "Fast Forward⏩", true});
    pathTypes.append({"theoryCallsPath", "Theory Calls Path", "Under Fast Forward", "2- Theory Calls", true});
    pathTypes.append({"hotSeatsPath", "Hot Seats Path", "Under Fast Forward", "3- Hotseats", true});
}

bool MemberTemplate::isPathTypeEnabled(const QString& key) const {
    for (const PathType& pt : pathTypes) {
        if (pt.key == key) return pt.enabled;
    }
    return true; // Default to enabled if not found
}

void MemberTemplate::setPathTypeEnabled(const QString& key, bool enabled) {
    for (PathType& pt : pathTypes) {
        if (pt.key == key) {
            pt.enabled = enabled;
            return;
        }
    }
}

PathType* MemberTemplate::getPathType(const QString& key) {
    for (PathType& pt : pathTypes) {
        if (pt.key == key) return &pt;
    }
    return nullptr;
}

const PathType* MemberTemplate::getPathType(const QString& key) const {
    for (const PathType& pt : pathTypes) {
        if (pt.key == key) return &pt;
    }
    return nullptr;
}

// MemberTemplate JSON serialization
QJsonObject MemberTemplate::toJson() const {
    QJsonObject obj;
    obj["archiveRootPrefix"] = archiveRootPrefix;
    obj["nhbCallsPath"] = nhbCallsPath;
    obj["fastForwardPath"] = fastForwardPath;
    obj["theoryCallsPath"] = theoryCallsPath;
    obj["hotSeatsPath"] = hotSeatsPath;
    obj["wmRootPath"] = wmRootPath;

    // Save path types
    QJsonArray pathTypesArray;
    for (const PathType& pt : pathTypes) {
        pathTypesArray.append(pt.toJson());
    }
    obj["pathTypes"] = pathTypesArray;

    return obj;
}

MemberTemplate MemberTemplate::fromJson(const QJsonObject& obj) {
    MemberTemplate tmpl;
    tmpl.archiveRootPrefix = obj["archiveRootPrefix"].toString();
    tmpl.nhbCallsPath = obj["nhbCallsPath"].toString();
    tmpl.fastForwardPath = obj["fastForwardPath"].toString();
    tmpl.theoryCallsPath = obj["theoryCallsPath"].toString();
    tmpl.hotSeatsPath = obj["hotSeatsPath"].toString();
    tmpl.wmRootPath = obj["wmRootPath"].toString();

    // Load path types
    if (obj.contains("pathTypes")) {
        QJsonArray arr = obj["pathTypes"].toArray();
        for (const QJsonValue& val : arr) {
            tmpl.pathTypes.append(PathType::fromJson(val.toObject()));
        }
    } else {
        // Initialize defaults if not present
        tmpl.initDefaultPathTypes();
    }

    return tmpl;
}

// MemberRegistry implementation
MemberRegistry* MemberRegistry::instance() {
    if (!s_instance) {
        s_instance = new MemberRegistry();
        s_instance->load();
    }
    return s_instance;
}

MemberRegistry::MemberRegistry(QObject* parent)
    : QObject(parent)
{
    initDefaults();
}

void MemberRegistry::initDefaults() {
    // Set default template based on your current structure
    m_template.archiveRootPrefix = "/Alen Sultanic - NHB+ - EGBs/";
    m_template.nhbCallsPath = "NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025";
    m_template.fastForwardPath = "Fast Forward⏩";
    m_template.theoryCallsPath = "2- Theory Calls";
    m_template.hotSeatsPath = "3- Hotseats";
    m_template.wmRootPath = "/latest-wm/";

    // Initialize path types
    m_template.initDefaultPathTypes();
}

QString MemberRegistry::configPath() const {
    const QString configDir = Settings::instance().configDirectory();
    return QDir(configDir).filePath("members.json");
}

QString MemberRegistry::legacyConfigPath() const {
    const QString overrideDirectory = QString::fromLocal8Bit(qgetenv("MEGACUSTOM_LEGACY_CONFIG_DIR"));
    if (!overrideDirectory.trimmed().isEmpty()) {
        return QDir(overrideDirectory).filePath("members.json");
    }
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath("members.json");
}

void MemberRegistry::reportPersistenceError(const QString& message) {
    m_lastPersistenceError = message;
    qWarning().noquote() << message;
    emit persistenceError(message);
}

void MemberRegistry::clearPersistenceError() {
    m_lastPersistenceError.clear();
}

bool MemberRegistry::migrateLegacyRegistry(const QString& targetPath) {
    const QString target = cleanAbsolutePath(targetPath);
    const QString legacy = cleanAbsolutePath(legacyConfigPath());
    if (target == legacy) return true;

    const QString markerPath = target + ".migration-v2.json";
    if (QFileInfo::exists(markerPath) && QFileInfo::exists(target)) return true;

    bool targetExists = QFileInfo::exists(target);
    bool legacyExists = QFileInfo::exists(legacy);
    if (!targetExists && !legacyExists) return true;

    QLockFile lock(target + ".lock");
    lock.setStaleLockTime(30000);
    if (!lock.tryLock(5000)) {
        reportPersistenceError(QString(
            "Member data is busy in another process. Close other MegaCustom windows and try again. Registry: %1")
            .arg(target));
        return false;
    }

    targetExists = QFileInfo::exists(target);
    legacyExists = QFileInfo::exists(legacy);
    if (QFileInfo::exists(markerPath) && targetExists) return true;

    QJsonObject targetRoot;
    QJsonObject legacyRoot;
    QByteArray targetBytes;
    QByteArray legacyBytes;
    QString error;

    if (targetExists && !readRegistryObject(target, &targetRoot, &targetBytes, &error)) {
        reportPersistenceError(error + " The file was left untouched; member changes are disabled.");
        return false;
    }
    if (legacyExists && !readRegistryObject(legacy, &legacyRoot, &legacyBytes, &error)) {
        reportPersistenceError(error + " The registry copies were left untouched; member changes are disabled.");
        return false;
    }

    const QString stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmsszzz");
    if (targetExists) {
        const QString backup = target + ".pre-migration-v2-active-" + stamp + ".bak";
        if (!QFile::copy(target, backup)) {
            reportPersistenceError(QString(
                "Could not create the pre-migration member backup %1. The registry was left untouched.")
                .arg(backup));
            return false;
        }
    }
    if (legacyExists) {
        const QString backup = target + ".pre-migration-v2-legacy-" + stamp + ".bak";
        if (!QFile::copy(legacy, backup)) {
            reportPersistenceError(QString(
                "Could not create the legacy member backup %1. The registry was left untouched.")
                .arg(backup));
            return false;
        }
    }

    QJsonObject selectedRoot;
    QString action;
    if (!targetExists) {
        selectedRoot = legacyRoot;
        action = "copied_legacy";
    } else if (!legacyRoot.isEmpty()) {
        selectedRoot = mergeRegistryRoots(targetRoot, legacyRoot);
        action = "merged_active_and_legacy";
    } else {
        selectedRoot = targetRoot;
        action = "kept_active";
    }

    const QByteArray selectedBytes = QJsonDocument(selectedRoot).toJson(QJsonDocument::Indented);
    if ((!targetExists || selectedBytes != targetBytes)
        && !writeBytesAtomically(target, selectedBytes, &error)) {
        reportPersistenceError(error + " The pre-migration backups were preserved.");
        return false;
    }

    QJsonObject marker;
    marker["version"] = kRegistryMigrationVersion;
    marker["completedAtUtc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    marker["action"] = action;
    marker["activePath"] = target;
    marker["legacyPath"] = legacy;
    marker["memberCount"] = selectedRoot.value("members").toArray().size();
    marker["groupCount"] = selectedRoot.value("groups").toArray().size();
    QString markerError;
    if (!writeBytesAtomically(markerPath,
                              QJsonDocument(marker).toJson(QJsonDocument::Indented),
                              &markerError)) {
        qWarning().noquote() << markerError
            << "The registry migration succeeded, but may be checked again next launch.";
    }

    qInfo() << "Member registry migration" << action
            << "members" << marker.value("memberCount").toInt()
            << "groups" << marker.value("groupCount").toInt();
    return true;
}

bool MemberRegistry::load() {
    const QString path = cleanAbsolutePath(configPath());
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        m_persistenceReady = false;
        reportPersistenceError(QString("Could not create the member registry directory for %1.").arg(path));
        return false;
    }
    if (!migrateLegacyRegistry(path)) {
        m_persistenceReady = false;
        return false;
    }

    if (!QFileInfo::exists(path)) {
        m_loadedRoot = QJsonObject();
        m_loadedFileHash.clear();
        m_loadedFileExisted = false;
        m_persistenceReady = true;
        clearPersistenceError();
        qDebug() << "No member registry found, using defaults";
        return false;
    }

    QJsonObject root;
    QByteArray bytes;
    QString error;
    if (!readRegistryObject(path, &root, &bytes, &error)) {
        m_persistenceReady = false;
        reportPersistenceError(error + " The file was left untouched; member changes are disabled.");
        return false;
    }

    MemberTemplate loadedTemplate = m_template;
    if (root.contains("template")) {
        loadedTemplate = MemberTemplate::fromJson(root["template"].toObject());
    }

    QMap<QString, MemberInfo> loadedMembers;
    QJsonArray membersArray = root["members"].toArray();
    for (const QJsonValue& val : membersArray) {
        MemberInfo info = MemberInfo::fromJson(val.toObject());
        loadedMembers[info.id] = info;
    }

    QMap<QString, MemberGroup> loadedGroups;
    if (root.contains("groups")) {
        QJsonArray groupsArray = root["groups"].toArray();
        for (const QJsonValue& val : groupsArray) {
            MemberGroup group = MemberGroup::fromJson(val.toObject());
            if (!group.name.isEmpty()) {
                loadedGroups[group.name] = group;
            }
        }
    }

    m_template = loadedTemplate;
    m_members = loadedMembers;
    m_groups = loadedGroups;
    m_loadedRoot = root;
    m_loadedFileHash = fileHash(bytes);
    m_loadedFileExisted = true;
    m_persistenceReady = true;
    clearPersistenceError();
    qDebug() << "Loaded" << m_members.size() << "members and" << m_groups.size() << "groups from registry";
    emit membersReloaded();
    return true;
}

QJsonObject MemberRegistry::serializeRegistry() const {
    QJsonObject root = m_loadedRoot;

    const QStringList templateFields = {
        "archiveRootPrefix", "nhbCallsPath", "fastForwardPath", "theoryCallsPath",
        "hotSeatsPath", "wmRootPath", "pathTypes"
    };
    root["template"] = replaceKnownFields(root.value("template").toObject(),
                                           m_template.toJson(), templateFields);

    const QStringList memberFields = {
        "id", "displayName", "sortOrder", "wmFolderPattern", "active", "notes", "paths",
        "email", "ipAddress", "macAddress", "socialHandle", "watermarkFields",
        "useGlobalWatermark", "wpUserId", "lastWpSync", "distributionFolder",
        "pipelineStatus", "createdAt", "updatedAt"
    };
    const QMap<QString, QJsonObject> existingMembers =
        objectsByKey(root.value("members").toArray(), "id");

    QJsonArray membersArray;
    for (const MemberInfo& info : m_members) {
        membersArray.append(replaceKnownFields(existingMembers.value(info.id),
                                               info.toJson(), memberFields));
    }
    root["members"] = membersArray;

    const QStringList groupFields = {
        "name", "description", "memberIds", "createdAt", "updatedAt"
    };
    const QMap<QString, QJsonObject> existingGroups =
        objectsByKey(root.value("groups").toArray(), "name");
    QJsonArray groupsArray;
    for (const MemberGroup& group : m_groups) {
        groupsArray.append(replaceKnownFields(existingGroups.value(group.name),
                                              group.toJson(), groupFields));
    }
    root["groups"] = groupsArray;
    return root;
}

bool MemberRegistry::createRotatingBackup(const QString& path) {
    if (!QFileInfo::exists(path)) return true;

    for (int generation = kBackupGenerations; generation >= 2; --generation) {
        const QString destination = QString("%1.bak.%2").arg(path).arg(generation);
        const QString source = QString("%1.bak.%2").arg(path).arg(generation - 1);
        if (!QFileInfo::exists(source)) continue;
        if (QFileInfo::exists(destination) && !QFile::remove(destination)) {
            reportPersistenceError(QString("Could not rotate member backup %1.").arg(destination));
            return false;
        }
        if (!QFile::rename(source, destination)) {
            reportPersistenceError(QString("Could not rotate member backup %1 to %2.")
                .arg(source, destination));
            return false;
        }
    }

    const QString newestBackup = path + ".bak.1";
    if (QFileInfo::exists(newestBackup) && !QFile::remove(newestBackup)) {
        reportPersistenceError(QString("Could not replace member backup %1.").arg(newestBackup));
        return false;
    }
    if (!QFile::copy(path, newestBackup)) {
        reportPersistenceError(QString(
            "Could not create member backup %1. The live registry was left untouched.")
            .arg(newestBackup));
        return false;
    }
    return true;
}

bool MemberRegistry::save() {
    const QString path = cleanAbsolutePath(configPath());
    if (!m_persistenceReady) {
        reportPersistenceError(QString(
            "Member changes are disabled because the registry did not load safely. Resolve or restore %1, then restart the app.")
            .arg(path));
        return false;
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        reportPersistenceError(QString("Could not create the member registry directory for %1.").arg(path));
        return false;
    }

    QLockFile lock(path + ".lock");
    lock.setStaleLockTime(30000);
    if (!lock.tryLock(5000)) {
        reportPersistenceError(QString(
            "Member data is busy in another process. Close other MegaCustom windows and try again. Registry: %1")
            .arg(path));
        return false;
    }

    const bool currentExists = QFileInfo::exists(path);
    QByteArray currentBytes;
    if (currentExists) {
        QFile currentFile(path);
        if (!currentFile.open(QIODevice::ReadOnly)) {
            reportPersistenceError(QString("Could not verify %1 before saving: %2")
                .arg(path, currentFile.errorString()));
            return false;
        }
        currentBytes = currentFile.readAll();
        if (currentFile.error() != QFileDevice::NoError) {
            reportPersistenceError(QString("Could not finish verifying %1: %2")
                .arg(path, currentFile.errorString()));
            return false;
        }
    }
    if (currentExists != m_loadedFileExisted
        || (currentExists && fileHash(currentBytes) != m_loadedFileHash)) {
        reportPersistenceError(QString(
            "Member data changed on disk after this window loaded it. Your attempted change was not saved or kept in memory. "
            "Close other MegaCustom windows, reopen this window, and try again. Registry: %1")
            .arg(path));
        return false;
    }

    if (!createRotatingBackup(path)) return false;

    const QJsonObject root = serializeRegistry();
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    QString error;
    if (!writeBytesAtomically(path, bytes, &error)) {
        reportPersistenceError(error);
        return false;
    }

    m_loadedRoot = root;
    m_loadedFileHash = fileHash(bytes);
    m_loadedFileExisted = true;
    clearPersistenceError();
    qDebug() << "Saved" << m_members.size() << "members to registry";
    return true;
}

bool MemberRegistry::setTemplate(const MemberTemplate& tmpl) {
    const MemberTemplate previous = m_template;
    m_template = tmpl;
    if (!save()) {
        m_template = previous;
        return false;
    }
    emit templateChanged();
    return true;
}

QList<MemberInfo> MemberRegistry::getAllMembers() const {
    QList<MemberInfo> list = m_members.values();
    std::sort(list.begin(), list.end(), [](const MemberInfo& a, const MemberInfo& b) {
        return a.sortOrder < b.sortOrder;
    });
    return list;
}

QList<MemberInfo> MemberRegistry::getActiveMembers() const {
    QList<MemberInfo> list;
    for (const MemberInfo& info : m_members) {
        if (info.active) {
            list.append(info);
        }
    }
    std::sort(list.begin(), list.end(), [](const MemberInfo& a, const MemberInfo& b) {
        return a.sortOrder < b.sortOrder;
    });
    return list;
}

MemberInfo MemberRegistry::getMember(const QString& id) const {
    return m_members.value(id);
}

bool MemberRegistry::hasMember(const QString& id) const {
    return m_members.contains(id);
}

bool MemberRegistry::addMember(const MemberInfo& member) {
    if (member.id.trimmed().isEmpty() || m_members.contains(member.id)) return false;
    m_members[member.id] = member;
    if (!save()) {
        m_members.remove(member.id);
        return false;
    }
    emit memberAdded(member.id);
    return true;
}

bool MemberRegistry::updateMember(const MemberInfo& member) {
    if (!m_members.contains(member.id)) return false;
    const MemberInfo previous = m_members.value(member.id);
    m_members[member.id] = member;
    if (!save()) {
        m_members[member.id] = previous;
        return false;
    }
    emit memberUpdated(member.id);
    return true;
}

bool MemberRegistry::removeMember(const QString& id) {
    if (!m_members.contains(id)) return false;
    const MemberInfo previous = m_members.value(id);
    const QMap<QString, MemberGroup> previousGroups = m_groups;
    QStringList changedGroups;
    m_members.remove(id);
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (auto it = m_groups.begin(); it != m_groups.end(); ++it) {
        if (it.value().memberIds.removeAll(id) > 0) {
            it.value().updatedAt = now;
            changedGroups.append(it.key());
        }
    }
    if (!save()) {
        m_members[id] = previous;
        m_groups = previousGroups;
        return false;
    }
    emit memberRemoved(id);
    for (const QString& group : changedGroups) emit groupUpdated(group);
    return true;
}

bool MemberRegistry::setMembers(const QList<MemberInfo>& members) {
    const QMap<QString, MemberInfo> previous = m_members;
    m_members.clear();
    QSet<QString> memberIds;
    for (const MemberInfo& info : members) {
        if (info.id.trimmed().isEmpty() || memberIds.contains(info.id)) {
            m_members = previous;
            return false;
        }
        memberIds.insert(info.id);
        m_members[info.id] = info;
    }
    if (!save()) {
        m_members = previous;
        return false;
    }
    emit membersReloaded();
    return true;
}

QString MemberRegistry::getMonthPath(const QString& memberId, const QString& month) const {
    if (!m_members.contains(memberId)) return QString();
    return m_members[memberId].paths.getMonthPath(month);
}

QString MemberRegistry::getTheoryCallsPath(const QString& memberId) const {
    if (!m_members.contains(memberId)) return QString();
    return m_members[memberId].paths.getTheoryCallsFullPath();
}

QString MemberRegistry::getHotSeatsPath(const QString& memberId) const {
    if (!m_members.contains(memberId)) return QString();
    return m_members[memberId].paths.getHotSeatsFullPath();
}

bool MemberRegistry::exportToFile(const QString& filePath) {
    QString error;
    const QByteArray bytes = QJsonDocument(serializeRegistry()).toJson(QJsonDocument::Indented);
    return writeBytesAtomically(filePath, bytes, &error);
}

bool MemberRegistry::importFromFile(const QString& filePath, bool mergeMode) {
    QJsonObject root;
    QString error;
    if (!readRegistryObject(filePath, &root, nullptr, &error)) return false;

    const MemberTemplate previousTemplate = m_template;
    const QMap<QString, MemberInfo> previousMembers = m_members;
    const QMap<QString, MemberGroup> previousGroups = m_groups;
    const QJsonObject previousRoot = m_loadedRoot;

    if (root.contains("template")) {
        m_template = MemberTemplate::fromJson(root["template"].toObject());
    }

    if (!mergeMode) {
        m_members.clear();
    }
    QJsonArray membersArray = root["members"].toArray();
    for (const QJsonValue& val : membersArray) {
        MemberInfo info = MemberInfo::fromJson(val.toObject());
        m_members[info.id] = info;  // Upsert: add new or update existing
    }

    // Import groups
    if (!mergeMode) {
        m_groups.clear();
    }
    if (root.contains("groups")) {
        QJsonArray groupsArray = root["groups"].toArray();
        for (const QJsonValue& val : groupsArray) {
            MemberGroup group = MemberGroup::fromJson(val.toObject());
            if (!group.name.isEmpty()) {
                m_groups[group.name] = group;
            }
        }
    }

    if (!mergeMode) {
        m_loadedRoot = root;
    } else {
        QJsonObject mergedRoot = m_loadedRoot;
        for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
            if (it.key() != "members" && it.key() != "groups" && it.key() != "template"
                && !mergedRoot.contains(it.key())) {
                mergedRoot.insert(it.key(), it.value());
            }
        }
        if (root.contains("template")) mergedRoot["template"] = root.value("template");

        QMap<QString, QJsonObject> rawMembers =
            objectsByKey(mergedRoot.value("members").toArray(), "id");
        const QMap<QString, QJsonObject> importedMembers =
            objectsByKey(root.value("members").toArray(), "id");
        for (auto it = importedMembers.constBegin(); it != importedMembers.constEnd(); ++it) {
            rawMembers[it.key()] = it.value();
        }
        QJsonArray memberValues;
        for (const QJsonObject& object : rawMembers) memberValues.append(object);
        mergedRoot["members"] = memberValues;

        QMap<QString, QJsonObject> rawGroups =
            objectsByKey(mergedRoot.value("groups").toArray(), "name");
        const QMap<QString, QJsonObject> importedGroups =
            objectsByKey(root.value("groups").toArray(), "name");
        for (auto it = importedGroups.constBegin(); it != importedGroups.constEnd(); ++it) {
            rawGroups[it.key()] = it.value();
        }
        QJsonArray groupValues;
        for (const QJsonObject& object : rawGroups) groupValues.append(object);
        mergedRoot["groups"] = groupValues;
        m_loadedRoot = mergedRoot;
    }

    if (!save()) {
        m_template = previousTemplate;
        m_members = previousMembers;
        m_groups = previousGroups;
        m_loadedRoot = previousRoot;
        return false;
    }
    emit membersReloaded();
    return true;
}

// ==================== Phase 2: Distribution Folder Management ====================

bool MemberRegistry::setDistributionFolder(const QString& memberId, const QString& folderPath) {
    if (!m_members.contains(memberId)) return false;

    const MemberInfo previous = m_members.value(memberId);
    m_members[memberId].distributionFolder = folderPath;
    m_members[memberId].updatedAt = QDateTime::currentSecsSinceEpoch();
    if (!save()) {
        m_members[memberId] = previous;
        return false;
    }
    emit memberUpdated(memberId);
    return true;
}

bool MemberRegistry::clearDistributionFolder(const QString& memberId) {
    if (!m_members.contains(memberId)) return false;

    const MemberInfo previous = m_members.value(memberId);
    m_members[memberId].distributionFolder.clear();
    m_members[memberId].updatedAt = QDateTime::currentSecsSinceEpoch();
    if (!save()) {
        m_members[memberId] = previous;
        return false;
    }
    emit memberUpdated(memberId);
    return true;
}

QList<MemberInfo> MemberRegistry::getMembersWithDistributionFolders() const {
    QList<MemberInfo> list;
    for (const MemberInfo& info : m_members) {
        if (!info.distributionFolder.isEmpty()) {
            list.append(info);
        }
    }
    std::sort(list.begin(), list.end(), [](const MemberInfo& a, const MemberInfo& b) {
        return a.sortOrder < b.sortOrder;
    });
    return list;
}

// ==================== Phase 2: Watermark Configuration ====================

bool MemberRegistry::setWatermarkFields(const QString& memberId, const QStringList& fields) {
    if (!m_members.contains(memberId)) return false;

    const MemberInfo previous = m_members.value(memberId);
    m_members[memberId].watermarkFields = fields;
    m_members[memberId].updatedAt = QDateTime::currentSecsSinceEpoch();
    if (!save()) {
        m_members[memberId] = previous;
        return false;
    }
    emit memberUpdated(memberId);
    return true;
}

bool MemberRegistry::setUseGlobalWatermark(const QString& memberId, bool useGlobal) {
    if (!m_members.contains(memberId)) return false;

    const MemberInfo previous = m_members.value(memberId);
    m_members[memberId].useGlobalWatermark = useGlobal;
    m_members[memberId].updatedAt = QDateTime::currentSecsSinceEpoch();
    if (!save()) {
        m_members[memberId] = previous;
        return false;
    }
    emit memberUpdated(memberId);
    return true;
}

QStringList MemberRegistry::availableWatermarkFields() {
    return QStringList{"name", "id", "email", "ip", "mac", "social"};
}

// ==================== Phase 2: WordPress Sync ====================

bool MemberRegistry::markWordPressSynced(const QString& memberId, const QString& wpUserId) {
    if (!m_members.contains(memberId)) return false;

    const MemberInfo previous = m_members.value(memberId);
    if (!wpUserId.isEmpty()) {
        m_members[memberId].wpUserId = wpUserId;
    }
    m_members[memberId].lastWpSync = QDateTime::currentSecsSinceEpoch();
    m_members[memberId].updatedAt = QDateTime::currentSecsSinceEpoch();
    if (!save()) {
        m_members[memberId] = previous;
        return false;
    }
    emit memberUpdated(memberId);
    return true;
}

QList<MemberInfo> MemberRegistry::getUnsyncedMembers() const {
    QList<MemberInfo> list;
    for (const MemberInfo& info : m_members) {
        if (info.lastWpSync == 0 || info.wpUserId.isEmpty()) {
            list.append(info);
        }
    }
    return list;
}

// ==================== Smart Folder Matching ====================

MemberRegistry::FolderMatch MemberRegistry::matchFolderToMember(const QString& folderName) const {
    FolderMatch result;
    result.folderName = folderName;
    result.matchType = "none";
    result.confidence = 0;

    QString folderLower = folderName.toLower();

    // Strip timestamp suffix if present (e.g., "memberId_20260225_143000" -> "memberId")
    QString folderBase = folderName;
    QRegularExpression tsRe("^(.+)_(\\d{8}_\\d{6})$");
    QRegularExpressionMatch tsMatch = tsRe.match(folderName);
    if (tsMatch.hasMatch()) {
        folderBase = tsMatch.captured(1);
    }
    QString folderBaseLower = folderBase.toLower();

    for (auto it = m_members.constBegin(); it != m_members.constEnd(); ++it) {
        const MemberInfo& member = it.value();

        // Strategy 1: wmFolderPattern glob match (highest confidence)
        if (!member.wmFolderPattern.isEmpty()) {
            // Convert simple glob to regex: * -> .*, ? -> .
            QString pattern = member.wmFolderPattern;
            pattern.replace(".", "\\.");
            pattern.replace("*", ".*");
            pattern.replace("?", ".");
            QRegularExpression patternRe("^" + pattern + "$", QRegularExpression::CaseInsensitiveOption);
            if (patternRe.match(folderName).hasMatch() || patternRe.match(folderBase).hasMatch()) {
                result.matchedMemberId = member.id;
                result.matchType = "pattern";
                result.confidence = 5;
                return result;
            }
        }

        // Strategy 2: Exact member ID match (with or without timestamp)
        if (folderBaseLower == member.id.toLower()) {
            result.matchedMemberId = member.id;
            result.matchType = "id";
            result.confidence = 5;
            return result;
        }

        // Strategy 3: Email prefix match
        if (!member.email.isEmpty()) {
            QString emailPrefix = member.email.section('@', 0, 0).toLower();
            if (!emailPrefix.isEmpty() && folderBaseLower == emailPrefix) {
                result.matchedMemberId = member.id;
                result.matchType = "email";
                result.confidence = 4;
                return result;
            }
        }

        // Strategy 4: Display name exact match
        if (!member.displayName.isEmpty()) {
            if (folderBaseLower == member.displayName.toLower()) {
                result.matchedMemberId = member.id;
                result.matchType = "name";
                result.confidence = 4;
                return result;
            }
        }
    }

    // Strategy 5: Fuzzy/partial matches (lower confidence, do a second pass)
    for (auto it = m_members.constBegin(); it != m_members.constEnd(); ++it) {
        const MemberInfo& member = it.value();
        QString idLower = member.id.toLower();
        QString nameLower = member.displayName.toLower();

        // Email prefix as substring
        if (!member.email.isEmpty()) {
            QString emailPrefix = member.email.section('@', 0, 0).toLower();
            if (!emailPrefix.isEmpty() && emailPrefix.length() >= 3 && folderBaseLower.contains(emailPrefix)) {
                result.matchedMemberId = member.id;
                result.matchType = "email";
                result.confidence = 3;
                return result;
            }
        }

        // Folder contains member ID as substring (min 3 chars to avoid false positives)
        if (idLower.length() >= 3 && folderBaseLower.contains(idLower)) {
            result.matchedMemberId = member.id;
            result.matchType = "fuzzy";
            result.confidence = 2;
            return result;
        }

        // Folder contains display name as substring
        if (!nameLower.isEmpty() && nameLower.length() >= 3 && folderBaseLower.contains(nameLower)) {
            result.matchedMemberId = member.id;
            result.matchType = "name";
            result.confidence = 2;
            return result;
        }
    }

    return result;
}

QList<MemberRegistry::FolderMatch> MemberRegistry::matchFoldersToMembers(const QStringList& folderNames) const {
    QList<FolderMatch> results;
    results.reserve(folderNames.size());
    for (const QString& name : folderNames) {
        results.append(matchFolderToMember(name));
    }
    return results;
}

// ==================== Pipeline Status ====================

bool MemberRegistry::recordWatermark(const QString& memberId, int fileCount) {
    if (!m_members.contains(memberId)) return false;
    const MemberInfo previous = m_members.value(memberId);
    auto& status = m_members[memberId].pipelineStatus;
    status.lastWatermarkDate = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm");
    status.watermarkCount += fileCount;
    m_members[memberId].updatedAt = QDateTime::currentSecsSinceEpoch();
    if (!save()) {
        m_members[memberId] = previous;
        return false;
    }
    emit memberUpdated(memberId);
    return true;
}

bool MemberRegistry::recordDistribution(const QString& memberId, int fileCount) {
    if (!m_members.contains(memberId)) return false;
    const MemberInfo previous = m_members.value(memberId);
    auto& status = m_members[memberId].pipelineStatus;
    status.lastDistributionDate = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm");
    status.distributionCount += fileCount;
    m_members[memberId].updatedAt = QDateTime::currentSecsSinceEpoch();
    if (!save()) {
        m_members[memberId] = previous;
        return false;
    }
    emit memberUpdated(memberId);
    return true;
}

// ==================== Phase 2: CSV Import/Export ====================

bool MemberRegistry::exportToCsv(const QString& filePath) {
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream out(&file);

    // Header
    out << "id,displayName,email,ipAddress,macAddress,socialHandle,distributionFolder,active\n";

    // Data rows
    for (const MemberInfo& m : m_members) {
        QString displayNameEscaped = m.displayName;
        displayNameEscaped.replace("\"", "\"\"");
        out << m.id << ","
            << "\"" << displayNameEscaped << "\","
            << m.email << ","
            << m.ipAddress << ","
            << m.macAddress << ","
            << m.socialHandle << ","
            << m.distributionFolder << ","
            << (m.active ? "true" : "false") << "\n";
    }

    out.flush();
    if (out.status() != QTextStream::Ok) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

bool MemberRegistry::importFromCsv(const QString& filePath, bool skipHeader) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream in(&file);
    bool firstLine = true;
    int imported = 0;
    const QMap<QString, MemberInfo> previousMembers = m_members;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        if (firstLine && skipHeader) {
            firstLine = false;
            continue;
        }
        firstLine = false;

        // Simple CSV parsing (doesn't handle all edge cases)
        QStringList fields = line.split(',');
        if (fields.size() >= 2) {
            MemberInfo info;
            info.id = fields[0].trimmed();
            info.displayName = fields[1].trimmed().remove("\"");

            if (fields.size() > 2) info.email = fields[2].trimmed();
            if (fields.size() > 3) info.ipAddress = fields[3].trimmed();
            if (fields.size() > 4) info.macAddress = fields[4].trimmed();
            if (fields.size() > 5) info.socialHandle = fields[5].trimmed();
            if (fields.size() > 6) info.distributionFolder = fields[6].trimmed();
            if (fields.size() > 7) info.active = (fields[7].trimmed().toLower() == "true");

            info.createdAt = QDateTime::currentSecsSinceEpoch();
            info.updatedAt = info.createdAt;

            // Default watermark fields
            if (info.watermarkFields.isEmpty()) {
                info.watermarkFields = {"name", "email", "ip"};
            }

            m_members[info.id] = info;
            imported++;
        }
    }

    file.close();

    if (imported > 0) {
        if (!save()) {
            m_members = previousMembers;
            return false;
        }
        emit membersReloaded();
    }

    return imported > 0;
}

// ==================== Phase 2: Filter/Search ====================

QList<MemberInfo> MemberRegistry::filterMembers(const QString& searchText,
                                                 bool activeOnly,
                                                 bool withDistributionFolder,
                                                 bool withEmail,
                                                 bool withIp,
                                                 bool missingWmInfo) const {
    QList<MemberInfo> list;

    for (const MemberInfo& info : m_members) {
        // Active filter
        if (activeOnly && !info.active) continue;

        // Distribution folder filter
        if (withDistributionFolder && info.distributionFolder.isEmpty()) continue;

        // Email filter
        if (withEmail && info.email.isEmpty()) continue;

        // IP filter
        if (withIp && info.ipAddress.isEmpty()) continue;

        // Missing watermark info filter (show members missing email OR IP)
        if (missingWmInfo && !info.email.isEmpty() && !info.ipAddress.isEmpty()) continue;

        // Search filter
        if (!searchText.isEmpty()) {
            bool match = info.id.contains(searchText, Qt::CaseInsensitive) ||
                         info.displayName.contains(searchText, Qt::CaseInsensitive) ||
                         info.email.contains(searchText, Qt::CaseInsensitive) ||
                         info.ipAddress.contains(searchText, Qt::CaseInsensitive);
            if (!match) continue;
        }

        list.append(info);
    }

    std::sort(list.begin(), list.end(), [](const MemberInfo& a, const MemberInfo& b) {
        return a.sortOrder < b.sortOrder;
    });

    return list;
}

// ==================== Member Groups ====================

QList<MemberGroup> MemberRegistry::getAllGroups() const {
    return m_groups.values();
}

MemberGroup MemberRegistry::getGroup(const QString& name) const {
    return m_groups.value(name);
}

bool MemberRegistry::hasGroup(const QString& name) const {
    return m_groups.contains(name);
}

QStringList MemberRegistry::getGroupNames() const {
    QStringList names = m_groups.keys();
    names.sort(Qt::CaseInsensitive);
    return names;
}

bool MemberRegistry::addGroup(const MemberGroup& group) {
    if (group.name.trimmed().isEmpty() || m_groups.contains(group.name)) return false;
    m_groups[group.name] = group;
    if (!save()) {
        m_groups.remove(group.name);
        return false;
    }
    emit groupAdded(group.name);
    return true;
}

bool MemberRegistry::updateGroup(const MemberGroup& group) {
    if (!m_groups.contains(group.name)) return false;
    const MemberGroup previous = m_groups.value(group.name);
    m_groups[group.name] = group;
    if (!save()) {
        m_groups[group.name] = previous;
        return false;
    }
    emit groupUpdated(group.name);
    return true;
}

bool MemberRegistry::removeGroup(const QString& name) {
    if (!m_groups.contains(name)) return false;
    const MemberGroup previous = m_groups.take(name);
    if (!save()) {
        m_groups[name] = previous;
        return false;
    }
    emit groupRemoved(name);
    return true;
}

bool MemberRegistry::renameGroup(const QString& oldName, const QString& newName) {
    const QString trimmedName = newName.trimmed();
    if (oldName.isEmpty() || trimmedName.isEmpty() || !m_groups.contains(oldName)
        || (oldName != trimmedName && m_groups.contains(trimmedName))) {
        return false;
    }
    if (oldName == trimmedName) return true;

    const QMap<QString, MemberGroup> previousGroups = m_groups;
    const QJsonObject previousRoot = m_loadedRoot;
    MemberGroup group = m_groups.take(oldName);
    group.name = trimmedName;
    group.updatedAt = QDateTime::currentSecsSinceEpoch();
    m_groups[trimmedName] = group;

    QMap<QString, QJsonObject> rawGroups =
        objectsByKey(m_loadedRoot.value("groups").toArray(), "name");
    if (rawGroups.contains(oldName)) {
        QJsonObject rawGroup = rawGroups.take(oldName);
        rawGroup["name"] = trimmedName;
        rawGroups[trimmedName] = rawGroup;
        QJsonArray values;
        for (const QJsonObject& object : rawGroups) values.append(object);
        m_loadedRoot["groups"] = values;
    }
    if (!save()) {
        m_groups = previousGroups;
        m_loadedRoot = previousRoot;
        return false;
    }
    emit groupRemoved(oldName);
    emit groupAdded(trimmedName);
    return true;
}

QStringList MemberRegistry::getGroupMemberIds(const QString& groupName) const {
    if (!m_groups.contains(groupName)) return {};
    QStringList result;
    for (const QString& id : m_groups[groupName].memberIds) {
        if (m_members.contains(id) && m_members[id].active) {
            result.append(id);
        }
    }
    return result;
}

QStringList MemberRegistry::getGroupsForMember(const QString& memberId) const {
    QStringList result;
    for (auto it = m_groups.constBegin(); it != m_groups.constEnd(); ++it) {
        if (it.value().memberIds.contains(memberId)) {
            result.append(it.key());
        }
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

} // namespace MegaCustom
