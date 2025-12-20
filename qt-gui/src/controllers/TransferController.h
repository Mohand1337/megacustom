#ifndef TRANSFER_CONTROLLER_H
#define TRANSFER_CONTROLLER_H

#include <QObject>
#include <QString>
#include <memory>

namespace MegaCustom {

// Forward declaration
class TransferControllerPrivate;

class TransferController : public QObject {
    Q_OBJECT

public:
    TransferController(void* api);
    ~TransferController();

    bool hasActiveTransfers() const;
    void cancelAllTransfers();
    void uploadFile(const QString& localPath, const QString& remotePath);
    void uploadFolder(const QString& localPath, const QString& remotePath);
    void downloadFile(const QString& remotePath, const QString& localPath);

signals:
    // Existing signals
    void transferStarted(const QString& path);
    void transferProgress(const QString& transferId, qint64 transferred, qint64 total, qint64 speed, int timeRemaining);
    void transferCompleted(const QString& path);
    void transferFailed(const QString& path, const QString& error);

    // Bridge signals for queue management
    void addTransfer(const QString& type, const QString& sourcePath, const QString& destPath, qint64 size);
    void pauseTransfer(const QString& transferId);
    void resumeTransfer(const QString& transferId);
    void cancelTransfer(const QString& transferId);

    // Response signals
    void transferComplete(const QString& transferId);
    void queueStatusChanged(int active, int pending, int completed, int failed);

    // Global speed update for status bar
    void globalSpeedUpdate(qint64 uploadSpeed, qint64 downloadSpeed);

private:
    std::unique_ptr<TransferControllerPrivate> m_d;
};

} // namespace MegaCustom

#endif // TRANSFER_CONTROLLER_H