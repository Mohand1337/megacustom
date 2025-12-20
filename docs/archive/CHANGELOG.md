# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.0] - 2025-12-08/10

### Added - Session 18: Multi-Account Support (December 7-8, 2025)

#### Multi-Account Infrastructure (6 Core Components)
- **AccountManager** (`src/accounts/AccountManager.h/cpp`) - Central coordinator singleton for all account operations
  - Manages active account switching
  - Coordinates between SessionPool, CredentialStore, and TransferManager
  - Emits signals for account changes and status updates
- **SessionPool** (`src/accounts/SessionPool.h/cpp`) - Multi-MegaApi caching with LRU eviction
  - Maximum 5 concurrent sessions to prevent resource exhaustion
  - Automatic session creation and cleanup
  - Per-account MegaApi instance management
- **CredentialStore** (`src/accounts/CredentialStore.h/cpp`) - Encrypted file storage for session tokens
  - AES-256 encryption for stored credentials
  - Keychain integration fallback for secure storage
  - Session token persistence across app restarts
- **TransferLogStore** (`src/accounts/TransferLogStore.h/cpp`) - SQLite-based transfer history tracking
  - Persistent cross-account transfer history
  - Query support for filtering and searching transfers
  - Automatic cleanup of old transfer records
- **CrossAccountTransferManager** (`src/accounts/CrossAccountTransferManager.h/cpp`) - Copy/move files between accounts via public links
  - Public link creation for source files
  - Link import to target account
  - Progress tracking and error handling
  - Delete source after successful move operations
- **AccountModels** (`src/accounts/AccountModels.h`) - Data structures
  - `MegaAccount` - Account information and credentials
  - `AccountGroup` - Account organization and grouping
  - `CrossAccountTransfer` - Transfer operation metadata

#### Multi-Account UI Components (4 Components)
- **AccountSwitcherWidget** (`src/widgets/AccountSwitcherWidget.h/cpp`) - Sidebar account list with groups
  - Visual account list in sidebar header
  - Group-based organization
  - Quick account switching with single click
  - Active account highlighting
- **AccountManagerDialog** (`src/dialogs/AccountManagerDialog.h/cpp`) - Full account management UI
  - Add/remove accounts
  - Edit account details
  - Group management
  - Credential viewing (masked)
- **QuickPeekPanel** (`src/widgets/QuickPeekPanel.h/cpp`) - Browse other accounts without switching
  - Non-destructive account browsing
  - File preview from any account
  - No active account change required
- **CrossAccountLogPanel** (`src/widgets/CrossAccountLogPanel.h/cpp`) - Transfer history view
  - SQLite-backed transfer history
  - Filtering by account, date, status
  - Transfer statistics and analytics

#### Cross-Account Operations
- **Copy to Account**: Creates temporary public link → imports to target account → revokes link
- **Move to Account**: Performs copy operation → deletes from source account on success
- **Progress Tracking**: All transfers logged to SQLite with status, timestamps, and metadata
- **Context Menu Integration**: Right-click on files in FileExplorer shows "Copy to Account" and "Move to Account"
- **Completion Notifications**: Message box feedback with transfer results and status bar updates

#### Keyboard Shortcuts
- `Ctrl+Tab` - Cycle to next account in list
- `Ctrl+Shift+Tab` - Cycle to previous account in list
- `Ctrl+Shift+A` - Open account switcher popup dialog

#### Critical Performance Optimization
- **Per-Account SDK Caching** (`src/accounts/SessionPool.cpp` lines 295-309)
  - Individual cache directories: `QStandardPaths::AppDataLocation/mega_cache/{accountId}/`
  - Prevents SDK from re-downloading entire filesystem tree on every restart
  - Without valid basePath parameter, SDK completely disables local node caching
  - Dramatically improves startup time and reduces network bandwidth usage
  - Each account maintains independent cache to prevent conflicts

### Added - Session 19: Code Cleanup & Documentation (December 10, 2025)
- **Documentation Consolidation**
  - Removed backup directories (`src_backup_*`) with obsolete code
  - Consolidated multi-account documentation across SESSION_RECOVERY.md, GUI_ROADMAP.md, PROGRESS.md
  - Updated all README files with current project status
  - Created comprehensive plan documentation for Sessions 18-19
- **Project Statistics Documentation**
  - Total source files: 2,346 (.cpp/.h files)
  - Total code: 40,000+ lines
  - Multi-account system: 6 core components, 4 UI components
  - GUI executable: 11MB
  - SDK library: 118MB (libSDKlib.a)

### Fixed - Session 18: Multi-Account Bug Fixes (December 8, 2025)
- **Sidebar buttons greyed out** after account switch
  - Root cause: `FileExplorer` and other widgets not notified of login state change
  - Fix: Call `setLoggedIn(true)` in `MainWindow::onAccountSwitched()` to refresh all UI components
  - File: `qt-gui/src/main/MainWindow.cpp`
- **App crash on startup** during initial account check
  - Root cause: AccountManager not fully initialized when first accessed
  - Fix: Defer initial account check using `QTimer::singleShot(0, ...)` to ensure event loop is running
  - File: `qt-gui/src/main/MainWindow.cpp`
- **"Invalid argument" error** on cross-account copy operations
  - Root cause: `MegaApi::exportNode()` expireTime parameter set to -1 (invalid)
  - Fix: Changed expireTime from -1 to 0 (permanent link, manually revoked after import)
  - File: `qt-gui/src/accounts/CrossAccountTransferManager.cpp`
- **No completion notification** for cross-account transfers
  - Root cause: Transfer completion signals not connected to MainWindow
  - Fix: Connected `CrossAccountTransferManager::transferCompleted` and `transferFailed` signals to MainWindow slots
  - Files: `qt-gui/src/main/MainWindow.h/cpp`
- **Missing QInputDialog include** in MainWindow
  - Fix: Added `#include <QInputDialog>` for destination path input dialogs
  - File: `qt-gui/src/main/MainWindow.cpp`

### Changed - Session 19: Code Cleanup (December 10, 2025)
- **Dead Code Removal**
  - Removed unused view mode slots in MainWindow: `onViewList()`, `onViewGrid()`, `onViewDetails()`
  - Removed obsolete action members: `viewListAction`, `viewGridAction`, `viewDetailsAction`
  - Commented out bridge layer files in `qt-gui/CMakeLists.txt` (kept for reference)
    - `src/bridge/FileBridge.h/cpp`
    - `src/bridge/TransferBridge.h/cpp`
    - `src/bridge/ShareBridge.h/cpp`
  - Controllers now use Mega SDK directly without bridge abstraction layer
- **Build Configuration**
  - Bridge layer excluded from compilation but preserved in source tree for reference
  - All GUI components now directly integrate with CLI modules and SDK

---

## [0.1.0] - 2025-12-03/06

### Added
- **PathUtils** - New centralized utility for path normalization (`src/utils/PathUtils.h`)
  - `normalizeRemotePath()` - removes \r, leading whitespace only (preserves trailing spaces in folder names), ensures starts with /
  - `normalizeLocalPath()` - uses trimmed() for local paths
  - `isPathEmpty()` - checks if path is empty after normalization
- **CloudCopierController** path validation features:
  - `PathValidationResult` struct for validation feedback (path, exists, isFolder, errorMessage)
  - `validateSources()` method for validating source paths
  - `validateDestinations()` method for validating destination paths
  - `sourcesValidated(QVector<PathValidationResult>)` signal for real-time feedback
  - `destinationsValidated(QVector<PathValidationResult>)` signal for real-time feedback
- Path validation feature in Cloud Copier Panel with purple "Validate" button
- **FileExplorer** new methods:
  - `sortByColumn(int column, Qt::SortOrder order)` - sort by column
  - `selectAll()` - select all items in view
  - `moveRequested` and `copyRequested` signals for clipboard operations
- **TransferController** status bar integration:
  - `globalSpeedUpdate(qint64 uploadSpeed, qint64 downloadSpeed)` signal
  - Real-time upload/download speed tracking and aggregation
  - Status bar labels now show live transfer speeds

### Fixed
- Fixed issue where folder names with trailing spaces (like "November. ") were being incorrectly trimmed
- Fixed iterator invalidation crash in Cloud Copier when removing sources/destinations
- Fixed paste destinations to preserve trailing spaces in folder names
- **Thread safety fixes** in controllers using QtConcurrent:
  - `MultiUploaderController` - changed `m_isUploading`, `m_isPaused`, `m_cancelRequested` to `std::atomic<bool>`
  - `FolderMapperController` - changed `m_isUploading`, `m_cancelRequested` to `std::atomic<bool>`
  - `CloudCopierController` - changed `m_applyToAllResolution` to `std::atomic<CopyConflictResolution>` (fixes race condition in conflict callback)
- **Dead code removal**:
  - Removed unused view mode slots (onViewList, onViewGrid, onViewDetails) and action members
  - Commented out bridge layer files from CMakeLists.txt (kept for reference)
- **Edit menu** now functional - Cut, Copy, Paste, Select All connected to FileExplorer
- **View menu** sorting now functional - Sort by Name/Size/Date connected to FileExplorer
- **Show Hidden** toggle now functional in View menu
- **Status bar speed display** - Upload/download speeds now update in real-time during transfers

### Changed
- Updated AddDestinationDialog.cpp to use PathUtils
- Updated MappingEditDialog.cpp to use PathUtils
- Updated SyncProfileDialog.cpp to use PathUtils
- Updated FolderMapperPanel.cpp to use PathUtils
- Updated RemoteFolderBrowserDialog.cpp to use PathUtils
- Updated CloudCopierPanel.cpp to use PathUtils
- Bridge layer files commented out in CMakeLists.txt (controllers now use SDK directly)
