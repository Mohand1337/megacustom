# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.3.1] - 2026-02-25

### Added - Full UI/UX Rebuild + Template-Based Watermarks

#### UI/UX Design System Rebuild
- **QSS design system** — All 6 panels (CloudCopier, Distribution, Downloader, LogViewer, MemberRegistry, Watermark) aligned to objectName + property-based styling
- **AnimationHelper utility** (`utils/AnimationHelper.h/cpp`) — QPropertyAnimation-based fade/slide effects for panel transitions
- **Light & dark themes** fully updated (`mega_light.qss`, `mega_dark.qss`) with new QSS rules:
  - `#WatermarkPreviewLabel` — monospace, themed background
  - `#ConnectionIndicator` — border-radius, themed bg color
  - `#UserLabel` — font-weight 500, themed color
- **Runtime theme switching** — Connected `SettingsPanel::settingsSaved` → `MainWindow::applySettings`
- **Removed all inline `setStyleSheet()` calls** — Replaced with property-based styling (`setProperty("type", "secondary")`)
- **Fixed `PanelDescription` → `PanelSubtitle`** in MemberRegistryPanel (3 instances had no QSS rule)

#### Template-Based Watermark Text System
- **13 template variables** for watermark text customization:
  - `{brand}` — Brand name (Easygroupbuys.com)
  - `{member}`, `{member_id}`, `{member_name}` — Core member fields
  - `{member_email}`, `{member_ip}`, `{member_mac}`, `{member_social}` — Extended member fields (NEW)
  - `{month}`, `{month_num}`, `{year}`, `{date}`, `{timestamp}` — Date/time fields
- **User-controlled format** — Text fields enabled in both Global and Per-Member mode (previously disabled in member mode)
- **Pre-fill defaults** — Switching to member mode auto-fills `{brand} - {member_name} ({member_id})`
- **Pre-start validation** — Warns about missing member fields (email, IP, MAC, social) before watermarking
- **Help dialog** — Updated with all 13 variables, categorized with section headers
- **Preset system** — Save/load/delete named watermark presets (text, CRF, interval, duration, FFmpeg preset)

### Fixed - Feb 25, 2026
- **CRITICAL: Watermark data mixing** — Two different members' data mixed in one watermark. Root cause: `Watermarker::watermarkVideoForMember()` loaded member data from C++ core's `MemberDatabase` (separate from Qt's `MemberRegistry` singleton). Fix: Bypass `watermarkVideoForMember()`/`watermarkPdfForMember()` entirely — expand templates in Qt layer using `TemplateExpander` + `MemberRegistry` (single source of truth)
- **WatermarkerController** also updated to use template expansion (was using old ForMember methods)
- **Dead code removed** — `setMemberDbPath()` (set but never read after template system rewrite)
- **Thread safety** — `std::atomic<bool>` for `m_cancelled` in DownloaderPanel and WatermarkPanel workers
- **Double-start guards** — Added `if (m_isRunning) return` to `onStartWatermark()` and `onStartDistribution()`
- **Transfer speed connection** — Moved from dead `connectSignals()` to `setTransferController()` in MainWindow
- **Status bar hardcoded colors** — Removed inline styles, replaced with QSS rules

### Changed - Feb 25, 2026
- **TemplateExpander** — Extended from 8 to 13 variables, longest-first replacement to prevent partial matches
- **WatermarkWorker::process()** — Complete rewrite: expands templates per-member, calls `watermarkVideo()`/`watermarkPdf()` directly
- **buildConfig()** — Only expands templates for global mode; member mode keeps raw templates for per-member expansion by worker
- **Windows build** — Verified one-liner pull+build+deploy PowerShell command (uses `;` not `&&`)

## [0.3.0] - 2026-02-24

### Added - Phase 2 Sessions: Watermarking, Distribution, Member Groups (Dec 2025 - Feb 2026)

#### Member Groups System
- **MemberGroup struct** (`MemberRegistry.h`) - Named groups of members for quick selection (e.g., "NHB2026")
- **Full CRUD** in `MemberRegistry` - `addGroup`, `updateGroup`, `removeGroup`, `getGroupMemberIds`, `getGroupsForMember`
- **JSON persistence** - Groups stored as top-level `"groups"` array in `members.json` (backward compatible)
- **Groups tab** in MemberRegistryPanel - Full management UI:
  - QSplitter layout: group list (left) + checkbox member assignment (right)
  - Create, Rename, Duplicate, Delete groups
  - Search filter for member assignment
  - Bulk Select All / Deselect All (respects search filter)
  - Live count updates without full UI rebuild (guard flag pattern)
  - Context menu on group list (Rename, Duplicate, Delete)
- **"Add to Group..." submenu** in Members table right-click context menu (checkmark toggle)
- **"Groups" column** (8th column) in Members table showing group membership in blue text
- **WatermarkPanel group selection** - Groups appear in member combo between "All Members" and individuals
  - `[Group] NHB2026 (5)` format with separators
  - `GROUP:` prefix convention for group data in QComboBox
  - Resolves to active member IDs on watermark start
- **DistributionPanel group combo** - Quick-select dropdown to check group members in table
  - Deselects all, then checks group members, resets combo to prompt

#### Distribution Panel UX Improvements
- **Stop confirmation dialog** - QMessageBox::question before cancelling distribution
- **Pause visual feedback** - Progress bar chunk dims to gray when paused, restores on resume
- **Real-time template validation** - Red border on invalid destination template via `textChanged` signal
- **Move mode warning banner** - Persistent red banner "WARNING: MOVE MODE" toggled by move checkbox
- **Two-row button layout** - Selection controls (Select All, Deselect All, Group, Bulk Rename) on row 1, execution controls (Preview, Start, Pause, Stop) on row 2

#### Phase 2 Core Systems (Implemented across Dec 2025 - Feb 2026)
- **MemberRegistry** (`utils/MemberRegistry.h/cpp`) - JSON-based member storage with paths, watermark config, distribution folders, WordPress sync tracking
- **MemberRegistryPanel** (`widgets/MemberRegistryPanel.h/cpp`) - Full member management UI with search, filters, import/export (JSON + CSV)
- **WatermarkPanel** (`widgets/WatermarkPanel.h/cpp`) - FFmpeg video watermarking with per-member personalization, batch processing
- **WatermarkerController** (`controllers/WatermarkerController.h/cpp`) - Async watermark orchestration with worker threads
- **DistributionPanel** (`widgets/DistributionPanel.h/cpp`) - Scan /latest-wm/, match to members, bulk copy/move to destinations
- **DistributionController** (`controllers/DistributionController.h/cpp`) - MegaApi-based cloud upload with progress tracking
- **CloudCopier** - Server-side copy/move operations for distribution
- **FolderCopyWorker** - QThread-based async folder copy with pause/resume/cancel
- **TemplateExpander** (`utils/TemplateExpander.h/cpp`) - Variable expansion for destination paths ({member}, {year}, {month}, etc.)
- **LogManager** (`core/LogManager.cpp`) - Persistent file logging
- **LogViewerPanel** (`widgets/LogViewerPanel.h/cpp`) - Searchable activity log viewer
- **WordPressConfigDialog** / **WatermarkSettingsDialog** - Configuration dialogs
- **RemoteFolderBrowserDialog** - MEGA cloud folder browser for binding member folders

### Fixed - Feb 2026
- **Account switching freeze** - Fixed blocking `fetchNodes()` call during account switch
- **FFmpeg watermark crashes** - Fixed argument ordering and process management
- **Distribution pipeline upload** - Injected MegaApi-based upload function into DistributionPipeline
- **Checkbox toggle rebuild bug** - Added `m_suppressGroupRefresh` guard to prevent full UI rebuild on every checkbox click in Groups tab

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
