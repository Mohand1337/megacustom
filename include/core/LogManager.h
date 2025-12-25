#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <string>
#include <vector>
#include <deque>
#include <map>
#include <functional>
#include <mutex>
#include <fstream>
#include <cstdint>
#include <chrono>

namespace MegaCustom {

/**
 * Log levels
 */
enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

/**
 * Log categories for filtering
 */
enum class LogCategory {
    General,
    Auth,
    Upload,
    Download,
    Sync,
    Watermark,
    Distribution,
    Member,
    WordPress,
    Folder,
    System
};

/**
 * Single log entry
 */
struct LogEntry {
    int64_t timestamp = 0;        // Unix timestamp in milliseconds
    LogLevel level = LogLevel::Info;
    LogCategory category = LogCategory::General;
    std::string action;           // e.g., "upload_start", "watermark_complete"
    std::string message;          // Human-readable message
    std::string details;          // Additional details (JSON or text)

    // Optional context
    std::string memberId;         // Associated member (if any)
    std::string filePath;         // Associated file (if any)
    std::string jobId;            // Associated job ID (if any)

    // For serialization
    std::string toJson() const;
    static LogEntry fromJson(const std::string& json);
    std::string toString() const;
};

/**
 * Distribution record - tracks what was sent where
 */
struct DistributionRecord {
    int64_t timestamp = 0;
    std::string jobId;            // Distribution job ID
    std::string memberId;
    std::string memberName;
    std::string sourceFile;       // Original file path
    std::string outputFile;       // Watermarked file (temp)
    std::string megaFolder;       // Destination MEGA folder
    std::string megaLink;         // Optional: generated share link

    enum class Status {
        Pending,
        Watermarking,
        Uploading,
        Completed,
        Failed
    };
    Status status = Status::Pending;
    std::string errorMessage;

    int64_t watermarkTimeMs = 0;  // Time spent watermarking
    int64_t uploadTimeMs = 0;     // Time spent uploading
    int64_t fileSizeBytes = 0;    // File size

    std::string toJson() const;
    static DistributionRecord fromJson(const std::string& json);
};

/**
 * Log filter for queries
 */
struct LogFilter {
    LogLevel minLevel = LogLevel::Debug;
    std::vector<LogCategory> categories;  // Empty = all
    std::string searchText;
    std::string memberId;
    std::string jobId;
    int64_t startTime = 0;        // Filter by time range
    int64_t endTime = 0;
    int limit = 100;              // Max entries to return
    int offset = 0;               // For pagination
};

/**
 * Log statistics
 */
struct LogStats {
    int totalEntries = 0;
    int errorCount = 0;
    int warningCount = 0;
    int64_t oldestEntry = 0;
    int64_t newestEntry = 0;

    // Distribution stats
    int totalDistributions = 0;
    int successfulDistributions = 0;
    int failedDistributions = 0;
    int64_t totalBytesDistributed = 0;
};

/**
 * Callback for real-time log events
 */
using LogCallback = std::function<void(const LogEntry&)>;

/**
 * LogManager - Centralized logging for all MegaCustom operations
 *
 * Features:
 * - Multiple log levels (Debug, Info, Warning, Error)
 * - Category-based filtering
 * - Persistent file storage with rotation
 * - Distribution history tracking
 * - Real-time callbacks for GUI updates
 * - Search and filter capabilities
 */
class LogManager {
public:
    /**
     * Get singleton instance
     */
    static LogManager& instance();

    // Prevent copying
    LogManager(const LogManager&) = delete;
    LogManager& operator=(const LogManager&) = delete;

    // ==================== Configuration ====================

    /**
     * Set log directory
     * Default: ~/.megacustom/logs/
     */
    void setLogDirectory(const std::string& path);
    std::string getLogDirectory() const { return m_logDir; }

    /**
     * Set minimum log level for file output
     */
    void setMinLevel(LogLevel level) { m_minLevel = level; }
    LogLevel getMinLevel() const { return m_minLevel; }

    /**
     * Set log retention days (auto-delete older logs)
     * Default: 30 days
     */
    void setRetentionDays(int days) { m_retentionDays = days; }

    /**
     * Enable/disable console output
     */
    void setConsoleOutput(bool enabled) { m_consoleOutput = enabled; }

    /**
     * Set callback for real-time log events
     */
    void setLogCallback(LogCallback callback) { m_logCallback = callback; }

    // ==================== Logging Methods ====================

    /**
     * Log a message
     */
    void log(LogLevel level, LogCategory category,
             const std::string& action, const std::string& message,
             const std::string& details = "");

    /**
     * Log with context (member/file/job)
     */
    void logWithContext(LogLevel level, LogCategory category,
                        const std::string& action, const std::string& message,
                        const std::string& memberId = "",
                        const std::string& filePath = "",
                        const std::string& jobId = "");

    // Convenience methods
    void debug(LogCategory cat, const std::string& action, const std::string& msg);
    void info(LogCategory cat, const std::string& action, const std::string& msg);
    void warning(LogCategory cat, const std::string& action, const std::string& msg);
    void error(LogCategory cat, const std::string& action, const std::string& msg);

    // Category-specific convenience methods
    void logUpload(const std::string& action, const std::string& msg,
                   const std::string& filePath = "");
    void logDownload(const std::string& action, const std::string& msg,
                     const std::string& filePath = "");
    void logWatermark(const std::string& action, const std::string& msg,
                      const std::string& filePath = "", const std::string& memberId = "");
    void logDistribution(const std::string& action, const std::string& msg,
                         const std::string& jobId = "", const std::string& memberId = "");
    void logMember(const std::string& action, const std::string& msg,
                   const std::string& memberId = "");
    void logWordPress(const std::string& action, const std::string& msg);
    void logAuth(const std::string& action, const std::string& msg);
    void logError(const std::string& action, const std::string& msg,
                  const std::string& details = "");

    // ==================== Distribution History ====================

    /**
     * Record a distribution operation
     */
    void recordDistribution(const DistributionRecord& record);

    /**
     * Update distribution record status
     */
    void updateDistributionStatus(const std::string& jobId,
                                   const std::string& memberId,
                                   DistributionRecord::Status status,
                                   const std::string& error = "");

    /**
     * Get distribution history
     */
    std::vector<DistributionRecord> getDistributionHistory(
        const std::string& memberId = "",
        int limit = 100,
        int64_t startTime = 0,
        int64_t endTime = 0);

    /**
     * Get distribution records for a job
     */
    std::vector<DistributionRecord> getDistributionsByJob(const std::string& jobId);

    // ==================== Query Methods ====================

    /**
     * Get log entries with filter
     */
    std::vector<LogEntry> getEntries(const LogFilter& filter = {});

    /**
     * Get recent entries
     */
    std::vector<LogEntry> getRecentEntries(int count = 50);

    /**
     * Get entries for a specific member
     */
    std::vector<LogEntry> getMemberLog(const std::string& memberId, int limit = 100);

    /**
     * Get errors only
     */
    std::vector<LogEntry> getErrors(int limit = 100);

    /**
     * Search log entries
     */
    std::vector<LogEntry> search(const std::string& query, int limit = 100);

    /**
     * Get log statistics
     */
    LogStats getStats();

    // ==================== Maintenance ====================

    /**
     * Rotate log files (called automatically)
     */
    void rotateLogs();

    /**
     * Clean old log files based on retention policy
     */
    void cleanOldLogs();

    /**
     * Export logs to file
     */
    bool exportLogs(const std::string& outputPath, const LogFilter& filter = {});

    /**
     * Clear all logs (use with caution)
     */
    void clearAll();

    /**
     * Flush pending writes to disk
     */
    void flush();

    // ==================== Utilities ====================

    /**
     * Convert LogLevel to string
     */
    static std::string levelToString(LogLevel level);
    static LogLevel stringToLevel(const std::string& str);

    /**
     * Convert LogCategory to string
     */
    static std::string categoryToString(LogCategory cat);
    static LogCategory stringToCategory(const std::string& str);

    /**
     * Get current timestamp in milliseconds
     */
    static int64_t currentTimeMs();

    /**
     * Format timestamp as string
     */
    static std::string formatTimestamp(int64_t timestamp);

private:
    LogManager();
    ~LogManager();

    // Configuration
    std::string m_logDir;
    LogLevel m_minLevel = LogLevel::Info;
    int m_retentionDays = 30;
    bool m_consoleOutput = true;  // Enable console output by default
    LogCallback m_logCallback;

    // File handles
    std::ofstream m_activityLog;
    std::ofstream m_errorLog;
    std::string m_currentLogDate;

    // Thread safety
    std::mutex m_mutex;

    // In-memory cache for recent entries (deque for O(1) pop_front)
    std::deque<LogEntry> m_recentEntries;
    static const size_t MAX_CACHED_ENTRIES = 1000;

    // Write buffer for batched disk writes
    std::vector<std::string> m_writeBuffer;
    std::chrono::steady_clock::time_point m_lastFlushTime;
    static const size_t WRITE_BUFFER_SIZE = 100;
    static constexpr std::chrono::seconds FLUSH_INTERVAL{5};

    // Distribution history (in-memory + file)
    std::vector<DistributionRecord> m_distributionHistory;

    // Internal methods
    void ensureLogDirectory();
    void openLogFiles();
    void writeToFile(const LogEntry& entry);
    void flushWriteBuffer();
    void writeDistributionRecord(const DistributionRecord& record);
    void loadRecentEntries();
    void loadDistributionHistory();
    void saveDistributionHistory();
    std::string getActivityLogPath() const;
    std::string getErrorLogPath() const;
    std::string getDistributionLogPath() const;
    std::string getCurrentDateString() const;
};

// ==================== Macros for convenient logging ====================

#define LOG_DEBUG(cat, action, msg) \
    MegaCustom::LogManager::instance().debug(cat, action, msg)

#define LOG_INFO(cat, action, msg) \
    MegaCustom::LogManager::instance().info(cat, action, msg)

#define LOG_WARNING(cat, action, msg) \
    MegaCustom::LogManager::instance().warning(cat, action, msg)

#define LOG_ERROR(cat, action, msg) \
    MegaCustom::LogManager::instance().error(cat, action, msg)

} // namespace MegaCustom

#endif // LOG_MANAGER_H
