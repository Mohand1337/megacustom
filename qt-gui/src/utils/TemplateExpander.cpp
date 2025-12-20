#include "TemplateExpander.h"
#include <QRegularExpression>
#include <QLocale>

namespace MegaCustom {

// === Variables Implementation ===

TemplateExpander::Variables TemplateExpander::Variables::fromMember(const MemberInfo& member) {
    Variables vars = withCurrentDateTime();
    vars.member = member.distributionFolder;
    vars.memberId = member.id;
    vars.memberName = member.displayName;
    return vars;
}

TemplateExpander::Variables TemplateExpander::Variables::withCurrentDateTime() {
    Variables vars;
    QDateTime now = QDateTime::currentDateTime();

    vars.month = QLocale::system().monthName(now.date().month());
    vars.monthNum = QString::number(now.date().month()).rightJustified(2, '0');
    vars.year = QString::number(now.date().year());
    vars.date = now.toString("yyyy-MM-dd");
    vars.timestamp = now.toString("yyyyMMdd_HHmmss");

    return vars;
}

// === Main Expansion Methods ===

QString TemplateExpander::expand(const QString& templatePath, const Variables& vars) {
    QString result = templatePath;

    // Member variables
    result.replace("{member}", vars.member);
    result.replace("{member_id}", vars.memberId);
    result.replace("{member_name}", vars.memberName);

    // Date/time variables
    result.replace("{month}", vars.month);
    result.replace("{month_num}", vars.monthNum);
    result.replace("{year}", vars.year);
    result.replace("{date}", vars.date);
    result.replace("{timestamp}", vars.timestamp);

    // Clean up any double slashes (except for protocol prefixes)
    static QRegularExpression doubleSlash("(?<!:)//+");
    result.replace(doubleSlash, "/");

    return result;
}

TemplateExpander::ExpansionResult TemplateExpander::expandForMember(
    const QString& templatePath, const MemberInfo& member)
{
    ExpansionResult result;
    result.memberId = member.id;
    result.memberName = member.displayName;
    result.originalTemplate = templatePath;
    result.isValid = true;

    // Check if member has a distribution folder
    if (!member.hasDistributionFolder()) {
        result.isValid = false;
        result.errorMessage = QString("Member '%1' has no distribution folder set")
            .arg(member.displayName);
        result.expandedPath = templatePath;
        return result;
    }

    // Expand the template
    Variables vars = Variables::fromMember(member);
    result.expandedPath = expand(templatePath, vars);

    return result;
}

QList<TemplateExpander::ExpansionResult> TemplateExpander::expandForMembers(
    const QString& templatePath, const QList<MemberInfo>& members)
{
    QList<ExpansionResult> results;
    results.reserve(members.size());

    for (const MemberInfo& member : members) {
        results.append(expandForMember(templatePath, member));
    }

    return results;
}

// === Variable Information ===

QStringList TemplateExpander::getAvailableVariables() {
    return {
        "member",
        "member_id",
        "member_name",
        "month",
        "month_num",
        "year",
        "date",
        "timestamp"
    };
}

QMap<QString, QString> TemplateExpander::getVariableDescriptions() {
    QMap<QString, QString> descriptions;
    descriptions["member"] = "Member's distribution folder path";
    descriptions["member_id"] = "Member's unique ID";
    descriptions["member_name"] = "Member's display name";
    descriptions["month"] = "Current month name (e.g., December)";
    descriptions["month_num"] = "Current month number (01-12)";
    descriptions["year"] = "Current year (e.g., 2025)";
    descriptions["date"] = "Current date (YYYY-MM-DD)";
    descriptions["timestamp"] = "Current timestamp (YYYYMMDD_HHMMSS)";
    return descriptions;
}

// === Validation & Analysis ===

bool TemplateExpander::hasVariables(const QString& templatePath) {
    static QRegularExpression varPattern("\\{[a-z_]+\\}");
    return templatePath.contains(varPattern);
}

bool TemplateExpander::hasMemberVariables(const QString& templatePath) {
    return templatePath.contains("{member}") ||
           templatePath.contains("{member_id}") ||
           templatePath.contains("{member_name}");
}

bool TemplateExpander::validateTemplate(const QString& templatePath, QString* error) {
    if (templatePath.isEmpty()) {
        if (error) *error = "Template path is empty";
        return false;
    }

    // Extract all variables used
    QStringList usedVars = extractVariables(templatePath);
    QStringList availableVars = getAvailableVariables();

    // Check for unknown variables
    for (const QString& var : usedVars) {
        if (!availableVars.contains(var)) {
            if (error) *error = QString("Unknown variable: {%1}").arg(var);
            return false;
        }
    }

    // Check for unclosed braces
    int openBraces = templatePath.count('{');
    int closeBraces = templatePath.count('}');
    if (openBraces != closeBraces) {
        if (error) *error = "Mismatched braces in template";
        return false;
    }

    return true;
}

QStringList TemplateExpander::extractVariables(const QString& templatePath) {
    QStringList variables;
    static QRegularExpression varPattern("\\{([a-z_]+)\\}");

    QRegularExpressionMatchIterator it = varPattern.globalMatch(templatePath);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString varName = match.captured(1);
        if (!variables.contains(varName)) {
            variables.append(varName);
        }
    }

    return variables;
}

} // namespace MegaCustom
