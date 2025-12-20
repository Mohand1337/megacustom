#ifndef FOLDER_MAPPER_H
#define FOLDER_MAPPER_H

#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <chrono>
#include <map>

namespace mega {
    class MegaApi;
    class MegaNode;
}

namespace MegaCustom {

/**
 * Folder mapping definition
 */
struct FolderMapping {
    std::string name;           // Unique name for this mapping
    std::string localPath;      // Local folder path (VPS)
    std::string remotePath;     // Remote folder path (MEGA)
    bool enabled = true;        // Whether this mapping is active
    std::string description;    // Optional description

    // Last sync info
    std::chrono::system_clock::time_point lastSync;
    int lastFileCount = 0;
    long long lastByteCount = 0;
};

/**
 * Upload options
 */
struct UploadOptions {
    bool dryRun = false;            // Preview only, don't upload
    bool incremental = true;        // Only upload new/changed files
    bool recursive = true;          // Include subdirectories
    bool showProgress = true;       // Display progress as it runs
    bool deleteRemoteOrphans = false;  // Delete remote files not in local
    int maxConcurrentUploads = 4;   // Parallel upload limit

    // Filters
    std::vector<std::string> excludePatterns;  // Patterns to exclude
    long long minFileSize = 0;      // Minimum file size
    long long maxFileSize = 0;      // Maximum file size (0 = unlimited)
};

/**
 * File comparison result for incremental upload
 */
struct FileCompareResult {
    std::string localPath;
    std::string remotePath;
    bool existsRemote = false;
    bool needsUpload = false;
    std::string reason;  // "new", "modified", "size_changed", "skip"
    long long localSize = 0;
    long long remoteSize = 0;
    std::chrono::system_clock::time_point localModTime;
    std::chrono::system_clock::time_point remoteModTime;
};

/**
 * Upload progress information
 */
struct MapUploadProgress {
    std::string mappingName;
    int totalFiles = 0;
    int uploadedFiles = 0;
    int skippedFiles = 0;
    int failedFiles = 0;
    long long totalBytes = 0;
    long long uploadedBytes = 0;
    std::string currentFile;
    double progressPercent = 0.0;
    double speedBytesPerSec = 0.0;
    std::chrono::seconds elapsedTime{0};
    std::chrono::seconds estimatedRemaining{0};
};

/**
 * Upload result for a single mapping
 */
struct MapUploadResult {
    std::string mappingName;
    bool success = false;
    int filesUploaded = 0;
    int filesSkipped = 0;
    int filesFailed = 0;
    long long bytesUploaded = 0;
    std::chrono::seconds duration{0};
    std::vector<std::string> errors;
    std::vector<std::string> uploadedFiles;
    std::vector<std::string> skippedFiles;
};

/**
 * Simple folder mapping for VPS-to-MEGA uploads
 * Designed for easy 1-to-1 folder synchronization
 */
class FolderMapper {
public:
    explicit FolderMapper(mega::MegaApi* megaApi);
    ~FolderMapper();

    // ========== Configuration Management ==========

    /**
     * Load mappings from config file
     * @param configPath Path to config file (default: ~/.megacustom/mappings.json)
     * @return true if loaded successfully
     */
    bool loadMappings(const std::string& configPath = "");

    /**
     * Save mappings to config file
     * @param configPath Path to config file (default: ~/.megacustom/mappings.json)
     * @return true if saved successfully
     */
    bool saveMappings(const std::string& configPath = "");

    /**
     * Add a new folder mapping
     * @param name Unique name for mapping
     * @param localPath Local folder path
     * @param remotePath Remote folder path
     * @param description Optional description
     * @return true if added successfully
     */
    bool addMapping(const std::string& name,
                   const std::string& localPath,
                   const std::string& remotePath,
                   const std::string& description = "");

    /**
     * Remove a folder mapping
     * @param nameOrIndex Name or 1-based index
     * @return true if removed successfully
     */
    bool removeMapping(const std::string& nameOrIndex);

    /**
     * Update an existing mapping
     * @param name Name of mapping to update
     * @param localPath New local path (empty to keep)
     * @param remotePath New remote path (empty to keep)
     * @return true if updated successfully
     */
    bool updateMapping(const std::string& name,
                      const std::string& localPath = "",
                      const std::string& remotePath = "");

    /**
     * Enable or disable a mapping
     * @param nameOrIndex Name or 1-based index
     * @param enabled Whether to enable
     * @return true if updated successfully
     */
    bool setMappingEnabled(const std::string& nameOrIndex, bool enabled);

    /**
     * Get a specific mapping
     * @param nameOrIndex Name or 1-based index
     * @return Mapping or nullopt if not found
     */
    std::optional<FolderMapping> getMapping(const std::string& nameOrIndex);

    /**
     * Get all mappings
     * @return Vector of all mappings
     */
    std::vector<FolderMapping> getAllMappings() const;

    /**
     * Get mapping count
     * @return Number of mappings
     */
    size_t getMappingCount() const;

    // ========== Upload Operations ==========

    /**
     * Upload a single mapping
     * @param nameOrIndex Name or 1-based index
     * @param options Upload options
     * @return Upload result
     */
    MapUploadResult uploadMapping(const std::string& nameOrIndex,
                                  const UploadOptions& options = UploadOptions());

    /**
     * Upload multiple mappings
     * @param namesOrIndices Names or 1-based indices
     * @param options Upload options
     * @return Vector of upload results
     */
    std::vector<MapUploadResult> uploadMappings(
        const std::vector<std::string>& namesOrIndices,
        const UploadOptions& options = UploadOptions());

    /**
     * Upload all enabled mappings
     * @param options Upload options
     * @return Vector of upload results
     */
    std::vector<MapUploadResult> uploadAll(const UploadOptions& options = UploadOptions());

    /**
     * Preview what would be uploaded (dry run)
     * @param nameOrIndex Name or 1-based index
     * @param options Upload options
     * @return Vector of file comparison results
     */
    std::vector<FileCompareResult> previewUpload(
        const std::string& nameOrIndex,
        const UploadOptions& options = UploadOptions());

    // ========== File Comparison ==========

    /**
     * Compare local and remote folders
     * @param localPath Local folder path
     * @param remotePath Remote folder path
     * @param recursive Include subdirectories
     * @return Vector of file comparison results
     */
    std::vector<FileCompareResult> compareFolders(
        const std::string& localPath,
        const std::string& remotePath,
        bool recursive = true);

    /**
     * Check if file needs upload (incremental check)
     * @param localPath Local file path
     * @param remoteNode Remote node to compare
     * @return Comparison result
     */
    FileCompareResult compareFile(const std::string& localPath,
                                  mega::MegaNode* remoteNode);

    // ========== Progress & Callbacks ==========

    /**
     * Set progress callback
     * @param callback Function called with progress updates
     */
    void setProgressCallback(std::function<void(const MapUploadProgress&)> callback);

    /**
     * Set file upload callback (called per file)
     * @param callback Function called when file upload completes
     */
    void setFileCallback(std::function<void(const std::string& file, bool success)> callback);

    // ========== Utility ==========

    /**
     * Get default config path
     * @return Path to default config file
     */
    static std::string getDefaultConfigPath();

    /**
     * Validate a mapping (check paths exist)
     * @param mapping Mapping to validate
     * @return Vector of validation errors (empty if valid)
     */
    std::vector<std::string> validateMapping(const FolderMapping& mapping);

    /**
     * Format size for display
     * @param bytes Size in bytes
     * @return Formatted string (e.g., "1.5 GB")
     */
    static std::string formatSize(long long bytes);

    /**
     * Format duration for display
     * @param seconds Duration in seconds
     * @return Formatted string (e.g., "2m 30s")
     */
    static std::string formatDuration(int seconds);

private:
    mega::MegaApi* m_megaApi;
    std::vector<FolderMapping> m_mappings;
    std::string m_configPath;

    // Callbacks
    std::function<void(const MapUploadProgress&)> m_progressCallback;
    std::function<void(const std::string&, bool)> m_fileCallback;

    // Internal helpers
    FolderMapping* findMapping(const std::string& nameOrIndex);
    std::vector<std::string> collectLocalFiles(const std::string& path, bool recursive);
    mega::MegaNode* getRemoteNode(const std::string& path);
    mega::MegaNode* ensureRemotePath(const std::string& path);
    std::map<std::string, mega::MegaNode*> buildRemoteFileMap(mega::MegaNode* folder,
                                                               const std::string& basePath);
    bool matchesExcludePattern(const std::string& path,
                               const std::vector<std::string>& patterns);
    void updateProgress(MapUploadProgress& progress, const std::string& currentFile,
                       long long bytesUploaded);
};

} // namespace MegaCustom

#endif // FOLDER_MAPPER_H
