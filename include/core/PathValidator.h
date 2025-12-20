#ifndef MEGACUSTOM_PATH_VALIDATOR_H
#define MEGACUSTOM_PATH_VALIDATOR_H

#include <string>
#include <vector>
#include <filesystem>

namespace megacustom {

/**
 * Centralized path validation to prevent path traversal attacks.
 *
 * Checks for:
 * - Directory traversal sequences (../)
 * - Null bytes
 * - Invalid characters
 * - Symlink attacks
 */
class PathValidator {
public:
    /**
     * Check if path is valid (no traversal sequences, null bytes, etc.)
     * @param path Path to validate
     * @return true if path is safe
     */
    static bool isValidPath(const std::string& path);

    /**
     * Check if resolved path stays within base directory
     * Resolves symlinks and normalizes path before comparison
     * @param path Path to check
     * @param baseDir Base directory that path must be within
     * @return true if path is within baseDir
     */
    static bool isWithinBaseDir(const std::string& path, const std::string& baseDir);

    /**
     * Sanitize path by removing dangerous sequences
     * @param path Path to sanitize
     * @return Sanitized path
     */
    static std::string sanitize(const std::string& path);

    /**
     * Check if path contains null byte
     * @param path Path to check
     * @return true if path contains null byte
     */
    static bool containsNullByte(const std::string& path);

    /**
     * Check if path contains traversal sequences
     * @param path Path to check
     * @return true if path contains ../ or similar
     */
    static bool containsTraversal(const std::string& path);

    /**
     * Check if path contains invalid characters for the current OS
     * @param path Path to check
     * @return true if path contains invalid characters
     */
    static bool containsInvalidChars(const std::string& path);

    /**
     * Normalize path (resolve . and .. without following symlinks)
     * @param path Path to normalize
     * @return Normalized path
     */
    static std::string normalize(const std::string& path);

    /**
     * Join paths safely, validating result
     * @param base Base path
     * @param relative Relative path to append
     * @return Joined path
     * @throws std::invalid_argument if result would escape base
     */
    static std::string safeJoin(const std::string& base, const std::string& relative);

    /**
     * Create directory safely with validation
     * @param path Directory path to create
     * @param baseDir Optional base directory to validate against
     * @return true if directory was created or already exists
     */
    static bool createDirectorySafe(const std::string& path,
                                     const std::string& baseDir = "");

    /**
     * Copy file safely with path validation
     * @param source Source file path
     * @param destination Destination file path
     * @param baseDir Optional base directory to validate against
     * @return true if copy succeeded
     */
    static bool copyFileSafe(const std::string& source,
                              const std::string& destination,
                              const std::string& baseDir = "");

    // Platform-specific invalid characters
#ifdef _WIN32
    static constexpr const char* INVALID_CHARS = "<>:\"|?*";
#else
    static constexpr const char* INVALID_CHARS = "";  // Unix allows most chars
#endif

private:
    // Traversal patterns to check
    static const std::vector<std::string> TRAVERSAL_PATTERNS;
};

} // namespace megacustom

#endif // MEGACUSTOM_PATH_VALIDATOR_H
