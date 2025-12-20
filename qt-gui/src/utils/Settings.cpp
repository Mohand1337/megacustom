#include "Settings.h"
#include "Constants.h"
#include <QDebug>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QCoreApplication>
#include <QFile>

namespace MegaCustom {

namespace {
    bool ensureDirectoryExists(const QString& path) {
        QDir dir;
        if (!dir.mkpath(path)) {
            qWarning() << "Failed to create directory:" << path;
            return false;
        }
        return true;
    }

    // Check if running in portable mode
    // Portable mode is enabled if:
    // 1. A "portable.marker" file exists next to the executable, OR
    // 2. A "settings.ini" file already exists next to the executable
    bool isPortableMode() {
        QString exeDir = QCoreApplication::applicationDirPath();
        return QFile::exists(exeDir + "/portable.marker") ||
               QFile::exists(exeDir + "/settings.ini");
    }

    // Get the configuration directory path
    // In portable mode: same directory as the executable
    // In standard mode: platform-specific config location (AppData on Windows, ~/.config on Linux)
    QString getConfigPath() {
        static QString cachedPath;
        static bool pathDetermined = false;

        if (!pathDetermined) {
            if (isPortableMode()) {
                cachedPath = QCoreApplication::applicationDirPath();
                qDebug() << "Running in PORTABLE mode - config at:" << cachedPath;
            } else {
                cachedPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/MegaCustom";
                qDebug() << "Running in STANDARD mode - config at:" << cachedPath;
            }
            pathDetermined = true;
        }
        return cachedPath;
    }
} // anonymous namespace

Settings& Settings::instance() {
    static Settings instance;
    return instance;
}

bool Settings::isPortable() const {
    return isPortableMode();
}

QString Settings::configDirectory() const {
    return getConfigPath();
}

void Settings::load() {
    QString configPath = getConfigPath();
    if (!ensureDirectoryExists(configPath)) {
        qWarning() << "Settings::load() - could not create config directory, using defaults";
    }

    QSettings settings(configPath + "/settings.ini", QSettings::IniFormat);

    // Authentication
    m_rememberLogin = settings.value("auth/rememberLogin", false).toBool();
    m_lastEmail = settings.value("auth/lastEmail", "").toString();
    m_apiKey = settings.value("auth/apiKey", "").toString();

    // Paths
    m_lastLocalPath = settings.value("paths/lastLocal", QDir::homePath()).toString();
    m_lastRemotePath = settings.value("paths/lastRemote", "/").toString();

    // General
    m_darkMode = settings.value("ui/darkMode", false).toBool();
    m_showHidden = settings.value("ui/showHidden", false).toBool();
    m_showTrayIcon = settings.value("ui/showTrayIcon", true).toBool();
    m_showNotifications = settings.value("ui/showNotifications", true).toBool();
    m_windowGeometry = settings.value("ui/windowGeometry").toByteArray();
    m_windowState = settings.value("ui/windowState").toByteArray();

    // Sync
    m_syncInterval = settings.value("sync/interval", 0).toInt();
    m_syncOnStartup = settings.value("sync/onStartup", false).toBool();

    // Advanced
    m_uploadBandwidthLimit = settings.value("advanced/uploadLimit", 0).toInt();
    m_downloadBandwidthLimit = settings.value("advanced/downloadLimit", 0).toInt();
    m_parallelTransfers = settings.value("advanced/parallelTransfers", 4).toInt();
    m_excludePatterns = settings.value("advanced/excludePatterns", "*.tmp, *.bak, .git").toString();
    m_skipHiddenFiles = settings.value("advanced/skipHidden", false).toBool();
    m_cachePath = settings.value("advanced/cachePath", configPath + "/cache").toString();
    m_loggingEnabled = settings.value("advanced/logging", true).toBool();

    qDebug() << "Settings loaded from" << settings.fileName();
}

void Settings::save() {
    QString configPath = getConfigPath();
    if (!ensureDirectoryExists(configPath)) {
        qWarning() << "Settings::save() - could not create config directory, settings may not persist";
        return;
    }

    QSettings settings(configPath + "/settings.ini", QSettings::IniFormat);

    // Authentication
    settings.setValue("auth/rememberLogin", m_rememberLogin);
    settings.setValue("auth/lastEmail", m_lastEmail);
    settings.setValue("auth/apiKey", m_apiKey);

    // Paths
    settings.setValue("paths/lastLocal", m_lastLocalPath);
    settings.setValue("paths/lastRemote", m_lastRemotePath);

    // General
    settings.setValue("ui/darkMode", m_darkMode);
    settings.setValue("ui/showHidden", m_showHidden);
    settings.setValue("ui/showTrayIcon", m_showTrayIcon);
    settings.setValue("ui/showNotifications", m_showNotifications);
    settings.setValue("ui/windowGeometry", m_windowGeometry);
    settings.setValue("ui/windowState", m_windowState);

    // Sync
    settings.setValue("sync/interval", m_syncInterval);
    settings.setValue("sync/onStartup", m_syncOnStartup);

    // Advanced
    settings.setValue("advanced/uploadLimit", m_uploadBandwidthLimit);
    settings.setValue("advanced/downloadLimit", m_downloadBandwidthLimit);
    settings.setValue("advanced/parallelTransfers", m_parallelTransfers);
    settings.setValue("advanced/excludePatterns", m_excludePatterns);
    settings.setValue("advanced/skipHidden", m_skipHiddenFiles);
    settings.setValue("advanced/cachePath", m_cachePath);
    settings.setValue("advanced/logging", m_loggingEnabled);

    settings.sync();
    qDebug() << "Settings saved to" << settings.fileName();
}

void Settings::loadFromFile(const QString& file) {
    QSettings settings(file, QSettings::IniFormat);
    // Load from custom file - implementation similar to load()
    qDebug() << "Settings::loadFromFile() from" << file;
}

// Authentication
bool Settings::autoLogin() const { return m_rememberLogin; }
bool Settings::rememberLogin() const { return m_rememberLogin; }
void Settings::setRememberLogin(bool remember) { m_rememberLogin = remember; }
QString Settings::apiKey() const { return m_apiKey; }
QString Settings::sessionFile() const {
    // Return full path to session file in config directory
    return getConfigPath() + "/session.dat";
}
QString Settings::lastEmail() const { return m_lastEmail; }
void Settings::setLastEmail(const QString& email) { m_lastEmail = email; }

// Paths
QString Settings::lastLocalPath() const { return m_lastLocalPath; }
QString Settings::lastRemotePath() const { return m_lastRemotePath; }
void Settings::setLastLocalPath(const QString& path) { m_lastLocalPath = path; }
void Settings::setLastRemotePath(const QString& path) { m_lastRemotePath = path; }

// General
bool Settings::darkMode() const { return m_darkMode; }
void Settings::setDarkMode(bool enabled) { m_darkMode = enabled; }
bool Settings::showHiddenFiles() const { return m_showHidden; }
void Settings::setShowHiddenFiles(bool show) { m_showHidden = show; }
bool Settings::showTrayIcon() const { return m_showTrayIcon; }
void Settings::setShowTrayIcon(bool show) { m_showTrayIcon = show; }
bool Settings::showNotifications() const { return m_showNotifications; }
void Settings::setShowNotifications(bool show) { m_showNotifications = show; }

// Sync
int Settings::syncInterval() const { return m_syncInterval; }
void Settings::setSyncInterval(int minutes) { m_syncInterval = minutes; }
bool Settings::syncOnStartup() const { return m_syncOnStartup; }
void Settings::setSyncOnStartup(bool enabled) { m_syncOnStartup = enabled; }

// Advanced
int Settings::uploadBandwidthLimit() const { return m_uploadBandwidthLimit; }
void Settings::setUploadBandwidthLimit(int kbps) { m_uploadBandwidthLimit = kbps; }
int Settings::downloadBandwidthLimit() const { return m_downloadBandwidthLimit; }
void Settings::setDownloadBandwidthLimit(int kbps) { m_downloadBandwidthLimit = kbps; }
int Settings::parallelTransfers() const { return m_parallelTransfers; }
void Settings::setParallelTransfers(int count) { m_parallelTransfers = count; }
QString Settings::excludePatterns() const { return m_excludePatterns; }
void Settings::setExcludePatterns(const QString& patterns) { m_excludePatterns = patterns; }
bool Settings::skipHiddenFiles() const { return m_skipHiddenFiles; }
void Settings::setSkipHiddenFiles(bool skip) { m_skipHiddenFiles = skip; }
QString Settings::cachePath() const { return m_cachePath; }
void Settings::setCachePath(const QString& path) { m_cachePath = path; }
bool Settings::loggingEnabled() const { return m_loggingEnabled; }
void Settings::setLoggingEnabled(bool enabled) { m_loggingEnabled = enabled; }

// Window
QByteArray Settings::windowGeometry() const { return m_windowGeometry; }
QByteArray Settings::windowState() const { return m_windowState; }
void Settings::setWindowGeometry(const QByteArray& geometry) { m_windowGeometry = geometry; }
void Settings::setWindowState(const QByteArray& state) { m_windowState = state; }

} // namespace MegaCustom
