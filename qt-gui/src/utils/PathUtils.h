#ifndef MEGACUSTOM_PATHUTILS_H
#define MEGACUSTOM_PATHUTILS_H

#include <QString>

namespace MegaCustom {
namespace PathUtils {

/**
 * Normalize a remote MEGA path:
 * - Remove leading whitespace only (preserve trailing - folder names can end with spaces)
 * - Remove Windows line endings (\r)
 * - Ensure path starts with / (but don't duplicate if already starts with /)
 */
inline QString normalizeRemotePath(const QString& path) {
    QString result = path;

    // Remove \r from Windows line endings
    result.remove('\r');

    // Remove leading whitespace only (preserve trailing spaces in folder names)
    int startPos = 0;
    while (startPos < result.length() &&
           (result[startPos] == ' ' || result[startPos] == '\t')) {
        startPos++;
    }
    result = result.mid(startPos);

    // Ensure starts with / but don't duplicate
    if (!result.isEmpty() && !result.startsWith('/')) {
        result = '/' + result;
    }

    return result;
}

/**
 * Check if a path is empty after normalization
 * (only whitespace counts as empty)
 */
inline bool isPathEmpty(const QString& path) {
    QString normalized = normalizeRemotePath(path);
    return normalized.isEmpty() || normalized == "/";
}

/**
 * Normalize a local filesystem path:
 * - Trim both leading and trailing whitespace (local paths don't have trailing spaces)
 */
inline QString normalizeLocalPath(const QString& path) {
    return path.trimmed();
}

} // namespace PathUtils
} // namespace MegaCustom

#endif // MEGACUSTOM_PATHUTILS_H
