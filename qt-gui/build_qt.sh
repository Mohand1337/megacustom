#!/bin/bash

# Qt6 GUI Build Script for MegaCustom
# This script builds the Qt6 desktop application

set -e  # Exit on error

echo "=========================================="
echo "Building MegaCustom Qt6 Desktop GUI"
echo "=========================================="
echo

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check for Qt6
echo "Checking for Qt6..."
if ! command -v qmake6 &> /dev/null && ! command -v qmake &> /dev/null; then
    echo -e "${RED}✗ Qt6 not found!${NC}"
    echo "Please install Qt6 first:"
    echo "  Ubuntu/Debian: sudo apt install qt6-base-dev qt6-tools-dev"
    echo "  Fedora: sudo dnf install qt6-qtbase-devel"
    echo "  Windows: Download from https://www.qt.io/download"
    exit 1
fi

# Determine qmake command
QMAKE_CMD=""
if command -v qmake6 &> /dev/null; then
    QMAKE_CMD="qmake6"
elif command -v qmake &> /dev/null; then
    # Check if it's Qt6
    QT_VERSION=$(qmake -query QT_VERSION)
    if [[ $QT_VERSION == 6.* ]]; then
        QMAKE_CMD="qmake"
    else
        echo -e "${RED}✗ Found Qt $QT_VERSION but Qt6 is required${NC}"
        exit 1
    fi
fi

echo -e "${GREEN}✓ Found Qt6 (using $QMAKE_CMD)${NC}"

# Create build directory
BUILD_DIR="build-qt"
if [ -d "$BUILD_DIR" ]; then
    echo "Cleaning existing build directory..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Create stub files for missing components (temporary)
echo "Creating stub implementations..."

# Create minimal stubs directory
mkdir -p stubs/{controllers,widgets,utils,models,dialogs}

# Create AuthController stub
cat > stubs/controllers/AuthController.h << 'EOF'
#ifndef AUTH_CONTROLLER_H
#define AUTH_CONTROLLER_H
#include <QObject>
namespace MegaCustom {
class AuthController : public QObject {
    Q_OBJECT
public:
    AuthController(void* api) : QObject() {}
    QString currentUser() const { return "user@example.com"; }
    void login(const QString&, const QString&) {}
    void logout() {}
    void saveSession(const QString&, const QString&) {}
    void restoreSession(const QString&) {}
signals:
    void loginSuccess(const QString&);
    void loginFailed(const QString&);
    void logoutComplete();
};
}
#endif
EOF

# Create FileController stub
cat > stubs/controllers/FileController.h << 'EOF'
#ifndef FILE_CONTROLLER_H
#define FILE_CONTROLLER_H
#include <QObject>
namespace MegaCustom {
class FileController : public QObject {
    Q_OBJECT
public:
    FileController(void* api) : QObject() {}
    QString currentLocalPath() const { return "/"; }
    QString currentRemotePath() const { return "/"; }
    void navigateToLocal(const QString&) {}
    void navigateToRemote(const QString&) {}
    void refreshRemote(const QString&) {}
    void createRemoteFolder(const QString&) {}
    void deleteRemote(const QString&) {}
    void renameRemote(const QString&, const QString&) {}
};
}
#endif
EOF

# Create TransferController stub
cat > stubs/controllers/TransferController.h << 'EOF'
#ifndef TRANSFER_CONTROLLER_H
#define TRANSFER_CONTROLLER_H
#include <QObject>
namespace MegaCustom {
class TransferController : public QObject {
    Q_OBJECT
public:
    TransferController(void* api) : QObject() {}
    bool hasActiveTransfers() const { return false; }
    void cancelAllTransfers() {}
    void uploadFile(const QString&, const QString&) {}
    void uploadFolder(const QString&, const QString&) {}
    void downloadFile(const QString&, const QString&) {}
signals:
    void transferStarted(const QString&);
    void transferProgress(const QString&, qint64, qint64);
    void transferCompleted(const QString&);
    void transferFailed(const QString&, const QString&);
};
}
#endif
EOF

# Create TransferQueue stub
cat > stubs/widgets/TransferQueue.h << 'EOF'
#ifndef TRANSFER_QUEUE_H
#define TRANSFER_QUEUE_H
#include <QWidget>
namespace MegaCustom {
class TransferController;
class TransferQueue : public QWidget {
    Q_OBJECT
public:
    TransferQueue(QWidget* parent = nullptr) : QWidget(parent) {}
    void setTransferController(TransferController*) {}
};
}
#endif
EOF

# Create Settings stub
cat > stubs/utils/Settings.h << 'EOF'
#ifndef SETTINGS_H
#define SETTINGS_H
#include <QString>
#include <QByteArray>
namespace MegaCustom {
class Settings {
public:
    static Settings& instance() { static Settings s; return s; }
    void load() {}
    void save() {}
    void loadFromFile(const QString&) {}
    bool autoLogin() const { return false; }
    bool rememberLogin() const { return false; }
    void setRememberLogin(bool) {}
    QString apiKey() const { return ""; }
    QString sessionFile() const { return "session.dat"; }
    QString lastEmail() const { return ""; }
    QString lastLocalPath() const { return "/home"; }
    QString lastRemotePath() const { return "/"; }
    void setLastLocalPath(const QString&) {}
    void setLastRemotePath(const QString&) {}
    bool darkMode() const { return false; }
    bool showHiddenFiles() const { return false; }
    QByteArray windowGeometry() const { return QByteArray(); }
    QByteArray windowState() const { return QByteArray(); }
    void setWindowGeometry(const QByteArray&) {}
    void setWindowState(const QByteArray&) {}
};
}
#endif
EOF

# Create MegaManager stub
cat > stubs/MegaManager.h << 'EOF'
#ifndef MEGA_MANAGER_H
#define MEGA_MANAGER_H
#include <memory>
namespace mega { class MegaApi; }
namespace MegaCustom {
class MegaManager {
public:
    MegaManager() {}
    bool initialize(const std::string&) { return true; }
    mega::MegaApi* getMegaApi() { return nullptr; }
    static MegaManager& getInstance() { static MegaManager m; return m; }
    bool isInitialized() const { return true; }
};
}
#endif
EOF

# Create AboutDialog stub
cat > stubs/dialogs/AboutDialog.h << 'EOF'
#ifndef ABOUT_DIALOG_H
#define ABOUT_DIALOG_H
#include <QDialog>
namespace MegaCustom {
class AboutDialog : public QDialog {
public:
    AboutDialog(QWidget* parent = nullptr) : QDialog(parent) {}
};
}
#endif
EOF

# Create SettingsDialog stub
cat > stubs/dialogs/SettingsDialog.h << 'EOF'
#ifndef SETTINGS_DIALOG_H
#define SETTINGS_DIALOG_H
#include <QDialog>
namespace MegaCustom {
class SettingsDialog : public QDialog {
public:
    SettingsDialog(QWidget* parent = nullptr) : QDialog(parent) {}
};
}
#endif
EOF

# Create simplified CMakeLists.txt for stub build
cat > ../CMakeLists_stub.txt << 'EOF'
cmake_minimum_required(VERSION 3.16)
project(MegaCustomGUI VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
qt_standard_project_setup()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/build-qt/stubs
    ${CMAKE_CURRENT_SOURCE_DIR}/build-qt/stubs/controllers
    ${CMAKE_CURRENT_SOURCE_DIR}/build-qt/stubs/widgets
    ${CMAKE_CURRENT_SOURCE_DIR}/build-qt/stubs/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/build-qt/stubs/dialogs
)

set(SOURCES
    src/main.cpp
    src/main/Application.cpp
    src/main/MainWindow.cpp
    src/widgets/FileExplorer.cpp
    src/dialogs/LoginDialog.cpp
)

qt_add_executable(MegaCustomGUI ${SOURCES})

target_link_libraries(MegaCustomGUI PRIVATE Qt6::Core Qt6::Widgets)

# Simplified includes for stub build
target_include_directories(MegaCustomGUI PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/build-qt/stubs
)
EOF

# Configure with CMake
echo
echo "Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_PREFIX_PATH="$(qmake6 -query QT_INSTALL_PREFIX 2>/dev/null || qmake -query QT_INSTALL_PREFIX)"

if [ $? -ne 0 ]; then
    echo -e "${RED}✗ CMake configuration failed${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Configuration complete${NC}"

# Build
echo
echo "Building application..."
make -j$(nproc)

if [ $? -ne 0 ]; then
    echo -e "${RED}✗ Build failed${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Build complete${NC}"

# Check if executable was created
if [ -f "MegaCustomGUI" ]; then
    echo
    echo -e "${GREEN}=========================================="
    echo "✓ MegaCustom Qt6 GUI built successfully!"
    echo "=========================================="
    echo
    echo "Executable: $(pwd)/MegaCustomGUI"
    echo
    echo "To run:"
    echo "  cd build-qt"
    echo "  ./MegaCustomGUI"
    echo
    echo "Note: This is a stub build for testing the UI."
    echo "Full backend integration requires linking with Mega SDK."
    echo -e "${NC}"
else
    echo -e "${RED}✗ Executable not found${NC}"
    exit 1
fi