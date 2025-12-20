#ifndef TRANSFER_LISTENER_BASE_H
#define TRANSFER_LISTENER_BASE_H

#include <megaapi.h>
#include <string>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <atomic>

namespace MegaCustom {

/**
 * Progress information for ongoing transfers
 */
struct TransferProgressInfo {
    std::string fileName;
    int64_t totalBytes = 0;
    int64_t bytesTransferred = 0;
    int64_t speed = 0;                    // bytes per second
    int progressPercentage = 0;           // 0-100
    std::chrono::seconds estimatedTimeRemaining{0};
    bool isPaused = false;
    int transferTag = 0;
};

/**
 * Result of a completed transfer
 */
struct TransferResultInfo {
    bool success = false;
    std::string fileName;
    std::string remotePath;
    int64_t fileSize = 0;
    int errorCode = 0;
    std::string errorMessage;
    std::chrono::milliseconds duration{0};
    int transferType = 0;  // MegaTransfer::TYPE_UPLOAD or TYPE_DOWNLOAD
};

/**
 * Callbacks for transfer events
 */
using TransferProgressCallback = std::function<void(const TransferProgressInfo&)>;
using TransferCompleteCallback = std::function<void(const TransferResultInfo&)>;

/**
 * Base class for transfer listeners
 *
 * Provides common functionality for tracking transfer progress,
 * waiting for completion, and handling errors. Derived classes
 * can extend this with additional functionality.
 *
 * Features:
 * - Thread-safe progress tracking
 * - Condition variable for synchronous waiting
 * - Progress and completion callbacks
 * - Timeout support
 *
 * Example usage:
 *   class MyUploadListener : public TransferListenerBase {
 *   protected:
 *       void onTransferComplete(const TransferResultInfo& result) override {
 *           // Custom completion logic
 *           TransferListenerBase::onTransferComplete(result);  // Call base
 *       }
 *   };
 */
class TransferListenerBase : public mega::MegaTransferListener {
public:
    TransferListenerBase() = default;
    virtual ~TransferListenerBase() = default;

    // ==================== MegaTransferListener Interface ====================

    void onTransferStart(mega::MegaApi* api, mega::MegaTransfer* transfer) override;
    void onTransferUpdate(mega::MegaApi* api, mega::MegaTransfer* transfer) override;
    void onTransferFinish(mega::MegaApi* api, mega::MegaTransfer* transfer,
                          mega::MegaError* error) override;
    void onTransferTemporaryError(mega::MegaApi* api, mega::MegaTransfer* transfer,
                                   mega::MegaError* error) override;

    // ==================== Configuration ====================

    /**
     * Set callback for progress updates
     */
    void setProgressCallback(TransferProgressCallback callback) {
        m_progressCallback = std::move(callback);
    }

    /**
     * Set callback for transfer completion
     */
    void setCompleteCallback(TransferCompleteCallback callback) {
        m_completeCallback = std::move(callback);
    }

    // ==================== Synchronous Operations ====================

    /**
     * Wait for the transfer to complete
     * @param timeoutSeconds Maximum time to wait (-1 = infinite)
     * @return true if completed, false if timed out
     */
    bool waitForCompletion(int timeoutSeconds = 300);

    /**
     * Check if transfer has completed (success or failure)
     */
    bool isCompleted() const { return m_completed.load(); }

    /**
     * Reset listener for reuse
     */
    void reset();

    // ==================== Results ====================

    /**
     * Get the current progress information
     */
    TransferProgressInfo getProgress() const;

    /**
     * Get the result after completion
     */
    TransferResultInfo getResult() const;

    /**
     * Check if transfer succeeded
     */
    bool wasSuccessful() const { return m_result.success; }

    /**
     * Get error message (empty if successful)
     */
    std::string getErrorMessage() const { return m_result.errorMessage; }

protected:
    /**
     * Called when transfer starts
     * Override to add custom behavior
     */
    virtual void onTransferBegin(const TransferProgressInfo& progress) {}

    /**
     * Called on progress update
     * Override to add custom behavior
     */
    virtual void onTransferProgress(const TransferProgressInfo& progress) {}

    /**
     * Called when transfer completes
     * Override to add custom behavior
     */
    virtual void onTransferComplete(const TransferResultInfo& result) {}

    /**
     * Called on temporary error (SDK will retry)
     * Override to add custom logging
     */
    virtual void onTransferRetry(int errorCode, const std::string& errorMessage) {}

protected:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_completed{false};

    TransferProgressInfo m_progress;
    TransferResultInfo m_result;
    std::chrono::steady_clock::time_point m_startTime;

    TransferProgressCallback m_progressCallback;
    TransferCompleteCallback m_completeCallback;
};

} // namespace MegaCustom

#endif // TRANSFER_LISTENER_BASE_H
