#ifndef MEGACUSTOM_TRANSFERPROGRESSLISTENER_H
#define MEGACUSTOM_TRANSFERPROGRESSLISTENER_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <megaapi.h>

namespace MegaCustom {

/**
 * @brief Reusable listener for MEGA transfer operations
 *
 * This listener receives callbacks from MEGA SDK during uploads/downloads
 * and marshals them to the Qt main thread via signals.
 *
 * Usage:
 *   auto* listener = new TransferProgressListener(this);
 *   connect(listener, &TransferProgressListener::progressUpdated, ...);
 *   connect(listener, &TransferProgressListener::transferFinished, ...);
 *   api->startUpload(..., listener);
 */
class TransferProgressListener : public QObject, public mega::MegaTransferListener {
    Q_OBJECT

public:
    explicit TransferProgressListener(QObject* parent = nullptr);
    ~TransferProgressListener() override = default;

    // Optional: Associate custom data with this listener
    void setTaskId(int taskId) { m_taskId = taskId; }
    int taskId() const { return m_taskId; }

    void setUserData(const QVariant& data) { m_userData = data; }
    QVariant userData() const { return m_userData; }

signals:
    /**
     * @brief Progress update during transfer
     * @param taskId Custom task ID (if set)
     * @param bytesTransferred Bytes transferred so far
     * @param totalBytes Total bytes to transfer
     * @param speedBps Current transfer speed in bytes per second
     */
    void progressUpdated(int taskId, qint64 bytesTransferred, qint64 totalBytes, double speedBps);

    /**
     * @brief Transfer completed (success or failure)
     * @param taskId Custom task ID (if set)
     * @param success True if transfer succeeded
     * @param errorMessage Error message if failed, empty if success
     */
    void transferFinished(int taskId, bool success, const QString& errorMessage);

    /**
     * @brief Transfer started
     * @param taskId Custom task ID (if set)
     * @param fileName Name of file being transferred
     */
    void transferStarted(int taskId, const QString& fileName);

protected:
    // MegaTransferListener interface
    void onTransferStart(mega::MegaApi* api, mega::MegaTransfer* transfer) override;
    void onTransferUpdate(mega::MegaApi* api, mega::MegaTransfer* transfer) override;
    void onTransferFinish(mega::MegaApi* api, mega::MegaTransfer* transfer, mega::MegaError* error) override;
    void onTransferTemporaryError(mega::MegaApi* api, mega::MegaTransfer* transfer, mega::MegaError* error) override;

private:
    int m_taskId = 0;
    QVariant m_userData;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_TRANSFERPROGRESSLISTENER_H
