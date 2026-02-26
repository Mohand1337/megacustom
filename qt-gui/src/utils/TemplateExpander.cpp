#include "TemplateExpander.h"
#include "Constants.h"
#include <QRegularExpression>
#include <QLocale>

namespace MegaCustom {

// === Variables Implementation ===

TemplateExpander::Variables TemplateExpander::Variables::fromMember(const MemberInfo& member) {
    Variables vars = withCurrentDateTime();
    vars.brand = QString::fromUtf8(MegaCustom::Constants::BRAND_NAME);
    vars.member = member.distributionFolder;
    vars.memberId = member.id;
    vars.memberName = member.displayName;
    vars.memberEmail = member.email;
    vars.memberIp = member.ipAddress;
    vars.memberMac = member.macAddress;
    vars.memberSocial = member.socialHandle;

    // Member path variables
    vars.archiveRoot = member.paths.archiveRoot;
    vars.nhbCalls = member.paths.nhbCallsPath;
    vars.fastForward = member.paths.fastForwardPath;
    vars.theoryCalls = member.paths.theoryCallsPath;
    vars.hotSeats = member.paths.hotSeatsPath;

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

    // Brand
    result.replace("{brand}", vars.brand);

    // Member variables (longest first to avoid partial matches)
    result.replace("{member_social}", vars.memberSocial);
    result.replace("{member_email}", vars.memberEmail);
    result.replace("{member_name}", vars.memberName);
    result.replace("{member_mac}", vars.memberMac);
    result.replace("{member_ip}", vars.memberIp);
    result.replace("{member_id}", vars.memberId);
    result.replace("{member}", vars.member);

    // Member path variables (longest first)
    result.replace("{archive_root}", vars.archiveRoot);
    result.replace("{nhb_calls}", vars.nhbCalls);
    result.replace("{fast_forward}", vars.fastForward);
    result.replace("{theory_calls}", vars.theoryCalls);
    result.replace("{hot_seats}", vars.hotSeats);

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

    // Check if template uses {member} and member has no distribution folder
    if (templatePath.contains("{member}") && !member.hasDistributionFolder()) {
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
        "brand",
        "member",
        "member_id",
        "member_name",
        "member_email",
        "member_ip",
        "member_mac",
        "member_social",
        "archive_root",
        "nhb_calls",
        "fast_forward",
        "theory_calls",
        "hot_seats",
        "month",
        "month_num",
        "year",
        "date",
        "timestamp"
    };
}

QMap<QString, QString> TemplateExpander::getVariableDescriptions() {
    QMap<QString, QString> descriptions;
    descriptions["brand"] = "Brand name (e.g., Easygroupbuys.com)";
    descriptions["member"] = "Member's distribution folder path";
    descriptions["member_id"] = "Member's unique ID";
    descriptions["member_name"] = "Member's display name";
    descriptions["member_email"] = "Member's email address";
    descriptions["member_ip"] = "Member's IP address";
    descriptions["member_mac"] = "Member's MAC address";
    descriptions["member_social"] = "Member's social media handle";
    descriptions["archive_root"] = "Member's archive root path";
    descriptions["nhb_calls"] = "Member's NHB calls subpath";
    descriptions["fast_forward"] = "Member's Fast Forward subpath";
    descriptions["theory_calls"] = "Member's theory calls subpath";
    descriptions["hot_seats"] = "Member's hot seats subpath";
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
           templatePath.contains("{member_name}") ||
           templatePath.contains("{member_email}") ||
           templatePath.contains("{member_ip}") ||
           templatePath.contains("{member_mac}") ||
           templatePath.contains("{member_social}") ||
           templatePath.contains("{archive_root}") ||
           templatePath.contains("{nhb_calls}") ||
           templatePath.contains("{fast_forward}") ||
           templatePath.contains("{theory_calls}") ||
           templatePath.contains("{hot_seats}");
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
