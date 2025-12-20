/**
 * FileController implementation
 * Handles file system navigation and operations for both local and remote files
 */

#include "FileController.h"
#include "accounts/AccountManager.h"
#include "operations/FolderManager.h"
#include "search/CloudSearchIndex.h"
#include "megaapi.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QDirIterator>
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

// Helper function to get the active MegaApi from AccountManager
static mega::MegaApi* getDefaultMegaApi() {
    mega::MegaApi* api = AccountManager::instance().activeApi();
    if (api && api->isLoggedIn()) {
        return api;
    }
    return nullptr;
}

// Helper to get the MegaApi this controller should use
static mega::MegaApi* getMegaApiForController(void* storedApi) {
    if (storedApi) {
        return static_cast<mega::MegaApi*>(storedApi);
    }
    return getDefaultMegaApi();
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

    emit loadingStarted(targetPath);

    void* storedApi = m_megaApi;

    QtConcurrent::run([this, targetPath, storedApi]() {
        mega::MegaApi* megaApi = getMegaApiForController(storedApi);

        if (!megaApi) {
            QMetaObject::invokeMethod(this, [this]() {
                emit loadingError("MegaApi not initialized");
                emit operationFailed("MegaApi not initialized");
                emit loadingFinished();
            }, Qt::QueuedConnection);
            return;
        }

        if (megaApi->isLoggedIn() <= 0) {
            QMetaObject::invokeMethod(this, [this]() {
                emit loadingError("Not logged in");
                emit operationFailed("Not logged in");
                emit loadingFinished();
            }, Qt::QueuedConnection);
            return;
        }

        MegaNodePtr folderNode;
        if (targetPath == "/") {
            folderNode.reset(megaApi->getRootNode());
        } else {
            folderNode.reset(megaApi->getNodeByPath(targetPath.toUtf8().constData()));
        }

        if (!folderNode) {
            QMetaObject::invokeMethod(this, [this]() {
                emit loadingError("Folder not found");
                emit operationFailed("Folder not found");
                emit loadingFinished();
            }, Qt::QueuedConnection);
            return;
        }

        MegaNodeListPtr children(megaApi->getChildren(folderNode.get()));
        QVariantList files;

        if (children) {
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
                }
            }
        }

        QMetaObject::invokeMethod(this, [this, files]() {
            emit fileListReceived(files);
            emit remoteListUpdated();
            emit loadingFinished();
        }, Qt::QueuedConnection);
    });
}

void FileController::createRemoteFolder(const QString& name) {
    qDebug() << "Creating remote folder:" << name;

    mega::MegaApi* megaApi = getMegaApiForController(m_megaApi);

    if (!megaApi || !megaApi->isLoggedIn()) {
        emit operationFailed("Not logged in");
        return;
    }

    FolderManager folderManager(megaApi);

    QString fullPath;
    if (name.startsWith("/")) {
        fullPath = name;
    } else {
        fullPath = m_currentRemotePath;
        if (!fullPath.endsWith("/")) fullPath += "/";
        fullPath += name;
    }

    while (fullPath.contains("//")) {
        fullPath.replace("//", "/");
    }

    auto result = folderManager.createFolder(fullPath.toStdString(), true);

    if (result.success) {
        refreshRemote(m_currentRemotePath);
        emit remoteListUpdated();
    } else {
        emit operationFailed(QString::fromStdString(result.errorMessage));
    }
}

void FileController::createRemoteFile(const QString& name) {
    qDebug() << "Creating remote file:" << name;

    mega::MegaApi* megaApi = getMegaApiForController(m_megaApi);

    if (!megaApi || !megaApi->isLoggedIn()) {
        emit operationFailed("Not logged in");
        return;
    }

    QString tempPath = QDir::tempPath() + "/" + name;
    QFile tempFile(tempPath);
    if (!tempFile.open(QIODevice::WriteOnly)) {
        emit operationFailed("Could not create temporary file");
        return;
    }
    tempFile.close();

    MegaNodePtr parentNode;
    if (m_currentRemotePath == "/") {
        parentNode.reset(megaApi->getRootNode());
    } else {
        parentNode.reset(megaApi->getNodeByPath(m_currentRemotePath.toUtf8().constData()));
    }

    if (!parentNode) {
        QFile::remove(tempPath);
        emit operationFailed("Parent folder not found");
        return;
    }

    megaApi->startUpload(
        tempPath.toUtf8().constData(),
        parentNode.get(),
        name.toUtf8().constData(),
        0,
        nullptr,
        true,  // isSourceTemporary - delete temp file after upload
        false,
        nullptr,
        nullptr
    );

    emit remoteListUpdated();
}

void FileController::deleteRemote(const QString& path) {
    qDebug() << "Deleting remote:" << path;

    mega::MegaApi* megaApi = getMegaApiForController(m_megaApi);

    if (!megaApi || !megaApi->isLoggedIn()) {
        emit operationFailed("Not logged in");
        return;
    }

    FolderManager folderManager(megaApi);

    QString fullPath = path.startsWith("/") ? path : m_currentRemotePath + "/" + path;
    auto result = folderManager.deleteFolder(fullPath.toStdString(), true);

    if (result.success) {
        emit remoteListUpdated();
    } else {
        emit operationFailed(QString::fromStdString(result.errorMessage));
    }
}

void FileController::renameRemote(const QString& oldPath, const QString& newName) {
    qDebug() << "Renaming remote:" << oldPath << "to" << newName;

    mega::MegaApi* megaApi = getMegaApiForController(m_megaApi);

    if (!megaApi || !megaApi->isLoggedIn()) {
        emit operationFailed("Not logged in");
        return;
    }

    FolderManager folderManager(megaApi);

    QString fullPath = oldPath.startsWith("/") ? oldPath : m_currentRemotePath + "/" + oldPath;
    auto result = folderManager.renameFolder(fullPath.toStdString(), newName.toStdString());

    if (result.success) {
        emit remoteListUpdated();
    } else {
        emit operationFailed(QString::fromStdString(result.errorMessage));
    }
}

void FileController::searchRemote(const QString& query) {
    qDebug() << "Searching remote for:" << query;

    mega::MegaApi* megaApi = getMegaApiForController(m_megaApi);

    if (!megaApi || !megaApi->isLoggedIn()) {
        emit operationFailed("Not logged in");
        return;
    }

    emit loadingStarted(query);

    mega::MegaApi* capturedApi = megaApi;

    QtConcurrent::run([this, query, capturedApi]() {
        MegaNodePtr rootNode(capturedApi->getRootNode());
        if (!rootNode) {
            QMetaObject::invokeMethod(this, [this]() {
                emit loadingError("Could not get root node");
                emit loadingFinished();
            }, Qt::QueuedConnection);
            return;
        }

        mega::MegaSearchFilter* filter = mega::MegaSearchFilter::createInstance();
        filter->byName(query.toUtf8().constData());
        MegaNodeListPtr searchResults(capturedApi->search(filter));

        QVariantList results;

        if (searchResults) {
            for (int i = 0; i < searchResults->size(); i++) {
                mega::MegaNode* node = searchResults->get(i);
                if (node) {
                    QVariantMap fileInfo;
                    QString nodeName = QString::fromUtf8(node->getName());

                    const char* pathStr = capturedApi->getNodePath(node);
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
        }

        delete filter;

        QMetaObject::invokeMethod(this, [this, results]() {
            emit searchResultsReceived(results);
            emit loadingFinished();
        }, Qt::QueuedConnection);
    });
}

void FileController::getStorageInfo() {
    qDebug() << "Getting storage info...";

    mega::MegaApi* megaApi = getMegaApiForController(m_megaApi);

    if (!megaApi || !megaApi->isLoggedIn()) {
        return;
    }

    mega::MegaApi* capturedApi = megaApi;

    QtConcurrent::run([this, capturedApi]() {
        MegaNodePtr rootNode(capturedApi->getRootNode());
        qint64 used = 0;
        if (rootNode) {
            used = static_cast<qint64>(capturedApi->getSize(rootNode.get()));
        }

        // Default to free tier (20GB), estimate higher tiers based on usage
        qint64 total = 20LL * 1024 * 1024 * 1024;
        if (used > total) {
            if (used > 400LL * 1024 * 1024 * 1024) {
                total = 2048LL * 1024 * 1024 * 1024;
            } else if (used > 200LL * 1024 * 1024 * 1024) {
                total = 400LL * 1024 * 1024 * 1024;
            } else {
                total = 200LL * 1024 * 1024 * 1024;
            }
        }

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

    mega::MegaApi* megaApi = getMegaApiForController(m_megaApi);

    if (!megaApi || !megaApi->isLoggedIn()) {
        emit operationFailed("Not logged in");
        return;
    }

    emit searchIndexBuildStarted();

    mega::MegaApi* capturedApi = megaApi;

    QtConcurrent::run([this, index, capturedApi]() {
        QElapsedTimer timer;
        timer.start();

        index->clear();

        MegaNodePtr rootNode(capturedApi->getRootNode());
        if (!rootNode) {
            QMetaObject::invokeMethod(this, [this]() {
                emit operationFailed("Could not get root node");
            }, Qt::QueuedConnection);
            return;
        }

        int nodeCount = 0;
        int lastProgress = 0;

        std::function<void(mega::MegaNode*, const QString&, int)> traverseNode;
        traverseNode = [&](mega::MegaNode* parentNode, const QString& parentPath, int depth) {
            MegaNodeListPtr children(capturedApi->getChildren(parentNode));
            if (!children) return;

            for (int i = 0; i < children->size(); i++) {
                mega::MegaNode* node = children->get(i);
                if (!node) continue;

                QString nodeName = QString::fromUtf8(node->getName());
                QString nodePath = parentPath.isEmpty() ? "/" + nodeName :
                                   (parentPath == "/" ? "/" + nodeName : parentPath + "/" + nodeName);

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

                if (nodeCount - lastProgress >= 100) {
                    lastProgress = nodeCount;
                    QMetaObject::invokeMethod(this, [this, nodeCount]() {
                        emit searchIndexBuildProgress(nodeCount);
                    }, Qt::QueuedConnection);
                }

                if (node->isFolder()) {
                    traverseNode(node, nodePath, depth + 1);
                }
            }
        };

        traverseNode(rootNode.get(), "", 0);

        qint64 elapsed = timer.elapsed();
        qDebug() << "Search index built:" << nodeCount << "nodes in" << elapsed << "ms";

        index->finishBuilding();

        QMetaObject::invokeMethod(this, [this, nodeCount]() {
            emit searchIndexBuildCompleted(nodeCount);
        }, Qt::QueuedConnection);
    });
}

} // namespace MegaCustom
