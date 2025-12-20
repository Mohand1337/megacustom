#include "SearchQueryParser.h"
#include "CloudSearchIndex.h"
#include <QDebug>
#include <QRegularExpression>

namespace MegaCustom {

SearchQueryParser::SearchQueryParser()
{
}

ParsedQuery SearchQueryParser::parse(const QString& query) const
{
    ParsedQuery result;
    QString remaining = query.trimmed();

    if (remaining.isEmpty()) {
        return result;
    }

    // Regular expression to match operators
    // Format: operator:value or just term
    static QRegularExpression operatorRe(
        R"((?:^|\s)(ext|size|dm|path|type|regex):(\S+)|(?:^|\s)(!?)("[^"]+"|[^\s|]+))",
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatchIterator it = operatorRe.globalMatch(remaining);

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();

        // Check if it's an operator:value pair
        QString op = match.captured(1).toLower();
        QString opValue = match.captured(2);

        if (!op.isEmpty() && !opValue.isEmpty()) {
            // Handle operators
            if (op == "ext") {
                // Extension filter: ext:pdf or ext:pdf,docx
                QStringList exts = opValue.toLower().split(',', Qt::SkipEmptyParts);
                for (const QString& ext : exts) {
                    // Remove leading dot if present
                    QString cleanExt = ext.startsWith('.') ? ext.mid(1) : ext;
                    result.extensions.append(cleanExt);
                }
            }
            else if (op == "size") {
                parseSizeRange(opValue, result);
            }
            else if (op == "dm") {
                parseDateRange(opValue, result);
            }
            else if (op == "path") {
                result.pathContains = opValue.toLower();
            }
            else if (op == "type") {
                QString typeVal = opValue.toLower();
                if (typeVal == "folder" || typeVal == "dir" || typeVal == "directory") {
                    result.typeFilter = ParsedQuery::TypeFilter::FolderOnly;
                } else if (typeVal == "file") {
                    result.typeFilter = ParsedQuery::TypeFilter::FileOnly;
                }
            }
            else if (op == "regex") {
                result.regexPattern = QRegularExpression(opValue,
                    QRegularExpression::CaseInsensitiveOption);
                result.hasRegex = result.regexPattern.isValid();
                if (!result.hasRegex) {
                    qWarning() << "SearchQueryParser: Invalid regex pattern:" << opValue;
                }
            }
        }
        else {
            // Regular term (possibly with ! prefix)
            QString notPrefix = match.captured(3);
            QString term = match.captured(4);

            // Remove quotes if present
            if (term.startsWith('"') && term.endsWith('"')) {
                term = term.mid(1, term.length() - 2);
            }

            if (term.isEmpty()) continue;

            // Check for OR operator (|)
            if (term == "|") continue; // Skip the operator itself

            // Check if previous term was | (OR)
            // This is handled by checking if term contains |
            if (term.contains('|')) {
                QStringList orParts = term.split('|', Qt::SkipEmptyParts);
                for (const QString& part : orParts) {
                    result.orTerms.append(part.trimmed().toLower());
                }
                continue;
            }

            // Check for wildcard patterns
            if (term.contains('*') || term.contains('?')) {
                result.wildcardPatterns.append(term.toLower());
                continue;
            }

            // Regular term
            QString lowerTerm = term.toLower();
            if (notPrefix == "!") {
                result.notTerms.append(lowerTerm);
            } else {
                result.terms.append(lowerTerm);
            }
        }
    }

    // If no terms were parsed but query wasn't empty, treat whole query as search term
    if (result.isEmpty() && !remaining.isEmpty()) {
        // Check for wildcards
        if (remaining.contains('*') || remaining.contains('?')) {
            result.wildcardPatterns.append(remaining.toLower());
        } else {
            result.terms.append(remaining.toLower());
        }
    }

    return result;
}

bool SearchQueryParser::matches(const IndexedNode& node, const ParsedQuery& query) const
{
    // Check type filter first (fast)
    if (query.typeFilter == ParsedQuery::TypeFilter::FolderOnly && !node.isFolder) {
        return false;
    }
    if (query.typeFilter == ParsedQuery::TypeFilter::FileOnly && node.isFolder) {
        return false;
    }

    // Check NOT terms (early exit)
    for (const QString& notTerm : query.notTerms) {
        if (node.nameLower.contains(notTerm) || node.pathLower.contains(notTerm)) {
            return false;
        }
    }

    // Check extension filter
    if (!query.extensions.isEmpty()) {
        bool extMatch = false;
        for (const QString& ext : query.extensions) {
            if (node.extension == ext) {
                extMatch = true;
                break;
            }
        }
        if (!extMatch) return false;
    }

    // Check size range
    if (query.minSize && node.size < *query.minSize) {
        return false;
    }
    if (query.maxSize && node.size > *query.maxSize) {
        return false;
    }

    // Check date range
    if (query.minDate) {
        QDateTime nodeDate = QDateTime::fromSecsSinceEpoch(node.modificationTime);
        if (nodeDate < *query.minDate) {
            return false;
        }
    }
    if (query.maxDate) {
        QDateTime nodeDate = QDateTime::fromSecsSinceEpoch(node.modificationTime);
        if (nodeDate > *query.maxDate) {
            return false;
        }
    }

    // Check path filter
    if (!query.pathContains.isEmpty()) {
        if (!node.pathLower.contains(query.pathContains)) {
            return false;
        }
    }

    // Check regex pattern
    if (query.hasRegex) {
        QRegularExpressionMatch match = query.regexPattern.match(node.name);
        if (!match.hasMatch()) {
            return false;
        }
    }

    // Check wildcard patterns (all must match)
    for (const QString& pattern : query.wildcardPatterns) {
        if (!matchesWildcard(node.nameLower, pattern)) {
            return false;
        }
    }

    // Check OR terms (at least one must match if any exist)
    if (!query.orTerms.isEmpty()) {
        bool orMatch = false;
        for (const QString& orTerm : query.orTerms) {
            if (node.nameLower.contains(orTerm)) {
                orMatch = true;
                break;
            }
        }
        if (!orMatch) return false;
    }

    // Check regular terms (all must match - AND logic)
    for (const QString& term : query.terms) {
        if (!node.nameLower.contains(term) && !node.pathLower.contains(term)) {
            return false;
        }
    }

    return true;
}

std::optional<qint64> SearchQueryParser::parseSize(const QString& sizeStr) const
{
    static QRegularExpression sizeRe(
        R"(^([<>]?)(\d+(?:\.\d+)?)\s*(b|kb|mb|gb|tb)?$)",
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatch match = sizeRe.match(sizeStr.trimmed());
    if (!match.hasMatch()) {
        return std::nullopt;
    }

    double value = match.captured(2).toDouble();
    QString unit = match.captured(3).toLower();

    qint64 multiplier = 1;
    if (unit == "kb") multiplier = KB;
    else if (unit == "mb") multiplier = MB;
    else if (unit == "gb") multiplier = GB;
    else if (unit == "tb") multiplier = TB;

    return static_cast<qint64>(value * multiplier);
}

std::optional<QDateTime> SearchQueryParser::parseDate(const QString& dateStr) const
{
    QString lower = dateStr.toLower().trimmed();

    QDateTime now = QDateTime::currentDateTime();

    // Handle special keywords
    if (lower == "today") {
        return now.date().startOfDay();
    }
    else if (lower == "yesterday") {
        return now.date().addDays(-1).startOfDay();
    }
    else if (lower == "thisweek") {
        return now.date().addDays(-7).startOfDay();
    }
    else if (lower == "thismonth") {
        return now.date().addMonths(-1).startOfDay();
    }
    else if (lower == "thisyear") {
        return now.date().addYears(-1).startOfDay();
    }

    // Try to parse as date
    QDateTime parsed = QDateTime::fromString(dateStr, "yyyy-MM-dd");
    if (parsed.isValid()) {
        return parsed;
    }

    parsed = QDateTime::fromString(dateStr, "yyyy/MM/dd");
    if (parsed.isValid()) {
        return parsed;
    }

    parsed = QDateTime::fromString(dateStr, "MM-dd-yyyy");
    if (parsed.isValid()) {
        return parsed;
    }

    return std::nullopt;
}

void SearchQueryParser::parseDateRange(const QString& dateStr, ParsedQuery& query) const
{
    QString str = dateStr.trimmed();

    // Check for range: date1-date2 or date1..date2
    int rangeIdx = str.indexOf('-');
    if (rangeIdx == -1) rangeIdx = str.indexOf("..");

    if (rangeIdx > 0 && rangeIdx < str.length() - 1) {
        // Looks like a range, but could be part of date format
        // Only treat as range if we find two valid dates
        QString left = str.left(rangeIdx);
        QString right = str.mid(rangeIdx + (str.contains("..") ? 2 : 1));

        auto minDate = parseDate(left);
        auto maxDate = parseDate(right);

        if (minDate && maxDate) {
            query.minDate = minDate;
            query.maxDate = maxDate;
            return;
        }
    }

    // Check for comparison operators
    if (str.startsWith('>')) {
        query.minDate = parseDate(str.mid(1));
    }
    else if (str.startsWith('<')) {
        query.maxDate = parseDate(str.mid(1));
    }
    else {
        // Single date - match that specific day
        auto date = parseDate(str);
        if (date) {
            query.minDate = date;
            query.maxDate = date->addDays(1).addSecs(-1);
        }
    }
}

void SearchQueryParser::parseSizeRange(const QString& sizeStr, ParsedQuery& query) const
{
    QString str = sizeStr.trimmed();

    // Check for range: size1-size2
    int rangeIdx = str.indexOf('-');
    // Make sure it's not just a negative number
    if (rangeIdx > 0 && rangeIdx < str.length() - 1) {
        QString left = str.left(rangeIdx);
        QString right = str.mid(rangeIdx + 1);

        // Verify both parts look like sizes
        if (left.length() > 0 && right.length() > 0) {
            auto minSize = parseSize(left);
            auto maxSize = parseSize(right);

            if (minSize && maxSize) {
                query.minSize = minSize;
                query.maxSize = maxSize;
                return;
            }
        }
    }

    // Check for comparison operators
    if (str.startsWith('>')) {
        query.minSize = parseSize(str.mid(1));
    }
    else if (str.startsWith('<')) {
        query.maxSize = parseSize(str.mid(1));
    }
    else {
        // Exact size match (with small tolerance)
        auto size = parseSize(str);
        if (size) {
            // Allow 5% tolerance for "exact" match
            qint64 tolerance = *size / 20;
            query.minSize = *size - tolerance;
            query.maxSize = *size + tolerance;
        }
    }
}

QRegularExpression SearchQueryParser::wildcardToRegex(const QString& pattern) const
{
    QString regex = QRegularExpression::escape(pattern);

    // Replace escaped wildcards with regex equivalents
    regex.replace("\\*", ".*");
    regex.replace("\\?", ".");

    // Anchor the pattern
    regex = "^" + regex + "$";

    return QRegularExpression(regex, QRegularExpression::CaseInsensitiveOption);
}

bool SearchQueryParser::matchesWildcard(const QString& text, const QString& pattern) const
{
    // Simple wildcard matching without full regex
    // Supports * (any chars) and ? (single char)

    int ti = 0;  // text index
    int pi = 0;  // pattern index
    int starIdx = -1;
    int matchIdx = 0;

    while (ti < text.length()) {
        if (pi < pattern.length() && (pattern[pi] == '?' || pattern[pi] == text[ti])) {
            // Characters match or ? matches any
            ti++;
            pi++;
        }
        else if (pi < pattern.length() && pattern[pi] == '*') {
            // * matches any sequence
            starIdx = pi;
            matchIdx = ti;
            pi++;
        }
        else if (starIdx != -1) {
            // Backtrack to last * and try matching one more char
            pi = starIdx + 1;
            matchIdx++;
            ti = matchIdx;
        }
        else {
            // No match
            return false;
        }
    }

    // Check remaining pattern characters (must all be *)
    while (pi < pattern.length() && pattern[pi] == '*') {
        pi++;
    }

    return pi == pattern.length();
}

} // namespace MegaCustom
