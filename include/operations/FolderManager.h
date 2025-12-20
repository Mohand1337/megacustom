#ifndef FOLDER_MANAGER_H
#define FOLDER_MANAGER_H

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <optional>
#include <chrono>

namespace mega {
    class MegaApi;
    class MegaNode;
    class MegaNodeList;
}

namespace MegaCustom {

/**
 * Folder information structure
 */
struct FolderInfo {
    std::string name;
    std::string path;
    std::string handle;
    long long size;
    int fileCount;
    int folderCount;
    std::chrono::system_clock::time_point creationTime;
    std::chrono::system_clock::time_point modificationTime;
    bool isShared;
    bool isInShare;
    bool isOutShare;
    std::string owner;
};

/**
 * Folder tree node
 */
struct FolderTreeNode {
    FolderInfo info;
    std::vector<std::unique_ptr<FolderTreeNode>> children;
    std::vector<std::string> files;
    int depth;
};

/**
 * Folder operation result
 */
struct FolderOperationResult {
    bool success;
    std::string folderPath;
    std::string operationType;
    std::string errorMessage;
    int errorCode;
};

/**
 * Folder copy/move options
 */
struct FolderTransferOptions {
    bool overwriteExisting = false;
    bool mergeContents = true;
    bool preserveTimestamps = true;
    bool includeShares = false;
    bool followSymlinks = false;
    std::function<bool(const std::string&)> fileFilter;
};

/**
 * Manages folder operations in Mega
 */
class FolderManager {
public:
    explicit FolderManager(mega::MegaApi* megaApi);
    ~FolderManager();

    /**
     * Create a new folder
     * @param path Full path for the new folder
     * @param createParents Create parent folders if they don't exist
     * @return Operation result
     */
    FolderOperationResult createFolder(const std::string& path, bool createParents = true);

    /**
     * Create multiple folders
     * @param paths List of folder paths to create
     * @return Vector of operation results
     */
    std::vector<FolderOperationResult> createFolders(const std::vector<std::string>& paths);

    /**
     * Create folder structure from template
     * @param basePath Base path for the structure
     * @param templateName Template name (e.g., "project", "photo_album")
     * @return Operation result
     */
    FolderOperationResult createFromTemplate(const std::string& basePath,
                                            const std::string& templateName);

    /**
     * Delete a folder
     * @param path Folder path to delete
     * @param moveToTrash Move to trash instead of permanent deletion
     * @return Operation result
     */
    FolderOperationResult deleteFolder(const std::string& path, bool moveToTrash = true);

    /**
     * Delete multiple folders
     * @param paths List of folder paths to delete
     * @param moveToTrash Move to trash instead of permanent deletion
     * @return Vector of operation results
     */
    std::vector<FolderOperationResult> deleteFolders(const std::vector<std::string>& paths,
                                                     bool moveToTrash = true);

    /**
     * Move folder to new location
     * @param sourcePath Source folder path
     * @param destinationPath Destination path
     * @param options Transfer options
     * @return Operation result
     */
    FolderOperationResult moveFolder(const std::string& sourcePath,
                                    const std::string& destinationPath,
                                    const FolderTransferOptions& options = {});

    /**
     * Copy folder to new location
     * @param sourcePath Source folder path
     * @param destinationPath Destination path
     * @param options Transfer options
     * @return Operation result
     */
    FolderOperationResult copyFolder(const std::string& sourcePath,
                                    const std::string& destinationPath,
                                    const FolderTransferOptions& options = {});

    /**
     * Rename a folder
     * @param path Folder path
     * @param newName New folder name
     * @return Operation result
     */
    FolderOperationResult renameFolder(const std::string& path, const std::string& newName);

    /**
     * Get folder information
     * @param path Folder path
     * @return Folder info or nullopt if not found
     */
    std::optional<FolderInfo> getFolderInfo(const std::string& path);

    /**
     * List folder contents
     * @param path Folder path
     * @param recursive Include subdirectories
     * @param includeFiles Include files in listing
     * @return Vector of folder/file paths
     */
    std::vector<std::string> listContents(const std::string& path,
                                         bool recursive = false,
                                         bool includeFiles = true);

    /**
     * Get folder tree structure
     * @param path Root folder path
     * @param maxDepth Maximum depth to traverse (-1 = unlimited)
     * @return Folder tree root node
     */
    std::unique_ptr<FolderTreeNode> getFolderTree(const std::string& path,
                                                  int maxDepth = -1);

    /**
     * Search for folders
     * @param pattern Search pattern (glob or regex)
     * @param basePath Base path to search from
     * @param useRegex Use regex instead of glob
     * @return Vector of matching folder paths
     */
    std::vector<std::string> searchFolders(const std::string& pattern,
                                          const std::string& basePath = "/",
                                          bool useRegex = false);

    /**
     * Calculate folder size
     * @param path Folder path
     * @param includeSubfolders Include subfolder sizes
     * @return Total size in bytes
     */
    long long calculateFolderSize(const std::string& path, bool includeSubfolders = true);

    /**
     * Count items in folder
     * @param path Folder path
     * @param recursive Count recursively
     * @return Pair of (file count, folder count)
     */
    std::pair<int, int> countItems(const std::string& path, bool recursive = false);

    /**
     * Check if folder exists
     * @param path Folder path to check
     * @return true if folder exists
     */
    bool folderExists(const std::string& path);

    /**
     * Get or create folder (ensures folder exists)
     * @param path Folder path
     * @return Folder node or nullptr if failed
     */
    mega::MegaNode* ensureFolderExists(const std::string& path);

    /**
     * Share folder with user
     * @param path Folder path
     * @param email User email to share with
     * @param readOnly Read-only access
     * @return Operation result
     */
    FolderOperationResult shareFolder(const std::string& path,
                                     const std::string& email,
                                     bool readOnly = false);

    /**
     * Remove folder share
     * @param path Folder path
     * @param email User email to remove share from
     * @return Operation result
     */
    FolderOperationResult unshareFolder(const std::string& path,
                                       const std::string& email);

    /**
     * Get folder shares
     * @param path Folder path
     * @return Map of email to access level
     */
    std::map<std::string, std::string> getFolderShares(const std::string& path);

    /**
     * Create public link for folder
     * @param path Folder path
     * @param expireTime Optional expiration time
     * @return Public link URL or empty if failed
     */
    std::string createPublicLink(const std::string& path,
                                std::optional<std::chrono::system_clock::time_point> expireTime = {});

    /**
     * Remove public link
     * @param path Folder path
     * @return true if removed successfully
     */
    bool removePublicLink(const std::string& path);

    /**
     * Empty trash
     * @return Operation result
     */
    FolderOperationResult emptyTrash();

    /**
     * Restore from trash
     * @param path Path of item in trash
     * @param restorePath Where to restore (optional, uses original location if not specified)
     * @return Operation result
     */
    FolderOperationResult restoreFromTrash(const std::string& path,
                                          const std::string& restorePath = "");

    /**
     * Export folder structure to JSON
     * @param path Folder path
     * @param outputFile Output file path
     * @return true if exported successfully
     */
    bool exportFolderStructure(const std::string& path, const std::string& outputFile);

    /**
     * Import folder structure from JSON
     * @param inputFile Input file path
     * @param basePath Base path to create structure
     * @return Operation result
     */
    FolderOperationResult importFolderStructure(const std::string& inputFile,
                                               const std::string& basePath);

    /**
     * Set progress callback
     * @param callback Function called with progress updates
     */
    void setProgressCallback(std::function<void(const std::string&, int, int)> callback);

    /**
     * Add folder template
     * @param name Template name
     * @param structure Vector of folder paths relative to base
     */
    void addFolderTemplate(const std::string& name, const std::vector<std::string>& structure);

    /**
     * Get available folder templates
     * @return Map of template names to descriptions
     */
    std::map<std::string, std::string> getAvailableTemplates() const;

private:
    mega::MegaApi* m_megaApi;

    // Templates
    std::map<std::string, std::vector<std::string>> m_folderTemplates;

    // Callbacks
    std::function<void(const std::string&, int, int)> m_progressCallback;

    // Helper methods
    mega::MegaNode* getNodeByPath(const std::string& path);
    std::string getNodePath(mega::MegaNode* node);
    std::vector<std::string> splitPath(const std::string& path);
    std::string joinPath(const std::vector<std::string>& parts);
    void initializeTemplates();
    void traverseFolderTree(mega::MegaNode* node, FolderTreeNode* treeNode,
                           int currentDepth, int maxDepth);
    bool matchesPattern(const std::string& path, const std::string& pattern, bool useRegex);

    // Listener for folder operations
    class FolderListener;
    std::unique_ptr<FolderListener> m_listener;
};

} // namespace MegaCustom

#endif // FOLDER_MANAGER_H