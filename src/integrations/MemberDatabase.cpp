#include "integrations/MemberDatabase.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <sys/stat.h>
#include <iostream>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif

// Simple JSON handling (using the existing json_simple.hpp pattern)
#include "json_simple.hpp"

namespace {
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
} // anonymous namespace

namespace MegaCustom {

// ==================== Member Methods ====================

std::string Member::buildWatermarkText(const std::string& brandText) const {
    std::string result;

    if (!brandText.empty()) {
        result = brandText + " - ";
    }

    result += "Member #" + id;

    if (!name.empty() &&
        std::find(watermarkFields.begin(), watermarkFields.end(), "name") != watermarkFields.end()) {
        result += " (" + name + ")";
    }

    return result;
}

std::string Member::buildSecondaryWatermarkText() const {
    std::vector<std::string> parts;

    for (const auto& field : watermarkFields) {
        if (field == "email" && !email.empty()) {
            parts.push_back(email);
        } else if (field == "ip" && !ipAddress.empty()) {
            parts.push_back("IP: " + ipAddress);
        } else if (field == "mac" && !macAddress.empty()) {
            parts.push_back("MAC: " + macAddress);
        } else if (field == "social" && !socialHandle.empty()) {
            parts.push_back(socialHandle);
        }
    }

    if (parts.empty()) {
        return "Full identity on file - Legal consequences apply";
    }

    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) result += " - ";
        result += parts[i];
    }
    return result;
}

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
        // Default to ~/.config/MegaCustom/members.json (same as Qt MemberRegistry)
        // This ensures both C++ and Qt layers share the same member database
        std::string homeDir = getHomeDirectory();
        if (!homeDir.empty()) {
#ifdef _WIN32
            m_storagePath = homeDir + "\\.config\\MegaCustom\\members.json";
#else
            m_storagePath = homeDir + "/.config/MegaCustom/members.json";
#endif
        } else {
            m_storagePath = "./members.json";
        }
    } else {
        m_storagePath = storagePath;
    }

    // Ensure directory exists
#ifdef _WIN32
    size_t lastSep = m_storagePath.find_last_of("\\/");
#else
    size_t lastSep = m_storagePath.find_last_of('/');
#endif
    std::string dir = (lastSep != std::string::npos) ? m_storagePath.substr(0, lastSep) : "";
    if (!dir.empty()) {
        mkdir(dir.c_str(), 0755);
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

    m_members[newMember.id] = newMember;
    m_dirty = true;

    result.success = true;
    result.member = newMember;

    // Auto-save and notify
    save();
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

    // Update with new timestamp
    Member updatedMember = member;
    updatedMember.createdAt = it->second.createdAt;  // Preserve creation time
    updatedMember.updatedAt = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    m_members[member.id] = updatedMember;
    m_dirty = true;

    result.success = true;
    result.member = updatedMember;

    // Auto-save and notify
    save();
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
    m_members.erase(it);
    m_dirty = true;

    result.success = true;
    result.member = removedMember;

    // Auto-save and notify
    save();
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

    std::ifstream file(csvPath);
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

    std::ofstream file(csvPath);
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

    std::ifstream file(jsonPath);
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

    std::ofstream file(jsonPath);
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

    it->second.megaFolderPath = folderPath;
    it->second.megaFolderHandle = folderHandle;
    it->second.updatedAt = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    m_dirty = true;

    result.success = true;
    result.member = it->second;

    save();
    return result;
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

    it->second.watermarkFields = fields;
    it->second.updatedAt = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    m_dirty = true;

    result.success = true;
    result.member = it->second;

    save();
    return result;
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

    it->second.wpUserId = wpUserId;
    it->second.updatedAt = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    m_dirty = true;

    result.success = true;
    result.member = it->second;

    save();
    return result;
}

MemberResult MemberDatabase::markAsSynced(const std::string& memberId) {
    MemberResult result;

    auto it = m_members.find(memberId);
    if (it == m_members.end()) {
        result.error = "Member with ID '" + memberId + "' not found";
        notifyError(result.error);
        return result;
    }

    it->second.lastSynced = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    it->second.updatedAt = it->second.lastSynced;
    m_dirty = true;

    result.success = true;
    result.member = it->second;

    save();
    return result;
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
    std::ifstream file(m_storagePath);
    if (!file.is_open()) {
        // File doesn't exist yet - that's OK
        return true;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    return loadMembersFromJson(buffer.str());
}

bool MemberDatabase::saveToFile() {
    // Ensure directory exists
    std::string dir = m_storagePath.substr(0, m_storagePath.find_last_of('/'));
    if (!dir.empty()) {
        mkdir(dir.c_str(), 0755);
    }

    std::ofstream file(m_storagePath);
    if (!file.is_open()) {
        notifyError("Failed to save to: " + m_storagePath);
        return false;
    }

    file << allMembersToJson();
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
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"id\": \"" << member.id << "\",\n";
    // Use displayName for Qt compatibility, but also write name for backward compat
    ss << "  \"displayName\": \"" << member.name << "\",\n";
    ss << "  \"email\": \"" << member.email << "\",\n";
    ss << "  \"ipAddress\": \"" << member.ipAddress << "\",\n";
    ss << "  \"macAddress\": \"" << member.macAddress << "\",\n";
    ss << "  \"socialHandle\": \"" << member.socialHandle << "\",\n";
    // Use distributionFolder for Qt compatibility
    ss << "  \"distributionFolder\": \"" << member.megaFolderPath << "\",\n";
    ss << "  \"distributionFolderHandle\": \"" << member.megaFolderHandle << "\",\n";
    ss << "  \"wpUserId\": \"" << member.wpUserId << "\",\n";
    ss << "  \"lastWpSync\": " << member.lastSynced << ",\n";
    ss << "  \"active\": " << (member.active ? "true" : "false") << ",\n";
    ss << "  \"useGlobalWatermark\": " << (member.useGlobalWatermark ? "true" : "false") << ",\n";
    ss << "  \"createdAt\": " << member.createdAt << ",\n";
    ss << "  \"updatedAt\": " << member.updatedAt << ",\n";

    // Watermark fields array
    ss << "  \"watermarkFields\": [";
    for (size_t i = 0; i < member.watermarkFields.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << "\"" << member.watermarkFields[i] << "\"";
    }
    ss << "],\n";

    // Custom fields object
    ss << "  \"customFields\": {";
    bool first = true;
    for (const auto& [key, value] : member.customFields) {
        if (!first) ss << ", ";
        ss << "\"" << key << "\": \"" << value << "\"";
        first = false;
    }
    ss << "}\n";

    ss << "}";
    return ss.str();
}

std::string MemberDatabase::allMembersToJson() const {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"version\": 1,\n";
    ss << "  \"members\": [\n";

    bool first = true;
    for (const auto& [id, member] : m_members) {
        if (!first) ss << ",\n";
        // Indent each member object
        std::string memberJson = memberToJson(member);
        std::istringstream iss(memberJson);
        std::string line;
        while (std::getline(iss, line)) {
            ss << "    " << line << "\n";
        }
        first = false;
    }

    ss << "  ]\n";
    ss << "}\n";
    return ss.str();
}

bool MemberDatabase::loadMembersFromJson(const std::string& json) {
    // Simple JSON parsing - look for member objects
    // This is a basic implementation - for production, use nlohmann::json

    m_members.clear();

    // Find members array
    size_t membersStart = json.find("\"members\"");
    if (membersStart == std::string::npos) {
        return true;  // Empty file is OK
    }

    // Parse each member object (simplified parsing)
    size_t pos = membersStart;
    while ((pos = json.find("\"id\"", pos)) != std::string::npos) {
        Member member;

        // Extract ID
        size_t idStart = json.find("\"", pos + 4) + 1;
        size_t idEnd = json.find("\"", idStart);
        if (idStart != std::string::npos && idEnd != std::string::npos) {
            member.id = json.substr(idStart, idEnd - idStart);
        }

        // Helper lambda to extract string field
        auto extractField = [&json, pos](const std::string& fieldName) -> std::string {
            size_t fieldPos = json.find("\"" + fieldName + "\"", pos);
            if (fieldPos == std::string::npos || fieldPos > pos + 2000) return "";
            size_t valStart = json.find("\"", fieldPos + fieldName.length() + 2) + 1;
            size_t valEnd = json.find("\"", valStart);
            if (valStart != std::string::npos && valEnd != std::string::npos) {
                return json.substr(valStart, valEnd - valStart);
            }
            return "";
        };

        // Helper lambda to extract int64 field
        auto extractInt64 = [&json, pos](const std::string& fieldName) -> int64_t {
            size_t fieldPos = json.find("\"" + fieldName + "\"", pos);
            if (fieldPos == std::string::npos || fieldPos > pos + 2000) return 0;
            size_t valStart = json.find(":", fieldPos) + 1;
            while (valStart < json.size() && (json[valStart] == ' ' || json[valStart] == '\t')) valStart++;
            size_t valEnd = valStart;
            while (valEnd < json.size() && (isdigit(json[valEnd]) || json[valEnd] == '-')) valEnd++;
            if (valEnd > valStart) {
                return std::stoll(json.substr(valStart, valEnd - valStart));
            }
            return 0;
        };

        // Helper lambda to extract bool field
        auto extractBool = [&json, pos](const std::string& fieldName) -> bool {
            size_t fieldPos = json.find("\"" + fieldName + "\"", pos);
            if (fieldPos == std::string::npos || fieldPos > pos + 2000) return false;
            size_t valStart = json.find(":", fieldPos) + 1;
            return json.find("true", valStart) < valStart + 20;
        };

        // Try displayName first (Qt format), fall back to name (old format)
        member.name = extractField("displayName");
        if (member.name.empty()) {
            member.name = extractField("name");
        }
        member.email = extractField("email");
        member.ipAddress = extractField("ipAddress");
        member.macAddress = extractField("macAddress");
        member.socialHandle = extractField("socialHandle");
        // Try distributionFolder first (Qt format), fall back to megaFolderPath
        member.megaFolderPath = extractField("distributionFolder");
        if (member.megaFolderPath.empty()) {
            member.megaFolderPath = extractField("megaFolderPath");
        }
        member.megaFolderHandle = extractField("distributionFolderHandle");
        if (member.megaFolderHandle.empty()) {
            member.megaFolderHandle = extractField("megaFolderHandle");
        }
        member.wpUserId = extractField("wpUserId");
        // Try lastWpSync first (Qt format), fall back to lastSynced
        member.lastSynced = extractInt64("lastWpSync");
        if (member.lastSynced == 0) {
            member.lastSynced = extractInt64("lastSynced");
        }
        member.active = extractBool("active");
        member.useGlobalWatermark = extractBool("useGlobalWatermark");
        member.createdAt = extractInt64("createdAt");
        member.updatedAt = extractInt64("updatedAt");

        // Parse watermark fields array (simplified)
        size_t wfStart = json.find("\"watermarkFields\"", pos);
        if (wfStart != std::string::npos && wfStart < pos + 2000) {
            size_t arrStart = json.find("[", wfStart);
            size_t arrEnd = json.find("]", arrStart);
            if (arrStart != std::string::npos && arrEnd != std::string::npos) {
                std::string arrContent = json.substr(arrStart + 1, arrEnd - arrStart - 1);
                size_t fieldStart = 0;
                while ((fieldStart = arrContent.find("\"", fieldStart)) != std::string::npos) {
                    size_t fieldEnd = arrContent.find("\"", fieldStart + 1);
                    if (fieldEnd != std::string::npos) {
                        member.watermarkFields.push_back(
                            arrContent.substr(fieldStart + 1, fieldEnd - fieldStart - 1));
                        fieldStart = fieldEnd + 1;
                    } else break;
                }
            }
        }

        // Default watermark fields if empty
        if (member.watermarkFields.empty()) {
            member.watermarkFields = {"name", "email", "ip"};
        }

        if (!member.id.empty()) {
            m_members[member.id] = member;
        }

        pos = idEnd + 1;
    }

    m_dirty = false;
    return true;
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
