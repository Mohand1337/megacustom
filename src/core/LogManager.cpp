#include "core/LogManager.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

namespace MegaCustom {

// ==================== JSON Escaping Helper ====================

static std::string escapeJson(const std::string& str) {
    std::stringstream ss;
    for (char c : str) {
        switch (c) {
            case '"':  ss << "\\\""; break;
            case '\\': ss << "\\\\"; break;
            case '\b': ss << "\\b"; break;
            case '\f': ss << "\\f"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    ss << "\\u" << std::hex << std::setfill('0') << std::setw(4) << static_cast<int>(c);
                } else {
                    ss << c;
                }
        }
    }
    return ss.str();
}

// ==================== LogEntry ====================

std::string LogEntry::toJson() const {
    std::stringstream ss;
    ss << "{";
    ss << "\"timestamp\":" << timestamp << ",";
    ss << "\"level\":\"" << LogManager::levelToString(level) << "\",";
    ss << "\"category\":\"" << LogManager::categoryToString(category) << "\",";
    ss << "\"action\":\"" << escapeJson(action) << "\",";
    ss << "\"message\":\"" << escapeJson(message) << "\"";
    if (!details.empty()) ss << ",\"details\":\"" << escapeJson(details) << "\"";
    if (!memberId.empty()) ss << ",\"memberId\":\"" << escapeJson(memberId) << "\"";
    if (!filePath.empty()) ss << ",\"filePath\":\"" << escapeJson(filePath) << "\"";
    if (!jobId.empty()) ss << ",\"jobId\":\"" << escapeJson(jobId) << "\"";
    ss << "}";
    return ss.str();
}

LogEntry LogEntry::fromJson(const std::string& json) {
    LogEntry entry;

    auto getValue = [&json](const std::string& key) -> std::string {
        std::string searchKey = "\"" + key + "\":";
        size_t pos = json.find(searchKey);
        if (pos == std::string::npos) return "";

        pos += searchKey.length();
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '"')) pos++;

        if (pos >= json.size()) return "";

        // Check if it's a number
        if (isdigit(json[pos]) || json[pos] == '-') {
            size_t end = pos;
            while (end < json.size() && (isdigit(json[end]) || json[end] == '-' || json[end] == '.')) {
                end++;
            }
            return json.substr(pos, end - pos);
        }

        // It's a string
        size_t end = json.find('"', pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    };

    std::string tsStr = getValue("timestamp");
    if (!tsStr.empty()) {
        try {
            entry.timestamp = std::stoll(tsStr);
        } catch (const std::exception&) {
            entry.timestamp = 0;
        }
    }

    entry.level = LogManager::stringToLevel(getValue("level"));
    entry.category = LogManager::stringToCategory(getValue("category"));
    entry.action = getValue("action");
    entry.message = getValue("message");
    entry.details = getValue("details");
    entry.memberId = getValue("memberId");
    entry.filePath = getValue("filePath");
    entry.jobId = getValue("jobId");

    return entry;
}

std::string LogEntry::toString() const {
    std::stringstream ss;
    ss << LogManager::formatTimestamp(timestamp) << " ";
    ss << "[" << LogManager::levelToString(level) << "] ";
    ss << "[" << LogManager::categoryToString(category) << "] ";
    ss << action << ": " << message;
    if (!memberId.empty()) ss << " (member: " << memberId << ")";
    if (!filePath.empty()) ss << " (file: " << filePath << ")";
    return ss.str();
}

// ==================== DistributionRecord ====================

std::string DistributionRecord::toJson() const {
    std::stringstream ss;
    ss << "{";
    ss << "\"timestamp\":" << timestamp << ",";
    ss << "\"jobId\":\"" << escapeJson(jobId) << "\",";
    ss << "\"memberId\":\"" << escapeJson(memberId) << "\",";
    ss << "\"memberName\":\"" << escapeJson(memberName) << "\",";
    ss << "\"sourceFile\":\"" << escapeJson(sourceFile) << "\",";
    ss << "\"outputFile\":\"" << escapeJson(outputFile) << "\",";
    ss << "\"megaFolder\":\"" << escapeJson(megaFolder) << "\",";
    ss << "\"status\":" << static_cast<int>(status) << ",";
    ss << "\"watermarkTimeMs\":" << watermarkTimeMs << ",";
    ss << "\"uploadTimeMs\":" << uploadTimeMs << ",";
    ss << "\"fileSizeBytes\":" << fileSizeBytes;
    if (!megaLink.empty()) ss << ",\"megaLink\":\"" << escapeJson(megaLink) << "\"";
    if (!errorMessage.empty()) ss << ",\"errorMessage\":\"" << escapeJson(errorMessage) << "\"";
    ss << "}";
    return ss.str();
}

DistributionRecord DistributionRecord::fromJson(const std::string& json) {
    DistributionRecord record;

    auto getValue = [&json](const std::string& key) -> std::string {
        std::string searchKey = "\"" + key + "\":";
        size_t pos = json.find(searchKey);
        if (pos == std::string::npos) return "";

        pos += searchKey.length();
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '"')) pos++;

        if (pos >= json.size()) return "";

        if (isdigit(json[pos]) || json[pos] == '-') {
            size_t end = pos;
            while (end < json.size() && (isdigit(json[end]) || json[end] == '-' || json[end] == '.')) {
                end++;
            }
            return json.substr(pos, end - pos);
        }

        size_t end = json.find('"', pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    };

    try {
        std::string tsStr = getValue("timestamp");
        if (!tsStr.empty()) record.timestamp = std::stoll(tsStr);

        record.jobId = getValue("jobId");
        record.memberId = getValue("memberId");
        record.memberName = getValue("memberName");
        record.sourceFile = getValue("sourceFile");
        record.outputFile = getValue("outputFile");
        record.megaFolder = getValue("megaFolder");
        record.megaLink = getValue("megaLink");
        record.errorMessage = getValue("errorMessage");

        std::string statusStr = getValue("status");
        if (!statusStr.empty()) {
            int statusVal = std::stoi(statusStr);
            // Validate enum range (0-4 for Status enum)
            if (statusVal >= 0 && statusVal <= 4) {
                record.status = static_cast<DistributionRecord::Status>(statusVal);
            }
        }

        std::string wmTime = getValue("watermarkTimeMs");
        if (!wmTime.empty()) record.watermarkTimeMs = std::stoll(wmTime);

        std::string upTime = getValue("uploadTimeMs");
        if (!upTime.empty()) record.uploadTimeMs = std::stoll(upTime);

        std::string sizeStr = getValue("fileSizeBytes");
        if (!sizeStr.empty()) record.fileSizeBytes = std::stoll(sizeStr);
    } catch (const std::exception&) {
        // Return partially filled record on parse error
    }

    return record;
}

// ==================== LogManager Singleton ====================

LogManager& LogManager::instance() {
    static LogManager instance;
    return instance;
}

LogManager::LogManager() {
    // Default log directory
    const char* home = getenv("HOME");
    if (home) {
        m_logDir = std::string(home) + "/.megacustom/logs";
    } else {
        m_logDir = "./logs";
    }

    // Initialize flush timer
    m_lastFlushTime = std::chrono::steady_clock::now();

    ensureLogDirectory();
    openLogFiles();
    loadDistributionHistory();
}

LogManager::~LogManager() {
    flush();
    if (m_activityLog.is_open()) m_activityLog.close();
    if (m_errorLog.is_open()) m_errorLog.close();
}

// ==================== Configuration ====================

void LogManager::setLogDirectory(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_logDir = path;
    ensureLogDirectory();
    openLogFiles();
}

void LogManager::ensureLogDirectory() {
    int result = mkdir(m_logDir.c_str(), 0755);
    if (result != 0 && errno != EEXIST) {
        std::cerr << "LogManager: Failed to create log directory '" << m_logDir
                  << "': " << strerror(errno) << std::endl;
    }
}

std::string LogManager::getCurrentDateString() const {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    struct tm tm_now;
    localtime_r(&time_t_now, &tm_now);  // Thread-safe version

    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d",
             tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday);
    return buffer;
}

std::string LogManager::getActivityLogPath() const {
    return m_logDir + "/activity_" + getCurrentDateString() + ".log";
}

std::string LogManager::getErrorLogPath() const {
    return m_logDir + "/errors.log";
}

std::string LogManager::getDistributionLogPath() const {
    return m_logDir + "/distribution_history.json";
}

void LogManager::openLogFiles() {
    std::string currentDate = getCurrentDateString();

    // Check if we need to rotate (new day)
    if (m_currentLogDate != currentDate) {
        if (m_activityLog.is_open()) m_activityLog.close();
        m_currentLogDate = currentDate;
    }

    if (!m_activityLog.is_open()) {
        std::string activityPath = getActivityLogPath();
        m_activityLog.open(activityPath, std::ios::app);
        if (!m_activityLog.is_open()) {
            std::cerr << "LogManager: Failed to open activity log file: " << activityPath << std::endl;
        }
    }

    if (!m_errorLog.is_open()) {
        std::string errorPath = getErrorLogPath();
        m_errorLog.open(errorPath, std::ios::app);
        if (!m_errorLog.is_open()) {
            std::cerr << "LogManager: Failed to open error log file: " << errorPath << std::endl;
        }
    }
}

// ==================== Static Utilities ====================

int64_t LogManager::currentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string LogManager::formatTimestamp(int64_t timestamp) {
    time_t seconds = timestamp / 1000;
    int millis = timestamp % 1000;
    struct tm tm_info;
    localtime_r(&seconds, &tm_info);  // Thread-safe version

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
             tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec, millis);
    return buffer;
}

std::string LogManager::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error: return "ERROR";
        default: return "UNKNOWN";
    }
}

LogLevel LogManager::stringToLevel(const std::string& str) {
    if (str == "DEBUG") return LogLevel::Debug;
    if (str == "INFO") return LogLevel::Info;
    if (str == "WARN" || str == "WARNING") return LogLevel::Warning;
    if (str == "ERROR") return LogLevel::Error;
    return LogLevel::Info;
}

std::string LogManager::categoryToString(LogCategory cat) {
    switch (cat) {
        case LogCategory::General: return "GENERAL";
        case LogCategory::Auth: return "AUTH";
        case LogCategory::Upload: return "UPLOAD";
        case LogCategory::Download: return "DOWNLOAD";
        case LogCategory::Sync: return "SYNC";
        case LogCategory::Watermark: return "WATERMARK";
        case LogCategory::Distribution: return "DISTRIBUTION";
        case LogCategory::Member: return "MEMBER";
        case LogCategory::WordPress: return "WORDPRESS";
        case LogCategory::Folder: return "FOLDER";
        case LogCategory::System: return "SYSTEM";
        default: return "UNKNOWN";
    }
}

LogCategory LogManager::stringToCategory(const std::string& str) {
    if (str == "GENERAL") return LogCategory::General;
    if (str == "AUTH") return LogCategory::Auth;
    if (str == "UPLOAD") return LogCategory::Upload;
    if (str == "DOWNLOAD") return LogCategory::Download;
    if (str == "SYNC") return LogCategory::Sync;
    if (str == "WATERMARK") return LogCategory::Watermark;
    if (str == "DISTRIBUTION") return LogCategory::Distribution;
    if (str == "MEMBER") return LogCategory::Member;
    if (str == "WORDPRESS") return LogCategory::WordPress;
    if (str == "FOLDER") return LogCategory::Folder;
    if (str == "SYSTEM") return LogCategory::System;
    return LogCategory::General;
}

// ==================== Logging Methods ====================

void LogManager::log(LogLevel level, LogCategory category,
                     const std::string& action, const std::string& message,
                     const std::string& details) {
    logWithContext(level, category, action, message, "", "", "");
}

void LogManager::logWithContext(LogLevel level, LogCategory category,
                                const std::string& action, const std::string& message,
                                const std::string& memberId,
                                const std::string& filePath,
                                const std::string& jobId) {
    if (level < m_minLevel) return;

    LogEntry entry;
    entry.timestamp = currentTimeMs();
    entry.level = level;
    entry.category = category;
    entry.action = action;
    entry.message = message;
    entry.memberId = memberId;
    entry.filePath = filePath;
    entry.jobId = jobId;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Add to cache (deque for O(1) removal from front)
        m_recentEntries.push_back(entry);
        if (m_recentEntries.size() > MAX_CACHED_ENTRIES) {
            m_recentEntries.pop_front();  // O(1) instead of O(n)
        }

        // Write to file
        writeToFile(entry);

        // Console output
        if (m_consoleOutput) {
            std::cout << entry.toString() << std::endl;
        }
    }

    // Callback (outside lock)
    if (m_logCallback) {
        m_logCallback(entry);
    }
}

void LogManager::writeToFile(const LogEntry& entry) {
    // Check for date rotation
    std::string currentDate = getCurrentDateString();
    if (m_currentLogDate != currentDate) {
        flushWriteBuffer();  // Flush before rotation
        openLogFiles();
    }

    // Add to write buffer
    std::string logLine = entry.toString();
    m_writeBuffer.push_back(logLine);

    // Errors always go to error log immediately
    if (entry.level == LogLevel::Error && m_errorLog.is_open()) {
        m_errorLog << logLine << "\n";
        m_errorLog.flush();
    }

    // Check if we should flush the buffer
    auto now = std::chrono::steady_clock::now();
    bool shouldFlush = (m_writeBuffer.size() >= WRITE_BUFFER_SIZE) ||
                       ((now - m_lastFlushTime) >= FLUSH_INTERVAL) ||
                       (entry.level == LogLevel::Error);  // Flush on errors

    if (shouldFlush) {
        flushWriteBuffer();
    }
}

void LogManager::flushWriteBuffer() {
    if (m_writeBuffer.empty()) return;

    if (m_activityLog.is_open()) {
        for (const auto& line : m_writeBuffer) {
            m_activityLog << line << "\n";
        }
        m_activityLog.flush();
    }

    m_writeBuffer.clear();
    m_lastFlushTime = std::chrono::steady_clock::now();
}

// Convenience methods
void LogManager::debug(LogCategory cat, const std::string& action, const std::string& msg) {
    log(LogLevel::Debug, cat, action, msg);
}

void LogManager::info(LogCategory cat, const std::string& action, const std::string& msg) {
    log(LogLevel::Info, cat, action, msg);
}

void LogManager::warning(LogCategory cat, const std::string& action, const std::string& msg) {
    log(LogLevel::Warning, cat, action, msg);
}

void LogManager::error(LogCategory cat, const std::string& action, const std::string& msg) {
    log(LogLevel::Error, cat, action, msg);
}

// Category-specific methods
void LogManager::logUpload(const std::string& action, const std::string& msg,
                           const std::string& filePath) {
    logWithContext(LogLevel::Info, LogCategory::Upload, action, msg, "", filePath, "");
}

void LogManager::logDownload(const std::string& action, const std::string& msg,
                             const std::string& filePath) {
    logWithContext(LogLevel::Info, LogCategory::Download, action, msg, "", filePath, "");
}

void LogManager::logWatermark(const std::string& action, const std::string& msg,
                              const std::string& filePath, const std::string& memberId) {
    logWithContext(LogLevel::Info, LogCategory::Watermark, action, msg, memberId, filePath, "");
}

void LogManager::logDistribution(const std::string& action, const std::string& msg,
                                 const std::string& jobId, const std::string& memberId) {
    logWithContext(LogLevel::Info, LogCategory::Distribution, action, msg, memberId, "", jobId);
}

void LogManager::logMember(const std::string& action, const std::string& msg,
                           const std::string& memberId) {
    logWithContext(LogLevel::Info, LogCategory::Member, action, msg, memberId, "", "");
}

void LogManager::logWordPress(const std::string& action, const std::string& msg) {
    log(LogLevel::Info, LogCategory::WordPress, action, msg);
}

void LogManager::logAuth(const std::string& action, const std::string& msg) {
    log(LogLevel::Info, LogCategory::Auth, action, msg);
}

void LogManager::logError(const std::string& action, const std::string& msg,
                          const std::string& details) {
    log(LogLevel::Error, LogCategory::General, action, msg, details);
}

// ==================== Distribution History ====================

void LogManager::recordDistribution(const DistributionRecord& record) {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_distributionHistory.push_back(record);

    // Keep only recent history in memory
    if (m_distributionHistory.size() > 10000) {
        m_distributionHistory.erase(m_distributionHistory.begin(),
                                     m_distributionHistory.begin() + 1000);
    }

    writeDistributionRecord(record);
}

void LogManager::updateDistributionStatus(const std::string& jobId,
                                           const std::string& memberId,
                                           DistributionRecord::Status status,
                                           const std::string& error) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto& record : m_distributionHistory) {
        if (record.jobId == jobId && record.memberId == memberId) {
            record.status = status;
            if (!error.empty()) {
                record.errorMessage = error;
            }
            break;
        }
    }

    saveDistributionHistory();
}

void LogManager::writeDistributionRecord(const DistributionRecord& record) {
    std::ofstream file(getDistributionLogPath(), std::ios::app);
    if (file.is_open()) {
        file << record.toJson() << "\n";
    }
}

void LogManager::loadDistributionHistory() {
    std::ifstream file(getDistributionLogPath());
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line[0] == '{') {
            m_distributionHistory.push_back(DistributionRecord::fromJson(line));
        }
    }

    // Keep only recent
    if (m_distributionHistory.size() > 10000) {
        m_distributionHistory.erase(m_distributionHistory.begin(),
                                     m_distributionHistory.end() - 10000);
    }
}

void LogManager::saveDistributionHistory() {
    std::ofstream file(getDistributionLogPath());
    if (!file.is_open()) return;

    for (const auto& record : m_distributionHistory) {
        file << record.toJson() << "\n";
    }
}

std::vector<DistributionRecord> LogManager::getDistributionHistory(
    const std::string& memberId, int limit, int64_t startTime, int64_t endTime) {

    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<DistributionRecord> results;

    for (auto it = m_distributionHistory.rbegin();
         it != m_distributionHistory.rend() && static_cast<int>(results.size()) < limit;
         ++it) {

        // Apply filters
        if (!memberId.empty() && it->memberId != memberId) continue;
        if (startTime > 0 && it->timestamp < startTime) continue;
        if (endTime > 0 && it->timestamp > endTime) continue;

        results.push_back(*it);
    }

    return results;
}

std::vector<DistributionRecord> LogManager::getDistributionsByJob(const std::string& jobId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<DistributionRecord> results;

    for (const auto& record : m_distributionHistory) {
        if (record.jobId == jobId) {
            results.push_back(record);
        }
    }

    return results;
}

// ==================== Query Methods ====================

std::vector<LogEntry> LogManager::getEntries(const LogFilter& filter) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<LogEntry> results;

    int skipped = 0;
    for (auto it = m_recentEntries.rbegin();
         it != m_recentEntries.rend() && static_cast<int>(results.size()) < filter.limit;
         ++it) {

        // Apply filters
        if (it->level < filter.minLevel) continue;

        if (!filter.categories.empty()) {
            bool found = false;
            for (auto cat : filter.categories) {
                if (it->category == cat) { found = true; break; }
            }
            if (!found) continue;
        }

        if (!filter.searchText.empty()) {
            if (it->message.find(filter.searchText) == std::string::npos &&
                it->action.find(filter.searchText) == std::string::npos) {
                continue;
            }
        }

        if (!filter.memberId.empty() && it->memberId != filter.memberId) continue;
        if (!filter.jobId.empty() && it->jobId != filter.jobId) continue;
        if (filter.startTime > 0 && it->timestamp < filter.startTime) continue;
        if (filter.endTime > 0 && it->timestamp > filter.endTime) continue;

        // Handle pagination
        if (skipped < filter.offset) {
            skipped++;
            continue;
        }

        results.push_back(*it);
    }

    return results;
}

std::vector<LogEntry> LogManager::getRecentEntries(int count) {
    LogFilter filter;
    filter.limit = count;
    return getEntries(filter);
}

std::vector<LogEntry> LogManager::getMemberLog(const std::string& memberId, int limit) {
    LogFilter filter;
    filter.memberId = memberId;
    filter.limit = limit;
    return getEntries(filter);
}

std::vector<LogEntry> LogManager::getErrors(int limit) {
    LogFilter filter;
    filter.minLevel = LogLevel::Error;
    filter.limit = limit;
    return getEntries(filter);
}

std::vector<LogEntry> LogManager::search(const std::string& query, int limit) {
    LogFilter filter;
    filter.searchText = query;
    filter.limit = limit;
    return getEntries(filter);
}

LogStats LogManager::getStats() {
    std::lock_guard<std::mutex> lock(m_mutex);
    LogStats stats;

    stats.totalEntries = static_cast<int>(m_recentEntries.size());

    for (const auto& entry : m_recentEntries) {
        if (entry.level == LogLevel::Error) stats.errorCount++;
        if (entry.level == LogLevel::Warning) stats.warningCount++;

        if (stats.oldestEntry == 0 || entry.timestamp < stats.oldestEntry) {
            stats.oldestEntry = entry.timestamp;
        }
        if (entry.timestamp > stats.newestEntry) {
            stats.newestEntry = entry.timestamp;
        }
    }

    // Distribution stats
    stats.totalDistributions = static_cast<int>(m_distributionHistory.size());
    for (const auto& record : m_distributionHistory) {
        if (record.status == DistributionRecord::Status::Completed) {
            stats.successfulDistributions++;
            stats.totalBytesDistributed += record.fileSizeBytes;
        } else if (record.status == DistributionRecord::Status::Failed) {
            stats.failedDistributions++;
        }
    }

    return stats;
}

// ==================== Maintenance ====================

void LogManager::rotateLogs() {
    std::lock_guard<std::mutex> lock(m_mutex);
    openLogFiles();
}

void LogManager::cleanOldLogs() {
    // Calculate cutoff date
    auto now = std::chrono::system_clock::now();
    auto cutoff = now - std::chrono::hours(24 * m_retentionDays);
    auto cutoff_time = std::chrono::system_clock::to_time_t(cutoff);

    DIR* dir = opendir(m_logDir.c_str());
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        if (filename.find("activity_") == 0 && filename.find(".log") != std::string::npos) {
            std::string filepath = m_logDir + "/" + filename;
            struct stat st;
            if (stat(filepath.c_str(), &st) == 0) {
                if (st.st_mtime < cutoff_time) {
                    unlink(filepath.c_str());
                }
            }
        }
    }
    closedir(dir);
}

bool LogManager::exportLogs(const std::string& outputPath, const LogFilter& filter) {
    auto entries = getEntries(filter);

    std::ofstream file(outputPath);
    if (!file.is_open()) {
        std::cerr << "LogManager: Failed to open export file: " << outputPath << std::endl;
        return false;
    }

    for (const auto& entry : entries) {
        file << entry.toString() << "\n";
        if (file.fail()) {
            std::cerr << "LogManager: Write error during export to: " << outputPath << std::endl;
            return false;
        }
    }

    file.flush();
    if (file.fail()) {
        std::cerr << "LogManager: Failed to flush export file: " << outputPath << std::endl;
        return false;
    }

    return true;
}

void LogManager::clearAll() {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_recentEntries.clear();
    m_distributionHistory.clear();

    // Close and delete files
    if (m_activityLog.is_open()) m_activityLog.close();
    if (m_errorLog.is_open()) m_errorLog.close();

    // Reopen fresh
    openLogFiles();
}

void LogManager::flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    flushWriteBuffer();  // Flush buffered writes first
    if (m_activityLog.is_open()) m_activityLog.flush();
    if (m_errorLog.is_open()) m_errorLog.flush();
    saveDistributionHistory();
}

} // namespace MegaCustom
