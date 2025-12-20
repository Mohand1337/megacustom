#ifndef MEGACUSTOM_APPLICATION_H
#define MEGACUSTOM_APPLICATION_H

#include <QApplication>
#include <QObject>
#include <memory>
#include <QCommandLineParser>
#include <QSystemTrayIcon>

// Forward declarations

namespace mega {
    class MegaApi;
}

namespace MegaCustom {

// Forward declarations
class MainWindow;
class AuthController;
class FileController;
class TransferController;
class FolderMapperController;
class MultiUploaderController;
class SmartSyncController;
class CloudCopierController;
class DistributionController;
class WatermarkerController;
class SyncScheduler;
class MegaManager;

/**
 * Main application class that manages the lifecycle of the GUI application
 */
class Application : public QApplication
{
    Q_OBJECT

public:
    /**
     * Constructor
     * @param argc Argument count from main()
     * @param argv Argument values from main()
     */
    explicit Application(int& argc, char** argv);

    /**
     * Destructor
     */
    virtual ~Application();

    /**
     * Parse command line arguments
     * @return true if arguments were parsed successfully
     */
    bool parseCommandLine();

    /**
     * Check if application should only handle command line and exit
     * @return true if no GUI should be shown
     */
    bool isCommandLineOnly() const { return m_commandLineOnly; }

    /**
     * Check if application should start minimized
     * @return true if should start minimized
     */
    bool isMinimizedStart() const { return m_startMinimized; }

    /**
     * Initialize the backend (Mega SDK and core modules)
     * @return true if initialization was successful
     */
    bool initializeBackend();

    /**
     * Create the main window
     * @return true if window was created successfully
     */
    bool createMainWindow();

    /**
     * Get the main window instance
     * @return Pointer to main window or nullptr
     */
    MainWindow* getMainWindow() const { return m_mainWindow.get(); }

    /**
     * Attempt automatic login using saved credentials
     */
    void attemptAutoLogin();

    /**
     * Check if user is currently logged in
     * @return true if logged in
     */
    bool isLoggedIn() const;

    /**
     * Get the Mega API instance
     * @return Pointer to Mega API
     */
    mega::MegaApi* getMegaApi() const;

    /**
     * Get the SyncScheduler instance
     * @return Pointer to SyncScheduler
     */
    SyncScheduler* getSyncScheduler() const { return m_syncScheduler.get(); }

signals:
    /**
     * Emitted when login status changes
     * @param loggedIn true if logged in, false if logged out
     */
    void loginStatusChanged(bool loggedIn);

    /**
     * Emitted when a critical error occurs
     * @param error Error message
     */
    void criticalError(const QString& error);

    /**
     * Emitted when application is about to quit
     */
    void aboutToQuit();

public slots:
    /**
     * Show the main window
     */
    void showMainWindow();

    /**
     * Hide main window to system tray
     */
    void hideToTray();

    /**
     * Toggle window visibility
     */
    void toggleWindowVisibility();

    /**
     * Handle login success
     * @param sessionKey Session key for future logins
     */
    void onLoginSuccess(const QString& sessionKey);

    /**
     * Handle logout
     */
    void onLogout();

    /**
     * Show about dialog
     */
    void showAboutDialog();

    /**
     * Show settings dialog
     */
    void showSettingsDialog();

    /**
     * Handle quit request
     */
    void handleQuitRequest();

    /**
     * Load stylesheet by theme (light or dark)
     * @param darkMode true to load dark theme, false for light theme
     * @return true if stylesheet was loaded successfully
     *
     * This is a static method so MainWindow can use it to switch themes
     * at runtime without needing access to the Application instance.
     */
    static bool loadStylesheetByTheme(bool darkMode);

protected:
    /**
     * Handle application events
     * @param receiver Event receiver
     * @param event Event to process
     * @return true if event was handled
     */
    bool notify(QObject* receiver, QEvent* event) override;

private slots:
    /**
     * Handle system tray activation
     * @param reason Activation reason
     */
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);

    /**
     * Clean up before quitting
     */
    void cleanup();

private:
    /**
     * Initialize system tray icon
     */
    void initializeTrayIcon();

    /**
     * Create tray context menu
     */
    void createTrayMenu();

    /**
     * Save current session
     */
    void saveSession();

    /**
     * Restore previous session
     */
    void restoreSession();

    /**
     * Show error message
     * @param title Error title
     * @param message Error message
     */
    void showError(const QString& title, const QString& message);

    /**
     * Load the MEGA-style stylesheet
     */
    void loadStylesheet();

private:
    // Main components
    std::unique_ptr<MainWindow> m_mainWindow;
    // MegaManager is a singleton, we don't own it

    // Controllers
    std::unique_ptr<AuthController> m_authController;
    std::unique_ptr<FileController> m_fileController;
    std::unique_ptr<TransferController> m_transferController;
    std::unique_ptr<FolderMapperController> m_folderMapperController;
    std::unique_ptr<MultiUploaderController> m_multiUploaderController;
    std::unique_ptr<SmartSyncController> m_smartSyncController;
    std::unique_ptr<CloudCopierController> m_cloudCopierController;
    std::unique_ptr<DistributionController> m_distributionController;
    std::unique_ptr<WatermarkerController> m_watermarkerController;
    std::unique_ptr<SyncScheduler> m_syncScheduler;

    // System tray
    std::unique_ptr<QSystemTrayIcon> m_trayIcon;
    QMenu* m_trayMenu;

    // Command line parser
    QCommandLineParser m_parser;

    // State flags
    bool m_commandLineOnly;
    bool m_startMinimized;
    bool m_isLoggedIn;
    bool m_backendInitialized;

    // Session management
    QString m_sessionKey;
    QString m_currentUser;

    // Prevent copy
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_APPLICATION_H