#ifndef BACKEND_BRIDGE_H
#define BACKEND_BRIDGE_H

#include <QObject>
#include <QString>
#include <memory>

// Forward declarations for GUI components
namespace MegaCustom {
    class AuthController;
    class FileController;
    class TransferController;
}

// Forward declarations for CLI modules
namespace MegaCustom {
    class AuthenticationModule;
    class FileOperations;
    class TransferManager;
}

namespace MegaCustom {

/**
 * BackendBridge - Main integration layer between GUI and CLI modules
 *
 * This class acts as a bridge pattern implementation, connecting the
 * Qt6 GUI controllers to the existing CLI modules. It handles:
 * - Signal/callback adaptation
 * - Async operation management
 * - Error translation
 * - Progress reporting
 */
class BackendBridge : public QObject {
    Q_OBJECT

public:
    explicit BackendBridge(QObject* parent = nullptr);
    virtual ~BackendBridge();

    /**
     * Initialize the backend systems
     * Sets up Mega SDK and prepares CLI modules
     */
    bool initialize(const QString& configPath = QString());

    /**
     * Connect GUI controllers to CLI modules
     */
    void connectAuthentication(AuthController* guiController);
    void connectFileOperations(FileController* guiController);
    void connectTransfers(TransferController* guiController);

    /**
     * Get status information
     */
    bool isInitialized() const { return m_initialized; }
    bool isConnected() const { return m_connected; }
    QString currentUser() const { return m_currentUser; }

signals:
    /**
     * Backend status signals
     */
    void initializationComplete();
    void initializationFailed(const QString& error);
    void connectionStatusChanged(bool connected);
    void backendError(const QString& error);

public slots:
    /**
     * Handle shutdown and cleanup
     */
    void shutdown();

private:
    /**
     * Internal initialization methods
     */
    bool initializeMegaSDK();
    bool initializeCLIModules();
    void setupSignalConnections();

private:
    // Backend components (owned by bridge)
    // Note: MegaManager is a singleton, so we don't own it
    std::unique_ptr<MegaCustom::AuthenticationModule> m_authModule;
    std::unique_ptr<MegaCustom::FileOperations> m_fileOps;
    std::unique_ptr<MegaCustom::TransferManager> m_transferMgr;

    // GUI components (not owned, just referenced)
    AuthController* m_guiAuth = nullptr;
    FileController* m_guiFile = nullptr;
    TransferController* m_guiTransfer = nullptr;

    // State
    bool m_initialized = false;
    bool m_connected = false;
    QString m_currentUser;
    QString m_configPath;
};

} // namespace MegaCustom

#endif // BACKEND_BRIDGE_H