#ifndef MEGACUSTOM_MEGAUPLOADUTILS_H
#define MEGACUSTOM_MEGAUPLOADUTILS_H

/**
 * Shared MEGA upload utilities.
 * Provides synchronous listeners and upload helpers for use in worker threads.
 * Used by DistributionController and WatermarkPanel's auto-upload pipeline.
 *
 * All functions block the calling thread — never call from the main/GUI thread.
 */

#include <megaapi.h>
#include <QDebug>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace MegaCustom {

// ==================== Synchronous Listeners ====================

/**
 * Synchronous listener for MEGA request operations (folder creation, etc.)
 * Uses condition_variable to block calling thread until completion.
 */
class SyncRequestListener : public mega::MegaRequestListener {
public:
    void onRequestFinish(mega::MegaApi*, mega::MegaRequest* request,
                        mega::MegaError* error) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_success = (error->getErrorCode() == mega::MegaError::API_OK);
        m_errorCode = error->getErrorCode();
        m_errorString = error->getErrorString() ? error->getErrorString() : "";
        if (request->getNodeHandle()) {
            m_nodeHandle = request->getNodeHandle();
        }
        m_finished = true;
        m_cv.notify_all();
    }

    bool waitForCompletion(int timeoutSeconds = 60) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, std::chrono::seconds(timeoutSeconds),
                            [this] { return m_finished; });
    }

    bool isSuccess() const { return m_success; }
    int errorCode() const { return m_errorCode; }
    std::string errorString() const { return m_errorString; }
    mega::MegaHandle nodeHandle() const { return m_nodeHandle; }

    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_finished = false;
        m_success = false;
        m_errorCode = 0;
        m_errorString.clear();
        m_nodeHandle = mega::INVALID_HANDLE;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_finished = false;
    bool m_success = false;
    int m_errorCode = 0;
    std::string m_errorString;
    mega::MegaHandle m_nodeHandle = mega::INVALID_HANDLE;
};

/**
 * Synchronous listener for MEGA transfer operations (uploads).
 * Uses condition_variable to block calling thread until transfer completes.
 */
class SyncTransferListener : public mega::MegaTransferListener {
public:
    void onTransferFinish(mega::MegaApi*, mega::MegaTransfer*,
                         mega::MegaError* error) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_success = (error->getErrorCode() == mega::MegaError::API_OK);
        m_errorCode = error->getErrorCode();
        m_errorString = error->getErrorString() ? error->getErrorString() : "";
        m_finished = true;
        m_cv.notify_all();
    }

    void onTransferUpdate(mega::MegaApi*, mega::MegaTransfer*) override {
        // Could track progress here if needed
    }

    void onTransferTemporaryError(mega::MegaApi*, mega::MegaTransfer*,
                                  mega::MegaError*) override {
        // SDK will retry automatically
    }

    bool waitForCompletion(int timeoutSeconds = 600) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, std::chrono::seconds(timeoutSeconds),
                            [this] { return m_finished; });
    }

    bool isSuccess() const { return m_success; }
    int errorCode() const { return m_errorCode; }
    std::string errorString() const { return m_errorString; }

    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_finished = false;
        m_success = false;
        m_errorCode = 0;
        m_errorString.clear();
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_finished = false;
    bool m_success = false;
    int m_errorCode = 0;
    std::string m_errorString;
};

// ==================== MegaApi Upload Helpers ====================

/**
 * Ensure a MEGA cloud folder exists, creating it recursively if needed.
 * @return The MegaNode for the folder, or nullptr on failure.
 * Caller takes ownership of the returned pointer.
 */
inline mega::MegaNode* ensureFolderExists(mega::MegaApi* api, const std::string& path) {
    if (!api || path.empty()) return nullptr;

    // Try to find existing folder
    mega::MegaNode* node = api->getNodeByPath(path.c_str());
    if (node) return node;

    // Split path into components
    std::vector<std::string> components;
    std::string current;
    for (char c : path) {
        if (c == '/') {
            if (!current.empty()) {
                components.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        components.push_back(current);
    }

    // Start from root
    std::unique_ptr<mega::MegaNode> currentNode(api->getRootNode());
    if (!currentNode) return nullptr;

    SyncRequestListener listener;

    for (const auto& component : components) {
        std::unique_ptr<mega::MegaNode> childNode(
            api->getChildNode(currentNode.get(), component.c_str())
        );

        if (childNode && childNode->isFolder()) {
            currentNode = std::move(childNode);
        } else if (!childNode) {
            // Create folder
            listener.reset();
            api->createFolder(component.c_str(), currentNode.get(), &listener);

            if (!listener.waitForCompletion(30)) {
                qWarning() << "ensureFolderExists: Timeout creating folder:" << component.c_str();
                return nullptr;
            }

            if (!listener.isSuccess()) {
                qWarning() << "ensureFolderExists: Failed to create folder:" << component.c_str()
                           << "error:" << listener.errorString().c_str();
                return nullptr;
            }

            // Get the newly created folder
            currentNode.reset(api->getChildNode(currentNode.get(), component.c_str()));
            if (!currentNode) {
                return nullptr;
            }
        } else {
            // Component exists but is not a folder
            return nullptr;
        }
    }

    return currentNode.release();
}

/**
 * Upload a local file to a MEGA cloud folder using MegaApi.
 * Blocks until completion (for use in worker threads only).
 */
inline bool megaApiUpload(mega::MegaApi* api, const std::string& localPath,
                          const std::string& remotePath, std::string& error) {
    if (!api) {
        error = "MegaApi not available";
        return false;
    }

    // Resolve destination folder
    std::unique_ptr<mega::MegaNode> destNode(ensureFolderExists(api, remotePath));
    if (!destNode) {
        error = "Cannot access or create destination folder: " + remotePath;
        return false;
    }

    // Upload file
    SyncTransferListener listener;
    api->startUpload(localPath.c_str(),
                     destNode.get(),
                     nullptr,   // filename (use original)
                     0,         // mtime
                     nullptr,   // appData
                     false,     // isSourceTemporary
                     false,     // startFirst
                     nullptr,   // cancelToken
                     &listener);

    // Wait for upload to complete (10 min timeout for large files)
    if (!listener.waitForCompletion(600)) {
        error = "Upload timeout for: " + localPath;
        return false;
    }

    if (!listener.isSuccess()) {
        error = "Upload failed: " + listener.errorString();
        return false;
    }

    return true;
}

} // namespace MegaCustom

#endif // MEGACUSTOM_MEGAUPLOADUTILS_H
