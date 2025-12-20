#include "FileBridge.h"
#include "controllers/FileController.h"
#include "bridge/BackendModules.h"  // Real CLI modules
#include "core/MegaManager.h"
#include "operations/FileOperations.h"
#include "operations/FolderManager.h"
#include "megaapi.h"
#include <QDebug>
#include <QTimer>
#include <QFileInfo>
#include <QDateTime>
#include <QVariantMap>
#include <QtConcurrent>

namespace MegaCustom {

FileBridge::FileBridge(QObject* parent)
    : QObject(parent) {
    qDebug() << "FileBridge: Created file operations bridge";
}

FileBridge::~FileBridge() {
    qDebug() << "FileBridge: Destroyed";
}

void FileBridge::setFileModule(FileOperations* module) {
    m_fileModule = module;
    qDebug() << "FileBridge: File module set";

    if (m_fileModule) {
        // TODO: Set up callbacks from CLI module
        // m_fileModule->setListCallback(
        //     [this](const std::vector<MegaNode*>& nodes) {
        //         onFileListReceived(nodes);
        //     });
        // m_fileModule->setTransferCallback(
        //     [this](const std::string& id, double progress, size_t speed) {
        //         onTransferProgress(QString::fromStdString(id), progress, speed);
        //     });
    }
}

void FileBridge::connectToGUI(FileController* guiController) {
    if (!guiController) {
        qDebug() << "FileBridge: Cannot connect - null GUI controller";
        return;
    }

    m_guiController = guiController;

    // Disconnect any existing connections
    disconnect(m_guiController, nullptr, this, nullptr);
    disconnect(this, nullptr, m_guiController, nullptr);

    // Connect GUI signals to bridge slots
    connect(m_guiController, &FileController::listFiles,
            this, &FileBridge::handleListFiles);

    connect(m_guiController, &FileController::uploadFiles,
            this, &FileBridge::handleUploadFiles);

    connect(m_guiController, &FileController::downloadFiles,
            this, &FileBridge::handleDownloadFiles);

    connect(m_guiController, &FileController::deleteFiles,
            this, &FileBridge::handleDeleteFiles);

    connect(m_guiController, &FileController::createFolder,
            this, &FileBridge::handleCreateFolder);

    // Connect bridge signals to GUI signals
    connect(this, &FileBridge::fileListReceived,
            m_guiController, &FileController::fileListReceived);

    connect(this, &FileBridge::uploadProgress,
            m_guiController, &FileController::uploadProgress);

    connect(this, &FileBridge::downloadProgress,
            m_guiController, &FileController::downloadProgress);

    qDebug() << "FileBridge: Connected to GUI controller";
}

void FileBridge::handleListFiles(const QString& path) {
    qDebug() << "FileBridge: List files requested for" << path;

    MegaManager& manager = MegaManager::getInstance();
    mega::MegaApi* megaApi = manager.getMegaApi();

    if (!megaApi || !megaApi->isLoggedIn()) {
        emit fileListError("Not logged in");
        return;
    }

    // Run in background thread to avoid blocking UI (using QtConcurrent for safe lifecycle)
    QtConcurrent::run([this, path, megaApi]() {
        std::string remotePath = path.isEmpty() ? "/" : path.toStdString();

        // Get the folder node
        mega::MegaNode* folderNode = megaApi->getNodeByPath(remotePath.c_str());
        if (!folderNode) {
            QTimer::singleShot(0, this, [this]() {
                emit fileListError("Folder not found");
            });
            return;
        }

        // Get children
        mega::MegaNodeList* children = megaApi->getChildren(folderNode);
        QVariantList files;

        if (children) {
            for (int i = 0; i < children->size(); i++) {
                mega::MegaNode* node = children->get(i);
                if (node) {
                    QVariantMap fileInfo;
                    QString nodeName = QString::fromUtf8(node->getName());
                    fileInfo["name"] = nodeName;
                    fileInfo["path"] = (path == "/" ? "/" : path + "/") + nodeName;
                    fileInfo["size"] = static_cast<qint64>(node->getSize());
                    fileInfo["modified"] = static_cast<qint64>(node->getModificationTime());
                    fileInfo["isFolder"] = node->isFolder();
                    fileInfo["handle"] = QString::number(node->getHandle());
                    files.append(fileInfo);
                }
            }
            delete children;
        }

        delete folderNode;

        // Emit on main thread
        QTimer::singleShot(0, this, [this, files]() {
            emit fileListReceived(files);
        });
    });
}

void FileBridge::handleUploadFiles(const QStringList& localPaths, const QString& remotePath) {
    qDebug() << "FileBridge: Upload requested -" << localPaths.size() << "files to" << remotePath;

    MegaManager& manager = MegaManager::getInstance();
    mega::MegaApi* megaApi = manager.getMegaApi();

    if (!megaApi || !megaApi->isLoggedIn()) {
        for (const auto& path : localPaths) {
            QString transferId = generateTransferId();
            emit uploadFailed(transferId, "Not logged in");
        }
        return;
    }

    // Process each file in background
    for (const auto& localPath : localPaths) {
        QString transferId = generateTransferId();
        m_activeTransfers[transferId] = localPath;
        emit uploadStarted(localPath, transferId);

        QtConcurrent::run([this, localPath, remotePath, transferId, megaApi]() {
            FileOperations fileOps(megaApi);

            UploadConfig config;
            config.preserveTimestamp = true;
            config.detectDuplicates = true;

            auto result = fileOps.uploadFile(
                localPath.toStdString(),
                remotePath.toStdString(),
                config
            );

            // Emit results on main thread
            QTimer::singleShot(0, this, [this, transferId, result, localPath]() {
                if (result.success) {
                    qint64 size = static_cast<qint64>(result.fileSize);
                    emit uploadProgress(transferId, size, size);
                    emit uploadCompleted(transferId);
                } else {
                    emit uploadFailed(transferId, QString::fromStdString(result.errorMessage));
                }
                m_activeTransfers.remove(transferId);
            });
        });
    }
}

void FileBridge::handleDownloadFiles(const QStringList& remotePaths, const QString& localPath) {
    qDebug() << "FileBridge: Download requested -" << remotePaths.size() << "files to" << localPath;

    MegaManager& manager = MegaManager::getInstance();
    mega::MegaApi* megaApi = manager.getMegaApi();

    if (!megaApi || !megaApi->isLoggedIn()) {
        for (const auto& path : remotePaths) {
            QString transferId = generateTransferId();
            emit downloadFailed(transferId, "Not logged in");
        }
        return;
    }

    // Process each file in background
    for (const auto& remotePath : remotePaths) {
        QString transferId = generateTransferId();
        m_activeTransfers[transferId] = remotePath;
        emit downloadStarted(remotePath, transferId);

        QtConcurrent::run([this, remotePath, localPath, transferId, megaApi]() {
            // Get the remote node
            mega::MegaNode* node = megaApi->getNodeByPath(remotePath.toStdString().c_str());
            if (!node) {
                QTimer::singleShot(0, this, [this, transferId]() {
                    emit downloadFailed(transferId, "Remote file not found");
                    m_activeTransfers.remove(transferId);
                });
                return;
            }

            FileOperations fileOps(megaApi);
            DownloadConfig config;
            config.resumeIfExists = true;
            config.verifyChecksum = true;

            auto result = fileOps.downloadFile(
                node,
                localPath.toStdString(),
                config
            );

            delete node;

            // Emit results on main thread
            QTimer::singleShot(0, this, [this, transferId, result]() {
                if (result.success) {
                    qint64 size = static_cast<qint64>(result.fileSize);
                    emit downloadProgress(transferId, size, size);
                    emit downloadCompleted(transferId);
                } else {
                    emit downloadFailed(transferId, QString::fromStdString(result.errorMessage));
                }
                m_activeTransfers.remove(transferId);
            });
        });
    }
}

void FileBridge::handleDeleteFiles(const QStringList& paths) {
    qDebug() << "FileBridge: Delete requested for" << paths.size() << "files";

    MegaManager& manager = MegaManager::getInstance();
    mega::MegaApi* megaApi = manager.getMegaApi();

    if (!megaApi || !megaApi->isLoggedIn()) {
        for (const auto& path : paths) {
            emit deletionFailed(path, "Not logged in");
        }
        return;
    }

    for (const auto& path : paths) {
        QtConcurrent::run([this, path, megaApi]() {
            FolderManager folderMgr(megaApi);
            auto result = folderMgr.deleteFolder(path.toStdString(), true);  // Move to trash

            QTimer::singleShot(0, this, [this, path, result]() {
                if (result.success) {
                    emit fileDeleted(path);
                } else {
                    emit deletionFailed(path, QString::fromStdString(result.errorMessage));
                }
            });
        });
    }
}

void FileBridge::handleCreateFolder(const QString& path, const QString& name) {
    qDebug() << "FileBridge: Create folder requested -" << name << "in" << path;

    MegaManager& manager = MegaManager::getInstance();
    mega::MegaApi* megaApi = manager.getMegaApi();

    if (!megaApi || !megaApi->isLoggedIn()) {
        emit folderCreationFailed("Not logged in");
        return;
    }

    QtConcurrent::run([this, path, name, megaApi]() {
        FolderManager folderMgr(megaApi);
        QString fullPath = path + "/" + name;
        auto result = folderMgr.createFolder(fullPath.toStdString(), true);

        QTimer::singleShot(0, this, [this, fullPath, result]() {
            if (result.success) {
                emit folderCreated(fullPath);
            } else {
                emit folderCreationFailed(QString::fromStdString(result.errorMessage));
            }
        });
    });
}

void FileBridge::handleMoveFile(const QString& sourcePath, const QString& destPath) {
    qDebug() << "FileBridge: Move file from" << sourcePath << "to" << destPath;

    if (!m_fileModule) {
        emit moveFailed("Backend not initialized");
        return;
    }

    // Run move in background thread using QtConcurrent for safe lifecycle
    QtConcurrent::run([this, sourcePath, destPath]() {
        MegaManager& manager = MegaManager::getInstance();
        mega::MegaApi* megaApi = manager.getMegaApi();

        if (!megaApi) {
            QTimer::singleShot(0, this, [this]() {
                emit moveFailed("SDK not initialized");
            });
            return;
        }

        FolderManager folderMgr(megaApi);
        FolderOperationResult result = folderMgr.moveFolder(
            sourcePath.toStdString(),
            destPath.toStdString()
        );

        QTimer::singleShot(0, this, [this, sourcePath, destPath, result]() {
            if (result.success) {
                emit fileMoved(sourcePath, destPath);
            } else {
                emit moveFailed(QString::fromStdString(result.errorMessage));
            }
        });
    });
}

void FileBridge::handleCopyFile(const QString& sourcePath, const QString& destPath) {
    qDebug() << "FileBridge: Copy file from" << sourcePath << "to" << destPath;

    if (!m_fileModule) {
        emit copyFailed("Backend not initialized");
        return;
    }

    // Run copy in background thread using QtConcurrent for safe lifecycle
    QtConcurrent::run([this, sourcePath, destPath]() {
        MegaManager& manager = MegaManager::getInstance();
        mega::MegaApi* megaApi = manager.getMegaApi();

        if (!megaApi) {
            QTimer::singleShot(0, this, [this]() {
                emit copyFailed("SDK not initialized");
            });
            return;
        }

        FolderManager folderMgr(megaApi);
        FolderOperationResult result = folderMgr.copyFolder(
            sourcePath.toStdString(),
            destPath.toStdString()
        );

        QTimer::singleShot(0, this, [this, sourcePath, destPath, result]() {
            if (result.success) {
                emit fileCopied(sourcePath, destPath);
            } else {
                emit copyFailed(QString::fromStdString(result.errorMessage));
            }
        });
    });
}

void FileBridge::handleShareFile(const QString& path, const QString& email, bool readOnly) {
    qDebug() << "FileBridge: Share file" << path << "with" << email;

    if (!m_fileModule) {
        emit shareFailed("Backend not initialized");
        return;
    }

    // Run share in background thread using QtConcurrent for safe lifecycle
    QtConcurrent::run([this, path, email, readOnly]() {
        MegaManager& manager = MegaManager::getInstance();
        mega::MegaApi* megaApi = manager.getMegaApi();

        if (!megaApi) {
            QTimer::singleShot(0, this, [this]() {
                emit shareFailed("SDK not initialized");
            });
            return;
        }

        FolderManager folderMgr(megaApi);

        // If email is provided, share with that user; otherwise create public link
        if (!email.isEmpty()) {
            // Share with specific user (shareFolder takes bool readOnly)
            FolderOperationResult result = folderMgr.shareFolder(
                path.toStdString(),
                email.toStdString(),
                readOnly
            );

            QTimer::singleShot(0, this, [this, path, email, result]() {
                if (result.success) {
                    emit fileShared(path, QString("Shared with %1").arg(email));
                } else {
                    emit shareFailed(QString::fromStdString(result.errorMessage));
                }
            });
        } else {
            // Create public link
            std::string link = folderMgr.createPublicLink(path.toStdString());

            QTimer::singleShot(0, this, [this, path, link]() {
                if (!link.empty()) {
                    emit fileShared(path, QString::fromStdString(link));
                } else {
                    emit shareFailed("Failed to create public link");
                }
            });
        }
    });
}

void FileBridge::handleGetStorageInfo() {
    qDebug() << "FileBridge: Storage info requested";

    MegaManager& manager = MegaManager::getInstance();
    mega::MegaApi* megaApi = manager.getMegaApi();

    if (!megaApi || !megaApi->isLoggedIn()) {
        emit storageInfoError("Not logged in");
        return;
    }

    QtConcurrent::run([this, megaApi]() {
        // Calculate storage used from root node recursively
        mega::MegaNode* rootNode = megaApi->getRootNode();
        qint64 used = 0;
        if (rootNode) {
            used = static_cast<qint64>(megaApi->getSize(rootNode));
            delete rootNode;
        }

        // Standard MEGA free tier is 20GB (can be upgraded)
        // For accurate quota info, we'd need to use getAccountDetails with a listener
        qint64 total = 20LL * 1024 * 1024 * 1024;  // 20GB default
        qint64 available = total - used;
        if (available < 0) available = 0;

        QTimer::singleShot(0, this, [this, used, total, available]() {
            emit storageInfoReceived(used, total, available);
        });
    });
}

void FileBridge::onFileListReceived(const std::vector<void*>& files) {
    QVariantList fileList;

    for (void* node : files) {
        fileList.append(convertFileInfo(node));
    }

    emit fileListReceived(fileList);
}

void FileBridge::onOperationComplete(const QString& operation, bool success, const QString& result) {
    qDebug() << "FileBridge: Operation" << operation << "completed -"
             << (success ? "success" : "failed") << result;

    // Route to appropriate signal based on operation
    // This would be expanded based on actual CLI callbacks
}

void FileBridge::onTransferProgress(const QString& transferId, double progress, size_t speed) {
    if (m_activeTransfers.contains(transferId)) {
        // Convert progress percentage to bytes
        // This would need actual file size tracking
        qDebug() << "FileBridge: Transfer" << transferId
                 << "progress:" << progress << "% speed:" << speed << "B/s";
    }
}

QVariantMap FileBridge::convertFileInfo(void* megaNode) const {
    QVariantMap info;

    mega::MegaNode* node = static_cast<mega::MegaNode*>(megaNode);
    if (!node) {
        info["name"] = "Unknown";
        info["path"] = "/";
        info["size"] = 0;
        info["modified"] = QDateTime::currentDateTime().toSecsSinceEpoch();
        info["isFolder"] = false;
        return info;
    }

    // Extract actual info from MegaNode
    const char* name = node->getName();
    info["name"] = name ? QString::fromUtf8(name) : "Unknown";

    // Get full path from MegaApi
    MegaManager& manager = MegaManager::getInstance();
    mega::MegaApi* megaApi = manager.getMegaApi();
    if (megaApi) {
        char* nodePath = megaApi->getNodePath(node);
        if (nodePath) {
            info["path"] = QString::fromUtf8(nodePath);
            delete[] nodePath;
        } else {
            info["path"] = QString("/") + info["name"].toString();
        }
    }

    info["size"] = static_cast<qint64>(node->getSize());
    info["modified"] = static_cast<qint64>(node->getModificationTime());
    info["created"] = static_cast<qint64>(node->getCreationTime());
    info["isFolder"] = node->isFolder();
    info["isFile"] = node->isFile();
    info["handle"] = QString::number(node->getHandle());

    // Sharing info
    info["isShared"] = node->isShared();
    info["isExported"] = node->isExported();

    // Get public link if exported (via exportPublicLink API)
    // Note: The actual link needs to be generated via exportPublicLink which is async
    // For now, just mark as exported - link can be retrieved separately

    // Fingerprint for files
    if (node->isFile()) {
        const char* fingerprint = node->getFingerprint();
        if (fingerprint) {
            info["fingerprint"] = QString::fromUtf8(fingerprint);
        }
    }

    // Child count for folders
    if (node->isFolder() && megaApi) {
        info["childCount"] = megaApi->getNumChildren(node);
    }

    return info;
}

QString FileBridge::generateTransferId() {
    return QString("transfer_%1").arg(m_nextTransferId++);
}

} // namespace MegaCustom

#include "moc_FileBridge.cpp"