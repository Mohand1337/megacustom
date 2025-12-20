#ifndef MEGA_CUSTOM_ERROR_H
#define MEGA_CUSTOM_ERROR_H

#include <string>
#include <exception>
#include <optional>

namespace MegaCustom {

/**
 * Error categories for MegaCustom operations
 */
enum class ErrorCategory {
    None = 0,          // No error
    Authentication,    // Login, session, 2FA errors
    Network,           // Connection, timeout errors
    FileSystem,        // Local file access errors
    CloudStorage,      // MEGA API/storage errors
    Transfer,          // Upload/download errors
    Validation,        // Input validation errors
    Configuration,     // Config file errors
    Permission,        // Access denied errors
    NotFound,          // Resource not found
    Conflict,          // Resource conflicts
    Quota,             // Storage/bandwidth quota exceeded
    Internal,          // Internal/unexpected errors
};

/**
 * Common error codes across MegaCustom
 */
enum class ErrorCode {
    // Success
    OK = 0,

    // Authentication (100-199)
    AUTH_INVALID_CREDENTIALS = 100,
    AUTH_SESSION_EXPIRED = 101,
    AUTH_2FA_REQUIRED = 102,
    AUTH_2FA_INVALID = 103,
    AUTH_NOT_LOGGED_IN = 104,
    AUTH_ACCOUNT_BLOCKED = 105,

    // Network (200-299)
    NETWORK_DISCONNECTED = 200,
    NETWORK_TIMEOUT = 201,
    NETWORK_SSL_ERROR = 202,
    NETWORK_DNS_FAILED = 203,

    // File System (300-399)
    FS_FILE_NOT_FOUND = 300,
    FS_DIRECTORY_NOT_FOUND = 301,
    FS_ACCESS_DENIED = 302,
    FS_DISK_FULL = 303,
    FS_FILE_EXISTS = 304,
    FS_INVALID_PATH = 305,
    FS_READ_ERROR = 306,
    FS_WRITE_ERROR = 307,

    // Cloud Storage (400-499)
    CLOUD_NODE_NOT_FOUND = 400,
    CLOUD_FOLDER_NOT_FOUND = 401,
    CLOUD_ACCESS_DENIED = 402,
    CLOUD_OVER_QUOTA = 403,
    CLOUD_BANDWIDTH_EXCEEDED = 404,
    CLOUD_INVALID_LINK = 405,
    CLOUD_LINK_EXPIRED = 406,
    CLOUD_FILE_TOO_LARGE = 407,

    // Transfer (500-599)
    TRANSFER_FAILED = 500,
    TRANSFER_CANCELLED = 501,
    TRANSFER_PAUSED = 502,
    TRANSFER_TIMEOUT = 503,
    TRANSFER_INCOMPLETE = 504,
    TRANSFER_CHECKSUM_MISMATCH = 505,

    // Validation (600-699)
    VALIDATION_INVALID_EMAIL = 600,
    VALIDATION_INVALID_PATH = 601,
    VALIDATION_INVALID_CONFIG = 602,
    VALIDATION_MISSING_FIELD = 603,
    VALIDATION_INVALID_FORMAT = 604,

    // Configuration (700-799)
    CONFIG_FILE_NOT_FOUND = 700,
    CONFIG_PARSE_ERROR = 701,
    CONFIG_INVALID_VALUE = 702,

    // Other (900-999)
    CANCELLED = 900,
    UNKNOWN_ERROR = 999,
};

/**
 * Get human-readable category name
 */
inline const char* getCategoryName(ErrorCategory category) {
    switch (category) {
        case ErrorCategory::None: return "None";
        case ErrorCategory::Authentication: return "Authentication";
        case ErrorCategory::Network: return "Network";
        case ErrorCategory::FileSystem: return "FileSystem";
        case ErrorCategory::CloudStorage: return "CloudStorage";
        case ErrorCategory::Transfer: return "Transfer";
        case ErrorCategory::Validation: return "Validation";
        case ErrorCategory::Configuration: return "Configuration";
        case ErrorCategory::Permission: return "Permission";
        case ErrorCategory::NotFound: return "NotFound";
        case ErrorCategory::Conflict: return "Conflict";
        case ErrorCategory::Quota: return "Quota";
        case ErrorCategory::Internal: return "Internal";
        default: return "Unknown";
    }
}

/**
 * Get category for an error code
 */
inline ErrorCategory getCategoryForCode(ErrorCode code) {
    int c = static_cast<int>(code);
    if (c == 0) return ErrorCategory::None;
    if (c >= 100 && c < 200) return ErrorCategory::Authentication;
    if (c >= 200 && c < 300) return ErrorCategory::Network;
    if (c >= 300 && c < 400) return ErrorCategory::FileSystem;
    if (c >= 400 && c < 500) return ErrorCategory::CloudStorage;
    if (c >= 500 && c < 600) return ErrorCategory::Transfer;
    if (c >= 600 && c < 700) return ErrorCategory::Validation;
    if (c >= 700 && c < 800) return ErrorCategory::Configuration;
    return ErrorCategory::Internal;
}

/**
 * Detailed error information
 *
 * Can be used as return type or with exceptions.
 * Supports conversion to bool for easy checking.
 */
class Error {
public:
    /**
     * Create success (no error)
     */
    Error() : m_code(ErrorCode::OK) {}

    /**
     * Create error with code and message
     */
    Error(ErrorCode code, const std::string& message)
        : m_code(code), m_message(message) {}

    /**
     * Create error with code, message, and details
     */
    Error(ErrorCode code, const std::string& message, const std::string& details)
        : m_code(code), m_message(message), m_details(details) {}

    /**
     * Check if this represents success (no error)
     */
    bool isOk() const { return m_code == ErrorCode::OK; }

    /**
     * Check if this represents an error
     */
    bool isError() const { return m_code != ErrorCode::OK; }

    /**
     * Boolean conversion - true if no error
     */
    explicit operator bool() const { return isOk(); }

    // Accessors
    ErrorCode code() const { return m_code; }
    ErrorCategory category() const { return getCategoryForCode(m_code); }
    const std::string& message() const { return m_message; }
    const std::string& details() const { return m_details; }

    /**
     * Set additional details
     */
    Error& withDetails(const std::string& details) {
        m_details = details;
        return *this;
    }

    /**
     * Set underlying MEGA SDK error code
     */
    Error& withMegaError(int megaErrorCode) {
        m_megaErrorCode = megaErrorCode;
        return *this;
    }

    int megaErrorCode() const { return m_megaErrorCode.value_or(0); }
    bool hasMegaError() const { return m_megaErrorCode.has_value(); }

    /**
     * Format full error string
     */
    std::string toString() const {
        if (isOk()) return "OK";

        std::string result = "[" + std::string(getCategoryName(category())) + "] ";
        result += m_message;
        if (!m_details.empty()) {
            result += " (" + m_details + ")";
        }
        return result;
    }

    // Factory methods for common errors
    static Error ok() { return Error(); }

    static Error notLoggedIn() {
        return Error(ErrorCode::AUTH_NOT_LOGGED_IN, "Not logged in");
    }

    static Error fileNotFound(const std::string& path) {
        return Error(ErrorCode::FS_FILE_NOT_FOUND, "File not found", path);
    }

    static Error nodeNotFound(const std::string& path) {
        return Error(ErrorCode::CLOUD_NODE_NOT_FOUND, "Cloud node not found", path);
    }

    static Error transferFailed(const std::string& reason) {
        return Error(ErrorCode::TRANSFER_FAILED, "Transfer failed", reason);
    }

    static Error cancelled() {
        return Error(ErrorCode::CANCELLED, "Operation cancelled");
    }

    static Error fromMegaError(int megaErrorCode, const std::string& megaMessage);

private:
    ErrorCode m_code;
    std::string m_message;
    std::string m_details;
    std::optional<int> m_megaErrorCode;
};

/**
 * Exception wrapper for Error
 *
 * Use when exceptions are preferred over return codes.
 */
class ErrorException : public std::exception {
public:
    explicit ErrorException(const Error& error) : m_error(error) {
        m_what = m_error.toString();
    }

    ErrorException(ErrorCode code, const std::string& message)
        : m_error(code, message) {
        m_what = m_error.toString();
    }

    const char* what() const noexcept override {
        return m_what.c_str();
    }

    const Error& error() const { return m_error; }
    ErrorCode code() const { return m_error.code(); }

private:
    Error m_error;
    std::string m_what;
};

/**
 * Result type combining success value with possible error
 *
 * Alternative to exceptions for operations that can fail.
 *
 * Example:
 *   Result<std::string> getNodeName(const std::string& path) {
 *       if (notFound) return Error::nodeNotFound(path);
 *       return nodeName;  // Success
 *   }
 *
 *   auto result = getNodeName("/path");
 *   if (result) {
 *       std::cout << "Name: " << result.value() << std::endl;
 *   } else {
 *       std::cerr << "Error: " << result.error().message() << std::endl;
 *   }
 */
template<typename T>
class Result {
public:
    /**
     * Create success result with value
     */
    Result(const T& value) : m_value(value), m_error() {}
    Result(T&& value) : m_value(std::move(value)), m_error() {}

    /**
     * Create error result
     */
    Result(const Error& error) : m_value(std::nullopt), m_error(error) {}
    Result(Error&& error) : m_value(std::nullopt), m_error(std::move(error)) {}

    /**
     * Check if result is success
     */
    bool isOk() const { return m_value.has_value(); }
    bool isError() const { return !m_value.has_value(); }
    explicit operator bool() const { return isOk(); }

    /**
     * Get the value (throws if error)
     */
    const T& value() const {
        if (!m_value.has_value()) {
            throw ErrorException(m_error);
        }
        return m_value.value();
    }

    T& value() {
        if (!m_value.has_value()) {
            throw ErrorException(m_error);
        }
        return m_value.value();
    }

    /**
     * Get the value or a default
     */
    T valueOr(const T& defaultValue) const {
        return m_value.value_or(defaultValue);
    }

    /**
     * Get the error (empty if success)
     */
    const Error& error() const { return m_error; }

private:
    std::optional<T> m_value;
    Error m_error;
};

/**
 * Specialization for void (operation without return value)
 */
template<>
class Result<void> {
public:
    Result() : m_error() {}
    Result(const Error& error) : m_error(error) {}

    bool isOk() const { return m_error.isOk(); }
    bool isError() const { return m_error.isError(); }
    explicit operator bool() const { return isOk(); }

    const Error& error() const { return m_error; }

private:
    Error m_error;
};

} // namespace MegaCustom

#endif // MEGA_CUSTOM_ERROR_H
