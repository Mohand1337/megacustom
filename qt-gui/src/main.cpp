/**
 * MegaCustom GUI Application
 * Main entry point for Qt6 Desktop Application
 */

#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QMessageBox>
#include <QSplashScreen>
#include <QPixmap>
#include <QTimer>

#include "main/Application.h"
#include "main/MainWindow.h"
#include "utils/Settings.h"

// Version information
#define APP_NAME "MegaCustom"
#define APP_VERSION "1.0.0"
#define APP_ORGANIZATION "MegaCustom"
#define APP_DOMAIN "megacustom.app"

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