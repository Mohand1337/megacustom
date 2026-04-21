#include "ContentRouter.h"
#include "MemberRegistry.h"
#include <megaapi.h>
#include <QRegularExpression>
#include <QDir>
#include <QFileInfo>
#include <QDate>
#include <QDebug>
#include <memory>

namespace MegaCustom {

// ==================== Keyword Rules ====================

struct KeywordRule {
    ContentType type;
    QStringList keywords;
};

static QList<KeywordRule> builtinRules() {
    static QList<KeywordRule> rules = {
        { ContentType::HOT_SEATS,     { "hot seat", "hotseat", "hot-seat", "hotseats", "hot seats", "hot-seats" } },
        { ContentType::THEORY_CALLS,  { "theory call", "theory-call", "theorycall", "theory calls", "theory-calls", "theorycalls" } },
        { ContentType::FAST_FORWARD,  { "fast forward", "fast-forward", "fastforward" } },
    };
    return rules;
}

// ==================== Normalize ====================

QString ContentRouter::normalize(const QString& input) {
    QString s = input.toLower().trimmed();

    // Strip leading numbers, dots, dashes, underscores, spaces
    // e.g., "02-17-2026 FF Hot Seats" → "ff hot seats"
    // e.g., "3- Hotseats" → "hotseats"
    static QRegularExpression leadingJunk("^[\\d._\\-\\s]+");
    s.replace(leadingJunk, "");

    // Collapse multiple whitespace
    static QRegularExpression multiSpace("\\s+");
    s.replace(multiSpace, " ");

    return s.trimmed();
}

// ==================== Dynamic Match ====================

ContentType ContentRouter::dynamicMatch(
    const QString& folderName,
    const MemberInfo& member,
    QString* matchedKeyword) const
{
    QString normalizedFolder = normalize(folderName);
    if (normalizedFolder.isEmpty()) return ContentType::UNKNOWN;

    // Build candidate pairs: { normalized path value → ContentType }
    struct PathCandidate {
        QString value;
        ContentType type;
    };

    QList<PathCandidate> candidates;

    // Extract the last path segment for matching (e.g., "3- Hotseats" from full path)
    auto lastSegment = [](const QString& path) -> QString {
        if (path.isEmpty()) return {};
        int idx = path.lastIndexOf('/');
        return idx >= 0 ? path.mid(idx + 1) : path;
    };

    if (!member.paths.hotSeatsPath.isEmpty()) {
        candidates.append({ normalize(lastSegment(member.paths.hotSeatsPath)), ContentType::HOT_SEATS });
    }
    if (!member.paths.theoryCallsPath.isEmpty()) {
        candidates.append({ normalize(lastSegment(member.paths.theoryCallsPath)), ContentType::THEORY_CALLS });
    }
    if (!member.paths.fastForwardPath.isEmpty()) {
        candidates.append({ normalize(lastSegment(member.paths.fastForwardPath)), ContentType::FAST_FORWARD });
    }
    if (!member.paths.nhbCallsPath.isEmpty()) {
        // Also match against the last segment of NHB calls path
        candidates.append({ normalize(lastSegment(member.paths.nhbCallsPath)), ContentType::NHB_ROOT_FILES });
    }

    // Phase 1: Exact normalized match
    for (const auto& cand : candidates) {
        if (cand.value.isEmpty()) continue;
        if (normalizedFolder == cand.value) {
            if (matchedKeyword) *matchedKeyword = cand.value;
            return cand.type;
        }
    }

    // Phase 2: Contains match — folder name contains the path value or vice versa
    // More specific matches first (hot seats before fast forward)
    for (const auto& cand : candidates) {
        if (cand.value.isEmpty()) continue;
        // Skip single-word matches to avoid false positives
        if (cand.value.length() < 4) continue;

        if (normalizedFolder.contains(cand.value) || cand.value.contains(normalizedFolder)) {
            if (matchedKeyword) *matchedKeyword = cand.value;
            return cand.type;
        }
    }

    return ContentType::UNKNOWN;
}

// ==================== Keyword Match ====================

ContentType ContentRouter::keywordMatch(
    const QString& folderName,
    QString* matchedKeyword) const
{
    QString normalizedFolder = normalize(folderName);
    if (normalizedFolder.isEmpty()) return ContentType::UNKNOWN;

    // Also try with the full lowercase name (preserving date prefixes etc.)
    QString lowerName = folderName.toLower().trimmed();

    // Phase 1: Check specific types first (HOT_SEATS, THEORY_CALLS) — always take priority
    for (const auto& rule : builtinRules()) {
        if (rule.type == ContentType::FAST_FORWARD) continue;
        for (const QString& keyword : rule.keywords) {
            if (normalizedFolder.contains(keyword) || lowerName.contains(keyword)) {
                if (matchedKeyword) *matchedKeyword = keyword;
                return rule.type;
            }
        }
    }

    // Phase 2: Check generic FAST_FORWARD keywords
    for (const auto& rule : builtinRules()) {
        if (rule.type != ContentType::FAST_FORWARD) continue;
        for (const QString& keyword : rule.keywords) {
            if (normalizedFolder.contains(keyword) || lowerName.contains(keyword)) {
                if (matchedKeyword) *matchedKeyword = keyword;
                return rule.type;
            }
        }
    }

    // Phase 3: "FF" prefix heuristic (common abbreviation)
    // "FF Hot Seats" → HOT_SEATS, "FF Theory Call" → THEORY_CALLS, "FF Something" → FAST_FORWARD
    static QRegularExpression ffPrefix("\\bff\\b", QRegularExpression::CaseInsensitiveOption);
    if (ffPrefix.match(lowerName).hasMatch()) {
        // Specific subtypes already checked in Phase 1 — if we're here, none matched
        if (matchedKeyword) *matchedKeyword = "ff";
        return ContentType::FAST_FORWARD;
    }

    return ContentType::UNKNOWN;
}

// ==================== Classify Folder ====================

ContentType ContentRouter::classifyFolder(
    const QString& folderName,
    const MemberInfo& member,
    QString* matchedKeyword) const
{
    // Phase 1: Dynamic match against member's actual path values (highest confidence)
    ContentType result = dynamicMatch(folderName, member, matchedKeyword);
    if (result != ContentType::UNKNOWN) return result;

    // Phase 2: Keyword match against built-in rules
    result = keywordMatch(folderName, matchedKeyword);
    if (result != ContentType::UNKNOWN) return result;

    return ContentType::UNKNOWN;
}

// ==================== Classify Children ====================

QList<ContentRoute> ContentRouter::classifyChildren(
    mega::MegaApi* megaApi,
    const QString& memberFolderPath,
    const MemberInfo& member,
    const QString& month,
    const QString& fallbackDest) const
{
    QList<ContentRoute> routes;

    if (!megaApi) return routes;

    std::unique_ptr<mega::MegaNode> folderNode(
        megaApi->getNodeByPath(memberFolderPath.toUtf8().constData()));
    if (!folderNode || !folderNode->isFolder()) return routes;

    std::unique_ptr<mega::MegaNodeList> children(
        megaApi->getChildren(folderNode.get()));
    if (!children) return routes;

    // Collect root-level files
    QStringList rootFiles;
    QStringList rootFileNames;

    for (int i = 0; i < children->size(); ++i) {
        mega::MegaNode* child = children->get(i);
        if (!child) continue;

        QString childName = QString::fromUtf8(child->getName());
        QString childPath = memberFolderPath;
        if (!childPath.endsWith("/")) childPath += "/";
        childPath += childName;

        if (child->isFolder()) {
            // Classify the subfolder
            QString keyword;
            ContentType type = classifyFolder(childName, member, &keyword);

            ContentRoute route;
            route.sourcePath = childPath;
            route.childName = childName;
            route.isFolder = true;
            route.contentType = type;
            route.contentTypeLabel = contentTypeLabel(type);
            route.destinationPath = resolveDestination(type, member, month, fallbackDest);
            route.memberId = member.id;
            route.selected = true;

            qDebug() << "ContentRouter: Subfolder" << childName
                     << "→" << route.contentTypeLabel
                     << (keyword.isEmpty() ? "" : QString("(matched: %1)").arg(keyword));

            routes.append(route);
        } else {
            // Root-level file — collect for grouping
            rootFiles.append(childPath);
            rootFileNames.append(childName);
        }
    }

    // Group root-level files — by month if Auto mode, else single route
    if (!rootFiles.isEmpty()) {
        bool autoMonth = month.startsWith("Auto");

        if (autoMonth) {
            // Extract month from each file's date prefix and split accordingly
            static QRegularExpression dateRe("^(\\d{2})-\\d{2}-\\d{4}");
            QMap<QString, QStringList> filesByMonth;

            for (int idx = 0; idx < rootFiles.size(); ++idx) {
                QString monthKey = "Unknown";
                auto m = dateRe.match(rootFileNames[idx]);
                if (m.hasMatch()) {
                    int n = m.captured(1).toInt();
                    if (n >= 1 && n <= 12) {
                        monthKey = QDate(2000, n, 1).toString("MMMM");
                    }
                }
                filesByMonth[monthKey].append(rootFiles[idx]);
            }

            // Create one route per month
            for (auto it = filesByMonth.constBegin(); it != filesByMonth.constEnd(); ++it) {
                ContentRoute fileRoute;
                fileRoute.sourcePath = memberFolderPath;
                fileRoute.childName = QString("%1 files (%2)").arg(it.value().size()).arg(it.key());
                fileRoute.isFolder = false;
                fileRoute.contentType = ContentType::NHB_ROOT_FILES;
                fileRoute.contentTypeLabel = QString("NHB Files \xe2\x80\x94 %1").arg(it.key());
                fileRoute.destinationPath = resolveDestination(
                    ContentType::NHB_ROOT_FILES, member, it.key(), fallbackDest);
                fileRoute.memberId = member.id;
                fileRoute.selected = true;
                fileRoute.filePaths = it.value();

                qDebug() << "ContentRouter: Grouped" << it.value().size()
                         << "NHB files for" << it.key() << "→" << fileRoute.destinationPath;

                routes.prepend(fileRoute);
            }
        } else {
            // Single route with the specified month
            ContentRoute fileRoute;
            fileRoute.sourcePath = memberFolderPath;
            fileRoute.childName = QString("%1 root files").arg(rootFiles.size());
            fileRoute.isFolder = false;
            fileRoute.contentType = ContentType::NHB_ROOT_FILES;
            fileRoute.contentTypeLabel = contentTypeLabel(ContentType::NHB_ROOT_FILES);
            fileRoute.destinationPath = resolveDestination(
                ContentType::NHB_ROOT_FILES, member, month, fallbackDest);
            fileRoute.memberId = member.id;
            fileRoute.selected = true;
            fileRoute.filePaths = rootFiles;

            qDebug() << "ContentRouter: Grouped" << rootFiles.size()
                     << "root files → NHB Files (" << rootFileNames.join(", ").left(100) << "...)";

            routes.prepend(fileRoute);
        }
    }

    return routes;
}

// ==================== Classify Local Children ====================

QList<ContentRoute> ContentRouter::classifyLocalChildren(
    const QString& memberFolderPath,
    const MemberInfo& member,
    const QString& month,
    const QString& fallbackDest) const
{
    QList<ContentRoute> routes;

    QDir dir(memberFolderPath);
    if (!dir.exists()) return routes;

    QStringList rootFiles;
    QStringList rootFileNames;

    const QFileInfoList entries = dir.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QFileInfo& entry : entries) {
        const QString childName = entry.fileName();
        const QString childPath = entry.absoluteFilePath();

        if (entry.isDir()) {
            QString keyword;
            ContentType type = classifyFolder(childName, member, &keyword);

            ContentRoute route;
            route.sourcePath = childPath;
            route.childName = childName;
            route.isFolder = true;
            route.isLocalSource = true;
            route.contentType = type;
            route.contentTypeLabel = contentTypeLabel(type);
            route.destinationPath = resolveDestination(type, member, month, fallbackDest);
            route.memberId = member.id;
            route.selected = true;

            qDebug() << "ContentRouter[local]: Subfolder" << childName
                     << "\xe2\x86\x92" << route.contentTypeLabel
                     << (keyword.isEmpty() ? "" : QString("(matched: %1)").arg(keyword));

            routes.append(route);
        } else {
            rootFiles.append(childPath);
            rootFileNames.append(childName);
        }
    }

    if (!rootFiles.isEmpty()) {
        bool autoMonth = month.startsWith("Auto");

        if (autoMonth) {
            static QRegularExpression dateRe("^(\\d{2})-\\d{2}-\\d{4}");
            QMap<QString, QStringList> filesByMonth;

            for (int idx = 0; idx < rootFiles.size(); ++idx) {
                QString monthKey = "Unknown";
                auto m = dateRe.match(rootFileNames[idx]);
                if (m.hasMatch()) {
                    int n = m.captured(1).toInt();
                    if (n >= 1 && n <= 12) {
                        monthKey = QDate(2000, n, 1).toString("MMMM");
                    }
                }
                filesByMonth[monthKey].append(rootFiles[idx]);
            }

            for (auto it = filesByMonth.constBegin(); it != filesByMonth.constEnd(); ++it) {
                ContentRoute fileRoute;
                fileRoute.sourcePath = memberFolderPath;
                fileRoute.childName = QString("%1 files (%2)").arg(it.value().size()).arg(it.key());
                fileRoute.isFolder = false;
                fileRoute.isLocalSource = true;
                fileRoute.contentType = ContentType::NHB_ROOT_FILES;
                fileRoute.contentTypeLabel = QString("NHB Files \xe2\x80\x94 %1").arg(it.key());
                fileRoute.destinationPath = resolveDestination(
                    ContentType::NHB_ROOT_FILES, member, it.key(), fallbackDest);
                fileRoute.memberId = member.id;
                fileRoute.selected = true;
                fileRoute.localFilePaths = it.value();

                qDebug() << "ContentRouter[local]: Grouped" << it.value().size()
                         << "NHB files for" << it.key() << "\xe2\x86\x92" << fileRoute.destinationPath;

                routes.prepend(fileRoute);
            }
        } else {
            ContentRoute fileRoute;
            fileRoute.sourcePath = memberFolderPath;
            fileRoute.childName = QString("%1 root files").arg(rootFiles.size());
            fileRoute.isFolder = false;
            fileRoute.isLocalSource = true;
            fileRoute.contentType = ContentType::NHB_ROOT_FILES;
            fileRoute.contentTypeLabel = contentTypeLabel(ContentType::NHB_ROOT_FILES);
            fileRoute.destinationPath = resolveDestination(
                ContentType::NHB_ROOT_FILES, member, month, fallbackDest);
            fileRoute.memberId = member.id;
            fileRoute.selected = true;
            fileRoute.localFilePaths = rootFiles;

            qDebug() << "ContentRouter[local]: Grouped" << rootFiles.size() << "root files";

            routes.prepend(fileRoute);
        }
    }

    return routes;
}

// ==================== Resolve Destination ====================

QString ContentRouter::resolveDestination(
    ContentType type,
    const MemberInfo& member,
    const QString& month,
    const QString& fallbackDest)
{
    switch (type) {
    case ContentType::NHB_ROOT_FILES:
        if (!member.paths.archiveRoot.isEmpty() && !member.paths.nhbCallsPath.isEmpty()) {
            return member.paths.getMonthPath(month);
        }
        break;

    case ContentType::HOT_SEATS:
        if (!member.paths.archiveRoot.isEmpty() && !member.paths.fastForwardPath.isEmpty()
            && !member.paths.hotSeatsPath.isEmpty()) {
            return member.paths.getHotSeatsFullPath();
        }
        break;

    case ContentType::THEORY_CALLS:
        if (!member.paths.archiveRoot.isEmpty() && !member.paths.fastForwardPath.isEmpty()
            && !member.paths.theoryCallsPath.isEmpty()) {
            return member.paths.getTheoryCallsFullPath();
        }
        break;

    case ContentType::FAST_FORWARD:
        if (!member.paths.archiveRoot.isEmpty() && !member.paths.fastForwardPath.isEmpty()) {
            return member.paths.archiveRoot + "/" + member.paths.fastForwardPath;
        }
        break;

    case ContentType::UNKNOWN:
        break;
    }

    // Fallback to provided destination or distribution folder
    if (!fallbackDest.isEmpty()) return fallbackDest;
    if (!member.distributionFolder.isEmpty()) return member.distributionFolder;
    return {};
}

// ==================== Content Type Label ====================

QString ContentRouter::contentTypeLabel(ContentType type) {
    switch (type) {
    case ContentType::NHB_ROOT_FILES: return "NHB Files";
    case ContentType::HOT_SEATS:     return "Hot Seats";
    case ContentType::THEORY_CALLS:  return "Theory Calls";
    case ContentType::FAST_FORWARD:  return "Fast Forward";
    case ContentType::UNKNOWN:       return "Unknown";
    }
    return "Unknown";
}

} // namespace MegaCustom
