#ifndef MEGACUSTOM_CONSTANTS_H
#define MEGACUSTOM_CONSTANTS_H

#include <QString>

namespace MegaCustom {
namespace Constants {

// Application info
constexpr const char* APP_NAME = "MegaCustom";
constexpr const char* APP_VERSION = "1.0.0";
constexpr const char* APP_ORGANIZATION = "MegaCustom";
constexpr const char* APP_DOMAIN = "megacustom.app";

// MEGA SDK API Key (required for SDK initialization)
constexpr const char* MEGA_API_KEY = "9gETCbhB";

// Transfer defaults
constexpr qint64 DEFAULT_FILE_SIZE_ESTIMATE = 1024000;  // 1MB default for unknown size
constexpr int DEFAULT_MAX_CONCURRENT_TRANSFERS = 3;
constexpr int MAX_CONCURRENT_TRANSFERS_LIMIT = 10;

// Sync defaults
constexpr int DEFAULT_SYNC_INTERVAL_MINUTES = 60;
constexpr int MIN_SYNC_INTERVAL_MINUTES = 1;
constexpr int MAX_SYNC_INTERVAL_MINUTES = 1440;  // 24 hours

// Retry settings
constexpr int DEFAULT_MAX_RETRIES = 3;
constexpr int DEFAULT_RETRY_DELAY_SECONDS = 5;

// UI Theme colors
// NOTE: For programmatic color access, use ThemeManager::instance().color() or DesignTokens
// These string constants are kept for QSS compatibility and legacy code
namespace Colors {
    // MEGA brand colors - using original MEGA red for consistency with DesignTokens
    constexpr const char* MEGA_RED = "#D90007";         // Original MEGA red
    constexpr const char* MEGA_RED_HOVER = "#C00006";   // Hover state
    constexpr const char* MEGA_RED_PRESSED = "#A00005"; // Pressed state

    // Transfer status colors - aligned with DesignTokens
    constexpr const char* TRANSFER_FAILED = "#E31B57";     // support-error (light)
    constexpr const char* TRANSFER_COMPLETED = "#009B48";  // support-success (light)
    constexpr const char* TRANSFER_PAUSED = "#F7A308";     // support-warning (light)
    constexpr const char* TRANSFER_ACTIVE = "#05BAF1";     // support-info (light)
    constexpr const char* TRANSFER_PENDING = "#616366";    // text-secondary (light)

    // Status indicator colors - aligned with DesignTokens
    constexpr const char* STATUS_SUCCESS = "#009B48";  // support-success (light)
    constexpr const char* STATUS_WARNING = "#F7A308";  // support-warning (light)
    constexpr const char* STATUS_ERROR = "#E31B57";    // support-error (light)
}

// Timeout values (milliseconds)
constexpr int TRANSFER_TIMEOUT_MS = 300000;  // 5 minutes
constexpr int API_REQUEST_TIMEOUT_MS = 30000;  // 30 seconds
constexpr int UI_UPDATE_INTERVAL_MS = 500;

// File size thresholds
constexpr qint64 LARGE_FILE_THRESHOLD = 100 * 1024 * 1024;  // 100MB

} // namespace Constants
} // namespace MegaCustom

#endif // MEGACUSTOM_CONSTANTS_H
