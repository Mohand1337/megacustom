/**
 * MegaCustom GUI Application
 * Main entry point for Qt6 Desktop Application
 */

#include <QApplication>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QDebug>
#include <QMessageBox>
#include <QSplashScreen>
#include <QPixmap>
#include <QTimer>
#include <QDateTime>

#include "main/Application.h"
#include "main/MainWindow.h"
#include "utils/Settings.h"
#include "core/LogManager.h"

#include <csignal>
#include <cstdlib>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

// Version information
#define APP_NAME "MegaCustom"
#define APP_VERSION "1.0.0"
#define APP_ORGANIZATION "MegaCustom"
#define APP_DOMAIN "megacustom.app"

// Global crash log path — set once at startup
static std::string g_crashLogPath;

/**
 * Write a crash/fatal message to the crash log file.
 * Uses raw file I/O only — safe to call from signal handlers.
 */
static void writeCrashLog(const char* reason)
{
    if (g_crashLogPath.empty()) return;

    std::ofstream f(g_crashLogPath, std::ios::app);
    if (!f.is_open()) return;

    // Get time
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    char timeBuf[64] = {};
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));

    f << "\n=== CRASH === " << timeBuf << " ===\n"
      << "Reason: " << reason << "\n"
      << "App: MegaCustom " APP_VERSION "\n"
#ifdef _WIN32
      << "Platform: Windows\n"
#else
      << "Platform: Linux\n"
#endif
      << "==========================================\n";
    f.flush();
}

/**
 * Signal handler for crashes (SIGSEGV, SIGABRT, etc.)
 */
static void crashSignalHandler(int sig)
{
    const char* name = "Unknown signal";
    switch (sig) {
        case SIGSEGV: name = "SIGSEGV (Segmentation fault)"; break;
        case SIGABRT: name = "SIGABRT (Abort)"; break;
        case SIGFPE:  name = "SIGFPE (Floating point exception)"; break;
        case SIGILL:  name = "SIGILL (Illegal instruction)"; break;
    }
    writeCrashLog(name);

    // Re-raise to get default behavior (core dump / exit)
    signal(sig, SIG_DFL);
    raise(sig);
}

#ifdef _WIN32
/**
 * Windows unhandled exception filter — catches access violations,
 * stack overflows, etc. that signal handlers miss.
 */
static LONG WINAPI windowsCrashHandler(EXCEPTION_POINTERS* exInfo)
{
    const char* desc = "Unknown exception";
    if (exInfo && exInfo->ExceptionRecord) {
        switch (exInfo->ExceptionRecord->ExceptionCode) {
            case EXCEPTION_ACCESS_VIOLATION:    desc = "Access Violation"; break;
            case EXCEPTION_STACK_OVERFLOW:       desc = "Stack Overflow"; break;
            case EXCEPTION_INT_DIVIDE_BY_ZERO:   desc = "Integer Divide by Zero"; break;
            case EXCEPTION_ILLEGAL_INSTRUCTION:  desc = "Illegal Instruction"; break;
            case EXCEPTION_IN_PAGE_ERROR:        desc = "In-Page Error"; break;
            default: desc = "Unhandled Win32 Exception"; break;
        }
    }
    writeCrashLog(desc);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

/**
 * Install crash handlers so crashes leave a trace in the log directory.
 */
static void installCrashHandler()
{
    // Use same log directory as LogManager
    g_crashLogPath = MegaCustom::LogManager::instance().getLogDirectory() + "/crash.log";

    // POSIX signal handlers
    signal(SIGSEGV, crashSignalHandler);
    signal(SIGABRT, crashSignalHandler);
    signal(SIGFPE,  crashSignalHandler);
    signal(SIGILL,  crashSignalHandler);

#ifdef _WIN32
    SetUnhandledExceptionFilter(windowsCrashHandler);
#endif
}

/**
 * Qt message handler — routes qDebug/qWarning/qCritical/qFatal
 * to LogManager so they appear in the activity and error logs.
 */
static void qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    MegaCustom::LogLevel level;
    std::string action;

    switch (type) {
        case QtDebugMsg:    level = MegaCustom::LogLevel::Debug;   action = "qt_debug";    break;
        case QtInfoMsg:     level = MegaCustom::LogLevel::Info;    action = "qt_info";     break;
        case QtWarningMsg:  level = MegaCustom::LogLevel::Warning; action = "qt_warning";  break;
        case QtCriticalMsg: level = MegaCustom::LogLevel::Error;   action = "qt_critical"; break;
        case QtFatalMsg:
            // Write crash log for fatal Qt errors, then abort
            writeCrashLog(msg.toUtf8().constData());
            abort();
    }

    // Build context string (file:line if available)
    std::string details;
    if (context.file) {
        details = std::string(context.file) + ":" + std::to_string(context.line);
    }

    MegaCustom::LogManager::instance().log(level, MegaCustom::LogCategory::System,
                                            action, msg.toStdString(), details);

    // Also echo to stderr so debug output appears in the terminal
    fprintf(stderr, "%s\n", msg.toUtf8().constData());
}

/**
 * Set application style from QSS file
 */
void setApplicationStyle(QApplication& app) {
    QFile styleFile(":/styles/default.qss");
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        QTextStream stream(&styleFile);
        app.setStyleSheet(stream.readAll());
        styleFile.close();
    }
}

/**
 * Show splash screen during initialization
 */
QSplashScreen* showSplashScreen() {
    QPixmap pixmap(":/icons/splash.png");
    if (pixmap.isNull()) {
        // Create a simple splash if image not found
        pixmap = QPixmap(600, 400);
        pixmap.fill(Qt::white);
    }

    auto* splash = new QSplashScreen(pixmap);
    splash->show();
    splash->showMessage("Initializing MegaCustom...",
                       Qt::AlignBottom | Qt::AlignHCenter,
                       Qt::black);

    return splash;
}

int main(int argc, char *argv[])
{
    // Enable high DPI support for Windows
    #ifdef Q_OS_WIN
        QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
        QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    #endif

    // Workaround for Wayland maximize/resize crash on Qt6
    // Force XCB (X11) backend if on Linux and Wayland is causing issues
    #ifdef Q_OS_LINUX
        // Set before QApplication is created
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
            qputenv("QT_QPA_PLATFORM", "xcb");
        }
    #endif

    // Create application instance
    MegaCustom::Application app(argc, argv);

    // Install crash handler & Qt message router — must be done early
    installCrashHandler();
    qInstallMessageHandler(qtMessageHandler);

    // Set application information
    QApplication::setOrganizationName(APP_ORGANIZATION);
    QApplication::setOrganizationDomain(APP_DOMAIN);
    QApplication::setApplicationName(APP_NAME);
    QApplication::setApplicationVersion(APP_VERSION);
    QApplication::setWindowIcon(QIcon(":/icons/app_icon.ico"));

    // Parse command line arguments
    if (app.parseCommandLine()) {
        // If handled by command line (e.g., --version, --help), exit
        if (app.isCommandLineOnly()) {
            return 0;
        }
    }

    // Show splash screen
    QSplashScreen* splash = nullptr;
    if (!app.isMinimizedStart()) {
        splash = showSplashScreen();
        QApplication::processEvents();
    }

    // Initialize application
    try {
        if (splash) {
            splash->showMessage("Loading settings...",
                               Qt::AlignBottom | Qt::AlignHCenter,
                               Qt::black);
            QApplication::processEvents();
        }

        // Load settings
        MegaCustom::Settings::instance().load();

        // Configure LogManager with the correct (portable-aware) log path
        {
            QString logDir = MegaCustom::Settings::instance().configDirectory() + "/logs";
            QDir().mkpath(logDir);  // Ensure directory exists (handles nested paths)
            MegaCustom::LogManager::instance().setLogDirectory(logDir.toStdString());

            // Update crash log path now that log directory is correct
            g_crashLogPath = logDir.toStdString() + "/crash.log";

            MegaCustom::LogManager::instance().log(
                MegaCustom::LogLevel::Info,
                MegaCustom::LogCategory::System,
                "startup",
                "Log directory: " + logDir.toStdString());
        }

        if (splash) {
            splash->showMessage("Initializing Mega SDK...",
                               Qt::AlignBottom | Qt::AlignHCenter,
                               Qt::black);
            QApplication::processEvents();
        }

        // Initialize backend
        if (!app.initializeBackend()) {
            if (splash) splash->close();
            QMessageBox::critical(nullptr, "Initialization Error",
                "Failed to initialize Mega SDK.\n"
                "Please check your configuration and try again.");
            return 1;
        }

        if (splash) {
            splash->showMessage("Creating user interface...",
                               Qt::AlignBottom | Qt::AlignHCenter,
                               Qt::black);
            QApplication::processEvents();
        }

        // Apply application style
        setApplicationStyle(app);

        // Create and show main window
        if (!app.createMainWindow()) {
            if (splash) splash->close();
            QMessageBox::critical(nullptr, "UI Error",
                "Failed to create main window.\n"
                "The application will now exit.");
            return 1;
        }

        // Close splash screen
        if (splash) {
            splash->finish(app.getMainWindow());
            delete splash;
        }

        // Check for auto-login - attempt if session file exists
        QString sessionFile = MegaCustom::Settings::instance().sessionFile();
        if (QFile::exists(sessionFile)) {
            qDebug() << "Session file found, attempting auto-login...";
            app.attemptAutoLogin();
        }

        // Start the event loop
        return app.exec();

    } catch (const std::exception& e) {
        if (splash) splash->close();
        QMessageBox::critical(nullptr, "Fatal Error",
            QString("An unexpected error occurred:\n%1\n\n"
                    "The application will now exit.").arg(e.what()));
        return 1;
    } catch (...) {
        if (splash) splash->close();
        QMessageBox::critical(nullptr, "Fatal Error",
            "An unknown error occurred.\n"
            "The application will now exit.");
        return 1;
    }
}