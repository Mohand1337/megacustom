# MegaCustom

A powerful desktop application for MEGA cloud storage with multi-account support, cross-account transfers, and advanced file management.

## Quick Start

### GUI Application
```bash
cd qt-gui/build-qt
cmake .. && make -j4
./MegaCustomGUI
```

### CLI Tool
```bash
make clean && make
./megacustom auth login EMAIL PASSWORD
./megacustom folder list /
```

## Key Features

- **Multi-Account Support** - Manage multiple MEGA accounts with instant switching (Ctrl+Tab)
- **Cross-Account Transfers** - Copy/move files between accounts
- **File Explorer** - Modern dual-pane file browser with drag-and-drop
- **Folder Mapper** - Map local folders to MEGA for automated uploads
- **Multi Uploader** - Upload to multiple destinations with distribution rules
- **Smart Sync** - Bidirectional synchronization with conflict resolution
- **Cloud Copier** - Copy files between MEGA folders/accounts
- **Everything-like Search** - Instant search across millions of files (<50ms)

## Project Statistics

| Metric | Value |
|--------|-------|
| Total Lines of Code | ~52,643 |
| Qt GUI Source Files | 123 (61 .cpp, 62 .h) |
| Controllers | 7 |
| Dialogs | 14 |
| Widgets | 23 |
| CLI Modules | 11 |

## Documentation

| Document | Description |
|----------|-------------|
| [**USER_GUIDE.md**](docs/USER_GUIDE.md) | Complete user documentation - features, usage, troubleshooting |
| [**DEVELOPER_GUIDE.md**](docs/DEVELOPER_GUIDE.md) | Architecture, building, APIs, contributing |
| [docs/archive/](docs/archive/) | Historical documentation and changelogs |

## Requirements

- Qt 6.4+ with Widgets, Network, Concurrent, Sql
- CMake 3.16+
- C++17 compiler
- MEGA SDK (included in third_party/)

## Project Structure

```
mega-custom-app/
├── qt-gui/                 # Qt6 GUI Application
│   ├── build-qt/           # Build output (MegaCustomGUI)
│   └── src/
│       ├── main/           # Application, MainWindow
│       ├── widgets/        # 23 widget classes
│       ├── dialogs/        # 14 dialog classes
│       ├── controllers/    # 7 controller classes
│       ├── accounts/       # Multi-account system
│       └── search/         # Search indexing
├── src/                    # CLI source
│   ├── core/               # MegaManager, Auth, Config
│   ├── operations/         # File/Folder operations
│   └── features/           # FolderMapper, SmartSync, etc.
├── include/                # CLI headers
├── docs/                   # Documentation
│   ├── USER_GUIDE.md
│   ├── DEVELOPER_GUIDE.md
│   └── archive/            # Historical docs
└── third_party/sdk/        # MEGA SDK
```

## Version History

| Version | Date | Highlights |
|---------|------|------------|
| 0.3.0 | Planned | Member management, watermarking, distribution pipeline |
| 0.2.0 | Dec 8-10, 2025 | Multi-account support, cross-account transfers, search |
| 0.1.0 | Dec 3-6, 2025 | Initial GUI, all panels, scheduler |

---

## Upcoming Features (v0.3.0)

### Member Distribution System
- **Member Management** - Database with MEGA folder bindings per member
- **Video Watermarking** - FFmpeg-based personalized watermarks
- **PDF Watermarking** - Python-based document watermarking
- **WordPress Integration** - Sync member data via REST API
- **Distribution Pipeline** - Select files → Pick members → Watermark → Upload
- **Activity Logging** - Complete audit trail of distributions

### New Panels
- **Members** - Manage members with WP sync
- **Distribution** - Integrated watermark + upload pipeline
- **Watermark** - Standalone watermarking tool
- **Logs** - Activity log + Distribution history

See [DEVELOPER_GUIDE.md](docs/DEVELOPER_GUIDE.md#phase-2-roadmap) for technical details.

---

*Session 20 - December 10, 2025*
