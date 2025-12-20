/**
 * Mega Custom SDK Application
 * Main entry point
 */

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <cstdlib>
#include <iomanip>
#include <fstream>
#include <regex>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>
#include <cstring>

// Core includes
#include "core/MegaManager.h"
#include "core/ConfigManager.h"
#include "core/AuthenticationModule.h"
#include "operations/FileOperations.h"
#include "operations/FolderManager.h"
#include "features/MultiUploader.h"
#include "features/SmartSync.h"
#include "features/FolderMapper.h"
#include "features/Watermarker.h"
#include "features/DistributionPipeline.h"
#include "integrations/MemberDatabase.h"
#include "integrations/WordPressSync.h"
#include "core/LogManager.h"
#include "megaapi.h"  // Need full SDK header for MegaNode
// #include "ui/CLIParser.h"
#include <mutex>
#include <condition_variable>

// Synchronous rename listener for CLI operations
class SyncRenameListener : public mega::MegaRequestListener {
private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_finished = false;
    int m_errorCode = mega::MegaError::API_OK;
    std::string m_errorString;

public:
    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_finished = false;
        m_errorCode = mega::MegaError::API_OK;
        m_errorString.clear();
    }

    void wait() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return m_finished; });
    }

    int getErrorCode() const { return m_errorCode; }
    std::string getErrorString() const { return m_errorString; }

    void onRequestFinish(mega::MegaApi* api, mega::MegaRequest* request, mega::MegaError* error) override {
        (void)api;
        (void)request;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_finished = true;
        if (error) {
            m_errorCode = error->getErrorCode();
            m_errorString = error->getErrorString() ? error->getErrorString() : "";
        }
        m_cv.notify_all();
    }
};

// Version information
#define APP_NAME "MegaCustom"
#define APP_VERSION "1.0.0"
#define APP_DESCRIPTION "Advanced Mega.nz SDK Application"

/**
 * Get the session file path in the user's home directory
 */
std::string getSessionFilePath() {
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : "/tmp";
    }

    std::string configDir = std::string(home) + "/.megacustom";

    // Create directory if it doesn't exist
    mkdir(configDir.c_str(), 0700);

    return configDir + "/session.dat";
}

/**
 * Get encryption key for session (derived from machine ID + user)
 */
std::string getSessionEncryptionKey() {
    // In production, use a more secure method
    // For now, use a combination of hostname and username
    char hostname[256] = {0};
    gethostname(hostname, sizeof(hostname));

    const char* username = getenv("USER");
    if (!username) {
        username = "default";
    }

    // Create a key from hostname + username
    std::string key = std::string(hostname) + "_" + username + "_megacustom_key";

    // Ensure key is at least 32 characters
    while (key.length() < 32) {
        key += "0";
    }

    return key.substr(0, 32);
}

/**
 * Try to restore session from file
 */
bool tryRestoreSession(MegaCustom::MegaManager& manager) {
    std::string sessionFile = getSessionFilePath();
    std::string encryptionKey = getSessionEncryptionKey();

    // Check if session file exists
    struct stat buffer;
    if (stat(sessionFile.c_str(), &buffer) != 0) {
        return false; // No session file
    }

    // Try to load and restore session
    MegaCustom::AuthenticationModule auth(manager.getMegaApi());
    std::string sessionKey = auth.loadSession(sessionFile, encryptionKey);

    if (!sessionKey.empty()) {
        auto result = auth.loginWithSession(sessionKey);
        if (result.success) {
            // Verify that the SDK is actually logged in
            if (manager.getMegaApi()->isLoggedIn() > 0) {
                std::cout << "Session restored successfully.\n";
                return true;
            } else {
                std::cerr << "Session restore reported success but SDK not logged in.\n";
            }
        }
    }

    // If session restore failed, delete the invalid session file
    remove(sessionFile.c_str());
    return false;
}

/**
 * Save current session to file
 */
bool saveCurrentSession(MegaCustom::AuthenticationModule& auth) {
    std::string sessionFile = getSessionFilePath();
    std::string encryptionKey = getSessionEncryptionKey();

    if (auth.saveSession(sessionFile, encryptionKey)) {
        // Set restrictive permissions on session file
        chmod(sessionFile.c_str(), 0600);
        return true;
    }

    return false;
}

/**
 * Initialize MegaManager and try to restore session
 */
bool initializeManager(MegaCustom::MegaManager& manager) {
    if (manager.isInitialized()) {
        return true; // Already initialized
    }

    // Try to get API key from environment variable first
    const char* envApiKey = std::getenv("MEGA_API_KEY");
    std::string apiKey = envApiKey ? envApiKey : "YOUR_MEGA_API_KEY";

    if (!manager.initialize(apiKey)) {
        std::cerr << "Failed to initialize MegaManager\n";
        std::cerr << "Please set your Mega API key:\n";
        std::cerr << "  1. Set environment variable: export MEGA_API_KEY=your_key\n";
        std::cerr << "  2. Or replace YOUR_MEGA_API_KEY in the code\n";
        std::cerr << "\nYou can get your API key from: https://mega.nz/sdk\n";
        return false;
    }

    // Try to restore session after initialization
    tryRestoreSession(manager);

    return true;
}

/**
 * Print application header
 */
void printHeader() {
    std::cout << "\n";
    std::cout << "=================================================\n";
    std::cout << " " << APP_NAME << " v" << APP_VERSION << "\n";
    std::cout << " " << APP_DESCRIPTION << "\n";
    std::cout << "=================================================\n\n";
}

/**
 * Print usage information
 */
void printUsage(const std::string& programName) {
    std::cout << "Usage: " << programName << " <command> [options]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  auth        Authentication operations\n";
    std::cout << "  upload      Upload files/folders\n";
    std::cout << "  download    Download files/folders\n";
    std::cout << "  multiupload Multi-destination bulk uploads\n";
    std::cout << "  sync        Synchronize folders\n";
    std::cout << "  map         Folder mapping for easy VPS-to-MEGA uploads\n";
    std::cout << "  rename      Bulk rename operations\n";
    std::cout << "  folder      Folder management\n";
    std::cout << "  member      Member management for distribution\n";
    std::cout << "  watermark   Video/PDF watermarking\n";
    std::cout << "  distribute  Watermark and distribute files to members\n";
    std::cout << "  wp          WordPress member sync\n";
    std::cout << "  log         View activity logs and distribution history\n";
    std::cout << "  config      Configuration management\n";
    std::cout << "  help        Show this help message\n";
    std::cout << "  version     Show version information\n\n";
    std::cout << "Use '" << programName << " <command> --help' for command-specific help.\n";
}

/**
 * Print version information
 */
void printVersion() {
    std::cout << APP_NAME << " version " << APP_VERSION << "\n";
    std::cout << "Built with Mega C++ SDK\n";
    std::cout << "Copyright (c) 2024\n";
}

/**
 * Handle authentication command
 */
int handleAuth(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "--help") {
        std::cout << "Authentication Commands:\n";
        std::cout << "  login       Login to Mega account\n";
        std::cout << "  logout      Logout from current session\n";
        std::cout << "  status      Show authentication status\n";
        std::cout << "  2fa         Manage two-factor authentication\n";
        std::cout << "  session     Login with session key\n";
        return 0;
    }

    // Get MegaManager instance
    MegaCustom::MegaManager& manager = MegaCustom::MegaManager::getInstance();

    // Initialize and try to restore session
    if (!initializeManager(manager)) {
        return 1;
    }

    // Create AuthenticationModule
    MegaCustom::AuthenticationModule auth(manager.getMegaApi());

    const std::string& cmd = args[0];

    if (cmd == "login") {
        if (args.size() < 3) {
            std::cout << "Usage: megacustom auth login <email> <password>\n";
            return 1;
        }

        std::cout << "Logging in to Mega...\n";
        auto result = auth.login(args[1], args[2]);

        if (result.success) {
            std::cout << "Login successful!\n";

            // Save session for future use
            if (saveCurrentSession(auth)) {
                std::cout << "Session saved for automatic login.\n";
            }
        } else {
            std::cerr << "Login failed: " << result.errorMessage << "\n";
            if (result.requires2FA) {
                std::cout << "2FA is required. Use 'auth 2fa <pin>' to complete login.\n";
            }
            return 1;
        }
    }
    else if (cmd == "logout") {
        auth.logout(false);

        // Remove saved session
        std::string sessionFile = getSessionFilePath();
        remove(sessionFile.c_str());

        std::cout << "Logged out successfully.\n";
    }
    else if (cmd == "status") {
        if (auth.isLoggedIn()) {
            std::cout << "Status: Logged in\n";
            auto info = auth.getAccountInfo();
            if (!info.email.empty()) {
                std::cout << "Email: " << info.email << "\n";
            }
            std::cout << "Account type: " << info.accountType << "\n";
        } else {
            std::cout << "Status: Not logged in\n";
        }
    }
    else if (cmd == "session") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom auth session <session-key>\n";
            return 1;
        }

        std::cout << "Logging in with session key...\n";
        auto result = auth.loginWithSession(args[1]);

        if (result.success) {
            std::cout << "Session login successful!\n";
        } else {
            std::cerr << "Session login failed: " << result.errorMessage << "\n";
            return 1;
        }
    }
    else if (cmd == "2fa") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom auth 2fa <pin>\n";
            return 1;
        }

        auto result = auth.complete2FA(args[1]);

        if (result.success) {
            std::cout << "2FA authentication successful!\n";

            // Save session after successful 2FA
            if (saveCurrentSession(auth)) {
                std::cout << "Session saved for automatic login.\n";
            }
        } else {
            std::cerr << "2FA authentication failed: " << result.errorMessage << "\n";
            return 1;
        }
    }
    else {
        std::cerr << "Unknown auth command: " << cmd << "\n";
        return 1;
    }

    return 0;
}

/**
 * Handle upload command
 */
int handleUpload(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "--help") {
        std::cout << "Upload Commands:\n";
        std::cout << "  file <local> <remote>  Upload single file\n";
        std::cout << "  folder <local> <remote> Upload entire folder\n";
        std::cout << "  status                  Show upload statistics\n";
        return 0;
    }

    // Initialize MegaManager and restore session
    MegaCustom::MegaManager& manager = MegaCustom::MegaManager::getInstance();
    if (!initializeManager(manager)) {
        return 1;
    }

    // Check if logged in
    MegaCustom::AuthenticationModule auth(manager.getMegaApi());
    if (!auth.isLoggedIn()) {
        std::cerr << "Please login first using: megacustom auth login <email> <password>\n";
        return 1;
    }

    // Create FileOperations instance
    MegaCustom::FileOperations fileOps(manager.getMegaApi());

    const std::string& cmd = args[0];

    if (cmd == "file") {
        if (args.size() < 3) {
            std::cout << "Usage: megacustom upload file <local-file> <remote-path>\n";
            return 1;
        }

        std::cout << "Uploading " << args[1] << " to " << args[2] << "...\n";

        // Set progress callback
        fileOps.setProgressCallback([](const MegaCustom::TransferProgress& progress) {
            std::cout << "\rProgress: " << progress.progressPercentage << "%"
                     << " Speed: " << progress.speed / 1024 << " KB/s" << std::flush;
        });

        // Upload file
        auto result = fileOps.uploadFile(args[1], args[2]);

        if (result.success) {
            std::cout << "\nUpload successful!\n";
            std::cout << "File size: " << result.fileSize << " bytes\n";
            std::cout << "Duration: " << result.duration.count() << " ms\n";
        } else {
            std::cerr << "\nUpload failed: " << result.errorMessage << "\n";
            return 1;
        }
    }
    else if (cmd == "folder") {
        if (args.size() < 3) {
            std::cout << "Usage: megacustom upload folder <local-folder> <remote-folder>\n";
            return 1;
        }

        std::cout << "Uploading folder " << args[1] << " to " << args[2] << "...\n";

        // Upload directory
        auto results = fileOps.uploadDirectory(args[1], args[2], true);

        int successful = 0, failed = 0;
        for (const auto& result : results) {
            if (result.success) {
                successful++;
                std::cout << "✓ " << result.fileName << "\n";
            } else {
                failed++;
                std::cerr << "✗ " << result.fileName << ": " << result.errorMessage << "\n";
            }
        }

        std::cout << "\nUpload complete: " << successful << " successful, " << failed << " failed\n";
    }
    else if (cmd == "status") {
        std::cout << "Upload Statistics:\n";
        std::cout << fileOps.getTransferStatistics() << "\n";
    }
    else {
        std::cerr << "Unknown upload command: " << cmd << "\n";
        return 1;
    }

    return 0;
}

/**
 * Handle download command
 */
int handleDownload(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "--help") {
        std::cout << "Download Commands:\n";
        std::cout << "  file <remote> <local>  Download single file\n";
        std::cout << "  folder <remote> <local> Download entire folder\n";
        std::cout << "  check <remote>          Check if file exists\n";
        return 0;
    }

    // Initialize MegaManager and restore session
    MegaCustom::MegaManager& manager = MegaCustom::MegaManager::getInstance();
    if (!initializeManager(manager)) {
        return 1;
    }

    // Check if logged in
    MegaCustom::AuthenticationModule auth(manager.getMegaApi());
    if (!auth.isLoggedIn()) {
        std::cerr << "Please login first using: megacustom auth login <email> <password>\n";
        return 1;
    }

    // Create FileOperations instance
    MegaCustom::FileOperations fileOps(manager.getMegaApi());

    const std::string& cmd = args[0];

    if (cmd == "file") {
        if (args.size() < 3) {
            std::cout << "Usage: megacustom download file <remote-path> <local-file>\n";
            return 1;
        }

        // Get remote node
        mega::MegaNode* node = manager.getMegaApi()->getNodeByPath(args[1].c_str());
        if (!node) {
            std::cerr << "Remote file not found: " << args[1] << "\n";
            return 1;
        }

        std::cout << "Downloading " << args[1] << " to " << args[2] << "...\n";

        // Set progress callback
        fileOps.setProgressCallback([](const MegaCustom::TransferProgress& progress) {
            std::cout << "\rProgress: " << progress.progressPercentage << "%"
                     << " Speed: " << progress.speed / 1024 << " KB/s" << std::flush;
        });

        // Download file
        auto result = fileOps.downloadFile(node, args[2]);
        delete node;

        if (result.success) {
            std::cout << "\nDownload successful!\n";
            std::cout << "File size: " << result.fileSize << " bytes\n";
            std::cout << "Duration: " << result.duration.count() << " ms\n";
        } else {
            std::cerr << "\nDownload failed: " << result.errorMessage << "\n";
            return 1;
        }
    }
    else if (cmd == "folder") {
        if (args.size() < 3) {
            std::cout << "Usage: megacustom download folder <remote-folder> <local-folder>\n";
            return 1;
        }

        // Get remote node
        mega::MegaNode* node = manager.getMegaApi()->getNodeByPath(args[1].c_str());
        if (!node) {
            std::cerr << "Remote folder not found: " << args[1] << "\n";
            return 1;
        }

        if (!node->isFolder()) {
            std::cerr << args[1] << " is not a folder\n";
            delete node;
            return 1;
        }

        std::cout << "Downloading folder " << args[1] << " to " << args[2] << "...\n";

        // Download directory
        auto results = fileOps.downloadDirectory(node, args[2]);
        delete node;

        int successful = 0, failed = 0;
        for (const auto& result : results) {
            if (result.success) {
                successful++;
                std::cout << "✓ " << result.fileName << "\n";
            } else {
                failed++;
                std::cerr << "✗ " << result.fileName << ": " << result.errorMessage << "\n";
            }
        }

        std::cout << "\nDownload complete: " << successful << " successful, " << failed << " failed\n";
    }
    else if (cmd == "check") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom download check <remote-path>\n";
            return 1;
        }

        if (fileOps.remoteFileExists(args[1])) {
            std::cout << "File exists: " << args[1] << "\n";
        } else {
            std::cout << "File does not exist: " << args[1] << "\n";
        }
    }
    else {
        std::cerr << "Unknown download command: " << cmd << "\n";
        return 1;
    }

    return 0;
}

/**
 * Handle multiupload command
 */
int handleMultiUpload(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "--help") {
        std::cout << "Multi-Upload Commands:\n";
        std::cout << "  multiple <files...> <destinations...>  Upload to multiple destinations\n";
        std::cout << "  directory <dir> <destinations...>      Upload directory to multiple destinations\n";
        std::cout << "  bytype <dir> images:<dest1> videos:<dest2>  Upload by file type\n";
        std::cout << "  bysize <dir> large:<dest1> small:<dest2>    Upload by file size\n";
        std::cout << "  status                                 Show active upload tasks\n";
        std::cout << "  pause <task_id>                        Pause upload task\n";
        std::cout << "  resume <task_id>                       Resume paused task\n";
        std::cout << "  cancel <task_id>                       Cancel upload task\n";
        std::cout << "  stats                                  Show upload statistics\n";
        std::cout << "\nExamples:\n";
        std::cout << "  megacustom multiupload multiple file1.jpg file2.png /Images /Backup\n";
        std::cout << "  megacustom multiupload directory ./photos /Photos /Archive --recursive\n";
        std::cout << "  megacustom multiupload bytype ./media images:/Photos videos:/Videos\n";
        return 0;
    }

    // Initialize MegaManager and restore session
    MegaCustom::MegaManager& manager = MegaCustom::MegaManager::getInstance();
    if (!initializeManager(manager)) {
        return 1;
    }

    // Check if logged in
    MegaCustom::AuthenticationModule auth(manager.getMegaApi());
    if (!auth.isLoggedIn()) {
        std::cerr << "Please login first using: megacustom auth login <email> <password>\n";
        return 1;
    }

    // Create MultiUploader instance
    MegaCustom::MultiUploader uploader(manager.getMegaApi());

    const std::string& cmd = args[0];

    if (cmd == "multiple") {
        if (args.size() < 3) {
            std::cout << "Usage: megacustom multiupload multiple <files...> <destinations...>\n";
            return 1;
        }

        // Parse files and destinations
        std::vector<std::string> files;
        std::vector<MegaCustom::UploadDestination> destinations;

        // Find where destinations start (paths starting with /)
        size_t destStart = 1;
        for (size_t i = 1; i < args.size(); i++) {
            if (args[i][0] == '/') {
                destStart = i;
                break;
            }
            files.push_back(args[i]);
        }

        // Add destinations
        for (size_t i = destStart; i < args.size(); i++) {
            if (args[i][0] == '/') {
                MegaCustom::UploadDestination dest;
                dest.remotePath = args[i];
                dest.createIfMissing = true;
                destinations.push_back(dest);
            }
        }

        if (files.empty() || destinations.empty()) {
            std::cerr << "Error: Need at least one file and one destination\n";
            return 1;
        }

        std::cout << "Uploading " << files.size() << " files to " << destinations.size() << " destinations\n";

        // Create distribution rules (round-robin by default)
        std::vector<MegaCustom::DistributionRule> rules;
        MegaCustom::DistributionRule roundRobin;
        roundRobin.type = MegaCustom::DistributionRule::ROUND_ROBIN;
        roundRobin.destinationIndex = destinations.size();
        rules.push_back(roundRobin);

        // Start upload task
        std::string taskId = uploader.uploadToMultipleDestinations(files, destinations, rules);
        uploader.startTask(taskId, 4);

        std::cout << "Upload task started with ID: " << taskId << "\n";
        std::cout << "Use 'megacustom multiupload status' to check progress\n";
    }
    else if (cmd == "directory") {
        if (args.size() < 3) {
            std::cout << "Usage: megacustom multiupload directory <dir> <destinations...> [--recursive]\n";
            return 1;
        }

        std::string directory = args[1];
        std::vector<MegaCustom::UploadDestination> destinations;
        bool recursive = false;

        // Parse destinations and flags
        for (size_t i = 2; i < args.size(); i++) {
            if (args[i] == "--recursive") {
                recursive = true;
            } else if (args[i][0] == '/') {
                MegaCustom::UploadDestination dest;
                dest.remotePath = args[i];
                dest.createIfMissing = true;
                destinations.push_back(dest);
            }
        }

        if (destinations.empty()) {
            std::cerr << "Error: Need at least one destination\n";
            return 1;
        }

        std::cout << "Uploading directory " << directory << " to " << destinations.size() << " destinations\n";
        if (recursive) std::cout << "Including subdirectories\n";

        // Default rules
        std::vector<MegaCustom::DistributionRule> rules;

        // Start upload
        std::string taskId = uploader.uploadDirectoryToMultiple(directory, destinations, rules, recursive);
        uploader.startTask(taskId, 4);

        std::cout << "Upload task started with ID: " << taskId << "\n";
    }
    else if (cmd == "bytype") {
        if (args.size() < 3) {
            std::cout << "Usage: megacustom multiupload bytype <dir> images:<dest> videos:<dest> ...\n";
            return 1;
        }

        std::string directory = args[1];
        std::vector<MegaCustom::UploadDestination> destinations;
        std::vector<MegaCustom::DistributionRule> rules;

        // Parse type:destination pairs
        for (size_t i = 2; i < args.size(); i++) {
            size_t colonPos = args[i].find(':');
            if (colonPos != std::string::npos) {
                std::string type = args[i].substr(0, colonPos);
                std::string path = args[i].substr(colonPos + 1);

                MegaCustom::UploadDestination dest;
                dest.remotePath = path;
                dest.createIfMissing = true;

                int destIndex = destinations.size();
                destinations.push_back(dest);

                // Create rule for this type
                MegaCustom::DistributionRule rule;
                rule.type = MegaCustom::DistributionRule::BY_EXTENSION;
                rule.destinationIndex = destIndex;

                if (type == "images") {
                    rule.extensions = {".jpg", ".jpeg", ".png", ".gif", ".bmp", ".svg"};
                } else if (type == "videos") {
                    rule.extensions = {".mp4", ".avi", ".mkv", ".mov", ".wmv", ".webm"};
                } else if (type == "documents") {
                    rule.extensions = {".pdf", ".doc", ".docx", ".txt", ".odt"};
                }

                rules.push_back(rule);
            }
        }

        if (destinations.empty()) {
            std::cerr << "Error: No valid type:destination pairs found\n";
            return 1;
        }

        std::cout << "Uploading by file type from " << directory << "\n";

        std::string taskId = uploader.uploadDirectoryToMultiple(directory, destinations, rules, true);
        uploader.startTask(taskId, 4);

        std::cout << "Upload task started with ID: " << taskId << "\n";
    }
    else if (cmd == "status") {
        auto activeTasks = uploader.getActiveTasks();

        if (activeTasks.empty()) {
            std::cout << "No active upload tasks\n";
        } else {
            std::cout << "Active upload tasks:\n";
            for (const auto& taskId : activeTasks) {
                auto progress = uploader.getTaskProgress(taskId);
                if (progress.has_value()) {
                    std::cout << "\nTask: " << taskId << "\n";
                    std::cout << "  Progress: " << progress->completedFiles << "/" << progress->totalFiles << " files\n";
                    std::cout << "  Uploaded: " << (progress->uploadedBytes / (1024*1024)) << " MB\n";
                    std::cout << "  Overall: " << std::fixed << std::setprecision(1) << progress->overallProgress << "%\n";
                    std::cout << "  Current: " << progress->currentFile << "\n";
                }
            }
        }
    }
    else if (cmd == "stats") {
        std::cout << "Upload Statistics:\n";
        std::cout << uploader.getStatistics() << "\n";
    }
    else {
        std::cerr << "Unknown multiupload command: " << cmd << "\n";
        return 1;
    }

    return 0;
}

/**
 * Handle sync command
 */
int handleSync(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "--help") {
        std::cout << "Sync Commands:\n";
        std::cout << "  create <name> <local> <remote>  Create sync profile\n";
        std::cout << "  start <profile>                  Start synchronization\n";
        std::cout << "  analyze <local> <remote>         Analyze folders (dry run)\n";
        std::cout << "  stop <sync_id>                   Stop active sync\n";
        std::cout << "  pause <sync_id>                  Pause active sync\n";
        std::cout << "  resume <sync_id>                 Resume paused sync\n";
        std::cout << "  status                           Show sync status\n";
        std::cout << "  list                             List sync profiles\n";
        std::cout << "  schedule <profile> <interval>    Schedule automatic sync\n";
        std::cout << "  stats                            Show sync statistics\n";
        std::cout << "\nExamples:\n";
        std::cout << "  megacustom sync create backup /home/user/docs /Backup bidirectional\n";
        std::cout << "  megacustom sync start backup\n";
        std::cout << "  megacustom sync analyze /local/folder /remote/folder\n";
        return 0;
    }

    // Initialize MegaManager and restore session
    MegaCustom::MegaManager& manager = MegaCustom::MegaManager::getInstance();
    if (!initializeManager(manager)) {
        return 1;
    }

    // Check if logged in
    MegaCustom::AuthenticationModule auth(manager.getMegaApi());
    if (!auth.isLoggedIn()) {
        std::cerr << "Please login first using: megacustom auth login <email> <password>\n";
        return 1;
    }

    // Create SmartSync instance
    MegaCustom::SmartSync sync(manager.getMegaApi());

    const std::string& cmd = args[0];

    if (cmd == "create") {
        if (args.size() < 4) {
            std::cout << "Usage: megacustom sync create <name> <local_path> <remote_path> [direction]\n";
            std::cout << "Directions: bidirectional, upload, download, mirror_local, mirror_remote\n";
            return 1;
        }

        MegaCustom::SyncConfig config;
        config.name = args[1];
        config.localPath = args[2];
        config.remotePath = args[3];

        // Set sync direction
        if (args.size() > 4) {
            std::string direction = args[4];
            if (direction == "bidirectional") {
                config.direction = MegaCustom::SyncDirection::BIDIRECTIONAL;
            } else if (direction == "upload") {
                config.direction = MegaCustom::SyncDirection::LOCAL_TO_REMOTE;
            } else if (direction == "download") {
                config.direction = MegaCustom::SyncDirection::REMOTE_TO_LOCAL;
            } else if (direction == "mirror_local") {
                config.direction = MegaCustom::SyncDirection::MIRROR_LOCAL;
            } else if (direction == "mirror_remote") {
                config.direction = MegaCustom::SyncDirection::MIRROR_REMOTE;
            }
        } else {
            config.direction = MegaCustom::SyncDirection::BIDIRECTIONAL;
        }

        // Set default conflict resolution
        config.conflictStrategy = MegaCustom::ConflictResolution::NEWER_WINS;
        config.deleteOrphans = false;
        config.verifyTransfers = true;
        config.createBackups = true;

        std::string profileId = sync.createSyncProfile(config);
        std::cout << "Sync profile created: " << profileId << "\n";
        std::cout << "Name: " << config.name << "\n";
        std::cout << "Local: " << config.localPath << "\n";
        std::cout << "Remote: " << config.remotePath << "\n";
    }
    else if (cmd == "start") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom sync start <profile_id>\n";
            return 1;
        }

        std::string profileId = args[1];
        std::cout << "Starting sync for profile: " << profileId << "\n";

        if (sync.startSync(profileId)) {
            std::cout << "Sync started successfully\n";
            std::cout << "Use 'megacustom sync status' to check progress\n";
        } else {
            std::cerr << "Failed to start sync\n";
            return 1;
        }
    }
    else if (cmd == "analyze") {
        if (args.size() < 3) {
            std::cout << "Usage: megacustom sync analyze <local_path> <remote_path>\n";
            return 1;
        }

        MegaCustom::SyncConfig config;
        config.name = "analysis";
        config.localPath = args[1];
        config.remotePath = args[2];
        config.direction = MegaCustom::SyncDirection::BIDIRECTIONAL;
        config.conflictStrategy = MegaCustom::ConflictResolution::NEWER_WINS;

        std::cout << "Analyzing folders...\n";
        auto plan = sync.analyzeFolders(config, true);

        std::cout << "\nSync Analysis Results:\n";
        std::cout << "Files to upload: " << plan.filesToUpload.size() << "\n";
        std::cout << "Files to download: " << plan.filesToDownload.size() << "\n";
        std::cout << "Files to delete: " << plan.filesToDelete.size() << "\n";
        std::cout << "Conflicts found: " << plan.conflicts.size() << "\n";
        std::cout << "Total upload size: " << (plan.totalUploadSize / (1024*1024)) << " MB\n";
        std::cout << "Total download size: " << (plan.totalDownloadSize / (1024*1024)) << " MB\n";
        std::cout << "Estimated time: " << (plan.estimatedTimeSeconds / 60) << " minutes\n";

        if (!plan.conflicts.empty()) {
            std::cout << "\nConflicts:\n";
            for (const auto& conflict : plan.conflicts) {
                std::cout << "  " << conflict.path << ": " << conflict.description << "\n";
            }
        }
    }
    else if (cmd == "list") {
        auto profiles = sync.listSyncProfiles();

        if (profiles.empty()) {
            std::cout << "No sync profiles found\n";
        } else {
            std::cout << "Sync Profiles:\n";
            for (const auto& [id, name] : profiles) {
                std::cout << "  " << id << ": " << name << "\n";
            }
        }
    }
    else if (cmd == "status") {
        auto activeSyncs = sync.getActiveSyncs();

        if (activeSyncs.empty()) {
            std::cout << "No active syncs\n";
        } else {
            std::cout << "Active Syncs:\n";
            for (const auto& syncId : activeSyncs) {
                auto progress = sync.getSyncProgress(syncId);
                if (progress.has_value()) {
                    std::cout << "\nSync: " << syncId << "\n";
                    std::cout << "  Name: " << progress->syncName << "\n";
                    std::cout << "  Progress: " << progress->completedOperations << "/"
                             << progress->totalOperations << " operations\n";
                    std::cout << "  Bytes: " << (progress->bytesTransferred / (1024*1024)) << " MB\n";
                    std::cout << "  Speed: " << (progress->currentSpeed / (1024*1024)) << " MB/s\n";
                    std::cout << "  Current: " << progress->currentFile << "\n";
                    std::cout << "  Progress: " << std::fixed << std::setprecision(1)
                             << progress->progressPercentage << "%\n";
                }
            }
        }
    }
    else if (cmd == "stop") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom sync stop <sync_id>\n";
            return 1;
        }

        if (sync.stopSync(args[1])) {
            std::cout << "Sync stopped\n";
        } else {
            std::cerr << "Failed to stop sync\n";
            return 1;
        }
    }
    else if (cmd == "pause") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom sync pause <sync_id>\n";
            return 1;
        }

        if (sync.pauseSync(args[1])) {
            std::cout << "Sync paused\n";
        } else {
            std::cerr << "Failed to pause sync\n";
            return 1;
        }
    }
    else if (cmd == "resume") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom sync resume <sync_id>\n";
            return 1;
        }

        if (sync.resumeSync(args[1])) {
            std::cout << "Sync resumed\n";
        } else {
            std::cerr << "Failed to resume sync\n";
            return 1;
        }
    }
    else if (cmd == "schedule") {
        if (args.size() < 3) {
            std::cout << "Usage: megacustom sync schedule <profile_id> <interval_minutes>\n";
            return 1;
        }

        std::string profileId = args[1];
        int intervalMinutes;
        try {
            intervalMinutes = std::stoi(args[2]);
        } catch (const std::exception&) {
            std::cerr << "Error: Invalid interval value\n";
            return 1;
        }

        if (sync.enableAutoSync(profileId, std::chrono::minutes(intervalMinutes))) {
            std::cout << "Auto-sync enabled for profile " << profileId << "\n";
            std::cout << "Sync will run every " << intervalMinutes << " minutes\n";
        } else {
            std::cerr << "Failed to enable auto-sync\n";
            return 1;
        }
    }
    else if (cmd == "stats") {
        std::cout << "Sync Statistics:\n";
        std::cout << sync.getStatistics() << "\n";
    }
    else {
        std::cerr << "Unknown sync command: " << cmd << "\n";
        return 1;
    }

    return 0;
}

/**
 * Handle map command - Folder mapping for VPS-to-MEGA uploads
 */
int handleMap(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "--help") {
        std::cout << "Folder Mapping Commands:\n";
        std::cout << "  list                              List all folder mappings\n";
        std::cout << "  add <name> <local> <remote>       Add new mapping\n";
        std::cout << "  remove <name|number>              Remove mapping\n";
        std::cout << "  enable <name|number>              Enable mapping\n";
        std::cout << "  disable <name|number>             Disable mapping\n";
        std::cout << "  upload <name|number> [--dry-run]  Upload mapped folder\n";
        std::cout << "  upload-all [--dry-run]            Upload all enabled mappings\n";
        std::cout << "  preview <name|number>             Show what would be uploaded\n";
        std::cout << "  status <name|number>              Show mapping details\n";
        std::cout << "\nOptions:\n";
        std::cout << "  --dry-run     Preview changes without uploading\n";
        std::cout << "  --full        Upload all files (skip incremental check)\n";
        std::cout << "  --no-progress Disable progress display\n";
        std::cout << "\nExamples:\n";
        std::cout << "  megacustom map add site1 /var/www/site1 /Website1\n";
        std::cout << "  megacustom map upload site1\n";
        std::cout << "  megacustom map upload 1 --dry-run\n";
        std::cout << "  megacustom map upload-all\n";
        return 0;
    }

    // Initialize MegaManager and restore session
    MegaCustom::MegaManager& manager = MegaCustom::MegaManager::getInstance();
    if (!initializeManager(manager)) {
        return 1;
    }

    // Check if logged in (except for list command)
    const std::string& cmd = args[0];
    if (cmd != "list" && cmd != "add" && cmd != "remove" && cmd != "enable" && cmd != "disable") {
        MegaCustom::AuthenticationModule auth(manager.getMegaApi());
        if (!auth.isLoggedIn()) {
            std::cerr << "Please login first using: megacustom auth login <email> <password>\n";
            return 1;
        }
    }

    MegaCustom::FolderMapper mapper(manager.getMegaApi());

    // ============ LIST ============
    if (cmd == "list") {
        auto mappings = mapper.getAllMappings();
        if (mappings.empty()) {
            std::cout << "No folder mappings configured.\n";
            std::cout << "Add one with: megacustom map add <name> <local-path> <remote-path>\n";
            return 0;
        }

        std::cout << "\nFolder Mappings:\n";
        std::cout << std::string(80, '-') << "\n";
        std::cout << std::setw(4) << "#" << "  "
                  << std::setw(12) << std::left << "Name" << "  "
                  << std::setw(6) << "Status" << "  "
                  << "Local -> Remote\n";
        std::cout << std::string(80, '-') << "\n";

        int index = 1;
        for (const auto& m : mappings) {
            std::cout << std::setw(4) << std::right << index++ << "  "
                      << std::setw(12) << std::left << m.name << "  "
                      << std::setw(6) << (m.enabled ? "ON" : "OFF") << "  "
                      << m.localPath << " -> " << m.remotePath << "\n";
        }
        std::cout << std::string(80, '-') << "\n";
        std::cout << "Total: " << mappings.size() << " mappings\n";
        return 0;
    }

    // ============ ADD ============
    else if (cmd == "add") {
        if (args.size() < 4) {
            std::cout << "Usage: megacustom map add <name> <local-path> <remote-path> [description]\n";
            std::cout << "Example: megacustom map add site1 /var/www/site1 /Website1\n";
            return 1;
        }

        std::string description = (args.size() > 4) ? args[4] : "";
        if (mapper.addMapping(args[1], args[2], args[3], description)) {
            std::cout << "Mapping added: " << args[1] << "\n";
            std::cout << "  Local:  " << args[2] << "\n";
            std::cout << "  Remote: " << args[3] << "\n";
            return 0;
        }
        return 1;
    }

    // ============ REMOVE ============
    else if (cmd == "remove") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom map remove <name|number>\n";
            return 1;
        }

        if (mapper.removeMapping(args[1])) {
            std::cout << "Mapping removed: " << args[1] << "\n";
            return 0;
        }
        return 1;
    }

    // ============ ENABLE/DISABLE ============
    else if (cmd == "enable" || cmd == "disable") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom map " << cmd << " <name|number>\n";
            return 1;
        }

        bool enable = (cmd == "enable");
        if (mapper.setMappingEnabled(args[1], enable)) {
            std::cout << "Mapping " << (enable ? "enabled" : "disabled") << ": " << args[1] << "\n";
            return 0;
        }
        return 1;
    }

    // ============ STATUS ============
    else if (cmd == "status") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom map status <name|number>\n";
            return 1;
        }

        auto mapping = mapper.getMapping(args[1]);
        if (!mapping) {
            std::cerr << "Mapping not found: " << args[1] << "\n";
            return 1;
        }

        std::cout << "\nMapping: " << mapping->name << "\n";
        std::cout << std::string(40, '-') << "\n";
        std::cout << "  Status:      " << (mapping->enabled ? "Enabled" : "Disabled") << "\n";
        std::cout << "  Local Path:  " << mapping->localPath << "\n";
        std::cout << "  Remote Path: " << mapping->remotePath << "\n";
        if (!mapping->description.empty()) {
            std::cout << "  Description: " << mapping->description << "\n";
        }
        if (mapping->lastFileCount > 0) {
            std::cout << "  Last Upload: " << mapping->lastFileCount << " files ("
                      << MegaCustom::FolderMapper::formatSize(mapping->lastByteCount) << ")\n";
        }

        // Validate paths
        auto errors = mapper.validateMapping(*mapping);
        if (!errors.empty()) {
            std::cout << "\n  Warnings:\n";
            for (const auto& err : errors) {
                std::cout << "    - " << err << "\n";
            }
        }
        return 0;
    }

    // ============ PREVIEW ============
    else if (cmd == "preview") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom map preview <name|number>\n";
            return 1;
        }

        MegaCustom::UploadOptions options;
        options.dryRun = true;
        options.showProgress = true;
        options.incremental = true;

        mapper.uploadMapping(args[1], options);
        return 0;
    }

    // ============ UPLOAD ============
    else if (cmd == "upload") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom map upload <name|number> [--dry-run] [--full]\n";
            return 1;
        }

        MegaCustom::UploadOptions options;
        options.showProgress = true;
        options.incremental = true;
        options.dryRun = false;

        // Parse options
        for (size_t i = 2; i < args.size(); i++) {
            if (args[i] == "--dry-run") options.dryRun = true;
            else if (args[i] == "--full") options.incremental = false;
            else if (args[i] == "--no-progress") options.showProgress = false;
        }

        auto result = mapper.uploadMapping(args[1], options);
        return result.success ? 0 : 1;
    }

    // ============ UPLOAD-ALL ============
    else if (cmd == "upload-all") {
        MegaCustom::UploadOptions options;
        options.showProgress = true;
        options.incremental = true;
        options.dryRun = false;

        // Parse options
        for (size_t i = 1; i < args.size(); i++) {
            if (args[i] == "--dry-run") options.dryRun = true;
            else if (args[i] == "--full") options.incremental = false;
            else if (args[i] == "--no-progress") options.showProgress = false;
        }

        auto mappings = mapper.getAllMappings();
        int enabledCount = 0;
        for (const auto& m : mappings) {
            if (m.enabled) enabledCount++;
        }

        if (enabledCount == 0) {
            std::cout << "No enabled mappings to upload.\n";
            return 0;
        }

        std::cout << "\n=== Uploading " << enabledCount << " folder mappings ===\n";

        auto results = mapper.uploadAll(options);

        // Summary
        int successCount = 0, failCount = 0;
        long long totalBytes = 0;
        int totalFiles = 0;

        for (const auto& r : results) {
            if (r.success) {
                successCount++;
                totalBytes += r.bytesUploaded;
                totalFiles += r.filesUploaded;
            } else {
                failCount++;
            }
        }

        std::cout << "\n=== Upload Summary ===\n";
        std::cout << "  Successful: " << successCount << "/" << results.size() << " mappings\n";
        std::cout << "  Files:      " << totalFiles << " uploaded\n";
        std::cout << "  Data:       " << MegaCustom::FolderMapper::formatSize(totalBytes) << "\n";

        return (failCount == 0) ? 0 : 1;
    }

    else {
        std::cerr << "Unknown map command: " << cmd << "\n";
        std::cerr << "Use 'megacustom map --help' for usage.\n";
        return 1;
    }

    return 0;
}

/**
 * Handle rename command
 */
int handleRename(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "--help") {
        std::cout << "Rename Commands:\n";
        std::cout << "  single <path> <new-name>         Rename a single file/folder\n";
        std::cout << "  regex <path> <pattern> <replace> Rename using regex in a folder\n";
        std::cout << "  prefix <path> <prefix>           Add prefix to files in folder\n";
        std::cout << "  suffix <path> <suffix>           Add suffix to files in folder\n";
        std::cout << "  replace <path> <find> <replace>  Replace text in filenames\n";
        std::cout << "  sequence <path> <prefix>         Rename to sequence (prefix_001, etc)\n";
        std::cout << "\nOptions:\n";
        std::cout << "  --preview                        Show what would be renamed (dry run)\n";
        std::cout << "  --recursive                      Process subdirectories\n";
        std::cout << "  --files-only                     Only rename files (not folders)\n";
        std::cout << "  --folders-only                   Only rename folders (not files)\n";
        return 0;
    }

    // Initialize MegaManager and restore session
    MegaCustom::MegaManager& manager = MegaCustom::MegaManager::getInstance();
    if (!initializeManager(manager)) {
        return 1;
    }

    // Check if logged in
    MegaCustom::AuthenticationModule auth(manager.getMegaApi());
    if (!auth.isLoggedIn()) {
        std::cerr << "Please login first using: megacustom auth login <email> <password>\n";
        return 1;
    }

    const std::string& cmd = args[0];

    // Check options
    bool preview = false;
    bool recursive = false;
    bool filesOnly = false;
    bool foldersOnly = false;

    for (const auto& arg : args) {
        if (arg == "--preview") preview = true;
        if (arg == "--recursive") recursive = true;
        if (arg == "--files-only") filesOnly = true;
        if (arg == "--folders-only") foldersOnly = true;
    }

    // Create FolderManager for operations
    MegaCustom::FolderManager folderMgr(manager.getMegaApi());
    mega::MegaApi* megaApi = manager.getMegaApi();

    if (cmd == "single") {
        if (args.size() < 3) {
            std::cout << "Usage: megacustom rename single <path> <new-name>\n";
            return 1;
        }

        const std::string& path = args[1];
        const std::string& newName = args[2];

        // Get the node
        mega::MegaNode* node = megaApi->getNodeByPath(path.c_str());
        if (!node) {
            std::cerr << "✗ Path not found: " << path << "\n";
            return 1;
        }

        std::cout << "Renaming: " << node->getName() << " -> " << newName << "\n";

        if (preview) {
            std::cout << "  [Preview mode - no changes made]\n";
            delete node;
            return 0;
        }

        // Use async rename with synchronous wait
        SyncRenameListener renameListener;
        megaApi->renameNode(node, newName.c_str(), &renameListener);
        renameListener.wait();

        if (renameListener.getErrorCode() == mega::MegaError::API_OK) {
            std::cout << "✓ Renamed successfully\n";
        } else {
            std::cerr << "✗ Rename failed: " << renameListener.getErrorString() << "\n";
        }
        delete node;
    }
    else if (cmd == "prefix" || cmd == "suffix" || cmd == "replace" || cmd == "sequence") {
        if (args.size() < 3) {
            std::cout << "Usage: megacustom rename " << cmd << " <folder-path> <" << cmd << "-value>\n";
            if (cmd == "replace") {
                std::cout << "       megacustom rename replace <folder-path> <find> <replace>\n";
            }
            return 1;
        }

        const std::string& folderPath = args[1];

        // Get folder node
        mega::MegaNode* folderNode = megaApi->getNodeByPath(folderPath.c_str());
        if (!folderNode || !folderNode->isFolder()) {
            std::cerr << "✗ Folder not found: " << folderPath << "\n";
            if (folderNode) delete folderNode;
            return 1;
        }

        // Get children
        mega::MegaNodeList* children = megaApi->getChildren(folderNode);
        if (!children || children->size() == 0) {
            std::cout << "No items found in folder.\n";
            delete folderNode;
            if (children) delete children;
            return 0;
        }

        int renamedCount = 0;
        int skippedCount = 0;
        int errorCount = 0;
        int sequenceNum = 1;

        std::cout << "Processing " << children->size() << " items...\n";

        for (int i = 0; i < children->size(); ++i) {
            mega::MegaNode* child = children->get(i);
            bool isFolder = child->isFolder();

            // Apply filters
            if (filesOnly && isFolder) {
                skippedCount++;
                continue;
            }
            if (foldersOnly && !isFolder) {
                skippedCount++;
                continue;
            }

            std::string oldName = child->getName();
            std::string newName;

            if (cmd == "prefix") {
                newName = args[2] + oldName;
            }
            else if (cmd == "suffix") {
                // Insert suffix before extension for files
                size_t dotPos = oldName.rfind('.');
                if (!isFolder && dotPos != std::string::npos) {
                    newName = oldName.substr(0, dotPos) + args[2] + oldName.substr(dotPos);
                } else {
                    newName = oldName + args[2];
                }
            }
            else if (cmd == "replace") {
                if (args.size() < 4) {
                    std::cerr << "Replace requires both <find> and <replace> arguments\n";
                    delete children;
                    delete folderNode;
                    return 1;
                }
                const std::string& find = args[2];
                const std::string& replace = args[3];
                newName = oldName;
                size_t pos = 0;
                while ((pos = newName.find(find, pos)) != std::string::npos) {
                    newName.replace(pos, find.length(), replace);
                    pos += replace.length();
                }
            }
            else if (cmd == "sequence") {
                const std::string& seqPrefix = args[2];
                size_t dotPos = oldName.rfind('.');
                char seqStr[16];
                snprintf(seqStr, sizeof(seqStr), "%s_%03d", seqPrefix.c_str(), sequenceNum);
                if (!isFolder && dotPos != std::string::npos) {
                    newName = std::string(seqStr) + oldName.substr(dotPos);
                } else {
                    newName = seqStr;
                }
                sequenceNum++;
            }

            // Skip if name unchanged
            if (newName == oldName) {
                skippedCount++;
                continue;
            }

            std::cout << "  " << oldName << " -> " << newName;

            if (preview) {
                std::cout << " [preview]\n";
                renamedCount++;
            } else {
                SyncRenameListener renameListener;
                megaApi->renameNode(child, newName.c_str(), &renameListener);
                renameListener.wait();

                if (renameListener.getErrorCode() == mega::MegaError::API_OK) {
                    std::cout << " ✓\n";
                    renamedCount++;
                } else {
                    std::cout << " ✗ " << renameListener.getErrorString() << "\n";
                    errorCount++;
                }
            }
        }

        delete children;
        delete folderNode;

        std::cout << "\nSummary:\n";
        std::cout << "  " << (preview ? "Would rename" : "Renamed") << ": " << renamedCount << "\n";
        std::cout << "  Skipped: " << skippedCount << "\n";
        if (errorCount > 0) {
            std::cout << "  Errors: " << errorCount << "\n";
        }
    }
    else if (cmd == "regex") {
        if (args.size() < 4) {
            std::cout << "Usage: megacustom rename regex <folder-path> <pattern> <replacement>\n";
            std::cout << "Example: megacustom rename regex /MyFolder \"(.*)_old\" \"$1_new\"\n";
            return 1;
        }

        const std::string& folderPath = args[1];
        const std::string& pattern = args[2];
        const std::string& replacement = args[3];

        // Get folder node
        mega::MegaNode* folderNode = megaApi->getNodeByPath(folderPath.c_str());
        if (!folderNode || !folderNode->isFolder()) {
            std::cerr << "✗ Folder not found: " << folderPath << "\n";
            if (folderNode) delete folderNode;
            return 1;
        }

        // Compile regex
        std::regex re;
        try {
            re = std::regex(pattern);
        } catch (const std::regex_error& e) {
            std::cerr << "✗ Invalid regex pattern: " << e.what() << "\n";
            delete folderNode;
            return 1;
        }

        // Get children
        mega::MegaNodeList* children = megaApi->getChildren(folderNode);
        if (!children || children->size() == 0) {
            std::cout << "No items found in folder.\n";
            delete folderNode;
            if (children) delete children;
            return 0;
        }

        int renamedCount = 0;
        int skippedCount = 0;
        int errorCount = 0;

        std::cout << "Processing " << children->size() << " items with regex...\n";

        for (int i = 0; i < children->size(); ++i) {
            mega::MegaNode* child = children->get(i);
            bool isFolder = child->isFolder();

            // Apply filters
            if (filesOnly && isFolder) {
                skippedCount++;
                continue;
            }
            if (foldersOnly && !isFolder) {
                skippedCount++;
                continue;
            }

            std::string oldName = child->getName();
            std::string newName = std::regex_replace(oldName, re, replacement);

            // Skip if name unchanged
            if (newName == oldName) {
                skippedCount++;
                continue;
            }

            std::cout << "  " << oldName << " -> " << newName;

            if (preview) {
                std::cout << " [preview]\n";
                renamedCount++;
            } else {
                SyncRenameListener renameListener;
                megaApi->renameNode(child, newName.c_str(), &renameListener);
                renameListener.wait();

                if (renameListener.getErrorCode() == mega::MegaError::API_OK) {
                    std::cout << " ✓\n";
                    renamedCount++;
                } else {
                    std::cout << " ✗ " << renameListener.getErrorString() << "\n";
                    errorCount++;
                }
            }
        }

        delete children;
        delete folderNode;

        std::cout << "\nSummary:\n";
        std::cout << "  " << (preview ? "Would rename" : "Renamed") << ": " << renamedCount << "\n";
        std::cout << "  Skipped: " << skippedCount << "\n";
        if (errorCount > 0) {
            std::cout << "  Errors: " << errorCount << "\n";
        }
    }
    else {
        std::cerr << "Unknown rename command: " << cmd << "\n";
        std::cerr << "Use 'megacustom rename --help' for usage.\n";
        return 1;
    }

    return 0;
}

/**
 * Handle folder command
 */
int handleFolder(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "--help") {
        std::cout << "Folder Commands:\n";
        std::cout << "  create <path>           Create new folder\n";
        std::cout << "  delete <path>           Delete folder\n";
        std::cout << "  move <src> <dst>        Move folder\n";
        std::cout << "  copy <src> <dst>        Copy folder\n";
        std::cout << "  rename <path> <name>    Rename folder\n";
        std::cout << "  list <path>             List folder contents\n";
        std::cout << "  tree <path>             Show folder tree\n";
        std::cout << "  info <path>             Show folder information\n";
        std::cout << "  size <path>             Calculate folder size\n";
        std::cout << "  share <path> <email>    Share folder\n";
        std::cout << "  link <path>             Create public link\n";
        std::cout << "  trash empty             Empty trash\n";
        std::cout << "  trash restore <path>    Restore from trash\n";
        return 0;
    }

    // Initialize MegaManager and restore session
    MegaCustom::MegaManager& manager = MegaCustom::MegaManager::getInstance();
    if (!initializeManager(manager)) {
        return 1;
    }

    // Check if logged in
    MegaCustom::AuthenticationModule auth(manager.getMegaApi());
    if (!auth.isLoggedIn()) {
        std::cerr << "Please login first using: megacustom auth login <email> <password>\n";
        return 1;
    }

    // Create FolderManager instance
    MegaCustom::FolderManager folderMgr(manager.getMegaApi());

    const std::string& cmd = args[0];

    if (cmd == "create") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom folder create <path>\n";
            return 1;
        }

        std::cout << "Creating folder: " << args[1] << "\n";
        auto result = folderMgr.createFolder(args[1], true);

        if (result.success) {
            std::cout << "✓ Folder created successfully\n";
        } else {
            std::cerr << "✗ Failed to create folder: " << result.errorMessage << "\n";
            return 1;
        }
    }
    else if (cmd == "delete") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom folder delete <path>\n";
            return 1;
        }

        std::cout << "Moving folder to trash: " << args[1] << "\n";
        auto result = folderMgr.deleteFolder(args[1], true);  // Move to trash by default

        if (result.success) {
            std::cout << "✓ Folder moved to trash\n";
        } else {
            std::cerr << "✗ Failed to delete folder: " << result.errorMessage << "\n";
            return 1;
        }
    }
    else if (cmd == "move") {
        if (args.size() < 3) {
            std::cout << "Usage: megacustom folder move <source> <destination>\n";
            return 1;
        }

        std::cout << "Moving folder from " << args[1] << " to " << args[2] << "\n";
        auto result = folderMgr.moveFolder(args[1], args[2]);

        if (result.success) {
            std::cout << "✓ Folder moved successfully\n";
        } else {
            std::cerr << "✗ Failed to move folder: " << result.errorMessage << "\n";
            return 1;
        }
    }
    else if (cmd == "copy") {
        if (args.size() < 3) {
            std::cout << "Usage: megacustom folder copy <source> <destination>\n";
            return 1;
        }

        std::cout << "Copying folder from " << args[1] << " to " << args[2] << "\n";
        auto result = folderMgr.copyFolder(args[1], args[2]);

        if (result.success) {
            std::cout << "✓ Folder copied successfully\n";
        } else {
            std::cerr << "✗ Failed to copy folder: " << result.errorMessage << "\n";
            return 1;
        }
    }
    else if (cmd == "rename") {
        if (args.size() < 3) {
            std::cout << "Usage: megacustom folder rename <path> <new-name>\n";
            return 1;
        }

        std::cout << "Renaming folder " << args[1] << " to " << args[2] << "\n";
        auto result = folderMgr.renameFolder(args[1], args[2]);

        if (result.success) {
            std::cout << "✓ Folder renamed successfully\n";
        } else {
            std::cerr << "✗ Failed to rename folder: " << result.errorMessage << "\n";
            return 1;
        }
    }
    else if (cmd == "list") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom folder list <path> [--recursive]\n";
            return 1;
        }

        bool recursive = args.size() > 2 && args[2] == "--recursive";
        std::cout << "Listing contents of: " << args[1] << "\n";

        auto contents = folderMgr.listContents(args[1], recursive, true);

        if (contents.empty()) {
            std::cout << "Folder is empty or doesn't exist\n";
        } else {
            for (const auto& item : contents) {
                std::cout << "  " << item << "\n";
            }
            std::cout << "Total: " << contents.size() << " items\n";
        }
    }
    else if (cmd == "tree") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom folder tree <path> [max-depth]\n";
            return 1;
        }

        int maxDepth = 3;
        if (args.size() > 2) {
            try {
                maxDepth = std::stoi(args[2]);
            } catch (const std::exception&) {
                // Keep default
            }
        }
        std::cout << "Folder tree for: " << args[1] << "\n";

        auto tree = folderMgr.getFolderTree(args[1], maxDepth);
        if (tree) {
            std::function<void(const MegaCustom::FolderTreeNode*, const std::string&)> printTree;
            printTree = [&](const MegaCustom::FolderTreeNode* node, const std::string& indent) {
                std::cout << indent << "📁 " << node->info.name
                         << " (" << node->info.fileCount << " files, "
                         << node->info.folderCount << " folders)\n";

                for (const auto& child : node->children) {
                    printTree(child.get(), indent + "  ");
                }

                if (!node->files.empty() && node->depth < maxDepth) {
                    for (const auto& file : node->files) {
                        std::cout << indent << "  📄 " << file << "\n";
                    }
                }
            };

            printTree(tree.get(), "");
        } else {
            std::cout << "Folder not found or not accessible\n";
        }
    }
    else if (cmd == "info") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom folder info <path>\n";
            return 1;
        }

        auto info = folderMgr.getFolderInfo(args[1]);
        if (info) {
            std::cout << "Folder Information:\n";
            std::cout << "  Name: " << info->name << "\n";
            std::cout << "  Path: " << info->path << "\n";
            std::cout << "  Size: " << info->size / (1024.0 * 1024.0) << " MB\n";
            std::cout << "  Files: " << info->fileCount << "\n";
            std::cout << "  Folders: " << info->folderCount << "\n";
            std::cout << "  Shared: " << (info->isShared ? "Yes" : "No") << "\n";

            if (!info->owner.empty()) {
                std::cout << "  Owner: " << info->owner << "\n";
            }
        } else {
            std::cout << "Folder not found: " << args[1] << "\n";
            return 1;
        }
    }
    else if (cmd == "size") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom folder size <path>\n";
            return 1;
        }

        std::cout << "Calculating size of: " << args[1] << "\n";
        long long size = folderMgr.calculateFolderSize(args[1], true);

        if (size > 0) {
            double mb = size / (1024.0 * 1024.0);
            double gb = mb / 1024.0;

            if (gb >= 1.0) {
                std::cout << "Total size: " << gb << " GB\n";
            } else {
                std::cout << "Total size: " << mb << " MB\n";
            }
        } else {
            std::cout << "Folder is empty or doesn't exist\n";
        }
    }
    else if (cmd == "share") {
        if (args.size() < 3) {
            std::cout << "Usage: megacustom folder share <path> <email> [--readonly]\n";
            return 1;
        }

        bool readOnly = args.size() > 3 && args[3] == "--readonly";
        std::cout << "Sharing folder " << args[1] << " with " << args[2] << "\n";

        auto result = folderMgr.shareFolder(args[1], args[2], readOnly);

        if (result.success) {
            std::cout << "✓ Folder shared successfully\n";
        } else {
            std::cerr << "✗ Failed to share folder: " << result.errorMessage << "\n";
            return 1;
        }
    }
    else if (cmd == "link") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom folder link <path>\n";
            return 1;
        }

        std::cout << "Creating public link for: " << args[1] << "\n";
        std::string link = folderMgr.createPublicLink(args[1]);

        if (!link.empty()) {
            std::cout << "✓ Public link created:\n";
            std::cout << link << "\n";
        } else {
            std::cerr << "✗ Failed to create public link\n";
            return 1;
        }
    }
    else if (cmd == "trash") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom folder trash <empty|restore> [path]\n";
            return 1;
        }

        if (args[1] == "empty") {
            std::cout << "Emptying trash...\n";
            auto result = folderMgr.emptyTrash();

            if (result.success) {
                std::cout << "✓ Trash emptied successfully\n";
            } else {
                std::cerr << "✗ Failed to empty trash: " << result.errorMessage << "\n";
                return 1;
            }
        }
        else if (args[1] == "restore" && args.size() > 2) {
            std::cout << "Restoring from trash: " << args[2] << "\n";
            std::string restorePath = args.size() > 3 ? args[3] : "";
            auto result = folderMgr.restoreFromTrash(args[2], restorePath);

            if (result.success) {
                std::cout << "✓ Item restored successfully\n";
            } else {
                std::cerr << "✗ Failed to restore: " << result.errorMessage << "\n";
                return 1;
            }
        }
        else {
            std::cerr << "Unknown trash command: " << args[1] << "\n";
            return 1;
        }
    }
    else {
        std::cerr << "Unknown folder command: " << cmd << "\n";
        return 1;
    }

    return 0;
}

/**
 * Handle member command
 * Member management for distribution system
 */
int handleMember(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "--help") {
        std::cout << "Member Management Commands:\n";
        std::cout << "  list                              List all members\n";
        std::cout << "  add <id>                          Add new member\n";
        std::cout << "  show <id>                         Show member details\n";
        std::cout << "  update <id>                       Update member info\n";
        std::cout << "  remove <id>                       Remove member\n";
        std::cout << "  bind <id> <mega-folder>           Bind member to MEGA folder\n";
        std::cout << "  unbind <id>                       Unbind member from folder\n";
        std::cout << "  import <file.csv>                 Import members from CSV\n";
        std::cout << "  export <file.csv>                 Export members to CSV\n";
        std::cout << "\nAdd/Update Options:\n";
        std::cout << "  --name <name>                     Display name\n";
        std::cout << "  --email <email>                   Email address\n";
        std::cout << "  --ip <ip>                         IP address for watermark\n";
        std::cout << "  --mac <mac>                       MAC address for watermark\n";
        std::cout << "  --social <handle>                 Social media handle\n";
        std::cout << "  --wp-id <id>                      WordPress user ID\n";
        std::cout << "\nExamples:\n";
        std::cout << "  megacustom member add EGB001 --name \"John Smith\" --email john@example.com\n";
        std::cout << "  megacustom member bind EGB001 /Members/John_EGB001\n";
        std::cout << "  megacustom member list\n";
        std::cout << "  megacustom member import members.csv\n";
        return 0;
    }

    MegaCustom::MemberDatabase db;
    const std::string& cmd = args[0];

    // Helper to parse --option value pairs
    auto getOption = [&args](const std::string& opt) -> std::string {
        for (size_t i = 0; i < args.size() - 1; ++i) {
            if (args[i] == opt) {
                return args[i + 1];
            }
        }
        return "";
    };

    // ============ LIST ============
    if (cmd == "list") {
        MegaCustom::MemberFilter filter;

        // Parse filter options
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--active") filter.activeOnly = true;
            else if (args[i] == "--bound") filter.withFolderBinding = true;
            else if (args[i] == "--search" && i + 1 < args.size()) {
                filter.searchText = args[++i];
            }
        }

        auto result = db.getAllMembers(filter);
        if (!result.success) {
            std::cerr << "Error: " << result.error << "\n";
            return 1;
        }

        if (result.members.empty()) {
            std::cout << "No members found.\n";
            std::cout << "Add one with: megacustom member add <id> --name \"Name\" --email email@example.com\n";
            return 0;
        }

        std::cout << "\nMembers:\n";
        std::cout << std::string(90, '-') << "\n";
        std::cout << std::setw(10) << std::left << "ID" << "  "
                  << std::setw(20) << "Name" << "  "
                  << std::setw(25) << "Email" << "  "
                  << std::setw(6) << "Status" << "  "
                  << "MEGA Folder\n";
        std::cout << std::string(90, '-') << "\n";

        for (const auto& m : result.members) {
            std::cout << std::setw(10) << std::left << m.id << "  "
                      << std::setw(20) << (m.name.length() > 20 ? m.name.substr(0, 17) + "..." : m.name) << "  "
                      << std::setw(25) << (m.email.length() > 25 ? m.email.substr(0, 22) + "..." : m.email) << "  "
                      << std::setw(6) << (m.active ? "Active" : "Inactive") << "  "
                      << (m.megaFolderPath.empty() ? "(not bound)" : m.megaFolderPath) << "\n";
        }
        std::cout << std::string(90, '-') << "\n";
        std::cout << "Total: " << result.members.size() << " members\n";
        return 0;
    }

    // ============ ADD ============
    else if (cmd == "add") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom member add <id> [--name <name>] [--email <email>] ...\n";
            std::cout << "Example: megacustom member add EGB001 --name \"John Smith\" --email john@example.com\n";
            return 1;
        }

        MegaCustom::Member member;
        member.id = args[1];
        member.name = getOption("--name");
        member.email = getOption("--email");
        member.ipAddress = getOption("--ip");
        member.macAddress = getOption("--mac");
        member.socialHandle = getOption("--social");
        member.wpUserId = getOption("--wp-id");

        auto result = db.addMember(member);
        if (result.success) {
            std::cout << "Member added: " << member.id << "\n";
            if (!member.name.empty()) std::cout << "  Name:  " << member.name << "\n";
            if (!member.email.empty()) std::cout << "  Email: " << member.email << "\n";
            return 0;
        } else {
            std::cerr << "Error: " << result.error << "\n";
            return 1;
        }
    }

    // ============ SHOW ============
    else if (cmd == "show") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom member show <id>\n";
            return 1;
        }

        auto result = db.getMember(args[1]);
        if (!result.success || !result.member) {
            std::cerr << "Error: " << (result.error.empty() ? "Member not found" : result.error) << "\n";
            return 1;
        }

        const auto& m = *result.member;
        std::cout << "\nMember Details:\n";
        std::cout << std::string(50, '-') << "\n";
        std::cout << "  ID:           " << m.id << "\n";
        std::cout << "  Name:         " << (m.name.empty() ? "(not set)" : m.name) << "\n";
        std::cout << "  Email:        " << (m.email.empty() ? "(not set)" : m.email) << "\n";
        std::cout << "  IP Address:   " << (m.ipAddress.empty() ? "(not set)" : m.ipAddress) << "\n";
        std::cout << "  MAC Address:  " << (m.macAddress.empty() ? "(not set)" : m.macAddress) << "\n";
        std::cout << "  Social:       " << (m.socialHandle.empty() ? "(not set)" : m.socialHandle) << "\n";
        std::cout << "  WP User ID:   " << (m.wpUserId.empty() ? "(not set)" : m.wpUserId) << "\n";
        std::cout << "  Status:       " << (m.active ? "Active" : "Inactive") << "\n";
        std::cout << std::string(50, '-') << "\n";
        std::cout << "  MEGA Folder:  " << (m.megaFolderPath.empty() ? "(not bound)" : m.megaFolderPath) << "\n";
        if (!m.megaFolderHandle.empty()) {
            std::cout << "  Folder Handle: " << m.megaFolderHandle << "\n";
        }
        std::cout << std::string(50, '-') << "\n";
        std::cout << "  Watermark Fields: ";
        if (m.watermarkFields.empty()) {
            std::cout << "(default: name, email, ip)";
        } else {
            for (size_t i = 0; i < m.watermarkFields.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << m.watermarkFields[i];
            }
        }
        std::cout << "\n";
        std::cout << "  Global Watermark: " << (m.useGlobalWatermark ? "Yes" : "No") << "\n";

        if (!m.customFields.empty()) {
            std::cout << std::string(50, '-') << "\n";
            std::cout << "  Custom Fields:\n";
            for (const auto& [key, value] : m.customFields) {
                std::cout << "    " << key << ": " << value << "\n";
            }
        }
        return 0;
    }

    // ============ UPDATE ============
    else if (cmd == "update") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom member update <id> [--name <name>] [--email <email>] ...\n";
            return 1;
        }

        auto result = db.getMember(args[1]);
        if (!result.success || !result.member) {
            std::cerr << "Error: Member not found\n";
            return 1;
        }

        MegaCustom::Member member = *result.member;

        // Update only provided fields
        std::string val;
        if (!(val = getOption("--name")).empty()) member.name = val;
        if (!(val = getOption("--email")).empty()) member.email = val;
        if (!(val = getOption("--ip")).empty()) member.ipAddress = val;
        if (!(val = getOption("--mac")).empty()) member.macAddress = val;
        if (!(val = getOption("--social")).empty()) member.socialHandle = val;
        if (!(val = getOption("--wp-id")).empty()) member.wpUserId = val;

        // Handle active status
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--active") member.active = true;
            else if (args[i] == "--inactive") member.active = false;
        }

        auto updateResult = db.updateMember(member);
        if (updateResult.success) {
            std::cout << "Member updated: " << member.id << "\n";
            return 0;
        } else {
            std::cerr << "Error: " << updateResult.error << "\n";
            return 1;
        }
    }

    // ============ REMOVE ============
    else if (cmd == "remove") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom member remove <id>\n";
            return 1;
        }

        auto result = db.removeMember(args[1]);
        if (result.success) {
            std::cout << "Member removed: " << args[1] << "\n";
            return 0;
        } else {
            std::cerr << "Error: " << result.error << "\n";
            return 1;
        }
    }

    // ============ BIND ============
    else if (cmd == "bind") {
        if (args.size() < 3) {
            std::cout << "Usage: megacustom member bind <id> <mega-folder-path>\n";
            std::cout << "Example: megacustom member bind EGB001 /Members/John_EGB001\n";
            return 1;
        }

        auto result = db.bindFolder(args[1], args[2]);
        if (result.success) {
            std::cout << "Member " << args[1] << " bound to folder: " << args[2] << "\n";
            return 0;
        } else {
            std::cerr << "Error: " << result.error << "\n";
            return 1;
        }
    }

    // ============ UNBIND ============
    else if (cmd == "unbind") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom member unbind <id>\n";
            return 1;
        }

        auto result = db.unbindFolder(args[1]);
        if (result.success) {
            std::cout << "Member " << args[1] << " unbound from MEGA folder\n";
            return 0;
        } else {
            std::cerr << "Error: " << result.error << "\n";
            return 1;
        }
    }

    // ============ IMPORT ============
    else if (cmd == "import") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom member import <file.csv|file.json>\n";
            std::cout << "\nCSV Format (with header):\n";
            std::cout << "  id,name,email,ip,mac,social,mega_folder\n";
            return 1;
        }

        const std::string& file = args[1];
        MegaCustom::MemberResult result;

        if (file.find(".json") != std::string::npos) {
            result = db.importFromJson(file);
        } else {
            result = db.importFromCsv(file, true);  // Skip header
        }

        if (result.success) {
            std::cout << "Imported " << result.members.size() << " members from " << file << "\n";
            return 0;
        } else {
            std::cerr << "Error: " << result.error << "\n";
            return 1;
        }
    }

    // ============ EXPORT ============
    else if (cmd == "export") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom member export <file.csv|file.json>\n";
            return 1;
        }

        const std::string& file = args[1];
        MegaCustom::MemberResult result;

        if (file.find(".json") != std::string::npos) {
            result = db.exportToJson(file);
        } else {
            result = db.exportToCsv(file);
        }

        if (result.success) {
            std::cout << "Exported members to " << file << "\n";
            return 0;
        } else {
            std::cerr << "Error: " << result.error << "\n";
            return 1;
        }
    }

    else {
        std::cerr << "Unknown member command: " << cmd << "\n";
        std::cerr << "Use 'megacustom member --help' for usage information.\n";
        return 1;
    }

    return 0;
}

/**
 * Handle watermark command
 * Video and PDF watermarking for content protection
 */
int handleWatermark(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "--help") {
        std::cout << "Watermark Commands:\n";
        std::cout << "  video <input> [output]              Watermark a video file\n";
        std::cout << "  pdf <input> [output]                Watermark a PDF file\n";
        std::cout << "  file <input> [output]               Auto-detect and watermark\n";
        std::cout << "  batch <dir> <output-dir>            Watermark all files in directory\n";
        std::cout << "  member <input> <member-id> [dir]    Watermark for specific member\n";
        std::cout << "  check                               Check FFmpeg/Python availability\n";
        std::cout << "\nOptions:\n";
        std::cout << "  --text <text>                       Primary watermark text\n";
        std::cout << "  --secondary <text>                  Secondary line text\n";
        std::cout << "  --interval <seconds>                Time between appearances (default: 600)\n";
        std::cout << "  --duration <seconds>                How long watermark shows (default: 3)\n";
        std::cout << "  --font <path>                       Path to font file\n";
        std::cout << "  --preset <preset>                   FFmpeg preset (ultrafast/fast/medium)\n";
        std::cout << "  --crf <value>                       Quality 18-28 (default: 23)\n";
        std::cout << "  --opacity <value>                   PDF opacity 0.0-1.0 (default: 0.3)\n";
        std::cout << "  --coverage <value>                  PDF page coverage 0.0-1.0 (default: 0.5)\n";
        std::cout << "  --parallel <n>                      Parallel jobs for batch (default: 1)\n";
        std::cout << "\nExamples:\n";
        std::cout << "  megacustom watermark video input.mp4 output.mp4 --text \"My Brand\"\n";
        std::cout << "  megacustom watermark pdf doc.pdf --text \"Confidential\" --opacity 0.2\n";
        std::cout << "  megacustom watermark member video.mp4 EGB001\n";
        std::cout << "  megacustom watermark batch /videos /output --parallel 4\n";
        return 0;
    }

    // Check dependencies first
    if (args[0] == "check") {
        std::cout << "Checking watermarking dependencies...\n\n";

        bool ffmpegOk = MegaCustom::Watermarker::isFFmpegAvailable();
        bool pythonOk = MegaCustom::Watermarker::isPythonAvailable();

        std::cout << "FFmpeg:  " << (ffmpegOk ? "✓ Available" : "✗ Not found") << "\n";
        std::cout << "Python:  " << (pythonOk ? "✓ Available" : "✗ Not found") << "\n";
        std::cout << "Script:  " << MegaCustom::Watermarker::getPdfScriptPath() << "\n";

        if (!ffmpegOk) {
            std::cout << "\nTo install FFmpeg:\n";
            std::cout << "  Ubuntu/Debian: sudo apt install ffmpeg\n";
            std::cout << "  Or download static build to bin/ffmpeg\n";
        }
        if (!pythonOk) {
            std::cout << "\nTo install Python with dependencies:\n";
            std::cout << "  pip install reportlab PyPDF2\n";
        }

        return (ffmpegOk && pythonOk) ? 0 : 1;
    }

    MegaCustom::Watermarker watermarker;
    MegaCustom::WatermarkConfig config;
    const std::string& cmd = args[0];

    // Helper to parse --option value pairs
    auto getOption = [&args](const std::string& opt) -> std::string {
        for (size_t i = 0; i < args.size() - 1; ++i) {
            if (args[i] == opt) {
                return args[i + 1];
            }
        }
        return "";
    };

    auto getIntOption = [&getOption](const std::string& opt, int defaultVal) -> int {
        std::string val = getOption(opt);
        if (val.empty()) return defaultVal;
        try {
            return std::stoi(val);
        } catch (const std::exception&) {
            return defaultVal;
        }
    };

    auto getDoubleOption = [&getOption](const std::string& opt, double defaultVal) -> double {
        std::string val = getOption(opt);
        if (val.empty()) return defaultVal;
        try {
            return std::stod(val);
        } catch (const std::exception&) {
            return defaultVal;
        }
    };

    // Parse common options
    std::string textOpt = getOption("--text");
    if (!textOpt.empty()) config.primaryText = textOpt;

    std::string secOpt = getOption("--secondary");
    if (!secOpt.empty()) config.secondaryText = secOpt;

    config.intervalSeconds = getIntOption("--interval", 600);
    config.durationSeconds = getIntOption("--duration", 3);
    config.crf = getIntOption("--crf", 23);
    config.pdfOpacity = getDoubleOption("--opacity", 0.3);
    config.pdfCoverage = getDoubleOption("--coverage", 0.5);

    std::string presetOpt = getOption("--preset");
    if (!presetOpt.empty()) config.preset = presetOpt;

    std::string fontOpt = getOption("--font");
    if (!fontOpt.empty()) config.fontPath = fontOpt;

    watermarker.setConfig(config);

    // Set progress callback
    watermarker.setProgressCallback([](const MegaCustom::WatermarkProgress& progress) {
        std::cout << "\r[" << progress.currentIndex << "/" << progress.totalFiles << "] "
                  << progress.status << ": " << progress.currentFile
                  << " (" << std::fixed << std::setprecision(1) << progress.percentComplete << "%)"
                  << std::flush;
    });

    // ============ VIDEO ============
    if (cmd == "video") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom watermark video <input> [output] [--text <text>]\n";
            return 1;
        }

        if (!MegaCustom::Watermarker::isFFmpegAvailable()) {
            std::cerr << "Error: FFmpeg not found. Run 'megacustom watermark check' for info.\n";
            return 1;
        }

        std::string input = args[1];
        std::string output = (args.size() > 2 && args[2][0] != '-') ? args[2] : "";

        if (config.primaryText.empty()) {
            config.primaryText = "Easygroupbuys.com";
            watermarker.setConfig(config);
        }

        std::cout << "Watermarking video: " << input << "\n";
        auto result = watermarker.watermarkVideo(input, output);

        if (result.success) {
            std::cout << "\n✓ Video watermarked successfully\n";
            std::cout << "  Output: " << result.outputFile << "\n";
            std::cout << "  Time: " << (result.processingTimeMs / 1000) << "s\n";
            return 0;
        } else {
            std::cerr << "\n✗ Failed: " << result.error << "\n";
            return 1;
        }
    }

    // ============ PDF ============
    else if (cmd == "pdf") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom watermark pdf <input> [output] [--text <text>]\n";
            return 1;
        }

        if (!MegaCustom::Watermarker::isPythonAvailable()) {
            std::cerr << "Error: Python not found. Run 'megacustom watermark check' for info.\n";
            return 1;
        }

        std::string input = args[1];
        std::string output = (args.size() > 2 && args[2][0] != '-') ? args[2] : "";

        if (config.primaryText.empty()) {
            config.primaryText = "Easygroupbuys.com";
            watermarker.setConfig(config);
        }

        std::cout << "Watermarking PDF: " << input << "\n";
        auto result = watermarker.watermarkPdf(input, output);

        if (result.success) {
            std::cout << "\n✓ PDF watermarked successfully\n";
            std::cout << "  Output: " << result.outputFile << "\n";
            std::cout << "  Time: " << (result.processingTimeMs / 1000) << "s\n";
            return 0;
        } else {
            std::cerr << "\n✗ Failed: " << result.error << "\n";
            return 1;
        }
    }

    // ============ FILE (auto-detect) ============
    else if (cmd == "file") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom watermark file <input> [output] [--text <text>]\n";
            return 1;
        }

        std::string input = args[1];
        std::string output = (args.size() > 2 && args[2][0] != '-') ? args[2] : "";

        if (config.primaryText.empty()) {
            config.primaryText = "Easygroupbuys.com";
            watermarker.setConfig(config);
        }

        std::cout << "Watermarking file: " << input << "\n";
        auto result = watermarker.watermarkFile(input, output);

        if (result.success) {
            std::cout << "\n✓ File watermarked successfully\n";
            std::cout << "  Output: " << result.outputFile << "\n";
            return 0;
        } else {
            std::cerr << "\n✗ Failed: " << result.error << "\n";
            return 1;
        }
    }

    // ============ BATCH ============
    else if (cmd == "batch") {
        if (args.size() < 3) {
            std::cout << "Usage: megacustom watermark batch <input-dir> <output-dir> [--parallel <n>]\n";
            return 1;
        }

        std::string inputDir = args[1];
        std::string outputDir = args[2];
        int parallel = getIntOption("--parallel", 1);
        bool recursive = false;

        // Check for --recursive flag
        for (const auto& arg : args) {
            if (arg == "--recursive" || arg == "-r") {
                recursive = true;
                break;
            }
        }

        if (config.primaryText.empty()) {
            config.primaryText = "Easygroupbuys.com";
            watermarker.setConfig(config);
        }

        std::cout << "Batch watermarking directory: " << inputDir << "\n";
        std::cout << "Output directory: " << outputDir << "\n";
        std::cout << "Parallel jobs: " << parallel << "\n";

        auto results = watermarker.watermarkDirectory(inputDir, outputDir, recursive, parallel);

        int success = 0, failed = 0;
        for (const auto& r : results) {
            if (r.success) success++;
            else failed++;
        }

        std::cout << "\n\nBatch complete: " << success << " successful, " << failed << " failed\n";
        return (failed == 0) ? 0 : 1;
    }

    // ============ MEMBER (per-member watermarking) ============
    else if (cmd == "member") {
        if (args.size() < 3) {
            std::cout << "Usage: megacustom watermark member <input> <member-id> [output-dir]\n";
            std::cout << "Example: megacustom watermark member video.mp4 EGB001\n";
            return 1;
        }

        std::string input = args[1];
        std::string memberId = args[2];
        std::string outputDir = (args.size() > 3 && args[3][0] != '-') ? args[3] : "";

        // Verify member exists
        MegaCustom::MemberDatabase db;
        auto memberResult = db.getMember(memberId);
        if (!memberResult.success || !memberResult.member) {
            std::cerr << "Error: Member not found: " << memberId << "\n";
            std::cerr << "Add the member first with: megacustom member add " << memberId << " --name \"Name\"\n";
            return 1;
        }

        std::cout << "Watermarking for member: " << memberId << " (" << memberResult.member->name << ")\n";
        std::cout << "Input: " << input << "\n";

        MegaCustom::WatermarkResult result;
        if (MegaCustom::Watermarker::isVideoFile(input)) {
            result = watermarker.watermarkVideoForMember(input, memberId, outputDir);
        } else if (MegaCustom::Watermarker::isPdfFile(input)) {
            result = watermarker.watermarkPdfForMember(input, memberId, outputDir);
        } else {
            std::cerr << "Error: Unsupported file type. Must be video or PDF.\n";
            return 1;
        }

        if (result.success) {
            std::cout << "\n✓ Watermarked successfully for member " << memberId << "\n";
            std::cout << "  Output: " << result.outputFile << "\n";
            return 0;
        } else {
            std::cerr << "\n✗ Failed: " << result.error << "\n";
            return 1;
        }
    }

    else {
        std::cerr << "Unknown watermark command: " << cmd << "\n";
        std::cerr << "Use 'megacustom watermark --help' for usage information.\n";
        return 1;
    }

    return 0;
}

/**
 * Handle distribute command - integrated watermark + upload pipeline
 */
int handleDistribute(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "--help") {
        std::cout << "Distribution Pipeline Commands:\n";
        std::cout << "  run <files...> --members <id1,id2,...>   Distribute files to members\n";
        std::cout << "  preview <files...> --members <ids>       Preview distribution without executing\n";
        std::cout << "  list-targets                              List members with folders bound\n";
        std::cout << "\nOptions:\n";
        std::cout << "  --members <ids>                          Comma-separated member IDs (or 'all')\n";
        std::cout << "  --mode <mode>                            Watermark mode: per-member, global, none\n";
        std::cout << "  --text <text>                            Global watermark text (for mode=global)\n";
        std::cout << "  --secondary <text>                       Secondary line (for mode=global)\n";
        std::cout << "  --parallel <n>                           Parallel watermark jobs (default: 2)\n";
        std::cout << "  --keep-temp                              Don't delete temp files after upload\n";
        std::cout << "  --temp-dir <dir>                         Custom temp directory\n";
        std::cout << "\nWorkflow:\n";
        std::cout << "  1. Select source files (videos/PDFs)\n";
        std::cout << "  2. Select target members (with MEGA folder bindings)\n";
        std::cout << "  3. For each member:\n";
        std::cout << "     - Watermark file with member-specific info\n";
        std::cout << "     - Upload to member's bound MEGA folder\n";
        std::cout << "     - Clean up temp file\n";
        std::cout << "\nExamples:\n";
        std::cout << "  megacustom distribute run video.mp4 doc.pdf --members all\n";
        std::cout << "  megacustom distribute run *.mp4 --members EGB001,EGB002\n";
        std::cout << "  megacustom distribute preview course.mp4 --members all\n";
        std::cout << "  megacustom distribute run video.mp4 --mode global --text \"My Brand\"\n";
        return 0;
    }

    const std::string& cmd = args[0];

    // Helper to parse --option value pairs
    auto getOption = [&args](const std::string& opt) -> std::string {
        for (size_t i = 0; i < args.size() - 1; ++i) {
            if (args[i] == opt) {
                return args[i + 1];
            }
        }
        return "";
    };

    auto hasFlag = [&args](const std::string& flag) -> bool {
        for (const auto& arg : args) {
            if (arg == flag) return true;
        }
        return false;
    };

    auto getIntOption = [&getOption](const std::string& opt, int defaultVal) -> int {
        std::string val = getOption(opt);
        if (val.empty()) return defaultVal;
        try {
            return std::stoi(val);
        } catch (const std::exception&) {
            return defaultVal;
        }
    };

    // ============ LIST-TARGETS ============
    if (cmd == "list-targets") {
        MegaCustom::DistributionPipeline pipeline;
        auto members = pipeline.getMembersWithFolders();

        if (members.empty()) {
            std::cout << "No members with distribution folders bound.\n";
            std::cout << "Use 'megacustom member bind <id> <folder>' to bind MEGA folders to members.\n";
            return 0;
        }

        MegaCustom::MemberDatabase db;
        db.reload();

        std::cout << "Members with distribution folders:\n\n";
        std::cout << std::left << std::setw(12) << "ID"
                  << std::setw(20) << "Name"
                  << "Folder\n";
        std::cout << std::string(70, '-') << "\n";

        for (const auto& id : members) {
            auto result = db.getMember(id);
            if (result.success && result.member) {
                std::cout << std::left << std::setw(12) << id
                          << std::setw(20) << result.member->name
                          << result.member->megaFolderPath << "\n";
            }
        }

        std::cout << "\nTotal: " << members.size() << " members\n";
        return 0;
    }

    // ============ PREVIEW ============
    if (cmd == "preview") {
        // Collect source files (all args until --option)
        std::vector<std::string> sourceFiles;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i][0] == '-') break;
            sourceFiles.push_back(args[i]);
        }

        if (sourceFiles.empty()) {
            std::cerr << "Error: No source files specified\n";
            return 1;
        }

        // Parse members
        std::string membersOpt = getOption("--members");
        std::vector<std::string> memberIds;
        if (!membersOpt.empty() && membersOpt != "all") {
            // Split by comma
            std::stringstream ss(membersOpt);
            std::string id;
            while (std::getline(ss, id, ',')) {
                memberIds.push_back(id);
            }
        }
        // If empty or "all", preview will use all members with folders

        MegaCustom::DistributionPipeline pipeline;
        auto result = pipeline.previewDistribution(sourceFiles, memberIds);

        std::cout << "Distribution Preview\n";
        std::cout << std::string(60, '=') << "\n\n";

        std::cout << "Source files: " << result.sourceFiles.size() << "\n";
        for (const auto& f : result.sourceFiles) {
            std::cout << "  - " << f << "\n";
        }

        std::cout << "\nTarget members: " << result.totalMembers << "\n";
        for (const auto& m : result.memberResults) {
            std::string status;
            switch (m.state) {
                case MegaCustom::MemberDistributionStatus::State::Pending:
                    status = "Ready";
                    break;
                case MegaCustom::MemberDistributionStatus::State::Skipped:
                    status = "Skipped (no folder)";
                    break;
                default:
                    status = "Unknown";
            }

            std::cout << "\n  " << m.memberId << " (" << m.memberName << ") - " << status << "\n";
            if (!m.destinationFolder.empty()) {
                std::cout << "    Destination: " << m.destinationFolder << "\n";
                for (const auto& f : m.files) {
                    std::cout << "      -> " << f.uploadedPath << "\n";
                }
            }
        }

        std::cout << "\nTotal operations: " << result.totalFiles << " file uploads\n";

        if (!result.errors.empty()) {
            std::cout << "\nWarnings:\n";
            for (const auto& e : result.errors) {
                std::cout << "  ! " << e << "\n";
            }
        }

        return 0;
    }

    // ============ RUN ============
    if (cmd == "run") {
        // Collect source files
        std::vector<std::string> sourceFiles;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i][0] == '-') break;
            sourceFiles.push_back(args[i]);
        }

        if (sourceFiles.empty()) {
            std::cerr << "Error: No source files specified\n";
            std::cerr << "Usage: megacustom distribute run <files...> --members <ids>\n";
            return 1;
        }

        // Parse members
        std::string membersOpt = getOption("--members");
        if (membersOpt.empty()) {
            std::cerr << "Error: --members required. Use 'all' for all members with folders.\n";
            return 1;
        }

        std::vector<std::string> memberIds;
        if (membersOpt != "all") {
            std::stringstream ss(membersOpt);
            std::string id;
            while (std::getline(ss, id, ',')) {
                memberIds.push_back(id);
            }
        }

        // Configure pipeline
        MegaCustom::DistributionPipeline pipeline;
        MegaCustom::DistributionConfig config;

        // Parse watermark mode
        std::string modeOpt = getOption("--mode");
        if (modeOpt == "global") {
            config.watermarkMode = MegaCustom::DistributionConfig::WatermarkMode::Global;
            config.globalPrimaryText = getOption("--text");
            config.globalSecondaryText = getOption("--secondary");
            if (config.globalPrimaryText.empty()) {
                config.globalPrimaryText = "Easygroupbuys.com";
            }
        } else if (modeOpt == "none") {
            config.watermarkMode = MegaCustom::DistributionConfig::WatermarkMode::None;
        } else {
            config.watermarkMode = MegaCustom::DistributionConfig::WatermarkMode::PerMember;
        }

        config.parallelWatermarkJobs = getIntOption("--parallel", 2);
        config.deleteTempAfterUpload = !hasFlag("--keep-temp");

        std::string tempDir = getOption("--temp-dir");
        if (!tempDir.empty()) {
            config.tempDirectory = tempDir;
        }

        pipeline.setConfig(config);

        // Set progress callback
        pipeline.setProgressCallback([](const MegaCustom::DistributionProgress& progress) {
            std::cout << "\r[" << progress.membersProcessed << "/" << progress.totalMembers << " members] "
                      << progress.phase << ": " << progress.currentMember
                      << " (" << std::fixed << std::setprecision(1) << progress.overallPercent << "%)"
                      << "          " << std::flush;
        });

        std::cout << "Starting distribution...\n";
        std::cout << "  Files: " << sourceFiles.size() << "\n";
        std::cout << "  Members: " << (memberIds.empty() ? "all with folders" : std::to_string(memberIds.size())) << "\n";
        std::cout << "  Mode: " << modeOpt << "\n\n";

        auto result = pipeline.distribute(sourceFiles, memberIds);

        std::cout << "\n\n";
        std::cout << std::string(60, '=') << "\n";
        std::cout << "Distribution " << (result.success ? "Complete" : "Finished with errors") << "\n";
        std::cout << std::string(60, '=') << "\n\n";

        std::cout << "Results:\n";
        std::cout << "  Members: " << result.membersCompleted << " completed, "
                  << result.membersFailed << " failed, "
                  << result.membersSkipped << " skipped\n";
        std::cout << "  Files: " << result.filesUploaded << " uploaded, "
                  << result.filesFailed << " failed\n";

        int64_t durationMs = result.endTime - result.startTime;
        std::cout << "  Time: " << (durationMs / 1000) << "s\n";

        if (!result.errors.empty()) {
            std::cout << "\nErrors:\n";
            for (const auto& e : result.errors) {
                std::cout << "  ! " << e << "\n";
            }
        }

        // Show per-member summary
        std::cout << "\nPer-member results:\n";
        for (const auto& m : result.memberResults) {
            std::string status;
            switch (m.state) {
                case MegaCustom::MemberDistributionStatus::State::Completed:
                    status = "✓ Complete";
                    break;
                case MegaCustom::MemberDistributionStatus::State::Failed:
                    status = "✗ Failed";
                    break;
                case MegaCustom::MemberDistributionStatus::State::Skipped:
                    status = "- Skipped";
                    break;
                default:
                    status = "? Unknown";
            }
            std::cout << "  " << m.memberId << " (" << m.memberName << "): " << status;
            if (!m.lastError.empty()) {
                std::cout << " - " << m.lastError;
            }
            std::cout << "\n";
        }

        return result.success ? 0 : 1;
    }

    std::cerr << "Unknown distribute command: " << cmd << "\n";
    std::cerr << "Use 'megacustom distribute --help' for usage information.\n";
    return 1;
}

/**
 * Handle WordPress sync command
 */
int handleWordPress(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "--help") {
        std::cout << "WordPress Sync Commands:\n";
        std::cout << "  config                                   Configure WordPress connection\n";
        std::cout << "  test                                     Test WordPress connection\n";
        std::cout << "  sync                                     Sync all users from WordPress\n";
        std::cout << "  sync --id <wp-user-id>                   Sync specific user by ID\n";
        std::cout << "  sync --email <email>                     Sync specific user by email\n";
        std::cout << "  sync --role <role>                       Sync users with specific role\n";
        std::cout << "  preview                                  Preview sync without changes\n";
        std::cout << "  fields                                   Show available WordPress fields\n";
        std::cout << "  info                                     Show WordPress site info\n";
        std::cout << "\nConfig Options:\n";
        std::cout << "  --url <site-url>                         WordPress site URL\n";
        std::cout << "  --user <username>                        WordPress username\n";
        std::cout << "  --password <app-password>                Application password (not user password)\n";
        std::cout << "\nSync Options:\n";
        std::cout << "  --no-create                              Don't create new members\n";
        std::cout << "  --no-update                              Don't update existing members\n";
        std::cout << "\nExamples:\n";
        std::cout << "  megacustom wp config --url https://example.com --user admin --password xxxx-xxxx-xxxx\n";
        std::cout << "  megacustom wp test\n";
        std::cout << "  megacustom wp sync\n";
        std::cout << "  megacustom wp sync --id 42\n";
        std::cout << "  megacustom wp sync --role subscriber\n";
        std::cout << "  megacustom wp preview\n";
        return 0;
    }

    const std::string& cmd = args[0];

    // Helper to parse --option value pairs
    auto getOption = [&args](const std::string& opt) -> std::string {
        for (size_t i = 0; i < args.size() - 1; ++i) {
            if (args[i] == opt) {
                return args[i + 1];
            }
        }
        return "";
    };

    auto hasFlag = [&args](const std::string& flag) -> bool {
        for (const auto& arg : args) {
            if (arg == flag) return true;
        }
        return false;
    };

    MegaCustom::WordPressSync wp;

    // Try to load existing config
    wp.loadConfig();

    // ============ CONFIG ============
    if (cmd == "config") {
        std::string url = getOption("--url");
        std::string user = getOption("--user");
        std::string password = getOption("--password");

        if (url.empty() && user.empty() && password.empty()) {
            // Show current config
            auto config = wp.getConfig();
            std::cout << "WordPress Configuration:\n";
            std::cout << "  Site URL: " << (config.siteUrl.empty() ? "(not set)" : config.siteUrl) << "\n";
            std::cout << "  Username: " << (config.username.empty() ? "(not set)" : config.username) << "\n";
            std::cout << "  Password: " << (config.applicationPassword.empty() ? "(not set)" : "********") << "\n";
            return 0;
        }

        auto config = wp.getConfig();
        if (!url.empty()) config.siteUrl = url;
        if (!user.empty()) config.username = user;
        if (!password.empty()) config.applicationPassword = password;

        wp.setConfig(config);

        if (wp.saveConfig()) {
            std::cout << "✓ WordPress configuration saved\n";

            // Test connection
            std::string error;
            if (wp.testConnection(error)) {
                std::cout << "✓ Connection test successful\n";
            } else {
                std::cout << "! Connection test failed: " << error << "\n";
            }
            return 0;
        } else {
            std::cerr << "✗ Failed to save configuration\n";
            return 1;
        }
    }

    // ============ TEST ============
    if (cmd == "test") {
        auto config = wp.getConfig();
        if (config.siteUrl.empty()) {
            std::cerr << "Error: WordPress not configured. Run 'megacustom wp config' first.\n";
            return 1;
        }

        std::cout << "Testing connection to " << config.siteUrl << "...\n";

        std::string error;
        if (wp.testConnection(error)) {
            std::cout << "✓ Connection successful!\n";
            std::cout << "  Authenticated as: " << config.username << "\n";
            return 0;
        } else {
            std::cerr << "✗ Connection failed: " << error << "\n";
            return 1;
        }
    }

    // ============ INFO ============
    if (cmd == "info") {
        std::string error;
        auto info = wp.getSiteInfo(error);

        if (!error.empty()) {
            std::cerr << "Error: " << error << "\n";
            return 1;
        }

        std::cout << "WordPress Site Info:\n";
        for (const auto& [key, value] : info) {
            if (!value.empty()) {
                std::cout << "  " << key << ": " << value << "\n";
            }
        }
        return 0;
    }

    // ============ FIELDS ============
    if (cmd == "fields") {
        std::string error;
        auto fields = wp.getAvailableFields(error);

        if (!error.empty()) {
            std::cerr << "Error: " << error << "\n";
            return 1;
        }

        std::cout << "Available WordPress user fields:\n";
        for (const auto& field : fields) {
            std::cout << "  - " << field << "\n";
        }

        std::cout << "\nSupported member fields for mapping:\n";
        for (const auto& field : MegaCustom::WordPressSync::getSupportedMemberFields()) {
            std::cout << "  - " << field << "\n";
        }

        return 0;
    }

    // ============ PREVIEW ============
    if (cmd == "preview") {
        std::cout << "Preview: Checking what would be synced...\n\n";

        auto result = wp.previewSync();

        if (!result.error.empty()) {
            std::cerr << "Error: " << result.error << "\n";
            return 1;
        }

        std::cout << "WordPress users found: " << result.totalUsers << "\n\n";

        int wouldCreate = 0, wouldUpdate = 0;
        for (const auto& r : result.results) {
            std::string name = r.wpData.count("name") ? r.wpData.at("name") : "(unknown)";
            std::cout << "  " << r.memberId << " (" << name << "): ";

            if (r.action == "would_create") {
                std::cout << "would be CREATED\n";
                wouldCreate++;
            } else if (r.action == "would_update") {
                std::cout << "would be UPDATED\n";
                wouldUpdate++;
            }
        }

        std::cout << "\nSummary:\n";
        std::cout << "  Would create: " << wouldCreate << " new members\n";
        std::cout << "  Would update: " << wouldUpdate << " existing members\n";
        return 0;
    }

    // ============ SYNC ============
    if (cmd == "sync") {
        auto config = wp.getConfig();
        if (config.siteUrl.empty()) {
            std::cerr << "Error: WordPress not configured. Run 'megacustom wp config' first.\n";
            return 1;
        }

        // Apply sync options
        if (hasFlag("--no-create")) {
            config.createNewMembers = false;
        }
        if (hasFlag("--no-update")) {
            config.updateExisting = false;
        }
        wp.setConfig(config);

        // Set progress callback
        wp.setProgressCallback([](const MegaCustom::WpSyncProgress& progress) {
            std::cout << "\r[" << progress.currentUser << "/" << progress.totalUsers << "] "
                      << progress.status << ": " << progress.currentUsername
                      << " (" << std::fixed << std::setprecision(1) << progress.percentComplete << "%)"
                      << "          " << std::flush;
        });

        MegaCustom::SyncResult result;

        // Check for specific user options
        std::string userId = getOption("--id");
        std::string email = getOption("--email");
        std::string role = getOption("--role");

        if (!userId.empty()) {
            std::cout << "Syncing WordPress user ID: " << userId << "\n";
            result = wp.syncUser(userId);
        } else if (!email.empty()) {
            std::cout << "Syncing WordPress user by email: " << email << "\n";
            result = wp.syncUserByEmail(email);
        } else if (!role.empty()) {
            std::cout << "Syncing WordPress users with role: " << role << "\n";
            result = wp.syncByRole(role);
        } else {
            std::cout << "Syncing all WordPress users...\n";
            result = wp.syncAll();
        }

        std::cout << "\n\n";

        if (!result.error.empty() && result.totalUsers == 0) {
            std::cerr << "Error: " << result.error << "\n";
            return 1;
        }

        std::cout << "Sync " << (result.success ? "Complete" : "Finished with errors") << "\n";
        std::cout << std::string(50, '-') << "\n";
        std::cout << "Total users: " << result.totalUsers << "\n";
        std::cout << "  Created: " << result.usersCreated << "\n";
        std::cout << "  Updated: " << result.usersUpdated << "\n";
        std::cout << "  Skipped: " << result.usersSkipped << "\n";
        std::cout << "  Failed:  " << result.usersFailed << "\n";

        int64_t durationMs = result.syncEndTime - result.syncStartTime;
        std::cout << "Time: " << (durationMs / 1000.0) << "s\n";

        // Show any errors
        for (const auto& r : result.results) {
            if (r.action == "error" && !r.error.empty()) {
                std::cerr << "  ! " << r.memberId << ": " << r.error << "\n";
            }
        }

        return result.success ? 0 : 1;
    }

    std::cerr << "Unknown wp command: " << cmd << "\n";
    std::cerr << "Use 'megacustom wp --help' for usage information.\n";
    return 1;
}

/**
 * Handle log command - view and manage activity logs
 */
int handleLog(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "--help") {
        std::cout << "Log Commands:\n";
        std::cout << "  show [count]                           Show recent log entries (default: 50)\n";
        std::cout << "  errors [count]                         Show error entries only\n";
        std::cout << "  search <query>                         Search log entries\n";
        std::cout << "  member <member-id>                     Show logs for specific member\n";
        std::cout << "  stats                                  Show log statistics\n";
        std::cout << "  history [count]                        Show distribution history\n";
        std::cout << "  history --member <id>                  Show history for specific member\n";
        std::cout << "  export <output-file>                   Export logs to file\n";
        std::cout << "  clear                                  Clear all logs (use with caution)\n";
        std::cout << "\nOptions:\n";
        std::cout << "  --level <level>                        Filter by level (debug/info/warn/error)\n";
        std::cout << "  --category <cat>                       Filter by category\n";
        std::cout << "\nCategories: general, auth, upload, download, sync, watermark,\n";
        std::cout << "            distribution, member, wordpress, folder, system\n";
        std::cout << "\nExamples:\n";
        std::cout << "  megacustom log show 100\n";
        std::cout << "  megacustom log errors\n";
        std::cout << "  megacustom log search \"upload failed\"\n";
        std::cout << "  megacustom log member EGB001\n";
        std::cout << "  megacustom log history --member EGB001\n";
        return 0;
    }

    const std::string& cmd = args[0];
    auto& logManager = MegaCustom::LogManager::instance();

    // Helper to parse --option value pairs
    auto getOption = [&args](const std::string& opt) -> std::string {
        for (size_t i = 0; i < args.size() - 1; ++i) {
            if (args[i] == opt) {
                return args[i + 1];
            }
        }
        return "";
    };

    // ============ SHOW ============
    if (cmd == "show") {
        int count = 50;
        if (args.size() > 1 && !args[1].empty() && isdigit(args[1][0])) {
            try {
                count = std::stoi(args[1]);
            } catch (const std::exception&) {
                // Keep default
            }
        }

        // Parse filter options
        MegaCustom::LogFilter filter;
        std::string levelStr = getOption("--level");
        std::string categoryStr = getOption("--category");

        if (!levelStr.empty()) {
            filter.minLevel = MegaCustom::LogManager::stringToLevel(levelStr);
        }
        if (!categoryStr.empty()) {
            filter.categories.push_back(MegaCustom::LogManager::stringToCategory(categoryStr));
        }
        filter.limit = count;

        auto entries = logManager.getEntries(filter);

        if (entries.empty()) {
            std::cout << "No log entries found.\n";
            return 0;
        }

        std::cout << "Recent Log Entries (" << entries.size() << "):\n";
        std::cout << std::string(80, '-') << "\n";

        for (const auto& entry : entries) {
            std::cout << entry.toString() << "\n";
        }

        return 0;
    }

    // ============ ERRORS ============
    if (cmd == "errors") {
        int count = 50;
        if (args.size() > 1 && !args[1].empty() && isdigit(args[1][0])) {
            try {
                count = std::stoi(args[1]);
            } catch (const std::exception&) {
                // Keep default
            }
        }

        auto entries = logManager.getErrors(count);

        if (entries.empty()) {
            std::cout << "No error entries found.\n";
            return 0;
        }

        std::cout << "Error Log Entries (" << entries.size() << "):\n";
        std::cout << std::string(80, '-') << "\n";

        for (const auto& entry : entries) {
            std::cout << entry.toString() << "\n";
        }

        return 0;
    }

    // ============ SEARCH ============
    if (cmd == "search") {
        if (args.size() < 2) {
            std::cerr << "Error: Search query required\n";
            return 1;
        }

        std::string query = args[1];
        auto entries = logManager.search(query);

        if (entries.empty()) {
            std::cout << "No entries matching '" << query << "'\n";
            return 0;
        }

        std::cout << "Search Results for '" << query << "' (" << entries.size() << "):\n";
        std::cout << std::string(80, '-') << "\n";

        for (const auto& entry : entries) {
            std::cout << entry.toString() << "\n";
        }

        return 0;
    }

    // ============ MEMBER ============
    if (cmd == "member") {
        if (args.size() < 2) {
            std::cerr << "Error: Member ID required\n";
            return 1;
        }

        std::string memberId = args[1];
        auto entries = logManager.getMemberLog(memberId);

        if (entries.empty()) {
            std::cout << "No log entries for member: " << memberId << "\n";
            return 0;
        }

        std::cout << "Log Entries for Member " << memberId << " (" << entries.size() << "):\n";
        std::cout << std::string(80, '-') << "\n";

        for (const auto& entry : entries) {
            std::cout << entry.toString() << "\n";
        }

        return 0;
    }

    // ============ STATS ============
    if (cmd == "stats") {
        auto stats = logManager.getStats();

        std::cout << "Log Statistics:\n";
        std::cout << std::string(50, '-') << "\n";
        std::cout << "  Total entries: " << stats.totalEntries << "\n";
        std::cout << "  Errors: " << stats.errorCount << "\n";
        std::cout << "  Warnings: " << stats.warningCount << "\n";

        if (stats.oldestEntry > 0) {
            std::cout << "  Oldest: " << MegaCustom::LogManager::formatTimestamp(stats.oldestEntry) << "\n";
            std::cout << "  Newest: " << MegaCustom::LogManager::formatTimestamp(stats.newestEntry) << "\n";
        }

        std::cout << "\nDistribution Statistics:\n";
        std::cout << std::string(50, '-') << "\n";
        std::cout << "  Total distributions: " << stats.totalDistributions << "\n";
        std::cout << "  Successful: " << stats.successfulDistributions << "\n";
        std::cout << "  Failed: " << stats.failedDistributions << "\n";

        if (stats.totalBytesDistributed > 0) {
            double gb = stats.totalBytesDistributed / (1024.0 * 1024.0 * 1024.0);
            std::cout << "  Total data: " << std::fixed << std::setprecision(2) << gb << " GB\n";
        }

        return 0;
    }

    // ============ HISTORY ============
    if (cmd == "history") {
        int count = 50;
        std::string memberId = getOption("--member");

        if (args.size() > 1 && !args[1].empty() && isdigit(args[1][0])) {
            try {
                count = std::stoi(args[1]);
            } catch (const std::exception&) {
                // Keep default
            }
        }

        auto records = logManager.getDistributionHistory(memberId, count);

        if (records.empty()) {
            std::cout << "No distribution history found.\n";
            return 0;
        }

        std::cout << "Distribution History";
        if (!memberId.empty()) {
            std::cout << " for " << memberId;
        }
        std::cout << " (" << records.size() << " records):\n";
        std::cout << std::string(100, '-') << "\n";

        std::cout << std::left
                  << std::setw(20) << "Timestamp"
                  << std::setw(12) << "Member"
                  << std::setw(30) << "File"
                  << std::setw(12) << "Status"
                  << "\n";
        std::cout << std::string(100, '-') << "\n";

        for (const auto& record : records) {
            std::string statusStr;
            switch (record.status) {
                case MegaCustom::DistributionRecord::Status::Pending:
                    statusStr = "Pending";
                    break;
                case MegaCustom::DistributionRecord::Status::Watermarking:
                    statusStr = "Watermarking";
                    break;
                case MegaCustom::DistributionRecord::Status::Uploading:
                    statusStr = "Uploading";
                    break;
                case MegaCustom::DistributionRecord::Status::Completed:
                    statusStr = "Completed";
                    break;
                case MegaCustom::DistributionRecord::Status::Failed:
                    statusStr = "Failed";
                    break;
            }

            // Extract filename from path
            std::string filename = record.sourceFile;
            size_t lastSlash = filename.find_last_of('/');
            if (lastSlash != std::string::npos) {
                filename = filename.substr(lastSlash + 1);
            }
            if (filename.length() > 28) {
                filename = filename.substr(0, 25) + "...";
            }

            std::cout << std::left
                      << std::setw(20) << MegaCustom::LogManager::formatTimestamp(record.timestamp).substr(0, 19)
                      << std::setw(12) << record.memberId
                      << std::setw(30) << filename
                      << std::setw(12) << statusStr
                      << "\n";

            if (!record.errorMessage.empty()) {
                std::cout << "    Error: " << record.errorMessage << "\n";
            }
        }

        return 0;
    }

    // ============ EXPORT ============
    if (cmd == "export") {
        if (args.size() < 2) {
            std::cerr << "Error: Output file path required\n";
            return 1;
        }

        std::string outputPath = args[1];

        MegaCustom::LogFilter filter;
        filter.limit = 10000;  // Export up to 10k entries

        if (logManager.exportLogs(outputPath, filter)) {
            std::cout << "Logs exported to: " << outputPath << "\n";
            return 0;
        } else {
            std::cerr << "Failed to export logs\n";
            return 1;
        }
    }

    // ============ CLEAR ============
    if (cmd == "clear") {
        std::cout << "This will clear ALL log entries. Are you sure? (yes/no): ";
        std::string confirm;
        std::cin >> confirm;

        if (confirm == "yes") {
            logManager.clearAll();
            std::cout << "All logs cleared.\n";
            return 0;
        } else {
            std::cout << "Cancelled.\n";
            return 0;
        }
    }

    std::cerr << "Unknown log command: " << cmd << "\n";
    std::cerr << "Use 'megacustom log --help' for usage information.\n";
    return 1;
}

/**
 * Handle config command
 */
int handleConfig(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "--help") {
        std::cout << "Configuration Commands:\n";
        std::cout << "  show            Show current configuration\n";
        std::cout << "  set <key> <val> Set configuration value\n";
        std::cout << "  get <key>       Get configuration value\n";
        std::cout << "  profile list    List available profiles\n";
        std::cout << "  profile load    Load a profile\n";
        std::cout << "  profile save    Save current config as profile\n";
        std::cout << "  profile delete  Delete a profile\n";
        std::cout << "  reset           Reset to default configuration\n";
        std::cout << "\nExamples:\n";
        std::cout << "  megacustom config show\n";
        std::cout << "  megacustom config set transfer.maxConcurrent 8\n";
        std::cout << "  megacustom config get transfer.maxConcurrent\n";
        std::cout << "  megacustom config profile list\n";
        std::cout << "  megacustom config profile save work \"Work settings\"\n";
        std::cout << "  megacustom config profile load work\n";
        return 0;
    }

    MegaCustom::ConfigManager& config = MegaCustom::ConfigManager::getInstance();
    std::string cmd = args[0];

    // Get config directory path
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : "/tmp";
    }
    std::string configFile = std::string(home) + "/.megacustom/config.json";

    // Load existing config
    config.loadConfig(configFile);

    // ============ SHOW ============
    if (cmd == "show") {
        std::cout << "Current Configuration:\n";
        std::cout << "======================\n\n";

        // Show structured configuration
        auto authCfg = config.getAuthConfig();
        std::cout << "[Authentication]\n";
        std::cout << "  Session file:    " << authCfg.sessionFile << "\n";
        std::cout << "  2FA enabled:     " << (authCfg.use2FA ? "yes" : "no") << "\n";
        std::cout << "  Auto login:      " << (authCfg.autoLogin ? "yes" : "no") << "\n";
        std::cout << "  Session timeout: " << authCfg.sessionTimeout << " minutes\n\n";

        auto transferCfg = config.getTransferConfig();
        std::cout << "[Transfer]\n";
        std::cout << "  Max concurrent:  " << transferCfg.maxConcurrent << "\n";
        std::cout << "  Chunk size:      " << transferCfg.chunkSize << " bytes\n";
        std::cout << "  Bandwidth limit: " << (transferCfg.bandwidthLimit == 0 ? "unlimited" : std::to_string(transferCfg.bandwidthLimit) + " KB/s") << "\n";
        std::cout << "  Retry attempts:  " << transferCfg.retryAttempts << "\n\n";

        auto syncCfg = config.getSyncConfig();
        std::cout << "[Sync]\n";
        std::cout << "  Default direction:    " << syncCfg.defaultDirection << "\n";
        std::cout << "  Conflict resolution:  " << syncCfg.conflictResolution << "\n";
        std::cout << "  Create backups:       " << (syncCfg.createBackups ? "yes" : "no") << "\n";
        std::cout << "  Max backup versions:  " << syncCfg.maxBackupVersions << "\n";
        std::cout << "  Sync interval:        " << syncCfg.syncInterval << " minutes\n\n";

        auto renameCfg = config.getRenameConfig();
        std::cout << "[Rename]\n";
        std::cout << "  Safe mode:          " << (renameCfg.safeMode ? "yes" : "no") << "\n";
        std::cout << "  Preserve extension: " << (renameCfg.preserveExtension ? "yes" : "no") << "\n";
        std::cout << "  Max undo history:   " << renameCfg.maxUndoHistory << "\n\n";

        auto uiCfg = config.getUIConfig();
        std::cout << "[UI]\n";
        std::cout << "  Theme:          " << uiCfg.theme << "\n";
        std::cout << "  Language:       " << uiCfg.language << "\n";
        std::cout << "  Show progress:  " << (uiCfg.showProgressBar ? "yes" : "no") << "\n";
        std::cout << "  Confirm danger: " << (uiCfg.confirmDangerousOps ? "yes" : "no") << "\n";
    }
    // ============ GET ============
    else if (cmd == "get") {
        if (args.size() < 2) {
            std::cout << "Usage: megacustom config get <key>\n";
            std::cout << "Example keys: transfer.maxConcurrent, sync.createBackups\n";
            return 1;
        }

        std::string key = args[1];

        if (config.hasKey(key)) {
            // Try different types
            if (key.find(".max") != std::string::npos ||
                key.find("Concurrent") != std::string::npos ||
                key.find("Size") != std::string::npos ||
                key.find("Limit") != std::string::npos ||
                key.find("Attempts") != std::string::npos ||
                key.find("Delay") != std::string::npos ||
                key.find("Timeout") != std::string::npos ||
                key.find("Interval") != std::string::npos ||
                key.find("Versions") != std::string::npos ||
                key.find("History") != std::string::npos ||
                key.find("Level") != std::string::npos) {
                std::cout << key << " = " << config.getInt(key) << "\n";
            } else if (key.find("enabled") != std::string::npos ||
                       key.find("Enabled") != std::string::npos ||
                       key.find("create") != std::string::npos ||
                       key.find("preserve") != std::string::npos ||
                       key.find("safe") != std::string::npos ||
                       key.find("show") != std::string::npos ||
                       key.find("confirm") != std::string::npos ||
                       key.find("use2FA") != std::string::npos ||
                       key.find("auto") != std::string::npos) {
                std::cout << key << " = " << (config.getBool(key) ? "true" : "false") << "\n";
            } else {
                std::cout << key << " = " << config.getString(key) << "\n";
            }
        } else {
            std::cout << "Key not found: " << key << "\n";
            return 1;
        }
    }
    // ============ SET ============
    else if (cmd == "set") {
        if (args.size() < 3) {
            std::cout << "Usage: megacustom config set <key> <value>\n";
            return 1;
        }

        std::string key = args[1];
        std::string value = args[2];

        // Determine type and set
        if (value == "true" || value == "false" ||
            value == "yes" || value == "no") {
            bool boolVal = (value == "true" || value == "yes");
            config.setBool(key, boolVal);
            std::cout << "Set " << key << " = " << (boolVal ? "true" : "false") << "\n";
        } else {
            // Try to parse as integer
            try {
                int intVal = std::stoi(value);
                config.setInt(key, intVal);
                std::cout << "Set " << key << " = " << intVal << "\n";
            } catch (...) {
                // Set as string
                config.setString(key, value);
                std::cout << "Set " << key << " = " << value << "\n";
            }
        }

        // Save the config
        config.saveConfig(configFile);
    }
    // ============ PROFILE ============
    else if (cmd == "profile") {
        if (args.size() < 2 || args[1] == "--help") {
            std::cout << "Profile Commands:\n";
            std::cout << "  list                   List available profiles\n";
            std::cout << "  load <name>            Load a profile\n";
            std::cout << "  save <name> [desc]     Save current config as profile\n";
            std::cout << "  delete <name>          Delete a profile\n";
            return 0;
        }

        std::string subCmd = args[1];

        // ============ PROFILE LIST ============
        if (subCmd == "list") {
            auto profiles = config.listProfiles();
            if (profiles.empty()) {
                std::cout << "No configuration profiles found.\n";
                std::cout << "Use 'megacustom config profile save <name>' to create one.\n";
            } else {
                std::cout << "Available Profiles:\n";
                std::cout << "==================\n";
                for (const auto& name : profiles) {
                    std::cout << "  - " << name << "\n";
                }
            }
        }
        // ============ PROFILE LOAD ============
        else if (subCmd == "load") {
            if (args.size() < 3) {
                std::cout << "Usage: megacustom config profile load <name>\n";
                return 1;
            }

            std::string profileName = args[2];
            if (config.loadProfile(profileName)) {
                std::cout << "Profile '" << profileName << "' loaded successfully.\n";
                config.saveConfig(configFile);
            } else {
                std::cout << "Failed to load profile '" << profileName << "'.\n";
                std::cout << "Use 'megacustom config profile list' to see available profiles.\n";
                return 1;
            }
        }
        // ============ PROFILE SAVE ============
        else if (subCmd == "save") {
            if (args.size() < 3) {
                std::cout << "Usage: megacustom config profile save <name> [description]\n";
                return 1;
            }

            std::string profileName = args[2];
            std::string description = "";
            if (args.size() > 3) {
                // Concatenate remaining args as description
                for (size_t i = 3; i < args.size(); i++) {
                    if (!description.empty()) description += " ";
                    description += args[i];
                }
            }

            if (config.saveProfile(profileName, description)) {
                std::cout << "Profile '" << profileName << "' saved successfully.\n";
            } else {
                std::cout << "Failed to save profile '" << profileName << "'.\n";
                return 1;
            }
        }
        // ============ PROFILE DELETE ============
        else if (subCmd == "delete") {
            if (args.size() < 3) {
                std::cout << "Usage: megacustom config profile delete <name>\n";
                return 1;
            }

            std::string profileName = args[2];
            if (config.deleteProfile(profileName)) {
                std::cout << "Profile '" << profileName << "' deleted.\n";
            } else {
                std::cout << "Failed to delete profile '" << profileName << "'.\n";
                return 1;
            }
        }
        else {
            std::cout << "Unknown profile command: " << subCmd << "\n";
            std::cout << "Use 'megacustom config profile --help' for usage.\n";
            return 1;
        }
    }
    // ============ RESET ============
    else if (cmd == "reset") {
        std::cout << "Resetting configuration to defaults...\n";
        config.resetToDefaults();
        config.saveConfig(configFile);
        std::cout << "Configuration reset to defaults.\n";
    }
    else {
        std::cout << "Unknown config command: " << cmd << "\n";
        std::cout << "Use 'megacustom config --help' for usage.\n";
        return 1;
    }

    return 0;
}

/**
 * Main entry point
 */
int main(int argc, char* argv[]) {
    // Check if any arguments provided
    if (argc < 2) {
        printHeader();
        printUsage(argv[0]);
        return 1;
    }

    // Parse command
    std::string command = argv[1];
    std::vector<std::string> args;
    for (int i = 2; i < argc; i++) {
        args.push_back(argv[i]);
    }

    // Handle commands
    if (command == "help" || command == "--help" || command == "-h") {
        printHeader();
        printUsage(argv[0]);
        return 0;
    }
    else if (command == "version" || command == "--version" || command == "-v") {
        printVersion();
        return 0;
    }
    else if (command == "auth") {
        return handleAuth(args);
    }
    else if (command == "upload") {
        return handleUpload(args);
    }
    else if (command == "download") {
        return handleDownload(args);
    }
    else if (command == "multiupload") {
        return handleMultiUpload(args);
    }
    else if (command == "sync") {
        return handleSync(args);
    }
    else if (command == "map") {
        return handleMap(args);
    }
    else if (command == "rename") {
        return handleRename(args);
    }
    else if (command == "folder") {
        return handleFolder(args);
    }
    else if (command == "config") {
        return handleConfig(args);
    }
    else if (command == "member") {
        return handleMember(args);
    }
    else if (command == "watermark") {
        return handleWatermark(args);
    }
    else if (command == "distribute") {
        return handleDistribute(args);
    }
    else if (command == "wp" || command == "wordpress") {
        return handleWordPress(args);
    }
    else if (command == "log") {
        return handleLog(args);
    }
    else {
        std::cerr << "Error: Unknown command '" << command << "'\n";
        std::cerr << "Use '" << argv[0] << " help' for usage information.\n";
        return 1;
    }

    return 0;
}