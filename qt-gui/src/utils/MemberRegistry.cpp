#include "MemberRegistry.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QDebug>
#include <QDateTime>
#include <QTextStream>

namespace MegaCustom {

MemberRegistry* MemberRegistry::s_instance = nullptr;

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
    if (!distributionFolderHandle.isEmpty()) obj["distributionFolderHandle"] = distributionFolderHandle;

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
    info.distributionFolderHandle = obj["distributionFolderHandle"].toString();

    // Timestamps
    info.createdAt = obj["createdAt"].toInteger(0);
    info.updatedAt = obj["updatedAt"].toInteger(0);

    return info;
}

// Phase 2: Watermark text helpers
QString MemberInfo::buildWatermarkText(const QString& brandText) const {
    QString text;
    if (!brandText.isEmpty()) {
        text = brandText + " - ";
    }

    QStringList fields = watermarkFields;
    if (fields.isEmpty()) {
        fields = {"name", "id"};  // Default fields
    }

    QStringList parts;
    for (const QString& field : fields) {
        if (field == "name" && !displayName.isEmpty()) parts << displayName;
        else if (field == "id" && !id.isEmpty()) parts << id;
        else if (field == "email" && !email.isEmpty()) parts << email;
    }

    return text + parts.join(" - ");
}

QString MemberInfo::buildSecondaryWatermarkText() const {
    QStringList parts;

    QStringList fields = watermarkFields;
    if (fields.isEmpty()) {
        fields = {"email", "ip"};  // Default secondary fields
    }

    for (const QString& field : fields) {
        if (field == "email" && !email.isEmpty()) parts << email;
        else if (field == "ip" && !ipAddress.isEmpty()) parts << "IP: " + ipAddress;
        else if (field == "mac" && !macAddress.isEmpty()) parts << "MAC: " + macAddress;
        else if (field == "social" && !socialHandle.isEmpty()) parts << socialHandle;
    }

    return parts.join(" - ");
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
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    return configDir + "/members.json";
}

bool MemberRegistry::load() {
    QFile file(configPath());
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "No member registry found, using defaults";
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) {
        qWarning() << "Invalid member registry format";
        return false;
    }

    QJsonObject root = doc.object();

    // Load template
    if (root.contains("template")) {
        m_template = MemberTemplate::fromJson(root["template"].toObject());
    }

    // Load members
    m_members.clear();
    QJsonArray membersArray = root["members"].toArray();
    for (const QJsonValue& val : membersArray) {
        MemberInfo info = MemberInfo::fromJson(val.toObject());
        m_members[info.id] = info;
    }

    qDebug() << "Loaded" << m_members.size() << "members from registry";
    emit membersReloaded();
    return true;
}

bool MemberRegistry::save() {
    QJsonObject root;

    // Save template
    root["template"] = m_template.toJson();

    // Save members
    QJsonArray membersArray;
    for (const MemberInfo& info : m_members) {
        membersArray.append(info.toJson());
    }
    root["members"] = membersArray;

    QJsonDocument doc(root);

    QFile file(configPath());
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to save member registry:" << file.errorString();
        return false;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    qDebug() << "Saved" << m_members.size() << "members to registry";
    return true;
}

void MemberRegistry::setTemplate(const MemberTemplate& tmpl) {
    m_template = tmpl;
    save();
    emit templateChanged();
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

void MemberRegistry::addMember(const MemberInfo& member) {
    m_members[member.id] = member;
    save();
    emit memberAdded(member.id);
}

void MemberRegistry::updateMember(const MemberInfo& member) {
    if (m_members.contains(member.id)) {
        m_members[member.id] = member;
        save();
        emit memberUpdated(member.id);
    }
}

void MemberRegistry::removeMember(const QString& id) {
    if (m_members.remove(id) > 0) {
        save();
        emit memberRemoved(id);
    }
}

void MemberRegistry::setMembers(const QList<MemberInfo>& members) {
    m_members.clear();
    for (const MemberInfo& info : members) {
        m_members[info.id] = info;
    }
    save();
    emit membersReloaded();
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
    QJsonObject root;
    root["template"] = m_template.toJson();

    QJsonArray membersArray;
    for (const MemberInfo& info : m_members) {
        membersArray.append(info.toJson());
    }
    root["members"] = membersArray;

    QJsonDocument doc(root);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool MemberRegistry::importFromFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) {
        return false;
    }

    QJsonObject root = doc.object();

    if (root.contains("template")) {
        m_template = MemberTemplate::fromJson(root["template"].toObject());
    }

    m_members.clear();
    QJsonArray membersArray = root["members"].toArray();
    for (const QJsonValue& val : membersArray) {
        MemberInfo info = MemberInfo::fromJson(val.toObject());
        m_members[info.id] = info;
    }

    save();
    emit membersReloaded();
    return true;
}

// ==================== Phase 2: Distribution Folder Management ====================

void MemberRegistry::setDistributionFolder(const QString& memberId, const QString& folderPath,
                                            const QString& folderHandle) {
    if (!m_members.contains(memberId)) return;

    m_members[memberId].distributionFolder = folderPath;
    m_members[memberId].distributionFolderHandle = folderHandle;
    m_members[memberId].updatedAt = QDateTime::currentSecsSinceEpoch();
    save();
    emit memberUpdated(memberId);
}

void MemberRegistry::clearDistributionFolder(const QString& memberId) {
    if (!m_members.contains(memberId)) return;

    m_members[memberId].distributionFolder.clear();
    m_members[memberId].distributionFolderHandle.clear();
    m_members[memberId].updatedAt = QDateTime::currentSecsSinceEpoch();
    save();
    emit memberUpdated(memberId);
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

void MemberRegistry::setWatermarkFields(const QString& memberId, const QStringList& fields) {
    if (!m_members.contains(memberId)) return;

    m_members[memberId].watermarkFields = fields;
    m_members[memberId].updatedAt = QDateTime::currentSecsSinceEpoch();
    save();
    emit memberUpdated(memberId);
}

void MemberRegistry::setUseGlobalWatermark(const QString& memberId, bool useGlobal) {
    if (!m_members.contains(memberId)) return;

    m_members[memberId].useGlobalWatermark = useGlobal;
    m_members[memberId].updatedAt = QDateTime::currentSecsSinceEpoch();
    save();
    emit memberUpdated(memberId);
}

QStringList MemberRegistry::availableWatermarkFields() {
    return QStringList{"name", "id", "email", "ip", "mac", "social"};
}

// ==================== Phase 2: WordPress Sync ====================

void MemberRegistry::markWordPressSynced(const QString& memberId, const QString& wpUserId) {
    if (!m_members.contains(memberId)) return;

    if (!wpUserId.isEmpty()) {
        m_members[memberId].wpUserId = wpUserId;
    }
    m_members[memberId].lastWpSync = QDateTime::currentSecsSinceEpoch();
    m_members[memberId].updatedAt = QDateTime::currentSecsSinceEpoch();
    save();
    emit memberUpdated(memberId);
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

// ==================== Phase 2: CSV Import/Export ====================

bool MemberRegistry::exportToCsv(const QString& filePath) {
    QFile file(filePath);
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

    file.close();
    return true;
}

bool MemberRegistry::importFromCsv(const QString& filePath, bool skipHeader) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream in(&file);
    bool firstLine = true;
    int imported = 0;

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
        save();
        emit membersReloaded();
    }

    return imported > 0;
}

// ==================== Phase 2: Filter/Search ====================

QList<MemberInfo> MemberRegistry::filterMembers(const QString& searchText,
                                                 bool activeOnly,
                                                 bool withDistributionFolder) const {
    QList<MemberInfo> list;

    for (const MemberInfo& info : m_members) {
        // Active filter
        if (activeOnly && !info.active) continue;

        // Distribution folder filter
        if (withDistributionFolder && info.distributionFolder.isEmpty()) continue;

        // Search filter
        if (!searchText.isEmpty()) {
            bool match = info.id.contains(searchText, Qt::CaseInsensitive) ||
                         info.displayName.contains(searchText, Qt::CaseInsensitive) ||
                         info.email.contains(searchText, Qt::CaseInsensitive);
            if (!match) continue;
        }

        list.append(info);
    }

    std::sort(list.begin(), list.end(), [](const MemberInfo& a, const MemberInfo& b) {
        return a.sortOrder < b.sortOrder;
    });

    return list;
}

} // namespace MegaCustom
