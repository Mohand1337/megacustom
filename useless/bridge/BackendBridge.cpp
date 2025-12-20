#include "BackendBridge.h"
#include "controllers/AuthController.h"
#include "controllers/FileController.h"
#include "controllers/TransferController.h"
#include "utils/Constants.h"

// Include real CLI modules
#include "bridge/BackendModules.h"

// Additional includes for the real modules
#include <QDebug>
#include <QDir>
#include <QStandardPaths>

namespace MegaCustom {

BackendBridge::BackendBridge(QObject* parent)
    : QObject(parent) {
    qDebug() << "BackendBridge: Initializing backend integration layer";
}

BackendBridge::~BackendBridge() {
    qDebug() << "BackendBridge: Shutting down";
    shutdown();
}

bool BackendBridge::initialize(const QString& configPath) {
    qDebug() << "BackendBridge: Starting initialization";

    // Set config path or use default
    if (configPath.isEmpty()) {
        m_configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                      + "/MegaCustom";
    } else {
        m_configPath = configPath;
    }

    // Ensure config directory exists
    if (!QDir().mkpath(m_configPath)) {
        qWarning() << "BackendBridge: Failed to create config directory:" << m_configPath;
        emit initializationFailed("Failed to create config directory");
        return false;
    }
    qDebug() << "BackendBridge: Using config path:" << m_configPath;

    // Initialize Mega SDK
    if (!initializeMegaSDK()) {
        emit initializationFailed("Failed to initialize Mega SDK");
        return false;
    }

    // Initialize CLI modules
    if (!initializeCLIModules()) {
        emit initializationFailed("Failed to initialize CLI modules");
        return false;
    }

    // Set up signal connections
    setupSignalConnections();

    m_initialized = true;
    qDebug() << "BackendBridge: Initialization complete";
    emit initializationComplete();
    return true;
}

bool BackendBridge::initializeMegaSDK() {
    qDebug() << "BackendBridge: Initializing Mega SDK";

    // Use the singleton MegaManager instance
    MegaCustom::MegaManager& manager = MegaCustom::MegaManager::getInstance();

    // Get app key from environment or use built-in default
    std::string appKey;
    const char* envKey = std::getenv("MEGA_APP_KEY");
    if (!envKey) {
        envKey = std::getenv("MEGA_API_KEY");
    }
    if (envKey) {
        appKey = envKey;
    } else {
        // Use built-in API key for distributable app
        appKey = Constants::MEGA_API_KEY;
        qDebug() << "BackendBridge: Using built-in MEGA API key";
    }

    std::string basePath = m_configPath.toStdString() + "/mega_cache";
    bool result = manager.initialize(appKey, basePath);

    if (result) {
        qDebug() << "BackendBridge: Mega SDK initialized successfully";
    } else {
        qDebug() << "BackendBridge: Failed to initialize Mega SDK";
    }

    return result;
}

bool BackendBridge::initializeCLIModules() {
    qDebug() << "BackendBridge: Initializing CLI modules";

    // Get the MegaApi instance from the singleton
    MegaCustom::MegaManager& manager = MegaCustom::MegaManager::getInstance();
    mega::MegaApi* megaApi = manager.getMegaApi();

    if (!megaApi) {
        qDebug() << "BackendBridge: Cannot initialize modules - MegaApi not ready";
        return false;
    }

    // Create real CLI module instances
    m_authModule = std::make_unique<MegaCustom::AuthenticationModule>(megaApi);
    m_fileOps = std::make_unique<MegaCustom::FileOperations>(megaApi);
    m_transferMgr = std::make_unique<MegaCustom::TransferManager>(megaApi);

    qDebug() << "BackendBridge: All CLI modules initialized";
    return true;
}

void BackendBridge::setupSignalConnections() {
    qDebug() << "BackendBridge: Setting up signal connections";

    // TODO: Connect CLI module callbacks to Qt signals
    // This will involve creating adapter classes or lambda functions
    // to bridge the callback-based CLI to signal-based GUI
}

void BackendBridge::connectAuthentication(AuthController* guiController) {
    qDebug() << "BackendBridge: Connecting authentication controller";
    m_guiAuth = guiController;

    if (!m_guiAuth || !m_authModule) {
        qDebug() << "BackendBridge: Cannot connect auth - null components";
        return;
    }

    // Connect GUI signals to backend operations
    // TODO: Implement actual connections
    // connect(m_guiAuth, &AuthController::loginRequested,
    //         this, &BackendBridge::handleLogin);
}

void BackendBridge::connectFileOperations(FileController* guiController) {
    qDebug() << "BackendBridge: Connecting file operations controller";
    m_guiFile = guiController;

    if (!m_guiFile || !m_fileOps) {
        qDebug() << "BackendBridge: Cannot connect file ops - null components";
        return;
    }

    // Connect GUI signals to backend operations
    // TODO: Implement actual connections
}

void BackendBridge::connectTransfers(TransferController* guiController) {
    qDebug() << "BackendBridge: Connecting transfer controller";
    m_guiTransfer = guiController;

    if (!m_guiTransfer || !m_transferMgr) {
        qDebug() << "BackendBridge: Cannot connect transfers - null components";
        return;
    }

    // Connect GUI signals to backend operations
    // TODO: Implement actual connections
}

void BackendBridge::shutdown() {
    qDebug() << "BackendBridge: Performing shutdown";

    if (!m_initialized) {
        return;
    }

    // Disconnect from Mega
    if (m_connected) {
        // TODO: Logout from Mega
        m_connected = false;
        emit connectionStatusChanged(false);
    }

    // Clean up modules
    m_authModule.reset();
    m_fileOps.reset();
    m_transferMgr.reset();
    // Note: MegaManager is a singleton, we don't reset it

    m_initialized = false;
    qDebug() << "BackendBridge: Shutdown complete";
}

} // namespace MegaCustom

#include "moc_BackendBridge.cpp"