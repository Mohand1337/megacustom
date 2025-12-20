#ifndef CLOUDSEARCHINDEX_H
#define CLOUDSEARCHINDEX_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QHash>
#include <QDateTime>
#include <QMutex>
#include <QElapsedTimer>
#include <memory>

namespace MegaCustom {

/**
 * Represents an indexed node from the MEGA cloud
 */
struct IndexedNode {
    QString name;           // File/folder name
    QString nameLower;      // Lowercase name for case-insensitive search
    QString path;           // Full path in cloud
    QString pathLower;      // Lowercase path
    QString extension;      // File extension (empty for folders)
    QString handle;         // MEGA node handle
    qint64 size;            // File size in bytes (0 for folders)
    qint64 creationTime;    // Creation timestamp (seconds since epoch)
    qint64 modificationTime; // Modification timestamp
    bool isFolder;          // True if folder
    int depth;              // Depth in tree (root = 0)
};

/**
 * Represents a highlighted match span within text
 */
struct MatchSpan {
    int start;   // Start position in text
    int length;  // Length of match

    MatchSpan() : start(0), length(0) {}
    MatchSpan(int s, int l) : start(s), length(l) {}
};

/**
 * Search result with relevance scoring
 * Stores node data by value to avoid pointer invalidation after mutex release
 */
struct SearchResult {
    // Store node data by value (copy) to prevent race conditions
    QString name;
    QString nameLower;
    QString path;
    QString pathLower;
    QString extension;
    QString handle;
    qint64 size;
    qint64 creationTime;
    qint64 modificationTime;
    bool isFolder;
    int depth;
    int relevanceScore;

    // Match highlighting data
    QVector<MatchSpan> nameMatches;  // Match positions in name

    // Pointer to source node (only valid while holding index mutex)
    // Use for internal operations only - do not use after search() returns
    const IndexedNode* node;

    // Default constructor
    SearchResult() : size(0), creationTime(0), modificationTime(0), isFolder(false), depth(0), relevanceScore(0), node(nullptr) {}

    // Constructor from IndexedNode
    SearchResult(const IndexedNode* n, int score)
        : name(n ? n->name : QString())
        , nameLower(n ? n->nameLower : QString())
        , path(n ? n->path : QString())
        , pathLower(n ? n->pathLower : QString())
        , extension(n ? n->extension : QString())
        , handle(n ? n->handle : QString())
        , size(n ? n->size : 0)
        , creationTime(n ? n->creationTime : 0)
        , modificationTime(n ? n->modificationTime : 0)
        , isFolder(n ? n->isFolder : false)
        , depth(n ? n->depth : 0)
        , relevanceScore(score)
        , node(n) {}

    bool operator<(const SearchResult& other) const {
        return relevanceScore > other.relevanceScore; // Higher score first
    }
};

/**
 * Sort options for search results
 */
enum class SortField {
    Relevance,
    Name,
    Size,
    DateModified,
    DateCreated,
    Type,
    Path
};

enum class SortOrder {
    Ascending,
    Descending
};

class SearchQueryParser;

/**
 * In-memory index for instant cloud file search
 *
 * This class maintains a searchable index of all MEGA cloud files
 * for sub-100ms search performance (similar to voidtools Everything)
 */
class CloudSearchIndex : public QObject
{
    Q_OBJECT

public:
    explicit CloudSearchIndex(QObject* parent = nullptr);
    ~CloudSearchIndex();

    // Index building
    void clear();
    void addNode(const QString& name, const QString& path, qint64 size,
                 qint64 created, qint64 modified, const QString& handle,
                 bool isFolder, int depth = 0);
    void removeNode(const QString& handle);
    void updateNode(const QString& handle, const QString& newName, const QString& newPath);
    void finishBuilding();

    // Search
    QVector<SearchResult> search(const QString& query, int maxResults = 1000) const;
    QVector<SearchResult> searchWithSort(const QString& query,
                                          SortField sortBy = SortField::Relevance,
                                          SortOrder order = SortOrder::Descending,
                                          int maxResults = 1000) const;

    // Lookup
    const IndexedNode* getNodeByHandle(const QString& handle) const;
    QString getPathForHandle(const QString& handle) const;

    // Statistics
    int nodeCount() const { return m_nodes.size(); }
    int folderCount() const { return m_folderCount; }
    int fileCount() const { return m_nodes.size() - m_folderCount; }
    qint64 totalSize() const { return m_totalSize; }
    bool isBuilding() const { return m_isBuilding; }
    qint64 lastBuildTimeMs() const { return m_lastBuildTimeMs; }
    qint64 lastSearchTimeMs() const { return m_lastSearchTimeMs; }

signals:
    void indexingStarted();
    void indexingProgress(int current, int total);
    void indexingFinished(int nodeCount, qint64 elapsedMs);
    void indexCleared();

private:
    // Calculate relevance score for a match
    int calculateRelevance(const IndexedNode& node, const QString& searchTerm,
                          bool exactMatch, bool startsWithMatch) const;

    // Sort results
    void sortResults(QVector<SearchResult>& results, SortField sortBy, SortOrder order) const;

    // Internal matching (used by SearchQueryParser)
    friend class SearchQueryParser;

private:
    // Main storage
    QVector<IndexedNode> m_nodes;

    // Hash indexes for fast lookup
    QHash<QString, int> m_handleIndex;      // handle -> node index
    QHash<QString, QVector<int>> m_extensionIndex; // extension -> node indices
    QMultiHash<QString, int> m_wordIndex;   // word -> node indices (for fast term lookup)

    // Statistics
    int m_folderCount = 0;
    qint64 m_totalSize = 0;
    bool m_isBuilding = false;
    qint64 m_lastBuildTimeMs = 0;
    mutable qint64 m_lastSearchTimeMs = 0;

    // Thread safety
    mutable QMutex m_mutex;

    // Query parser
    std::unique_ptr<SearchQueryParser> m_parser;

    // Build timer
    QElapsedTimer m_buildTimer;
};

} // namespace MegaCustom

#endif // CLOUDSEARCHINDEX_H
