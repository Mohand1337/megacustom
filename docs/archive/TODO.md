# MegaCustom - TODO / Remaining Work

*Created: December 3, 2025 - Updated: December 10, 2025 (Session 19)*

---

## Recently Completed (Session 18+ - December 7-8, 2025)

### Multi-Account Support - COMPLETE
| Task | Status |
|------|--------|
| AccountModels.h (data structures) | DONE |
| CredentialStore (encrypted sessions) | DONE |
| SessionPool (multi-MegaApi caching) | DONE |
| AccountManager (central coordinator) | DONE |
| TransferLogStore (SQLite history) | DONE |
| CrossAccountTransferManager | DONE |
| AccountSwitcherWidget | DONE |
| QuickPeekPanel | DONE |
| CrossAccountLogPanel | DONE |
| AccountManagerDialog | DONE |
| MainWindow integration | DONE |
| Keyboard shortcuts (Ctrl+Tab cycling) | DONE |
| Transfer completion notifications | DONE |

### Bug Fixes Applied
| Bug | Fix |
|-----|-----|
| Sidebar buttons greyed out after login | Call `setLoggedIn(true)` on account switch |
| App crash on startup | Defer initial account check with `QTimer::singleShot(0,...)` |
| "Invalid argument" on cross-account copy | Change `exportNode()` expireTime from -1 to 0 |
| No completion notification | Connect `transferCompleted`/`transferFailed` signals to MainWindow |

---

## CRITICAL - FileExplorer Split View Bug

### Issue Description
The FileExplorer split view shows **identical content** in both tree and list views instead of:
- **Tree (left)**: Folder hierarchy for NAVIGATION
- **List/Grid (right)**: CONTENTS of the selected folder in tree

### Current Behavior
```
+------------------+------------------+
| Tree View        | List View        |
| - projects       | - projects       |  <- SAME CONTENT (WRONG)
+------------------+------------------+
```

### Expected Behavior
```
+------------------+------------------+
| Tree View        | List View        |
| ▼ home           | file1.txt        |  <- Tree = hierarchy
|   ▼ user         | file2.txt        |     List = contents of
|     ► projects   | subfolder/       |     selected folder
|     ► Documents  |                  |
+------------------+------------------+
```

### Root Cause (FileExplorer.cpp)
```cpp
// CURRENT - Both views get same root index
m_treeView->setRootIndex(index);
m_listView->setRootIndex(index);
```

### Required Fix
```cpp
// In setupUI() or initializeModel():

// 1. Tree shows full hierarchy from home path
m_treeView->setRootIndex(m_localModel->index(QDir::homePath()));

// 2. Connect tree selection to drive list view content
connect(m_treeView->selectionModel(), &QItemSelectionModel::currentChanged,
        this, &FileExplorer::onTreeSelectionChanged);

// 3. New slot to handle selection:
void FileExplorer::onTreeSelectionChanged(const QModelIndex& current, const QModelIndex&) {
    if (!current.isValid()) return;

    // Update list view to show contents of selected folder
    m_listView->setRootIndex(current);

    // Update current path
    m_currentPath = m_localModel->filePath(current);

    // Update file counts and status
    updateFileCount();
    updateStatus();

    emit pathChanged(m_currentPath);
}
```

### Files to Modify
| File | Changes Needed |
|------|----------------|
| `FileExplorer.h` | Add `onTreeSelectionChanged()` slot declaration |
| `FileExplorer.cpp` | Implement tree-drives-list navigation pattern |

---

## Recently Completed (Session 13 - December 4, 2025)

### Design Mockup Compliance - 9/10 DONE

| # | Task | Status |
|---|------|--------|
| 1 | TransferQueue progress bar colors (RED) | DONE |
| 2 | TransferQueue status badges | DONE |
| 3 | TransferQueue active row highlighting | DONE |
| 4 | LoginDialog subtitle text fix | DONE |
| 5 | MegaSidebar SYSTEM label | DONE |
| 6 | TopToolbar folder icon in breadcrumb | DONE |
| 7 | FileExplorer split view | PARTIAL - needs tree navigation fix |
| 8 | FileExplorer empty state (QStackedWidget) | DONE |
| 9 | SmartSyncPanel action badges | DONE |
| 10 | SettingsDialog sidebar navigation | DONE |

---

## Code TODOs (Found in Source)

These are actual TODO comments found in the codebase that need implementation:

### 1. CLI Rename Operations
- **File**: `src/main.cpp:1247`
- **TODO**: Implement rename operations
- **Status**: Placeholder code exists, needs full implementation

### 2. CLI Configuration Operations
- **File**: `src/main.cpp:1562`
- **TODO**: Implement configuration operations
- **Status**: Placeholder code exists, needs full implementation

### 3. Settings Cache Clearing
- **File**: `qt-gui/src/dialogs/SettingsDialog.cpp:597`
- **TODO**: Actually clear cache (currently just a button)
- **Status**: UI exists, backend implementation needed

### 4. QuickPeek Download Functionality
- **File**: `qt-gui/src/widgets/QuickPeekPanel.cpp:419`
- **TODO**: Implement download using the peeked account's MegaApi
- **Status**: UI shows files, download button needs implementation

### 5. SmartSync Schedule Dialog
- **File**: `qt-gui/src/widgets/SmartSyncPanel.cpp:505`
- **TODO**: Show schedule dialog (ScheduleSyncDialog exists but not connected)
- **Status**: Dialog class created, needs connection to panel

### 6. AdvancedSearch Rename Files
- **File**: `qt-gui/src/widgets/AdvancedSearchPanel.cpp:860`
- **TODO**: Actually rename files via FileController/MegaApi
- **Status**: UI shows rename option, backend call needed

### 7. AccountSwitcher Sync Status Check
- **File**: `qt-gui/src/widgets/AccountSwitcherWidget.cpp:524`
- **TODO**: Check if account is currently syncing before switch
- **Status**: Add sync status verification before allowing account switch

---

## High Priority (User Experience)

### 1. Remote Folder Browser Enhancement
- [x] Create `RemoteFolderBrowserDialog` class (EXISTS)
- [ ] Improve tree-view UX for MEGA cloud folders
- [ ] Add folder creation directly in browser
- [ ] Replace remaining QInputDialog instances

### 2. Input Validation
- [ ] Validate local paths exist before adding mapping
- [ ] Validate mapping names (no duplicates, no special chars)
- [ ] Validate remote paths format (must start with /)
- [ ] Show validation errors inline in forms

### 3. Error Handling UI
- [ ] Create standardized error dialog with details
- [ ] Add retry options for failed operations
- [ ] Improve error messages (user-friendly)
- [ ] Log errors for debugging

---

## Medium Priority (Feature Testing)

### 4. Test MultiUploader Panel
- [ ] Test adding multiple source files
- [ ] Test adding multiple destinations
- [ ] Test distribution rules (by extension, size, name)
- [ ] Test task queue (pause, resume, cancel)
- [ ] Verify progress tracking

### 5. Test SmartSync Panel
- [ ] Test creating sync profiles
- [ ] Test bidirectional sync
- [ ] Test conflict detection and resolution
- [ ] Test include/exclude patterns
- [ ] Verify auto-sync functionality

### 6. Test Scheduler
- [ ] Test scheduling folder mapping uploads
- [ ] Test hourly/daily/weekly schedules
- [ ] Test one-time scheduled tasks
- [ ] Verify task persistence (JSON)
- [ ] Test scheduler with app restart

---

## Low Priority (Polish)

### 7. Keyboard Shortcuts
- [ ] Ctrl+N - New mapping/profile
- [ ] Ctrl+E - Edit selected
- [ ] Delete - Remove selected
- [ ] Ctrl+R - Refresh
- [ ] F5 - Sync/Upload

### 8. Tooltips
- [ ] Add tooltips to all buttons
- [ ] Add tooltips to table columns
- [ ] Add tooltips to settings options

### 9. Dark Theme
- [ ] Complete dark theme QSS
- [ ] Test all dialogs in dark mode
- [ ] Add theme toggle in settings
- [ ] Remember theme preference

---

## Known Issues

### Critical
1. **FileExplorer Split View** - Tree and list show same content (documented above)

### Medium
2. **Upload segfault on shutdown** - Upload completes but app crashes on exit
3. **Upload sync delay** - New files may not appear immediately
4. **Remote folder browser** - Currently uses QInputDialog (needs tree dialog)

### Minor
5. **Settings not in sidebar** - Check if Settings button visible in SYSTEM section

### Resolved (December 8, 2025)
- ~~Cross-account transfer "Invalid argument"~~ - Fixed expireTime parameter
- ~~No completion notification~~ - Added message box feedback
- ~~Sidebar buttons not enabled after login~~ - Fixed onAccountSwitched()

---

## Files Modified in Session 13

| File | Changes |
|------|---------|
| `TransferQueue.cpp` | Added badges, row highlighting, fixed colors |
| `TransferQueue.h` | Added badge members and helper functions |
| `LoginDialog.cpp` | Fixed subtitle text |
| `MegaSidebar.cpp` | Added SYSTEM section label |
| `TopToolbar.cpp` | Added folder icon before breadcrumb |
| `FileExplorer.cpp` | Added QStackedWidget, fixed file counting |
| `FileExplorer.h` | Added m_contentStack member |
| `SmartSyncPanel.cpp` | Added createActionBadge() function |
| `SmartSyncPanel.h` | Added createActionBadge() declaration |
| `SettingsDialog.cpp` | Complete refactor to sidebar navigation |
| `SettingsDialog.h` | Added navigation members, new page methods |

---

## Quick Reference

### Build Commands
```bash
# GUI
cd "/home/mow/projects/Mega - SDK/mega-custom-app/qt-gui/build-qt"
cmake .. && make -j4
MEGA_API_KEY="9gETCbhB" ./MegaCustomGUI
```

### Design Mockups Location
```
/home/mow/projects/Mega - SDK/mega-custom-app/Design mockups visuals/
├── Full Application Overview.jpg
├── UI Component Sheet.jpg
├── Login Dialog.jpg
├── Settings Dialog.jpg
├── Transfer Queue Panel.jpg
├── Smart Sync Panel.jpg
└── Empty States.jpg
```

### Color Palette
| Color | Hex | Usage |
|-------|-----|-------|
| MEGA Red | #D90007 | Primary actions, buttons |
| Selection Pink | #FFE6E7 | Selected items, active rows |
| Download Blue | #0066CC | Download badges |
| Text Primary | #333333 | Main text |
| Border Gray | #E0E0E0 | Borders, dividers |

---

## Files Modified in Session 18+ (December 7-8, 2025)

### New Files Created
| File | Description |
|------|-------------|
| `qt-gui/src/accounts/AccountModels.h` | MegaAccount, AccountGroup, CrossAccountTransfer structs |
| `qt-gui/src/accounts/AccountManager.h/cpp` | Central account coordinator singleton |
| `qt-gui/src/accounts/CredentialStore.h/cpp` | Encrypted session storage |
| `qt-gui/src/accounts/SessionPool.h/cpp` | Multi-MegaApi caching with LRU eviction |
| `qt-gui/src/accounts/TransferLogStore.h/cpp` | SQLite transfer history |
| `qt-gui/src/accounts/CrossAccountTransferManager.h/cpp` | Copy/move via public links |
| `qt-gui/src/widgets/AccountSwitcherWidget.h/cpp` | Sidebar account list |
| `qt-gui/src/widgets/QuickPeekPanel.h/cpp` | Browse other accounts |
| `qt-gui/src/widgets/CrossAccountLogPanel.h/cpp` | Transfer history view |
| `qt-gui/src/dialogs/AccountManagerDialog.h/cpp` | Account management UI |

### Files Modified
| File | Changes |
|------|---------|
| `qt-gui/src/main/MainWindow.h` | Added account slots, include AccountModels.h |
| `qt-gui/src/main/MainWindow.cpp` | Account switching, transfer signals, shortcuts |
| `qt-gui/CMakeLists.txt` | Added all account-related source files |

---

## Summary

### Completed (Session 18)
- Multi-Account Support (100% - All 6 components)
- Cross-Account Transfers (Copy/Move working)
- Account Management UI (AccountManagerDialog)
- Quick Peek & Transfer History

### High Priority Remaining
- 7 code TODOs (implementation needed)
- FileExplorer split view fix (critical UX issue)
- Input validation improvements
- Error handling standardization

### Medium Priority
- Feature testing (MultiUploader, SmartSync, Scheduler)
- Remote folder browser enhancements

### Low Priority
- Keyboard shortcuts expansion
- Tooltips for all UI elements
- Dark theme completion

---

*Last Updated: December 10, 2025 - Session 19*
*Status: Core features 100% complete, polish & testing remain*
