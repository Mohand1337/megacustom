#ifndef MEGACUSTOM_CONTENTROUTER_H
#define MEGACUSTOM_CONTENTROUTER_H

#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>

namespace mega {
    class MegaApi;
}

namespace MegaCustom {

struct MemberInfo;

/**
 * Content type classification for smart routing
 */
enum class ContentType {
    NHB_ROOT_FILES,    // Root-level files (mp4, mp3, pdf) → NHB calls + month
    HOT_SEATS,         // Subfolder matching hot seats pattern
    THEORY_CALLS,      // Subfolder matching theory calls pattern
    FAST_FORWARD,      // Generic Fast Forward subfolder
    UNKNOWN            // Unclassified — falls back to template destination
};

/**
 * A single routed content item (file group or folder) within a member folder
 */
struct ContentRoute {
    QString sourcePath;           // Full MEGA path to child (or parent for file group)
    QString childName;            // Display name
    bool isFolder = false;        // true = subfolder, false = file group
    ContentType contentType = ContentType::UNKNOWN;
    QString contentTypeLabel;     // "Hot Seats", "Theory Calls", "NHB Files"
    QString destinationPath;      // Resolved from MemberPaths
    QString memberId;
    int parentFolderIndex = -1;   // Index in m_wmFolders for grouping
    bool selected = true;
    QStringList filePaths;        // Individual file paths (for NHB_ROOT_FILES group)
};

/**
 * ContentRouter — classifies member folder children and resolves destinations
 *
 * Classification strategy (in priority order):
 * 1. Dynamic matching: Compare child name against member's actual MemberPaths values
 * 2. Keyword matching: Match against built-in keyword lists
 * 3. Root files grouped as NHB content
 * 4. Fallback: Unknown → current template destination
 */
class ContentRouter {
public:
    ContentRouter() = default;

    /**
     * Scan a member folder's children and classify each one.
     * Root-level files are grouped into a single NHB_ROOT_FILES route.
     * Each subfolder gets its own route based on classification.
     */
    QList<ContentRoute> classifyChildren(
        mega::MegaApi* megaApi,
        const QString& memberFolderPath,
        const MemberInfo& member,
        const QString& month,
        const QString& fallbackDest = QString()) const;

    /**
     * Classify a single folder name.
     * Uses dynamic match (member paths) then keyword match.
     */
    ContentType classifyFolder(
        const QString& folderName,
        const MemberInfo& member,
        QString* matchedKeyword = nullptr) const;

    /**
     * Resolve destination path for a content type and member.
     */
    static QString resolveDestination(
        ContentType type,
        const MemberInfo& member,
        const QString& month,
        const QString& fallbackDest = QString());

    /**
     * Human-readable label for a content type.
     */
    static QString contentTypeLabel(ContentType type);

private:
    /**
     * Normalize a string for matching: lowercase, strip leading numbers/punctuation.
     */
    static QString normalize(const QString& input);

    /**
     * Match folder name against member's actual MemberPaths field values.
     */
    ContentType dynamicMatch(
        const QString& folderName,
        const MemberInfo& member,
        QString* matchedKeyword = nullptr) const;

    /**
     * Match folder name against built-in keyword rules.
     */
    ContentType keywordMatch(
        const QString& folderName,
        QString* matchedKeyword = nullptr) const;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_CONTENTROUTER_H
