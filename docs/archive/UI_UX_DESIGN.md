# MegaCustom Qt GUI - UI/UX Documentation

## Application Overview

**MegaCustom** is a cloud storage management application for MEGA.nz with advanced file management, folder synchronization, batch uploading, and intelligent sync.

- **Target Resolution**: 1200 x 700 pixels (minimum)
- **Framework**: Qt6 (C++)
- **Theme**: MEGA Light (red accent on white)

---

## Main Window Layout

```
+------------------------------------------------------------------+
|  MenuBar (File, Edit, View, Tools, Help)                         |
+------------------------------------------------------------------+
|  TopToolbar (Navigation, Search, Actions, View Mode)             |
+-------------+----------------------------------------------------+
|             |                                                    |
| MegaSidebar |          Content Stack                             |
| (220-280px) |  +----------------------------------------------+  |
|             |  | Active Panel (Cloud Drive, FolderMapper,     |  |
| - Cloud     |  | MultiUploader, SmartSync)                    |  |
|   Drive     |  |                                              |  |
| - Tools     |  +----------------------------------------------+  |
| - Settings  |                                                    |
+-------------+----------------------------------------------------+
|  TransferQueue (Bottom - Active/Pending/Completed transfers)    |
+------------------------------------------------------------------+
|  StatusBar (User, Connection, Progress)                          |
+------------------------------------------------------------------+
```

---

## Color Scheme

| Element | Hex Code | Usage |
|---------|----------|-------|
| **MEGA Red** | #D90007 | Primary actions, selected states |
| **Red Hover** | #C00006 | Button hover |
| **Red Light** | #FFE6E7 | Selected backgrounds |
| **White** | #FFFFFF | Main background |
| **Light Gray** | #FAFAFA | Sidebar, alternate rows |
| **Medium Gray** | #E0E0E0 | Borders |
| **Text Primary** | #333333 | Main text |
| **Text Secondary** | #666666 | Dimmed text |
| **Disabled** | #AAAAAA | Disabled elements |

---

## Navigation Components

### 1. MegaSidebar (Left Panel)

**Width**: 220-280px

**Sections**:
```
+---------------------------+
| MEGA (Logo - 24pt bold)   |
+---------------------------+
| [Cloud icon] Cloud Drive  | <- Navigates to file browser
|   [Refresh button]        |
|   +-- Folder Tree --+     | <- Shows cloud folders
|   | Folder 1        |     |
|   | Folder 2        |     |
|   +------------------+    |
+---------------------------+
| TOOLS (Section header)    |
| [Folder] Folder Mapper    | <- Maps local to cloud
| [Upload] Multi Uploader   | <- Batch uploads
| [Sync] Smart Sync         | <- Bidirectional sync
+---------------------------+
| [Transfer] Transfers      | <- View transfer queue
| [Gear] Settings           | <- App settings
+---------------------------+
```

**Button States**:
- Normal: Transparent, #444444 text
- Hover: #F0F0F0 background
- Selected: #FFE6E7 background, #D90007 text, bold

---

### 2. TopToolbar

**Height**: ~60px

**Layout** (left to right):
```
[<] [>] [^]  Cloud Drive > Folder1 > CurrentFolder    [Search...]    [Upload] [+Folder] [Delete] [Refresh]    [List] [Grid] [Detail]
 |   |   |              |                                   |              |                                        |
Back Fwd Up         Breadcrumb                           Search         Actions                                 View Mode
```

**Elements**:
- **Navigation**: Back (←), Forward (→), Up (↑) - 36x36px icon buttons
- **Breadcrumb**: Clickable path segments, current folder bold
- **Search**: Pill-shaped input, #F5F5F5 background
- **Actions**: Upload (red primary), New Folder, Delete, Refresh
- **View Mode**: List/Grid/Detail toggle buttons

---

## Core Panels

### 1. Cloud Drive Browser (FileExplorer)

**Purpose**: Browse and manage cloud files

**Layout**: Split View (Tree + Content)
```
+------------------+------------------------+
| Tree View        | Content View           |
| (Navigation)     | (Grid/List/Detail)     |
+------------------+------------------------+
| ▼ home           | file1.txt              |
|   ▼ user         | file2.txt              |
|     ► projects   | subfolder/             |
|     ► Documents  | image.png              |
|     ► Downloads  |                        |
+------------------+------------------------+
```

**Split View Behavior**:
- **Tree (left ~200px)**: Shows expandable folder hierarchy
- **Content (right)**: Shows contents of selected folder
- Click folder in tree → Content view updates to show that folder's contents
- Tree always visible for navigation

**View Modes** (Content View only):

| Mode | Display |
|------|---------|
| List | Single column with icons |
| Grid | Thumbnail icons in grid |
| Detail | Table with Name, Size, Modified columns |

**Empty State**:
When folder is empty, show centered:
- Cloud icon (64px)
- "No files yet" heading
- "Drag files here or click Upload..." description
- Red "Upload" button

**Interactions**:
- Single-click folder in tree → Navigate (update content view)
- Double-click folder in content → Navigate into
- Double-click file → Download/open
- Right-click → Context menu (Copy, Cut, Paste, Delete, Rename)
- Drag & drop → Upload files

---

### 2. Folder Mapper Panel

**Purpose**: Automatically upload local folders to cloud on schedule

**Layout**:
```
+------------------------------------------+
| FOLDER MAPPER - VPS-to-MEGA Upload Tool  |
+------------------------------------------+
| [Add] [Update] [Remove] [Edit] [Refresh] |
+------------------------------------------+
| Name:        [________________]          |
| Local Path:  [________] [Browse...]      |
| Remote Path: [________] [Browse...]      |
+------------------------------------------+
| MAPPINGS TABLE                           |
| Name | Local Path | Remote Path | Status |
|------|------------|-------------|--------|
| Logs | /var/log   | /Logs       | Ready  |
| Data | /data      | /Backup     | Ready  |
+------------------------------------------+
| [Preview] [Upload All] [Refresh]         |
+------------------------------------------+
| Progress: [=========>        ] 45%       |
| Current: processing_file.log             |
| Files: 45/100 | Speed: 2.5 MB/s          |
+------------------------------------------+
| Settings:                                |
| [x] Incremental (only new/modified)      |
| [x] Recursive (include subfolders)       |
| Concurrent uploads: [3]                  |
| Exclude: [*.tmp, *.temp]                 |
+------------------------------------------+
```

---

### 3. Multi Uploader Panel

**Purpose**: Upload files to multiple destinations with distribution rules

**Layout**:
```
+------------------------------------------+
| MULTI UPLOADER                           |
+------------------------------------------+
| SOURCE FILES                             |
| - document.pdf                           |
| - image.jpg                              |
| [+ Add Files] [+ Add Folder] [Clear]     |
| Total: 2 files | 5.5 MB                  |
+------------------------------------------+
| DESTINATIONS                             |
| - /Cloud/Projects                        |
| - /Cloud/Archive                         |
| [+ Add] [- Remove]                       |
+------------------------------------------+
| DISTRIBUTION RULES                       |
| Rule Type: [By File Type v]              |
| Pattern     | Destination                |
| *.pdf       | /Cloud/Documents           |
| *.jpg       | /Cloud/Media               |
| [+ Add Rule] [- Remove Rule]             |
+------------------------------------------+
| UPLOAD TASKS                             |
| File      | Destination   | Status       |
| doc.pdf   | /Documents    | Completed    |
| image.jpg | /Media        | Uploading    |
| [Start] [Pause] [Cancel]                 |
+------------------------------------------+
```

---

### 4. Smart Sync Panel

**Purpose**: Bidirectional folder synchronization with conflict resolution

**Layout**:
```
+------------------------------------------+
| SMART SYNC                               |
+------------------------------------------+
| PROFILES                                 |
| Name     | Local Path  | Remote Path    |
| Projects | ~/Documents | /Cloud/Docs    |
| [+ New] [Edit] [Delete]                  |
+------------------------------------------+
| CONFIGURATION                            |
| Direction: [Bidirectional v]             |
| Conflict: [Keep Newer v]                 |
| Include: [*.txt, *.doc]                  |
| Exclude: [*.tmp, .git]                   |
| [x] Auto-sync  Interval: [60] min        |
+------------------------------------------+
| [Analyze] [Start Sync] [Pause] [Stop]    |
+------------------------------------------+
| TABS: [Preview] [Conflicts] [Progress]   |
| +--------------------------------------+ |
| | File       | Action  | Status       | |
| | doc.pdf    | Upload  | Pending      | |
| | image.jpg  | Download| Completed    | |
| +--------------------------------------+ |
| Progress: [========>         ] 45%       |
+------------------------------------------+
```

**Sync Directions**:
- Bidirectional
- Local to Remote only
- Remote to Local only

**Conflict Resolution**:
- Ask User
- Keep Newer
- Keep Larger
- Keep Local
- Keep Remote
- Keep Both (rename)

---

### 5. Transfer Queue (Bottom Panel)

**Purpose**: Show active/pending/completed transfers

```
+----------------------------------------------------------+
| TRANSFERS (5 Active | 8 Pending | 42 Completed)          |
+----------------------------------------------------------+
| File           | Type     | Progress        | Speed      |
| project.zip    | Download | [=====>    ] 50%| 2.5 MB/s   |
| photo.jpg      | Upload   | [=======>  ] 75%| 1.8 MB/s   |
| backup.tar.gz  | Upload   | [==========]Done| -          |
+----------------------------------------------------------+
| [Cancel All] [Clear Completed]                           |
+----------------------------------------------------------+
```

---

## Dialogs

### Login Dialog
```
+---------------------------+
| MegaCustom Login          |
+---------------------------+
|        [MEGA Logo]        |
|                           |
| Email:    [____________]  |
| Password: [____________]  |
|                           |
| [x] Remember me           |
|                           |
| [Login]  [Cancel]         |
+---------------------------+
```

### Settings Dialog (Tabs)

**General Tab**:
- Start at login
- Show tray icon
- Dark mode toggle
- Language selector

**Sync Tab**:
- Sync profiles management
- Auto-sync interval
- Conflict resolution default

**Advanced Tab**:
- Speed limits (up/down)
- Parallel transfers (1-8)
- Exclude patterns
- Cache settings
- Logging level

---

## UI Element Specifications

### Buttons

| Type | Background | Text | Border |
|------|------------|------|--------|
| Standard | #FFFFFF | #333333 | 1px #E0E0E0 |
| Primary | #D90007 | #FFFFFF | none |
| Icon | transparent | #555555 | none |
| Disabled | #FAFAFA | #AAAAAA | 1px #E8E8E8 |

**Sizing**: Min height 32px, padding 8px 16px, border-radius 6px

### Inputs

| State | Background | Border |
|-------|------------|--------|
| Normal | #FFFFFF | 1px #E0E0E0 |
| Focus | #FFFFFF | 2px #D90007 |
| Disabled | #F8F8F8 | 1px #E8E8E8 |

### Checkboxes

- Size: 18x18px
- Unchecked: White with #CCCCCC border
- Checked: #D90007 background with white checkmark

### Tables

- Header: #FAFAFA background, bold text
- Rows: Alternating white/#FAFAFA
- Selected: #FFE6E7 background
- Grid lines: #E8E8E8

### Progress Bars

- Background: #E8E8E8
- Fill: #D90007
- Height: 8px
- Border-radius: 4px

---

## Typography

| Element | Size | Weight |
|---------|------|--------|
| Logo | 24px | Bold |
| Section Header | 10px | Bold |
| Body Text | 13px | Normal |
| Button Text | 13px | 500 |
| Primary Button | 13px | 600 |

**Font Family**: "Segoe UI", Arial, sans-serif

---

## User Flows

### 1. Login Flow
1. App launches → Login dialog
2. Enter email/password
3. Optional: 2FA code entry
4. Success → Main window loads with Cloud Drive

### 2. Browse Cloud Files
1. Click Cloud Drive in sidebar
2. Double-click folders to navigate
3. Breadcrumb updates
4. Use search to filter
5. Right-click for context menu

### 3. Set Up Folder Mapping
1. Click Folder Mapper
2. Enter name, local path, remote path
3. Click Add
4. Configure settings (incremental, exclude patterns)
5. Click Preview → Review files
6. Click Upload All → Monitor progress

### 4. Create Smart Sync
1. Click Smart Sync
2. Click New Profile
3. Configure paths and direction
4. Set conflict resolution
5. Click Analyze → Preview changes
6. Click Start Sync → Monitor progress

### 5. Upload Files
1. Drag files to Cloud Drive area, OR
2. Use Multi Uploader with distribution rules
3. Monitor in Transfer Queue
4. Clear completed when done

---

## Notes for Designer

1. **Primary Action Color**: Always use MEGA Red (#D90007)
2. **Selection Highlighting**: Use light red (#FFE6E7)
3. **Clean Minimal Look**: Reduce borders, use spacing
4. **Icon Style**: Simple line icons or Unicode symbols
5. **Responsive**: Sidebar and content should resize gracefully
6. **Accessibility**: Ensure sufficient contrast ratios

---

## Technical Implementation Notes

### FileExplorer Split View Implementation

The split view uses QSplitter with tree navigation on left and content view on right.

**Key Pattern**: Tree selection drives content view
```cpp
// Tree shows folder hierarchy
m_treeView->setRootIndex(m_localModel->index(QDir::homePath()));

// When tree selection changes, update content view
connect(m_treeView->selectionModel(), &QItemSelectionModel::currentChanged,
        [this](const QModelIndex& current) {
    m_listView->setRootIndex(current);  // Show contents of selected folder
    m_currentPath = m_localModel->filePath(current);
    updateStatus();
});
```

**Empty State with QStackedWidget**:
```cpp
// Page 0: List/Grid view (when folder has contents)
// Page 1: Empty state widget (when folder is empty)
m_contentStack->setCurrentIndex(isEmpty ? 1 : 0);
```

### SettingsDialog Sidebar Navigation

Uses QStackedWidget with custom navigation buttons instead of QTabWidget.

**Structure**:
```
QSplitter (horizontal)
├── QFrame (navigation, 180-220px)
│   └── QVBoxLayout
│       ├── NavButton "General" (checkable)
│       ├── NavButton "Sync"
│       ├── NavButton "Network"
│       └── NavButton "Advanced"
└── QStackedWidget (content pages)
    ├── Page 0: General settings
    ├── Page 1: Sync settings
    ├── Page 2: Network settings
    └── Page 3: Advanced settings
```

**Selection styling**:
```css
QPushButton:checked {
    background-color: #FFE6E7;
    color: #D90007;
    font-weight: bold;
}
```

### Transfer Queue Badges

Status badges using QLabel with custom styling:
```cpp
QLabel* badge = new QLabel("5 Active", this);
badge->setStyleSheet(
    "background-color: #D90007; color: white; "
    "border-radius: 10px; padding: 4px 12px; "
    "font-size: 12px; font-weight: bold;"
);
```

### SmartSync Action Badges

Color-coded action indicators:
```cpp
if (action == "Upload") color = "#D90007";      // Red
else if (action == "Download") color = "#0066CC"; // Blue
else color = "#999999";                          // Gray (Skip)
```

---

## Current Issues (December 4, 2025)

### CRITICAL: FileExplorer Split View
**Problem**: Tree and list views show identical content
**Expected**: Tree = folder hierarchy, List = contents of selected folder
**Status**: Documented, fix identified, implementation pending

See `TODO.md` for detailed fix instructions.

---

*Last Updated: December 4, 2025*
