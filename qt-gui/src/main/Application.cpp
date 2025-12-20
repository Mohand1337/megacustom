#include "Application.h"
#include "MainWindow.h"
#include "controllers/AuthController.h"
#include "controllers/FileController.h"
#include "controllers/TransferController.h"
#include "controllers/FolderMapperController.h"
#include "controllers/MultiUploaderController.h"
#include "controllers/SmartSyncController.h"
#include "controllers/CloudCopierController.h"
#include "controllers/DistributionController.h"
#include "controllers/WatermarkerController.h"
#include "scheduler/SyncScheduler.h"
#include "utils/Settings.h"
#include "utils/Constants.h"
#include "dialogs/AboutDialog.h"
// SettingsDialog removed - using SettingsPanel in MainWindow instead

// Backend includes - real CLI modules
#include "core/MegaManager.h"
#include "accounts/AccountManager.h"

#include <QSystemTrayIcon>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QDebug>
#include <QStyleFactory>
#include <QPalette>
#include <QColor>
#include "styles/MegaProxyStyle.h"
#include "styles/ThemeManager.h"
#include "styles/StyleSheetGenerator.h"

namespace MegaCustom {

Application::Application(int& argc, char** argv)
    : QApplication(argc, argv)
    , m_trayMenu(nullptr)
    , m_commandLineOnly(false)
    , m_startMinimized(false)
    , m_isLoggedIn(false)
    , m_backendInitialized(false)
{
    // Use custom MegaProxyStyle to ensure MEGA brand colors for menu selection highlights
    // This wraps Fusion style and overrides drawControl/drawPrimitive at the C++ level
    // because Qt6's Fusion style has a built-in palette that overrides QSS for menus
    setStyle(new MegaProxyStyle());

    // Also set the palette from the style for consistency
    setPalette(style()->standardPalette());

    // Set up command line parser
    m_parser.setApplicationDescription("MegaCustom - Advanced Mega.nz Desktop Client");
    m_parser.addHelpOption();
    m_parser.addVersionOption();

    QCommandLineOption minimizedOption(
        QStringList() << "m" << "minimized",
        "Start minimized to system tray");
    m_parser.addOption(minimizedOption);

    QCommandLineOption configOption(
        QStringList() << "c" << "config",
        "Specify configuration file",
        "file");
    m_parser.addOption(configOption);

    // Connect application signals
    connect(this, &QApplication::aboutToQuit,
            this, &Application::cleanup);
}

Application::~Application()
{
    cleanup();
}

bool Application::parseCommandLine()
{
    m_parser.process(*this);

    // Check for command line only options
    if (m_parser.isSet("version")) {
        m_commandLineOnly = true;
        return true;
    }

    if (m_parser.isSet("help")) {
        m_commandLineOnly = true;
        return true;
    }

    // Check for GUI options
    m_startMinimized = m_parser.isSet("minimized");

    // Load config file if specified
    if (m_parser.isSet("config")) {
        QString configFile = m_parser.value("config");
        Settings::instance().loadFromFile(configFile);
    }

    return true;
}

bool Application::initializeBackend()
{
    try {
        // Get the MegaManager singleton
        MegaCustom::MegaManager& megaManager = MegaCustom::MegaManager::getInstance();

        // Get API key from settings, environment, or use built-in default
        QString apiKey = Settings::instance().apiKey();
        if (apiKey.isEmpty()) {
            // Try both environment variable names for compatibility
            const char* envKey = std::getenv("MEGA_APP_KEY");
            if (!envKey) {
                envKey = std::getenv("MEGA_API_KEY");
            }
            if (envKey) {
                apiKey = QString::fromUtf8(envKey);
            }
        }

        // Use built-in API key as final fallback for distributable app
        if (apiKey.isEmpty()) {
            apiKey = QString::fromUtf8(Constants::MEGA_API_KEY);
            qDebug() << "Using built-in MEGA API key";
        }

        // Get cache path for SDK - use standard app data location
        QString projectCachePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                   + "/mega_cache";
        QDir().mkpath(projectCachePath);
        qDebug() << "Using SDK cache path:" << projectCachePath;

        // Initialize with API key and cache path
        if (!megaManager.initialize(apiKey.toStdString(), projectCachePath.toStdString())) {
            showError("Initialization Error",
                     "Failed to initialize Mega SDK.\n"
                     "Please check your API key and network connection.");
            return false;
        }

        // Create controllers
        // Note: FileController uses nullptr to dynamically get the active account's API
        // from AccountManager (for multi-account support). Other controllers that still
        // use MegaManager's API will need migration to AccountManager in the future.
        m_authController = std::make_unique<AuthController>(megaManager.getMegaApi());
        m_fileController = std::make_unique<FileController>(nullptr);  // Uses AccountManager's active API
        m_transferController = std::make_unique<TransferController>(megaManager.getMegaApi());
        m_folderMapperController = std::make_unique<FolderMapperController>(megaManager.getMegaApi());
        m_multiUploaderController = std::make_unique<MultiUploaderController>(megaManager.getMegaApi());
        m_smartSyncController = std::make_unique<SmartSyncController>(megaManager.getMegaApi());
        m_cloudCopierController = std::make_unique<CloudCopierController>(megaManager.getMegaApi());
        m_distributionController = std::make_unique<DistributionController>();
        m_watermarkerController = std::make_unique<WatermarkerController>();

        // Create scheduler and connect to controllers
        m_syncScheduler = std::make_unique<SyncScheduler>();
        m_syncScheduler->setFolderMapperController(m_folderMapperController.get());
        m_syncScheduler->setSmartSyncController(m_smartSyncController.get());
        m_syncScheduler->setMultiUploaderController(m_multiUploaderController.get());

        // Set scheduler interval from settings
        int syncInterval = Settings::instance().syncInterval();
        if (syncInterval > 0) {
            m_syncScheduler->setCheckInterval(syncInterval * 60);  // Convert minutes to seconds
        }

        // Connect auth controller signals
        connect(m_authController.get(), &AuthController::loginSuccess,
                this, &Application::onLoginSuccess);
        connect(m_authController.get(), &AuthController::logoutComplete,
                this, &Application::onLogout);

        // Initialize AccountManager for multi-account support
        AccountManager::initialize(this);

        m_backendInitialized = true;
        return true;

    } catch (const std::exception& e) {
        showError("Backend Error",
                 QString("Failed to initialize backend: %1").arg(e.what()));
        return false;
    }
}

bool Application::createMainWindow()
{
    try {
        // Load MEGA-style stylesheet
        loadStylesheet();

        // Create main window
        m_mainWindow = std::make_unique<MainWindow>();

        // Set controllers
        m_mainWindow->setAuthController(m_authController.get());
        m_mainWindow->setFileController(m_fileController.get());
        m_mainWindow->setTransferController(m_transferController.get());
        m_mainWindow->setFolderMapperController(m_folderMapperController.get());
        m_mainWindow->setMultiUploaderController(m_multiUploaderController.get());
        m_mainWindow->setSmartSyncController(m_smartSyncController.get());
        m_mainWindow->setCloudCopierController(m_cloudCopierController.get());
        m_mainWindow->setDistributionController(m_distributionController.get());
        m_mainWindow->setWatermarkerController(m_watermarkerController.get());

        // Initialize system tray
        if (QSystemTrayIcon::isSystemTrayAvailable()) {
            initializeTrayIcon();
        }

        // Show window unless starting minimized
        if (!m_startMinimized) {
            m_mainWindow->show();
        } else if (m_trayIcon) {
            m_trayIcon->showMessage("MegaCustom",
                                   "Application started in system tray",
                                   QSystemTrayIcon::Information,
                                   3000);
        }

        return true;

    } catch (const std::exception& e) {
        showError("UI Error",
                 QString("Failed to create user interface: %1").arg(e.what()));
        return false;
    }
}

void Application::attemptAutoLogin()
{
    if (!m_authController) return;

    // Try to restore session
    QString sessionFile = Settings::instance().sessionFile();
    if (QFile::exists(sessionFile)) {
        m_authController->restoreSession(sessionFile);
    }
}

bool Application::isLoggedIn() const
{
    return m_isLoggedIn;
}

mega::MegaApi* Application::getMegaApi() const
{
    MegaCustom::MegaManager& megaManager = MegaCustom::MegaManager::getInstance();
    return megaManager.getMegaApi();
}

void Application::showMainWindow()
{
    if (m_mainWindow) {
        m_mainWindow->show();
        m_mainWindow->raise();
        m_mainWindow->activateWindow();
    }
}

void Application::hideToTray()
{
    if (m_mainWindow && m_trayIcon) {
        m_mainWindow->hide();
        m_trayIcon->showMessage("MegaCustom",
                               "Application minimized to system tray",
                               QSystemTrayIcon::Information,
                               2000);
    }
}

void Application::toggleWindowVisibility()
{
    if (m_mainWindow) {
        if (m_mainWindow->isVisible()) {
            hideToTray();
        } else {
            showMainWindow();
        }
    }
}

void Application::onLoginSuccess(const QString& userEmail)
{
    m_isLoggedIn = true;
    m_currentUser = userEmail;

    // Save session for auto-login if "remember me" was checked
    if (Settings::instance().rememberLogin()) {
        qDebug() << "Saving session for:" << userEmail;
        // Save session securely using CredentialStore
        m_authController->saveSession(userEmail);
    }

    // Register this account with AccountManager if not already present
    AccountManager& accountMgr = AccountManager::instance();
    MegaAccount existingAccount = accountMgr.getAccountByEmail(userEmail);
    if (existingAccount.id.isEmpty()) {
        // Create a new account entry for this login
        accountMgr.registerExistingSession(userEmail, getMegaApi());
    } else {
        // Account exists - update its session token and switch to it
        accountMgr.updateAccountSession(existingAccount.id, getMegaApi());
        accountMgr.switchToAccount(existingAccount.id);
    }

    emit loginStatusChanged(true);

    // Update UI
    if (m_mainWindow) {
        m_mainWindow->onLoginStatusChanged(true);
    }

    // Start scheduler if sync on startup is enabled
    if (m_syncScheduler && Settings::instance().syncOnStartup()) {
        m_syncScheduler->start();
        qDebug() << "SyncScheduler started after login";
    }

    // Update tray menu
    createTrayMenu();
}

void Application::onLogout()
{
    m_isLoggedIn = false;
    m_sessionKey.clear();

    // Stop scheduler
    if (m_syncScheduler) {
        m_syncScheduler->stop();
        qDebug() << "SyncScheduler stopped on logout";
    }

    // Clear saved session
    QString sessionFile = Settings::instance().sessionFile();
    if (QFile::exists(sessionFile)) {
        QFile::remove(sessionFile);
    }

    emit loginStatusChanged(false);

    // Update UI
    if (m_mainWindow) {
        m_mainWindow->onLoginStatusChanged(false);
    }

    // Update tray menu
    createTrayMenu();
}

void Application::showAboutDialog()
{
    AboutDialog dialog(m_mainWindow.get());
    dialog.exec();
}

void Application::showSettingsDialog()
{
    // Show main window and switch to settings panel
    if (m_mainWindow) {
        m_mainWindow->show();
        m_mainWindow->raise();
        m_mainWindow->activateWindow();
        m_mainWindow->onSettings();  // Switch to settings panel
    }
}

void Application::handleQuitRequest()
{
    // Confirm if transfers are active
    if (m_transferController && m_transferController->hasActiveTransfers()) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            m_mainWindow.get(),
            "Confirm Exit",
            "There are active transfers. Are you sure you want to quit?",
            QMessageBox::Yes | QMessageBox::No
        );

        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    // Save session before quitting
    saveSession();

    // Quit application
    quit();
}

bool Application::notify(QObject* receiver, QEvent* event)
{
    try {
        return QApplication::notify(receiver, event);
    } catch (const std::exception& e) {
        qCritical() << "Exception in event handler:" << e.what();
        showError("Application Error",
                 QString("An error occurred: %1").arg(e.what()));
        return false;
    }
}

void Application::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    switch (reason) {
        case QSystemTrayIcon::DoubleClick:
            toggleWindowVisibility();
            break;
        case QSystemTrayIcon::MiddleClick:
            showMainWindow();
            break;
        default:
            break;
    }
}

void Application::cleanup()
{
    // Save current state
    saveSession();

    // Stop all transfers
    if (m_transferController) {
        m_transferController->cancelAllTransfers();
    }

    // Logout if needed
    if (m_isLoggedIn && m_authController) {
        m_authController->logout();
    }

    // Clean up main window
    if (m_mainWindow) {
        m_mainWindow->close();
        m_mainWindow.reset();
    }

    // Clean up tray icon
    if (m_trayIcon) {
        m_trayIcon->hide();
        m_trayIcon.reset();
    }

    // Clean up scheduler
    if (m_syncScheduler) {
        m_syncScheduler->stop();
        m_syncScheduler.reset();
    }

    // Clean up controllers
    m_authController.reset();
    m_fileController.reset();
    m_transferController.reset();
    m_folderMapperController.reset();
    m_multiUploaderController.reset();
    m_smartSyncController.reset();
    m_cloudCopierController.reset();

    // Shutdown AccountManager
    AccountManager::shutdown();

    // MegaManager is a singleton, we don't reset it
}

void Application::initializeTrayIcon()
{
    m_trayIcon = std::make_unique<QSystemTrayIcon>(this);
    m_trayIcon->setIcon(QIcon(":/icons/tray_icon.png"));
    m_trayIcon->setToolTip("MegaCustom");

    // Create tray menu
    createTrayMenu();

    // Connect signals
    connect(m_trayIcon.get(), &QSystemTrayIcon::activated,
            this, &Application::onTrayActivated);

    // Show tray icon
    m_trayIcon->show();
}

void Application::createTrayMenu()
{
    if (!m_trayIcon) return;

    // Delete old menu if exists
    if (m_trayMenu) {
        delete m_trayMenu;
    }

    // Create new menu
    m_trayMenu = new QMenu();

    // Add actions based on login status
    if (m_isLoggedIn) {
        m_trayMenu->addAction("Open MegaCustom", this, &Application::showMainWindow);
        m_trayMenu->addSeparator();
        m_trayMenu->addAction("Upload Files...", m_mainWindow.get(), &MainWindow::showUploadDialog);
        m_trayMenu->addAction("View Transfers", m_mainWindow.get(), &MainWindow::showTransfers);
        m_trayMenu->addSeparator();
        m_trayMenu->addAction("Logout", m_authController.get(), &AuthController::logout);
    } else {
        m_trayMenu->addAction("Open MegaCustom", this, &Application::showMainWindow);
        m_trayMenu->addAction("Login...", m_mainWindow.get(), &MainWindow::showLoginDialog);
    }

    m_trayMenu->addSeparator();
    m_trayMenu->addAction("Settings...", this, &Application::showSettingsDialog);
    m_trayMenu->addAction("About...", this, &Application::showAboutDialog);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction("Quit", this, &Application::handleQuitRequest);

    // Set menu to tray icon
    m_trayIcon->setContextMenu(m_trayMenu);
}

void Application::saveSession()
{
    Settings& settings = Settings::instance();

    // Save window geometry
    if (m_mainWindow) {
        settings.setWindowGeometry(m_mainWindow->saveGeometry());
        settings.setWindowState(m_mainWindow->saveState());
    }

    // Save current paths
    if (m_fileController) {
        settings.setLastLocalPath(m_fileController->currentLocalPath());
        settings.setLastRemotePath(m_fileController->currentRemotePath());
    }

    settings.save();
}

void Application::restoreSession()
{
    Settings& settings = Settings::instance();

    // Restore window geometry
    if (m_mainWindow) {
        QByteArray geometry = settings.windowGeometry();
        if (!geometry.isEmpty()) {
            m_mainWindow->restoreGeometry(geometry);
        }

        QByteArray state = settings.windowState();
        if (!state.isEmpty()) {
            m_mainWindow->restoreState(state);
        }
    }

    // Restore paths
    if (m_fileController) {
        QString localPath = settings.lastLocalPath();
        if (!localPath.isEmpty()) {
            m_fileController->navigateToLocal(localPath);
        }

        QString remotePath = settings.lastRemotePath();
        if (!remotePath.isEmpty()) {
            m_fileController->navigateToRemote(remotePath);
        }
    }
}

void Application::showError(const QString& title, const QString& message)
{
    qCritical() << title << ":" << message;

    if (m_mainWindow && m_mainWindow->isVisible()) {
        QMessageBox::critical(m_mainWindow.get(), title, message);
    } else {
        // If no main window, show standalone message box
        QMessageBox::critical(nullptr, title, message);
    }

    // Also show in system tray if available
    if (m_trayIcon) {
        m_trayIcon->showMessage(title, message, QSystemTrayIcon::Critical);
    }
}

void Application::loadStylesheet()
{
    // Load the light theme by default (can be overridden by Settings)
    loadStylesheetByTheme(false);
}

bool Application::loadStylesheetByTheme(bool darkMode)
{
    // Set theme in ThemeManager first
    ThemeManager::instance().setTheme(darkMode ? ThemeManager::Dark : ThemeManager::Light);

    QString themeName = darkMode ? "mega_dark.qss" : "mega_light.qss";

    // Try multiple locations for the stylesheet
    QStringList stylesheetPaths = {
        // Relative to build directory
        QCoreApplication::applicationDirPath() + "/../resources/styles/" + themeName,
        // Relative to project directory
        QCoreApplication::applicationDirPath() + "/../../resources/styles/" + themeName
    };

    QString stylesheet;
    bool loaded = false;

    for (const QString& path : stylesheetPaths) {
        QFile file(path);
        if (file.exists() && file.open(QFile::ReadOnly | QFile::Text)) {
            stylesheet = QString::fromUtf8(file.readAll());
            file.close();
            loaded = true;
            qDebug() << "Loaded stylesheet from:" << path;
            break;
        }
    }

    // Generate additional styles from DesignTokens
    QString generatedStyles = StyleSheetGenerator::generate();

    if (loaded && !stylesheet.isEmpty()) {
        // Combine: static QSS + generated styles
        QString combinedStylesheet = stylesheet + "\n\n/* Generated from DesignTokens */\n" + generatedStyles;
        qApp->setStyleSheet(combinedStylesheet);
        qDebug() << "MEGA" << (darkMode ? "dark" : "light") << "theme applied (with generated styles)";
        return true;
    } else {
        // Use only generated styles if QSS file not found
        qApp->setStyleSheet(generatedStyles);
        qDebug() << "Using generated stylesheet from DesignTokens (QSS file not found)";
        qWarning() << "Could not load" << themeName << "- tried paths:" << stylesheetPaths;
        return true;  // Still return true since we have generated styles
    }
}

} // namespace MegaCustom