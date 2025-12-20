#include "SyncScheduler.h"
#include "../controllers/FolderMapperController.h"
#include "../controllers/SmartSyncController.h"
#include "../controllers/MultiUploaderController.h"
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

namespace MegaCustom {

SyncScheduler::SyncScheduler(QObject* parent)
    : QObject(parent)
    , m_checkTimer(new QTimer(this))
{
    connect(m_checkTimer, &QTimer::timeout, this, &SyncScheduler::onTimerTick);

    // Load saved tasks on construction
    loadTasks();
}

SyncScheduler::~SyncScheduler() {
    stop();
    saveTasks();
}

void SyncScheduler::start() {
    if (m_isRunning) return;

    m_checkTimer->start(m_checkIntervalSec * 1000);
    m_isRunning = true;
    qDebug() << "SyncScheduler started with interval:" << m_checkIntervalSec << "seconds";
    emit schedulerStarted();
}

void SyncScheduler::stop() {
    if (!m_isRunning) return;

    m_checkTimer->stop();
    m_isRunning = false;
    qDebug() << "SyncScheduler stopped";
    emit schedulerStopped();
}

bool SyncScheduler::isRunning() const {
    return m_isRunning;
}

void SyncScheduler::setCheckInterval(int seconds) {
    m_checkIntervalSec = qMax(10, seconds);  // Minimum 10 seconds
    if (m_isRunning) {
        m_checkTimer->setInterval(m_checkIntervalSec * 1000);
    }
}

int SyncScheduler::checkInterval() const {
    return m_checkIntervalSec;
}

int SyncScheduler::addTask(const ScheduledTask& task) {
    ScheduledTask newTask = task;
    newTask.id = generateTaskId();

    // Set initial next run time if not set
    if (!newTask.nextRunTime.isValid()) {
        newTask.nextRunTime = QDateTime::currentDateTime().addSecs(60);
    }

    m_tasks.append(newTask);
    saveTasks();
    emit tasksChanged();

    qDebug() << "Added scheduled task:" << newTask.name << "ID:" << newTask.id;
    return newTask.id;
}

bool SyncScheduler::removeTask(int taskId) {
    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks[i].id == taskId) {
            qDebug() << "Removing scheduled task:" << m_tasks[i].name;
            m_tasks.removeAt(i);
            saveTasks();
            emit tasksChanged();
            return true;
        }
    }
    return false;
}

bool SyncScheduler::updateTask(const ScheduledTask& task) {
    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks[i].id == task.id) {
            m_tasks[i] = task;
            saveTasks();
            emit tasksChanged();
            return true;
        }
    }
    return false;
}

ScheduledTask* SyncScheduler::getTask(int taskId) {
    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks[i].id == taskId) {
            return &m_tasks[i];
        }
    }
    return nullptr;
}

QVector<ScheduledTask> SyncScheduler::getAllTasks() const {
    return m_tasks;
}

void SyncScheduler::setTaskEnabled(int taskId, bool enabled) {
    if (ScheduledTask* task = getTask(taskId)) {
        task->enabled = enabled;
        saveTasks();
        emit tasksChanged();
    }
}

bool SyncScheduler::isTaskEnabled(int taskId) const {
    for (const auto& task : m_tasks) {
        if (task.id == taskId) {
            return task.enabled;
        }
    }
    return false;
}

void SyncScheduler::runTaskNow(int taskId) {
    if (ScheduledTask* task = getTask(taskId)) {
        if (!task->isRunning) {
            executeTask(*task);
        } else {
            qDebug() << "Task already running:" << task->name;
        }
    }
}

void SyncScheduler::setFolderMapperController(FolderMapperController* controller) {
    // Disconnect from old controller
    if (m_folderMapperController) {
        disconnect(m_folderMapperController, nullptr, this, nullptr);
    }

    m_folderMapperController = controller;

    // Connect to new controller
    if (m_folderMapperController) {
        connect(m_folderMapperController, &FolderMapperController::uploadProgress,
                this, &SyncScheduler::onFolderMapperProgress);
        connect(m_folderMapperController, &FolderMapperController::uploadComplete,
                this, &SyncScheduler::onFolderMapperComplete);
    }
}

void SyncScheduler::setSmartSyncController(SmartSyncController* controller) {
    m_smartSyncController = controller;
}

void SyncScheduler::setMultiUploaderController(MultiUploaderController* controller) {
    m_multiUploaderController = controller;
}

void SyncScheduler::onTimerTick() {
    QDateTime now = QDateTime::currentDateTime();

    for (auto& task : m_tasks) {
        if (!task.enabled || task.isRunning) continue;

        if (task.nextRunTime.isValid() && task.nextRunTime <= now) {
            qDebug() << "Task due:" << task.name << "scheduled for" << task.nextRunTime;
            executeTask(task);
        }
    }
}

void SyncScheduler::executeTask(ScheduledTask& task) {
    task.isRunning = true;
    m_currentRunningTaskId = task.id;

    qDebug() << "Executing task:" << task.name << "Type:" << static_cast<int>(task.type);
    emit taskStarted(task.id, task.name);

    switch (task.type) {
        case ScheduledTask::TaskType::FOLDER_MAPPING:
            executeFolderMapping(task);
            break;
        case ScheduledTask::TaskType::SMART_SYNC:
            executeSmartSync(task);
            break;
        case ScheduledTask::TaskType::MULTI_UPLOAD:
            executeMultiUpload(task);
            break;
    }
}

void SyncScheduler::executeFolderMapping(ScheduledTask& task) {
    if (!m_folderMapperController) {
        task.isRunning = false;
        task.lastStatus = "No FolderMapper controller available";
        task.consecutiveFailures++;
        emit taskCompleted(task.id, task.name, false, task.lastStatus);
        return;
    }

    // Start the folder mapping upload operation
    // If profileName is set, use it; otherwise use localPath as mapping name
    QString mappingName = task.profileName.isEmpty() ? task.localPath : task.profileName;
    m_folderMapperController->uploadMapping(mappingName, false, true);  // Not dry run, incremental
}

void SyncScheduler::executeSmartSync(ScheduledTask& task) {
    if (!m_smartSyncController) {
        task.isRunning = false;
        task.lastStatus = "No SmartSync controller available";
        task.consecutiveFailures++;
        emit taskCompleted(task.id, task.name, false, task.lastStatus);
        return;
    }

    // Use profileName as the sync profile ID
    QString profileId = task.profileName;
    if (profileId.isEmpty()) {
        task.isRunning = false;
        task.lastStatus = "No sync profile specified";
        task.consecutiveFailures++;
        emit taskCompleted(task.id, task.name, false, task.lastStatus);
        return;
    }

    // Connect to completion signal for this task
    QObject::connect(m_smartSyncController, &SmartSyncController::syncComplete,
                     this, [this, taskId = task.id](const QString& profileId, bool success,
                            int uploaded, int downloaded, int errors) {
        Q_UNUSED(profileId);
        Q_UNUSED(uploaded);
        Q_UNUSED(downloaded);

        ScheduledTask* task = getTask(taskId);
        if (task && task->isRunning) {
            task->isRunning = false;
            task->lastRunTime = QDateTime::currentDateTime();
            task->lastStatus = success ? QString("Sync complete") :
                              QString("Sync failed with %1 errors").arg(errors);
            if (success) {
                task->consecutiveFailures = 0;
            } else {
                task->consecutiveFailures++;
            }
            updateNextRunTime(*task);
            emit taskCompleted(task->id, task->name, success, task->lastStatus);
            saveTasks();
        }
    }, Qt::SingleShotConnection);

    // Start the sync
    m_smartSyncController->startSync(profileId);
}

void SyncScheduler::executeMultiUpload(ScheduledTask& task) {
    if (!m_multiUploaderController) {
        task.isRunning = false;
        task.lastStatus = "No MultiUploader controller available";
        task.consecutiveFailures++;
        emit taskCompleted(task.id, task.name, false, task.lastStatus);
        return;
    }

    // Connect to completion signal for this task
    QObject::connect(m_multiUploaderController, &MultiUploaderController::uploadComplete,
                     this, [this, taskId = task.id](int successCount, int failCount, int skipCount) {
        Q_UNUSED(skipCount);

        ScheduledTask* task = getTask(taskId);
        if (task && task->isRunning) {
            task->isRunning = false;
            task->lastRunTime = QDateTime::currentDateTime();
            bool success = (failCount == 0);
            task->lastStatus = success ? QString("Uploaded %1 files").arg(successCount) :
                              QString("Uploaded %1, failed %2").arg(successCount).arg(failCount);
            if (success) {
                task->consecutiveFailures = 0;
            } else {
                task->consecutiveFailures++;
            }
            updateNextRunTime(*task);
            emit taskCompleted(task->id, task->name, success, task->lastStatus);
            saveTasks();
        }
    }, Qt::SingleShotConnection);

    // Start the upload
    m_multiUploaderController->startUpload();
}

void SyncScheduler::onFolderMapperProgress(const QString& mappingName, const QString& currentFile,
                                           int filesCompleted, int totalFiles,
                                           qint64 bytesUploaded, qint64 totalBytes,
                                           double speedBytesPerSec) {
    Q_UNUSED(mappingName);
    Q_UNUSED(bytesUploaded);
    Q_UNUSED(totalBytes);
    Q_UNUSED(speedBytesPerSec);

    if (m_currentRunningTaskId >= 0) {
        int percent = (totalFiles > 0) ? (filesCompleted * 100 / totalFiles) : 0;
        QString status = QString("Processing: %1 (%2/%3)").arg(currentFile).arg(filesCompleted).arg(totalFiles);
        emit taskProgress(m_currentRunningTaskId, percent, status);
    }
}

void SyncScheduler::onFolderMapperComplete(const QString& mappingName, bool success,
                                           int filesUploaded, int filesSkipped, int filesFailed) {
    Q_UNUSED(mappingName);

    if (m_currentRunningTaskId >= 0) {
        ScheduledTask* task = getTask(m_currentRunningTaskId);
        if (task) {
            task->isRunning = false;
            task->lastRunTime = QDateTime::currentDateTime();
            task->lastStatus = QString("Uploaded: %1, Skipped: %2, Failed: %3")
                               .arg(filesUploaded).arg(filesSkipped).arg(filesFailed);

            if (success) {
                task->consecutiveFailures = 0;
            } else {
                task->consecutiveFailures++;
            }

            updateNextRunTime(*task);
            saveTasks();

            emit taskCompleted(task->id, task->name, success, task->lastStatus);
        }
        m_currentRunningTaskId = -1;
    }
}

void SyncScheduler::updateNextRunTime(ScheduledTask& task) {
    QDateTime now = QDateTime::currentDateTime();

    switch (task.repeatMode) {
        case ScheduledTask::RepeatMode::ONCE:
            task.nextRunTime = QDateTime();  // Invalid = won't run again
            task.enabled = false;
            break;
        case ScheduledTask::RepeatMode::HOURLY:
            task.nextRunTime = now.addSecs(3600);
            break;
        case ScheduledTask::RepeatMode::DAILY:
            task.nextRunTime = now.addDays(1);
            break;
        case ScheduledTask::RepeatMode::WEEKLY:
            task.nextRunTime = now.addDays(7);
            break;
    }

    qDebug() << "Task" << task.name << "next run:" << task.nextRunTime;
}

int SyncScheduler::generateTaskId() {
    return m_nextTaskId++;
}

void SyncScheduler::loadTasks() {
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                         + "/MegaCustom/scheduler.json";

    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "No scheduler config found, starting fresh";
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) return;

    QJsonObject root = doc.object();
    m_nextTaskId = root["nextTaskId"].toInt(1);
    m_checkIntervalSec = root["checkInterval"].toInt(60);

    QJsonArray tasksArray = root["tasks"].toArray();
    m_tasks.clear();

    for (const QJsonValue& val : tasksArray) {
        QJsonObject obj = val.toObject();
        ScheduledTask task;

        task.id = obj["id"].toInt();
        task.name = obj["name"].toString();
        task.type = static_cast<ScheduledTask::TaskType>(obj["type"].toInt());
        task.repeatMode = static_cast<ScheduledTask::RepeatMode>(obj["repeatMode"].toInt());
        task.nextRunTime = QDateTime::fromString(obj["nextRunTime"].toString(), Qt::ISODate);
        task.lastRunTime = QDateTime::fromString(obj["lastRunTime"].toString(), Qt::ISODate);
        task.enabled = obj["enabled"].toBool(true);
        task.localPath = obj["localPath"].toString();
        task.remotePath = obj["remotePath"].toString();
        task.profileName = obj["profileName"].toString();
        task.lastStatus = obj["lastStatus"].toString();
        task.consecutiveFailures = obj["consecutiveFailures"].toInt(0);

        m_tasks.append(task);
    }

    qDebug() << "Loaded" << m_tasks.size() << "scheduled tasks";
}

void SyncScheduler::saveTasks() {
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                         + "/MegaCustom";
    if (!QDir().mkpath(configPath)) {
        qWarning() << "SyncScheduler: Failed to create config directory:" << configPath;
        return;
    }

    QJsonObject root;
    root["nextTaskId"] = m_nextTaskId;
    root["checkInterval"] = m_checkIntervalSec;

    QJsonArray tasksArray;
    for (const auto& task : m_tasks) {
        QJsonObject obj;
        obj["id"] = task.id;
        obj["name"] = task.name;
        obj["type"] = static_cast<int>(task.type);
        obj["repeatMode"] = static_cast<int>(task.repeatMode);
        obj["nextRunTime"] = task.nextRunTime.toString(Qt::ISODate);
        obj["lastRunTime"] = task.lastRunTime.toString(Qt::ISODate);
        obj["enabled"] = task.enabled;
        obj["localPath"] = task.localPath;
        obj["remotePath"] = task.remotePath;
        obj["profileName"] = task.profileName;
        obj["lastStatus"] = task.lastStatus;
        obj["consecutiveFailures"] = task.consecutiveFailures;

        tasksArray.append(obj);
    }
    root["tasks"] = tasksArray;

    QFile file(configPath + "/scheduler.json");
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
        qDebug() << "Saved" << m_tasks.size() << "scheduled tasks";
    }
}

} // namespace MegaCustom
