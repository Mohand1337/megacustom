/**
 * FolderManager Implementation
 * Manages folder operations in Mega cloud storage
 */

#include "operations/FolderManager.h"
#include "core/MegaManager.h"
#include <megaapi.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>
#include <chrono>
#include <algorithm>
#include <queue>
#include <condition_variable>
#include <json_simple.hpp>

namespace MegaCustom {

// Internal listener for folder operations
class FolderManager::FolderListener : public mega::MegaRequestListener {
public:
    FolderListener() : m_finished(false), m_success(false), m_errorCode(0) {}

    void onRequestFinish(mega::MegaApi* api, mega::MegaRequest* request, mega::MegaError* error) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_success = (error->getErrorCode() == mega::MegaError::API_OK);
        m_errorCode = error->getErrorCode();
        m_errorString = error->getErrorString() ? error->getErrorString() : "";

        if (request->getNodeHandle()) {
            m_nodeHandle = request->getNodeHandle();
        }

        if (request->getLink()) {
            m_publicLink = request->getLink();
        }

        m_finished = true;
        m_cv.notify_all();
    }

    bool waitForCompletion(int timeoutSeconds = 30) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, std::chrono::seconds(timeoutSeconds),
                            [this] { return m_finished; });
    }

    bool isSuccess() const { return m_success; }
    int getErrorCode() const { return m_errorCode; }
    std::string getErrorString() const { return m_errorString; }
    mega::MegaHandle getNodeHandle() const { return m_nodeHandle; }
    std::string getPublicLink() const { return m_publicLink; }

    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_finished = false;
        m_success = false;
        m_errorCode = 0;
        m_errorString.clear();
        m_nodeHandle = mega::INVALID_HANDLE;
        m_publicLink.clear();
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_finished;
    bool m_success;
    int m_errorCode;
    std::string m_errorString;
    mega::MegaHandle m_nodeHandle = mega::INVALID_HANDLE;
    std::string m_publicLink;
};

// Constructor
FolderManager::FolderManager(mega::MegaApi* megaApi)
    : m_megaApi(megaApi),
      m_listener(std::make_unique<FolderListener>()) {
    initializeTemplates();
}

// Destructor
FolderManager::~FolderManager() = default;

// Create a new folder
FolderOperationResult FolderManager::createFolder(const std::string& path, bool createParents) {
    FolderOperationResult result;
    result.folderPath = path;
    result.operationType = "create";

    if (!m_megaApi) {
        result.success = false;
        result.errorMessage = "Mega API not initialized";
        result.errorCode = -1;
        return result;
    }

    // Split path into components
    std::vector<std::string> pathComponents = splitPath(path);
    if (pathComponents.empty()) {
        result.success = false;
        result.errorMessage = "Invalid path";
        result.errorCode = -1;
        return result;
    }

    // Start from root
    std::unique_ptr<mega::MegaNode> currentNode(m_megaApi->getRootNode());
    if (!currentNode) {
        result.success = false;
        result.errorMessage = "Cannot access root node";
        result.errorCode = -1;
        return result;
    }

    // Navigate/create path
    std::string currentPath = "";
    for (const auto& component : pathComponents) {
        currentPath += "/" + component;

        // Check if folder exists
        std::unique_ptr<mega::MegaNode> childNode(
            m_megaApi->getChildNode(currentNode.get(), component.c_str())
        );

        if (childNode && childNode->isFolder()) {
            // Folder exists, move to it
            currentNode = std::move(childNode);
        } else if (!childNode) {
            // Folder doesn't exist, create it if allowed
            if (!createParents && currentPath != path) {
                result.success = false;
                result.errorMessage = "Parent folder doesn't exist: " + currentPath;
                result.errorCode = mega::MegaError::API_ENOENT;
                return result;
            }

            // Create the folder
            m_listener->reset();
            m_megaApi->createFolder(component.c_str(), currentNode.get(), m_listener.get());

            if (!m_listener->waitForCompletion()) {
                result.success = false;
                result.errorMessage = "Timeout creating folder: " + currentPath;
                result.errorCode = mega::MegaError::API_EFAILED;
                return result;
            }

            if (!m_listener->isSuccess()) {
                result.success = false;
                result.errorMessage = "Failed to create folder: " + m_listener->getErrorString();
                result.errorCode = m_listener->getErrorCode();
                return result;
            }

            // Get the newly created folder
            mega::MegaHandle newHandle = m_listener->getNodeHandle();
            currentNode.reset(m_megaApi->getNodeByHandle(newHandle));

            if (!currentNode) {
                result.success = false;
                result.errorMessage = "Failed to retrieve created folder";
                result.errorCode = mega::MegaError::API_ENOENT;
                return result;
            }
        } else {
            // Path component exists but is not a folder
            result.success = false;
            result.errorMessage = "Path component is not a folder: " + component;
            result.errorCode = mega::MegaError::API_EEXIST;
            return result;
        }
    }

    result.success = true;
    result.errorMessage = "Folder created successfully";
    result.errorCode = mega::MegaError::API_OK;
    return result;
}

// Create multiple folders
std::vector<FolderOperationResult> FolderManager::createFolders(const std::vector<std::string>& paths) {
    std::vector<FolderOperationResult> results;

    for (const auto& path : paths) {
        results.push_back(createFolder(path, true));

        // Notify progress if callback is set
        if (m_progressCallback) {
            int current = results.size();
            int total = paths.size();
            m_progressCallback("Creating folders", current, total);
        }
    }

    return results;
}

// Create folder structure from template
FolderOperationResult FolderManager::createFromTemplate(const std::string& basePath,
                                                       const std::string& templateName) {
    FolderOperationResult result;
    result.folderPath = basePath;
    result.operationType = "create_from_template";

    auto it = m_folderTemplates.find(templateName);
    if (it == m_folderTemplates.end()) {
        result.success = false;
        result.errorMessage = "Template not found: " + templateName;
        result.errorCode = -1;
        return result;
    }

    // Create base folder first
    result = createFolder(basePath, true);
    if (!result.success) {
        return result;
    }

    // Create template structure
    const auto& templatePaths = it->second;
    for (const auto& relativePath : templatePaths) {
        std::string fullPath = basePath + "/" + relativePath;
        FolderOperationResult subResult = createFolder(fullPath, true);

        if (!subResult.success) {
            result.success = false;
            result.errorMessage = "Failed to create template folder: " + fullPath;
            result.errorCode = subResult.errorCode;
            return result;
        }
    }

    result.success = true;
    result.errorMessage = "Template structure created successfully";
    return result;
}

// Delete a folder or file
FolderOperationResult FolderManager::deleteFolder(const std::string& path, bool moveToTrash) {
    FolderOperationResult result;
    result.folderPath = path;
    result.operationType = moveToTrash ? "move_to_trash" : "delete";

    std::unique_ptr<mega::MegaNode> node(getNodeByPath(path));
    if (!node) {
        result.success = false;
        result.errorMessage = "Item not found: " + path;
        result.errorCode = mega::MegaError::API_ENOENT;
        return result;
    }

    // Note: This function works for both files and folders

    m_listener->reset();

    if (moveToTrash) {
        mega::MegaNode* rubbishNode = m_megaApi->getRubbishNode();
        if (!rubbishNode) {
            result.success = false;
            result.errorMessage = "Cannot access trash";
            result.errorCode = mega::MegaError::API_EACCESS;
            return result;
        }
        m_megaApi->moveNode(node.get(), rubbishNode, m_listener.get());
        delete rubbishNode;
    } else {
        m_megaApi->remove(node.get(), m_listener.get());
    }

    if (!m_listener->waitForCompletion()) {
        result.success = false;
        result.errorMessage = "Timeout deleting item";
        result.errorCode = mega::MegaError::API_EFAILED;
        return result;
    }

    result.success = m_listener->isSuccess();
    result.errorMessage = result.success ? "Item deleted successfully" : m_listener->getErrorString();
    result.errorCode = m_listener->getErrorCode();

    return result;
}

// Delete multiple folders
std::vector<FolderOperationResult> FolderManager::deleteFolders(const std::vector<std::string>& paths,
                                                               bool moveToTrash) {
    std::vector<FolderOperationResult> results;

    for (const auto& path : paths) {
        results.push_back(deleteFolder(path, moveToTrash));

        if (m_progressCallback) {
            int current = results.size();
            int total = paths.size();
            m_progressCallback("Deleting folders", current, total);
        }
    }

    return results;
}

// Move folder to new location
FolderOperationResult FolderManager::moveFolder(const std::string& sourcePath,
                                               const std::string& destinationPath,
                                               const FolderTransferOptions& options) {
    FolderOperationResult result;
    result.folderPath = sourcePath;
    result.operationType = "move";

    std::unique_ptr<mega::MegaNode> sourceNode(getNodeByPath(sourcePath));
    if (!sourceNode) {
        result.success = false;
        result.errorMessage = "Source folder not found: " + sourcePath;
        result.errorCode = mega::MegaError::API_ENOENT;
        return result;
    }

    if (!sourceNode->isFolder()) {
        result.success = false;
        result.errorMessage = "Source is not a folder: " + sourcePath;
        result.errorCode = mega::MegaError::API_EARGS;
        return result;
    }

    // Get or create destination parent
    std::unique_ptr<mega::MegaNode> destParent(ensureFolderExists(destinationPath));
    if (!destParent) {
        result.success = false;
        result.errorMessage = "Cannot create destination: " + destinationPath;
        result.errorCode = mega::MegaError::API_EFAILED;
        return result;
    }

    // Check for existing folder with same name
    std::string folderName = sourceNode->getName();
    std::unique_ptr<mega::MegaNode> existingNode(
        m_megaApi->getChildNode(destParent.get(), folderName.c_str())
    );

    if (existingNode) {
        if (!options.overwriteExisting) {
            result.success = false;
            result.errorMessage = "Folder already exists at destination";
            result.errorCode = mega::MegaError::API_EEXIST;
            return result;
        }

        // Delete existing if overwrite is enabled
        FolderOperationResult deleteResult = deleteFolder(
            destinationPath + "/" + folderName, false
        );

        if (!deleteResult.success) {
            result.success = false;
            result.errorMessage = "Failed to overwrite existing folder";
            result.errorCode = deleteResult.errorCode;
            return result;
        }
    }

    // Perform the move
    m_listener->reset();
    m_megaApi->moveNode(sourceNode.get(), destParent.get(), m_listener.get());

    if (!m_listener->waitForCompletion()) {
        result.success = false;
        result.errorMessage = "Timeout moving folder";
        result.errorCode = mega::MegaError::API_EFAILED;
        return result;
    }

    result.success = m_listener->isSuccess();
    result.errorMessage = result.success ? "Folder moved successfully" : m_listener->getErrorString();
    result.errorCode = m_listener->getErrorCode();

    return result;
}

// Copy folder to new location
FolderOperationResult FolderManager::copyFolder(const std::string& sourcePath,
                                               const std::string& destinationPath,
                                               const FolderTransferOptions& options) {
    FolderOperationResult result;
    result.folderPath = sourcePath;
    result.operationType = "copy";

    std::unique_ptr<mega::MegaNode> sourceNode(getNodeByPath(sourcePath));
    if (!sourceNode) {
        result.success = false;
        result.errorMessage = "Source folder not found: " + sourcePath;
        result.errorCode = mega::MegaError::API_ENOENT;
        return result;
    }

    if (!sourceNode->isFolder()) {
        result.success = false;
        result.errorMessage = "Source is not a folder: " + sourcePath;
        result.errorCode = mega::MegaError::API_EARGS;
        return result;
    }

    // Get or create destination parent
    std::unique_ptr<mega::MegaNode> destParent(ensureFolderExists(destinationPath));
    if (!destParent) {
        result.success = false;
        result.errorMessage = "Cannot create destination: " + destinationPath;
        result.errorCode = mega::MegaError::API_EFAILED;
        return result;
    }

    // Perform the copy
    m_listener->reset();
    m_megaApi->copyNode(sourceNode.get(), destParent.get(), m_listener.get());

    if (!m_listener->waitForCompletion()) {
        result.success = false;
        result.errorMessage = "Timeout copying folder";
        result.errorCode = mega::MegaError::API_EFAILED;
        return result;
    }

    result.success = m_listener->isSuccess();
    result.errorMessage = result.success ? "Folder copied successfully" : m_listener->getErrorString();
    result.errorCode = m_listener->getErrorCode();

    return result;
}

// Rename a folder
FolderOperationResult FolderManager::renameFolder(const std::string& path, const std::string& newName) {
    FolderOperationResult result;
    result.folderPath = path;
    result.operationType = "rename";

    std::unique_ptr<mega::MegaNode> node(getNodeByPath(path));
    if (!node) {
        result.success = false;
        result.errorMessage = "Node not found: " + path;
        result.errorCode = mega::MegaError::API_ENOENT;
        return result;
    }

    // Note: renameNode works for both files and folders
    // Removed folder-only check to support file renaming

    m_listener->reset();
    m_megaApi->renameNode(node.get(), newName.c_str(), m_listener.get());

    if (!m_listener->waitForCompletion()) {
        result.success = false;
        result.errorMessage = "Timeout renaming node";
        result.errorCode = mega::MegaError::API_EFAILED;
        return result;
    }

    result.success = m_listener->isSuccess();
    result.errorMessage = result.success ? "Node renamed successfully" : m_listener->getErrorString();
    result.errorCode = m_listener->getErrorCode();

    return result;
}

// Get folder information
std::optional<FolderInfo> FolderManager::getFolderInfo(const std::string& path) {
    std::unique_ptr<mega::MegaNode> node(getNodeByPath(path));
    if (!node || !node->isFolder()) {
        return std::nullopt;
    }

    FolderInfo info;
    info.name = node->getName() ? node->getName() : "";
    info.path = path;
    info.handle = std::to_string(node->getHandle());

    // Calculate size and count items
    auto counts = countItems(path, true);
    info.fileCount = counts.first;
    info.folderCount = counts.second;
    info.size = calculateFolderSize(path, true);

    // Set timestamps
    info.creationTime = std::chrono::system_clock::from_time_t(node->getCreationTime());
    info.modificationTime = std::chrono::system_clock::from_time_t(node->getModificationTime());

    // Share information
    info.isShared = node->isShared();
    info.isInShare = node->isInShare();
    info.isOutShare = node->isOutShare();

    if (info.isInShare) {
        // Owner is a handle, convert to string
        mega::MegaHandle ownerHandle = node->getOwner();
        if (ownerHandle != mega::INVALID_HANDLE) {
            info.owner = std::to_string(ownerHandle);
        }
    }

    return info;
}

// List folder contents
std::vector<std::string> FolderManager::listContents(const std::string& path,
                                                    bool recursive,
                                                    bool includeFiles) {
    std::vector<std::string> contents;

    std::unique_ptr<mega::MegaNode> node(getNodeByPath(path));
    if (!node || !node->isFolder()) {
        return contents;
    }

    std::queue<std::pair<std::unique_ptr<mega::MegaNode>, std::string>> queue;
    queue.push({std::move(node), path});

    while (!queue.empty()) {
        auto current = std::move(queue.front());
        queue.pop();

        std::unique_ptr<mega::MegaNodeList> children(
            m_megaApi->getChildren(current.first.get())
        );

        if (children) {
            for (int i = 0; i < children->size(); i++) {
                mega::MegaNode* child = children->get(i);
                std::string childPath = current.second + "/" + child->getName();

                if (child->isFolder()) {
                    contents.push_back(childPath);

                    if (recursive) {
                        queue.push({
                            std::unique_ptr<mega::MegaNode>(child->copy()),
                            childPath
                        });
                    }
                } else if (includeFiles) {
                    contents.push_back(childPath);
                }
            }
        }
    }

    return contents;
}

// Get folder tree structure
std::unique_ptr<FolderTreeNode> FolderManager::getFolderTree(const std::string& path,
                                                            int maxDepth) {
    std::unique_ptr<mega::MegaNode> node(getNodeByPath(path));
    if (!node || !node->isFolder()) {
        return nullptr;
    }

    auto rootNode = std::make_unique<FolderTreeNode>();
    auto info = getFolderInfo(path);
    if (info) {
        rootNode->info = *info;
    }
    rootNode->depth = 0;

    traverseFolderTree(node.get(), rootNode.get(), 0, maxDepth);

    return rootNode;
}

// Search for folders
std::vector<std::string> FolderManager::searchFolders(const std::string& pattern,
                                                     const std::string& basePath,
                                                     bool useRegex) {
    std::vector<std::string> results;
    std::vector<std::string> allFolders = listContents(basePath, true, false);

    for (const auto& folderPath : allFolders) {
        if (matchesPattern(folderPath, pattern, useRegex)) {
            results.push_back(folderPath);
        }
    }

    return results;
}

// Calculate folder size
long long FolderManager::calculateFolderSize(const std::string& path, bool includeSubfolders) {
    long long totalSize = 0;

    std::unique_ptr<mega::MegaNode> node(getNodeByPath(path));
    if (!node || !node->isFolder()) {
        return 0;
    }

    std::queue<std::unique_ptr<mega::MegaNode>> queue;
    queue.push(std::move(node));

    while (!queue.empty()) {
        auto current = std::move(queue.front());
        queue.pop();

        std::unique_ptr<mega::MegaNodeList> children(
            m_megaApi->getChildren(current.get())
        );

        if (children) {
            for (int i = 0; i < children->size(); i++) {
                mega::MegaNode* child = children->get(i);

                if (child->isFile()) {
                    totalSize += child->getSize();
                } else if (child->isFolder() && includeSubfolders) {
                    queue.push(std::unique_ptr<mega::MegaNode>(child->copy()));
                }
            }
        }
    }

    return totalSize;
}

// Count items in folder
std::pair<int, int> FolderManager::countItems(const std::string& path, bool recursive) {
    int fileCount = 0;
    int folderCount = 0;

    std::unique_ptr<mega::MegaNode> node(getNodeByPath(path));
    if (!node || !node->isFolder()) {
        return {0, 0};
    }

    std::queue<std::unique_ptr<mega::MegaNode>> queue;
    queue.push(std::move(node));

    while (!queue.empty()) {
        auto current = std::move(queue.front());
        queue.pop();

        std::unique_ptr<mega::MegaNodeList> children(
            m_megaApi->getChildren(current.get())
        );

        if (children) {
            for (int i = 0; i < children->size(); i++) {
                mega::MegaNode* child = children->get(i);

                if (child->isFile()) {
                    fileCount++;
                } else if (child->isFolder()) {
                    folderCount++;
                    if (recursive) {
                        queue.push(std::unique_ptr<mega::MegaNode>(child->copy()));
                    }
                }
            }
        }
    }

    return {fileCount, folderCount};
}

// Check if folder exists
bool FolderManager::folderExists(const std::string& path) {
    std::unique_ptr<mega::MegaNode> node(getNodeByPath(path));
    return node && node->isFolder();
}

// Get or create folder (ensures folder exists)
mega::MegaNode* FolderManager::ensureFolderExists(const std::string& path) {
    std::unique_ptr<mega::MegaNode> node(getNodeByPath(path));
    if (node && node->isFolder()) {
        return node.release();
    }

    // Folder doesn't exist, create it
    FolderOperationResult result = createFolder(path, true);
    if (result.success) {
        return getNodeByPath(path);
    }

    return nullptr;
}

// Share folder with user
FolderOperationResult FolderManager::shareFolder(const std::string& path,
                                                const std::string& email,
                                                bool readOnly) {
    FolderOperationResult result;
    result.folderPath = path;
    result.operationType = "share";

    std::unique_ptr<mega::MegaNode> node(getNodeByPath(path));
    if (!node) {
        result.success = false;
        result.errorMessage = "Folder not found: " + path;
        result.errorCode = mega::MegaError::API_ENOENT;
        return result;
    }

    if (!node->isFolder()) {
        result.success = false;
        result.errorMessage = "Path is not a folder: " + path;
        result.errorCode = mega::MegaError::API_EARGS;
        return result;
    }

    int accessLevel = readOnly ? mega::MegaShare::ACCESS_READ : mega::MegaShare::ACCESS_READWRITE;

    m_listener->reset();
    m_megaApi->share(node.get(), email.c_str(), accessLevel, m_listener.get());

    if (!m_listener->waitForCompletion()) {
        result.success = false;
        result.errorMessage = "Timeout sharing folder";
        result.errorCode = mega::MegaError::API_EFAILED;
        return result;
    }

    result.success = m_listener->isSuccess();
    result.errorMessage = result.success ? "Folder shared successfully" : m_listener->getErrorString();
    result.errorCode = m_listener->getErrorCode();

    return result;
}

// Remove folder share
FolderOperationResult FolderManager::unshareFolder(const std::string& path,
                                                  const std::string& email) {
    FolderOperationResult result;
    result.folderPath = path;
    result.operationType = "unshare";

    std::unique_ptr<mega::MegaNode> node(getNodeByPath(path));
    if (!node) {
        result.success = false;
        result.errorMessage = "Folder not found: " + path;
        result.errorCode = mega::MegaError::API_ENOENT;
        return result;
    }

    m_listener->reset();
    // Remove share for specific user
    m_megaApi->share(node.get(), email.c_str(), mega::MegaShare::ACCESS_UNKNOWN, m_listener.get());

    if (!m_listener->waitForCompletion()) {
        result.success = false;
        result.errorMessage = "Timeout removing share";
        result.errorCode = mega::MegaError::API_EFAILED;
        return result;
    }

    result.success = m_listener->isSuccess();
    result.errorMessage = result.success ? "Share removed successfully" : m_listener->getErrorString();
    result.errorCode = m_listener->getErrorCode();

    return result;
}

// Get folder shares
std::map<std::string, std::string> FolderManager::getFolderShares(const std::string& path) {
    std::map<std::string, std::string> shares;

    std::unique_ptr<mega::MegaNode> node(getNodeByPath(path));
    if (!node || !node->isFolder()) {
        return shares;
    }

    std::unique_ptr<mega::MegaShareList> shareList(
        m_megaApi->getOutShares(node.get())
    );

    if (shareList) {
        for (int i = 0; i < shareList->size(); i++) {
            mega::MegaShare* share = shareList->get(i);
            std::string email = share->getUser() ? share->getUser() : "";
            std::string access;

            switch (share->getAccess()) {
                case mega::MegaShare::ACCESS_READ:
                    access = "read";
                    break;
                case mega::MegaShare::ACCESS_READWRITE:
                    access = "read-write";
                    break;
                case mega::MegaShare::ACCESS_FULL:
                    access = "full";
                    break;
                default:
                    access = "unknown";
                    break;
            }

            shares[email] = access;
        }
    }

    return shares;
}

// Create public link for folder
std::string FolderManager::createPublicLink(const std::string& path,
                                           std::optional<std::chrono::system_clock::time_point> expireTime) {
    std::unique_ptr<mega::MegaNode> node(getNodeByPath(path));
    if (!node || !node->isFolder()) {
        return "";
    }

    m_listener->reset();

    if (expireTime) {
        int64_t seconds = std::chrono::duration_cast<std::chrono::seconds>(
            expireTime->time_since_epoch()
        ).count();
        // Export node with expiry: writable=false, megaHosted=true
        m_megaApi->exportNode(node.get(), seconds, false, true, m_listener.get());
    } else {
        // Export node without expiry: writable=false, megaHosted=true
        m_megaApi->exportNode(node.get(), 0, false, true, m_listener.get());
    }

    if (!m_listener->waitForCompletion()) {
        return "";
    }

    if (!m_listener->isSuccess()) {
        return "";
    }

    return m_listener->getPublicLink();
}

// Remove public link
bool FolderManager::removePublicLink(const std::string& path) {
    std::unique_ptr<mega::MegaNode> node(getNodeByPath(path));
    if (!node || !node->isFolder()) {
        return false;
    }

    m_listener->reset();
    m_megaApi->disableExport(node.get(), m_listener.get());

    if (!m_listener->waitForCompletion()) {
        return false;
    }

    return m_listener->isSuccess();
}

// Empty trash
FolderOperationResult FolderManager::emptyTrash() {
    FolderOperationResult result;
    result.folderPath = "/trash";
    result.operationType = "empty_trash";

    m_listener->reset();
    m_megaApi->cleanRubbishBin(m_listener.get());

    if (!m_listener->waitForCompletion(60)) {  // Longer timeout for trash
        result.success = false;
        result.errorMessage = "Timeout emptying trash";
        result.errorCode = mega::MegaError::API_EFAILED;
        return result;
    }

    result.success = m_listener->isSuccess();
    result.errorMessage = result.success ? "Trash emptied successfully" : m_listener->getErrorString();
    result.errorCode = m_listener->getErrorCode();

    return result;
}

// Restore from trash
FolderOperationResult FolderManager::restoreFromTrash(const std::string& path,
                                                     const std::string& restorePath) {
    FolderOperationResult result;
    result.folderPath = path;
    result.operationType = "restore_from_trash";

    // Get trash node
    std::unique_ptr<mega::MegaNode> rubbishNode(m_megaApi->getRubbishNode());
    if (!rubbishNode) {
        result.success = false;
        result.errorMessage = "Cannot access trash";
        result.errorCode = mega::MegaError::API_EACCESS;
        return result;
    }

    // Find node in trash
    std::unique_ptr<mega::MegaNode> node;
    std::string searchName = path.substr(path.find_last_of('/') + 1);

    std::unique_ptr<mega::MegaNodeList> trashContents(
        m_megaApi->getChildren(rubbishNode.get())
    );

    if (trashContents) {
        for (int i = 0; i < trashContents->size(); i++) {
            mega::MegaNode* child = trashContents->get(i);
            if (child->getName() && searchName == child->getName()) {
                node.reset(child->copy());
                break;
            }
        }
    }

    if (!node) {
        result.success = false;
        result.errorMessage = "Item not found in trash: " + path;
        result.errorCode = mega::MegaError::API_ENOENT;
        return result;
    }

    // Determine restore location
    mega::MegaNode* targetNode = nullptr;
    if (!restorePath.empty()) {
        targetNode = ensureFolderExists(restorePath);
    } else {
        targetNode = m_megaApi->getRootNode();
    }

    if (!targetNode) {
        result.success = false;
        result.errorMessage = "Cannot access restore location";
        result.errorCode = mega::MegaError::API_EACCESS;
        return result;
    }

    // Restore the node
    m_listener->reset();
    m_megaApi->moveNode(node.get(), targetNode, m_listener.get());

    if (targetNode != m_megaApi->getRootNode()) {
        delete targetNode;
    }

    if (!m_listener->waitForCompletion()) {
        result.success = false;
        result.errorMessage = "Timeout restoring from trash";
        result.errorCode = mega::MegaError::API_EFAILED;
        return result;
    }

    result.success = m_listener->isSuccess();
    result.errorMessage = result.success ? "Restored from trash successfully" : m_listener->getErrorString();
    result.errorCode = m_listener->getErrorCode();

    return result;
}

// Export folder structure to JSON
bool FolderManager::exportFolderStructure(const std::string& path, const std::string& outputFile) {
    auto tree = getFolderTree(path, -1);
    if (!tree) {
        return false;
    }

    nlohmann::json root;
    root["name"] = tree->info.name;
    root["path"] = tree->info.path;
    root["size"] = static_cast<double>(tree->info.size);
    root["fileCount"] = static_cast<double>(tree->info.fileCount);
    root["folderCount"] = static_cast<double>(tree->info.folderCount);

    nlohmann::json children = nlohmann::json::array();
    std::function<void(const FolderTreeNode*, nlohmann::json&)> exportNode;
    exportNode = [&](const FolderTreeNode* node, nlohmann::json& arr) {
        for (const auto& child : node->children) {
            nlohmann::json childObj;
            childObj["name"] = child->info.name;
            childObj["path"] = child->info.path;
            childObj["size"] = static_cast<double>(child->info.size);

            if (!child->children.empty()) {
                nlohmann::json subChildren = nlohmann::json::array();
                exportNode(child.get(), subChildren);
                childObj["children"] = subChildren;
            }

            arr.push_back(childObj);
        }
    };

    exportNode(tree.get(), children);
    root["children"] = children;

    try {
        std::ofstream file(outputFile);
        file << root.dump(2);  // Pretty print with 2 space indentation
        file.close();
        return true;
    } catch (...) {
        return false;
    }
}

// Import folder structure from JSON
FolderOperationResult FolderManager::importFolderStructure(const std::string& inputFile,
                                                          const std::string& basePath) {
    FolderOperationResult result;
    result.folderPath = basePath;
    result.operationType = "import_structure";

    try {
        std::ifstream file(inputFile);
        if (!file.is_open()) {
            result.success = false;
            result.errorMessage = "Cannot open input file";
            result.errorCode = -1;
            return result;
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
        file.close();

        auto json = nlohmann::json::parse(content);
        if (!json.is_object()) {
            result.success = false;
            result.errorMessage = "Invalid JSON structure";
            result.errorCode = -1;
            return result;
        }

        std::function<bool(const nlohmann::json&, const std::string&)> createStructure;
        createStructure = [&](const nlohmann::json& node, const std::string& parentPath) -> bool {
            if (node.is_object() && node.contains("children") && node["children"].is_array()) {
                const nlohmann::json& children = node["children"];
                size_t childCount = children.size();

                for (size_t i = 0; i < childCount; i++) {
                    const nlohmann::json& child = children[i];
                    if (child.is_object() && child.contains("name")) {
                        std::string childPath = parentPath + "/" + child["name"].get<std::string>();

                        auto createResult = createFolder(childPath, true);
                        if (!createResult.success) {
                            return false;
                        }

                        if (child.contains("children")) {
                            if (!createStructure(child, childPath)) {
                                return false;
                            }
                        }
                    }
                }
            }
            return true;
        };

        // Create base folder
        auto baseResult = createFolder(basePath, true);
        if (!baseResult.success) {
            return baseResult;
        }

        // Create structure
        if (createStructure(json, basePath)) {
            result.success = true;
            result.errorMessage = "Structure imported successfully";
            result.errorCode = mega::MegaError::API_OK;
        } else {
            result.success = false;
            result.errorMessage = "Failed to create complete structure";
            result.errorCode = mega::MegaError::API_EFAILED;
        }

    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = std::string("Error: ") + e.what();
        result.errorCode = -1;
    }

    return result;
}

// Set progress callback
void FolderManager::setProgressCallback(std::function<void(const std::string&, int, int)> callback) {
    m_progressCallback = callback;
}

// Add folder template
void FolderManager::addFolderTemplate(const std::string& name, const std::vector<std::string>& structure) {
    m_folderTemplates[name] = structure;
}

// Get available folder templates
std::map<std::string, std::string> FolderManager::getAvailableTemplates() const {
    std::map<std::string, std::string> templates;

    for (const auto& [name, structure] : m_folderTemplates) {
        std::stringstream desc;
        desc << "Template with " << structure.size() << " folders";
        templates[name] = desc.str();
    }

    return templates;
}

// Helper: Get node by path
mega::MegaNode* FolderManager::getNodeByPath(const std::string& path) {
    if (!m_megaApi) return nullptr;

    if (path == "/" || path.empty()) {
        return m_megaApi->getRootNode();
    }

    std::vector<std::string> pathComponents = splitPath(path);
    if (pathComponents.empty()) return nullptr;

    mega::MegaNode* currentNode = m_megaApi->getRootNode();
    if (!currentNode) return nullptr;

    for (const auto& component : pathComponents) {
        mega::MegaNode* childNode = m_megaApi->getChildNode(currentNode, component.c_str());

        if (currentNode != m_megaApi->getRootNode()) {
            delete currentNode;
        }

        if (!childNode) {
            return nullptr;
        }

        currentNode = childNode;
    }

    return currentNode;
}

// Helper: Get node path
std::string FolderManager::getNodePath(mega::MegaNode* node) {
    if (!node || !m_megaApi) return "";

    std::string path;
    mega::MegaNode* current = node;

    while (current && current->getType() != mega::MegaNode::TYPE_ROOT) {
        std::string name = current->getName() ? current->getName() : "";
        path = "/" + name + path;

        mega::MegaNode* parent = m_megaApi->getParentNode(current);
        if (current != node) {
            delete current;
        }
        current = parent;
    }

    if (current && current != node) {
        delete current;
    }

    return path.empty() ? "/" : path;
}

// Helper: Split path into components
std::vector<std::string> FolderManager::splitPath(const std::string& path) {
    std::vector<std::string> components;

    if (path.empty() || path == "/") {
        return components;
    }

    std::stringstream ss(path);
    std::string component;

    while (std::getline(ss, component, '/')) {
        if (!component.empty()) {
            components.push_back(component);
        }
    }

    return components;
}

// Helper: Join path components
std::string FolderManager::joinPath(const std::vector<std::string>& parts) {
    if (parts.empty()) return "/";

    std::string path;
    for (const auto& part : parts) {
        path += "/" + part;
    }

    return path;
}

// Helper: Initialize templates
void FolderManager::initializeTemplates() {
    // Project template
    m_folderTemplates["project"] = {
        "src",
        "src/core",
        "src/utils",
        "include",
        "tests",
        "docs",
        "build",
        "resources"
    };

    // Photo album template
    m_folderTemplates["photo_album"] = {
        "originals",
        "edited",
        "thumbnails",
        "raw",
        "exports"
    };

    // Documents template
    m_folderTemplates["documents"] = {
        "invoices",
        "contracts",
        "reports",
        "presentations",
        "spreadsheets"
    };

    // Media project template
    m_folderTemplates["media_project"] = {
        "video",
        "audio",
        "images",
        "graphics",
        "exports",
        "project_files"
    };

    // Website template
    m_folderTemplates["website"] = {
        "html",
        "css",
        "js",
        "images",
        "fonts",
        "downloads"
    };
}

// Helper: Traverse folder tree
void FolderManager::traverseFolderTree(mega::MegaNode* node, FolderTreeNode* treeNode,
                                      int currentDepth, int maxDepth) {
    if (!node || !treeNode) return;
    if (maxDepth >= 0 && currentDepth >= maxDepth) return;

    std::unique_ptr<mega::MegaNodeList> children(m_megaApi->getChildren(node));
    if (!children) return;

    for (int i = 0; i < children->size(); i++) {
        mega::MegaNode* child = children->get(i);

        if (child->isFolder()) {
            auto childTreeNode = std::make_unique<FolderTreeNode>();
            childTreeNode->depth = currentDepth + 1;

            // Get folder info
            std::string childPath = getNodePath(child);
            auto info = getFolderInfo(childPath);
            if (info) {
                childTreeNode->info = *info;
            }

            // Recursively traverse
            traverseFolderTree(child, childTreeNode.get(), currentDepth + 1, maxDepth);

            treeNode->children.push_back(std::move(childTreeNode));
        } else if (child->isFile()) {
            treeNode->files.push_back(child->getName() ? child->getName() : "");
        }
    }
}

// Helper: Match pattern
bool FolderManager::matchesPattern(const std::string& path, const std::string& pattern, bool useRegex) {
    if (useRegex) {
        try {
            std::regex re(pattern);
            return std::regex_search(path, re);
        } catch (...) {
            return false;
        }
    } else {
        // Simple glob matching (basic implementation)
        std::string regexPattern = pattern;

        // Escape special regex characters
        for (char c : {'.', '+', '(', ')', '[', ']', '{', '}', '^', '$', '|', '\\'}) {
            size_t pos = 0;
            std::string search(1, c);
            std::string replace = "\\" + search;
            while ((pos = regexPattern.find(search, pos)) != std::string::npos) {
                regexPattern.replace(pos, 1, replace);
                pos += 2;
            }
        }

        // Convert glob to regex
        size_t pos = 0;
        while ((pos = regexPattern.find("*", pos)) != std::string::npos) {
            regexPattern.replace(pos, 1, ".*");
            pos += 2;
        }

        pos = 0;
        while ((pos = regexPattern.find("?", pos)) != std::string::npos) {
            regexPattern.replace(pos, 1, ".");
            pos += 1;
        }

        try {
            std::regex re(regexPattern);
            return std::regex_match(path, re);
        } catch (...) {
            return false;
        }
    }
}

} // namespace MegaCustom