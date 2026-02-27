# MegaCustom - Advanced Mega.nz Cloud Storage Client

A powerful, feature-rich desktop application built on the official Mega.nz C++ SDK, providing both CLI and Qt6 GUI interfaces with multi-account support, watermarking, member distribution, intelligent sync, and self-learning metrics.

## Project Status: ~95% Complete

| Phase | Status | Description |
|-------|--------|-------------|
| Phase 1 | COMPLETE | CLI + GUI + Multi-Account + Search |
| Phase 2 | ~95% | Members, Watermarking, Distribution, Groups, Smart Engine |

## Quick Start

### Run GUI
```bash
cd qt-gui/build-qt
cmake .. && make -j$(nproc)
export MEGA_APP_KEY="9gETCbhB"
./MegaCustomGUI
```

### Run CLI
```bash
make && ./megacustom help
./megacustom auth login EMAIL PASSWORD
./megacustom folder list /
```

## Features

### Core
- **Multi-Account Support** — Switch between MEGA accounts instantly (Ctrl+Tab), up to 5 cached sessions (LRU)
- **Cross-Account Transfers** — Copy/move files between accounts via public links
- **Everything-like Search** — Instant cloud search (<100ms) with regex, size, date, type filters
- **Dual-Pane File Explorer** — Cloud file browsing with drag-drop, context menus, breadcrumb navigation
- **System Tray** — Background operation with tray icon and menu
- **Light & Dark Themes** — QSS-based design system with MEGA branding

### File Management
- **Folder Mapper** — Map local folders to MEGA destinations, incremental upload
- **Multi Uploader** — Upload to multiple destinations with smart routing rules
- **Smart Sync** — Bidirectional sync with conflict resolution and scheduling
- **Cloud Copier** — Cloud-to-cloud copy/move with template expansion and member mode
- **Transfer Queue** — Progress tracking with real-time speed display

### Member Management & Distribution
- **Member Registry** — JSON-based member database with groups, paths, watermark config
- **Member Groups** — Named groups (e.g., "NHB2026") for quick selection across panels
- **Distribution Panel** — Scan folders, match to members, bulk upload to member destinations
- **WordPress Sync** — Import members from WordPress REST API
- **Import/Export** — JSON and CSV support

### Watermarking
- **Video Watermarking** — FFmpeg-based with configurable text, position, opacity, interval, duration
- **Template Variables** — 13 variables: `{brand}`, `{member_name}`, `{member_id}`, `{member_email}`, `{member_ip}`, `{member_mac}`, `{member_social}`, `{member}`, `{month}`, `{month_num}`, `{year}`, `{date}`, `{timestamp}`
- **Per-Member Personalization** — Each member gets unique watermark text
- **Preset System** — Save/load/delete named watermark presets
- **Auto-Upload Pipeline** — Watermark → Upload to MEGA → Delete local → Next member (saves disk space)
- **Pipeline Integration** — Downloader → Watermark → Distribution (auto-send between panels)

### Smart Engine (Self-Learning)
- **MetricsStore** — SQLite database recording every operation (sizes, durations, speeds)
- **EMA Learning** — Exponential Moving Average adapts predictions to changing conditions
- **Pre-Flight Checks** — Estimates disk space needed before starting, warns if insufficient
- **Smart Estimates** — Live display of predicted output size, duration, upload time
- **Confidence Levels** — Conservative (new) → Learning → Improving → Confident (20+ operations)

### Logging & History
- **Activity Log** — Searchable activity viewer with level/category filtering
- **Transfer Log** — SQLite-backed cross-account transfer history
- **Distribution Tracking** — Per-member watermark and distribution counts

## Architecture

```
Application
├── MainWindow (161 signal/slot connections)
│   ├── MegaSidebar → QStackedWidget (14 panels)
│   ├── TopToolbar (breadcrumb, search, file actions)
│   ├── FileExplorer (cloud drive browser)
│   ├── 12 Feature Panels
│   └── TransferQueue
├── 9 Controllers (Auth, File, Transfer, FolderMapper, MultiUploader,
│   SmartSync, CloudCopier, Distribution, Watermarker)
├── Account System (AccountManager, SessionPool, CredentialStore)
├── Search System (CloudSearchIndex, SearchQueryParser)
├── Styling (ThemeManager, DesignTokens, StyleSheetGenerator)
└── Singletons (MemberRegistry, MetricsStore, Settings, IconProvider)
```

### Key Patterns
- **Dependency Injection** — Controllers injected into panels via setters
- **Signal/Slot** — Loose coupling via Qt signals for inter-component communication
- **Worker Threads** — QThread for heavy operations (watermarking, distribution, downloads)
- **MVC Separation** — Controllers (logic), Panels (UI), Models (data)
- **Template Expansion** — 13-variable system for watermark text and destination paths

## Project Statistics

| Metric | Value |
|--------|-------|
| Lines of Code | ~62,000+ |
| Panels/Widgets | 30+ |
| Controllers | 9 |
| Dialogs | 17 |
| SQLite Databases | 2 (transfer_log.db, metrics.db) |
| Signal Connections | 161 (MainWindow) |

## Building

### Prerequisites
- C++17 compiler (GCC 7+, Clang 5+, MSVC 2017+)
- CMake 3.16+
- Qt 6 (Widgets, Network, Concurrent, Sql, Svg)
- OpenSSL, CURL, ZLIB, libsodium, SQLite3, ICU, crypto++, c-ares

### Linux
```bash
cd qt-gui/build-qt && cmake .. && make -j$(nproc)
```

### Windows (VCPKG)
```powershell
$cmake = "path\to\cmake.exe"
& $cmake --build qt-gui\build-win64 --config Release
```

## Documentation

| File | Description |
|------|-------------|
| `claude.md` | Full project context (AI agent reference) |
| `progress.md` | Quick status tracker |
| `docs/USER_GUIDE.md` | End-user documentation |
| `docs/DEVELOPER_GUIDE.md` | Technical reference |
| `docs/archive/CHANGELOG.md` | Detailed version history |
| `docs/archive/TODO.md` | Remaining work |

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+Tab | Cycle to next account |
| Ctrl+Shift+Tab | Cycle to previous account |
| Ctrl+Shift+A | Open account switcher |
| Ctrl+Shift+F | Advanced search |
| Ctrl+F | Find in current view |
| F5 | Refresh |
| F2 | Rename |

## License

This project uses the Mega SDK which has its own licensing terms. The custom application code is provided as-is for personal use.

---

*Last Updated: February 27, 2026*
*Version: 0.3.3*
