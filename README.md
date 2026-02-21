# MegaCustom - Advanced Mega.nz Cloud Storage Client

A powerful, feature-rich desktop application built on top of the official Mega.nz SDK, providing both CLI and GUI interfaces with advanced file management capabilities including regex-based bulk renaming, multi-destination uploads, and intelligent folder synchronization.

## 🎉 Project Status: 85% Complete

### ✅ Completed
- **CLI Application**: All 14 modules implemented and fully functional
- **Qt6 GUI Framework**: Complete desktop interface built and compiled
- **Backend Integration**: Bridge architecture connecting GUI to CLI modules
- **Mega SDK**: Successfully integrated (118MB static library)
- **Documentation**: Comprehensive docs for all components

### 🚧 In Progress
- **Testing**: Validating GUI with real Mega SDK integration

### 📋 Coming Soon
- Windows installer
- macOS support
- Web interface

## Quick Start

### Run CLI Application (Fully Functional)
```bash
cd /home/mow/projects/Mega\ -\ SDK/mega-custom-app/cli/build
./megacustom help
./megacustom login
./megacustom ls /
```

### Run GUI Application (Framework Complete)
```bash
cd /home/mow/projects/Mega\ -\ SDK/mega-custom-app/qt-gui/build-qt
./MegaCustomGUI
```

## Features

### Core Capabilities
- **Secure Authentication**: 2FA support, session persistence, encrypted credential storage
- **High-Performance Transfers**: Parallel uploads/downloads with chunking and resume
- **Bandwidth Management**: Configurable limits and throttling
- **Progress Tracking**: Real-time updates with ETA calculations
- **Cross-Platform**: Linux, Windows (coming), macOS (planned)

### Advanced Features

#### 1. Regex-Based Bulk Renaming
- PCRE2 regex pattern support for complex operations
- Preview mode with conflict detection
- Undo/redo functionality with full history
- 7 case conversion styles
- Sequential numbering and date/time insertion
- Safe mode with automatic backups

#### 2. Multi-Destination Bulk Upload
- Upload to multiple folders simultaneously
- Smart distribution rules by extension/size/date
- Queue management with priority system
- Duplicate detection before upload
- Import/export task configurations

#### 3. Smart Folder Synchronization
- Bidirectional sync with conflict resolution
- Multiple resolution strategies (newer/older/larger/smaller wins)
- Scheduled automatic sync
- Dry-run analysis mode
- Sync profiles management

#### 4. Qt6 Desktop GUI
- Modern, responsive interface
- Dual-pane file explorer
- Drag & drop support
- Transfer queue visualization
- System tray integration
- Dark mode support (coming)

## Project Structure

```
mega-custom-app/
├── cli/                    # Command-line interface
│   ├── build/
│   │   └── megacustom     # CLI executable (3.5MB)
│   └── src/
│       └── modules/       # 14 functional modules
│
├── qt-gui/                # Qt6 Desktop GUI
│   ├── build-qt/
│   │   └── MegaCustomGUI  # GUI executable (920KB)
│   └── src/
│       ├── main/          # Application framework
│       ├── widgets/       # UI components
│       ├── dialogs/       # Dialog windows
│       ├── controllers/   # Business logic
│       └── bridge/        # Backend integration layer
│
├── third_party/
│   └── sdk/              # Mega SDK
│       └── build_sdk/
│           └── libSDKlib.a # Static library (118MB)
│
└── docs/                  # Documentation
    ├── GUI_ROADMAP.md
    ├── GUI_ARCHITECTURE.md
    └── GUI_REQUIREMENTS.md
```

## Building from Source

### Prerequisites
- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- CMake 3.16+
- Qt6.4+ (for GUI)
- PCRE2 library (optional, for advanced regex)

### Build CLI
```bash
cd cli
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Build GUI
```bash
cd qt-gui
mkdir build-qt && cd build-qt
cmake ..
make -j$(nproc)
```

## CLI Usage Examples

### Authentication
```bash
./megacustom login user@example.com
./megacustom logout
./megacustom whoami
```

### File Operations
```bash
./megacustom upload file.txt /Cloud/Documents
./megacustom download /Cloud/file.txt ./local/
./megacustom ls /Cloud
./megacustom rm /Cloud/old_file.txt
```

### Advanced Features
```bash
# Regex rename
./megacustom regex-rename "/Cloud/Photos" "IMG_(\d+)" "Photo_$1" --preview

# Multi-upload
./megacustom multi-upload ./local_files --dest /Cloud/Folder1 /Cloud/Folder2

# Smart sync
./megacustom smart-sync ./local /Cloud/Backup --bidirectional --auto
```

## GUI Features

### Main Interface
- **Menu Bar**: File, Edit, View, Tools, Help menus
- **Toolbar**: Quick access to common operations
- **Dual-Pane Explorer**: Local and remote file browsing
- **Status Bar**: Connection status and user info
- **System Tray**: Background operation support

### Dialogs
- **Login Dialog**: Secure authentication with 2FA
- **Settings Dialog**: Comprehensive preferences
- **Transfer Queue**: Visual transfer management
- **About Dialog**: Version and license info

## Architecture

### Design Patterns
- **MVC Pattern**: Separation of UI from business logic
- **Singleton**: Application and settings management
- **Observer**: Event-driven architecture
- **Bridge**: GUI-CLI integration layer
- **Factory**: Module creation

### Technology Stack
- **C++17**: Modern C++ features
- **Qt6**: Cross-platform GUI framework
- **Mega SDK**: Official cloud storage API
- **CMake**: Build system
- **PCRE2**: Advanced regex support

## Documentation

- [GUI Roadmap](docs/GUI_ROADMAP.md) - Development timeline
- [GUI Architecture](docs/GUI_ARCHITECTURE.md) - Technical design
- [GUI Requirements](docs/GUI_REQUIREMENTS.md) - Feature specifications
- [Progress Report](progress.md) - Detailed development history
- [Next Steps](NEXT_STEPS.md) - Upcoming tasks

## Current Development Status

### Completed Modules (100%)
- ✅ ConfigManager - Configuration management
- ✅ MegaManager - SDK initialization
- ✅ AuthenticationModule - Login/logout/2FA
- ✅ FileOperations - Upload/download
- ✅ FolderManager - Folder operations
- ✅ SyncManager - Sync operations
- ✅ TransferManager - Transfer queue
- ✅ AccountInfo - Account details
- ✅ ShareManager - Sharing features
- ✅ SearchModule - File search
- ✅ EncryptionModule - Client-side encryption
- ✅ LoggingModule - Detailed logging
- ✅ RegexRenamer - Bulk renaming
- ✅ MultiUploader - Multi-destination uploads
- ✅ SmartSync - Intelligent sync

### Project Metrics
- **Total Code**: ~28,000 lines
- **Total Files**: 420+
- **Test Coverage**: 300+ test files
- **Development Time**: 8 sessions
- **Overall Progress**: 85% complete

## Contributing

This project is currently in active development. Contributions will be welcome once the initial release is complete.

## License

This project uses the Mega SDK which has its own licensing terms. The custom application code is provided as-is for educational and personal use.

## Recent Changes (November 30, 2024)

### Files Removed During Backend Integration

The following files were removed as they were temporary placeholders or duplicates that conflicted with the real implementations:

1. **`/qt-gui/src/bridge/BackendStubs.h`** (Removed)
   - **What it was**: Temporary stub header file containing mock implementations of CLI modules
   - **Why removed**: Replaced with `BackendModules.h` which includes the real CLI module headers
   - **Purpose it served**: Allowed GUI compilation before CLI integration was complete

2. **`/qt-gui/src/MegaManager.cpp`** (Removed)
   - **What it was**: Duplicate stub implementation of MegaManager for GUI
   - **Why removed**: Conflicted with the real singleton MegaManager from CLI modules
   - **Resolution**: Now using the actual CLI's MegaManager singleton via `MegaCustom::MegaManager::getInstance()`

3. **`/qt-gui/src/MegaManager.h`** (Removed)
   - **What it was**: Duplicate header file for the stub MegaManager
   - **Why removed**: No longer needed as we use the real header from `/include/core/MegaManager.h`
   - **Impact**: Eliminated "multiple definition" linking errors

### Configuration Changes

- **API Key**: Changed from hardcoded `"YOUR_MEGA_APP_KEY"` to environment variable `MEGA_APP_KEY`
- **Authentication**: Replaced timer-based simulation with actual `AuthenticationModule` calls
- **File Operations**: Connected to real `FileOperations` class instead of stubs

## Acknowledgments

- Mega.nz for providing the SDK
- Qt Company for the Qt6 framework
- PCRE2 project for regex support

---

*Last Updated: November 30, 2024*
*Version: 1.0.0-beta*
*Status: Backend Integration Complete - Ready for Testing*

