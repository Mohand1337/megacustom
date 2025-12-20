#include "FolderMapperController.h"
#include "features/FolderMapper.h"
#include "core/MegaManager.h"
#include <QDebug>
#include <QThread>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

namespace MegaCustom {

FolderMapperController::FolderMapperController(void* megaApi, QObject* parent)
    : QObject(parent)
    , m_megaApi(megaApi)
{
    qDebug() << "FolderMapperController: Initialized";
}

FolderMapperController::~FolderMapperController()
{
}

void FolderMapperController::loadMappings()
{
    qDebug() << "FolderMapperController: Loading mappings";

    // Clear the table first to prevent doubling
    emit clearMappings();

    try {
        mega::MegaApi* api = static_cast<mega::MegaApi*>(m_megaApi);
        FolderMapper mapper(api);

        if (!mapper.loadMappings()) {
            qDebug() << "FolderMapperController: No existing mappings file or failed to load";
        }

        auto mappings = mapper.getAllMappings();
        m_mappingCount = mappings.size();

        // Emit signal for each mapping
        for (const auto& mapping : mappings) {
            emit mappingAdded(
                QString::fromStdString(mapping.name),
                QString::fromStdString(mapping.localPath),
                QString::fromStdString(mapping.remotePath),
                mapping.enabled
            );
        }

        emit mappingsLoaded(m_mappingCount);
    } catch (const std::exception& e) {
        emit error("Load Mappings", QString::fromStdString(e.what()));
    }
}

void FolderMapperController::saveMappings()
{
    qDebug() << "FolderMapperController: Saving mappings";

    try {
        mega::MegaApi* api = static_cast<mega::MegaApi*>(m_megaApi);
        FolderMapper mapper(api);
        mapper.loadMappings();

        if (!mapper.saveMappings()) {
            emit error("Save Mappings", "Failed to save mappings to file");
        }
    } catch (const std::exception& e) {
        emit error("Save Mappings", QString::fromStdString(e.what()));
    }
}

void FolderMapperController::addMapping(const QString& name, const QString& localPath,
                                         const QString& remotePath)
{
    qDebug() << "FolderMapperController: Adding mapping" << name;

    try {
        mega::MegaApi* api = static_cast<mega::MegaApi*>(m_megaApi);
        FolderMapper mapper(api);
        mapper.loadMappings();

        bool success = mapper.addMapping(
            name.toStdString(),
            localPath.toStdString(),
            remotePath.toStdString()
        );

        if (success) {
            mapper.saveMappings();
            m_mappingCount++;
            emit mappingAdded(name, localPath, remotePath, true);
        } else {
            emit error("Add Mapping", QString("Failed to add mapping '%1'").arg(name));
        }
    } catch (const std::exception& e) {
        emit error("Add Mapping", QString::fromStdString(e.what()));
    }
}

void FolderMapperController::removeMapping(const QString& name)
{
    qDebug() << "FolderMapperController: Removing mapping" << name;

    try {
        mega::MegaApi* api = static_cast<mega::MegaApi*>(m_megaApi);
        FolderMapper mapper(api);
        mapper.loadMappings();

        bool success = mapper.removeMapping(name.toStdString());

        if (success) {
            mapper.saveMappings();
            m_mappingCount--;
            emit mappingRemoved(name);
        } else {
            emit error("Remove Mapping", QString("Failed to remove mapping '%1'").arg(name));
        }
    } catch (const std::exception& e) {
        emit error("Remove Mapping", QString::fromStdString(e.what()));
    }
}

void FolderMapperController::updateMapping(const QString& name, const QString& localPath,
                                            const QString& remotePath)
{
    qDebug() << "FolderMapperController: Updating mapping" << name;

    try {
        mega::MegaApi* api = static_cast<mega::MegaApi*>(m_megaApi);
        FolderMapper mapper(api);
        mapper.loadMappings();

        bool success = mapper.updateMapping(
            name.toStdString(),
            localPath.toStdString(),
            remotePath.toStdString()
        );

        if (success) {
            mapper.saveMappings();
            emit mappingUpdated(name);
        } else {
            emit error("Update Mapping", QString("Failed to update mapping '%1'").arg(name));
        }
    } catch (const std::exception& e) {
        emit error("Update Mapping", QString::fromStdString(e.what()));
    }
}

void FolderMapperController::setMappingEnabled(const QString& name, bool enabled)
{
    qDebug() << "FolderMapperController: Setting mapping" << name << "enabled:" << enabled;

    try {
        mega::MegaApi* api = static_cast<mega::MegaApi*>(m_megaApi);
        FolderMapper mapper(api);
        mapper.loadMappings();

        bool success = mapper.setMappingEnabled(name.toStdString(), enabled);

        if (success) {
            mapper.saveMappings();
            emit mappingUpdated(name);
        } else {
            emit error("Enable/Disable", QString("Failed to %1 mapping '%2'")
                .arg(enabled ? "enable" : "disable").arg(name));
        }
    } catch (const std::exception& e) {
        emit error("Enable/Disable", QString::fromStdString(e.what()));
    }
}

void FolderMapperController::uploadMapping(const QString& name, bool dryRun, bool incremental)
{
    qDebug() << "FolderMapperController: Uploading mapping" << name
             << "dryRun:" << dryRun << "incremental:" << incremental;

    if (m_isUploading) {
        emit error("Upload", "An upload is already in progress");
        return;
    }

    m_isUploading = true;
    m_cancelRequested = false;

    // Run upload in background thread
    QtConcurrent::run([this, name, dryRun, incremental]() {
        try {
            mega::MegaApi* api = static_cast<mega::MegaApi*>(m_megaApi);
            FolderMapper mapper(api);
            mapper.loadMappings();

            // Set up progress callback
            mapper.setProgressCallback([this, name](const MapUploadProgress& progress) {
                QMetaObject::invokeMethod(this, [this, name, progress]() {
                    emit uploadProgress(
                        name,
                        QString::fromStdString(progress.currentFile),
                        progress.uploadedFiles,
                        progress.totalFiles,
                        progress.uploadedBytes,
                        progress.totalBytes,
                        progress.speedBytesPerSec
                    );
                }, Qt::QueuedConnection);
            });

            // Emit upload started
            QMetaObject::invokeMethod(this, [this, name]() {
                emit uploadStarted(name);
            }, Qt::QueuedConnection);

            // Perform upload
            UploadOptions options;
            options.dryRun = dryRun;
            options.incremental = incremental;
            options.recursive = true;
            options.showProgress = true;

            MapUploadResult result = mapper.uploadMapping(name.toStdString(), options);

            // Emit completion
            QMetaObject::invokeMethod(this, [this, name, result]() {
                m_isUploading = false;
                emit uploadComplete(
                    name,
                    result.success,
                    result.filesUploaded,
                    result.filesSkipped,
                    result.filesFailed
                );
            }, Qt::QueuedConnection);

        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, e]() {
                m_isUploading = false;
                emit error("Upload", QString::fromStdString(e.what()));
            }, Qt::QueuedConnection);
        }
    });
}

void FolderMapperController::uploadAll(bool dryRun, bool incremental)
{
    qDebug() << "FolderMapperController: Uploading all mappings";

    if (m_isUploading) {
        emit error("Upload", "An upload is already in progress");
        return;
    }

    m_isUploading = true;
    m_cancelRequested = false;

    // Run upload in background thread
    QtConcurrent::run([this, dryRun, incremental]() {
        try {
            mega::MegaApi* api = static_cast<mega::MegaApi*>(m_megaApi);
            FolderMapper mapper(api);
            mapper.loadMappings();

            UploadOptions options;
            options.dryRun = dryRun;
            options.incremental = incremental;
            options.recursive = true;
            options.showProgress = true;

            auto results = mapper.uploadAll(options);

            int totalUploaded = 0;
            int totalSkipped = 0;
            int totalFailed = 0;
            bool allSuccess = true;

            for (const auto& result : results) {
                totalUploaded += result.filesUploaded;
                totalSkipped += result.filesSkipped;
                totalFailed += result.filesFailed;
                if (!result.success) allSuccess = false;
            }

            QMetaObject::invokeMethod(this, [this, allSuccess, totalUploaded, totalSkipped, totalFailed]() {
                m_isUploading = false;
                emit uploadComplete("All Mappings", allSuccess, totalUploaded, totalSkipped, totalFailed);
            }, Qt::QueuedConnection);

        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, e]() {
                m_isUploading = false;
                emit error("Upload All", QString::fromStdString(e.what()));
            }, Qt::QueuedConnection);
        }
    });
}

void FolderMapperController::previewUpload(const QString& name)
{
    qDebug() << "FolderMapperController: Previewing upload for" << name;

    try {
        mega::MegaApi* api = static_cast<mega::MegaApi*>(m_megaApi);
        FolderMapper mapper(api);
        mapper.loadMappings();

        UploadOptions options;
        options.dryRun = true;
        options.incremental = true;
        options.recursive = true;

        auto preview = mapper.previewUpload(name.toStdString(), options);

        int toUpload = 0;
        int toSkip = 0;
        qint64 totalBytes = 0;

        for (const auto& file : preview) {
            if (file.needsUpload) {
                toUpload++;
                totalBytes += file.localSize;
            } else {
                toSkip++;
            }
        }

        emit previewReady(name, toUpload, toSkip, totalBytes);

    } catch (const std::exception& e) {
        emit error("Preview", QString::fromStdString(e.what()));
    }
}

void FolderMapperController::cancelUpload()
{
    qDebug() << "FolderMapperController: Cancelling upload";
    m_cancelRequested = true;
    // Note: The actual cancellation would need to be implemented in FolderMapper
    // by checking a flag periodically during upload
}

} // namespace MegaCustom
