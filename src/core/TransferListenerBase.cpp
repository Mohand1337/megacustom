#include "core/TransferListenerBase.h"
#include <iostream>

namespace MegaCustom {

void TransferListenerBase::onTransferStart(mega::MegaApi* /*api*/, mega::MegaTransfer* transfer) {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_startTime = std::chrono::steady_clock::now();
    m_completed = false;

    // Initialize progress
    m_progress.transferTag = transfer->getTag();
    m_progress.fileName = transfer->getFileName() ? transfer->getFileName() : "";
    m_progress.totalBytes = transfer->getTotalBytes();
    m_progress.bytesTransferred = 0;
    m_progress.speed = 0;
    m_progress.progressPercentage = 0;
    m_progress.isPaused = false;

    // Notify derived class
    onTransferBegin(m_progress);
}

void TransferListenerBase::onTransferUpdate(mega::MegaApi* /*api*/, mega::MegaTransfer* transfer) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Update progress
    m_progress.bytesTransferred = transfer->getTransferredBytes();
    m_progress.speed = transfer->getSpeed();
    m_progress.isPaused = transfer->getState() == mega::MegaTransfer::STATE_PAUSED;

    // Calculate percentage
    if (m_progress.totalBytes > 0) {
        m_progress.progressPercentage = static_cast<int>(
            (m_progress.bytesTransferred * 100) / m_progress.totalBytes);
    }

    // Calculate ETA
    if (m_progress.speed > 0) {
        int64_t remaining = m_progress.totalBytes - m_progress.bytesTransferred;
        m_progress.estimatedTimeRemaining = std::chrono::seconds(
            remaining / static_cast<int64_t>(m_progress.speed));
    }

    // Notify derived class
    onTransferProgress(m_progress);

    // Call progress callback
    if (m_progressCallback) {
        m_progressCallback(m_progress);
    }
}

void TransferListenerBase::onTransferFinish(mega::MegaApi* /*api*/, mega::MegaTransfer* transfer,
                                             mega::MegaError* error) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Populate result
    m_result.success = (error->getErrorCode() == mega::MegaError::API_OK);
    m_result.fileName = transfer->getFileName() ? transfer->getFileName() : "";
    m_result.remotePath = transfer->getPath() ? transfer->getPath() : "";
    m_result.fileSize = transfer->getTotalBytes();
    m_result.errorCode = error->getErrorCode();
    m_result.errorMessage = error->getErrorString() ? error->getErrorString() : "";
    m_result.transferType = transfer->getType();

    // Calculate duration
    auto endTime = std::chrono::steady_clock::now();
    m_result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - m_startTime);

    // Notify derived class
    onTransferComplete(m_result);

    // Call completion callback
    if (m_completeCallback) {
        m_completeCallback(m_result);
    }

    // Signal completion
    m_completed = true;
    m_cv.notify_all();
}

void TransferListenerBase::onTransferTemporaryError(mega::MegaApi* /*api*/,
                                                      mega::MegaTransfer* /*transfer*/,
                                                      mega::MegaError* error) {
    int errorCode = error->getErrorCode();
    std::string errorMessage = error->getErrorString() ? error->getErrorString() : "";

    // Notify derived class (no lock needed as this is informational)
    onTransferRetry(errorCode, errorMessage);
}

bool TransferListenerBase::waitForCompletion(int timeoutSeconds) {
    std::unique_lock<std::mutex> lock(m_mutex);

    if (m_completed) {
        return true;
    }

    if (timeoutSeconds < 0) {
        // Wait indefinitely
        m_cv.wait(lock, [this] { return m_completed.load(); });
        return true;
    }

    return m_cv.wait_for(lock, std::chrono::seconds(timeoutSeconds),
                         [this] { return m_completed.load(); });
}

void TransferListenerBase::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_completed = false;
    m_progress = TransferProgressInfo{};
    m_result = TransferResultInfo{};
}

TransferProgressInfo TransferListenerBase::getProgress() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_progress;
}

TransferResultInfo TransferListenerBase::getResult() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_result;
}

} // namespace MegaCustom
