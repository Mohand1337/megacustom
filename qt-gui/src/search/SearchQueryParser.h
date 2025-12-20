#ifndef SEARCHQUERYPARSER_H
#define SEARCHQUERYPARSER_H

#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QRegularExpression>
#include <optional>

namespace MegaCustom {

// Forward declaration
struct IndexedNode;

/**
 * Parsed search query with all filter components
 *
 * Supports voidtools Everything-like syntax:
 * - Simple terms: "report" (name contains)
 * - Wildcards: "*.mp4", "test?.doc"
 * - Extension filter: ext:pdf or ext:pdf,docx
 * - Size filter: size:>100mb, size:<1gb, size:10kb-50mb
 * - Date modified: dm:today, dm:yesterday, dm:thisweek, dm:>2024-01-01
 * - Path filter: path:Documents
 * - Type filter: type:folder or type:file
 * - Regex: regex:^test.*\.pdf$
 * - NOT operator: !backup (exclude items containing "backup")
 * - OR operator: doc | pdf (match either)
 */
struct ParsedQuery {
    // Search terms (AND logic by default)
    QStringList terms;              // Positive search terms
    QStringList notTerms;           // Negative terms (excluded)
    QStringList orTerms;            // OR terms (any match)

    // Extension filter
    QStringList extensions;         // e.g., {"pdf", "docx"}

    // Size filter
    std::optional<qint64> minSize;  // Minimum size in bytes
    std::optional<qint64> maxSize;  // Maximum size in bytes

    // Date filter
    std::optional<QDateTime> minDate;
    std::optional<QDateTime> maxDate;

    // Path filter
    QString pathContains;           // Path must contain this string

    // Type filter
    enum class TypeFilter { Any, FileOnly, FolderOnly };
    TypeFilter typeFilter = TypeFilter::Any;

    // Regex filter
    QRegularExpression regexPattern;
    bool hasRegex = false;

    // Wildcard patterns (converted from user input like *.mp4)
    QStringList wildcardPatterns;

    // Check if query is empty/matches everything
    bool isEmpty() const {
        return terms.isEmpty() && notTerms.isEmpty() && orTerms.isEmpty() &&
               extensions.isEmpty() && !minSize && !maxSize &&
               !minDate && !maxDate && pathContains.isEmpty() &&
               typeFilter == TypeFilter::Any && !hasRegex &&
               wildcardPatterns.isEmpty();
    }
};

/**
 * Parser for Everything-like search queries
 */
class SearchQueryParser
{
public:
    SearchQueryParser();

    /**
     * Parse a search query string into structured components
     * @param query Raw query string from user input
     * @return Parsed query structure
     */
    ParsedQuery parse(const QString& query) const;

    /**
     * Check if an indexed node matches the parsed query
     * @param node The node to check
     * @param query The parsed query
     * @return true if node matches all criteria
     */
    bool matches(const IndexedNode& node, const ParsedQuery& query) const;

private:
    // Parse size string like "100mb", "1.5gb", ">50kb"
    std::optional<qint64> parseSize(const QString& sizeStr) const;

    // Parse date string like "today", "2024-01-01", ">2024-01-01"
    std::optional<QDateTime> parseDate(const QString& dateStr) const;

    // Parse date range string and set min/max dates
    void parseDateRange(const QString& dateStr, ParsedQuery& query) const;

    // Parse size range string like "10kb-50mb"
    void parseSizeRange(const QString& sizeStr, ParsedQuery& query) const;

    // Convert wildcard pattern to regex
    QRegularExpression wildcardToRegex(const QString& pattern) const;

    // Check if string matches wildcard pattern
    bool matchesWildcard(const QString& text, const QString& pattern) const;

    // Size unit multipliers
    static constexpr qint64 KB = 1024;
    static constexpr qint64 MB = 1024 * 1024;
    static constexpr qint64 GB = 1024 * 1024 * 1024;
    static constexpr qint64 TB = 1024LL * 1024 * 1024 * 1024;
};

} // namespace MegaCustom

#endif // SEARCHQUERYPARSER_H
