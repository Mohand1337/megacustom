#include "features/FolderMapper.h"
#include "megaapi.h"
#include "json_simple.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <set>
#include <map>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace MegaCustom {

// ============================================================================
// Constructor / Destructor
// ============================================================================

FolderMapper::FolderMapper(mega::MegaApi* megaApi)
    : m_megaApi(megaApi)
    , m_configPath(getDefaultConfigPath())
{
    loadMappings();
}

FolderMapper::~FolderMapper() {
    saveMappings();
}

// ============================================================================
// Static Utility Functions
// ============================================================================

std::string FolderMapper::getDefaultConfigPath() {
    const char* home = getenv("HOME");
    if (home) {
        fs::path configDir = fs::path(home) / ".megacustom";
        if (!fs::exists(configDir)) {
            fs::create_directories(configDir);
        }
        return (configDir / "mappings.json").string();
    }
    return "mappings.json";
}

std::string FolderMapper::formatSize(long long bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unitIndex = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024 && unitIndex < 4) {
        size /= 1024;
        unitIndex++;
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(unitIndex == 0 ? 0 : 1) << size << " " << units[unitIndex];
    return oss.str();
}

std::string FolderMapper::formatDuration(int seconds) {
    if (seconds < 60) {
        return std::to_string(seconds) + "s";
    } else if (seconds < 3600) {
        int mins = seconds / 60;
        int secs = seconds % 60;
        return std::to_string(mins) + "m " + std::to_string(secs) + "s";
    } else {
        int hours = seconds / 3600;
        int mins = (seconds % 3600) / 60;
        return std::to_string(hours) + "h " + std::to_string(mins) + "m";
    }
}

// ============================================================================
// Configuration Management
// ============================================================================

bool FolderMapper::loadMappings(const std::string& configPath) {
    std::string path = configPath.empty() ? m_configPath : configPath;
    m_configPath = path;

    if (!fs::exists(path)) {
        // No config file yet, start with empty mappings
        return true;
    }

    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            std::cerr << "Error: Cannot open config file: " << path << std::endl;
            return false;
        }

        // Read file content and parse manually (simple JSON doesn't have full parsing)
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

        // Simple manual parsing - find each mapping object in the array
        m_mappings.clear();

        // Find the mappings array
        size_t arrayStart = content.find("[");
        if (arrayStart == std::string::npos) {
            return true; // No mappings array
        }

        // Find each object in the array (delimited by { })
        size_t objStart = arrayStart;
        while ((objStart = content.find("{", objStart + 1)) != std::string::npos) {
            size_t objEnd = content.find("}", objStart);
            if (objEnd == std::string::npos) break;

            std::string objStr = content.substr(objStart, objEnd - objStart + 1);
            FolderMapping mapping;

            // Helper lambda to extract string value
            auto extractString = [&objStr](const std::string& key) -> std::string {
                std::string searchKey = "\"" + key + "\":";
                size_t keyPos = objStr.find(searchKey);
                if (keyPos == std::string::npos) return "";

                size_t valueStart = objStr.find("\"", keyPos + searchKey.length());
                if (valueStart == std::string::npos) return "";
                valueStart++; // Skip opening quote

                size_t valueEnd = objStr.find("\"", valueStart);
                if (valueEnd == std::string::npos) return "";

                return objStr.substr(valueStart, valueEnd - valueStart);
            };

            // Helper lambda to extract bool value
            auto extractBool = [&objStr](const std::string& key, bool defaultVal) -> bool {
                std::string searchKey = "\"" + key + "\":";
                size_t keyPos = objStr.find(searchKey);
                if (keyPos == std::string::npos) return defaultVal;

                size_t valueStart = keyPos + searchKey.length();
                while (valueStart < objStr.size() && objStr[valueStart] == ' ') valueStart++;

                if (objStr.substr(valueStart, 4) == "true") return true;
                if (objStr.substr(valueStart, 5) == "false") return false;
                return defaultVal;
            };

            mapping.name = extractString("name");
            mapping.localPath = extractString("localPath");
            mapping.remotePath = extractString("remotePath");
            mapping.description = extractString("description");
            mapping.enabled = extractBool("enabled", true);

            if (!mapping.name.empty() && !mapping.localPath.empty() && !mapping.remotePath.empty()) {
                m_mappings.push_back(mapping);
            }

            objStart = objEnd;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading config: " << e.what() << std::endl;
        return false;
    }
}

bool FolderMapper::saveMappings(const std::string& configPath) {
    std::string path = configPath.empty() ? m_configPath : configPath;

    try {
        // Ensure directory exists
        fs::path configDir = fs::path(path).parent_path();
        if (!configDir.empty() && !fs::exists(configDir)) {
            fs::create_directories(configDir);
        }

        json config;
        json mappingsArray = json::array();

        for (const auto& m : m_mappings) {
            json mapping;
            mapping["name"] = m.name;
            mapping["localPath"] = m.localPath;
            mapping["remotePath"] = m.remotePath;
            mapping["enabled"] = m.enabled;
            mapping["description"] = m.description;
            mapping["lastFileCount"] = m.lastFileCount;
            mapping["lastByteCount"] = static_cast<int>(m.lastByteCount / 1024); // Store KB

            mappingsArray.push_back(mapping);
        }

        config["mappings"] = mappingsArray;
        config["version"] = "1.0";

        std::ofstream file(path);
        if (!file.is_open()) {
            std::cerr << "Error: Cannot write config file: " << path << std::endl;
            return false;
        }

        file << config.dump(2) << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error saving config: " << e.what() << std::endl;
        return false;
    }
}

bool FolderMapper::addMapping(const std::string& name,
                              const std::string& localPath,
                              const std::string& remotePath,
                              const std::string& description) {
    // Check for duplicate name
    for (const auto& m : m_mappings) {
        if (m.name == name) {
            std::cerr << "Error: Mapping '" << name << "' already exists" << std::endl;
            return false;
        }
    }

    // Normalize paths
    std::string normalLocal = localPath;
    std::string normalRemote = remotePath;

    // Ensure remote path starts with /
    if (!normalRemote.empty() && normalRemote[0] != '/') {
        normalRemote = "/" + normalRemote;
    }

    // Remove trailing slashes
    while (!normalLocal.empty() && normalLocal.back() == '/') {
        normalLocal.pop_back();
    }
    while (normalRemote.size() > 1 && normalRemote.back() == '/') {
        normalRemote.pop_back();
    }

    FolderMapping mapping;
    mapping.name = name;
    mapping.localPath = normalLocal;
    mapping.remotePath = normalRemote;
    mapping.description = description;
    mapping.enabled = true;

    m_mappings.push_back(mapping);
    saveMappings();

    return true;
}

bool FolderMapper::removeMapping(const std::string& nameOrIndex) {
    FolderMapping* mapping = findMapping(nameOrIndex);
    if (!mapping) {
        std::cerr << "Error: Mapping not found: " << nameOrIndex << std::endl;
        return false;
    }

    std::string name = mapping->name;
    m_mappings.erase(
        std::remove_if(m_mappings.begin(), m_mappings.end(),
                       [&name](const FolderMapping& m) { return m.name == name; }),
        m_mappings.end());

    saveMappings();
    return true;
}

bool FolderMapper::updateMapping(const std::string& name,
                                 const std::string& localPath,
                                 const std::string& remotePath) {
    FolderMapping* mapping = findMapping(name);
    if (!mapping) {
        std::cerr << "Error: Mapping not found: " << name << std::endl;
        return false;
    }

    if (!localPath.empty()) {
        mapping->localPath = localPath;
    }
    if (!remotePath.empty()) {
        std::string normalRemote = remotePath;
        if (!normalRemote.empty() && normalRemote[0] != '/') {
            normalRemote = "/" + normalRemote;
        }
        mapping->remotePath = normalRemote;
    }

    saveMappings();
    return true;
}

bool FolderMapper::setMappingEnabled(const std::string& nameOrIndex, bool enabled) {
    FolderMapping* mapping = findMapping(nameOrIndex);
    if (!mapping) {
        std::cerr << "Error: Mapping not found: " << nameOrIndex << std::endl;
        return false;
    }

    mapping->enabled = enabled;
    saveMappings();
    return true;
}

std::optional<FolderMapping> FolderMapper::getMapping(const std::string& nameOrIndex) {
    FolderMapping* mapping = findMapping(nameOrIndex);
    if (mapping) {
        return *mapping;
    }
    return std::nullopt;
}

std::vector<FolderMapping> FolderMapper::getAllMappings() const {
    return m_mappings;
}

size_t FolderMapper::getMappingCount() const {
    return m_mappings.size();
}

FolderMapping* FolderMapper::findMapping(const std::string& nameOrIndex) {
    // Try as index first (1-based)
    try {
        int index = std::stoi(nameOrIndex);
        if (index >= 1 && index <= static_cast<int>(m_mappings.size())) {
            return &m_mappings[index - 1];
        }
    } catch (...) {
        // Not a number, try as name
    }

    // Try as name
    for (auto& m : m_mappings) {
        if (m.name == nameOrIndex) {
            return &m;
        }
    }

    return nullptr;
}

std::vector<std::string> FolderMapper::validateMapping(const FolderMapping& mapping) {
    std::vector<std::string> errors;

    if (mapping.name.empty()) {
        errors.push_back("Mapping name is empty");
    }

    if (mapping.localPath.empty()) {
        errors.push_back("Local path is empty");
    } else if (!fs::exists(mapping.localPath)) {
        errors.push_back("Local path does not exist: " + mapping.localPath);
    } else if (!fs::is_directory(mapping.localPath)) {
        errors.push_back("Local path is not a directory: " + mapping.localPath);
    }

    if (mapping.remotePath.empty()) {
        errors.push_back("Remote path is empty");
    }

    return errors;
}

// ============================================================================
// File Collection and Comparison
// ============================================================================

std::vector<std::string> FolderMapper::collectLocalFiles(const std::string& path, bool recursive) {
    std::vector<std::string> files;

    try {
        if (recursive) {
            for (const auto& entry : fs::recursive_directory_iterator(path)) {
                if (entry.is_regular_file()) {
                    files.push_back(entry.path().string());
                }
            }
        } else {
            for (const auto& entry : fs::directory_iterator(path)) {
                if (entry.is_regular_file()) {
                    files.push_back(entry.path().string());
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error scanning directory: " << e.what() << std::endl;
    }

    return files;
}

mega::MegaNode* FolderMapper::getRemoteNode(const std::string& path) {
    if (path.empty() || path == "/") {
        return m_megaApi->getRootNode();
    }
    return m_megaApi->getNodeByPath(path.c_str());
}

mega::MegaNode* FolderMapper::ensureRemotePath(const std::string& path) {
    if (path.empty() || path == "/") {
        return m_megaApi->getRootNode();
    }

    mega::MegaNode* node = m_megaApi->getNodeByPath(path.c_str());
    if (node) {
        return node;
    }

    // Create path components
    std::vector<std::string> parts;
    std::string current;
    for (char c : path) {
        if (c == '/') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }

    // Store root node pointer once - getRootNode() returns a NEW pointer each call
    mega::MegaNode* rootNode = m_megaApi->getRootNode();
    mega::MegaNode* parent = rootNode;
    std::string builtPath;

    for (const auto& part : parts) {
        builtPath += "/" + part;
        mega::MegaNode* child = m_megaApi->getNodeByPath(builtPath.c_str());

        if (!child) {
            // Create folder
            m_megaApi->createFolder(part.c_str(), parent);

            // Wait for creation
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            child = m_megaApi->getNodeByPath(builtPath.c_str());
            if (!child) {
                std::cerr << "Failed to create remote folder: " << builtPath << std::endl;
                // Clean up root node before returning
                if (parent != rootNode) {
                    delete parent;
                }
                delete rootNode;
                return nullptr;
            }
        }

        // Only delete non-root nodes (compare against stored pointer)
        if (parent != rootNode) {
            delete parent;
        }
        parent = child;
    }

    // Clean up root node if it wasn't returned as the final result
    if (parent != rootNode) {
        delete rootNode;
    }

    return parent;
}

std::map<std::string, mega::MegaNode*> FolderMapper::buildRemoteFileMap(
    mega::MegaNode* folder, const std::string& basePath) {
    std::map<std::string, mega::MegaNode*> fileMap;

    if (!folder) return fileMap;

    std::unique_ptr<mega::MegaNodeList> children(m_megaApi->getChildren(folder));
    if (!children) return fileMap;

    for (int i = 0; i < children->size(); i++) {
        mega::MegaNode* child = children->get(i);
        std::string childPath = basePath + "/" + child->getName();

        if (child->isFile()) {
            // Store a copy of the node (the original is owned by the list)
            fileMap[childPath] = child->copy();
        } else if (child->isFolder()) {
            // Recurse into subfolders
            auto subMap = buildRemoteFileMap(child, childPath);
            fileMap.insert(subMap.begin(), subMap.end());
        }
    }

    return fileMap;
}

FileCompareResult FolderMapper::compareFile(const std::string& localPath, mega::MegaNode* remoteNode) {
    FileCompareResult result;
    result.localPath = localPath;

    try {
        auto localSize = fs::file_size(localPath);
        auto localTime = fs::last_write_time(localPath);

        // Convert to system_clock
        auto localTimePoint = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            localTime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());

        result.localSize = static_cast<long long>(localSize);
        result.localModTime = localTimePoint;

        if (!remoteNode) {
            result.existsRemote = false;
            result.needsUpload = true;
            result.reason = "new";
            return result;
        }

        result.existsRemote = true;
        result.remoteSize = remoteNode->getSize();
        result.remoteModTime = std::chrono::system_clock::from_time_t(remoteNode->getModificationTime());
        result.remotePath = remoteNode->getName();

        // Compare sizes first (fast check)
        if (result.localSize != result.remoteSize) {
            result.needsUpload = true;
            result.reason = "size_changed";
            return result;
        }

        // Compare modification times
        auto localTimeT = std::chrono::system_clock::to_time_t(result.localModTime);
        auto remoteTimeT = remoteNode->getModificationTime();

        // Allow 2 second tolerance for filesystem differences
        if (std::abs(localTimeT - remoteTimeT) > 2) {
            if (localTimeT > remoteTimeT) {
                result.needsUpload = true;
                result.reason = "modified";
            } else {
                result.needsUpload = false;
                result.reason = "skip";
            }
            return result;
        }

        // Files are identical
        result.needsUpload = false;
        result.reason = "skip";
        return result;

    } catch (const std::exception& e) {
        result.needsUpload = true;
        result.reason = "error: " + std::string(e.what());
        return result;
    }
}

std::vector<FileCompareResult> FolderMapper::compareFolders(
    const std::string& localPath,
    const std::string& remotePath,
    bool recursive) {

    std::vector<FileCompareResult> results;

    // Get remote folder and build file map
    mega::MegaNode* remoteFolder = getRemoteNode(remotePath);
    std::map<std::string, mega::MegaNode*> remoteFiles;

    if (remoteFolder) {
        remoteFiles = buildRemoteFileMap(remoteFolder, "");
    }

    // Collect local files
    auto localFiles = collectLocalFiles(localPath, recursive);

    for (const auto& localFile : localFiles) {
        // Calculate relative path
        std::string relativePath = localFile.substr(localPath.length());
        if (!relativePath.empty() && relativePath[0] == '/') {
            relativePath = relativePath.substr(1);
        }

        // Find corresponding remote file
        std::string remoteFilePath = "/" + relativePath;
        mega::MegaNode* remoteNode = nullptr;

        auto it = remoteFiles.find(remoteFilePath);
        if (it != remoteFiles.end()) {
            remoteNode = it->second;
        }

        auto compareResult = compareFile(localFile, remoteNode);
        compareResult.remotePath = remotePath + "/" + relativePath;
        results.push_back(compareResult);
    }

    // Clean up remote nodes
    for (auto& [path, node] : remoteFiles) {
        delete node;
    }

    return results;
}

bool FolderMapper::matchesExcludePattern(const std::string& path,
                                          const std::vector<std::string>& patterns) {
    std::string filename = fs::path(path).filename().string();

    for (const auto& pattern : patterns) {
        // Simple wildcard matching
        if (pattern == filename) return true;

        // *.ext pattern
        if (pattern.length() > 1 && pattern[0] == '*') {
            std::string ext = pattern.substr(1);
            if (filename.length() >= ext.length() &&
                filename.substr(filename.length() - ext.length()) == ext) {
                return true;
            }
        }

        // Check for hidden files pattern
        if (pattern == ".*" && !filename.empty() && filename[0] == '.') {
            return true;
        }
    }

    return false;
}

// ============================================================================
// Upload Operations
// ============================================================================

std::vector<FileCompareResult> FolderMapper::previewUpload(
    const std::string& nameOrIndex,
    const UploadOptions& options) {

    FolderMapping* mapping = findMapping(nameOrIndex);
    if (!mapping) {
        std::cerr << "Error: Mapping not found: " << nameOrIndex << std::endl;
        return {};
    }

    // Validate mapping
    auto errors = validateMapping(*mapping);
    if (!errors.empty()) {
        for (const auto& err : errors) {
            std::cerr << "Validation error: " << err << std::endl;
        }
        return {};
    }

    auto results = compareFolders(mapping->localPath, mapping->remotePath, options.recursive);

    // Apply filters
    std::vector<FileCompareResult> filtered;
    for (const auto& r : results) {
        // Check exclude patterns
        if (matchesExcludePattern(r.localPath, options.excludePatterns)) {
            continue;
        }

        // Check size filters
        if (options.minFileSize > 0 && r.localSize < options.minFileSize) {
            continue;
        }
        if (options.maxFileSize > 0 && r.localSize > options.maxFileSize) {
            continue;
        }

        filtered.push_back(r);
    }

    return filtered;
}

MapUploadResult FolderMapper::uploadMapping(const std::string& nameOrIndex,
                                             const UploadOptions& options) {
    MapUploadResult result;
    auto startTime = std::chrono::steady_clock::now();

    FolderMapping* mapping = findMapping(nameOrIndex);
    if (!mapping) {
        result.success = false;
        result.errors.push_back("Mapping not found: " + nameOrIndex);
        return result;
    }

    result.mappingName = mapping->name;

    // Validate mapping
    auto errors = validateMapping(*mapping);
    if (!errors.empty()) {
        result.success = false;
        result.errors = errors;
        return result;
    }

    // Get files to upload
    std::vector<FileCompareResult> filesToProcess;

    if (options.incremental) {
        filesToProcess = previewUpload(nameOrIndex, options);
    } else {
        // Upload all files
        auto allFiles = collectLocalFiles(mapping->localPath, options.recursive);
        for (const auto& file : allFiles) {
            FileCompareResult r;
            r.localPath = file;
            r.needsUpload = true;
            r.reason = "full_sync";
            r.localSize = static_cast<long long>(fs::file_size(file));

            // Calculate remote path
            std::string relativePath = file.substr(mapping->localPath.length());
            r.remotePath = mapping->remotePath + relativePath;

            filesToProcess.push_back(r);
        }
    }

    // Calculate totals
    MapUploadProgress progress;
    progress.mappingName = mapping->name;
    progress.totalFiles = 0;
    progress.totalBytes = 0;

    for (const auto& f : filesToProcess) {
        if (f.needsUpload) {
            progress.totalFiles++;
            progress.totalBytes += f.localSize;
        }
    }

    // Dry run - just report what would happen
    if (options.dryRun) {
        std::cout << "\n=== DRY RUN: " << mapping->name << " ===\n";
        std::cout << "Local:  " << mapping->localPath << "\n";
        std::cout << "Remote: " << mapping->remotePath << "\n\n";

        int newCount = 0, modifiedCount = 0, skipCount = 0;
        long long newBytes = 0, modifiedBytes = 0;

        for (const auto& f : filesToProcess) {
            if (!f.needsUpload) {
                skipCount++;
                result.skippedFiles.push_back(f.localPath);
                continue;
            }

            std::string relativePath = f.localPath.substr(mapping->localPath.length());

            if (f.reason == "new" || f.reason == "full_sync") {
                newCount++;
                newBytes += f.localSize;
                std::cout << "  [NEW]      " << relativePath << " (" << formatSize(f.localSize) << ")\n";
            } else if (f.reason == "modified" || f.reason == "size_changed") {
                modifiedCount++;
                modifiedBytes += f.localSize;
                std::cout << "  [MODIFIED] " << relativePath << " (" << formatSize(f.localSize) << ")\n";
            }
        }

        std::cout << "\nSummary:\n";
        std::cout << "  New files:      " << newCount << " (" << formatSize(newBytes) << ")\n";
        std::cout << "  Modified files: " << modifiedCount << " (" << formatSize(modifiedBytes) << ")\n";
        std::cout << "  Skipped files:  " << skipCount << "\n";
        std::cout << "  Total upload:   " << (newCount + modifiedCount) << " files ("
                  << formatSize(newBytes + modifiedBytes) << ")\n";

        result.success = true;
        result.filesSkipped = skipCount;
        return result;
    }

    // Ensure remote path exists
    mega::MegaNode* remoteBase = ensureRemotePath(mapping->remotePath);
    if (!remoteBase) {
        result.success = false;
        result.errors.push_back("Failed to access/create remote path: " + mapping->remotePath);
        return result;
    }

    // Perform actual uploads
    if (options.showProgress) {
        std::cout << "\n=== Uploading: " << mapping->name << " ===\n";
        std::cout << "Files to upload: " << progress.totalFiles << " (" << formatSize(progress.totalBytes) << ")\n\n";
    }

    auto uploadStartTime = std::chrono::steady_clock::now();

    // Cache for created remote paths to avoid duplicate creation attempts
    std::map<std::string, mega::MegaNode*> pathCache;
    std::set<std::string> failedPaths;

    for (const auto& f : filesToProcess) {
        if (!f.needsUpload) {
            result.filesSkipped++;
            result.skippedFiles.push_back(f.localPath);
            continue;
        }

        std::string relativePath = f.localPath.substr(mapping->localPath.length());
        std::string remoteFilePath = mapping->remotePath + relativePath;

        // Ensure parent folder exists (with caching to avoid duplicate attempts)
        fs::path parentPath = fs::path(remoteFilePath).parent_path();
        std::string parentPathStr = parentPath.string();
        mega::MegaNode* parentNode = nullptr;

        // Check cache first
        if (pathCache.count(parentPathStr)) {
            parentNode = pathCache[parentPathStr];
        } else if (failedPaths.count(parentPathStr)) {
            // Already tried and failed
            result.filesFailed++;
            result.errors.push_back("Parent folder unavailable: " + parentPathStr);
            continue;
        } else {
            // First time trying this path
            parentNode = ensureRemotePath(parentPathStr);
            if (parentNode) {
                pathCache[parentPathStr] = parentNode;
            } else {
                failedPaths.insert(parentPathStr);
            }
        }

        if (!parentNode) {
            result.filesFailed++;
            result.errors.push_back("Failed to create parent: " + parentPathStr);
            continue;
        }

        // Show progress
        if (options.showProgress) {
            std::cout << "  [" << (progress.uploadedFiles + 1) << "/" << progress.totalFiles << "] "
                      << relativePath << " (" << formatSize(f.localSize) << ")... ";
            std::cout.flush();
        }

        progress.currentFile = relativePath;

        // Start upload using proper API
        std::string filename = fs::path(f.localPath).filename().string();

        // Check if file already exists and delete it first
        std::unique_ptr<mega::MegaNodeList> children(m_megaApi->getChildren(parentNode));
        if (children) {
            for (int i = 0; i < children->size(); i++) {
                mega::MegaNode* child = children->get(i);
                if (child->isFile() && std::string(child->getName()) == filename) {
                    m_megaApi->remove(child);
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    break;
                }
            }
        }

        // Get modification time
        auto mtime = fs::last_write_time(f.localPath);
        auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(
            mtime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        int64_t mtimeSeconds = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::time_point(sctp.time_since_epoch()));

        // Start upload with full API signature
        m_megaApi->startUpload(f.localPath.c_str(), parentNode, filename.c_str(),
                               mtimeSeconds, nullptr, false, false, nullptr, nullptr);

        // Simple wait for upload (in production, use proper listener)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Wait for transfer to complete (poll-based)
        int waitCount = 0;
        const int maxWait = 600;  // 60 seconds max per file
        while (waitCount < maxWait) {
            std::unique_ptr<mega::MegaTransferList> transfers(m_megaApi->getTransfers(mega::MegaTransfer::TYPE_UPLOAD));
            if (!transfers || transfers->size() == 0) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            waitCount++;
        }

        if (waitCount >= maxWait) {
            if (options.showProgress) std::cout << "TIMEOUT\n";
            result.filesFailed++;
            result.errors.push_back("Upload timeout: " + relativePath);
        } else {
            if (options.showProgress) std::cout << "OK\n";
            result.filesUploaded++;
            result.uploadedFiles.push_back(relativePath);
            result.bytesUploaded += f.localSize;
        }

        progress.uploadedFiles++;
        progress.uploadedBytes += f.localSize;

        // Calculate speed and ETA
        auto elapsed = std::chrono::steady_clock::now() - uploadStartTime;
        auto elapsedSecs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        if (elapsedSecs > 0) {
            progress.speedBytesPerSec = static_cast<double>(progress.uploadedBytes) / elapsedSecs;
            long long remainingBytes = progress.totalBytes - progress.uploadedBytes;
            if (progress.speedBytesPerSec > 0) {
                progress.estimatedRemaining = std::chrono::seconds(
                    static_cast<long long>(remainingBytes / progress.speedBytesPerSec));
            }
        }

        if (m_progressCallback) {
            m_progressCallback(progress);
        }

        if (m_fileCallback) {
            m_fileCallback(relativePath, true);
        }
    }

    // Update mapping stats
    mapping->lastSync = std::chrono::system_clock::now();
    mapping->lastFileCount = result.filesUploaded;
    mapping->lastByteCount = result.bytesUploaded;
    saveMappings();

    // Calculate duration
    auto endTime = std::chrono::steady_clock::now();
    result.duration = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime);
    result.success = (result.filesFailed == 0);

    if (options.showProgress) {
        std::cout << "\n=== Upload Complete ===\n";
        std::cout << "  Uploaded: " << result.filesUploaded << " files (" << formatSize(result.bytesUploaded) << ")\n";
        std::cout << "  Skipped:  " << result.filesSkipped << " files\n";
        std::cout << "  Failed:   " << result.filesFailed << " files\n";
        std::cout << "  Duration: " << formatDuration(static_cast<int>(result.duration.count())) << "\n";
    }

    return result;
}

std::vector<MapUploadResult> FolderMapper::uploadMappings(
    const std::vector<std::string>& namesOrIndices,
    const UploadOptions& options) {

    std::vector<MapUploadResult> results;

    for (const auto& nameOrIndex : namesOrIndices) {
        auto result = uploadMapping(nameOrIndex, options);
        results.push_back(result);
    }

    return results;
}

std::vector<MapUploadResult> FolderMapper::uploadAll(const UploadOptions& options) {
    std::vector<MapUploadResult> results;

    for (const auto& mapping : m_mappings) {
        if (mapping.enabled) {
            auto result = uploadMapping(mapping.name, options);
            results.push_back(result);
        }
    }

    return results;
}

// ============================================================================
// Callbacks
// ============================================================================

void FolderMapper::setProgressCallback(std::function<void(const MapUploadProgress&)> callback) {
    m_progressCallback = callback;
}

void FolderMapper::setFileCallback(std::function<void(const std::string&, bool)> callback) {
    m_fileCallback = callback;
}

} // namespace MegaCustom
