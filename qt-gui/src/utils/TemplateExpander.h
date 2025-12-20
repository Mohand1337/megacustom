#ifndef MEGACUSTOM_TEMPLATEEXPANDER_H
#define MEGACUSTOM_TEMPLATEEXPANDER_H

#include <QString>
#include <QStringList>
#include <QList>
#include <QDateTime>
#include "MemberRegistry.h"

namespace MegaCustom {

/**
 * TemplateExpander - Utility class for expanding path templates with variables
 *
 * Supported variables:
 *   {member}       - Member's distribution folder path
 *   {member_id}    - Member's ID
 *   {member_name}  - Member's display name
 *   {month}        - Current month name (e.g., "December")
 *   {month_num}    - Current month number (e.g., "12")
 *   {year}         - Current year (e.g., "2025")
 *   {date}         - Current date (YYYY-MM-DD)
 *   {timestamp}    - Current timestamp (YYYYMMDD_HHMMSS)
 *
 * Example:
 *   Template: "/Archive/{member}/Updates/{month}/"
 *   Member with folder "/Members/Alice"
 *   Result: "/Archive/Members/Alice/Updates/December/"
 */
class TemplateExpander {
public:
    /**
     * Variables for template expansion
     */
    struct Variables {
        QString member;          // Distribution folder path
        QString memberId;        // Member ID
        QString memberName;      // Display name
        QString month;           // Month name
        QString monthNum;        // Month number (01-12)
        QString year;            // Year
        QString date;            // YYYY-MM-DD
        QString timestamp;       // YYYYMMDD_HHMMSS

        // Create from MemberInfo
        static Variables fromMember(const MemberInfo& member);

        // Create with current date/time (no member info)
        static Variables withCurrentDateTime();
    };

    /**
     * Result of template expansion for a single member
     */
    struct ExpansionResult {
        QString memberId;
        QString memberName;
        QString originalTemplate;
        QString expandedPath;
        bool isValid;
        QString errorMessage;
    };

    /**
     * Expand a template with the given variables
     * @param templatePath The template string with {variable} placeholders
     * @param vars The variables to substitute
     * @return The expanded string
     */
    static QString expand(const QString& templatePath, const Variables& vars);

    /**
     * Expand a template for a single member
     * @param templatePath The template string
     * @param member The member info
     * @return ExpansionResult with expanded path
     */
    static ExpansionResult expandForMember(const QString& templatePath, const MemberInfo& member);

    /**
     * Expand a template for all members with distribution folders
     * @param templatePath The template string
     * @param members List of members (typically from getMembersWithDistributionFolders())
     * @return List of expansion results, one per member
     */
    static QList<ExpansionResult> expandForMembers(const QString& templatePath,
                                                    const QList<MemberInfo>& members);

    /**
     * Get list of available template variables
     * @return List of variable names without braces
     */
    static QStringList getAvailableVariables();

    /**
     * Get list of available variables with descriptions (for UI)
     * @return Map of variable name to description
     */
    static QMap<QString, QString> getVariableDescriptions();

    /**
     * Check if a template string contains any variables
     * @param templatePath The template string to check
     * @return true if contains at least one {variable}
     */
    static bool hasVariables(const QString& templatePath);

    /**
     * Check if a template contains member-specific variables
     * @param templatePath The template string to check
     * @return true if contains {member}, {member_id}, or {member_name}
     */
    static bool hasMemberVariables(const QString& templatePath);

    /**
     * Validate a template string
     * @param templatePath The template string to validate
     * @param error Optional pointer to receive error message
     * @return true if valid
     */
    static bool validateTemplate(const QString& templatePath, QString* error = nullptr);

    /**
     * Extract variable names from a template
     * @param templatePath The template string
     * @return List of variable names found (without braces)
     */
    static QStringList extractVariables(const QString& templatePath);

private:
    TemplateExpander() = default; // Static utility class
};

} // namespace MegaCustom

#endif // MEGACUSTOM_TEMPLATEEXPANDER_H
