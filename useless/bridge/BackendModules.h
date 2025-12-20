#ifndef BACKEND_MODULES_H
#define BACKEND_MODULES_H

// Include the real CLI modules from the main project
// These are the actual implementations we'll use instead of stubs

// Core modules
#include "../../../include/core/MegaManager.h"
#include "../../../include/core/AuthenticationModule.h"
#include "../../../include/core/ConfigManager.h"

// Operations modules
#include "../../../include/operations/FileOperations.h"
#include "../../../include/operations/FolderManager.h"

// Note: TransferManager might need to be created or found
// For now, we'll create a simple adapter

namespace MegaCustom {

/**
 * TransferManager adapter for the GUI
 * This wraps the FileOperations transfer functionality
 */
class TransferManager {
private:
    mega::MegaApi* m_api;
    FileOperations* m_fileOps;

public:
    explicit TransferManager(mega::MegaApi* api)
        : m_api(api), m_fileOps(nullptr) {
        if (m_api) {
            m_fileOps = new FileOperations(m_api);
        }
    }

    ~TransferManager() {
        delete m_fileOps;
    }

    void addTransfer(const std::string& path) {
        // Use FileOperations to handle transfers
        if (m_fileOps) {
            // For now, this is a placeholder
            // The actual implementation would track transfer handles
            // and coordinate with FileOperations upload/download methods
        }
    }

    void cancelTransfer(const std::string& path) {
        // Use FileOperations to cancel transfers
        if (m_fileOps) {
            // For now, this is a placeholder
            // The actual implementation would use transfer handles
            // to cancel specific transfers via the Mega API
        }
    }

    FileOperations* getFileOperations() {
        return m_fileOps;
    }
};

} // namespace MegaCustom

#endif // BACKEND_MODULES_H