#include "features/DistributionPipeline.h"
#include "features/Watermarker.h"
#include "integrations/MemberDatabase.h"
#include "core/LogManager.h"
#include "core/PathValidator.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <future>
#include <algorithm>
#include <filesystem>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <random>

namespace fs = std::filesystem;

namespace MegaCustom {

// ==================== Constructor/Destructor ====================

DistributionPipeline::DistributionPipeline() {
    // Set default temp directory
    m_config.tempDirectory = getDefaultTempDirectory();

    // Default member database path
    const char* home = getenv("HOME");
    if (home) {
        m_memberDbPath = std::string(home) + "/.megacustom/members.json";
    }
}

DistributionPipeline::~DistributionPipeline() {
    // Ensure any ongoing operation is cancelled
    m_cancelled = true;
}

// ==================== Static Utilities ====================

std::string DistributionPipeline::generateJobId() {
    // Generate unique job ID: dist_YYYYMMDD_HHMMSS_XXXX
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    struct tm* tm_now = localtime(&time_t_now);

    // Random suffix
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "dist_%04d%02d%02d_%02d%02d%02d_%04d",
             tm_now->tm_year + 1900, tm_now->tm_mon + 1, tm_now->tm_mday,
             tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec,
             dis(gen));

    return buffer;
}

std::string DistributionPipeline::getDefaultTempDirectory() {
    const char* tmpdir = getenv("TMPDIR");
    if (tmpdir) return std::string(tmpdir) + "/megacustom_dist";

    tmpdir = getenv("TMP");
    if (tmpdir) return std::string(tmpdir) + "/megacustom_dist";

    return "/tmp/megacustom_dist";
}

int64_t DistributionPipeline::currentTimeMs() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

// ==================== Validation ====================

std::map<std::string, std::string> DistributionPipeline::validateSourceFiles(
    const std::vector<std::string>& sourceFiles) {

    std::map<std::string, std::string> results;

    for (const auto& file : sourceFiles) {
        // Check if file exists
        struct stat st;
        if (stat(file.c_str(), &st) != 0) {
            results[file] = "File not found";
            continue;
        }

        // Check if it's a regular file
        if (!S_ISREG(st.st_mode)) {
            results[file] = "Not a regular file";
            continue;
        }

        // Check if it's a supported type
        if (!Watermarker::isVideoFile(file) && !Watermarker::isPdfFile(file)) {
            results[file] = "Unsupported file type (must be video or PDF)";
            continue;
        }

        // File is valid
        results[file] = "";
    }

    return results;
}

// ==================== Member Loading ====================

bool DistributionPipeline::loadMembers(
    std::vector<std::string>& memberIds,
    std::map<std::string, std::string>& memberNames,
    std::map<std::string, std::string>& memberFolders) {

    // Constructor loads from file automatically
    MemberDatabase db(m_memberDbPath);

    auto allMembersResult = db.getAllMembers();
    if (!allMembersResult.success) {
        return false;
    }

    // If no specific members requested, use all with folders
    if (memberIds.empty()) {
        for (const auto& member : allMembersResult.members) {
            if (!member.megaFolderPath.empty()) {
                memberIds.push_back(member.id);
            }
        }
    }

    // Build lookup maps
    for (const auto& member : allMembersResult.members) {
        memberNames[member.id] = member.name;
        memberFolders[member.id] = member.megaFolderPath;
    }

    return true;
}

std::vector<std::string> DistributionPipeline::getMembersWithFolders() {
    std::vector<std::string> result;

    // Constructor loads from file automatically
    MemberDatabase db(m_memberDbPath);
    auto membersResult = db.getAllMembers();
    if (membersResult.success) {
        for (const auto& m : membersResult.members) {
            if (!m.megaFolderPath.empty()) {
                result.push_back(m.id);
            }
        }
    }

    return result;
}

// ==================== Progress Reporting ====================

void DistributionPipeline::reportProgress(
    const std::string& phase,
    const std::string& member,
    const std::string& file,
    const std::string& operation,
    int membersProcessed,
    int totalMembers,
    int filesProcessed,
    int totalFiles) {

    std::lock_guard<std::mutex> lock(m_progressMutex);

    m_currentProgress.phase = phase;
    m_currentProgress.currentMember = member;
    m_currentProgress.currentFile = file;
    m_currentProgress.currentOperation = operation;
    m_currentProgress.membersProcessed = membersProcessed;
    m_currentProgress.totalMembers = totalMembers;
    m_currentProgress.filesProcessed = filesProcessed;
    m_currentProgress.totalFiles = totalFiles;
    m_currentProgress.overallPercent = calculateOverallProgress(
        membersProcessed, totalMembers, filesProcessed, totalFiles);

    m_currentProgress.elapsedMs = currentTimeMs() - m_startTime;

    // Estimate remaining time
    if (m_currentProgress.overallPercent > 0) {
        double remaining = (100.0 - m_currentProgress.overallPercent) /
                          m_currentProgress.overallPercent *
                          m_currentProgress.elapsedMs;
        m_currentProgress.estimatedRemainingMs = static_cast<int64_t>(remaining);
    }

    if (m_progressCallback) {
        m_progressCallback(m_currentProgress);
    }
}

double DistributionPipeline::calculateOverallProgress(
    int membersProcessed, int totalMembers,
    int filesProcessed, int totalFiles) {

    if (totalMembers == 0 || totalFiles == 0) return 0.0;

    // Weight: 70% watermarking, 30% uploading
    // Each file goes through watermark + upload
    int totalOps = totalFiles * 2;  // watermark + upload per file
    int completedOps = filesProcessed;  // Simplified for now

    return (static_cast<double>(completedOps) / totalOps) * 100.0;
}

bool DistributionPipeline::checkPauseCancel() {
    while (m_paused && !m_cancelled) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return !m_cancelled;
}

// ==================== Core Operations ====================

bool DistributionPipeline::watermarkForMember(
    const std::string& sourceFile,
    const std::string& memberId,
    std::string& outputPath,
    std::string& error) {

    Watermarker watermarker;

    // Configure watermarker based on our config
    WatermarkConfig wmConfig = watermarker.getConfig();

    // Set temp output directory
    std::string tempDir = m_config.tempDirectory + "/" + memberId;

    // Create temp directory if needed (safe, no shell injection)
    if (!megacustom::PathValidator::isValidPath(tempDir)) {
        error = "Invalid temp directory path";
        return false;
    }
    try {
        fs::create_directories(tempDir);
    } catch (const fs::filesystem_error& e) {
        error = "Failed to create temp directory: " + std::string(e.what());
        return false;
    }

    // Generate output filename
    outputPath = watermarker.generateOutputPath(sourceFile, tempDir);

    // Watermark based on mode
    WatermarkResult result;

    switch (m_config.watermarkMode) {
        case DistributionConfig::WatermarkMode::None:
            // No watermarking, just copy to temp (safe, no shell injection)
            {
                if (!megacustom::PathValidator::isValidPath(sourceFile) ||
                    !megacustom::PathValidator::isValidPath(outputPath)) {
                    result.success = false;
                    result.error = "Invalid file path";
                } else {
                    try {
                        fs::copy_file(sourceFile, outputPath, fs::copy_options::overwrite_existing);
                        result.success = true;
                        result.outputFile = outputPath;
                    } catch (const fs::filesystem_error& e) {
                        result.success = false;
                        result.error = "Failed to copy file: " + std::string(e.what());
                    }
                }
            }
            break;

        case DistributionConfig::WatermarkMode::Global:
            // Use global watermark text
            wmConfig.primaryText = m_config.globalPrimaryText;
            wmConfig.secondaryText = m_config.globalSecondaryText;
            watermarker.setConfig(wmConfig);
            result = watermarker.watermarkFile(sourceFile, outputPath);
            break;

        case DistributionConfig::WatermarkMode::PerMember:
            // Use member-specific watermark
            result = watermarker.watermarkVideoForMember(sourceFile, memberId, tempDir);
            if (result.success) {
                outputPath = result.outputFile;
            }
            break;
    }

    if (!result.success) {
        error = result.error;
        return false;
    }

    outputPath = result.outputFile;
    return true;
}

bool DistributionPipeline::uploadToMegaFolder(
    const std::string& localPath,
    const std::string& megaFolder,
    std::string& error) {

    // Validate paths to prevent injection attacks
    if (!megacustom::PathValidator::isValidPath(localPath)) {
        error = "Invalid local path";
        return false;
    }
    if (!megacustom::PathValidator::isValidPath(megaFolder)) {
        error = "Invalid MEGA folder path";
        return false;
    }

    // Use execvp for safe command execution (no shell injection)
    // Fork and exec to capture output safely
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        error = "Failed to create pipe";
        return false;
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        error = "Failed to fork process";
        return false;
    }

    if (pid == 0) {
        // Child process
        close(pipefd[0]);  // Close read end
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        // Execute megacustom upload with explicit arguments (safe)
        execlp("./megacustom", "megacustom", "upload",
               localPath.c_str(), megaFolder.c_str(), nullptr);

        // If exec fails
        _exit(127);
    }

    // Parent process
    close(pipefd[1]);  // Close write end

    std::string output;
    char buffer[256];
    ssize_t bytesRead;
    while ((bytesRead = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytesRead] = '\0';
        output += buffer;
    }
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        error = "Upload failed: " + output;
        return false;
    }

    return true;
}

MemberDistributionStatus DistributionPipeline::processOneMember(
    const std::string& memberId,
    const std::string& memberName,
    const std::string& memberFolder,
    const std::vector<std::string>& sourceFiles) {

    MemberDistributionStatus status;
    status.memberId = memberId;
    status.memberName = memberName;
    status.destinationFolder = memberFolder;

    LogManager::instance().logDistribution("member_start",
        "Processing member " + memberName + " (" + memberId + ") → " + memberFolder,
        m_currentProgress.jobId, memberId);

    // Check if member has a folder binding
    if (memberFolder.empty()) {
        status.state = MemberDistributionStatus::State::Skipped;
        status.lastError = "No distribution folder bound";
        LogManager::instance().logDistribution("member_skipped",
            "Skipped member " + memberId + ": No distribution folder bound",
            m_currentProgress.jobId, memberId);
        return status;
    }

    // Initialize file status for each source file
    for (const auto& file : sourceFiles) {
        MemberDistributionStatus::FileStatus fs;
        fs.sourceFile = file;
        status.files.push_back(fs);
    }

    status.state = MemberDistributionStatus::State::Watermarking;

    // Process each file
    for (size_t i = 0; i < sourceFiles.size(); ++i) {
        if (!checkPauseCancel()) {
            status.state = MemberDistributionStatus::State::Failed;
            status.lastError = "Cancelled";
            return status;
        }

        const std::string& sourceFile = sourceFiles[i];
        auto& fileStatus = status.files[i];

        // Track watermark timing
        auto watermarkStart = std::chrono::steady_clock::now();
        int64_t watermarkTimeMs = 0;
        int64_t fileSizeBytes = 0;

        // Get file size
        struct stat st;
        if (stat(sourceFile.c_str(), &st) == 0) {
            fileSizeBytes = st.st_size;
        }

        // Watermark
        std::string outputPath, error;
        if (watermarkForMember(sourceFile, memberId, outputPath, error)) {
            fileStatus.watermarkedFile = outputPath;
            fileStatus.watermarkDone = true;
            status.filesWatermarked++;

            auto watermarkEnd = std::chrono::steady_clock::now();
            watermarkTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                watermarkEnd - watermarkStart).count();
        } else {
            fileStatus.error = error;
            status.filesFailed++;
            continue;  // Skip upload for this file
        }

        if (!checkPauseCancel()) {
            status.state = MemberDistributionStatus::State::Failed;
            status.lastError = "Cancelled";
            return status;
        }

        // Upload
        status.state = MemberDistributionStatus::State::Uploading;
        auto uploadStart = std::chrono::steady_clock::now();

        if (uploadToMegaFolder(outputPath, memberFolder, error)) {
            auto uploadEnd = std::chrono::steady_clock::now();
            int64_t uploadTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                uploadEnd - uploadStart).count();

            // Extract filename from outputPath
            size_t lastSlash = outputPath.find_last_of('/');
            std::string filename = (lastSlash != std::string::npos)
                ? outputPath.substr(lastSlash + 1)
                : outputPath;

            fileStatus.uploadedPath = memberFolder + "/" + filename;
            fileStatus.uploadDone = true;
            status.filesUploaded++;

            // Record distribution in history with timing data
            DistributionRecord record;
            record.timestamp = LogManager::currentTimeMs();
            record.jobId = m_currentProgress.jobId;
            record.memberId = memberId;
            record.memberName = memberName;
            record.sourceFile = sourceFile;
            record.outputFile = outputPath;
            record.megaFolder = memberFolder;
            record.status = DistributionRecord::Status::Completed;
            record.watermarkTimeMs = watermarkTimeMs;
            record.uploadTimeMs = uploadTimeMs;
            record.fileSizeBytes = fileSizeBytes;
            LogManager::instance().recordDistribution(record);

            // Clean up temp file if configured
            if (m_config.deleteTempAfterUpload) {
                unlink(outputPath.c_str());
            }
        } else {
            auto uploadEnd = std::chrono::steady_clock::now();
            int64_t uploadTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                uploadEnd - uploadStart).count();

            fileStatus.error = error;
            status.filesFailed++;

            // Record failed distribution with timing data
            DistributionRecord record;
            record.timestamp = LogManager::currentTimeMs();
            record.jobId = m_currentProgress.jobId;
            record.memberId = memberId;
            record.memberName = memberName;
            record.sourceFile = sourceFile;
            record.outputFile = outputPath;
            record.megaFolder = memberFolder;
            record.status = DistributionRecord::Status::Failed;
            record.errorMessage = error;
            record.watermarkTimeMs = watermarkTimeMs;
            record.uploadTimeMs = uploadTimeMs;
            record.fileSizeBytes = fileSizeBytes;
            LogManager::instance().recordDistribution(record);
        }
    }

    // Determine final state
    if (status.filesFailed == 0) {
        status.state = MemberDistributionStatus::State::Completed;
        LogManager::instance().logDistribution("member_complete",
            "Member " + memberId + " completed: " + std::to_string(status.filesUploaded) + " files uploaded",
            m_currentProgress.jobId, memberId);
    } else if (status.filesUploaded > 0) {
        status.state = MemberDistributionStatus::State::Completed;  // Partial success
        LogManager::instance().logDistribution("member_partial",
            "Member " + memberId + " partial: " + std::to_string(status.filesUploaded) + " uploaded, " +
            std::to_string(status.filesFailed) + " failed",
            m_currentProgress.jobId, memberId);
    } else {
        status.state = MemberDistributionStatus::State::Failed;
        LogManager::instance().logDistribution("member_failed",
            "Member " + memberId + " failed: " + status.lastError,
            m_currentProgress.jobId, memberId);
    }

    return status;
}

// ==================== Main Distribution Method ====================

DistributionResult DistributionPipeline::distribute(
    const std::vector<std::string>& sourceFiles,
    const std::vector<std::string>& memberIds) {

    DistributionResult result;
    result.jobId = generateJobId();
    result.startTime = currentTimeMs();
    result.sourceFiles = sourceFiles;

    m_startTime = result.startTime;
    m_cancelled = false;
    m_paused = false;
    m_currentProgress.jobId = result.jobId;
    m_currentProgress.errorCount = 0;

    LogManager::instance().logDistribution("distribution_start",
        "Starting distribution job " + result.jobId + " with " +
        std::to_string(sourceFiles.size()) + " files to " +
        std::to_string(memberIds.empty() ? 0 : memberIds.size()) + " members",
        result.jobId);

    // Validate source files
    auto validation = validateSourceFiles(sourceFiles);
    for (const auto& [file, error] : validation) {
        if (!error.empty()) {
            result.errors.push_back(file + ": " + error);
        }
    }

    if (!result.errors.empty()) {
        result.success = false;
        result.endTime = currentTimeMs();
        return result;
    }

    // Load members
    std::vector<std::string> targetMembers = memberIds;
    std::map<std::string, std::string> memberNames;
    std::map<std::string, std::string> memberFolders;

    if (!loadMembers(targetMembers, memberNames, memberFolders)) {
        result.errors.push_back("Failed to load member database");
        result.success = false;
        result.endTime = currentTimeMs();
        return result;
    }

    if (targetMembers.empty()) {
        result.errors.push_back("No members with distribution folders found");
        result.success = false;
        result.endTime = currentTimeMs();
        return result;
    }

    // Create temp directory (safe, no shell injection)
    if (!megacustom::PathValidator::isValidPath(m_config.tempDirectory)) {
        result.errors.push_back("Invalid temp directory path");
        result.success = false;
        result.endTime = currentTimeMs();
        return result;
    }
    try {
        fs::create_directories(m_config.tempDirectory);
    } catch (const fs::filesystem_error& e) {
        result.errors.push_back("Failed to create temp directory: " + std::string(e.what()));
        result.success = false;
        result.endTime = currentTimeMs();
        return result;
    }

    result.totalMembers = static_cast<int>(targetMembers.size());
    result.totalFiles = static_cast<int>(sourceFiles.size() * targetMembers.size());

    reportProgress("starting", "", "", "Initializing distribution...",
                   0, result.totalMembers, 0, result.totalFiles);

    // Process each member
    int memberIndex = 0;
    int filesProcessed = 0;

    for (const auto& memberId : targetMembers) {
        if (!checkPauseCancel()) {
            result.errors.push_back("Distribution cancelled");
            break;
        }

        std::string name = memberNames[memberId];
        std::string folder = memberFolders[memberId];

        reportProgress("processing", memberId, "", "Processing member " + name + "...",
                       memberIndex, result.totalMembers, filesProcessed, result.totalFiles);

        // Process this member
        auto memberStatus = processOneMember(memberId, name, folder, sourceFiles);

        // Collect temp files for tracking
        for (const auto& fs : memberStatus.files) {
            if (!fs.watermarkedFile.empty()) {
                result.tempFilesCreated.push_back(fs.watermarkedFile);
            }
        }

        // Update counts
        result.memberResults.push_back(memberStatus);

        switch (memberStatus.state) {
            case MemberDistributionStatus::State::Completed:
                result.membersCompleted++;
                break;
            case MemberDistributionStatus::State::Failed:
                result.membersFailed++;
                break;
            case MemberDistributionStatus::State::Skipped:
                result.membersSkipped++;
                break;
            default:
                break;
        }

        result.filesWatermarked += memberStatus.filesWatermarked;
        result.filesUploaded += memberStatus.filesUploaded;
        result.filesFailed += memberStatus.filesFailed;

        filesProcessed += static_cast<int>(sourceFiles.size());
        memberIndex++;

        // Continue despite errors if configured
        if (!m_config.resumeOnError && memberStatus.state == MemberDistributionStatus::State::Failed) {
            result.errors.push_back("Stopped due to error (resumeOnError=false)");
            break;
        }
    }

    result.endTime = currentTimeMs();
    result.success = (result.membersFailed == 0 && result.membersSkipped < result.totalMembers);

    reportProgress("complete", "", "", "Distribution completed",
                   result.totalMembers, result.totalMembers,
                   result.totalFiles, result.totalFiles);

    // Log distribution completion
    std::stringstream summary;
    summary << "Distribution job " << result.jobId << " complete: "
            << result.membersCompleted << " completed, "
            << result.membersFailed << " failed, "
            << result.membersSkipped << " skipped. "
            << "Files: " << result.filesWatermarked << " watermarked, "
            << result.filesUploaded << " uploaded";
    LogManager::instance().logDistribution("distribution_complete", summary.str(), result.jobId);

    return result;
}

DistributionResult DistributionPipeline::distributeToMember(
    const std::vector<std::string>& sourceFiles,
    const std::string& memberId) {

    return distribute(sourceFiles, {memberId});
}

DistributionResult DistributionPipeline::previewDistribution(
    const std::vector<std::string>& sourceFiles,
    const std::vector<std::string>& memberIds) {

    DistributionResult result;
    result.jobId = generateJobId() + "_preview";
    result.startTime = currentTimeMs();
    result.sourceFiles = sourceFiles;

    // Validate files
    auto validation = validateSourceFiles(sourceFiles);
    for (const auto& [file, error] : validation) {
        if (!error.empty()) {
            result.errors.push_back(file + ": " + error);
        }
    }

    // Load members
    std::vector<std::string> targetMembers = memberIds;
    std::map<std::string, std::string> memberNames;
    std::map<std::string, std::string> memberFolders;

    if (!loadMembers(targetMembers, memberNames, memberFolders)) {
        result.errors.push_back("Failed to load member database");
    }

    result.totalMembers = static_cast<int>(targetMembers.size());
    result.totalFiles = static_cast<int>(sourceFiles.size() * targetMembers.size());

    // Build preview status for each member
    for (const auto& memberId : targetMembers) {
        MemberDistributionStatus status;
        status.memberId = memberId;
        status.memberName = memberNames[memberId];
        status.destinationFolder = memberFolders[memberId];

        if (status.destinationFolder.empty()) {
            status.state = MemberDistributionStatus::State::Skipped;
            status.lastError = "No distribution folder bound";
            result.membersSkipped++;
        } else {
            status.state = MemberDistributionStatus::State::Pending;
            result.membersCompleted++;  // Would be processed

            for (const auto& file : sourceFiles) {
                MemberDistributionStatus::FileStatus fs;
                fs.sourceFile = file;

                // Generate expected output path
                Watermarker wm;
                std::string expectedOutput = wm.generateOutputPath(
                    file, m_config.tempDirectory + "/" + memberId);

                fs.watermarkedFile = expectedOutput;

                // Expected upload path
                size_t lastSlash = expectedOutput.find_last_of('/');
                std::string filename = (lastSlash != std::string::npos)
                    ? expectedOutput.substr(lastSlash + 1)
                    : expectedOutput;
                fs.uploadedPath = status.destinationFolder + "/" + filename;

                status.files.push_back(fs);
            }
        }

        result.memberResults.push_back(status);
    }

    result.endTime = currentTimeMs();
    result.success = result.errors.empty();

    return result;
}

DistributionResult DistributionPipeline::retryFailed(const DistributionResult& previousResult) {
    // Collect failed members
    std::vector<std::string> failedMembers;
    for (const auto& memberStatus : previousResult.memberResults) {
        if (memberStatus.state == MemberDistributionStatus::State::Failed) {
            failedMembers.push_back(memberStatus.memberId);
        }
    }

    if (failedMembers.empty()) {
        DistributionResult result;
        result.success = true;
        result.errors.push_back("No failed members to retry");
        return result;
    }

    return distribute(previousResult.sourceFiles, failedMembers);
}

int DistributionPipeline::cleanupTempFiles(const DistributionResult& result) {
    int deleted = 0;

    for (const auto& tempFile : result.tempFilesCreated) {
        if (unlink(tempFile.c_str()) == 0) {
            deleted++;
        }
    }

    // Also try to remove member subdirectories
    for (const auto& memberStatus : result.memberResults) {
        std::string memberTempDir = m_config.tempDirectory + "/" + memberStatus.memberId;
        rmdir(memberTempDir.c_str());  // Only removes if empty
    }

    return deleted;
}

} // namespace MegaCustom
