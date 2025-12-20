#ifndef MEGACUSTOM_MAINWINDOW_H
#define MEGACUSTOM_MAINWINDOW_H

#include <QMainWindow>
#include <memory>
#include "../accounts/AccountModels.h"

QT_BEGIN_NAMESPACE
class QSplitter;
class QStatusBar;
class QProgressBar;
class QLabel;
class QToolBar;
class QMenu;
class QAction;
class QCloseEvent;
class QTabWidget;
class QStackedWidget;
QT_END_NAMESPACE

namespace MegaCustom {

// Forward declarations
class FileExplorer;
class TransferQueue;
class AuthController;
class FileController;
class TransferController;
class FolderMapperController;
class MultiUploaderController;
class SmartSyncController;
class CloudCopierController;
class DistributionController;
class WatermarkerController;
class FolderMapperPanel;
class MultiUploaderPanel;
class SmartSyncPanel;
class CloudCopierPanel;
class MemberRegistryPanel;
class DistributionPanel;
class DownloaderPanel;
class WatermarkPanel;
class LogViewerPanel;
class MegaSidebar;
class TopToolbar;
class BreadcrumbWidget;
class SettingsPanel;
class SearchResultsPanel;
class CloudSearchIndex;
class AdvancedSearchPanel;
class CrossAccountLogPanel;
class CrossAccountTransferManager;
class TransferLogStore;
class QuickPeekPanel;

/**
 * Main application window
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /**
     * Constructor
     * @param parent Parent widget
     */
    explicit MainWindow(QWidget* parent = nullptr);

    /**
     * Destructor
     */
    virtual ~MainWindow();

    /**
     * Set authentication controller
     * @param controller Auth controller instance
     */
    void setAuthController(AuthController* controller);

    /**
     * Set file controller
     * @param controller File controller instance
     */
    void setFileController(FileController* controller);

    /**
     * Set transfer controller
     * @param controller Transfer controller instance
     */
    void setTransferController(TransferController* controller);

    /**
     * Set folder mapper controller
     * @param controller FolderMapper controller instance
     */
    void setFolderMapperController(FolderMapperController* controller);

    /**
     * Set multi uploader controller
     * @param controller MultiUploader controller instance
     */
    void setMultiUploaderController(MultiUploaderController* controller);

    /**
     * Set smart sync controller
     * @param controller SmartSync controller instance
     */
    void setSmartSyncController(SmartSyncController* controller);

    /**
     * Set cloud copier controller
     * @param controller CloudCopier controller instance
     */
    void setCloudCopierController(CloudCopierController* controller);

    /**
     * Set distribution controller
     * @param controller Distribution controller instance
     */
    void setDistributionController(DistributionController* controller);

    /**
     * Set watermarker controller
     * @param controller Watermarker controller instance
     */
    void setWatermarkerController(WatermarkerController* controller);

    /**
     * Apply current settings
     */
    void applySettings();

    /**
     * Save window state
     */
    QByteArray saveState() const;

    /**
     * Restore window state
     * @param state Previous state
     */
    bool restoreState(const QByteArray& state);

public slots:
    /**
     * Show login dialog
     */
    void showLoginDialog();

    /**
     * Show upload dialog
     */
    void showUploadDialog();

    /**
     * Show download dialog
     */
    void showDownloadDialog();

    /**
     * Show transfers panel
     */
    void showTransfers();

    /**
     * Show/hide transfers panel
     */
    void toggleTransfers();

    /**
     * Show settings panel
     */
    void onSettings();

    /**
     * Handle login status change
     * @param loggedIn true if logged in
     */
    void onLoginStatusChanged(bool loggedIn);

    /**
     * Update status bar
     * @param message Status message
     */
    void updateStatus(const QString& message);

    /**
     * Update transfer progress
     * @param progress Progress percentage (0-100)
     */
    void updateTransferProgress(int progress);

    /**
     * Show error message
     * @param title Error title
     * @param message Error message
     */
    void showError(const QString& title, const QString& message);

    /**
     * Show information message
     * @param title Message title
     * @param message Message text
     */
    void showInfo(const QString& title, const QString& message);

protected:
    /**
     * Handle close event
     * @param event Close event
     */
    void closeEvent(QCloseEvent* event) override;

    /**
     * Handle drag enter event
     * @param event Drag enter event
     */
    void dragEnterEvent(QDragEnterEvent* event) override;

    /**
     * Handle drop event
     * @param event Drop event
     */
    void dropEvent(QDropEvent* event) override;

private slots:
    // File menu actions
    void onNewFolder();
    void onCreateFile();
    void onUploadFile();
    void onUploadFolder();
    void onDownload();
    void onDelete();
    void onRename();
    void onProperties();
    void onExit();

    // Edit menu actions
    void onCut();
    void onCopy();
    void onPaste();
    void onSelectAll();
    void onFind();

    // View menu actions
    void onRefresh();
    void onShowHidden();
    void onSortByName();
    void onSortBySize();
    void onSortByDate();

    // Tools menu actions
    void onAdvancedSearch();
    void onRegexRename();

    // Help menu actions
    void onHelp();
    void onKeyboardShortcuts();
    void onAbout();

    // Context menu actions
    void onContextMenuRequested(const QPoint& pos);

    // File explorer signals
    void onLocalFileSelected(const QString& path);
    void onRemoteFileSelected(const QString& path);
    void onLocalPathChanged(const QString& path);
    void onRemotePathChanged(const QString& path);
    void onFilesDropped(const QStringList& files, const QString& target);

    // Transfer signals
    void onTransferStarted(const QString& file);
    void onTransferProgress(const QString& file, qint64 bytesTransferred, qint64 totalBytes);
    void onTransferCompleted(const QString& file);
    void onTransferFailed(const QString& file, const QString& error);

    // Sidebar navigation (MEGA redesign)
    void onNavigationItemClicked(int item);

    // Toolbar navigation (MEGA redesign)
    void onBreadcrumbPathClicked(const QString& path);
    void onSearchTextChanged(const QString& text);
    void onGlobalSearchRequested(const QString& text);
    void onSearchResultsReceived(const QVariantList& results);
    void onStorageInfoReceived(qint64 usedBytes, qint64 totalBytes);

    // Search panel signals
    void showSearchPanel();
    void hideSearchPanel();
    void onSearchResultActivated(const QString& handle, const QString& path, bool isFolder);

    // Account management
    void onAccountSwitchRequested(const QString& accountId);
    void onAccountSwitched(const QString& accountId);
    void onLoginRequired(const QString& accountId);
    void onAddAccountRequested();
    void onManageAccountsRequested();
    void cycleToNextAccount();
    void cycleToPreviousAccount();
    void showAccountSwitcher();

    // Cross-account transfers
    void onCrossAccountCopy(const QStringList& paths, const QString& targetAccountId);
    void onCrossAccountMove(const QStringList& paths, const QString& targetAccountId);
    void onShowTransferLog();
    void onCrossAccountTransferCompleted(const MegaCustom::CrossAccountTransfer& transfer);
    void onCrossAccountTransferFailed(const MegaCustom::CrossAccountTransfer& transfer);
    void onSharedLinksWillBreak(const QStringList& sourcePaths,
                                const QStringList& pathsWithLinks,
                                const QString& sourceAccountId,
                                const QString& targetAccountId,
                                const QString& targetPath);

    // Quick peek
    void onQuickPeekRequested(const QString& accountId);
    void onQuickPeekCopyToActive(const QStringList& paths, const QString& sourceAccountId);

private:
    /**
     * Create actions
     */
    void createActions();

    /**
     * Create menus
     */
    void createMenus();

    /**
     * Create status bar
     */
    void createStatusBar();

    /**
     * Set up UI
     */
    void setupUI();

    /**
     * Update actions based on state
     */
    void updateActions();

    /**
     * Connect signals and slots
     */
    void connectSignals();

    /**
     * Setup account management shortcuts
     */
    void setupAccountShortcuts();

    /**
     * Load window settings
     */
    void loadSettings();

    /**
     * Save window settings
     */
    void saveSettings();

    /**
     * Check for unsaved changes
     * @return true if safe to close
     */
    bool checkUnsavedChanges();

private:
    // Controllers
    AuthController* m_authController;
    FileController* m_fileController;
    TransferController* m_transferController;

    // Main widgets
    QSplitter* m_centralSplitter;
    FileExplorer* m_remoteExplorer;
    TransferQueue* m_transferQueue;

    // Feature panels
    FolderMapperPanel* m_folderMapperPanel;
    MultiUploaderPanel* m_multiUploaderPanel;
    SmartSyncPanel* m_smartSyncPanel;
    CloudCopierPanel* m_cloudCopierPanel;
    MemberRegistryPanel* m_memberRegistryPanel;
    DistributionPanel* m_distributionPanel;
    DownloaderPanel* m_downloaderPanel;
    WatermarkPanel* m_watermarkPanel;
    LogViewerPanel* m_logViewerPanel;
    SettingsPanel* m_settingsPanel;

    // MEGA-style layout widgets
    MegaSidebar* m_sidebar;
    TopToolbar* m_topToolbar;
    QWidget* m_toolbarContainer;  // Fixed-height container to prevent resize on toolbar visibility
    QStackedWidget* m_contentStack;

    // Search components
    SearchResultsPanel* m_searchPanel;
    CloudSearchIndex* m_searchIndex;
    AdvancedSearchPanel* m_advancedSearchPanel;

    // Cross-account components
    CrossAccountLogPanel* m_crossAccountLogPanel;
    CrossAccountTransferManager* m_crossAccountTransferManager;
    TransferLogStore* m_transferLogStore;
    QuickPeekPanel* m_quickPeekPanel;

    // Feature controllers
    FolderMapperController* m_folderMapperController;
    MultiUploaderController* m_multiUploaderController;
    SmartSyncController* m_smartSyncController;
    CloudCopierController* m_cloudCopierController;
    DistributionController* m_distributionController;
    WatermarkerController* m_watermarkerController;

    // Status bar widgets
    QStatusBar* m_statusBar;
    QLabel* m_statusLabel;
    QLabel* m_connectionIndicator;
    QLabel* m_connectionLabel;
    QLabel* m_userLabel;
    QLabel* m_uploadSpeedLabel;
    QLabel* m_downloadSpeedLabel;
    QProgressBar* m_progressBar;

    // Menus
    QMenu* m_fileMenu;
    QMenu* m_editMenu;
    QMenu* m_viewMenu;
    QMenu* m_toolsMenu;
    QMenu* m_helpMenu;

    // File menu actions
    QAction* m_newFolderAction;
    QAction* m_uploadFileAction;
    QAction* m_uploadFolderAction;
    QAction* m_downloadAction;
    QAction* m_deleteAction;
    QAction* m_renameAction;
    QAction* m_propertiesAction;
    QAction* m_exitAction;

    // Edit menu actions
    QAction* m_cutAction;
    QAction* m_copyAction;
    QAction* m_pasteAction;
    QAction* m_selectAllAction;
    QAction* m_findAction;

    // View menu actions
    QAction* m_refreshAction;
    QAction* m_showHiddenAction;
    QAction* m_sortByNameAction;
    QAction* m_sortBySizeAction;
    QAction* m_sortByDateAction;
    QAction* m_showTransfersAction;

    // Tools menu actions
    QAction* m_advancedSearchAction;
    QAction* m_regexRenameAction;
    QAction* m_transferLogAction;
    QAction* m_settingsAction;

    // Help menu actions
    QAction* m_helpAction;
    QAction* m_shortcutsAction;
    QAction* m_aboutAction;

    // Toolbar actions
    QAction* m_loginAction;
    QAction* m_logoutAction;

    // State
    bool m_isLoggedIn;
    bool m_loginDialogShowing;
    QString m_currentUser;
    QString m_pendingLoginAccountId;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_MAINWINDOW_H