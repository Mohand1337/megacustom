#include "CloudSearchIndex.h"
#include "SearchQueryParser.h"
#include <QDebug>
#include <QFileInfo>
#include <algorithm>

namespace MegaCustom {

CloudSearchIndex::CloudSearchIndex(QObject* parent)
    : QObject(parent)
    , m_parser(std::make_unique<SearchQueryParser>())
{
}

CloudSearchIndex::~CloudSearchIndex() = default;

void CloudSearchIndex::clear()
{
    {
        QMutexLocker locker(&m_mutex);

        m_nodes.clear();
        m_handleIndex.clear();
        m_extensionIndex.clear();
        m_wordIndex.clear();
        m_folderCount = 0;
        m_totalSize = 0;
        m_isBuilding = false;
    }
    // Emit signal outside mutex to prevent deadlocks
    emit indexCleared();
}

void CloudSearchIndex::addNode(const QString& name, const QString& path, qint64 size,
                                qint64 created, qint64 modified, const QString& handle,
                                bool isFolder, int depth)
{
    bool shouldEmitStarted = false;
    bool shouldEmitProgress = false;
    int currentSize = 0;

    {
        QMutexLocker locker(&m_mutex);

        if (!m_isBuilding) {
            m_isBuilding = true;
            m_buildTimer.start();
            shouldEmitStarted = true;
        }

        // Create indexed node
        IndexedNode node;
        node.name = name;
        node.nameLower = name.toLower();
        node.path = path;
        node.pathLower = path.toLower();
        node.handle = handle;
        node.size = size;
        node.creationTime = created;
        node.modificationTime = modified;
        node.isFolder = isFolder;
        node.depth = depth;

        // Extract extension for files
        if (!isFolder) {
            int dotPos = name.lastIndexOf('.');
            if (dotPos > 0 && dotPos < name.length() - 1) {
                node.extension = name.mid(dotPos + 1).toLower();
            }
        }

        // Store node
        int nodeIndex = m_nodes.size();
        m_nodes.append(node);

        // Update handle index
        m_handleIndex.insert(handle, nodeIndex);

        // Update extension index
        if (!node.extension.isEmpty()) {
            m_extensionIndex[node.extension].append(nodeIndex);
        }

        // Update word index (for fast term lookup)
        // Split name into words for indexing
        QStringList words = node.nameLower.split(QRegularExpression("[\\s_\\-\\.]"),
                                                  Qt::SkipEmptyParts);
        for (const QString& word : words) {
            if (word.length() >= 2) { // Only index words with 2+ chars
                m_wordIndex.insert(word, nodeIndex);
            }
        }

        // Update statistics
        if (isFolder) {
            m_folderCount++;
        } else {
            m_totalSize += size;
        }

        // Check if we should emit progress
        currentSize = m_nodes.size();
        if (currentSize % 1000 == 0) {
            shouldEmitProgress = true;
        }
    }

    // Emit signals outside mutex to prevent deadlocks
    if (shouldEmitStarted) {
        emit indexingStarted();
    }
    if (shouldEmitProgress) {
        emit indexingProgress(currentSize, 0); // 0 = unknown total
    }
}

void CloudSearchIndex::removeNode(const QString& handle)
{
    QMutexLocker locker(&m_mutex);

    auto it = m_handleIndex.find(handle);
    if (it == m_handleIndex.end()) return;

    int index = it.value();
    const IndexedNode& node = m_nodes[index];

    // Update statistics
    if (node.isFolder) {
        m_folderCount--;
    } else {
        m_totalSize -= node.size;
    }

    // Remove from extension index
    if (!node.extension.isEmpty()) {
        m_extensionIndex[node.extension].removeOne(index);
    }

    // Remove from word index
    QStringList words = node.nameLower.split(QRegularExpression("[\\s_\\-\\.]"),
                                              Qt::SkipEmptyParts);
    for (const QString& word : words) {
        m_wordIndex.remove(word, index);
    }

    // Remove from handle index
    m_handleIndex.erase(it);

    // Mark node as deleted (we don't actually remove to preserve indices)
    // Instead clear the name to mark as deleted
    m_nodes[index].name.clear();
    m_nodes[index].handle.clear();
}

void CloudSearchIndex::updateNode(const QString& handle, const QString& newName, const QString& newPath)
{
    QMutexLocker locker(&m_mutex);

    auto it = m_handleIndex.find(handle);
    if (it == m_handleIndex.end()) return;

    int index = it.value();
    IndexedNode& node = m_nodes[index];

    // Remove old word index entries
    QStringList oldWords = node.nameLower.split(QRegularExpression("[\\s_\\-\\.]"),
                                                 Qt::SkipEmptyParts);
    for (const QString& word : oldWords) {
        m_wordIndex.remove(word, index);
    }

    // Remove from old extension index
    if (!node.extension.isEmpty()) {
        m_extensionIndex[node.extension].removeOne(index);
    }

    // Update node data
    node.name = newName;
    node.nameLower = newName.toLower();
    node.path = newPath;
    node.pathLower = newPath.toLower();

    // Update extension
    if (!node.isFolder) {
        int dotPos = newName.lastIndexOf('.');
        if (dotPos > 0 && dotPos < newName.length() - 1) {
            node.extension = newName.mid(dotPos + 1).toLower();
        } else {
            node.extension.clear();
        }
    }

    // Add new word index entries
    QStringList newWords = node.nameLower.split(QRegularExpression("[\\s_\\-\\.]"),
                                                 Qt::SkipEmptyParts);
    for (const QString& word : newWords) {
        if (word.length() >= 2) {
            m_wordIndex.insert(word, index);
        }
    }

    // Add to new extension index
    if (!node.extension.isEmpty()) {
        m_extensionIndex[node.extension].append(index);
    }
}

void CloudSearchIndex::finishBuilding()
{
    int nodeCount = 0;
    qint64 buildTimeMs = 0;
    int files = 0;
    int folders = 0;
    qint64 totalSizeMB = 0;

    {
        QMutexLocker locker(&m_mutex);

        m_isBuilding = false;
        m_lastBuildTimeMs = m_buildTimer.elapsed();

        nodeCount = m_nodes.size();
        buildTimeMs = m_lastBuildTimeMs;
        files = fileCount();
        folders = m_folderCount;
        totalSizeMB = m_totalSize / (1024*1024);
    }

    qDebug() << "CloudSearchIndex: Built index with" << nodeCount << "nodes in"
             << buildTimeMs << "ms";
    qDebug() << "  Files:" << files << "Folders:" << folders
             << "Total size:" << totalSizeMB << "MB";

    // Emit signal outside mutex to prevent deadlocks
    emit indexingFinished(nodeCount, buildTimeMs);
}

QVector<SearchResult> CloudSearchIndex::search(const QString& query, int maxResults) const
{
    return searchWithSort(query, SortField::Relevance, SortOrder::Descending, maxResults);
}

QVector<SearchResult> CloudSearchIndex::searchWithSort(const QString& query,
                                                        SortField sortBy,
                                                        SortOrder order,
                                                        int maxResults) const
{
    QElapsedTimer timer;
    timer.start();

    QVector<SearchResult> results;

    // Parse the query
    ParsedQuery parsed = m_parser->parse(query);

    // Empty query returns nothing (or could return all)
    if (parsed.isEmpty()) {
        m_lastSearchTimeMs = timer.elapsed();
        return results;
    }

    QMutexLocker locker(&m_mutex);

    // Search all nodes
    results.reserve(std::min(static_cast<qsizetype>(maxResults * 2), m_nodes.size()));

    for (int i = 0; i < m_nodes.size(); ++i) {
        const IndexedNode& node = m_nodes[i];

        // Skip deleted nodes
        if (node.name.isEmpty()) continue;

        // Check if matches
        if (m_parser->matches(node, parsed)) {
            // Calculate relevance score
            bool exactMatch = false;
            bool startsWithMatch = false;

            // Check for exact/starts-with matches on first term
            if (!parsed.terms.isEmpty()) {
                const QString& firstTerm = parsed.terms.first();
                exactMatch = (node.nameLower == firstTerm);
                startsWithMatch = node.nameLower.startsWith(firstTerm);
            }

            int score = calculateRelevance(node, parsed.terms.isEmpty() ? QString() : parsed.terms.first(),
                                           exactMatch, startsWithMatch);

            SearchResult result(&node, score);

            // Find match positions in name for highlighting
            for (const QString& term : parsed.terms) {
                if (term.isEmpty()) continue;
                int pos = 0;
                while ((pos = node.nameLower.indexOf(term, pos)) != -1) {
                    result.nameMatches.append(MatchSpan(pos, term.length()));
                    pos += term.length();
                }
            }

            results.append(result);
        }
    }

    // Sort results
    sortResults(results, sortBy, order);

    // Limit results
    if (results.size() > maxResults) {
        results.resize(maxResults);
    }

    m_lastSearchTimeMs = timer.elapsed();
    qDebug() << "CloudSearchIndex: Search for" << query << "found" << results.size()
             << "results in" << m_lastSearchTimeMs << "ms";

    return results;
}

const IndexedNode* CloudSearchIndex::getNodeByHandle(const QString& handle) const
{
    QMutexLocker locker(&m_mutex);

    auto it = m_handleIndex.find(handle);
    if (it != m_handleIndex.end()) {
        return &m_nodes[it.value()];
    }
    return nullptr;
}

QString CloudSearchIndex::getPathForHandle(const QString& handle) const
{
    const IndexedNode* node = getNodeByHandle(handle);
    return node ? node->path : QString();
}

int CloudSearchIndex::calculateRelevance(const IndexedNode& node, const QString& searchTerm,
                                          bool exactMatch, bool startsWithMatch) const
{
    int score = 0;

    // Exact name match is highest priority
    if (exactMatch) {
        score += 100;
    }
    // Name starts with search term
    else if (startsWithMatch) {
        score += 50;
    }
    // Name contains search term
    else if (!searchTerm.isEmpty() && node.nameLower.contains(searchTerm)) {
        score += 20;
    }

    // Boost for folders (often more important in navigation)
    if (node.isFolder) {
        score += 5;
    }

    // Boost for recent files (modified in last 7 days)
    qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 daysSinceModified = (now - node.modificationTime) / 86400;
    if (daysSinceModified < 7) {
        score += 10 - daysSinceModified;
    } else if (daysSinceModified < 30) {
        score += 3;
    }

    // Prefer shallower paths (less depth)
    if (node.depth < 3) {
        score += (3 - node.depth) * 2;
    }

    return score;
}

void CloudSearchIndex::sortResults(QVector<SearchResult>& results,
                                    SortField sortBy, SortOrder order) const
{
    auto comparator = [sortBy, order](const SearchResult& a, const SearchResult& b) -> bool {
        int cmp = 0;

        switch (sortBy) {
        case SortField::Relevance:
            cmp = a.relevanceScore - b.relevanceScore;
            break;

        case SortField::Name:
            // Use by-value fields (safe after mutex release)
            cmp = QString::compare(a.nameLower, b.nameLower, Qt::CaseInsensitive);
            break;

        case SortField::Size:
            if (a.size < b.size) cmp = -1;
            else if (a.size > b.size) cmp = 1;
            break;

        case SortField::DateModified:
            if (a.modificationTime < b.modificationTime) cmp = -1;
            else if (a.modificationTime > b.modificationTime) cmp = 1;
            break;

        case SortField::DateCreated:
            if (a.creationTime < b.creationTime) cmp = -1;
            else if (a.creationTime > b.creationTime) cmp = 1;
            break;

        case SortField::Type:
            // Folders first, then by extension
            if (a.isFolder != b.isFolder) {
                cmp = a.isFolder ? -1 : 1;
            } else {
                cmp = QString::compare(a.extension, b.extension, Qt::CaseInsensitive);
            }
            break;

        case SortField::Path:
            cmp = QString::compare(a.pathLower, b.pathLower, Qt::CaseInsensitive);
            break;
        }

        // Apply sort order
        if (order == SortOrder::Descending) {
            return cmp > 0;
        } else {
            return cmp < 0;
        }
    };

    std::sort(results.begin(), results.end(), comparator);
}

} // namespace MegaCustom
