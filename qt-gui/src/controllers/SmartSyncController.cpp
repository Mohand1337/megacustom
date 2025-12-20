#include "SmartSyncController.h"
#include "megaapi.h"
#include <QDir>
#include <QFileInfo>
#include <QDirIterator>
#include <QSettings>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>
#include <QtConcurrent>
#include <QDebug>

namespace MegaCustom {

SmartSyncController::SmartSyncController(void* megaApi, QObject* parent)
    : QObject(parent)
    , m_megaApi(megaApi)
{
    loadProfiles();
}

SmartSyncController::~SmartSyncController() {
    if (m_isSyncing) {
        stopSync(m_currentSyncProfileId);
    }
    saveProfiles();
}

void SmartSyncController::loadProfiles() {
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                         + "/MegaCustom/sync_profiles.json";

    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "No sync profiles found";
        emit profilesLoaded(0);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isArray()) {
        emit profilesLoaded(0);
        return;
    }

    QJsonArray arr = doc.array();
    m_profiles.clear();

    for (const QJsonValue& val : arr) {
        QJsonObject obj = val.toObject();
        SyncProfile profile;

        profile.id = obj["id"].toString();
        profile.name = obj["name"].toString();
        profile.localPath = obj["localPath"].toString();
        profile.remotePath = obj["remotePath"].toString();
        profile.direction = static_cast<SyncDirection>(obj["direction"].toInt());
        profile.conflictResolution = static_cast<ConflictResolution>(obj["conflictResolution"].toInt());
        profile.includePatterns = obj["includePatterns"].toString();
        profile.excludePatterns = obj["excludePatterns"].toString();
        profile.syncHiddenFiles = obj["syncHidden"].toBool();
        profile.syncTempFiles = obj["syncTemp"].toBool();
        profile.deleteOrphans = obj["deleteOrphans"].toBool();
        profile.verifyAfterSync = obj["verify"].toBool(true);
        profile.autoSyncEnabled = obj["autoSync"].toBool();
        profile.autoSyncIntervalMinutes = obj["autoSyncInterval"].toInt(60);
        profile.lastSyncTime = QDateTime::fromString(obj["lastSync"].toString(), Qt::ISODate);

        m_profiles.append(profile);
    }

    qDebug() << "Loaded" << m_profiles.size() << "sync profiles";
    emit profilesLoaded(m_profiles.size());
}

void SmartSyncController::saveProfiles() {
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                         + "/MegaCustom";
    if (!QDir().mkpath(configPath)) {
        qWarning() << "SmartSyncController: Failed to create config directory:" << configPath;
        return;
    }

    QJsonArray arr;
    for (const auto& profile : m_profiles) {
        QJsonObject obj;
        obj["id"] = profile.id;
        obj["name"] = profile.name;
        obj["localPath"] = profile.localPath;
        obj["remotePath"] = profile.remotePath;
        obj["direction"] = static_cast<int>(profile.direction);
        obj["conflictResolution"] = static_cast<int>(profile.conflictResolution);
        obj["includePatterns"] = profile.includePatterns;
        obj["excludePatterns"] = profile.excludePatterns;
        obj["syncHidden"] = profile.syncHiddenFiles;
        obj["syncTemp"] = profile.syncTempFiles;
        obj["deleteOrphans"] = profile.deleteOrphans;
        obj["verify"] = profile.verifyAfterSync;
        obj["autoSync"] = profile.autoSyncEnabled;
        obj["autoSyncInterval"] = profile.autoSyncIntervalMinutes;
        obj["lastSync"] = profile.lastSyncTime.toString(Qt::ISODate);

        arr.append(obj);
    }

    QFile file(configPath + "/sync_profiles.json");
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
        file.close();
        qDebug() << "Saved" << m_profiles.size() << "sync profiles";
    }
}

void SmartSyncController::createProfile(const QString& name, const QString& localPath,
                                         const QString& remotePath) {
    SyncProfile profile;
    profile.id = generateProfileId();
    profile.name = name;
    profile.localPath = localPath;
    profile.remotePath = remotePath;

    m_profiles.append(profile);
    saveProfiles();

    emit profileCreated(profile.id, profile.name);
    qDebug() << "Created sync profile:" << name;
}

void SmartSyncController::updateProfile(const QString& profileId, const SyncProfile& profile) {
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles[i].id == profileId) {
            m_profiles[i] = profile;
            m_profiles[i].id = profileId;  // Preserve ID
            saveProfiles();
            emit profileUpdated(profileId);
            return;
        }
    }
}

void SmartSyncController::deleteProfile(const QString& profileId) {
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles[i].id == profileId) {
            m_profiles.removeAt(i);
            saveProfiles();
            emit profileDeleted(profileId);
            return;
        }
    }
}

SyncProfile* SmartSyncController::getProfile(const QString& profileId) {
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles[i].id == profileId) {
            return &m_profiles[i];
        }
    }
    return nullptr;
}

QVector<SyncProfile> SmartSyncController::getAllProfiles() const {
    return m_profiles;
}

void SmartSyncController::setDirection(const QString& profileId, SyncDirection direction) {
    if (SyncProfile* profile = getProfile(profileId)) {
        profile->direction = direction;
        saveProfiles();
        emit profileUpdated(profileId);
    }
}

void SmartSyncController::setConflictResolution(const QString& profileId,
                                                 ConflictResolution resolution) {
    if (SyncProfile* profile = getProfile(profileId)) {
        profile->conflictResolution = resolution;
        saveProfiles();
        emit profileUpdated(profileId);
    }
}

void SmartSyncController::setFilters(const QString& profileId, const QString& include,
                                      const QString& exclude) {
    if (SyncProfile* profile = getProfile(profileId)) {
        profile->includePatterns = include;
        profile->excludePatterns = exclude;
        saveProfiles();
        emit profileUpdated(profileId);
    }
}

void SmartSyncController::setAutoSync(const QString& profileId, bool enabled, int intervalMinutes) {
    if (SyncProfile* profile = getProfile(profileId)) {
        profile->autoSyncEnabled = enabled;
        profile->autoSyncIntervalMinutes = intervalMinutes;
        saveProfiles();
        emit profileUpdated(profileId);
    }
}

void SmartSyncController::analyzeProfile(const QString& profileId) {
    SyncProfile* profile = getProfile(profileId);
    if (!profile) {
        emit error("Analyze", "Profile not found: " + profileId);
        return;
    }

    emit analysisStarted(profileId);

    // Run analysis in background
    QString localPath = profile->localPath;
    QString remotePath = profile->remotePath;
    SyncDirection direction = profile->direction;

    QtConcurrent::run([this, profileId, localPath, remotePath, direction]() {
        {
            QMutexLocker locker(&m_dataMutex);
            m_pendingActions.clear();
            m_conflicts.clear();
        }

        int totalFiles = 0;
        int uploads = 0;
        int downloads = 0;
        int deletions = 0;
        int conflicts = 0;

        // Scan local directory
        QDir localDir(localPath);
        if (!localDir.exists()) {
            QMetaObject::invokeMethod(this, [this, profileId]() {
                emit error("Analyze", "Local directory does not exist");
                emit analysisComplete(profileId, 0, 0, 0, 0);
            }, Qt::QueuedConnection);
            return;
        }

        QDirIterator it(localPath, QDir::Files | QDir::NoDotAndDotDot,
                       QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            totalFiles++;

            SyncAction action;
            action.id = totalFiles;
            action.localPath = it.filePath();
            action.filePath = localDir.relativeFilePath(it.filePath());
            action.remotePath = remotePath + "/" + action.filePath;

            QFileInfo fi = it.fileInfo();
            action.localSize = fi.size();
            action.localModTime = fi.lastModified();

            // For now, assume file needs upload (real implementation would check remote)
            if (direction != SyncDirection::REMOTE_TO_LOCAL) {
                action.actionType = SyncAction::ActionType::UPLOAD;
                uploads++;
            } else {
                action.actionType = SyncAction::ActionType::SKIP;
            }

            {
                QMutexLocker locker(&m_dataMutex);
                m_pendingActions.append(action);
            }

            // Emit progress every 100 files
            if (totalFiles % 100 == 0) {
                QMetaObject::invokeMethod(this, [this, profileId, totalFiles]() {
                    emit analysisProgress(profileId, totalFiles, totalFiles);
                }, Qt::QueuedConnection);
            }
        }

        QMetaObject::invokeMethod(this, [this, profileId, uploads, downloads, deletions, conflicts]() {
            emit actionsReady(profileId, m_pendingActions);
            emit analysisComplete(profileId, uploads, downloads, deletions, conflicts);
        }, Qt::QueuedConnection);
    });
}

void SmartSyncController::startSync(const QString& profileId) {
    SyncProfile profileCopy;
    {
        QMutexLocker locker(&m_dataMutex);
        SyncProfile* profile = nullptr;
        for (int i = 0; i < m_profiles.size(); ++i) {
            if (m_profiles[i].id == profileId) {
                profile = &m_profiles[i];
                break;
            }
        }
        if (!profile) {
            emit error("Start Sync", "Profile not found: " + profileId);
            return;
        }

        if (m_isSyncing) {
            emit error("Start Sync", "Another sync is already in progress");
            return;
        }

        m_isSyncing = true;
        m_isPaused = false;
        m_cancelRequested = false;
        m_currentSyncProfileId = profileId;
        profile->isActive = true;

        // Copy profile data to avoid race conditions
        profileCopy = *profile;
    }

    emit syncStarted(profileId);

    // Run sync in background with copied profile data
    QtConcurrent::run([this, profileId, profileCopy]() mutable {
        performSync(profileCopy);

        // Update the original profile under lock after sync completes
        QMutexLocker locker(&m_dataMutex);
        for (int i = 0; i < m_profiles.size(); ++i) {
            if (m_profiles[i].id == profileId) {
                m_profiles[i].lastSyncTime = profileCopy.lastSyncTime;
                m_profiles[i].isActive = profileCopy.isActive;
                break;
            }
        }
    });
}

void SmartSyncController::performSync(SyncProfile& profile) {
    mega::MegaApi* api = static_cast<mega::MegaApi*>(m_megaApi);
    if (!api) {
        QMetaObject::invokeMethod(this, [this]() {
            emit error("Sync", "API not available");
            m_isSyncing = false;
        }, Qt::QueuedConnection);
        return;
    }

    // Make a local copy of pending actions to avoid holding lock during entire sync
    QVector<SyncAction> localActions;
    {
        QMutexLocker locker(&m_dataMutex);
        localActions = m_pendingActions;
    }

    int filesUploaded = 0;
    int filesDownloaded = 0;
    int errors = 0;
    int totalFiles = localActions.size();
    qint64 totalBytes = 0;
    qint64 bytesTransferred = 0;

    // Calculate total bytes
    for (const auto& action : localActions) {
        if (action.actionType == SyncAction::ActionType::UPLOAD) {
            totalBytes += action.localSize;
        } else if (action.actionType == SyncAction::ActionType::DOWNLOAD) {
            totalBytes += action.remoteSize;
        }
    }

    for (int i = 0; i < localActions.size(); ++i) {
        if (m_cancelRequested) break;

        while (m_isPaused && !m_cancelRequested) {
            QThread::msleep(100);
        }

        if (m_cancelRequested) break;

        SyncAction& action = localActions[i];
        QString profileId = profile.id;

        QMetaObject::invokeMethod(this, [this, profileId, action, i, totalFiles, bytesTransferred, totalBytes]() {
            emit syncProgress(profileId, action.filePath, i, totalFiles,
                            bytesTransferred, totalBytes);
        }, Qt::QueuedConnection);

        switch (action.actionType) {
            case SyncAction::ActionType::UPLOAD: {
                // Find or create remote folder
                QString remoteDir = QFileInfo(action.remotePath).path();
                std::unique_ptr<mega::MegaNode> parentNode(
                    api->getNodeByPath(remoteDir.toUtf8().constData()));

                if (parentNode) {
                    api->startUpload(action.localPath.toUtf8().constData(),
                                   parentNode.get(), nullptr, 0, nullptr, false, false, nullptr);
                    filesUploaded++;
                    bytesTransferred += action.localSize;
                } else {
                    errors++;
                }
                break;
            }
            case SyncAction::ActionType::DOWNLOAD: {
                std::unique_ptr<mega::MegaNode> fileNode(
                    api->getNodeByPath(action.remotePath.toUtf8().constData()));

                if (fileNode) {
                    QString localDir = QFileInfo(action.localPath).path();
                    if (!QDir().mkpath(localDir)) {
                        qWarning() << "SmartSyncController: Failed to create local directory:" << localDir;
                        errors++;
                        break;
                    }
                    // startDownload(node, localPath, customName, appData, startFirst, cancelToken,
                    //               collisionCheck, collisionResolution, undelete, listener)
                    api->startDownload(fileNode.get(), action.localPath.toUtf8().constData(),
                                      nullptr, nullptr, false, nullptr,
                                      mega::MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                                      mega::MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N,
                                      false, nullptr);
                    filesDownloaded++;
                    bytesTransferred += action.remoteSize;
                } else {
                    errors++;
                }
                break;
            }
            case SyncAction::ActionType::DELETE_LOCAL: {
                QFile::remove(action.localPath);
                break;
            }
            case SyncAction::ActionType::DELETE_REMOTE: {
                std::unique_ptr<mega::MegaNode> node(
                    api->getNodeByPath(action.remotePath.toUtf8().constData()));
                if (node) {
                    api->remove(node.get());
                }
                break;
            }
            default:
                break;
        }

        // Small delay to avoid overwhelming the API
        QThread::msleep(10);
    }

    // Update profile
    profile.lastSyncTime = QDateTime::currentDateTime();
    profile.isActive = false;
    m_isSyncing = false;

    // Add history entry
    SyncHistoryEntry entry;
    entry.timestamp = QDateTime::currentDateTime();
    entry.profileName = profile.name;
    entry.filesUploaded = filesUploaded;
    entry.filesDownloaded = filesDownloaded;
    entry.errors = errors;
    entry.status = m_cancelRequested ? "Cancelled" : (errors > 0 ? "Completed with errors" : "Success");
    addHistoryEntry(profile.id, entry);

    QString profileId = profile.id;
    QMetaObject::invokeMethod(this, [this, profileId, filesUploaded, filesDownloaded, errors]() {
        saveProfiles();
        emit syncComplete(profileId, errors == 0, filesUploaded, filesDownloaded, errors);
    }, Qt::QueuedConnection);
}

void SmartSyncController::pauseSync(const QString& profileId) {
    QMutexLocker locker(&m_dataMutex);
    if (m_currentSyncProfileId == profileId && m_isSyncing) {
        m_isPaused = true;
        for (int i = 0; i < m_profiles.size(); ++i) {
            if (m_profiles[i].id == profileId) {
                m_profiles[i].isPaused = true;
                break;
            }
        }
        emit syncPaused(profileId);
    }
}

void SmartSyncController::resumeSync(const QString& profileId) {
    QMutexLocker locker(&m_dataMutex);
    if (m_currentSyncProfileId == profileId && m_isPaused) {
        m_isPaused = false;
        for (int i = 0; i < m_profiles.size(); ++i) {
            if (m_profiles[i].id == profileId) {
                m_profiles[i].isPaused = false;
                break;
            }
        }
        emit syncResumed(profileId);
    }
}

void SmartSyncController::stopSync(const QString& profileId) {
    QMutexLocker locker(&m_dataMutex);
    if (m_currentSyncProfileId == profileId) {
        m_cancelRequested = true;
        m_isPaused = false;

        for (int i = 0; i < m_profiles.size(); ++i) {
            if (m_profiles[i].id == profileId) {
                m_profiles[i].isActive = false;
                m_profiles[i].isPaused = false;
                break;
            }
        }

        emit syncCancelled(profileId);
    }
}

void SmartSyncController::resolveConflict(int conflictId, const QString& resolution) {
    QMutexLocker locker(&m_dataMutex);
    for (auto& conflict : m_conflicts) {
        if (conflict.id == conflictId) {
            conflict.resolved = true;
            conflict.resolution = resolution;
            emit conflictResolved(conflictId, resolution);
            return;
        }
    }
}

void SmartSyncController::resolveAllConflicts(const QString& profileId,
                                               ConflictResolution strategy) {
    QMutexLocker locker(&m_dataMutex);
    for (auto& conflict : m_conflicts) {
        if (!conflict.resolved) {
            QString resolution;
            switch (strategy) {
                case ConflictResolution::KEEP_LOCAL:
                    resolution = "keep_local";
                    break;
                case ConflictResolution::KEEP_REMOTE:
                    resolution = "keep_remote";
                    break;
                case ConflictResolution::KEEP_NEWER:
                    resolution = conflict.localModTime > conflict.remoteModTime
                                 ? "keep_local" : "keep_remote";
                    break;
                case ConflictResolution::KEEP_LARGER:
                    resolution = conflict.localSize > conflict.remoteSize
                                 ? "keep_local" : "keep_remote";
                    break;
                case ConflictResolution::KEEP_BOTH:
                    resolution = "keep_both";
                    break;
                default:
                    continue;
            }
            conflict.resolved = true;
            conflict.resolution = resolution;
            emit conflictResolved(conflict.id, resolution);
        }
    }
    emit conflictsCleared(profileId);
}

QVector<SyncConflict> SmartSyncController::getConflicts(const QString& profileId) const {
    Q_UNUSED(profileId);
    QMutexLocker locker(&m_dataMutex);
    return m_conflicts;
}

QVector<SyncHistoryEntry> SmartSyncController::getHistory(const QString& profileId,
                                                           int maxEntries) const {
    if (m_history.contains(profileId)) {
        QVector<SyncHistoryEntry> history = m_history[profileId];
        if (history.size() > maxEntries) {
            return history.mid(history.size() - maxEntries);
        }
        return history;
    }
    return {};
}

void SmartSyncController::exportProfile(const QString& profileId, const QString& filePath) {
    SyncProfile* profile = getProfile(profileId);
    if (!profile) {
        emit error("Export", "Profile not found");
        return;
    }

    QJsonObject obj;
    obj["name"] = profile->name;
    obj["localPath"] = profile->localPath;
    obj["remotePath"] = profile->remotePath;
    obj["direction"] = static_cast<int>(profile->direction);
    obj["conflictResolution"] = static_cast<int>(profile->conflictResolution);
    obj["includePatterns"] = profile->includePatterns;
    obj["excludePatterns"] = profile->excludePatterns;
    obj["syncHidden"] = profile->syncHiddenFiles;
    obj["syncTemp"] = profile->syncTempFiles;
    obj["deleteOrphans"] = profile->deleteOrphans;
    obj["verify"] = profile->verifyAfterSync;
    obj["autoSync"] = profile->autoSyncEnabled;
    obj["autoSyncInterval"] = profile->autoSyncIntervalMinutes;

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        file.close();
        qDebug() << "Exported profile to" << filePath;
    } else {
        emit error("Export", "Failed to write file: " + filePath);
    }
}

void SmartSyncController::importProfile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit error("Import", "Failed to read file: " + filePath);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) {
        emit error("Import", "Invalid profile format");
        return;
    }

    QJsonObject obj = doc.object();
    SyncProfile profile;
    profile.id = generateProfileId();
    profile.name = obj["name"].toString() + " (imported)";
    profile.localPath = obj["localPath"].toString();
    profile.remotePath = obj["remotePath"].toString();
    profile.direction = static_cast<SyncDirection>(obj["direction"].toInt());
    profile.conflictResolution = static_cast<ConflictResolution>(obj["conflictResolution"].toInt());
    profile.includePatterns = obj["includePatterns"].toString();
    profile.excludePatterns = obj["excludePatterns"].toString();
    profile.syncHiddenFiles = obj["syncHidden"].toBool();
    profile.syncTempFiles = obj["syncTemp"].toBool();
    profile.deleteOrphans = obj["deleteOrphans"].toBool();
    profile.verifyAfterSync = obj["verify"].toBool(true);
    profile.autoSyncEnabled = obj["autoSync"].toBool();
    profile.autoSyncIntervalMinutes = obj["autoSyncInterval"].toInt(60);

    m_profiles.append(profile);
    saveProfiles();
    emit profileCreated(profile.id, profile.name);
}

void SmartSyncController::addHistoryEntry(const QString& profileId,
                                           const SyncHistoryEntry& entry) {
    QMutexLocker locker(&m_dataMutex);
    m_history[profileId].append(entry);

    // Keep max 100 entries per profile
    if (m_history[profileId].size() > 100) {
        m_history[profileId].removeFirst();
    }
}

QString SmartSyncController::generateProfileId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

} // namespace MegaCustom
