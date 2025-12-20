#ifndef REGEX_RENAMER_H
#define REGEX_RENAMER_H

#include <string>
#include <vector>
#include <regex>
#include <functional>
#include <optional>
#include <map>
#include <memory>
#include <chrono>

namespace mega {
    class MegaApi;
    class MegaNode;
}

namespace MegaCustom {

/**
 * Rename operation result
 */
struct RenameResult {
    std::string originalName;
    std::string newName;
    std::string fullPath;
    bool success;
    std::string errorMessage;
    bool wasSkipped;  // Skipped due to conflict or rule
};

/**
 * Rename preview entry
 */
struct RenamePreview {
    std::string originalName;
    std::string proposedName;
    std::string fullPath;
    bool hasConflict;
    std::string conflictReason;
    mega::MegaNode* node;
};

/**
 * Rename pattern configuration
 */
struct RenamePattern {
    // Basic patterns
    std::string searchPattern;     // Regex pattern to match
    std::string replacePattern;    // Replacement pattern
    bool caseSensitive = true;
    bool useExtendedRegex = true;  // Use PCRE2 extended syntax

    // Advanced options
    bool preserveExtension = true;
    bool applyToExtension = false;
    int maxReplacements = -1;       // -1 = replace all

    // Naming templates
    bool useSequentialNumbering = false;
    int numberingStart = 1;
    int numberingPadding = 3;       // e.g., 001, 002, etc.
    std::string numberingFormat;   // e.g., "IMG_{num:04d}"

    // Date/time patterns
    bool insertDateTime = false;
    std::string dateTimeFormat = "%Y%m%d_%H%M%S";
    bool useFileModTime = true;    // Use file modification time

    // Case conversion
    enum CaseConversion {
        NONE,
        LOWERCASE,
        UPPERCASE,
        TITLE_CASE,
        SENTENCE_CASE,
        CAMEL_CASE,
        SNAKE_CASE,
        KEBAB_CASE
    } caseConversion = NONE;

    // Character replacement
    std::map<std::string, std::string> characterReplacements;
    bool sanitizeForFilesystem = false;  // Remove illegal characters
    bool normalizeUnicode = false;       // Normalize Unicode characters
};

/**
 * Undo/Redo operation
 */
struct RenameOperation {
    std::string operationId;
    std::vector<RenameResult> results;
    std::chrono::system_clock::time_point timestamp;
    RenamePattern pattern;
};

/**
 * Advanced regex-based bulk file renaming
 */
class RegexRenamer {
public:
    explicit RegexRenamer(mega::MegaApi* megaApi);
    ~RegexRenamer();

    /**
     * Preview rename operations without applying
     * @param nodes Vector of nodes to rename
     * @param pattern Rename pattern configuration
     * @return Vector of rename previews
     */
    std::vector<RenamePreview> previewRename(
        const std::vector<mega::MegaNode*>& nodes,
        const RenamePattern& pattern);

    /**
     * Preview rename by path pattern
     * @param pathPattern Path pattern (e.g., "/photos/*.jpg")
     * @param renamePattern Rename pattern configuration
     * @param recursive Apply to subdirectories
     * @return Vector of rename previews
     */
    std::vector<RenamePreview> previewRenameByPath(
        const std::string& pathPattern,
        const RenamePattern& pattern,
        bool recursive = false);

    /**
     * Apply rename operations
     * @param previews Vector of approved previews
     * @param dryRun Perform dry run without actual renaming
     * @return Vector of rename results
     */
    std::vector<RenameResult> applyRename(
        const std::vector<RenamePreview>& previews,
        bool dryRun = false);

    /**
     * Bulk rename with pattern
     * @param nodes Nodes to rename
     * @param pattern Rename pattern
     * @param autoResolveConflicts Automatically resolve naming conflicts
     * @return Vector of rename results
     */
    std::vector<RenameResult> bulkRename(
        const std::vector<mega::MegaNode*>& nodes,
        const RenamePattern& pattern,
        bool autoResolveConflicts = false);

    /**
     * Rename files matching regex in path
     * @param pathPattern Path pattern
     * @param renamePattern Rename pattern
     * @param recursive Process subdirectories
     * @return Vector of rename results
     */
    std::vector<RenameResult> renameByPathPattern(
        const std::string& pathPattern,
        const RenamePattern& pattern,
        bool recursive = false);

    /**
     * Add custom rename rule
     * @param name Rule name
     * @param pattern Rename pattern
     */
    void addCustomRule(const std::string& name, const RenamePattern& pattern);

    /**
     * Apply predefined rule
     * @param ruleName Name of the rule
     * @param nodes Nodes to rename
     * @return Vector of rename results
     */
    std::vector<RenameResult> applyRule(
        const std::string& ruleName,
        const std::vector<mega::MegaNode*>& nodes);

    /**
     * Get available predefined rules
     * @return Map of rule names to descriptions
     */
    std::map<std::string, std::string> getAvailableRules() const;

    /**
     * Undo last rename operation
     * @return true if undo successful
     */
    bool undoLastRename();

    /**
     * Redo previously undone operation
     * @return true if redo successful
     */
    bool redoRename();

    /**
     * Get undo history
     * @param limit Maximum number of operations
     * @return Vector of rename operations
     */
    std::vector<RenameOperation> getUndoHistory(int limit = 10) const;

    /**
     * Clear undo/redo history
     */
    void clearHistory();

    /**
     * Extract metadata from file
     * @param node File node
     * @return Map of metadata key-value pairs
     */
    std::map<std::string, std::string> extractMetadata(mega::MegaNode* node);

    /**
     * Create pattern from template
     * @param templateName Template name (e.g., "photo_organize")
     * @return Rename pattern or nullopt if template not found
     */
    std::optional<RenamePattern> createFromTemplate(const std::string& templateName);

    /**
     * Validate regex pattern
     * @param pattern Regex pattern to validate
     * @param errorMsg Output error message if invalid
     * @return true if pattern is valid
     */
    static bool validateRegexPattern(const std::string& pattern, std::string& errorMsg);

    /**
     * Test regex pattern on sample text
     * @param pattern Regex pattern
     * @param sampleText Text to test
     * @param replacement Replacement pattern
     * @return Result of applying pattern
     */
    static std::string testPattern(
        const std::string& pattern,
        const std::string& sampleText,
        const std::string& replacement);

    /**
     * Generate unique name if conflict exists
     * @param baseName Base name
     * @param existingNames List of existing names
     * @return Unique name
     */
    static std::string generateUniqueName(
        const std::string& baseName,
        const std::vector<std::string>& existingNames);

    /**
     * Sanitize filename for filesystem
     * @param filename Filename to sanitize
     * @param replacementChar Character to replace illegal chars
     * @return Sanitized filename
     */
    static std::string sanitizeFilename(
        const std::string& filename,
        char replacementChar = '_');

    /**
     * Set progress callback for bulk operations
     * @param callback Function called with progress (current, total)
     */
    void setProgressCallback(
        std::function<void(int current, int total, const std::string& currentFile)> callback);

    /**
     * Set conflict resolution callback
     * @param callback Function to resolve conflicts
     */
    void setConflictResolver(
        std::function<std::string(const std::string& original, const std::string& proposed)> callback);

    /**
     * Enable/disable safe mode (creates backup of names before renaming)
     * @param enable Enable flag
     */
    void setSafeMode(bool enable);

    /**
     * Export rename rules to file
     * @param filePath Path to export file
     * @return true if exported successfully
     */
    bool exportRules(const std::string& filePath) const;

    /**
     * Import rename rules from file
     * @param filePath Path to import file
     * @return true if imported successfully
     */
    bool importRules(const std::string& filePath);

private:
    mega::MegaApi* m_megaApi;

    // Pattern templates
    std::map<std::string, RenamePattern> m_customRules;
    std::map<std::string, RenamePattern> m_templates;

    // Undo/Redo management
    std::vector<RenameOperation> m_undoStack;
    std::vector<RenameOperation> m_redoStack;
    size_t m_maxHistorySize = 50;

    // Safe mode backup
    bool m_safeMode = true;
    std::map<std::string, std::string> m_backupNames;

    // Callbacks
    std::function<void(int, int, const std::string&)> m_progressCallback;
    std::function<std::string(const std::string&, const std::string&)> m_conflictResolver;

    // Helper methods
    std::string applyPattern(const std::string& input, const RenamePattern& pattern);
    std::string applyRegex(const std::string& input, const RenamePattern& pattern);
    std::string applyNumbering(const std::string& input, int index, const RenamePattern& pattern);
    std::string applyDateTime(const std::string& input, mega::MegaNode* node, const RenamePattern& pattern);
    std::string applyCaseConversion(const std::string& input, RenamePattern::CaseConversion conversion);
    bool checkNameConflict(const std::string& name, mega::MegaNode* parent);
    void initializeTemplates();
    void saveBackup(mega::MegaNode* node);
    void restoreBackup(const std::string& nodeHandle);
    std::string extractExtension(const std::string& filename);
    std::string removeExtension(const std::string& filename);

    // Metadata extraction
    std::map<std::string, std::string> extractImageMetadata(mega::MegaNode* node);
    std::map<std::string, std::string> extractAudioMetadata(mega::MegaNode* node);
    std::map<std::string, std::string> extractVideoMetadata(mega::MegaNode* node);
};

} // namespace MegaCustom

#endif // REGEX_RENAMER_H