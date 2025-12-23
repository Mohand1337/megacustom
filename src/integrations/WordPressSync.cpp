#include "integrations/WordPressSync.h"
#include "integrations/MemberDatabase.h"
#include "core/LogManager.h"
#include "core/PathValidator.h"
#include "core/Crypto.h"
#include "json_simple.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <curl/curl.h>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace fs = std::filesystem;

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

// ==================== CURL Callback ====================

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

// ==================== Constructor ====================

WordPressSync::WordPressSync() {
    // Default member database path
    std::string homeDir = getHomeDirectory();
    if (!homeDir.empty()) {
#ifdef _WIN32
        m_memberDbPath = homeDir + "\\.megacustom\\members.json";
#else
        m_memberDbPath = homeDir + "/.megacustom/members.json";
#endif
    }

    // Set default field mappings
    m_config.fieldMappings = getDefaultFieldMappings();
}

// ==================== Static Utilities ====================

std::map<std::string, std::string> WordPressSync::getDefaultFieldMappings() {
    return {
        {"id", "wpUserId"},
        {"name", "name"},
        {"email", "email"},
        {"slug", "id"},  // WordPress slug becomes member ID
        {"meta.ip_address", "ipAddress"},
        {"meta.mac_address", "macAddress"},
        {"meta.social_handle", "socialHandle"},
        {"meta.member_id", "id"}  // Custom field overrides slug
    };
}

std::vector<std::string> WordPressSync::getSupportedMemberFields() {
    return {
        "id",
        "name",
        "email",
        "ipAddress",
        "macAddress",
        "socialHandle",
        "wpUserId",
        "megaFolderPath"
    };
}

std::string WordPressSync::urlEncode(const std::string& str) {
    CURL* curl = curl_easy_init();
    if (!curl) return str;

    char* output = curl_easy_escape(curl, str.c_str(), static_cast<int>(str.length()));
    std::string result = output ? output : str;
    if (output) {
        curl_free(output);
    }
    curl_easy_cleanup(curl);
    return result;
}

std::string WordPressSync::base64Encode(const std::string& str) {
    static const char* base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string ret;
    int i = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    size_t in_len = str.size();
    const unsigned char* bytes_to_encode = reinterpret_cast<const unsigned char*>(str.c_str());

    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; i < 4; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for (int j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

        for (int j = 0; j < i + 1; j++)
            ret += base64_chars[char_array_4[j]];

        while (i++ < 3)
            ret += '=';
    }

    return ret;
}

// ==================== Configuration ====================

std::string WordPressSync::getConfigFilePath() const {
    std::string homeDir = getHomeDirectory();
    if (!homeDir.empty()) {
#ifdef _WIN32
        return homeDir + "\\.megacustom\\wordpress.json";
#else
        return homeDir + "/.megacustom/wordpress.json";
#endif
    }
    return "./wordpress.json";
}

bool WordPressSync::loadConfig(const std::string& configPath) {
    std::string path = configPath.empty() ? getConfigFilePath() : configPath;

    std::ifstream file(path);
    if (!file.is_open()) {
        m_lastError = "Cannot open config file: " + path;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();

    // Parse JSON config using nlohmann::json
    nlohmann::json configJson = nlohmann::json::parse(content);
    if (configJson.is_null()) {
        m_lastError = "Config JSON parse error: invalid JSON";
        return false;
    }

    // Helper to safely get string values
    auto getString = [&configJson](const std::string& key) -> std::string {
        if (configJson.contains(key) && configJson[key].is_string()) {
            return configJson[key].get<std::string>();
        }
        return "";
    };

    m_config.siteUrl = getString("siteUrl");
    m_config.username = getString("username");
    std::string storedPassword = getString("applicationPassword");

    // Check if password is encrypted
    bool isEncrypted = configJson.contains("encrypted") &&
                       configJson["encrypted"].is_boolean() &&
                       configJson["encrypted"].get<bool>();

    if (isEncrypted && !storedPassword.empty()) {
        // Decrypt the password
        try {
            std::string machineKey = megacustom::Crypto::getMachineKey();
            m_config.applicationPassword = megacustom::Crypto::decrypt(storedPassword, machineKey);
        } catch (const megacustom::CryptoException& e) {
            m_lastError = "Failed to decrypt password: " + std::string(e.what());
            // Still allow loading - user may need to re-enter password
            m_config.applicationPassword = "";
        }
    } else {
        // Old format - password stored in plaintext (migrate on next save)
        m_config.applicationPassword = storedPassword;
    }

    std::string customEndpoint = getString("customEndpoint");
    if (!customEndpoint.empty()) {
        m_config.customEndpoint = customEndpoint;
    }

    return !m_config.siteUrl.empty();
}

bool WordPressSync::saveConfig(const std::string& configPath) {
    std::string path = configPath.empty() ? getConfigFilePath() : configPath;

    // Ensure directory exists (safe, no shell injection)
#ifdef _WIN32
    size_t lastSep = path.find_last_of("\\/");
#else
    size_t lastSep = path.find_last_of('/');
#endif
    std::string dir = (lastSep != std::string::npos) ? path.substr(0, lastSep) : "";
    if (!dir.empty()) {
        if (!megacustom::PathValidator::isValidPath(dir)) {
            m_lastError = "Invalid config directory path";
            return false;
        }
        try {
            fs::create_directories(dir);
        } catch (const fs::filesystem_error& e) {
            m_lastError = "Failed to create config directory: " + std::string(e.what());
            return false;
        }
    }

    std::ofstream file(path);
    if (!file.is_open()) {
        m_lastError = "Cannot write config file: " + path;
        return false;
    }

    // Encrypt the application password before saving
    std::string encryptedPassword;
    try {
        std::string machineKey = megacustom::Crypto::getMachineKey();
        encryptedPassword = megacustom::Crypto::encrypt(m_config.applicationPassword, machineKey);
    } catch (const megacustom::CryptoException& e) {
        m_lastError = "Failed to encrypt password: " + std::string(e.what());
        return false;
    }

    file << "{\n";
    file << "  \"siteUrl\": \"" << m_config.siteUrl << "\",\n";
    file << "  \"username\": \"" << m_config.username << "\",\n";
    file << "  \"applicationPassword\": \"" << encryptedPassword << "\",\n";
    file << "  \"encrypted\": true";
    if (!m_config.customEndpoint.empty()) {
        file << ",\n  \"customEndpoint\": \"" << m_config.customEndpoint << "\"";
    }
    file << "\n}\n";

    file.close();
    return true;
}

// ==================== HTTP Helpers ====================

std::string WordPressSync::buildAuthHeader() const {
    std::string credentials = m_config.username + ":" + m_config.applicationPassword;
    return "Basic " + base64Encode(credentials);
}

WordPressSync::HttpResponse WordPressSync::httpGet(const std::string& url) {
    HttpResponse response;

    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "Failed to initialize CURL";
        return response;
    }

    std::string responseBody;
    struct curl_slist* headers = nullptr;

    // Set authorization header
    std::string authHeader = "Authorization: " + buildAuthHeader();
    headers = curl_slist_append(headers, authHeader.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, m_config.timeout);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        response.error = curl_easy_strerror(res);
    } else {
        long httpCode;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        response.statusCode = static_cast<int>(httpCode);
        response.body = responseBody;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return response;
}

WordPressSync::HttpResponse WordPressSync::httpPost(const std::string& url, const std::string& body) {
    HttpResponse response;

    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "Failed to initialize CURL";
        return response;
    }

    std::string responseBody;
    struct curl_slist* headers = nullptr;

    std::string authHeader = "Authorization: " + buildAuthHeader();
    headers = curl_slist_append(headers, authHeader.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, m_config.timeout);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        response.error = curl_easy_strerror(res);
    } else {
        long httpCode;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        response.statusCode = static_cast<int>(httpCode);
        response.body = responseBody;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return response;
}

// ==================== JSON Parsing ====================

std::map<std::string, std::string> WordPressSync::parseUserJson(const std::string& jsonStr) {
    std::map<std::string, std::string> result;

    // Parse JSON using nlohmann::json - handles escaped quotes, nested objects, etc.
    nlohmann::json user = nlohmann::json::parse(jsonStr);
    if (user.is_null()) {
        // Return empty map on parse error
        return result;
    }

    // Helper to safely extract string value from JSON
    auto getString = [](const nlohmann::json& obj, const std::string& key) -> std::string {
        if (!obj.contains(key)) return "";
        const nlohmann::json& val = obj[key];
        if (val.is_string()) {
            return val.get<std::string>();
        } else if (val.is_number()) {
            return std::to_string(val.get<int64_t>());
        }
        return "";
    };

    // Extract common WordPress user fields
    result["id"] = getString(user, "id");
    result["name"] = getString(user, "name");
    result["email"] = getString(user, "email");
    result["slug"] = getString(user, "slug");
    result["url"] = getString(user, "url");
    result["description"] = getString(user, "description");
    result["link"] = getString(user, "link");
    result["registered_date"] = getString(user, "registered_date");

    // Extract nested meta fields - properly handles nested objects now!
    if (user.contains("meta") && user["meta"].is_object()) {
        const nlohmann::json& meta = user["meta"];

        // Extract known meta fields
        result["meta.ip_address"] = getString(meta, "ip_address");
        result["meta.mac_address"] = getString(meta, "mac_address");
        result["meta.social_handle"] = getString(meta, "social_handle");
        result["meta.member_id"] = getString(meta, "member_id");

        // Also extract any other meta fields dynamically using iterator
        for (auto it = meta.begin(); it != meta.end(); ++it) {
            std::string metaKey = "meta." + it->first;
            if (result.find(metaKey) == result.end()) {  // Don't overwrite known fields
                const nlohmann::json& value = it->second;
                if (value.is_string()) {
                    result[metaKey] = value.get<std::string>();
                } else if (value.is_number()) {
                    result[metaKey] = std::to_string(value.get<int64_t>());
                }
            }
        }
    }

    // Handle roles array if present (WordPress returns roles as array)
    if (user.contains("roles") && user["roles"].is_array() && !user["roles"].empty()) {
        const nlohmann::json& firstRole = user["roles"][static_cast<size_t>(0)];
        if (firstRole.is_string()) {
            result["roles"] = firstRole.get<std::string>();
        }
    }

    return result;
}

std::vector<std::map<std::string, std::string>> WordPressSync::parseUsersJson(const std::string& jsonStr) {
    std::vector<std::map<std::string, std::string>> results;

    // Parse JSON using nlohmann::json - properly handles arrays with any content
    nlohmann::json parsed = nlohmann::json::parse(jsonStr);
    if (parsed.is_null()) {
        return results;
    }

    // Handle array of users
    if (parsed.is_array()) {
        // Iterate using index-based access
        for (size_t i = 0; i < parsed.size(); ++i) {
            // Convert each user object back to string and parse via parseUserJson
            // This reuses the robust single-user parsing logic
            results.push_back(parseUserJson(parsed[i].dump()));
        }
    } else if (parsed.is_object()) {
        // Single user object (not an array)
        results.push_back(parseUserJson(jsonStr));
    }

    return results;
}

// ==================== Member Conversion ====================

Member WordPressSync::wpDataToMember(const std::map<std::string, std::string>& wpData) {
    Member member;

    // Apply field mappings
    for (const auto& [wpField, memberField] : m_config.fieldMappings) {
        auto it = wpData.find(wpField);
        if (it == wpData.end() || it->second.empty()) continue;

        const std::string& value = it->second;

        if (memberField == "id") {
            member.id = value;
        } else if (memberField == "name") {
            member.name = value;
        } else if (memberField == "email") {
            member.email = value;
        } else if (memberField == "ipAddress") {
            member.ipAddress = value;
        } else if (memberField == "macAddress") {
            member.macAddress = value;
        } else if (memberField == "socialHandle") {
            member.socialHandle = value;
        } else if (memberField == "wpUserId") {
            member.wpUserId = value;
        } else if (memberField == "megaFolderPath") {
            member.megaFolderPath = value;
        }
    }

    // Ensure we have a WordPress user ID
    if (member.wpUserId.empty()) {
        auto it = wpData.find("id");
        if (it != wpData.end()) {
            member.wpUserId = it->second;
        }
    }

    // Generate member ID if not set
    if (member.id.empty()) {
        member.id = buildMemberId(wpData);
    }

    // Use email as name if name is empty
    if (member.name.empty() && !member.email.empty()) {
        member.name = member.email.substr(0, member.email.find('@'));
    }

    // Set sync timestamp
    member.lastSynced = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    return member;
}

void WordPressSync::mergeWpDataToMember(Member& member, const std::map<std::string, std::string>& wpData) {
    // Update fields from WordPress, keeping existing values if WP has none
    for (const auto& [wpField, memberField] : m_config.fieldMappings) {
        auto it = wpData.find(wpField);
        if (it == wpData.end() || it->second.empty()) continue;

        const std::string& value = it->second;

        if (memberField == "name" && !value.empty()) {
            member.name = value;
        } else if (memberField == "email" && !value.empty()) {
            member.email = value;
        } else if (memberField == "ipAddress" && !value.empty()) {
            member.ipAddress = value;
        } else if (memberField == "macAddress" && !value.empty()) {
            member.macAddress = value;
        } else if (memberField == "socialHandle" && !value.empty()) {
            member.socialHandle = value;
        }
        // Don't overwrite id, wpUserId, or megaFolderPath
    }

    // Update sync timestamp
    member.lastSynced = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string WordPressSync::buildMemberId(const std::map<std::string, std::string>& wpData) const {
    // Check for custom member_id in meta
    auto metaIt = wpData.find("meta.member_id");
    if (metaIt != wpData.end() && !metaIt->second.empty()) {
        return metaIt->second;
    }

    // Use slug if available
    auto slugIt = wpData.find("slug");
    if (slugIt != wpData.end() && !slugIt->second.empty()) {
        std::string slug = slugIt->second;
        // Convert to uppercase for consistency
        std::transform(slug.begin(), slug.end(), slug.begin(), ::toupper);
        return slug;
    }

    // Fall back to WP{user_id}
    auto idIt = wpData.find("id");
    if (idIt != wpData.end() && !idIt->second.empty()) {
        return "WP" + idIt->second;
    }

    return "";
}

// ==================== Progress Reporting ====================

void WordPressSync::reportProgress(int current, int total, const std::string& username, const std::string& status) {
    if (m_progressCallback) {
        WpSyncProgress progress;
        progress.currentUser = current;
        progress.totalUsers = total;
        progress.currentUsername = username;
        progress.status = status;
        progress.percentComplete = total > 0 ? (static_cast<double>(current) / total * 100.0) : 0.0;
        m_progressCallback(progress);
    }
}

// ==================== Connection Testing ====================

bool WordPressSync::testConnection(std::string& error) {
    if (m_config.siteUrl.empty()) {
        error = "Site URL not configured";
        return false;
    }

    if (m_config.username.empty() || m_config.applicationPassword.empty()) {
        error = "Credentials not configured";
        return false;
    }

    // Test by fetching current user
    std::string url = m_config.siteUrl + "/wp-json/wp/v2/users/me";
    auto response = httpGet(url);

    if (!response.error.empty()) {
        error = "Connection failed: " + response.error;
        return false;
    }

    if (response.statusCode == 401) {
        error = "Authentication failed - check username and application password";
        return false;
    }

    if (response.statusCode == 404) {
        error = "REST API not found - ensure WordPress REST API is enabled";
        return false;
    }

    if (response.statusCode != 200) {
        error = "Unexpected response: HTTP " + std::to_string(response.statusCode);
        return false;
    }

    // Parse response to verify it's valid user data
    auto userData = parseUserJson(response.body);
    if (userData["id"].empty()) {
        error = "Invalid response from WordPress";
        return false;
    }

    return true;
}

std::map<std::string, std::string> WordPressSync::getSiteInfo(std::string& error) {
    std::map<std::string, std::string> info;

    std::string url = m_config.siteUrl + "/wp-json";
    auto response = httpGet(url);

    if (!response.error.empty()) {
        error = response.error;
        return info;
    }

    if (response.statusCode != 200) {
        error = "HTTP " + std::to_string(response.statusCode);
        return info;
    }

    // Parse site info from response using nlohmann::json
    nlohmann::json siteJson = nlohmann::json::parse(response.body);
    if (siteJson.is_null()) {
        error = "JSON parse error: invalid response";
        return info;
    }

    // Helper to safely extract string values
    auto getString = [&siteJson](const std::string& key) -> std::string {
        if (siteJson.contains(key) && siteJson[key].is_string()) {
            return siteJson[key].get<std::string>();
        }
        return "";
    };

    info["name"] = getString("name");
    info["description"] = getString("description");
    info["url"] = getString("url");
    info["home"] = getString("home");

    return info;
}

std::vector<std::string> WordPressSync::getAvailableFields(std::string& error) {
    std::vector<std::string> fields;

    // Fetch a single user to see available fields
    std::string url = m_config.siteUrl + m_config.usersEndpoint + "?per_page=1";
    auto response = httpGet(url);

    if (!response.error.empty()) {
        error = response.error;
        return fields;
    }

    if (response.statusCode != 200) {
        error = "HTTP " + std::to_string(response.statusCode);
        return fields;
    }

    auto users = parseUsersJson(response.body);
    if (!users.empty()) {
        for (const auto& [key, value] : users[0]) {
            fields.push_back(key);
        }
    }

    return fields;
}

// ==================== Sync Operations ====================

SyncResult WordPressSync::syncAll() {
    SyncResult result;
    result.syncStartTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    m_cancelled = false;

    LogManager::instance().logWordPress("sync_start", "Starting WordPress sync from " + m_config.siteUrl);

    // Load member database
    MemberDatabase db(m_memberDbPath);

    // Fetch all users from WordPress
    std::vector<std::map<std::string, std::string>> allUsers;
    int page = 1;

    reportProgress(0, 0, "", "fetching");

    while (!m_cancelled) {
        std::string url = m_config.siteUrl + m_config.usersEndpoint +
                         "?per_page=" + std::to_string(m_config.perPage) +
                         "&page=" + std::to_string(page);

        auto response = httpGet(url);

        if (!response.error.empty()) {
            LogManager::instance().logError("wp_sync_error", "WordPress API error: " + response.error);
            result.error = response.error;
            result.success = false;
            return result;
        }

        if (response.statusCode == 400) {
            // No more pages
            break;
        }

        if (response.statusCode != 200) {
            LogManager::instance().logError("wp_sync_error", "WordPress HTTP error: " + std::to_string(response.statusCode));
            result.error = "HTTP " + std::to_string(response.statusCode);
            result.success = false;
            return result;
        }

        auto users = parseUsersJson(response.body);
        if (users.empty()) {
            break;
        }

        allUsers.insert(allUsers.end(), users.begin(), users.end());
        page++;

        // Check if we got fewer than requested (last page)
        if (users.size() < static_cast<size_t>(m_config.perPage)) {
            break;
        }
    }

    result.totalUsers = static_cast<int>(allUsers.size());
    reportProgress(0, result.totalUsers, "", "syncing");

    // Process each user
    int current = 0;
    for (const auto& wpData : allUsers) {
        if (m_cancelled) {
            result.error = "Cancelled";
            break;
        }

        current++;
        std::string username = wpData.count("name") ? wpData.at("name") : wpData.at("id");
        reportProgress(current, result.totalUsers, username, "syncing");

        UserSyncResult userResult;
        userResult.wpUserId = wpData.count("id") ? wpData.at("id") : "";
        userResult.wpData = wpData;

        // Build member ID
        std::string memberId = buildMemberId(wpData);
        userResult.memberId = memberId;

        if (memberId.empty()) {
            userResult.success = false;
            userResult.action = "error";
            userResult.error = "Cannot determine member ID";
            result.usersFailed++;
            result.results.push_back(userResult);
            continue;
        }

        // Check if member exists
        auto existingMember = db.getMember(memberId);

        if (existingMember.success && existingMember.member) {
            // Update existing member
            if (m_config.updateExisting) {
                Member updated = *existingMember.member;
                mergeWpDataToMember(updated, wpData);
                auto updateResult = db.updateMember(updated);

                if (updateResult.success) {
                    userResult.success = true;
                    userResult.action = "updated";
                    result.usersUpdated++;
                    LogManager::instance().logMember("member_updated", "Updated member from WordPress: " + memberId, memberId);
                } else {
                    userResult.success = false;
                    userResult.action = "error";
                    userResult.error = updateResult.error;
                    result.usersFailed++;
                    LogManager::instance().logError("wp_update_failed", "Failed to update member " + memberId + ": " + updateResult.error);
                }
            } else {
                userResult.success = true;
                userResult.action = "skipped";
                result.usersSkipped++;
            }
        } else {
            // Create new member
            if (m_config.createNewMembers) {
                Member newMember = wpDataToMember(wpData);
                auto addResult = db.addMember(newMember);

                if (addResult.success) {
                    userResult.success = true;
                    userResult.action = "created";
                    result.usersCreated++;
                    LogManager::instance().logMember("member_created", "Created member from WordPress: " + memberId, memberId);
                } else {
                    userResult.success = false;
                    userResult.action = "error";
                    userResult.error = addResult.error;
                    result.usersFailed++;
                    LogManager::instance().logError("wp_create_failed", "Failed to create member " + memberId + ": " + addResult.error);
                }
            } else {
                userResult.success = true;
                userResult.action = "skipped";
                result.usersSkipped++;
            }
        }

        result.results.push_back(userResult);
    }

    // Save database
    db.save();

    result.syncEndTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    result.success = (result.usersFailed == 0);
    reportProgress(result.totalUsers, result.totalUsers, "", "complete");

    std::stringstream summary;
    summary << "WordPress sync complete: " << result.usersCreated << " created, "
            << result.usersUpdated << " updated, " << result.usersSkipped << " skipped, "
            << result.usersFailed << " failed";
    LogManager::instance().logWordPress("sync_complete", summary.str());

    return result;
}

SyncResult WordPressSync::syncUser(const std::string& wpUserId) {
    SyncResult result;
    result.syncStartTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Fetch specific user
    std::string url = m_config.siteUrl + m_config.usersEndpoint + "/" + wpUserId;
    auto response = httpGet(url);

    if (!response.error.empty()) {
        result.error = response.error;
        return result;
    }

    if (response.statusCode == 404) {
        result.error = "User not found: " + wpUserId;
        return result;
    }

    if (response.statusCode != 200) {
        result.error = "HTTP " + std::to_string(response.statusCode);
        return result;
    }

    auto wpData = parseUserJson(response.body);
    result.totalUsers = 1;

    // Process the user (same logic as syncAll but for one user)
    MemberDatabase db(m_memberDbPath);

    UserSyncResult userResult;
    userResult.wpUserId = wpUserId;
    userResult.wpData = wpData;

    std::string memberId = buildMemberId(wpData);
    userResult.memberId = memberId;

    if (memberId.empty()) {
        userResult.success = false;
        userResult.action = "error";
        userResult.error = "Cannot determine member ID";
        result.usersFailed = 1;
        result.results.push_back(userResult);
        return result;
    }

    auto existingMember = db.getMember(memberId);

    if (existingMember.success && existingMember.member) {
        Member updated = *existingMember.member;
        mergeWpDataToMember(updated, wpData);
        auto updateResult = db.updateMember(updated);

        if (updateResult.success) {
            userResult.success = true;
            userResult.action = "updated";
            result.usersUpdated = 1;
        } else {
            userResult.success = false;
            userResult.action = "error";
            userResult.error = updateResult.error;
            result.usersFailed = 1;
        }
    } else {
        Member newMember = wpDataToMember(wpData);
        auto addResult = db.addMember(newMember);

        if (addResult.success) {
            userResult.success = true;
            userResult.action = "created";
            result.usersCreated = 1;
        } else {
            userResult.success = false;
            userResult.action = "error";
            userResult.error = addResult.error;
            result.usersFailed = 1;
        }
    }

    result.results.push_back(userResult);
    db.save();

    result.syncEndTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    result.success = (result.usersFailed == 0);

    return result;
}

SyncResult WordPressSync::syncUserByEmail(const std::string& email) {
    SyncResult result;

    // Search for user by email
    std::string url = m_config.siteUrl + m_config.usersEndpoint + "?search=" + urlEncode(email);
    auto response = httpGet(url);

    if (!response.error.empty()) {
        result.error = response.error;
        return result;
    }

    if (response.statusCode != 200) {
        result.error = "HTTP " + std::to_string(response.statusCode);
        return result;
    }

    auto users = parseUsersJson(response.body);
    if (users.empty()) {
        result.error = "User not found with email: " + email;
        return result;
    }

    // Find exact email match
    for (const auto& user : users) {
        if (user.count("email") && user.at("email") == email) {
            return syncUser(user.at("id"));
        }
    }

    // If no exact match, use first result
    if (users[0].count("id")) {
        return syncUser(users[0].at("id"));
    }

    result.error = "User not found with email: " + email;
    return result;
}

SyncResult WordPressSync::syncByRole(const std::string& role) {
    // Temporarily modify endpoint to filter by role
    std::string originalEndpoint = m_config.usersEndpoint;
    m_config.usersEndpoint = m_config.usersEndpoint + "?roles=" + urlEncode(role);

    SyncResult result = syncAll();

    m_config.usersEndpoint = originalEndpoint;
    return result;
}

SyncResult WordPressSync::previewSync() {
    SyncResult result;
    result.syncStartTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Temporarily disable creating/updating
    bool originalCreate = m_config.createNewMembers;
    bool originalUpdate = m_config.updateExisting;
    m_config.createNewMembers = false;
    m_config.updateExisting = false;

    // Fetch users but don't modify database
    MemberDatabase db(m_memberDbPath);

    std::string url = m_config.siteUrl + m_config.usersEndpoint + "?per_page=" + std::to_string(m_config.perPage);
    auto response = httpGet(url);

    m_config.createNewMembers = originalCreate;
    m_config.updateExisting = originalUpdate;

    if (!response.error.empty()) {
        result.error = response.error;
        return result;
    }

    if (response.statusCode != 200) {
        result.error = "HTTP " + std::to_string(response.statusCode);
        return result;
    }

    auto users = parseUsersJson(response.body);
    result.totalUsers = static_cast<int>(users.size());

    for (const auto& wpData : users) {
        UserSyncResult userResult;
        userResult.wpUserId = wpData.count("id") ? wpData.at("id") : "";
        userResult.wpData = wpData;

        std::string memberId = buildMemberId(wpData);
        userResult.memberId = memberId;

        auto existingMember = db.getMember(memberId);

        if (existingMember.success && existingMember.member) {
            userResult.action = "would_update";
            result.usersUpdated++;
        } else {
            userResult.action = "would_create";
            result.usersCreated++;
        }

        userResult.success = true;
        result.results.push_back(userResult);
    }

    result.syncEndTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    result.success = true;

    return result;
}

std::vector<WpUser> WordPressSync::fetchAllUsers(std::string& error) {
    std::vector<WpUser> users;
    m_cancelled = false;

    // Build base URL with role filter if set
    std::string baseUrl = m_config.siteUrl + m_config.usersEndpoint;
    std::string roleParam = "";
    if (!m_config.roleFilter.empty()) {
        roleParam = "&roles=" + urlEncode(m_config.roleFilter);
    }

    int page = 1;
    reportProgress(0, 0, "", "fetching");

    while (!m_cancelled) {
        std::string url = baseUrl + "?per_page=" + std::to_string(m_config.perPage) +
                         "&page=" + std::to_string(page) + roleParam;

        auto response = httpGet(url);

        if (!response.error.empty()) {
            error = response.error;
            return users;
        }

        if (response.statusCode == 400) {
            // No more pages
            break;
        }

        if (response.statusCode != 200) {
            error = "HTTP " + std::to_string(response.statusCode);
            return users;
        }

        auto parsed = parseUsersJson(response.body);
        if (parsed.empty()) {
            break;
        }

        // Convert to WpUser structs
        for (const auto& wpData : parsed) {
            WpUser user;
            user.id = wpData.count("id") ? std::stoi(wpData.at("id")) : 0;
            user.username = wpData.count("slug") ? wpData.at("slug") : "";
            user.displayName = wpData.count("name") ? wpData.at("name") : "";
            user.email = wpData.count("email") ? wpData.at("email") : "";
            user.registeredDate = wpData.count("registered_date") ? wpData.at("registered_date") : "";

            // Try to get role from the response (WordPress doesn't always include it)
            // Role might be in a different format depending on WP version
            if (wpData.count("roles")) {
                user.role = wpData.at("roles");
            } else if (!m_config.roleFilter.empty()) {
                user.role = m_config.roleFilter;  // Use filter as fallback
            }

            // Collect meta fields
            for (const auto& [key, value] : wpData) {
                if (key.substr(0, 5) == "meta.") {
                    user.meta[key.substr(5)] = value;
                }
            }

            users.push_back(user);
        }

        reportProgress(static_cast<int>(users.size()), 0, "", "fetching");

        // Check if we got fewer than requested (last page)
        if (parsed.size() < static_cast<size_t>(m_config.perPage)) {
            break;
        }

        page++;
    }

    if (m_cancelled) {
        error = "Cancelled";
    }

    return users;
}

// ==================== Field Mapping ====================

void WordPressSync::setFieldMapping(const std::string& wpField, const std::string& memberField) {
    m_config.fieldMappings[wpField] = memberField;
}

} // namespace MegaCustom
