/**
 * TransferProgressListener implementation
 * Marshals MEGA SDK transfer callbacks to Qt signals on the main thread
 */

#include "TransferProgressListener.h"
#include <QMetaObject>
#include <QDebug>

namespace MegaCustom {

TransferProgressListener::TransferProgressListener(QObject* parent)
    : QObject(parent)
{
}

void TransferProgressListener::onTransferStart(mega::MegaApi* api, mega::MegaTransfer* transfer) {
    Q_UNUSED(api);

    QString fileName = QString::fromUtf8(transfer->getFileName());
    int taskId = m_taskId;

    QMetaObject::invokeMethod(this, [this, taskId, fileName]() {
        emit transferStarted(taskId, fileName);
    }, Qt::QueuedConnection);
}

void TransferProgressListener::onTransferUpdate(mega::MegaApi* api, mega::MegaTransfer* transfer) {
    Q_UNUSED(api);

    qint64 transferred = transfer->getTransferredBytes();
    qint64 total = transfer->getTotalBytes();
    double speed = transfer->getSpeed();
    int taskId = m_taskId;

    QMetaObject::invokeMethod(this, [this, taskId, transferred, total, speed]() {
        emit progressUpdated(taskId, transferred, total, speed);
    }, Qt::QueuedConnection);
}

void TransferProgressListener::onTransferFinish(mega::MegaApi* api, mega::MegaTransfer* transfer, mega::MegaError* error) {
    Q_UNUSED(api);
    Q_UNUSED(transfer);

    bool success = (error->getErrorCode() == mega::MegaError::API_OK);
    QString errorMsg = success ? QString() : QString::fromUtf8(error->getErrorString());
    int taskId = m_taskId;

    QMetaObject::invokeMethod(this, [this, taskId, success, errorMsg]() {
        emit transferFinished(taskId, success, errorMsg);

        // Auto-delete after emitting finished signal
        // Give time for the slot to process before deletion
        deleteLater();
    }, Qt::QueuedConnection);
}

void TransferProgressListener::onTransferTemporaryError(mega::MegaApi* api, mega::MegaTransfer* transfer, mega::MegaError* error) {
    Q_UNUSED(api);
    Q_UNUSED(transfer);

    // Log temporary errors but don't fail the transfer
    qDebug() << "TransferProgressListener: Temporary error for task" << m_taskId
             << ":" << QString::fromUtf8(error->getErrorString());
}

} // namespace MegaCustom
