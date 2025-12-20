#ifndef SETTINGS_H
#define SETTINGS_H

#include <QString>
#include <QByteArray>

namespace MegaCustom {

/**
 * @brief Application settings manager (singleton)
 *
 * Provides centralized access to all application settings including
 * authentication preferences, UI state, sync configuration, and
 * advanced transfer options. Settings are persisted to QSettings
 * (platform-specific storage) and can be imported from JSON files.
 *
 * Supports portable mode: if a "portable.marker" file exists next to
 * the executable, settings are stored in the app directory instead of
 * the platform-specific location (AppData on Windows, ~/.config on Linux).
 *
 * @note Thread-safe for read operations. Write operations should be
 *       performed from the main thread.
 *
 * Example usage:
 * @code
 * Settings& settings = Settings::instance();
 * settings.setDarkMode(true);
 * settings.save();
 * @endcode
 */
class Settings {
public:
    /**
     * @brief Get the singleton instance
     * @return Reference to the global Settings instance
     */
    static Settings& instance();

    /**
     * @brief Load settings from persistent storage (QSettings)
     */
    void load();

    /**
     * @brief Save current settings to persistent storage
     */
    void save();

    /**
     * @brief Import settings from a JSON file
     * @param file Path to the JSON configuration file
     */
    void loadFromFile(const QString& file);

    // -------------------------------------------------------------------------
    // Portable Mode Support
    // -------------------------------------------------------------------------

    /**
     * @brief Check if running in portable mode
     * @return true if portable.marker exists next to the executable
     *
     * In portable mode, all settings and data are stored in the application
     * directory, making the app self-contained and movable.
     */
    bool isPortable() const;

    /**
     * @brief Get the configuration directory path
     * @return Path where settings and data are stored
     *
     * In portable mode: same directory as the executable
     * In standard mode: platform-specific config location
     */
    QString configDirectory() const;

    // -------------------------------------------------------------------------
    // Authentication Settings
    // -------------------------------------------------------------------------

    /**
     * @brief Check if auto-login is enabled
     * @return true if the app should attempt automatic login on startup
     */
    bool autoLogin() const;

    /**
     * @brief Check if login credentials should be remembered
     * @return true if credentials are persisted between sessions
     */
    bool rememberLogin() const;

    /**
     * @brief Set whether to remember login credentials
     * @param remember true to persist credentials
     */
    void setRememberLogin(bool remember);

    /**
     * @brief Get the MEGA API key
     * @return API key string (from Constants or override)
     */
    QString apiKey() const;

    /**
     * @brief Get the path to the session token file
     * @return Absolute path to the encrypted session file
     */
    QString sessionFile() const;

    /**
     * @brief Get the last used email address
     * @return Email from the most recent login
     */
    QString lastEmail() const;

    /**
     * @brief Set the last used email address
     * @param email Email to remember for next login
     */
    void setLastEmail(const QString& email);

    // -------------------------------------------------------------------------
    // Path Settings
    // -------------------------------------------------------------------------

    /**
     * @brief Get the last browsed local filesystem path
     * @return Local directory path (defaults to home directory)
     */
    QString lastLocalPath() const;

    /**
     * @brief Get the last browsed remote (cloud) path
     * @return Remote path in MEGA (defaults to root "/")
     */
    QString lastRemotePath() const;

    /**
     * @brief Set the last browsed local path
     * @param path Local directory path to remember
     */
    void setLastLocalPath(const QString& path);

    /**
     * @brief Set the last browsed remote path
     * @param path Remote MEGA path to remember
     */
    void setLastRemotePath(const QString& path);

    // -------------------------------------------------------------------------
    // General UI Settings
    // -------------------------------------------------------------------------

    /**
     * @brief Check if dark mode is enabled
     * @return true if dark theme is active
     */
    bool darkMode() const;

    /**
     * @brief Enable or disable dark mode
     * @param enabled true to enable dark theme
     */
    void setDarkMode(bool enabled);

    /**
     * @brief Check if hidden files should be displayed
     * @return true if hidden files are visible in file browsers
     */
    bool showHiddenFiles() const;

    /**
     * @brief Set hidden files visibility
     * @param show true to show hidden files (dotfiles)
     */
    void setShowHiddenFiles(bool show);

    /**
     * @brief Check if system tray icon is enabled
     * @return true if tray icon should be shown
     */
    bool showTrayIcon() const;

    /**
     * @brief Enable or disable system tray icon
     * @param show true to show tray icon
     */
    void setShowTrayIcon(bool show);

    /**
     * @brief Check if desktop notifications are enabled
     * @return true if notifications should be displayed
     */
    bool showNotifications() const;

    /**
     * @brief Enable or disable desktop notifications
     * @param show true to enable notifications
     */
    void setShowNotifications(bool show);

    // -------------------------------------------------------------------------
    // Sync Settings
    // -------------------------------------------------------------------------

    /**
     * @brief Get the automatic sync interval
     * @return Interval in minutes (0 = disabled)
     */
    int syncInterval() const;

    /**
     * @brief Set the automatic sync interval
     * @param minutes Sync interval (0 to disable, 1-1440 for active sync)
     */
    void setSyncInterval(int minutes);

    /**
     * @brief Check if sync should run on application startup
     * @return true if initial sync is enabled
     */
    bool syncOnStartup() const;

    /**
     * @brief Enable or disable sync on startup
     * @param enabled true to sync when application starts
     */
    void setSyncOnStartup(bool enabled);

    // -------------------------------------------------------------------------
    // Advanced Transfer Settings
    // -------------------------------------------------------------------------

    /**
     * @brief Get the upload bandwidth limit
     * @return Limit in KB/s (0 = unlimited)
     */
    int uploadBandwidthLimit() const;

    /**
     * @brief Set the upload bandwidth limit
     * @param kbps Limit in KB/s (0 for unlimited)
     */
    void setUploadBandwidthLimit(int kbps);

    /**
     * @brief Get the download bandwidth limit
     * @return Limit in KB/s (0 = unlimited)
     */
    int downloadBandwidthLimit() const;

    /**
     * @brief Set the download bandwidth limit
     * @param kbps Limit in KB/s (0 for unlimited)
     */
    void setDownloadBandwidthLimit(int kbps);

    /**
     * @brief Get the number of parallel transfers allowed
     * @return Number of concurrent transfers (1-10)
     */
    int parallelTransfers() const;

    /**
     * @brief Set the number of parallel transfers
     * @param count Number of concurrent transfers (clamped to 1-10)
     */
    void setParallelTransfers(int count);

    /**
     * @brief Get file exclusion patterns
     * @return Comma-separated glob patterns (e.g., "*.tmp, *.bak, .git")
     */
    QString excludePatterns() const;

    /**
     * @brief Set file exclusion patterns
     * @param patterns Comma-separated glob patterns to exclude from transfers
     */
    void setExcludePatterns(const QString& patterns);

    /**
     * @brief Check if hidden files should be skipped during transfers
     * @return true if dotfiles are excluded from sync/transfer
     */
    bool skipHiddenFiles() const;

    /**
     * @brief Set whether to skip hidden files during transfers
     * @param skip true to exclude dotfiles
     */
    void setSkipHiddenFiles(bool skip);

    /**
     * @brief Get the local cache directory path
     * @return Path to cache directory (empty = default location)
     */
    QString cachePath() const;

    /**
     * @brief Set the local cache directory path
     * @param path Custom cache directory (empty for default)
     */
    void setCachePath(const QString& path);

    /**
     * @brief Check if debug logging is enabled
     * @return true if logging to file is active
     */
    bool loggingEnabled() const;

    /**
     * @brief Enable or disable debug logging
     * @param enabled true to write logs to file
     */
    void setLoggingEnabled(bool enabled);

    // -------------------------------------------------------------------------
    // Window State Persistence
    // -------------------------------------------------------------------------

    /**
     * @brief Get saved window geometry
     * @return Serialized geometry data for QWidget::restoreGeometry()
     */
    QByteArray windowGeometry() const;

    /**
     * @brief Get saved window state (toolbars, docks)
     * @return Serialized state data for QMainWindow::restoreState()
     */
    QByteArray windowState() const;

    /**
     * @brief Save window geometry
     * @param geometry Data from QWidget::saveGeometry()
     */
    void setWindowGeometry(const QByteArray& geometry);

    /**
     * @brief Save window state
     * @param state Data from QMainWindow::saveState()
     */
    void setWindowState(const QByteArray& state);

private:
    Settings() = default;
    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;

    // Authentication
    bool m_rememberLogin = false;
    QString m_lastEmail;
    QString m_apiKey;

    // Paths
    QString m_lastLocalPath = "/home";
    QString m_lastRemotePath = "/";

    // General
    bool m_darkMode = false;
    bool m_showHidden = false;
    bool m_showTrayIcon = true;
    bool m_showNotifications = true;

    // Sync
    int m_syncInterval = 0;  // 0 = disabled
    bool m_syncOnStartup = false;

    // Advanced
    int m_uploadBandwidthLimit = 0;  // 0 = unlimited
    int m_downloadBandwidthLimit = 0;
    int m_parallelTransfers = 4;
    QString m_excludePatterns = "*.tmp, *.bak, .git";
    bool m_skipHiddenFiles = false;
    QString m_cachePath;
    bool m_loggingEnabled = true;

    // Window
    QByteArray m_windowGeometry;
    QByteArray m_windowState;
};

} // namespace MegaCustom

#endif // SETTINGS_H
