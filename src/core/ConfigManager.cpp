/**
 * ConfigManager Implementation
 * Stub implementation for initial compilation
 */

#include "core/ConfigManager.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <cstdlib>

namespace fs = std::filesystem;

namespace MegaCustom {

// Helper function to get profiles directory
static std::string getProfilesDir() {
    std::string home;
#ifdef _WIN32
    char* userProfile = std::getenv("USERPROFILE");
    if (userProfile) {
        home = userProfile;
    }
#else
    char* homeEnv = std::getenv("HOME");
    if (homeEnv) {
        home = homeEnv;
    }
#endif
    if (home.empty()) {
        home = ".";
    }
    return home + "/.megacustom/profiles";
}

// Helper function to ensure profiles directory exists
static bool ensureProfilesDir() {
    try {
        std::string dir = getProfilesDir();
        if (!fs::exists(dir)) {
            return fs::create_directories(dir);
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// Private constructor for singleton
ConfigManager::ConfigManager() {
    initializeDefaults();
    m_autoSaveEnabled = false;
    m_autoSaveInterval = 300; // 5 minutes default
}

ConfigManager::~ConfigManager() {
    // Cleanup if needed
}

// Singleton instance getter
ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::loadConfig(const std::string& filePath) {
    try {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            std::cerr << "Failed to open config file: " << filePath << std::endl;
            return false;
        }

        file >> m_config;
        m_configFilePath = filePath;

        // Merge with defaults
        m_config = mergeConfigs(m_defaultConfig, m_config);

        if (m_changeCallback) {
            m_changeCallback("config_loaded");
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading config: " << e.what() << std::endl;
        return false;
    }
}

bool ConfigManager::saveConfig(const std::string& filePath) {
    try {
        std::string targetPath = filePath.empty() ? m_configFilePath : filePath;
        if (targetPath.empty()) {
            std::cerr << "No config file path specified" << std::endl;
            return false;
        }

        std::ofstream file(targetPath);
        if (!file.is_open()) {
            std::cerr << "Failed to open config file for writing: " << targetPath << std::endl;
            return false;
        }

        file << m_config.dump(4); // Pretty print with 4 spaces
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error saving config: " << e.what() << std::endl;
        return false;
    }
}

std::string ConfigManager::getString(const std::string& key, const std::string& defaultValue) const {
    try {
        auto value = navigateToKey(key);
        if (value.is_string()) {
            return value.get<std::string>();
        }
    } catch (...) {
        // Key not found or wrong type
    }
    return defaultValue;
}

int ConfigManager::getInt(const std::string& key, int defaultValue) const {
    try {
        auto value = navigateToKey(key);
        if (value.is_number_integer()) {
            return value.get<int>();
        }
    } catch (...) {
        // Key not found or wrong type
    }
    return defaultValue;
}

double ConfigManager::getDouble(const std::string& key, double defaultValue) const {
    try {
        auto value = navigateToKey(key);
        if (value.is_number()) {
            return value.get<double>();
        }
    } catch (...) {
        // Key not found or wrong type
    }
    return defaultValue;
}

bool ConfigManager::getBool(const std::string& key, bool defaultValue) const {
    try {
        auto value = navigateToKey(key);
        if (value.is_boolean()) {
            return value.get<bool>();
        }
    } catch (...) {
        // Key not found or wrong type
    }
    return defaultValue;
}

void ConfigManager::setString(const std::string& key, const std::string& value) {
    setValueAtKey(key, nlohmann::json(value));
    notifyChange(key);
}

void ConfigManager::setInt(const std::string& key, int value) {
    setValueAtKey(key, nlohmann::json(value));
    notifyChange(key);
}

void ConfigManager::setDouble(const std::string& key, double value) {
    setValueAtKey(key, nlohmann::json(value));
    notifyChange(key);
}

void ConfigManager::setBool(const std::string& key, bool value) {
    setValueAtKey(key, nlohmann::json(value));
    notifyChange(key);
}

bool ConfigManager::hasKey(const std::string& key) const {
    try {
        navigateToKey(key);
        return true;
    } catch (...) {
        return false;
    }
}

void ConfigManager::clear() {
    m_config.clear();
    notifyChange("config_cleared");
}

void ConfigManager::resetToDefaults() {
    m_config = m_defaultConfig;
    notifyChange("config_reset");
}

std::string ConfigManager::exportToJson(bool prettyPrint) const {
    if (prettyPrint) {
        return m_config.dump(4);
    }
    return m_config.dump();
}

bool ConfigManager::importFromJson(const std::string& jsonString) {
    try {
        m_config = nlohmann::json::parse(jsonString);
        notifyChange("config_imported");
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error importing JSON: " << e.what() << std::endl;
        return false;
    }
}

void ConfigManager::setChangeCallback(std::function<void(const std::string&)> callback) {
    m_changeCallback = callback;
}

// Helper methods
void ConfigManager::initializeDefaults() {
    m_defaultConfig = {
        {"auth", {
            {"sessionFile", "~/.megacustom/session.enc"},
            {"use2FA", true},
            {"autoLogin", false},
            {"sessionTimeout", 1440}
        }},
        {"transfers", {
            {"maxConcurrent", 4},
            {"chunkSize", 10485760},
            {"bandwidthLimit", 0},
            {"retryAttempts", 3},
            {"retryDelay", 5}
        }},
        {"sync", {
            {"defaultDirection", "bidirectional"},
            {"conflictResolution", "newer-wins"},
            {"createBackups", true},
            {"maxBackupVersions", 5},
            {"syncInterval", 30}
        }},
        {"rename", {
            {"safeMode", true},
            {"preserveExtension", true},
            {"maxUndoHistory", 50},
            {"previewByDefault", true}
        }},
        {"ui", {
            {"theme", "default"},
            {"language", "en"},
            {"showProgressBar", true},
            {"confirmDangerousOps", true},
            {"logLevel", 2}
        }}
    };

    m_config = m_defaultConfig;
}

nlohmann::json ConfigManager::navigateToKey(const std::string& key) const {
    auto keys = splitKey(key);
    nlohmann::json current = m_config;

    for (const auto& k : keys) {
        if (current.contains(k)) {
            current = current[k];
        } else {
            throw std::runtime_error("Key not found: " + key);
        }
    }

    return current;
}

void ConfigManager::setValueAtKey(const std::string& key, const nlohmann::json& value) {
    auto keys = splitKey(key);
    if (keys.empty()) return;

    nlohmann::json* current = &m_config;

    for (size_t i = 0; i < keys.size() - 1; ++i) {
        if (!current->contains(keys[i])) {
            (*current)[keys[i]] = nlohmann::json::object();
        }
        current = &(*current)[keys[i]];
    }

    (*current)[keys.back()] = value;
}

std::vector<std::string> ConfigManager::splitKey(const std::string& key) const {
    std::vector<std::string> result;
    std::stringstream ss(key);
    std::string item;

    while (std::getline(ss, item, '.')) {
        if (!item.empty()) {
            result.push_back(item);
        }
    }

    return result;
}

void ConfigManager::notifyChange(const std::string& key) {
    if (m_changeCallback) {
        m_changeCallback(key);
    }

    // Auto-save if enabled
    if (m_autoSaveEnabled && !m_configFilePath.empty()) {
        saveConfig();
    }
}

nlohmann::json ConfigManager::getDefaultConfig() {
    ConfigManager& instance = getInstance();
    return instance.m_defaultConfig;
}

nlohmann::json ConfigManager::mergeConfigs(const nlohmann::json& base, const nlohmann::json& overlay) {
    nlohmann::json result = base;

    for (auto it = overlay.begin(); it != overlay.end(); ++it) {
        const std::string& key = it->first;
        const nlohmann::json& value = it->second;

        if (result.contains(key) && result[key].is_object() && value.is_object()) {
            result[key] = mergeConfigs(result[key], value);
        } else {
            result[key] = value;
        }
    }

    return result;
}

// Get specific configuration structures
ConfigManager::AuthConfig ConfigManager::getAuthConfig() const {
    AuthConfig config;
    config.sessionFile = getString("auth.sessionFile", "~/.megacustom/session.enc");
    config.use2FA = getBool("auth.use2FA", true);
    config.autoLogin = getBool("auth.autoLogin", false);
    config.sessionTimeout = getInt("auth.sessionTimeout", 1440);
    return config;
}

ConfigManager::TransferConfig ConfigManager::getTransferConfig() const {
    TransferConfig config;
    config.maxConcurrent = getInt("transfers.maxConcurrent", 4);
    config.chunkSize = getInt("transfers.chunkSize", 10485760);
    config.bandwidthLimit = getInt("transfers.bandwidthLimit", 0);
    config.retryAttempts = getInt("transfers.retryAttempts", 3);
    config.retryDelay = getInt("transfers.retryDelay", 5);
    return config;
}

ConfigManager::SyncConfig ConfigManager::getSyncConfig() const {
    SyncConfig config;
    config.defaultDirection = getString("sync.defaultDirection", "bidirectional");
    config.conflictResolution = getString("sync.conflictResolution", "newer-wins");
    config.createBackups = getBool("sync.createBackups", true);
    config.maxBackupVersions = getInt("sync.maxBackupVersions", 5);
    config.syncInterval = getInt("sync.syncInterval", 30);
    return config;
}

ConfigManager::RenameConfig ConfigManager::getRenameConfig() const {
    RenameConfig config;
    config.safeMode = getBool("rename.safeMode", true);
    config.preserveExtension = getBool("rename.preserveExtension", true);
    config.maxUndoHistory = getInt("rename.maxUndoHistory", 50);
    config.previewByDefault = getBool("rename.previewByDefault", true);
    return config;
}

ConfigManager::UIConfig ConfigManager::getUIConfig() const {
    UIConfig config;
    config.theme = getString("ui.theme", "default");
    config.language = getString("ui.language", "en");
    config.showProgressBar = getBool("ui.showProgressBar", true);
    config.confirmDangerousOps = getBool("ui.confirmDangerousOps", true);
    config.logLevel = getInt("ui.logLevel", 2);
    return config;
}

// Profile management implementations
bool ConfigManager::loadProfile(const std::string& profileName) {
    try {
        std::string profilePath = getProfilesDir() + "/" + profileName + ".json";

        // Check if profile exists on disk
        if (!fs::exists(profilePath)) {
            std::cerr << "Profile not found: " << profileName << std::endl;
            return false;
        }

        // Read profile from file
        std::ifstream file(profilePath);
        if (!file.is_open()) {
            std::cerr << "Failed to open profile file: " << profilePath << std::endl;
            return false;
        }

        nlohmann::json profileJson;
        file >> profileJson;
        file.close();

        // Validate profile structure
        if (!profileJson.contains("name") || !profileJson.contains("settings")) {
            std::cerr << "Invalid profile format: " << profileName << std::endl;
            return false;
        }

        // Load profile settings into current config
        nlohmann::json settings = profileJson["settings"];
        m_config = mergeConfigs(m_defaultConfig, settings);

        // Update in-memory profile cache
        ConfigProfile profile;
        // Use explicit access with default fallbacks (json_simple doesn't have .value())
        profile.name = profileJson["name"].is_string() ? profileJson["name"].get<std::string>() : profileName;
        profile.description = profileJson["description"].is_string() ? profileJson["description"].get<std::string>() : "";
        profile.settings = settings;
        profile.isDefault = profileJson["isDefault"].is_boolean() ? profileJson["isDefault"].get<bool>() : false;

        // Parse lastModified timestamp
        if (profileJson.contains("lastModified") && profileJson["lastModified"].is_number()) {
            auto timestamp = static_cast<time_t>(profileJson["lastModified"].get<int>());
            profile.lastModified = std::chrono::system_clock::from_time_t(timestamp);
        } else {
            profile.lastModified = std::chrono::system_clock::now();
        }

        m_profiles[profileName] = profile;

        if (m_changeCallback) {
            m_changeCallback("profile_loaded:" + profileName);
        }

        std::cout << "Profile loaded: " << profileName << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading profile: " << e.what() << std::endl;
        return false;
    }
}

bool ConfigManager::saveProfile(const std::string& profileName, const std::string& description) {
    try {
        // Ensure profiles directory exists
        if (!ensureProfilesDir()) {
            std::cerr << "Failed to create profiles directory" << std::endl;
            return false;
        }

        std::string profilePath = getProfilesDir() + "/" + profileName + ".json";

        // Create profile object
        ConfigProfile profile;
        profile.name = profileName;
        profile.description = description;
        profile.settings = m_config;
        profile.isDefault = false;
        profile.lastModified = std::chrono::system_clock::now();

        // Convert to JSON
        nlohmann::json profileJson;
        profileJson["name"] = profile.name;
        profileJson["description"] = profile.description;
        profileJson["settings"] = profile.settings;
        profileJson["isDefault"] = profile.isDefault;
        // Cast time_t to int for json_simple compatibility
        profileJson["lastModified"] = static_cast<int>(std::chrono::system_clock::to_time_t(profile.lastModified));

        // Write to file
        std::ofstream file(profilePath);
        if (!file.is_open()) {
            std::cerr << "Failed to open profile file for writing: " << profilePath << std::endl;
            return false;
        }

        file << profileJson.dump(4);  // Pretty print with 4 spaces
        file.close();

        // Update in-memory cache
        m_profiles[profileName] = profile;

        if (m_changeCallback) {
            m_changeCallback("profile_saved:" + profileName);
        }

        std::cout << "Profile saved: " << profileName << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error saving profile: " << e.what() << std::endl;
        return false;
    }
}

std::vector<std::string> ConfigManager::listProfiles() const {
    std::vector<std::string> profiles;

    try {
        std::string profilesDir = getProfilesDir();

        // Check if directory exists
        if (!fs::exists(profilesDir)) {
            return profiles;
        }

        // Scan for JSON files in profiles directory
        for (const auto& entry : fs::directory_iterator(profilesDir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (filename.size() > 5 && filename.substr(filename.size() - 5) == ".json") {
                    // Remove .json extension
                    profiles.push_back(filename.substr(0, filename.size() - 5));
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error listing profiles: " << e.what() << std::endl;
    }

    return profiles;
}

bool ConfigManager::deleteProfile(const std::string& profileName) {
    try {
        std::string profilePath = getProfilesDir() + "/" + profileName + ".json";

        // Check if profile exists
        if (!fs::exists(profilePath)) {
            std::cerr << "Profile not found: " << profileName << std::endl;
            return false;
        }

        // Remove from disk
        if (!fs::remove(profilePath)) {
            std::cerr << "Failed to delete profile file: " << profilePath << std::endl;
            return false;
        }

        // Remove from in-memory cache
        m_profiles.erase(profileName);

        if (m_changeCallback) {
            m_changeCallback("profile_deleted:" + profileName);
        }

        std::cout << "Profile deleted: " << profileName << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error deleting profile: " << e.what() << std::endl;
        return false;
    }
}

std::vector<std::string> ConfigManager::getArray(const std::string& key) const {
    // TODO: Implement array getter
    return std::vector<std::string>();
}

nlohmann::json ConfigManager::getObject(const std::string& key) const {
    try {
        return navigateToKey(key);
    } catch (...) {
        return nlohmann::json::object();
    }
}

void ConfigManager::setArray(const std::string& key, const std::vector<std::string>& value) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& item : value) {
        arr.push_back(item);
    }
    setValueAtKey(key, arr);
    notifyChange(key);
}

void ConfigManager::setObject(const std::string& key, const nlohmann::json& value) {
    setValueAtKey(key, value);
    notifyChange(key);
}

} // namespace MegaCustom