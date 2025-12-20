#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <any>
#include <functional>
#include <optional>
#include <chrono>
// Use simple JSON if nlohmann/json is not available
#ifdef USE_NLOHMANN_JSON
#include <nlohmann/json.hpp>
#else
#include "json_simple.hpp"
#endif

namespace MegaCustom {

/**
 * Configuration value type
 */
enum class ConfigType {
    STRING,
    INTEGER,
    DOUBLE,
    BOOLEAN,
    ARRAY,
    OBJECT
};

/**
 * Configuration schema validation
 */
struct ConfigSchema {
    std::string key;
    ConfigType type;
    bool required;
    std::any defaultValue;
    std::function<bool(const std::any&)> validator;
    std::string description;
};

/**
 * Configuration profile
 */
struct ConfigProfile {
    std::string name;
    std::string description;
    nlohmann::json settings;
    bool isDefault;
    std::chrono::system_clock::time_point lastModified;
};

/**
 * Manages application configuration
 */
class ConfigManager {
public:
    // Singleton pattern
    static ConfigManager& getInstance();

    // Delete copy constructor and assignment
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    /**
     * Load configuration from file
     * @param filePath Path to configuration file
     * @return true if loaded successfully
     */
    bool loadConfig(const std::string& filePath);

    /**
     * Save configuration to file
     * @param filePath Path to save configuration
     * @return true if saved successfully
     */
    bool saveConfig(const std::string& filePath = "");

    /**
     * Load configuration profile
     * @param profileName Profile to load
     * @return true if loaded successfully
     */
    bool loadProfile(const std::string& profileName);

    /**
     * Save current configuration as profile
     * @param profileName Profile name
     * @param description Profile description
     * @return true if saved successfully
     */
    bool saveProfile(const std::string& profileName, const std::string& description = "");

    /**
     * List available profiles
     * @return Vector of profile names
     */
    std::vector<std::string> listProfiles() const;

    /**
     * Delete a profile
     * @param profileName Profile to delete
     * @return true if deleted successfully
     */
    bool deleteProfile(const std::string& profileName);

    /**
     * Get string value
     * @param key Configuration key
     * @param defaultValue Default if not found
     * @return Configuration value
     */
    std::string getString(const std::string& key, const std::string& defaultValue = "") const;

    /**
     * Get integer value
     * @param key Configuration key
     * @param defaultValue Default if not found
     * @return Configuration value
     */
    int getInt(const std::string& key, int defaultValue = 0) const;

    /**
     * Get double value
     * @param key Configuration key
     * @param defaultValue Default if not found
     * @return Configuration value
     */
    double getDouble(const std::string& key, double defaultValue = 0.0) const;

    /**
     * Get boolean value
     * @param key Configuration key
     * @param defaultValue Default if not found
     * @return Configuration value
     */
    bool getBool(const std::string& key, bool defaultValue = false) const;

    /**
     * Get array value
     * @param key Configuration key
     * @return Array as vector of strings
     */
    std::vector<std::string> getArray(const std::string& key) const;

    /**
     * Get object value as JSON
     * @param key Configuration key
     * @return JSON object
     */
    nlohmann::json getObject(const std::string& key) const;

    /**
     * Set string value
     * @param key Configuration key
     * @param value Value to set
     */
    void setString(const std::string& key, const std::string& value);

    /**
     * Set integer value
     * @param key Configuration key
     * @param value Value to set
     */
    void setInt(const std::string& key, int value);

    /**
     * Set double value
     * @param key Configuration key
     * @param value Value to set
     */
    void setDouble(const std::string& key, double value);

    /**
     * Set boolean value
     * @param key Configuration key
     * @param value Value to set
     */
    void setBool(const std::string& key, bool value);

    /**
     * Set array value
     * @param key Configuration key
     * @param value Array values
     */
    void setArray(const std::string& key, const std::vector<std::string>& value);

    /**
     * Set object value
     * @param key Configuration key
     * @param value JSON object
     */
    void setObject(const std::string& key, const nlohmann::json& value);

    /**
     * Check if key exists
     * @param key Configuration key
     * @return true if key exists
     */
    bool hasKey(const std::string& key) const;

    /**
     * Remove a key
     * @param key Configuration key to remove
     */
    void removeKey(const std::string& key);

    /**
     * Get all keys
     * @return Vector of all configuration keys
     */
    std::vector<std::string> getAllKeys() const;

    /**
     * Clear all configuration
     */
    void clear();

    /**
     * Reset to defaults
     */
    void resetToDefaults();

    /**
     * Add configuration schema for validation
     * @param schema Schema definition
     */
    void addSchema(const ConfigSchema& schema);

    /**
     * Validate configuration against schema
     * @return Vector of validation errors (empty if valid)
     */
    std::vector<std::string> validate() const;

    /**
     * Set configuration change callback
     * @param callback Function called when configuration changes
     */
    void setChangeCallback(std::function<void(const std::string&)> callback);

    /**
     * Export configuration to JSON string
     * @param prettyPrint Format with indentation
     * @return JSON string
     */
    std::string exportToJson(bool prettyPrint = true) const;

    /**
     * Import configuration from JSON string
     * @param jsonString JSON configuration
     * @return true if imported successfully
     */
    bool importFromJson(const std::string& jsonString);

    /**
     * Get configuration for specific module
     * @param moduleName Module name
     * @return Module configuration object
     */
    nlohmann::json getModuleConfig(const std::string& moduleName) const;

    /**
     * Set configuration for specific module
     * @param moduleName Module name
     * @param config Module configuration
     */
    void setModuleConfig(const std::string& moduleName, const nlohmann::json& config);

    /**
     * Enable auto-save
     * @param enable Enable flag
     * @param interval Save interval in seconds
     */
    void enableAutoSave(bool enable, int interval = 300);

    /**
     * Watch configuration file for changes
     * @param filePath File to watch
     * @param enable Enable watching
     */
    void watchConfigFile(const std::string& filePath, bool enable);

    /**
     * Get default configuration
     * @return Default configuration object
     */
    static nlohmann::json getDefaultConfig();

    /**
     * Merge configurations (second overrides first)
     * @param base Base configuration
     * @param overlay Overlay configuration
     * @return Merged configuration
     */
    static nlohmann::json mergeConfigs(const nlohmann::json& base,
                                       const nlohmann::json& overlay);

    // Specific application configurations
    struct AuthConfig {
        std::string sessionFile;
        bool use2FA;
        bool autoLogin;
        int sessionTimeout;  // minutes
    };

    struct TransferConfig {
        int maxConcurrent;
        size_t chunkSize;
        int bandwidthLimit;
        int retryAttempts;
        int retryDelay;  // seconds
    };

    struct SyncConfig {
        std::string defaultDirection;
        std::string conflictResolution;
        bool createBackups;
        int maxBackupVersions;
        int syncInterval;  // minutes
    };

    struct RenameConfig {
        bool safeMode;
        bool preserveExtension;
        int maxUndoHistory;
        bool previewByDefault;
    };

    struct UIConfig {
        std::string theme;
        std::string language;
        bool showProgressBar;
        bool confirmDangerousOps;
        int logLevel;
    };

    /**
     * Get authentication configuration
     * @return Auth configuration
     */
    AuthConfig getAuthConfig() const;

    /**
     * Get transfer configuration
     * @return Transfer configuration
     */
    TransferConfig getTransferConfig() const;

    /**
     * Get sync configuration
     * @return Sync configuration
     */
    SyncConfig getSyncConfig() const;

    /**
     * Get rename configuration
     * @return Rename configuration
     */
    RenameConfig getRenameConfig() const;

    /**
     * Get UI configuration
     * @return UI configuration
     */
    UIConfig getUIConfig() const;

private:
    // Private constructor for singleton
    ConfigManager();
    ~ConfigManager();

    // Configuration storage
    nlohmann::json m_config;
    nlohmann::json m_defaultConfig;
    std::map<std::string, ConfigProfile> m_profiles;
    std::vector<ConfigSchema> m_schemas;

    // File management
    std::string m_configFilePath;
    bool m_autoSaveEnabled;
    int m_autoSaveInterval;

    // Callbacks
    std::function<void(const std::string&)> m_changeCallback;

    // Helper methods
    void initializeDefaults();
    nlohmann::json navigateToKey(const std::string& key) const;
    void setValueAtKey(const std::string& key, const nlohmann::json& value);
    std::vector<std::string> splitKey(const std::string& key) const;
    bool validateValue(const ConfigSchema& schema, const std::any& value) const;
    void autoSaveThread();
    void watchFileThread(const std::string& filePath);
    void notifyChange(const std::string& key);
};

} // namespace MegaCustom

#endif // CONFIG_MANAGER_H