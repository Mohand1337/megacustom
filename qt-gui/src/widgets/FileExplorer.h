#ifndef MEGACUSTOM_FILEEXPLORER_H
#define MEGACUSTOM_FILEEXPLORER_H

#include <QWidget>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <memory>

QT_BEGIN_NAMESPACE
class QTreeView;
class QLineEdit;
class QPushButton;
class QLabel;
class QFileSystemModel;
class QStandardItemModel;
class QItemSelection;
class QMenu;
QT_END_NAMESPACE

namespace MegaCustom {

// Forward declarations
class FileController;
class FileSystemModel;
class LoadingSpinner;
class ModernMenu;

/**
 * File explorer widget for browsing local and remote files
 */
class FileExplorer : public QWidget
{
    Q_OBJECT

public:
    /**
     * Explorer type
     */
    enum ExplorerType {
        Local,  // Browse local filesystem
        Remote  // Browse Mega cloud storage
    };

    /**
     * Constructor
     * @param type Explorer type (local or remote)
     * @param parent Parent widget
     */
    explicit FileExplorer(ExplorerType type, QWidget* parent = nullptr);

    /**
     * Destructor
     */
    virtual ~FileExplorer();

    /**
     * Set file controller
     * @param controller File controller instance
     */
    void setFileController(FileController* controller);

    /**
     * Get current path
     * @return Current directory path
     */
    QString currentPath() const;

    /**
     * Navigate to path
     * @param path Path to navigate to
     */
    void navigateTo(const QString& path);

    /**
     * Get selected files
     * @return List of selected file paths
     */
    QStringList selectedFiles() const;

    /**
     * Set show hidden files
     * @param show true to show hidden files
     */
    void setShowHidden(bool show);

    /**
     * Clear the explorer
     */
    void clear();

    /**
     * Refresh current directory
     */
    void refresh();

    /**
     * Check if explorer is enabled
     * @return true if enabled
     */
    bool isEnabled() const;

    /**
     * Set search filter text
     * @param filter Filter text to match against file names
     */
    void setSearchFilter(const QString& filter);

    /**
     * Clear search filter
     */
    void clearSearchFilter();

    /**
     * Display search results (replaces current directory view)
     * @param results List of file/folder info maps from search
     */
    void showSearchResults(const QVariantList& results);

    /**
     * Check if there is a selection
     * @return true if items are selected
     */
    bool hasSelection() const;

    /**
     * Check if clipboard has content
     * @return true if clipboard is not empty
     */
    bool hasClipboard() const;

signals:
    /**
     * Emitted when a file is double-clicked
     * @param path File path
     */
    void fileDoubleClicked(const QString& path);

    /**
     * Emitted when selection changes
     * @param selectedPaths Selected file paths
     */
    void selectionChanged(const QStringList& selectedPaths);

    /**
     * Emitted when current path changes
     * @param path New path
     */
    void pathChanged(const QString& path);

    /**
     * Emitted when files are dropped
     * @param files List of file paths
     */
    void filesDropped(const QStringList& files);

    /**
     * Emitted when upload is requested
     * @param localPath Local file path to upload
     */
    void uploadRequested(const QString& localPath);

    /**
     * Emitted when download is requested
     * @param remotePath Remote file path to download
     */
    void downloadRequested(const QString& remotePath);

    /**
     * Emitted when context menu is requested
     * @param path Path where menu was requested
     * @param globalPos Global position for menu
     */
    void contextMenuRequested(const QString& path, const QPoint& globalPos);

    /**
     * Emitted when a move operation is requested
     * @param source Source path to move
     * @param destination Destination directory
     */
    void moveRequested(const QString& source, const QString& destination);

    /**
     * Emitted when a copy operation is requested
     * @param source Source path to copy
     * @param destination Destination directory
     */
    void copyRequested(const QString& source, const QString& destination);

    /**
     * Emitted when cross-account copy is requested
     * @param sourcePaths Source paths to copy
     * @param targetAccountId Target account ID
     */
    void crossAccountCopyRequested(const QStringList& sourcePaths, const QString& targetAccountId);

    /**
     * Emitted when cross-account move is requested
     * @param sourcePaths Source paths to move
     * @param targetAccountId Target account ID
     */
    void crossAccountMoveRequested(const QStringList& sourcePaths, const QString& targetAccountId);

public slots:
    /**
     * Go back in history
     */
    void goBack();

    /**
     * Go forward in history
     */
    void goForward();

    /**
     * Go up one directory
     */
    void goUp();

    /**
     * Go to home directory
     */
    void goHome();

    /**
     * Create new folder
     */
    void createNewFolder();

    /**
     * Create new file
     */
    void createNewFile();

    /**
     * Delete selected items
     */
    void deleteSelected();

    /**
     * Rename selected item
     */
    void renameSelected();

    /**
     * Copy selected items
     */
    void copySelected();

    /**
     * Cut selected items
     */
    void cutSelected();

    /**
     * Paste items
     */
    void paste();

    /**
     * Sort by column
     * @param column Column index to sort by
     * @param order Sort order (ascending/descending)
     */
    void sortByColumn(int column, Qt::SortOrder order);

    /**
     * Select all items
     */
    void selectAll();

protected:
    /**
     * Handle drag enter event
     * @param event Drag enter event
     */
    void dragEnterEvent(QDragEnterEvent* event) override;

    /**
     * Handle drag move event
     * @param event Drag move event
     */
    void dragMoveEvent(QDragMoveEvent* event) override;

    /**
     * Handle drop event
     * @param event Drop event
     */
    void dropEvent(QDropEvent* event) override;

    /**
     * Handle resize event
     * @param event Resize event
     */
    void resizeEvent(QResizeEvent* event) override;

private slots:
    /**
     * Handle item double click
     * @param index Model index of clicked item
     */
    void onItemDoubleClicked(const QModelIndex& index);

    /**
     * Handle selection change
     * @param selected Selected items
     * @param deselected Deselected items
     */
    void onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);


    /**
     * Handle custom context menu
     * @param pos Position for menu
     */
    void onCustomContextMenu(const QPoint& pos);

    /**
     * Handle remote file list received
     * @param files List of file info maps
     */
    void onRemoteFileListReceived(const QVariantList& files);

    /**
     * Handle loading started
     * @param path Path being loaded
     */
    void onLoadingStarted(const QString& path);

    /**
     * Handle loading finished
     */
    void onLoadingFinished();

    /**
     * Handle loading error
     * @param error Error message
     */
    void onLoadingError(const QString& error);

    /**
     * Update navigation buttons
     */
    void updateNavigationButtons();

    /**
     * Update status information
     */
    void updateStatus();

private:
    /**
     * Set up the UI
     */
    void setupUI();

    /**
     * Create context menu
     */
    void createContextMenu();

    /**
     * Update cross-account menus with available accounts
     */
    void updateCrossAccountMenus();

    /**
     * Initialize file system model
     */
    void initializeModel();

    /**
     * Add to history
     * @param path Path to add
     */
    void addToHistory(const QString& path);

    /**
     * Get icon for file
     * @param path File path
     * @param isDir true if directory
     * @return Icon for file type
     */
    QIcon getFileIcon(const QString& path, bool isDir) const;

    /**
     * Format file size
     * @param bytes Size in bytes
     * @return Formatted size string
     */
    QString formatFileSize(qint64 bytes) const;

private:
    // Type
    ExplorerType m_type;

    // Controllers
    FileController* m_fileController;

    // UI elements
    LoadingSpinner* m_loadingSpinner;
    bool m_isLoading;

    // Views
    QTreeView* m_treeView;
    QWidget* m_emptyStateWidget;
    QPushButton* m_emptyStateUploadBtn;

    // Model
    QFileSystemModel* m_localModel;
    QStandardItemModel* m_remoteModel;

    // Context menu (using ModernMenu for modern styling)
    ModernMenu* m_contextMenu;
    QAction* m_copyAction;
    QAction* m_cutAction;
    QAction* m_pasteAction;
    QAction* m_deleteAction;
    QAction* m_renameAction;
    QAction* m_newFolderAction;
    QAction* m_propertiesAction;
    ModernMenu* m_copyToAccountMenu;
    ModernMenu* m_moveToAccountMenu;

    // State
    QString m_currentPath;
    QStringList m_history;
    int m_historyIndex;
    bool m_showHidden;
    QStringList m_clipboard;
    bool m_clipboardCut;
    QString m_searchFilter;

    // Status
    QLabel* m_statusLabel;
    int m_fileCount;
    int m_folderCount;
    qint64 m_totalSize;

    // Rename synchronization
    bool m_waitingForRenameRefresh = false;
    QString m_pendingSelectAfterRefresh;

    /**
     * Select a file by name in the current view
     * @param fileName Name of file to select
     */
    void selectFileByName(const QString& fileName);
};

} // namespace MegaCustom

#endif // MEGACUSTOM_FILEEXPLORER_H