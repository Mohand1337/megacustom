#include "controllers/FileController.h"
#include "core/MegaManager.h"
#include "accounts/AccountManager.h"
#include "operations/FolderManager.h"
#include "search/CloudSearchIndex.h"
#include "megaapi.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QtConcurrent>
#include <QMetaObject>
#include <QElapsedTimer>
#include <vector>
#include <string>
#include <functional>
#include <memory>

namespace MegaCustom {

// RAII wrapper for MegaNode to prevent memory leaks
struct MegaNodeDeleter {
    void operator()(mega::MegaNode* node) const {
        delete node;
    }
};
using MegaNodePtr = std::unique_ptr<mega::MegaNode, MegaNodeDeleter>;

// RAII wrapper for MegaNodeList to prevent memory leaks
struct MegaNodeListDeleter {
    void operator()(mega::MegaNodeList* list) const {
        delete list;
    }
};
using MegaNodeListPtr = std::unique_ptr<mega::MegaNodeList, MegaNodeListDeleter>;

// Helper function to get the active MegaApi from AccountManager, falling back to MegaManager
static mega::MegaApi* getDefaultMegaApi() {
    // First try AccountManager (for multi-account support)
    mega::MegaApi* api = AccountManager::instance().activeApi();
    if (api && api->isLoggedIn()) {
        return api;
    }

    // Fall back to legacy MegaManager singleton
    MegaManager& manager = MegaManager::getInstance();
    return manager.getMegaApi();
}

FileController::FileController(void* api)
    : QObject()
    , m_megaApi(api)
    , m_currentLocalPath(QDir::homePath())
    , m_currentRemotePath("/")
{
    if (api) {
        mega::MegaApi* megaApi = static_cast<mega::MegaApi*>(api);
        const char* email = megaApi->getMyEmail();
        qDebug() << "FileController constructed with specific MegaApi for:" << (email ? email : "unknown");
    } else {
        qDebug() << "FileController constructed (using default/active account)";
    }
}

// Helper to get the MegaApi this controller should use
mega::MegaApi* FileController_getMegaApi(void* storedApi) {
    if (storedApi) {
        return static_cast<mega::MegaApi*>(storedApi);
    }
    return getDefaultMegaApi();
}

QString FileController::currentLocalPath() const {
    return m_currentLocalPath;
}

QString FileController::currentRemotePath() const {
    return m_currentRemotePath;
}

void FileController::navigateToLocal(const QString& path) {
    qDebug() << "Navigate to local:" << path;
    m_currentLocalPath = path;
    emit localPathChanged(path);
}

void FileController::navigateToRemote(const QString& path) {
    qDebug() << "Navigate to remote:" << path;
    m_currentRemotePath = path;
    emit remotePathChanged(path);
}

void FileController::refreshRemote(const QString& path) {
    qDebug() << "Refreshing remote path:" << path;

    QString targetPath = path.isEmpty() ? "/" : path;
    m_currentRemotePath = targetPath;

    // Emit loading started signal immediately (UI thread)
    emit loadingStarted(targetPath);

    // Capture the stored API pointer for use in background thread
    void* storedApi = m_megaApi;

    // Run file listing in background thread to avoid UI freeze
    (void)QtConcurrent::run([this, targetPath, storedApi]() {
        // Get the MegaApi for this controller (specific or default)
        mega::MegaApi* megaApi = FileController_getMegaApi(storedApi);

        if (!megaApi) {
            qDebug() << "Error: MegaApi not initialized";
            QMetaObject::invokeMethod(this, [this]() {
                emit loadingError("MegaApi not initialized");
                emit operationFailed("MegaApi not initialized");
                emit loadingFinished();
            }, Qt::QueuedConnection);
            return;
        }

        // Check if logged in
        int loginStatus = megaApi->isLoggedIn();
        qDebug() << "Login status:" << loginStatus;

        if (loginStatus <= 0) {
            qDebug() << "Error: Not logged in";
            QMetaObject::invokeMethod(this, [this]() {
                emit loadingError("Not logged in");
                emit operationFailed("Not logged in");
                emit loadingFinished();
            }, Qt::QueuedConnection);
            return;
        }

        // Debug: Show account info
        const char* email = megaApi->getMyEmail();
        qDebug() << "Logged in as:" << (email ? email : "unknown");

        // Get the folder node
        mega::MegaNode* folderNode = nullptr;

        if (targetPath == "/") {
            folderNode = megaApi->getRootNode();
            qDebug() << "Using getRootNode() for root path";
        } else {
            folderNode = megaApi->getNodeByPath(targetPath.toStdString().c_str());
        }

        if (!folderNode) {
            qDebug() << "Error: Folder not found:" << targetPath;
            QMetaObject::invokeMethod(this, [this]() {
                emit loadingError("Folder not found");
                emit operationFailed("Folder not found");
                emit loadingFinished();
            }, Qt::QueuedConnection);
            return;
        }

        qDebug() << "Got folder node - handle:" << folderNode->getHandle()
                 << "name:" << folderNode->getName()
                 << "isFolder:" << folderNode->isFolder();

        // Get children
        mega::MegaNodeList* children = megaApi->getChildren(folderNode);
        QVariantList files;

        if (children) {
            qDebug() << "Found" << children->size() << "items in" << targetPath;
            for (int i = 0; i < children->size(); i++) {
                mega::MegaNode* node = children->get(i);
                if (node) {
                    QVariantMap fileInfo;
                    QString nodeName = QString::fromUtf8(node->getName());
                    fileInfo["name"] = nodeName;
                    fileInfo["path"] = (targetPath == "/" ? "/" : targetPath + "/") + nodeName;
                    fileInfo["size"] = static_cast<qint64>(node->getSize());
                    fileInfo["modified"] = static_cast<qint64>(node->getModificationTime());
                    fileInfo["isFolder"] = node->isFolder();
                    fileInfo["handle"] = QString::number(node->getHandle());
                    files.append(fileInfo);

                    qDebug() << "  -" << node->getName() << (node->isFolder() ? "(folder)" : "(file)");
                }
            }
            delete children;
        } else {
            qDebug() << "getChildren returned null for" << targetPath;
        }

        delete folderNode;

        // Emit signals back on UI thread
        qDebug() << "Emitting fileListReceived with" << files.size() << "items";
        QMetaObject::invokeMethod(this, [this, files]() {
            emit fileListReceived(files);
            emit remoteListUpdated();
            emit loadingFinished();
        }, Qt::QueuedConnection);
    });
}

void FileController::createRemoteFolder(const QString& name) {
    qDebug() << "Creating remote folder:" << name;

    mega::MegaApi* megaApi = FileController_getMegaApi(m_megaApi);

    if (!megaApi || !megaApi->isLoggedIn()) {
        qDebug() << "Error: Not logged in";
        emit operationFailed("Not logged in");
        return;
    }

    FolderManager folderManager(megaApi);

    // Determine full path - if name is already absolute path, use it directly
    QString fullPath;
    if (name.startsWith("/")) {
        fullPath = name;
    } else {
        // Combine current path with new folder name
        fullPath = m_currentRemotePath;
        if (!fullPath.endsWith("/")) fullPath += "/";
        fullPath += name;
    }

    // Normalize path - remove double slashes
    while (fullPath.contains("//")) {
        fullPath.replace("//", "/");
    }

    qDebug() << "Creating folder at path:" << fullPath;

    auto result = folderManager.createFolder(fullPath.toStdString(), true);

    if (result.success) {
        qDebug() << "Folder created successfully:" << fullPath;
        // Refresh the current directory to show the new folder
        refreshRemote(m_currentRemotePath);
        emit remoteListUpdated();
    } else {
        qDebug() << "Failed to create folder:" << QString::fromStdString(result.errorMessage);
        emit operationFailed(QString::fromStdString(result.errorMessage));
    }
}

void FileController::createRemoteFile(const QString& name) {
    qDebug() << "Creating remote file:" << name;

    mega::MegaApi* megaApi = FileController_getMegaApi(m_megaApi);

    if (!megaApi || !megaApi->isLoggedIn()) {
        qDebug() << "Error: Not logged in";
        emit operationFailed("Not logged in");
        return;
    }

    // Create a temporary empty file
    QString tempPath = QDir::tempPath() + "/" + name;
    QFile tempFile(tempPath);
    if (!tempFile.open(QIODevice::WriteOnly)) {
        qDebug() << "Error: Could not create temp file:" << tempPath;
        emit operationFailed("Could not create temporary file");
        return;
    }
    tempFile.close();

    // Get the parent node for upload destination
    mega::MegaNode* parentNode = nullptr;
    if (m_currentRemotePath == "/") {
        parentNode = megaApi->getRootNode();
    } else {
        parentNode = megaApi->getNodeByPath(m_currentRemotePath.toStdString().c_str());
    }

    if (!parentNode) {
        qDebug() << "Error: Parent folder not found:" << m_currentRemotePath;
        QFile::remove(tempPath);
        emit operationFailed("Parent folder not found");
        return;
    }

    // Upload the empty file
    // Parameters: localPath, parent, fileName, mtime, appData, isSourceTemporary, startFirst, cancelToken, listener
    megaApi->startUpload(
        tempPath.toStdString().c_str(),  // localPath
        parentNode,                       // parent node
        name.toStdString().c_str(),      // fileName
        0,                               // mtime (0 = use current time)
        nullptr,                         // appData
        true,                            // isSourceTemporary (delete temp file after upload)
        false,                           // startFirst
        nullptr,                         // cancelToken
        nullptr                          // listener
    );

    delete parentNode;

    // The temp file will be cleaned up after upload completes
    // For now we emit success - the upload listener will handle completion
    qDebug() << "Empty file upload started:" << name;
    emit remoteListUpdated();
}

void FileController::deleteRemote(const QString& path) {
    qDebug() << "Deleting remote:" << path;

    mega::MegaApi* megaApi = FileController_getMegaApi(m_megaApi);

    if (!megaApi || !megaApi->isLoggedIn()) {
        qDebug() << "Error: Not logged in";
        emit operationFailed("Not logged in");
        return;
    }

    FolderManager folderManager(megaApi);

    // Use the full path if provided, otherwise combine with current path
    QString fullPath = path.startsWith("/") ? path : m_currentRemotePath + "/" + path;

    auto result = folderManager.deleteFolder(fullPath.toStdString(), true);  // Move to trash

    if (result.success) {
        qDebug() << "Item deleted successfully:" << fullPath;
        emit remoteListUpdated();
    } else {
        qDebug() << "Failed to delete:" << QString::fromStdString(result.errorMessage);
        emit operationFailed(QString::fromStdString(result.errorMessage));
    }
}

void FileController::renameRemote(const QString& oldPath, const QString& newName) {
    qDebug() << "Renaming remote:" << oldPath << "to" << newName;

    mega::MegaApi* megaApi = FileController_getMegaApi(m_megaApi);

    if (!megaApi || !megaApi->isLoggedIn()) {
        qDebug() << "Error: Not logged in";
        emit operationFailed("Not logged in");
        return;
    }

    FolderManager folderManager(megaApi);

    // Use the full path if provided, otherwise combine with current path
    QString fullPath = oldPath.startsWith("/") ? oldPath : m_currentRemotePath + "/" + oldPath;

    auto result = folderManager.renameFolder(fullPath.toStdString(), newName.toStdString());

    if (result.success) {
        qDebug() << "Item renamed successfully:" << fullPath << "->" << newName;
        emit remoteListUpdated();
    } else {
        qDebug() << "Failed to rename:" << QString::fromStdString(result.errorMessage);
        emit operationFailed(QString::fromStdString(result.errorMessage));
    }
}

void FileController::searchRemote(const QString& query) {
    qDebug() << "Searching remote for:" << query;

    mega::MegaApi* megaApi = FileController_getMegaApi(m_megaApi);

    if (!megaApi || !megaApi->isLoggedIn()) {
        qDebug() << "Error: Not logged in";
        emit operationFailed("Not logged in");
        return;
    }

    emit loadingStarted(query);

    // Capture the API pointer for use in the background thread (use specific API if bound)
    mega::MegaApi* capturedApi = megaApi;
    // Run search in background thread
    (void)QtConcurrent::run([this, query, capturedApi]() {
        mega::MegaApi* megaApi = capturedApi;
        // Get root node for search
        mega::MegaNode* rootNode = megaApi->getRootNode();
        if (!rootNode) {
            QMetaObject::invokeMethod(this, [this]() {
                emit loadingError("Could not get root node");
                emit loadingFinished();
            }, Qt::QueuedConnection);
            return;
        }

        // Perform search
        mega::MegaSearchFilter* filter = mega::MegaSearchFilter::createInstance();
        filter->byName(query.toStdString().c_str());
        mega::MegaNodeList* searchResults = megaApi->search(filter);

        QVariantList results;

        if (searchResults) {
            qDebug() << "Found" << searchResults->size() << "results for:" << query;
            for (int i = 0; i < searchResults->size(); i++) {
                mega::MegaNode* node = searchResults->get(i);
                if (node) {
                    QVariantMap fileInfo;
                    QString nodeName = QString::fromUtf8(node->getName());

                    // Build full path
                    const char* pathStr = megaApi->getNodePath(node);
                    QString path = pathStr ? QString::fromUtf8(pathStr) : nodeName;
                    delete[] pathStr;

                    fileInfo["name"] = nodeName;
                    fileInfo["path"] = path;
                    fileInfo["size"] = static_cast<qint64>(node->getSize());
                    fileInfo["modified"] = static_cast<qint64>(node->getModificationTime());
                    fileInfo["isFolder"] = node->isFolder();
                    fileInfo["handle"] = QString::number(node->getHandle());
                    results.append(fileInfo);
                }
            }
            delete searchResults;
        }

        delete filter;
        delete rootNode;

        // Emit results back on UI thread
        QMetaObject::invokeMethod(this, [this, results]() {
            emit searchResultsReceived(results);
            emit loadingFinished();
        }, Qt::QueuedConnection);
    });
}

void FileController::getStorageInfo() {
    qDebug() << "Getting storage info...";

    mega::MegaApi* megaApi = FileController_getMegaApi(m_megaApi);

    if (!megaApi || !megaApi->isLoggedIn()) {
        qDebug() << "Error: Not logged in for storage info";
        return;
    }

    // Capture the API pointer for use in the background thread (use specific API if bound)
    mega::MegaApi* capturedApi = megaApi;
    // Run in background thread
    (void)QtConcurrent::run([this, capturedApi]() {
        mega::MegaApi* megaApi = capturedApi;
        // Calculate storage used from root node recursively
        mega::MegaNode* rootNode = megaApi->getRootNode();
        qint64 used = 0;
        if (rootNode) {
            used = static_cast<qint64>(megaApi->getSize(rootNode));
            delete rootNode;
        }

        // Get total storage from account details
        // For now, check if pro account (larger quota) or free (20GB)
        // Note: getAccountDetails requires async callback for accurate data
        qint64 total = 20LL * 1024 * 1024 * 1024;  // 20GB default (free tier)

        // If used > 20GB, likely a pro account - estimate based on usage
        if (used > total) {
            // Round up to next tier estimate
            if (used > 400LL * 1024 * 1024 * 1024) {
                total = 2048LL * 1024 * 1024 * 1024;  // 2TB
            } else if (used > 200LL * 1024 * 1024 * 1024) {
                total = 400LL * 1024 * 1024 * 1024;   // 400GB
            } else {
                total = 200LL * 1024 * 1024 * 1024;   // 200GB
            }
        }

        qDebug() << "Storage info - used:" << used << "total:" << total;

        // Emit back on UI thread
        QMetaObject::invokeMethod(this, [this, used, total]() {
            emit storageInfoReceived(used, total);
        }, Qt::QueuedConnection);
    });
}

void FileController::buildSearchIndex(CloudSearchIndex* index) {
    if (!index) {
        qDebug() << "Error: Search index is null";
        return;
    }

    qDebug() << "Building search index...";

    mega::MegaApi* megaApi = FileController_getMegaApi(m_megaApi);

    if (!megaApi || !megaApi->isLoggedIn()) {
        qDebug() << "Error: Not logged in for index building";
        emit operationFailed("Not logged in");
        return;
    }

    emit searchIndexBuildStarted();

    // Capture the API pointer for use in the background thread (use specific API if bound)
    mega::MegaApi* capturedApi = megaApi;
    // Run in background thread to avoid blocking UI
    (void)QtConcurrent::run([this, index, capturedApi]() {
        mega::MegaApi* megaApi = capturedApi;
        QElapsedTimer timer;
        timer.start();

        // Clear any existing index
        index->clear();

        // Get root node with RAII for automatic cleanup
        MegaNodePtr rootNode(megaApi->getRootNode());
        if (!rootNode) {
            qDebug() << "Error: Could not get root node for index building";
            QMetaObject::invokeMethod(this, [this]() {
                emit operationFailed("Could not get root node");
            }, Qt::QueuedConnection);
            return;
        }

        int nodeCount = 0;
        int lastProgress = 0;

        // Recursive function to traverse all nodes
        // Note: MegaNodeList owns its child nodes, so we only need to delete the list
        std::function<void(mega::MegaNode*, const QString&, int)> traverseNode;
        traverseNode = [&](mega::MegaNode* parentNode, const QString& parentPath, int depth) {
            // Use RAII for children list - automatically deleted when scope exits
            MegaNodeListPtr children(megaApi->getChildren(parentNode));
            if (!children) return;

            for (int i = 0; i < children->size(); i++) {
                mega::MegaNode* node = children->get(i);
                if (!node) continue;

                QString nodeName = QString::fromUtf8(node->getName());
                QString nodePath = parentPath.isEmpty() ? "/" + nodeName :
                                   (parentPath == "/" ? "/" + nodeName : parentPath + "/" + nodeName);

                // Add to index
                index->addNode(
                    nodeName,
                    nodePath,
                    static_cast<qint64>(node->getSize()),
                    static_cast<qint64>(node->getCreationTime()),
                    static_cast<qint64>(node->getModificationTime()),
                    QString::number(node->getHandle()),
                    node->isFolder(),
                    depth
                );

                nodeCount++;

                // Emit progress every 100 nodes
                if (nodeCount - lastProgress >= 100) {
                    lastProgress = nodeCount;
                    QMetaObject::invokeMethod(this, [this, nodeCount]() {
                        emit searchIndexBuildProgress(nodeCount);
                    }, Qt::QueuedConnection);
                }

                // Recurse into folders
                if (node->isFolder()) {
                    traverseNode(node, nodePath, depth + 1);
                }
            }
            // children is automatically deleted here by MegaNodeListPtr
        };

        // Start traversal from root
        traverseNode(rootNode.get(), "", 0);
        // rootNode is automatically deleted here by MegaNodePtr

        qint64 elapsed = timer.elapsed();
        qDebug() << "Search index built:" << nodeCount << "nodes indexed in" << elapsed << "ms";

        // Finalize index building (builds secondary indexes)
        index->finishBuilding();

        // Emit completion on UI thread
        QMetaObject::invokeMethod(this, [this, nodeCount]() {
            emit searchIndexBuildCompleted(nodeCount);
        }, Qt::QueuedConnection);
    });
}

} // namespace MegaCustom

#include "moc_FileController.cpp"