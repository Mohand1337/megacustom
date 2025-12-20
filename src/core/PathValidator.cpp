#include "core/PathValidator.h"
#include "core/LogManager.h"
#include <algorithm>
#include <regex>
#include <cstring>

namespace megacustom {

namespace fs = std::filesystem;

const std::vector<std::string> PathValidator::TRAVERSAL_PATTERNS = {
    "..",
    "../",
    "..\\",
    "%2e%2e",     // URL encoded
    "%2e%2e/",
    "%2e%2e\\",
    "..%2f",
    "..%5c",
    "%252e%252e", // Double URL encoded
};

bool PathValidator::containsNullByte(const std::string& path) {
    return path.find('\0') != std::string::npos;
}

bool PathValidator::containsTraversal(const std::string& path) {
    // Convert to lowercase for case-insensitive check
    std::string lowerPath = path;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);

    for (const auto& pattern : TRAVERSAL_PATTERNS) {
        if (lowerPath.find(pattern) != std::string::npos) {
            return true;
        }
    }

    return false;
}

bool PathValidator::containsInvalidChars(const std::string& path) {
    if (std::strlen(INVALID_CHARS) == 0) {
        return false;
    }

    return path.find_first_of(INVALID_CHARS) != std::string::npos;
}

bool PathValidator::isValidPath(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    if (containsNullByte(path)) {
        MegaCustom::LogManager::instance().log(MegaCustom::LogLevel::Warning, MegaCustom::LogCategory::System,
            "isValidPath", "Null byte detected in path", path);
        return false;
    }

    if (containsTraversal(path)) {
        MegaCustom::LogManager::instance().log(MegaCustom::LogLevel::Warning, MegaCustom::LogCategory::System,
            "isValidPath", "Traversal sequence detected", path);
        return false;
    }

    if (containsInvalidChars(path)) {
        MegaCustom::LogManager::instance().log(MegaCustom::LogLevel::Warning, MegaCustom::LogCategory::System,
            "isValidPath", "Invalid characters detected", path);
        return false;
    }

    return true;
}

bool PathValidator::isWithinBaseDir(const std::string& path, const std::string& baseDir) {
    if (path.empty() || baseDir.empty()) {
        return false;
    }

    try {
        // Get canonical paths (resolves symlinks)
        fs::path canonicalBase = fs::weakly_canonical(baseDir);
        fs::path canonicalPath = fs::weakly_canonical(path);

        // Convert to strings for comparison
        std::string baseStr = canonicalBase.string();
        std::string pathStr = canonicalPath.string();

        // Ensure base ends with separator for proper prefix matching
        if (!baseStr.empty() && baseStr.back() != fs::path::preferred_separator) {
            baseStr += fs::path::preferred_separator;
        }

        // Check if path starts with base
        return pathStr.size() >= baseStr.size() &&
               pathStr.compare(0, baseStr.size(), baseStr) == 0;

    } catch (const fs::filesystem_error& e) {
        MegaCustom::LogManager::instance().log(MegaCustom::LogLevel::Warning, MegaCustom::LogCategory::System,
            "isWithinBaseDir", "Path validation error", e.what());
        return false;
    }
}

std::string PathValidator::sanitize(const std::string& path) {
    if (path.empty()) {
        return "";
    }

    std::string result = path;

    // Remove null bytes
    result.erase(std::remove(result.begin(), result.end(), '\0'), result.end());

    // Remove traversal sequences iteratively until none remain
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& pattern : TRAVERSAL_PATTERNS) {
            size_t pos;
            while ((pos = result.find(pattern)) != std::string::npos) {
                result.erase(pos, pattern.length());
                changed = true;
            }
        }
    }

    // Remove invalid characters on Windows
    if (std::strlen(INVALID_CHARS) > 0) {
        result.erase(
            std::remove_if(result.begin(), result.end(),
                [](char c) { return std::strchr(INVALID_CHARS, c) != nullptr; }),
            result.end()
        );
    }

    // Normalize multiple separators
    std::string normalized;
    char lastChar = '\0';
    for (char c : result) {
        bool isSep = (c == '/' || c == '\\');
        bool lastWasSep = (lastChar == '/' || lastChar == '\\');

        if (!(isSep && lastWasSep)) {
            normalized += c;
        }
        lastChar = c;
    }

    return normalized;
}

std::string PathValidator::normalize(const std::string& path) {
    if (path.empty()) {
        return "";
    }

    try {
        fs::path p(path);
        fs::path normalized;

        for (const auto& part : p) {
            if (part == "..") {
                if (!normalized.empty() && normalized.filename() != "..") {
                    normalized = normalized.parent_path();
                }
            } else if (part != ".") {
                normalized /= part;
            }
        }

        return normalized.string();

    } catch (const std::exception&) {
        return sanitize(path);
    }
}

std::string PathValidator::safeJoin(const std::string& base, const std::string& relative) {
    if (!isValidPath(relative)) {
        throw std::invalid_argument("Invalid relative path: contains dangerous sequences");
    }

    // Don't allow absolute relative paths
    fs::path relPath(relative);
    if (relPath.is_absolute()) {
        throw std::invalid_argument("Relative path must not be absolute");
    }

    fs::path result = fs::path(base) / relPath;
    std::string resultStr = result.lexically_normal().string();

    // Verify result is still within base
    if (!isWithinBaseDir(resultStr, base)) {
        throw std::invalid_argument("Resulting path escapes base directory");
    }

    return resultStr;
}

bool PathValidator::createDirectorySafe(const std::string& path,
                                         const std::string& baseDir) {
    if (!isValidPath(path)) {
        MegaCustom::LogManager::instance().log(MegaCustom::LogLevel::Error, MegaCustom::LogCategory::System,
            "createDirectorySafe", "Invalid path rejected", path);
        return false;
    }

    if (!baseDir.empty() && !isWithinBaseDir(path, baseDir)) {
        MegaCustom::LogManager::instance().log(MegaCustom::LogLevel::Error, MegaCustom::LogCategory::System,
            "createDirectorySafe", "Path outside allowed base directory", path);
        return false;
    }

    try {
        fs::create_directories(path);
        return true;
    } catch (const fs::filesystem_error& e) {
        MegaCustom::LogManager::instance().log(MegaCustom::LogLevel::Error, MegaCustom::LogCategory::System,
            "createDirectorySafe", "Failed to create directory", e.what());
        return false;
    }
}

bool PathValidator::copyFileSafe(const std::string& source,
                                  const std::string& destination,
                                  const std::string& baseDir) {
    if (!isValidPath(source) || !isValidPath(destination)) {
        MegaCustom::LogManager::instance().log(MegaCustom::LogLevel::Error, MegaCustom::LogCategory::System,
            "copyFileSafe", "Invalid path rejected", source + " -> " + destination);
        return false;
    }

    if (!baseDir.empty()) {
        if (!isWithinBaseDir(source, baseDir) || !isWithinBaseDir(destination, baseDir)) {
            MegaCustom::LogManager::instance().log(MegaCustom::LogLevel::Error, MegaCustom::LogCategory::System,
                "copyFileSafe", "Path outside allowed base directory", source);
            return false;
        }
    }

    try {
        // Create parent directory if needed
        fs::path destPath(destination);
        if (destPath.has_parent_path()) {
            fs::create_directories(destPath.parent_path());
        }

        fs::copy_file(source, destination, fs::copy_options::overwrite_existing);
        return true;
    } catch (const fs::filesystem_error& e) {
        MegaCustom::LogManager::instance().log(MegaCustom::LogLevel::Error, MegaCustom::LogCategory::System,
            "copyFileSafe", "Failed to copy file", e.what());
        return false;
    }
}

} // namespace megacustom
