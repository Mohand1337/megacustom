#include "integrations/MemberDatabase.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <sys/stat.h>
#include <iostream>
#include <iomanip>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif

// JSON handling - use real nlohmann/json when available, fall back to built-in
#ifdef USE_NLOHMANN_JSON
#include <nlohmann/json.hpp>
#else
#include "json_simple.hpp"
#endif

namespace {
namespace fs = std::filesystem;

// Cross-platform function to get user home directory
std::string getHomeDirectory() {
#ifdef _WIN32
    char* userProfile = std::getenv("USERPROFILE");
    if (userProfile) {
        return std::string(userProfile);
    }
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, path))) {
        return std::string(path);
    }
    return "C:\\Users\\Default";
#else
    const char* home = std::getenv("HOME");
    return home ? std::string(home) : "/tmp";
#endif
}

std::string pathToUtf8(const fs::path& path) {
    return path.u8string();
}

void migrateLegacyMembers(const fs::path& target) {
    std::error_code ec;
    if (fs::exists(target, ec)) {
        return;
    }

    const fs::path home = fs::u8path(getHomeDirectory());
    const fs::path candidates[] = {
        home / ".config" / "MegaCustom" / "members.json",
        home / ".megacustom" / "members.json"
    };

    for (const fs::path& candidate : candidates) {
        if (candidate == target || !fs::exists(candidate, ec)) {
            continue;
        }
        const fs::path parent = target.parent_path();
        if (!parent.empty()) {
            fs::create_directories(parent, ec);
            if (ec) {
                return;
            }
        }
        fs::copy_file(candidate, target, fs::copy_options::none, ec);
        return;
    }
}

std::string jsonString(const nlohmann::json& object, const std::string& key,
                       const std::string& fallback = {}) {
    return object.contains(key) && object[key].is_string()
        ? object[key].get<std::string>() : fallback;
}

int64_t jsonInt64(const nlohmann::json& object, const std::string& key,
                  int64_t fallback = 0) {
    return object.contains(key) && object[key].is_number()
        ? object[key].get<int64_t>() : fallback;
}

bool jsonBool(const nlohmann::json& object, const std::string& key, bool fallback) {
    return object.contains(key) && object[key].is_boolean()
        ? object[key].get<bool>() : fallback;
}

nlohmann::json memberJsonObject(const MegaCustom::Member& member,
                                nlohmann::json object = nlohmann::json::object()) {
    if (!object.is_object()) {
        object = nlohmann::json::object();
    }

    object["id"] = member.id;
    object["displayName"] = member.name;
    object["name"] = member.name;
    object["email"] = member.email;
    object["ipAddress"] = member.ipAddress;
    object["macAddress"] = member.macAddress;
    object["socialHandle"] = member.socialHandle;
    object["distributionFolder"] = member.megaFolderPath;
    object["distributionFolderHandle"] = member.megaFolderHandle;
    object["wpUserId"] = member.wpUserId;
    object["lastWpSync"] = member.lastSynced;
    object["active"] = member.active;
    object["useGlobalWatermark"] = member.useGlobalWatermark;
    object["createdAt"] = member.createdAt;
    object["updatedAt"] = member.updatedAt;

    nlohmann::json watermarkFields = nlohmann::json::array();
    for (const std::string& field : member.watermarkFields) {
        watermarkFields.push_back(field);
    }
    object["watermarkFields"] = watermarkFields;

    nlohmann::json customFields = nlohmann::json::object();
    for (const auto& [key, value] : member.customFields) {
        customFields[key] = value;
    }
    object["customFields"] = customFields;
    return object;
}

bool replaceFileAtomically(const fs::path& temporary, const fs::path& target,
                           std::error_code& ec) {
#ifdef _WIN32
    if (MoveFileExW(temporary.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ec.clear();
        return true;
    }
    ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    return false;
#else
    fs::rename(temporary, target, ec);
    return !ec;
#endif
}
} // anonymous namespace

namespace MegaCustom {

// ==================== Member Methods ====================

std::string Member::getDisplayString() const {
    std::string result = id;
    if (!name.empty()) {
        result += " - " + name;
    }
    if (!email.empty()) {
        result += " (" + email + ")";
    }
    return result;
}

// ==================== MemberDatabase Constructor ====================

MemberDatabase::MemberDatabase(const std::string& storagePath) {
    if (storagePath.empty()) {
        if (const char* configured = std::getenv("MEGACUSTOM_CONFIG_DIR");
            configured && *configured != '\0') {
            m_storagePath = pathToUtf8(fs::u8path(configured) / "members.json");
        } else {
            m_storagePath = pathToUtf8(fs::u8path(getHomeDirectory())
                / ".config" / "MegaCustom" / "members.json");
        }
    } else {
        m_storagePath = storagePath;
    }

    const fs::path target = fs::u8path(m_storagePath);
    migrateLegacyMembers(target);
    std::error_code ec;
    if (!target.parent_path().empty()) {
        fs::create_directories(target.parent_path(), ec);
    }

    // Load existing data
    loadFromFile();
}

// ==================== CRUD Operations ====================

MemberResult MemberDatabase::addMember(const Member& member) {
    MemberResult result;

    // Validate member ID
    if (member.id.empty()) {
        result.error = "Member ID cannot be empty";
        notifyError(result.error);
        return result;
    }

    // Check for duplicate
    if (m_members.find(member.id) != m_members.end()) {
        result.error = "Member with ID '" + member.id + "' already exists";
        notifyError(result.error);
        return result;
    }

    // Add member with timestamps
    Member newMember = member;
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    newMember.createdAt = timestamp;
    newMember.updatedAt = timestamp;

    // Default watermark fields if not set
    if (newMember.watermarkFields.empty()) {
        newMember.watermarkFields = {"name", "email", "ip"};
    }

    const bool wasDirty = m_dirty;
    m_members[newMember.id] = newMember;
    m_dirty = true;

    if (!save()) {
        m_members.erase(newMember.id);
        m_dirty = wasDirty;
        result.error = "Failed to persist new member '" + newMember.id + "'";
        return result;
    }

    result.success = true;
    result.member = newMember;
    if (m_onMemberAdded) {
        m_onMemberAdded(newMember);
    }

    return result;
}

MemberResult MemberDatabase::updateMember(const Member& member) {
    MemberResult result;

    auto it = m_members.find(member.id);
    if (it == m_members.end()) {
        result.error = "Member with ID '" + member.id + "' not found";
        notifyError(result.error);
        return result;
    }

    const Member previousMember = it->second;
    const bool wasDirty = m_dirty;

    // Update with new timestamp
    Member updatedMember = member;
    updatedMember.createdAt = it->second.createdAt;  // Preserve creation time
    updatedMember.updatedAt = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    m_members[member.id] = updatedMember;
    m_dirty = true;

    if (!save()) {
        m_members[member.id] = previousMember;
        m_dirty = wasDirty;
        result.error = "Failed to persist member update for '" + member.id + "'";
        return result;
    }

    result.success = true;
    result.member = updatedMember;
    if (m_onMemberUpdated) {
        m_onMemberUpdated(updatedMember);
    }

    return result;
}

MemberResult MemberDatabase::removeMember(const std::string& memberId) {
    MemberResult result;

    auto it = m_members.find(memberId);
    if (it == m_members.end()) {
        result.error = "Member with ID '" + memberId + "' not found";
        notifyError(result.error);
        return result;
    }

    Member removedMember = it->second;
    const bool wasDirty = m_dirty;
    m_members.erase(it);
    m_dirty = true;

    if (!save()) {
        m_members[memberId] = removedMember;
        m_dirty = wasDirty;
        result.error = "Failed to persist member removal for '" + memberId + "'";
        return result;
    }

    result.success = true;
    result.member = removedMember;
    if (m_onMemberRemoved) {
        m_onMemberRemoved(removedMember);
    }

    return result;
}

MemberResult MemberDatabase::getMember(const std::string& memberId) const {
    MemberResult result;

    auto it = m_members.find(memberId);
    if (it == m_members.end()) {
        result.error = "Member with ID '" + memberId + "' not found";
        return result;
    }

    result.success = true;
    result.member = it->second;
    return result;
}

MemberResult MemberDatabase::getAllMembers(const MemberFilter& filter) const {
    MemberResult result;
    result.success = true;

    for (const auto& [id, member] : m_members) {
        // Apply filters
        if (filter.activeOnly && !member.active) {
            continue;
        }

        if (filter.withFolderBinding && !member.hasFolderBinding()) {
            continue;
        }

        if (!filter.searchText.empty()) {
            std::string searchLower = filter.searchText;
            std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

            std::string idLower = member.id;
            std::string nameLower = member.name;
            std::string emailLower = member.email;
            std::transform(idLower.begin(), idLower.end(), idLower.begin(), ::tolower);
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            std::transform(emailLower.begin(), emailLower.end(), emailLower.begin(), ::tolower);

            if (idLower.find(searchLower) == std::string::npos &&
                nameLower.find(searchLower) == std::string::npos &&
                emailLower.find(searchLower) == std::string::npos) {
                continue;
            }
        }

        if (!filter.wpSyncStatus.empty() && filter.wpSyncStatus != "all") {
            bool isSynced = member.lastSynced > 0;
            if (filter.wpSyncStatus == "synced" && !isSynced) continue;
            if (filter.wpSyncStatus == "unsynced" && isSynced) continue;
        }

        result.members.push_back(member);
    }

    // Sort by ID
    std::sort(result.members.begin(), result.members.end(),
              [](const Member& a, const Member& b) { return a.id < b.id; });

    return result;
}

bool MemberDatabase::memberExists(const std::string& memberId) const {
    return m_members.find(memberId) != m_members.end();
}

// ==================== Batch Operations ====================

MemberResult MemberDatabase::importFromCsv(const std::string& csvPath, bool skipHeader) {
    MemberResult result;

    std::ifstream file(fs::u8path(csvPath));
    if (!file.is_open()) {
        result.error = "Failed to open CSV file: " + csvPath;
        notifyError(result.error);
        return result;
    }

    std::string line;
    int lineNum = 0;
    int imported = 0;
    int skipped = 0;

    while (std::getline(file, line)) {
        lineNum++;

        if (skipHeader && lineNum == 1) {
            continue;
        }

        if (line.empty()) continue;

        auto fields = parseCsvLine(line);
        if (fields.size() < 2) {
            skipped++;
            continue;
        }

        Member member;
        member.id = fields[0];
        member.name = fields.size() > 1 ? fields[1] : "";
        member.email = fields.size() > 2 ? fields[2] : "";
        member.ipAddress = fields.size() > 3 ? fields[3] : "";
        member.macAddress = fields.size() > 4 ? fields[4] : "";
        member.socialHandle = fields.size() > 5 ? fields[5] : "";
        member.megaFolderPath = fields.size() > 6 ? fields[6] : "";

        auto addResult = addMember(member);
        if (addResult.success) {
            imported++;
            result.members.push_back(*addResult.member);
        } else {
            skipped++;
        }
    }

    result.success = true;
    return result;
}

MemberResult MemberDatabase::exportToCsv(const std::string& csvPath, const MemberFilter& filter) const {
    MemberResult result;

    std::ofstream file(fs::u8path(csvPath));
    if (!file.is_open()) {
        result.error = "Failed to create CSV file: " + csvPath;
        return result;
    }

    // Write header
    file << "ID,Name,Email,IP,MAC,Social,MegaFolder\n";

    auto members = getAllMembers(filter);
    for (const auto& member : members.members) {
        file << memberToCsvLine(member) << "\n";
    }

    result.success = true;
    return result;
}

MemberResult MemberDatabase::importFromJson(const std::string& jsonPath) {
    MemberResult result;

    std::ifstream file(fs::u8path(jsonPath));
    if (!file.is_open()) {
        result.error = "Failed to open JSON file: " + jsonPath;
        notifyError(result.error);
        return result;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    if (loadMembersFromJson(buffer.str())) {
        result.success = true;
        result.members = getAllMembers().members;
    } else {
        result.error = "Failed to parse JSON file";
    }

    return result;
}

MemberResult MemberDatabase::exportToJson(const std::string& jsonPath, const MemberFilter& filter) const {
    MemberResult result;

    std::ofstream file(fs::u8path(jsonPath));
    if (!file.is_open()) {
        result.error = "Failed to create JSON file: " + jsonPath;
        return result;
    }

    file << allMembersToJson();
    result.success = true;
    return result;
}

// ==================== MEGA Folder Operations ====================

MemberResult MemberDatabase::bindFolder(const std::string& memberId,
                                        const std::string& folderPath,
                                        const std::string& folderHandle) {
    MemberResult result;

    auto it = m_members.find(memberId);
    if (it == m_members.end()) {
        result.error = "Member with ID '" + memberId + "' not found";
        notifyError(result.error);
        return result;
    }

    Member updated = it->second;
    updated.megaFolderPath = folderPath;
    updated.megaFolderHandle = folderHandle;
    return updateMember(updated);
}

MemberResult MemberDatabase::unbindFolder(const std::string& memberId) {
    return bindFolder(memberId, "", "");
}

MemberResult MemberDatabase::getMembersWithFolders() const {
    MemberFilter filter;
    filter.withFolderBinding = true;
    return getAllMembers(filter);
}

// ==================== Watermark Configuration ====================

MemberResult MemberDatabase::setWatermarkFields(const std::string& memberId,
                                                const std::vector<std::string>& fields) {
    MemberResult result;

    auto it = m_members.find(memberId);
    if (it == m_members.end()) {
        result.error = "Member with ID '" + memberId + "' not found";
        notifyError(result.error);
        return result;
    }

    Member updated = it->second;
    updated.watermarkFields = fields;
    return updateMember(updated);
}

std::vector<std::string> MemberDatabase::getAvailableWatermarkFields() {
    return {"name", "email", "ip", "mac", "social", "id"};
}

// ==================== WordPress Sync ====================

MemberResult MemberDatabase::setWordPressUserId(const std::string& memberId,
                                                const std::string& wpUserId) {
    MemberResult result;

    auto it = m_members.find(memberId);
    if (it == m_members.end()) {
        result.error = "Member with ID '" + memberId + "' not found";
        notifyError(result.error);
        return result;
    }

    Member updated = it->second;
    updated.wpUserId = wpUserId;
    return updateMember(updated);
}

MemberResult MemberDatabase::markAsSynced(const std::string& memberId) {
    MemberResult result;

    auto it = m_members.find(memberId);
    if (it == m_members.end()) {
        result.error = "Member with ID '" + memberId + "' not found";
        notifyError(result.error);
        return result;
    }

    Member updated = it->second;
    updated.lastSynced = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return updateMember(updated);
}

MemberResult MemberDatabase::getUnsyncedMembers() const {
    MemberFilter filter;
    filter.wpSyncStatus = "unsynced";
    return getAllMembers(filter);
}

// ==================== Persistence ====================

bool MemberDatabase::save() {
    return saveToFile();
}

bool MemberDatabase::reload() {
    return loadFromFile();
}

bool MemberDatabase::loadFromFile() {
    std::ifstream file(fs::u8path(m_storagePath));
    if (!file.is_open()) {
        // File doesn't exist yet - that's OK
        return true;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    return loadMembersFromJson(buffer.str());
}

bool MemberDatabase::saveToFile() {
    const fs::path storagePath = fs::u8path(m_storagePath);
    std::error_code ec;
    if (!storagePath.parent_path().empty()) {
        fs::create_directories(storagePath.parent_path(), ec);
    }
    if (ec) {
        notifyError("Failed to create member database directory: " + ec.message());
        return false;
    }

    nlohmann::json root = nlohmann::json::object();
    std::map<std::string, nlohmann::json> existingMembers;
    if (fs::exists(storagePath, ec)) {
        std::ifstream existingFile(storagePath);
        const std::string existingContent((std::istreambuf_iterator<char>(existingFile)),
                                          std::istreambuf_iterator<char>());
        if (!existingContent.empty()) {
            try {
                root = nlohmann::json::parse(existingContent);
            } catch (const std::exception& e) {
                notifyError("Refusing to overwrite invalid member registry: " + std::string(e.what()));
                return false;
            }
            if (!root.is_object()) {
                notifyError("Refusing to overwrite invalid member registry root");
                return false;
            }

            const nlohmann::json& existingArray = root["members"];
            if (existingArray.is_array()) {
                for (size_t i = 0; i < existingArray.size(); ++i) {
                    const nlohmann::json& object = existingArray[i];
                    const std::string id = jsonString(object, "id");
                    if (!id.empty()) {
                        existingMembers[id] = object;
                    }
                }
            }
        }
    }

    nlohmann::json members = nlohmann::json::array();
    for (const auto& [id, member] : m_members) {
        auto existing = existingMembers.find(id);
        members.push_back(memberJsonObject(
            member,
            existing == existingMembers.end() ? nlohmann::json::object() : existing->second));
    }
    root["members"] = members;

    fs::path temporaryPath = storagePath;
    temporaryPath += ".tmp";
    std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        notifyError("Failed to save to: " + m_storagePath);
        return false;
    }

    file << root.dump(2) << '\n';
    file.flush();
    if (!file.good()) {
        file.close();
        fs::remove(temporaryPath, ec);
        notifyError("Failed while writing member registry: " + m_storagePath);
        return false;
    }
    file.close();

#ifndef _WIN32
    fs::permissions(temporaryPath,
                    fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, ec);
    if (ec) {
        fs::remove(temporaryPath, ec);
        notifyError("Failed to secure member registry permissions");
        return false;
    }
#endif

    if (!replaceFileAtomically(temporaryPath, storagePath, ec)) {
        const std::error_code replaceError = ec;
        std::error_code cleanupError;
        fs::remove(temporaryPath, cleanupError);
        notifyError("Failed to replace member registry: " + replaceError.message());
        return false;
    }

    m_dirty = false;
    return true;
}

void MemberDatabase::notifyError(const std::string& error) {
    if (m_onError) {
        m_onError(error);
    }
}

// ==================== JSON Helpers ====================

std::string MemberDatabase::memberToJson(const Member& member) const {
    return memberJsonObject(member).dump(2);
}

std::string MemberDatabase::allMembersToJson() const {
    nlohmann::json root = nlohmann::json::object();
    nlohmann::json members = nlohmann::json::array();
    for (const auto& [id, member] : m_members) {
        members.push_back(memberJsonObject(member));
    }
    root["version"] = 1;
    root["members"] = members;
    return root.dump(2) + "\n";
}

bool MemberDatabase::loadMembersFromJson(const std::string& json) {
    try {
        const nlohmann::json root = nlohmann::json::parse(json);
        if (!root.is_object()) {
            return false;
        }
        if (!root.contains("members")) {
            m_members.clear();
            m_dirty = false;
            return true;
        }

        const nlohmann::json& members = root["members"];
        if (!members.is_array()) {
            return false;
        }

        std::map<std::string, Member> loaded;
        for (size_t i = 0; i < members.size(); ++i) {
            const nlohmann::json& object = members[i];
            if (!object.is_object()) {
                continue;
            }

            Member member;
            member.id = jsonString(object, "id");
            member.name = jsonString(object, "displayName", jsonString(object, "name"));
            member.email = jsonString(object, "email");
            member.ipAddress = jsonString(object, "ipAddress");
            member.macAddress = jsonString(object, "macAddress");
            member.socialHandle = jsonString(object, "socialHandle");
            member.megaFolderPath = jsonString(
                object, "distributionFolder", jsonString(object, "megaFolderPath"));
            member.megaFolderHandle = jsonString(
                object, "distributionFolderHandle", jsonString(object, "megaFolderHandle"));
            member.wpUserId = jsonString(object, "wpUserId");
            member.lastSynced = jsonInt64(
                object, "lastWpSync", jsonInt64(object, "lastSynced"));
            member.active = jsonBool(object, "active", true);
            member.useGlobalWatermark = jsonBool(object, "useGlobalWatermark", false);
            member.createdAt = jsonInt64(object, "createdAt");
            member.updatedAt = jsonInt64(object, "updatedAt");

            const nlohmann::json& fields = object["watermarkFields"];
            if (fields.is_array()) {
                for (size_t fieldIndex = 0; fieldIndex < fields.size(); ++fieldIndex) {
                    if (fields[fieldIndex].is_string()) {
                        member.watermarkFields.push_back(fields[fieldIndex].get<std::string>());
                    }
                }
            }
            if (member.watermarkFields.empty()) {
                member.watermarkFields = {"name", "email", "ip"};
            }

            const nlohmann::json& customFields = object["customFields"];
            if (customFields.is_object()) {
                for (const auto& [key, value] : customFields.items()) {
                    if (value.is_string()) {
                        member.customFields[key] = value.get<std::string>();
                    }
                }
            }

            if (!member.id.empty()) {
                loaded[member.id] = std::move(member);
            }
        }

        m_members = std::move(loaded);
        m_dirty = false;
        return true;
    } catch (const std::exception& e) {
        notifyError("Failed to parse member registry: " + std::string(e.what()));
        return false;
    }
}

// ==================== CSV Helpers ====================

std::vector<std::string> MemberDatabase::parseCsvLine(const std::string& line) const {
    std::vector<std::string> fields;
    std::string field;
    bool inQuotes = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];

        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == ',' && !inQuotes) {
            fields.push_back(field);
            field.clear();
        } else {
            field += c;
        }
    }
    fields.push_back(field);  // Last field

    return fields;
}

std::string MemberDatabase::memberToCsvLine(const Member& member) const {
    auto escapeField = [](const std::string& s) -> std::string {
        if (s.find(',') != std::string::npos || s.find('"') != std::string::npos) {
            std::string escaped = s;
            size_t pos = 0;
            while ((pos = escaped.find('"', pos)) != std::string::npos) {
                escaped.insert(pos, "\"");
                pos += 2;
            }
            return "\"" + escaped + "\"";
        }
        return s;
    };

    return escapeField(member.id) + "," +
           escapeField(member.name) + "," +
           escapeField(member.email) + "," +
           escapeField(member.ipAddress) + "," +
           escapeField(member.macAddress) + "," +
           escapeField(member.socialHandle) + "," +
           escapeField(member.megaFolderPath);
}

} // namespace MegaCustom
