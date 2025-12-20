# MegaCustom User Guide

**Version 1.0** | **Last Updated: December 10, 2025**

A comprehensive guide to using MegaCustom - your powerful MEGA.nz cloud storage management application with advanced features for file management, synchronization, and multi-account support.

---

## Table of Contents

1. [Getting Started](#getting-started)
2. [Features Overview](#features-overview)
3. [Multi-Account Support](#multi-account-support)
4. [Search](#search)
5. [Settings](#settings)
6. [Keyboard Shortcuts](#keyboard-shortcuts)
7. [Troubleshooting](#troubleshooting)

---

## Getting Started

### Installation and Building

#### Prerequisites
- **Operating System**: Windows 10/11, macOS, or Linux
- **Qt6.5+** (for GUI)
- **C++17 compiler** (GCC 7+, Clang 5+)
- **CMake 3.16+**
- **MEGA API Key** (Required)

#### Building the Application

**GUI Version:**
```bash
cd /home/mow/projects/Mega\ -\ SDK/mega-custom-app/qt-gui/build-qt
cmake .. && make -j4
```

**CLI Version:**
```bash
cd /home/mow/projects/Mega\ -\ SDK/mega-custom-app
make clean && make
```

#### Setting Your API Key

Before running the application, you must set your MEGA API key:

```bash
export MEGA_APP_KEY="9gETCbhB"
# or
export MEGA_API_KEY="9gETCbhB"
```

For permanent setup, add this to your shell configuration file (`~/.bashrc`, `~/.zshrc`, etc.).

### First Launch

1. **Launch the Application**
   ```bash
   cd qt-gui/build-qt
   ./MegaCustomGUI
   ```

2. **Login Dialog Appears**
   - The login dialog will appear automatically on first launch
   - Enter your MEGA email and password
   - Check "Remember me" to save your session
   - Click "Login"

3. **Two-Factor Authentication (2FA)**
   - If you have 2FA enabled, you'll be prompted for your code
   - Enter the 6-digit code from your authenticator app
   - Optional: Check "Trust this device" to skip 2FA on future logins

4. **Main Window Opens**
   - After successful login, the main window opens showing Cloud Drive
   - You'll see your MEGA folder structure in the left sidebar
   - The main area displays your files and folders

### Login and Authentication

#### Session Management
- **Automatic Login**: If you checked "Remember me", you'll be logged in automatically on next launch
- **Session Persistence**: Your session is saved securely across app restarts
- **Session Timeout**: Configurable in Settings (default: no timeout)
- **Logout**: File menu → Logout to clear credentials

#### Multiple Sessions
MegaCustom supports managing multiple MEGA accounts simultaneously:
- Add accounts via Tools → Account Manager
- Switch between accounts using `Ctrl+Tab`
- Each account maintains its own session and cache

---

## Features Overview

### 1. File Explorer (Cloud Drive)

The File Explorer is your main interface for browsing and managing files in your MEGA cloud storage.

#### Interface Layout
```
+-------------------+---------------------------+
| Folder Tree       | File List (Grid/List)     |
| (Navigation)      | (Contents)                |
+-------------------+---------------------------+
| ▼ Cloud Drive     | file1.txt                 |
|   ▼ Documents     | file2.pdf                 |
|     ► Work        | folder1/                  |
|     ► Personal    | image.jpg                 |
+-------------------+---------------------------+
```

#### Key Features

**Navigation**
- **Breadcrumb Bar**: Shows current path (e.g., Cloud Drive > Documents > Work)
- **Back/Forward Buttons**: Navigate through your browsing history (← →)
- **Up Button**: Go to parent folder (↑)
- **Double-click folders**: Navigate into folders
- **Tree view**: Click folders in left panel to navigate

**View Modes**
- **List View** (☰): Single column with file icons and names
- **Grid View** (▦): Thumbnail grid layout
- **Detail View** (☷): Table with Name, Size, Modified Date columns

**File Operations**
- **Upload**: Drag & drop files from your desktop, or use Upload button (Ctrl+U)
- **Download**: Right-click → Download, or press Ctrl+D
- **New Folder**: Click "+ Folder" button or press Ctrl+Shift+N
- **Rename**: Right-click → Rename, or select and press F2
- **Delete**: Right-click → Delete, or press Delete key
- **Copy/Cut/Paste**: Standard Ctrl+C, Ctrl+X, Ctrl+V operations

**Selection**
- **Single Select**: Click on a file or folder
- **Multi-Select**:
  - Hold Ctrl and click to select multiple individual items
  - Hold Shift and click to select a range
  - Ctrl+A to select all items

**Empty State**
When a folder is empty, you'll see:
- Cloud icon
- "No files yet" message
- "Upload" button to add files

---

### 2. Folder Mapper (VPS-to-MEGA Upload Tool)

Folder Mapper allows you to define mappings between local folders (on your VPS or computer) and MEGA cloud folders, then upload them with a single click.

#### Use Cases
- Backup server logs to MEGA regularly
- Sync website files to cloud backup
- Upload photo collections to organized cloud folders
- Incremental backups (only upload new/changed files)

#### Creating a Mapping

1. **Click "Folder Mapper"** in the left sidebar
2. **Enter Mapping Details**:
   - **Name**: A friendly name (e.g., "Server Logs", "Website Backup")
   - **Local Path**: Browse to your local folder (e.g., `/var/www/mysite`)
   - **Remote Path**: Browse or type MEGA destination (e.g., `/Backups/Website`)
3. **Click "Add"** to save the mapping

#### Managing Mappings

**Edit a Mapping**
1. Select the mapping in the table
2. Click "Edit"
3. Modify details and click "Update"

**Remove a Mapping**
1. Select the mapping
2. Click "Remove"
3. Confirm deletion

**Refresh List**
- Click "Refresh" to reload mappings from configuration

#### Uploading Files

**Upload Selected Mapping**
1. Select one or more mappings in the table
2. Click "Upload Selected"
3. Monitor progress in the progress section

**Upload All Mappings**
1. Click "Upload All"
2. All mappings will be uploaded sequentially
3. Progress is shown for each mapping

**Preview Mode (Dry Run)**
1. Click "Preview" before uploading
2. See which files will be uploaded without actually uploading
3. Review the list to ensure correctness

#### Settings

**Incremental Upload**
- ☑ **Enabled**: Only upload new or modified files (faster)
- ☐ **Disabled**: Upload all files every time

**Recursive Upload**
- ☑ **Enabled**: Include all subfolders
- ☐ **Disabled**: Only upload files in the root folder

**Concurrent Uploads**
- Set number of simultaneous file uploads (1-8)
- Higher = faster, but uses more bandwidth
- Default: 3

**Exclude Patterns**
- Enter patterns to skip certain files
- Example: `*.tmp, *.temp, .git, node_modules`
- Comma-separated list

#### Progress Tracking

During upload, you'll see:
- **Overall Progress Bar**: Percentage complete
- **Current File**: Name of file being uploaded
- **File Count**: "45/100 files uploaded"
- **Upload Speed**: "2.5 MB/s"
- **Status**: Ready, Uploading, Completed, Error

---

### 3. Multi Uploader (Multi-Destination Upload)

Upload files to multiple MEGA destinations simultaneously with intelligent distribution rules.

#### Use Cases
- Organize files by type (PDFs to Documents, images to Photos)
- Distribute files by size (large files to Archive, small to Active)
- Upload to multiple project folders at once
- Apply complex routing rules based on file attributes

#### How to Use

**Step 1: Add Source Files**
1. Click "+ Add Files" to select individual files
2. Click "+ Add Folder" to select entire folders
3. Files appear in the "Source Files" list
4. See total file count and size

**Step 2: Add Destinations**
1. Click "+ Add" under Destinations
2. Browse or type MEGA folder path
3. Add multiple destinations as needed
4. Remove destinations with "- Remove" button

**Step 3: Configure Distribution Rules**

**Rule Types:**
- **By Extension**: Route files based on file type
  - Example: `*.pdf → /Documents`, `*.jpg → /Photos`
- **By Size**: Route based on file size
  - Example: `>10MB → /Archive`, `<10MB → /Active`
- **By Name**: Route based on filename pattern
  - Example: `project* → /Projects`, `backup* → /Backups`
- **Default**: Files that don't match any rule go here

**Adding Rules**
1. Click "+ Add Rule"
2. Select rule type
3. Enter pattern (e.g., `*.pdf`, `>5MB`, `report*`)
4. Select destination folder
5. Click "Add"

**Step 4: Start Upload**
1. Review tasks in the task queue
2. Click "Start" to begin uploading
3. Monitor progress in real-time

#### Upload Controls

- **Start**: Begin uploading queued tasks
- **Pause**: Temporarily pause all uploads
- **Resume**: Continue paused uploads
- **Cancel**: Stop and cancel current uploads
- **Clear Completed**: Remove finished tasks from queue

#### Task Queue

The task queue shows:
- **File Name**: Name of each file
- **Destination**: Where it will be uploaded
- **Status**: Pending, Uploading, Completed, Error
- **Progress**: Individual file progress bar

---

### 4. Smart Sync (Bidirectional Synchronization)

Keep local and MEGA cloud folders synchronized automatically with conflict resolution.

#### Use Cases
- Sync Documents folder between computer and cloud
- Keep project files synchronized across devices
- Automatic backup of important folders
- Team collaboration with shared folders

#### Creating a Sync Profile

1. **Click "Smart Sync"** in left sidebar
2. **Click "+ New"** to create a profile
3. **Configure Profile**:

**Basic Settings**
- **Profile Name**: Descriptive name (e.g., "Work Documents Sync")
- **Local Path**: Browse to local folder
- **Remote Path**: Browse to MEGA folder

**Sync Direction**
- **Bidirectional**: Sync changes both ways (Local ↔ Remote)
- **Local → Remote**: Only upload changes to cloud
- **Remote → Local**: Only download changes from cloud

**Conflict Resolution**
Choose what happens when the same file is modified in both locations:
- **Ask User**: Show dialog to choose manually (safest)
- **Keep Newer**: Keep the most recently modified file
- **Keep Older**: Keep the older version
- **Keep Larger**: Keep the larger file
- **Keep Local**: Always prefer local version
- **Keep Remote**: Always prefer cloud version
- **Keep Both**: Rename files to keep both versions

4. **Click "Save"** to create the profile

#### Advanced Filters

**Include Patterns**
- Only sync files matching these patterns
- Example: `*.txt, *.doc, *.pdf`
- Leave empty to sync all files

**Exclude Patterns**
- Skip files matching these patterns
- Example: `*.tmp, *.bak, .git, node_modules`
- Useful for temporary files and build artifacts

#### Running a Sync

**Manual Sync**
1. Select a profile from the list
2. Click "Analyze" to preview changes (recommended)
3. Review the preview in the "Preview" tab
4. Click "Start Sync" to execute

**Auto-Sync**
1. Check ☑ "Auto-sync" in profile settings
2. Set interval in minutes (e.g., 60 for hourly)
3. Sync will run automatically in the background

#### Monitoring Sync Progress

**Preview Tab**
- Shows files that will be uploaded/downloaded
- Action column: Upload, Download, Skip
- Review before syncing

**Conflicts Tab**
- Lists files with conflicts
- Shows both versions
- Resolve manually if conflict resolution is "Ask User"

**Progress Tab**
- Real-time sync progress
- Current file being transferred
- Speed and ETA
- Overall percentage

**History Tab**
- Past sync operations
- Timestamp and status
- Files transferred count
- Errors and warnings

---

### 5. Cloud Copier (Cloud-to-Cloud Copy)

Copy or move files between different locations within your MEGA cloud storage without downloading them.

#### Use Cases
- Reorganize cloud storage structure
- Duplicate files to multiple backup locations
- Move files between project folders
- Distribute files to team folders

#### How to Use

**Step 1: Add Source Files**
1. Enter source file/folder path(s)
2. Or paste multiple paths (one per line)
3. Click "Validate" to check paths exist

**Step 2: Add Destinations**
1. Enter destination folder path(s)
2. Or paste multiple destinations (one per line)
3. Paths are automatically normalized (leading `/` added)

**Step 3: Copy or Move**
1. Click "Copy" to duplicate files
2. Click "Move" to transfer files (deletes from source)

#### Path Validation

The purple "Validate" button checks:
- ✓ Path exists in your cloud storage
- ✓ Path is a folder (for destinations)
- ✗ Error messages if path is invalid

#### Conflict Resolution

If a file already exists at the destination:
- **Ask**: Show dialog to choose action
- **Skip**: Don't copy this file
- **Overwrite**: Replace existing file
- **Rename**: Keep both (rename the new file)
- **Apply to All**: Use same choice for all conflicts

#### Progress Tracking
- Progress bar for each file
- Overall percentage
- Current file being copied
- Speed (for large files)

---

## Multi-Account Support

MegaCustom supports managing multiple MEGA accounts simultaneously with seamless switching and cross-account transfers.

### Adding Accounts

1. **Open Account Manager**
   - Menu: Tools → Account Manager
   - Or press `Ctrl+Shift+A`

2. **Add New Account**
   - Click "Add Account"
   - Enter email and password
   - Enter 2FA code if required
   - Click "Login"

3. **Organize with Groups** (Optional)
   - Create groups (e.g., "Personal", "Work", "Family")
   - Assign accounts to groups
   - Groups appear in Account Switcher for organization

### Switching Between Accounts

**Method 1: Keyboard Shortcuts**
- `Ctrl+Tab`: Switch to next account
- `Ctrl+Shift+Tab`: Switch to previous account

**Method 2: Account Switcher Widget**
- Located in the sidebar header
- Click on account name dropdown
- Select desired account from list
- Groups are shown for easy organization

**Method 3: Account Manager Dialog**
- Press `Ctrl+Shift+A`
- Double-click an account to switch

**Visual Indicator**
- Active account is highlighted in MEGA Red (#D90007)
- Account name shown in sidebar and status bar

### Cross-Account Transfers

Transfer files between different MEGA accounts without downloading to your computer.

#### Copy to Another Account

1. **Select Files** in File Explorer
2. **Right-click** → "Copy to Account"
3. **Choose Destination Account** from dropdown
4. **Enter Destination Path** in the account (e.g., `/Shared Files`)
5. **Click "Copy"**

**What Happens:**
- Source file is made temporarily public (link created)
- File is imported into destination account
- Public link is revoked for security
- Transfer is logged in Transfer History

#### Move to Another Account

1. **Select Files** in File Explorer
2. **Right-click** → "Move to Account"
3. **Choose Destination Account**
4. **Enter Destination Path**
5. **Click "Move"**

**What Happens:**
- File is copied to destination account (as above)
- File is deleted from source account after successful copy
- Cannot be undone - use Copy if unsure

#### Transfer History

View all cross-account transfers:

1. **Open Transfer Log**
   - Menu: Tools → Cross-Account Transfer Log
   - Or press `Ctrl+Shift+L`

2. **Filter History**
   - By account (source or destination)
   - By date range
   - By status (Success, Failed, In Progress)

3. **View Details**
   - Transfer timestamp
   - Source and destination paths
   - File size
   - Transfer status
   - Error messages (if failed)

### Quick Peek

Browse files from other accounts without switching your active account.

1. **Open Quick Peek Panel**
   - Menu: Tools → Quick Peek

2. **Select Account to Browse**
   - Choose from dropdown list
   - Account is not switched

3. **Browse Files**
   - Navigate folders
   - View file details
   - Preview files
   - Download files (TODO: not yet implemented)

4. **Close Panel**
   - Your active account remains unchanged
   - Non-destructive browsing

### Account Session Management

**Credential Storage**
- Encrypted using AES-256
- Stored locally in `~/.config/MegaCustom/credentials.dat`
- Optional: OS keychain integration (if QtKeychain available)

**Session Pooling**
- Maximum 5 concurrent sessions
- Least Recently Used (LRU) eviction
- Automatic session cleanup
- Per-account cache directories

**Cache Locations**
- Linux: `~/.config/MegaCustom/mega_cache/{accountId}/`
- Windows: `%APPDATA%/MegaCustom/mega_cache/{accountId}/`
- macOS: `~/Library/Preferences/MegaCustom/mega_cache/{accountId}/`

**Session Recovery**
- Sessions persist across app restarts
- Auto-login for remembered accounts
- Failed sessions are removed automatically

---

## Search

MegaCustom features an "Everything-like" instant search system that can search millions of files in milliseconds.

### Basic Search

**Quick Search (in toolbar)**
1. Click search box in top toolbar
2. Type your search query
3. Results appear instantly as you type
4. Press Enter or click result to navigate

**Search Results Panel**
- File name and path
- File size and modified date
- Relevance score (0-100)
- Status: "Found 1,234 items in 12ms"

**Sorting Results**
- Click column headers to sort
- Sort by: Name, Size, Date, Path, Relevance
- Ascending/Descending order

### Advanced Search

For complex queries, use Advanced Search:

1. **Open Advanced Search**
   - Menu: Tools → Advanced Search
   - Or press `Ctrl+Shift+F`

2. **Use Search Operators**

#### Search Operators

**Extension Filter**
```
ext:pdf          # Find PDF files
ext:jpg,png      # Find JPG or PNG files
ext:.doc         # Leading dot is optional
```

**Size Filter**
```
size:>10MB       # Files larger than 10MB
size:<5MB        # Files smaller than 5MB
size:>1GB        # Files larger than 1GB
```

**Date Filter**
```
dm:>2025-01-01   # Modified after January 1, 2025
dm:<2024-12-31   # Modified before December 31, 2024
dm:2025-06       # Modified in June 2025
```

**Path Filter**
```
path:documents   # Path contains "documents"
path:work/2025   # Path contains "work/2025"
```

**Type Filter**
```
type:folder      # Only folders
type:file        # Only files
```

**Regex Filter**
```
regex:^report.*\.pdf$    # Files starting with "report" ending in .pdf
regex:backup_\d{4}       # Files matching "backup_" followed by 4 digits
```

**Wildcard Patterns**
```
*report*         # Contains "report" anywhere
project*.txt     # Starts with "project", ends with .txt
?.pdf            # Single character followed by .pdf
```

**Boolean Operators**
```
report AND 2025          # Must contain both "report" and "2025"
invoice OR receipt       # Contains either "invoice" or "receipt"
document NOT draft       # Contains "document" but not "draft"
```

**Combining Operators**
```
ext:pdf size:>5MB path:work dm:>2025-01-01
# PDFs larger than 5MB in "work" path modified after Jan 1, 2025

report* ext:pdf,docx dm:>2024-12-01
# Files starting with "report", PDF or DOCX, modified after Dec 1, 2024

type:folder path:backup dm:<2024-01-01
# Folders in "backup" path modified before 2024
```

### Search Index

**How it Works**
- Index is built when you first log in
- ~1-2 seconds per 100,000 files
- Updates incrementally (no full rebuilds)
- Searches execute in <50ms on 1M+ files

**Index Status**
- Status bar shows: "Index: Ready (1,234,567 files)"
- Or "Indexing... 45% (567,890 files)"

**Manual Rebuild**
- Usually not needed (auto-updates)
- Settings → Advanced → "Rebuild Search Index"
- Use if search results seem outdated

**Memory Usage**
- ~50-100 bytes per file indexed
- 100,000 files ≈ 5-10 MB RAM
- 1,000,000 files ≈ 50-100 MB RAM

---

## Settings

Access settings via File menu → Settings, or press `Ctrl+,`.

### General Settings

**Startup Options**
- ☑ **Start at system login**: Launch MegaCustom when you log in to your computer
- ☑ **Show system tray icon**: Minimize to system tray instead of taskbar
- ☑ **Show desktop notifications**: Display notifications for transfers and events

**Appearance**
- ☑ **Enable dark mode**: Switch to dark theme (in development)
- **Language**: Select interface language
  - English (default)
  - Spanish, French, German, Chinese, Japanese

### Sync Settings

**Sync Profiles**
- Manage sync profiles created in Smart Sync panel
- Add, Edit, Remove profiles
- Enable/disable individual profiles

**Automatic Sync**
- ☑ **Enable automatic sync every [60] minutes**
  - Interval: 1-1440 minutes (1 min - 24 hours)
  - All enabled profiles sync automatically
- ☑ **Sync all profiles on application startup**
- ☑ **Sync when local files change (watch mode)**
  - Monitors local folders for changes
  - Triggers sync automatically
  - Uses file system watchers

**Conflict Resolution**
- ☑ **Automatically resolve conflicts**
- **Default resolution**:
  - Keep newer version (recommended)
  - Keep older version
  - Keep larger file
  - Keep local version
  - Keep remote version
  - Rename both versions

### Network Settings

**Bandwidth Limits**
- **Upload limit**: 0 KB/s (Unlimited) to 100,000 KB/s
- **Download limit**: 0 KB/s (Unlimited) to 100,000 KB/s
- 0 = No limit (uses full bandwidth)

**Parallel Transfers**
- **Concurrent transfers**: 1-8
- Default: 4
- Higher number = faster for multiple small files
- Lower number = more stable, less resource usage

**Proxy Settings**
- ☑ **Use proxy server**
- **Host**: proxy.example.com
- **Port**: 8080 (1-65535)
- Useful for corporate networks or privacy

### Advanced Settings

**File Filters**
- **Exclude patterns**: `*.tmp, *.bak, .git`
  - Comma-separated list
  - Applies to sync and folder mapping
  - Supports wildcards
- **Max file size**: 0 MB (No limit) to 10,000 MB
  - Files larger than this are skipped
  - 0 = No limit
- ☑ **Skip hidden files**: Ignore files starting with `.` (Linux/macOS)
- ☑ **Skip temporary files**: Skip `*.tmp`, `*.temp`, etc.

**Cache & Logging**
- **Cache path**: Where SDK stores local cache
  - Default: OS-specific location
  - Click "Browse..." to change
- **Max cache size**: 100-10,000 MB
  - Default: 500 MB
  - Automatically cleaned when exceeded
- **Clear Cache**: Delete all cached data
  - Forces re-download of file metadata
  - Use if experiencing sync issues

**Logging**
- ☑ **Enable logging**: Write logs to file
- **Log level**:
  - Error: Only errors
  - Warning: Errors + warnings
  - Info: Normal operation (default)
  - Debug: Detailed information
  - Verbose: Everything (very large logs)
- **Log location**: `~/.config/MegaCustom/logs/`

---

## Keyboard Shortcuts

### Account Management
| Shortcut | Action |
|----------|--------|
| `Ctrl+Tab` | Switch to next account |
| `Ctrl+Shift+Tab` | Switch to previous account |
| `Ctrl+Shift+A` | Open account switcher |

### File Operations
| Shortcut | Action |
|----------|--------|
| `Ctrl+U` | Upload files |
| `Ctrl+D` | Download selected files |
| `Ctrl+Shift+N` | Create new folder |
| `Delete` | Delete selected items |
| `F2` | Rename selected item |

### Edit
| Shortcut | Action |
|----------|--------|
| `Ctrl+X` | Cut |
| `Ctrl+C` | Copy |
| `Ctrl+V` | Paste |
| `Ctrl+A` | Select all |
| `Ctrl+F` | Find/Search |

### View
| Shortcut | Action |
|----------|--------|
| `Ctrl+H` | Show/hide hidden files |
| `Ctrl+Shift+F` | Advanced search |
| `Ctrl+Shift+L` | Cross-account transfer log |

### Application
| Shortcut | Action |
|----------|--------|
| `Ctrl+,` | Settings |
| `F1` | Keyboard shortcuts (help) |
| `Ctrl+Q` | Quit application |

### Navigation
| Shortcut | Action |
|----------|--------|
| `Backspace` | Go to parent folder |
| `Alt+Left` | Back in navigation history |
| `Alt+Right` | Forward in navigation history |
| `Enter` | Open selected folder/file |

### Planned Shortcuts (Not Yet Implemented)
| Shortcut | Action |
|----------|--------|
| `Ctrl+N` | New mapping/profile |
| `Ctrl+E` | Edit selected |
| `Ctrl+R` | Refresh |
| `F5` | Sync/Upload |

---

## Troubleshooting

### Common Issues and Solutions

#### 1. Login Fails

**Problem**: "Invalid credentials" or login fails

**Solutions**:
- Verify email and password are correct
- Check CAPS LOCK is off
- If using 2FA, ensure code is current (not expired)
- Check internet connection
- Verify MEGA service is online at status.mega.nz

**Advanced**:
- Delete session file: `~/.config/MegaCustom/credentials.dat`
- Check log file for errors: `~/.config/MegaCustom/logs/megacustom.log`

---

#### 2. Upload Segfault on Shutdown

**Problem**: Application crashes when exiting after upload completes

**Status**: Known issue, does not affect functionality

**Workaround**:
- Upload completes successfully before crash
- Files are saved correctly
- Restart application to verify upload

**Fix**: Planned for future release

---

#### 3. Uploaded Files Not Appearing

**Problem**: Files uploaded but don't show in File Explorer

**Cause**: SDK cache delay

**Solutions**:
- Wait 10-30 seconds and refresh (F5)
- Click "Refresh" button in toolbar
- Navigate to parent folder and back
- Restart application to force cache rebuild

**Prevention**:
- This is an SDK caching behavior
- Future versions will force refresh after upload

---

#### 4. Sync Conflicts

**Problem**: Sync shows many conflicts, unsure how to resolve

**Solutions**:
1. Open Smart Sync panel
2. Select your profile
3. Click "Analyze" (don't start sync yet)
4. Go to "Conflicts" tab
5. Review each conflict:
   - Green = Local version
   - Blue = Remote version
   - Timestamps and sizes shown
6. Choose resolution strategy:
   - For most recent work: "Keep Newer"
   - For backups: "Keep Both"
   - For manual control: "Ask User"
7. Update profile conflict resolution
8. Re-analyze and check "Preview" tab
9. Click "Start Sync" when ready

**Prevention**:
- Use "Keep Newer" for single-user scenarios
- Use "Keep Both" for important files
- Use "Ask User" when unsure

---

#### 5. Slow Search

**Problem**: Search takes a long time or doesn't work

**Solutions**:
- Wait for initial index to complete (status bar shows progress)
- For large accounts (1M+ files), initial index takes 15-20 minutes
- Subsequent searches are instant (<50ms)
- Check "Index: Ready" status in status bar

**Rebuild Index**:
1. Settings → Advanced
2. Click "Rebuild Search Index"
3. Wait for completion
4. Try search again

---

#### 6. High Memory Usage

**Problem**: Application uses a lot of RAM

**Causes**:
- Large file count in account (search index)
- Multiple active accounts (session pool)
- Large transfer queue

**Solutions**:
- Reduce concurrent transfers (Settings → Network → Parallel Transfers)
- Close unused account sessions (Account Manager → Remove)
- Clear cache (Settings → Advanced → Clear Cache)

**Expected Usage**:
- Baseline: ~100-200 MB
- With search index (100K files): +5-10 MB
- With search index (1M files): +50-100 MB
- Per active transfer: +10-50 MB

---

#### 7. Transfer Stalls

**Problem**: Upload/download stops at certain percentage

**Solutions**:
- Check internet connection stability
- Disable VPN temporarily (can interfere with MEGA)
- Reduce concurrent transfers (Settings → Network)
- Clear cache and retry
- Check firewall isn't blocking application

**Resume Transfer**:
- Click "Pause" then "Resume" in Transfer Queue
- Or cancel and restart the transfer

---

#### 8. Folder Mapper Not Uploading

**Problem**: Mapping created but "Upload" does nothing

**Checks**:
- Local path exists and is readable
- Remote path is valid (starts with `/`)
- No permission issues on local folder
- MEGA account has storage space

**Debug**:
1. Click "Preview" instead of "Upload"
2. Check what files are detected
3. If preview is empty:
   - Verify local path
   - Check exclude patterns (might be too broad)
   - Ensure "Recursive" is checked if you have subfolders
4. Check application logs for errors

---

#### 9. Cross-Account Transfer Fails

**Problem**: "Invalid argument" or transfer fails between accounts

**Solutions**:
- Ensure both accounts are logged in
- Verify destination account has storage space
- Check destination path exists (create folder first)
- Ensure source file isn't a folder (folders not supported yet)

**Workaround**:
- For folders: Upload to first account, then download and upload to second
- For large files: Use public link method manually

---

#### 10. Dark Mode Not Working

**Status**: Dark mode is in development

**Workaround**:
- Use OS dark mode (may partially theme the app)
- Wait for future release with complete dark theme

---

#### 11. Settings Not Saving

**Problem**: Changes in Settings dialog don't persist

**Solutions**:
- Click "Save" or "Apply" (not just "OK")
- Check write permissions on `~/.config/MegaCustom/`
- Verify disk space available
- Check logs for permission errors

**Reset Settings**:
```bash
rm ~/.config/MegaCustom/settings.ini
# Restart application - defaults will be used
```

---

#### 12. Application Won't Start

**Problem**: MegaCustomGUI doesn't launch

**Solutions**:
1. Check API key is set:
   ```bash
   echo $MEGA_APP_KEY
   echo $MEGA_API_KEY
   ```
2. Run from terminal to see error messages:
   ```bash
   ./MegaCustomGUI
   ```
3. Check Qt6 is installed:
   ```bash
   qmake --version
   ```
4. Verify executable permissions:
   ```bash
   chmod +x MegaCustomGUI
   ```
5. Check dependencies:
   ```bash
   ldd MegaCustomGUI
   ```

**Common Errors**:
- "API key not set": Set `MEGA_APP_KEY` environment variable
- "Qt plugin not found": Install Qt6 plugins
- "Shared library error": Install missing dependencies

---

### Getting Help

#### Log Files

Logs are located at:
- Linux: `~/.config/MegaCustom/logs/`
- Windows: `%APPDATA%/MegaCustom/logs/`
- macOS: `~/Library/Logs/MegaCustom/`

Enable verbose logging:
1. Settings → Advanced
2. ☑ Enable logging
3. Log level: Verbose
4. Reproduce issue
5. Check latest log file

#### Configuration Files

- **Settings**: `~/.config/MegaCustom/settings.ini`
- **Credentials**: `~/.config/MegaCustom/credentials.dat` (encrypted)
- **Sync Profiles**: `~/.config/MegaCustom/sync_profiles.json`
- **Folder Mappings**: `~/.config/MegaCustom/folder_mappings.json`
- **Scheduler**: `~/.config/MegaCustom/scheduler.json`
- **Transfer History**: `~/.config/MegaCustom/transfer_log.db` (SQLite)

#### Reporting Issues

When reporting issues, include:
1. **MegaCustom version**: Help → About
2. **Operating System**: OS and version
3. **Steps to reproduce**: Detailed steps
4. **Expected vs Actual**: What should happen vs what happened
5. **Log files**: Relevant portions (use verbose logging)
6. **Screenshots**: If visual issue

**Where to Report**:
- GitHub Issues: (repository URL)
- Email: (support email)

---

### Performance Tips

#### For Large Accounts (100K+ files)

1. **Wait for initial index**
   - First login takes time to index
   - Subsequent logins are fast (cache used)

2. **Reduce search scope**
   - Use path filters: `path:documents` instead of searching all
   - Use specific extensions: `ext:pdf` instead of all files

3. **Limit parallel transfers**
   - Settings → Network → Concurrent transfers: 2-4
   - Higher numbers don't always mean faster

4. **Clear old transfer history**
   - Transfer Log → Clear history older than 30 days
   - Reduces database size

#### For Slow Connections

1. **Reduce bandwidth usage**
   - Settings → Network → Upload/Download limits
   - Set reasonable limits (e.g., 500 KB/s)

2. **Schedule syncs during off-hours**
   - Smart Sync → Auto-sync → Set interval to sync at night
   - Reduces impact on work hours

3. **Use incremental uploads**
   - Folder Mapper → ☑ Incremental
   - Only uploads changed files

#### For Multiple Accounts

1. **Close unused sessions**
   - Account Manager → Remove accounts you're not actively using
   - Sessions are cached (max 5)

2. **Group accounts logically**
   - Use groups in Account Manager
   - Easy to find the account you need

3. **Use Quick Peek for browsing**
   - Don't switch accounts just to browse
   - Quick Peek is faster and non-destructive

---

## Appendix

### CLI Commands (Alternative Interface)

MegaCustom also includes a powerful command-line interface:

```bash
# Authentication
./megacustom auth login EMAIL PASSWORD
./megacustom auth status
./megacustom auth logout

# Folder Operations
./megacustom folder list /
./megacustom folder create /new-folder
./megacustom folder rename /old-name new-name
./megacustom folder delete /folder-name

# File Transfers
./megacustom upload file /local/file.txt /remote/path
./megacustom download file /remote/file.txt /local/path

# Advanced Features
./megacustom multiupload --help
./megacustom sync --help
./megacustom map --help
./megacustom rename --help
```

See CLI documentation for detailed command reference.

---

### Technical Specifications

**System Requirements**
- **OS**: Windows 10/11, macOS 10.14+, Linux (kernel 4.x+)
- **RAM**: 200 MB minimum, 500 MB recommended
- **Storage**: 50 MB installation, 500 MB for cache (configurable)
- **Network**: Internet connection required
- **Display**: 1366x768 minimum resolution

**File Limits**
- **Max file size**: 5 GB per file (MEGA limitation)
- **Concurrent transfers**: 1-8 configurable
- **Search index**: Supports 1M+ files
- **Sync profiles**: Unlimited
- **Folder mappings**: Unlimited
- **Accounts**: 5 concurrent sessions (unlimited total)

**Performance Metrics**
- **Search speed**: <50ms for 1M+ files
- **Index build**: ~1-2 seconds per 100K files
- **Transfer speed**: Limited by network and MEGA account type
- **Startup time**: <3 seconds (after initial index)

---

### Version History

**Version 1.0** (December 10, 2025)
- Complete multi-account support
- Everything-like instant search
- Cross-account transfers
- All core features implemented
- 37,265 lines of code
- 61 .cpp files, 62 .h files

**Previous Milestones**
- Session 18: Multi-Account Support
- Session 17: MEGAsync UI/UX Comparison
- Session 14: Code Quality & Thread Safety
- Session 13: Path Handling & Utilities
- Session 12: UI/UX Improvements
- Session 11: Bug Fixes

---

### Credits and License

**MegaCustom** is built on top of the official MEGA SDK.

**Components**:
- Qt6 Framework (LGPL)
- MEGA SDK (Mega Limited)
- PCRE2 (BSD License)
- SQLite (Public Domain)

**Third-Party Icons**:
- Feather Icons (MIT License)

---

**End of User Guide**

For developer documentation, see:
- `/docs/GUI_ARCHITECTURE.md` - Technical architecture
- `/docs/archive/UI_UX_DESIGN.md` - Design specifications
- `/qt-gui/README.md` - Qt GUI specific documentation
- `/README.md` - Project overview

For technical support or feature requests, please contact the development team or open an issue on the project repository.

---

*Last Updated: December 10, 2025*
*MegaCustom Version 1.0*
*Document Version 1.0*
