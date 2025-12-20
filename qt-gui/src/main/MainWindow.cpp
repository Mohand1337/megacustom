#include "MainWindow.h"
#include "Application.h"
#include "widgets/FileExplorer.h"
#include "widgets/TransferQueue.h"
#include "widgets/FolderMapperPanel.h"
#include "widgets/MultiUploaderPanel.h"
#include "widgets/SmartSyncPanel.h"
#include "widgets/CloudCopierPanel.h"
#include "widgets/MemberRegistryPanel.h"
#include "widgets/DistributionPanel.h"
#include "widgets/DownloaderPanel.h"
#include "widgets/WatermarkPanel.h"
#include "widgets/LogViewerPanel.h"
#include "widgets/MegaSidebar.h"
#include "widgets/TopToolbar.h"
#include "widgets/BreadcrumbWidget.h"
#include "controllers/AuthController.h"
#include "controllers/FileController.h"
#include "controllers/TransferController.h"
#include "controllers/FolderMapperController.h"
#include "controllers/MultiUploaderController.h"
#include "controllers/SmartSyncController.h"
#include "controllers/CloudCopierController.h"
#include "controllers/DistributionController.h"
#include "controllers/WatermarkerController.h"
#include "dialogs/LoginDialog.h"
#include "widgets/SettingsPanel.h"
#include "widgets/SearchResultsPanel.h"
#include "widgets/AdvancedSearchPanel.h"
#include "search/CloudSearchIndex.h"
#include "utils/Settings.h"
#include "utils/DpiScaler.h"
#include "styles/ThemeManager.h"
#include "core/MegaManager.h"
#include "accounts/AccountManager.h"
#include "accounts/TransferLogStore.h"
#include "accounts/CrossAccountTransferManager.h"
#include "dialogs/AccountManagerDialog.h"
#include "dialogs/RemoteFolderBrowserDialog.h"
#include "widgets/CrossAccountLogPanel.h"
#include "widgets/QuickPeekPanel.h"

#include <QMenuBar>
#include <QShortcut>
#include <QTabWidget>
#include <QStackedWidget>
#include <QToolBar>
#include <QStatusBar>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QMessageBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QCloseEvent>
#include <QSettings>
#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

namespace MegaCustom {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_authController(nullptr)
    , m_fileController(nullptr)
    , m_transferController(nullptr)
    , m_folderMapperPanel(nullptr)
    , m_multiUploaderPanel(nullptr)
    , m_smartSyncPanel(nullptr)
    , m_cloudCopierPanel(nullptr)
    , m_memberRegistryPanel(nullptr)
    , m_distributionPanel(nullptr)
    , m_downloaderPanel(nullptr)
    , m_watermarkPanel(nullptr)
    , m_logViewerPanel(nullptr)
    , m_sidebar(nullptr)
    , m_topToolbar(nullptr)
    , m_toolbarContainer(nullptr)
    , m_contentStack(nullptr)
    , m_folderMapperController(nullptr)
    , m_multiUploaderController(nullptr)
    , m_smartSyncController(nullptr)
    , m_cloudCopierController(nullptr)
    , m_searchPanel(nullptr)
    , m_searchIndex(nullptr)
    , m_advancedSearchPanel(nullptr)
    , m_crossAccountLogPanel(nullptr)
    , m_crossAccountTransferManager(nullptr)
    , m_transferLogStore(nullptr)
    , m_quickPeekPanel(nullptr)
    , m_isLoggedIn(false)
    , m_loginDialogShowing(false)
{
    setupUI();
    createActions();
    createMenus();
    createStatusBar();
    connectSignals();
    loadSettings();

    // Set initial state
    updateActions();
    setAcceptDrops(true);

    // Set window properties
    setWindowTitle("MegaCustom - Cloud Storage Manager");

    // Screen-aware window sizing
    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen) {
        QRect available = screen->availableGeometry();
        int targetWidth = qMin(1200, available.width() * 80 / 100);
        int targetHeight = qMin(700, available.height() * 80 / 100);
        resize(targetWidth, targetHeight);
    } else {
        resize(1200, 700);
    }
}

MainWindow::~MainWindow()
{
    saveSettings();
}

void MainWindow::setAuthController(AuthController* controller)
{
    m_authController = controller;

    if (m_authController) {
        connect(m_authController, &AuthController::loginSuccess,
                this, [this](const QString& sessionKey) {
                    m_isLoggedIn = true;
                    updateActions();
                    updateStatus("Logged in successfully");
                });

        connect(m_authController, &AuthController::loginFailed,
                this, [this](const QString& error) {
                    showError("Login Failed", error);
                });

        connect(m_authController, &AuthController::logoutComplete,
                this, [this]() {
                    m_isLoggedIn = false;
                    updateActions();
                    updateStatus("Logged out");
                });
    }
}

void MainWindow::setFileController(FileController* controller)
{
    m_fileController = controller;

    if (m_fileController && m_remoteExplorer) {
        m_remoteExplorer->setFileController(controller);

        // Connect search results signal
        connect(m_fileController, &FileController::searchResultsReceived,
                this, &MainWindow::onSearchResultsReceived);

        // Connect storage info signal for sidebar
        connect(m_fileController, &FileController::storageInfoReceived,
                this, &MainWindow::onStorageInfoReceived);
    }

    // Pass FileController to panels that need remote folder browsing
    if (m_fileController && m_folderMapperPanel) {
        m_folderMapperPanel->setFileController(controller);
    }
    if (m_fileController && m_multiUploaderPanel) {
        m_multiUploaderPanel->setFileController(controller);
    }
    if (m_fileController && m_distributionPanel) {
        m_distributionPanel->setFileController(controller);
    }
    if (m_fileController && m_memberRegistryPanel) {
        m_memberRegistryPanel->setFileController(controller);
    }
}

void MainWindow::setTransferController(TransferController* controller)
{
    m_transferController = controller;

    if (m_transferController && m_transferQueue) {
        m_transferQueue->setTransferController(controller);

        connect(m_transferController, &TransferController::transferStarted,
                this, &MainWindow::onTransferStarted);
        connect(m_transferController, &TransferController::transferProgress,
                this, &MainWindow::onTransferProgress);
        connect(m_transferController, &TransferController::transferCompleted,
                this, &MainWindow::onTransferCompleted);
        connect(m_transferController, &TransferController::transferFailed,
                this, &MainWindow::onTransferFailed);
    }
}

void MainWindow::setFolderMapperController(FolderMapperController* controller)
{
    m_folderMapperController = controller;

    if (m_folderMapperController && m_folderMapperPanel) {
        m_folderMapperPanel->setController(controller);

        // Connect panel signals to controller slots
        connect(m_folderMapperPanel, &FolderMapperPanel::addMappingRequested,
                m_folderMapperController, &FolderMapperController::addMapping);
        connect(m_folderMapperPanel, &FolderMapperPanel::removeMappingRequested,
                m_folderMapperController, &FolderMapperController::removeMapping);
        connect(m_folderMapperPanel, &FolderMapperPanel::editMappingRequested,
                m_folderMapperController, &FolderMapperController::updateMapping);
        connect(m_folderMapperPanel, &FolderMapperPanel::toggleMappingEnabled,
                m_folderMapperController, &FolderMapperController::setMappingEnabled);
        connect(m_folderMapperPanel, &FolderMapperPanel::uploadMappingRequested,
                m_folderMapperController, &FolderMapperController::uploadMapping);
        connect(m_folderMapperPanel, &FolderMapperPanel::uploadAllRequested,
                m_folderMapperController, &FolderMapperController::uploadAll);
        connect(m_folderMapperPanel, &FolderMapperPanel::previewUploadRequested,
                m_folderMapperController, &FolderMapperController::previewUpload);
        connect(m_folderMapperPanel, &FolderMapperPanel::cancelUploadRequested,
                m_folderMapperController, &FolderMapperController::cancelUpload);
        connect(m_folderMapperPanel, &FolderMapperPanel::refreshMappingsRequested,
                m_folderMapperController, &FolderMapperController::loadMappings);

        // Connect controller signals to panel slots
        connect(m_folderMapperController, &FolderMapperController::clearMappings,
                m_folderMapperPanel, &FolderMapperPanel::clearMappingsTable);
        connect(m_folderMapperController, &FolderMapperController::mappingsLoaded,
                m_folderMapperPanel, &FolderMapperPanel::onMappingsLoaded);
        connect(m_folderMapperController, &FolderMapperController::mappingAdded,
                m_folderMapperPanel, &FolderMapperPanel::onMappingAdded);
        connect(m_folderMapperController, &FolderMapperController::mappingRemoved,
                m_folderMapperPanel, &FolderMapperPanel::onMappingRemoved);
        connect(m_folderMapperController, &FolderMapperController::mappingUpdated,
                m_folderMapperPanel, &FolderMapperPanel::onMappingUpdated);
        connect(m_folderMapperController, &FolderMapperController::uploadStarted,
                m_folderMapperPanel, &FolderMapperPanel::onUploadStarted);
        connect(m_folderMapperController, &FolderMapperController::uploadProgress,
                m_folderMapperPanel, &FolderMapperPanel::onUploadProgress);
        connect(m_folderMapperController, &FolderMapperController::uploadComplete,
                m_folderMapperPanel, &FolderMapperPanel::onUploadComplete);
        connect(m_folderMapperController, &FolderMapperController::previewReady,
                m_folderMapperPanel, &FolderMapperPanel::onPreviewReady);
        connect(m_folderMapperController, &FolderMapperController::error,
                m_folderMapperPanel, &FolderMapperPanel::onError);

        // Load initial mappings
        m_folderMapperController->loadMappings();
    }
}

void MainWindow::setMultiUploaderController(MultiUploaderController* controller)
{
    m_multiUploaderController = controller;

    if (m_multiUploaderController && m_multiUploaderPanel) {
        m_multiUploaderPanel->setController(controller);
        // Signal/slot connections are set up in setController()
    }
}

void MainWindow::setSmartSyncController(SmartSyncController* controller)
{
    m_smartSyncController = controller;

    if (m_smartSyncController && m_smartSyncPanel) {
        m_smartSyncPanel->setController(controller);
        // Signal/slot connections are set up in setController()
    }
}

void MainWindow::setCloudCopierController(CloudCopierController* controller)
{
    m_cloudCopierController = controller;

    if (m_cloudCopierController && m_cloudCopierPanel) {
        m_cloudCopierPanel->setController(controller);

        // Set file controller for cloud browsing
        if (m_fileController) {
            m_cloudCopierPanel->setFileController(m_fileController);
        }

        // Connect panel signals to controller slots
        connect(m_cloudCopierPanel, &CloudCopierPanel::addSourceRequested,
                m_cloudCopierController, &CloudCopierController::addSource);
        connect(m_cloudCopierPanel, &CloudCopierPanel::removeSourceRequested,
                m_cloudCopierController, &CloudCopierController::removeSource);
        connect(m_cloudCopierPanel, &CloudCopierPanel::clearSourcesRequested,
                m_cloudCopierController, &CloudCopierController::clearSources);
        connect(m_cloudCopierPanel, &CloudCopierPanel::addDestinationRequested,
                m_cloudCopierController, &CloudCopierController::addDestination);
        connect(m_cloudCopierPanel, &CloudCopierPanel::removeDestinationRequested,
                m_cloudCopierController, &CloudCopierController::removeDestination);
        connect(m_cloudCopierPanel, &CloudCopierPanel::clearDestinationsRequested,
                m_cloudCopierController, &CloudCopierController::clearDestinations);
        connect(m_cloudCopierPanel, &CloudCopierPanel::saveTemplateRequested,
                m_cloudCopierController, &CloudCopierController::saveTemplate);
        connect(m_cloudCopierPanel, &CloudCopierPanel::loadTemplateRequested,
                m_cloudCopierController, &CloudCopierController::loadTemplate);
        connect(m_cloudCopierPanel, &CloudCopierPanel::deleteTemplateRequested,
                m_cloudCopierController, &CloudCopierController::deleteTemplate);
        connect(m_cloudCopierPanel, &CloudCopierPanel::importDestinationsRequested,
                m_cloudCopierController, &CloudCopierController::importDestinationsFromFile);
        connect(m_cloudCopierPanel, &CloudCopierPanel::exportDestinationsRequested,
                m_cloudCopierController, &CloudCopierController::exportDestinationsToFile);
        connect(m_cloudCopierPanel, &CloudCopierPanel::previewCopyRequested,
                m_cloudCopierController, &CloudCopierController::previewCopy);
        connect(m_cloudCopierPanel, &CloudCopierPanel::startCopyRequested,
                m_cloudCopierController, &CloudCopierController::startCopy);
        connect(m_cloudCopierPanel, &CloudCopierPanel::pauseCopyRequested,
                m_cloudCopierController, &CloudCopierController::pauseCopy);
        connect(m_cloudCopierPanel, &CloudCopierPanel::cancelCopyRequested,
                m_cloudCopierController, &CloudCopierController::cancelCopy);
        connect(m_cloudCopierPanel, &CloudCopierPanel::clearCompletedRequested,
                m_cloudCopierController, &CloudCopierController::clearCompletedTasks);

        // NOTE: Controller→panel signal connections are established in
        // CloudCopierPanel::setController() - do not duplicate them here!

        // Validation connections (not in setController, so keep these)
        connect(m_cloudCopierPanel, &CloudCopierPanel::validateDestinationsRequested,
                m_cloudCopierController, &CloudCopierController::validateDestinations);
        connect(m_cloudCopierController, &CloudCopierController::destinationsValidated,
                m_cloudCopierPanel, &CloudCopierPanel::onDestinationsValidated);
        connect(m_cloudCopierController, &CloudCopierController::sourcesValidated,
                m_cloudCopierPanel, &CloudCopierPanel::onSourcesValidated);
    }

    // Set up DistributionPanel with MegaApi directly (not CloudCopierController)
    // This avoids duplicate completion popups from the controller
    if (m_distributionPanel) {
        MegaCustom::MegaManager& megaManager = MegaCustom::MegaManager::getInstance();
        m_distributionPanel->setMegaApi(megaManager.getMegaApi());
    }
}

void MainWindow::setDistributionController(DistributionController* controller)
{
    m_distributionController = controller;

    if (m_distributionController && m_distributionPanel) {
        m_distributionPanel->setDistributionController(controller);
        qDebug() << "MainWindow: DistributionController connected to DistributionPanel";
    }
}

void MainWindow::setWatermarkerController(WatermarkerController* controller)
{
    m_watermarkerController = controller;

    if (m_watermarkerController && m_watermarkPanel) {
        m_watermarkPanel->setController(controller);
        qDebug() << "MainWindow: WatermarkerController connected to WatermarkPanel";
    }
}

void MainWindow::applySettings()
{
    // Apply theme
    Settings& settings = Settings::instance();
    bool darkMode = settings.darkMode();

    // Load the appropriate stylesheet (light or dark)
    // This uses Application's static method to set the global app stylesheet
    Application::loadStylesheetByTheme(darkMode);

    // Apply other settings
    if (m_remoteExplorer) {
        m_remoteExplorer->setShowHidden(settings.showHiddenFiles());
    }
}

void MainWindow::showLoginDialog()
{
    if (!m_authController) {
        showError("Error", "Authentication controller not initialized");
        return;
    }

    LoginDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        QString email = dialog.email();
        QString password = dialog.password();
        bool rememberMe = dialog.rememberMe();

        // Store remember me preference and email
        Settings::instance().setRememberLogin(rememberMe);
        if (rememberMe) {
            Settings::instance().setLastEmail(email);
        }
        Settings::instance().save();

        // Attempt login
        m_authController->login(email, password);
        updateStatus("Logging in...");
    }
}

void MainWindow::showUploadDialog()
{
    QStringList files = QFileDialog::getOpenFileNames(
        this,
        "Select Files to Upload",
        Settings::instance().lastLocalPath(),
        "All Files (*.*)"
    );

    if (!files.isEmpty() && m_transferController) {
        QString remotePath = m_remoteExplorer ? m_remoteExplorer->currentPath() : "/";
        for (const QString& file : files) {
            m_transferController->uploadFile(file, remotePath);
        }
    }
}

void MainWindow::showDownloadDialog()
{
    if (!m_remoteExplorer) return;

    QStringList selectedFiles = m_remoteExplorer->selectedFiles();
    if (selectedFiles.isEmpty()) {
        showInfo("No Selection", "Please select files to download");
        return;
    }

    QString downloadPath = QFileDialog::getExistingDirectory(
        this,
        "Select Download Folder",
        Settings::instance().lastLocalPath()
    );

    if (!downloadPath.isEmpty() && m_transferController) {
        for (const QString& file : selectedFiles) {
            m_transferController->downloadFile(file, downloadPath);
        }
    }
}

void MainWindow::showTransfers()
{
    if (m_transferQueue) {
        m_transferQueue->show();
        m_transferQueue->raise();
    }
}

void MainWindow::toggleTransfers()
{
    if (m_transferQueue) {
        m_transferQueue->setVisible(!m_transferQueue->isVisible());
    }
}

void MainWindow::updateStatus(const QString& message)
{
    m_statusLabel->setText(message);
}

void MainWindow::updateTransferProgress(int progress)
{
    m_progressBar->setValue(progress);
    m_progressBar->setVisible(progress > 0 && progress < 100);
}

void MainWindow::showError(const QString& title, const QString& message)
{
    QMessageBox::critical(this, title, message);
}

void MainWindow::showInfo(const QString& title, const QString& message)
{
    QMessageBox::information(this, title, message);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (checkUnsavedChanges()) {
        saveSettings();
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    if (event->mimeData()->hasUrls() && m_isLoggedIn) {
        QStringList files;
        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                files << url.toLocalFile();
            }
        }

        if (!files.isEmpty()) {
            onFilesDropped(files, m_remoteExplorer->currentPath());
        }
    }
}

void MainWindow::setupUI()
{
    // ========================================
    // MEGA-STYLE LAYOUT
    // ========================================
    // +------------------------------------------------------------------+
    // | Menu Bar                                                         |
    // +------------------------------------------------------------------+
    // | +----------+ +--------------------------------------------------+ |
    // | |          | | Top Toolbar (breadcrumb, search, actions)        | |
    // | | MEGA     | +--------------------------------------------------+ |
    // | | SIDEBAR  | |                                                  | |
    // | |          | |          Content Area (QStackedWidget)          | |
    // | | Cloud    | |  - Cloud Browser (FileExplorer)                 | |
    // | | Drive    | |  - FolderMapperPanel                            | |
    // | | -------- | |  - MultiUploaderPanel                           | |
    // | | TOOLS    | |  - SmartSyncPanel                               | |
    // | | FolderMap| |  - Transfers                                    | |
    // | | MultiUp  | +--------------------------------------------------+ |
    // | | SmartSync| | Transfer Queue (collapsible)                    | |
    // | +----------+ +--------------------------------------------------+ |
    // +------------------------------------------------------------------+
    // | Status Bar                                                       |
    // +------------------------------------------------------------------+

    // Create remote file explorer (no local explorer - use native file dialogs)
    m_remoteExplorer = new FileExplorer(FileExplorer::Remote, this);
    m_remoteExplorer->setEnabled(false); // Disabled until login

    // Create feature panels
    m_folderMapperPanel = new FolderMapperPanel(this);
    m_multiUploaderPanel = new MultiUploaderPanel(this);
    m_smartSyncPanel = new SmartSyncPanel(this);
    m_cloudCopierPanel = new CloudCopierPanel(this);
    m_memberRegistryPanel = new MemberRegistryPanel(this);
    m_distributionPanel = new DistributionPanel(this);
    m_downloaderPanel = new DownloaderPanel(this);
    m_watermarkPanel = new WatermarkPanel(this);
    m_logViewerPanel = new LogViewerPanel(this);

    // Create transfer queue
    m_transferQueue = new TransferQueue(this);
    m_transferQueue->setObjectName("TransferQueue");

    // ========================================
    // MEGA SIDEBAR (Left side)
    // ========================================
    m_sidebar = new MegaSidebar(this);

    // Connect sidebar navigation signals
    connect(m_sidebar, &MegaSidebar::navigationItemClicked,
            this, [this](MegaSidebar::NavigationItem item) {
                onNavigationItemClicked(static_cast<int>(item));
            });

    // Connect sidebar account signals
    connect(m_sidebar, &MegaSidebar::accountSwitchRequested,
            this, &MainWindow::onAccountSwitchRequested);
    connect(m_sidebar, &MegaSidebar::addAccountRequested,
            this, &MainWindow::onAddAccountRequested);
    connect(m_sidebar, &MegaSidebar::manageAccountsRequested,
            this, &MainWindow::onManageAccountsRequested);

    // ========================================
    // TOP TOOLBAR
    // ========================================
    m_topToolbar = new TopToolbar(this);

    // Connect toolbar signals
    connect(m_topToolbar, &TopToolbar::pathSegmentClicked,
            this, &MainWindow::onBreadcrumbPathClicked);
    connect(m_topToolbar, &TopToolbar::searchRequested,
            this, &MainWindow::onGlobalSearchRequested);
    connect(m_topToolbar, &TopToolbar::searchTextChanged,
            this, &MainWindow::onSearchTextChanged);
    connect(m_topToolbar, &TopToolbar::uploadClicked,
            this, &MainWindow::onUploadFile);
    connect(m_topToolbar, &TopToolbar::downloadClicked,
            this, &MainWindow::onDownload);
    connect(m_topToolbar, &TopToolbar::newFolderClicked,
            this, &MainWindow::onNewFolder);
    connect(m_topToolbar, &TopToolbar::createFileClicked,
            this, &MainWindow::onCreateFile);
    connect(m_topToolbar, &TopToolbar::deleteClicked,
            this, &MainWindow::onDelete);
    connect(m_topToolbar, &TopToolbar::refreshClicked,
            this, &MainWindow::onRefresh);

    // ========================================
    // CONTENT STACK (Central area)
    // ========================================
    m_contentStack = new QStackedWidget(this);
    m_contentStack->setObjectName("ContentStack");

    // Add pages to stack (order matches NavigationItem enum)
    // CloudDrive uses only remote explorer (no local file browser)
    m_contentStack->addWidget(m_remoteExplorer);      // 0: CloudDrive
    m_contentStack->addWidget(m_folderMapperPanel);   // 1: FolderMapper
    m_contentStack->addWidget(m_multiUploaderPanel);  // 2: MultiUploader
    m_contentStack->addWidget(m_cloudCopierPanel);    // 3: CloudCopier
    m_contentStack->addWidget(m_smartSyncPanel);      // 4: SmartSync
    m_contentStack->addWidget(m_memberRegistryPanel); // 5: MemberRegistry
    m_contentStack->addWidget(m_distributionPanel);   // 6: Distribution
    m_contentStack->addWidget(m_watermarkPanel);      // 7: Watermark
    m_contentStack->addWidget(m_logViewerPanel);     // 8: LogViewer
    m_settingsPanel = new SettingsPanel(this);
    m_settingsPanel->loadSettings();
    m_contentStack->addWidget(m_settingsPanel);       // 9: Settings
    m_contentStack->addWidget(m_transferQueue);       // 10: Transfers
    m_contentStack->addWidget(m_downloaderPanel);    // 11: Downloader

    // Connect Downloader -> Watermark pipeline
    connect(m_downloaderPanel, &DownloaderPanel::sendToWatermark,
            m_watermarkPanel, &WatermarkPanel::addFilesFromDownloader);
    connect(m_downloaderPanel, &DownloaderPanel::sendToWatermark,
            this, [this](const QStringList& files) {
                Q_UNUSED(files);
                // Switch to Watermark panel after sending files
                m_contentStack->setCurrentWidget(m_watermarkPanel);
                m_sidebar->setActiveItem(MegaSidebar::NavigationItem::Watermark);
            });

    // Connect Watermark -> Distribution pipeline
    connect(m_watermarkPanel, &WatermarkPanel::sendToDistribution,
            m_distributionPanel, &DistributionPanel::addFilesFromWatermark);
    connect(m_watermarkPanel, &WatermarkPanel::sendToDistribution,
            this, [this](const QStringList& files) {
                Q_UNUSED(files);
                // Switch to Distribution panel after sending files
                m_contentStack->setCurrentWidget(m_distributionPanel);
                m_sidebar->setActiveItem(MegaSidebar::NavigationItem::Distribution);
            });

    // Connect MemberRegistry -> Watermark (member selection integration)
    connect(m_memberRegistryPanel, &MemberRegistryPanel::memberSelected,
            m_watermarkPanel, &WatermarkPanel::selectMember);

    // Advanced Search Panel (Tools menu only, no sidebar)
    m_advancedSearchPanel = new AdvancedSearchPanel(this);
    m_contentStack->addWidget(m_advancedSearchPanel); // 12: AdvancedSearch

    // Cross-Account Transfer Log Panel
    m_crossAccountLogPanel = new CrossAccountLogPanel(this);
    m_contentStack->addWidget(m_crossAccountLogPanel); // 13: CrossAccountTransferLog

    // Initialize cross-account transfer manager and log store
    m_transferLogStore = new TransferLogStore(this);
    m_transferLogStore->initialize();

    m_crossAccountTransferManager = new CrossAccountTransferManager(
        AccountManager::instance().sessionPool(),
        m_transferLogStore,
        this);

    m_crossAccountLogPanel->setTransferManager(m_crossAccountTransferManager);

    // Connect cross-account transfer signals for user feedback
    connect(m_crossAccountTransferManager, &CrossAccountTransferManager::transferCompleted,
            this, &MainWindow::onCrossAccountTransferCompleted);
    connect(m_crossAccountTransferManager, &CrossAccountTransferManager::transferFailed,
            this, &MainWindow::onCrossAccountTransferFailed);
    connect(m_crossAccountTransferManager, &CrossAccountTransferManager::sharedLinksWillBreak,
            this, &MainWindow::onSharedLinksWillBreak);

    // Connect transfer started/completed to update account sync status
    connect(m_crossAccountTransferManager, &CrossAccountTransferManager::transferStarted,
            this, [](const CrossAccountTransfer& transfer) {
                AccountManager::instance().setAccountSyncing(transfer.sourceAccountId, true);
                AccountManager::instance().setAccountSyncing(transfer.targetAccountId, true);
            });
    connect(m_crossAccountTransferManager, &CrossAccountTransferManager::transferCompleted,
            this, [](const CrossAccountTransfer& transfer) {
                AccountManager::instance().setAccountSyncing(transfer.sourceAccountId, false);
                AccountManager::instance().setAccountSyncing(transfer.targetAccountId, false);
            });
    connect(m_crossAccountTransferManager, &CrossAccountTransferManager::transferFailed,
            this, [](const CrossAccountTransfer& transfer) {
                AccountManager::instance().setAccountSyncing(transfer.sourceAccountId, false);
                AccountManager::instance().setAccountSyncing(transfer.targetAccountId, false);
            });

    // Quick Peek Panel (slide-out panel for browsing other accounts)
    m_quickPeekPanel = new QuickPeekPanel(this);
    m_quickPeekPanel->setSessionPool(AccountManager::instance().sessionPool());

    // Connect Quick Peek signals
    connect(m_quickPeekPanel, &QuickPeekPanel::switchToAccountRequested,
            this, &MainWindow::onAccountSwitchRequested);
    connect(m_quickPeekPanel, &QuickPeekPanel::copyToActiveRequested,
            this, &MainWindow::onQuickPeekCopyToActive);
    connect(m_quickPeekPanel, &QuickPeekPanel::panelClosed,
            this, [this]() {
                // Collapse the quick peek panel in the splitter
                if (m_centralSplitter) {
                    QList<int> sizes = m_centralSplitter->sizes();
                    if (sizes.size() >= 3) {
                        sizes[1] += sizes[2]; // Give space back to content
                        sizes[2] = 0;
                        m_centralSplitter->setSizes(sizes);
                    }
                }
            });

    // Connect sidebar quick peek signal
    connect(m_sidebar, &MegaSidebar::quickPeekRequested,
            this, &MainWindow::onQuickPeekRequested);

    // ========================================
    // RIGHT SIDE LAYOUT (Toolbar + Content)
    // ========================================
    QWidget* rightWidget = new QWidget(this);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    // Wrap toolbar in fixed-height container to prevent resize on visibility changes
    m_toolbarContainer = new QWidget(this);
    m_toolbarContainer->setFixedHeight(DpiScaler::scale(48));
    m_toolbarContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QVBoxLayout* toolbarContainerLayout = new QVBoxLayout(m_toolbarContainer);
    toolbarContainerLayout->setContentsMargins(0, 0, 0, 0);
    toolbarContainerLayout->setSpacing(0);
    toolbarContainerLayout->addWidget(m_topToolbar);

    rightLayout->addWidget(m_toolbarContainer);
    rightLayout->addWidget(m_contentStack, 1);

    // ========================================
    // CENTRAL SPLITTER (Sidebar + Right side + QuickPeek)
    // ========================================
    m_centralSplitter = new QSplitter(Qt::Horizontal, this);
    m_centralSplitter->addWidget(m_sidebar);
    m_centralSplitter->addWidget(rightWidget);
    m_centralSplitter->addWidget(m_quickPeekPanel);
    m_centralSplitter->setStretchFactor(0, 0); // Sidebar doesn't stretch
    m_centralSplitter->setStretchFactor(1, 1); // Right side stretches
    m_centralSplitter->setStretchFactor(2, 0); // Quick peek panel doesn't stretch
    m_centralSplitter->setCollapsible(0, false); // Sidebar always visible
    m_centralSplitter->setCollapsible(2, true);  // Quick peek can be collapsed

    // Set initial splitter sizes (quick peek hidden initially)
    m_centralSplitter->setSizes({240, 960, 0});

    // Set central widget
    setCentralWidget(m_centralSplitter);

    // Set initial navigation state
    m_sidebar->setActiveItem(MegaSidebar::NavigationItem::CloudDrive);
    m_contentStack->setCurrentIndex(0);

    // ========================================
    // SEARCH INDEX AND PANEL
    // ========================================
    m_searchIndex = new CloudSearchIndex(this);
    m_searchPanel = new SearchResultsPanel(this);
    m_searchPanel->setSearchIndex(m_searchIndex);
    m_searchPanel->hide(); // Initially hidden, shown on search focus

    // Connect Advanced Search Panel to search index
    if (m_advancedSearchPanel) {
        m_advancedSearchPanel->setSearchIndex(m_searchIndex);

        // Connect navigation signal
        connect(m_advancedSearchPanel, &AdvancedSearchPanel::navigateToPath,
                this, &MainWindow::onSearchResultActivated);

        // Connect rename signal to FileController
        connect(m_advancedSearchPanel, &AdvancedSearchPanel::renameRequested,
                this, [this](const QString& path, const QString& newName) {
            if (m_fileController) {
                qDebug() << "MainWindow: Rename request from AdvancedSearchPanel:" << path << "->" << newName;
                m_fileController->renameRemote(path, newName);
            }
        });
    }

    // Setup account keyboard shortcuts
    setupAccountShortcuts();
}

void MainWindow::createActions()
{
    // File menu actions
    m_newFolderAction = new QAction(QIcon(":/icons/folder-plus.svg"), "New &Folder", this);
    m_newFolderAction->setShortcut(QKeySequence("Ctrl+Shift+N"));
    connect(m_newFolderAction, &QAction::triggered, this, &MainWindow::onNewFolder);

    m_uploadFileAction = new QAction(QIcon(":/icons/upload.svg"), "&Upload Files...", this);
    m_uploadFileAction->setShortcut(QKeySequence("Ctrl+U"));
    connect(m_uploadFileAction, &QAction::triggered, this, &MainWindow::onUploadFile);

    m_uploadFolderAction = new QAction(QIcon(":/icons/folder.svg"), "Upload Fol&der...", this);
    connect(m_uploadFolderAction, &QAction::triggered, this, &MainWindow::onUploadFolder);

    m_downloadAction = new QAction(QIcon(":/icons/download.svg"), "&Download", this);
    m_downloadAction->setShortcut(QKeySequence("Ctrl+D"));
    connect(m_downloadAction, &QAction::triggered, this, &MainWindow::onDownload);

    m_deleteAction = new QAction(QIcon(":/icons/trash-2.svg"), "De&lete", this);
    m_deleteAction->setShortcut(QKeySequence::Delete);
    connect(m_deleteAction, &QAction::triggered, this, &MainWindow::onDelete);

    m_renameAction = new QAction(QIcon(":/icons/edit.svg"), "&Rename", this);
    m_renameAction->setShortcut(QKeySequence("F2"));
    connect(m_renameAction, &QAction::triggered, this, &MainWindow::onRename);

    m_exitAction = new QAction(QIcon(":/icons/x.svg"), "E&xit", this);
    m_exitAction->setShortcut(QKeySequence("Ctrl+Q"));
    connect(m_exitAction, &QAction::triggered, this, &MainWindow::onExit);

    // Edit menu actions
    m_cutAction = new QAction(QIcon(":/icons/scissors.svg"), "Cu&t", this);
    m_cutAction->setShortcut(QKeySequence::Cut);
    connect(m_cutAction, &QAction::triggered, this, &MainWindow::onCut);

    m_copyAction = new QAction(QIcon(":/icons/copy.svg"), "&Copy", this);
    m_copyAction->setShortcut(QKeySequence::Copy);
    connect(m_copyAction, &QAction::triggered, this, &MainWindow::onCopy);

    m_pasteAction = new QAction(QIcon(":/icons/clipboard.svg"), "&Paste", this);
    m_pasteAction->setShortcut(QKeySequence::Paste);
    connect(m_pasteAction, &QAction::triggered, this, &MainWindow::onPaste);

    m_selectAllAction = new QAction("Select &All", this);
    m_selectAllAction->setShortcut(QKeySequence::SelectAll);
    connect(m_selectAllAction, &QAction::triggered, this, &MainWindow::onSelectAll);

    m_findAction = new QAction(QIcon(":/icons/search.svg"), "&Find...", this);
    m_findAction->setShortcut(QKeySequence::Find);
    connect(m_findAction, &QAction::triggered, this, &MainWindow::onFind);

    // View menu actions
    m_refreshAction = new QAction(QIcon(":/icons/refresh-cw.svg"), "&Refresh", this);
    m_refreshAction->setShortcut(QKeySequence::Refresh);
    connect(m_refreshAction, &QAction::triggered, this, &MainWindow::onRefresh);

    m_showHiddenAction = new QAction("Show &Hidden Files", this);
    m_showHiddenAction->setShortcut(QKeySequence("Ctrl+H"));
    m_showHiddenAction->setCheckable(true);
    m_showHiddenAction->setChecked(false);
    connect(m_showHiddenAction, &QAction::triggered, this, &MainWindow::onShowHidden);

    m_sortByNameAction = new QAction("Sort by &Name", this);
    connect(m_sortByNameAction, &QAction::triggered, this, &MainWindow::onSortByName);

    m_sortBySizeAction = new QAction("Sort by &Size", this);
    connect(m_sortBySizeAction, &QAction::triggered, this, &MainWindow::onSortBySize);

    m_sortByDateAction = new QAction("Sort by &Date", this);
    connect(m_sortByDateAction, &QAction::triggered, this, &MainWindow::onSortByDate);

    m_showTransfersAction = new QAction(QIcon(":/icons/hard-drive.svg"), "Show &Transfers", this);
    m_showTransfersAction->setCheckable(true);
    m_showTransfersAction->setChecked(true);
    connect(m_showTransfersAction, &QAction::triggered, this, &MainWindow::toggleTransfers);

    // Tools menu actions
    m_advancedSearchAction = new QAction(QIcon(":/icons/search.svg"), "&Advanced Search...", this);
    m_advancedSearchAction->setShortcut(QKeySequence("Ctrl+Shift+F"));
    connect(m_advancedSearchAction, &QAction::triggered, this, &MainWindow::onAdvancedSearch);

    m_transferLogAction = new QAction(QIcon(":/icons/copy.svg"), "&Cross-Account Transfer Log...", this);
    m_transferLogAction->setShortcut(QKeySequence("Ctrl+Shift+L"));
    connect(m_transferLogAction, &QAction::triggered, this, &MainWindow::onShowTransferLog);

    m_settingsAction = new QAction(QIcon(":/icons/settings.svg"), "&Settings...", this);
    m_settingsAction->setShortcut(QKeySequence("Ctrl+,"));
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::onSettings);

    // Login/logout actions
    m_loginAction = new QAction(QIcon(":/icons/user.svg"), "&Login...", this);
    connect(m_loginAction, &QAction::triggered, this, &MainWindow::showLoginDialog);

    m_logoutAction = new QAction(QIcon(":/icons/log-out.svg"), "Log&out", this);
    connect(m_logoutAction, &QAction::triggered, [this]() {
        if (m_authController) {
            m_authController->logout();
        }
    });
}

void MainWindow::createMenus()
{
    // Apply widget-specific stylesheet to menuBar (Qt6 priority - overrides platform defaults)
    // NOTE: Must use rgba() format for alpha transparency - hex #RRGGBBAA is NOT supported in Qt stylesheets
    auto& tm = ThemeManager::instance();

    auto updateMenuStyles = [this, &tm]() {
        QString menuBarStyle = QString(R"(
            QMenuBar {
                background-color: %1;
                border-bottom: 1px solid %2;
                padding: 4px;
            }
            QMenuBar::item {
                background-color: transparent;
                padding: 6px 12px;
                border-radius: 4px;
            }
            QMenuBar::item:selected {
                background-color: %3;
                color: %4;
            }
        )")
        .arg(tm.surfacePrimary().name())
        .arg(tm.borderSubtle().name())
        .arg(tm.surface2().name())
        .arg(tm.brandDefault().name());

        menuBar()->setStyleSheet(menuBarStyle);
    };

    updateMenuStyles();

    // Connect to theme changes for dynamic updates
    connect(&tm, &ThemeManager::themeChanged, this, updateMenuStyles);

    // Common style for all dropdown menus
    // NOTE: Must use rgba() format for alpha transparency - hex #RRGGBBAA is NOT supported in Qt stylesheets
    QString menuStyle = QString(R"(
        QMenu {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 8px;
            padding: 4px;
        }
        QMenu::item {
            padding: 8px 24px;
            border-radius: 4px;
        }
        QMenu::item:selected {
            background-color: %3;
            color: %4;
        }
        QMenu::item:disabled {
            color: %5;
        }
        QMenu::separator {
            height: 1px;
            background-color: %6;
            margin: 4px 8px;
        }
    )")
    .arg(tm.surfacePrimary().name())
    .arg(tm.borderSubtle().name())
    .arg(tm.surface2().name())
    .arg(tm.textPrimary().name())
    .arg(tm.textDisabled().name())
    .arg(tm.borderSubtle().name());

    // File menu
    m_fileMenu = menuBar()->addMenu("&File");
    m_fileMenu->setStyleSheet(menuStyle);
    m_fileMenu->addAction(m_loginAction);
    m_fileMenu->addAction(m_logoutAction);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_newFolderAction);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_uploadFileAction);
    m_fileMenu->addAction(m_uploadFolderAction);
    m_fileMenu->addAction(m_downloadAction);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_deleteAction);
    m_fileMenu->addAction(m_renameAction);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_exitAction);

    // Edit menu
    m_editMenu = menuBar()->addMenu("&Edit");
    m_editMenu->setStyleSheet(menuStyle);
    m_editMenu->addAction(m_cutAction);
    m_editMenu->addAction(m_copyAction);
    m_editMenu->addAction(m_pasteAction);
    m_editMenu->addSeparator();
    m_editMenu->addAction(m_selectAllAction);
    m_editMenu->addSeparator();
    m_editMenu->addAction(m_findAction);

    // View menu
    m_viewMenu = menuBar()->addMenu("&View");
    m_viewMenu->setStyleSheet(menuStyle);
    m_viewMenu->addAction(m_refreshAction);
    m_viewMenu->addAction(m_showHiddenAction);
    m_viewMenu->addSeparator();
    QMenu* sortMenu = m_viewMenu->addMenu("&Sort By");
    sortMenu->setStyleSheet(menuStyle);
    sortMenu->addAction(m_sortByNameAction);
    sortMenu->addAction(m_sortBySizeAction);
    sortMenu->addAction(m_sortByDateAction);
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_showTransfersAction);

    // Tools menu
    m_toolsMenu = menuBar()->addMenu("&Tools");
    m_toolsMenu->setStyleSheet(menuStyle);
    m_toolsMenu->addAction(m_advancedSearchAction);
    m_toolsMenu->addAction(m_transferLogAction);
    m_toolsMenu->addSeparator();
    m_toolsMenu->addAction(m_settingsAction);

    // Help menu
    m_helpMenu = menuBar()->addMenu("&Help");
    m_helpMenu->setStyleSheet(menuStyle);

    m_shortcutsAction = new QAction("&Keyboard Shortcuts...", this);
    m_shortcutsAction->setShortcut(QKeySequence("F1"));
    connect(m_shortcutsAction, &QAction::triggered, this, &MainWindow::onKeyboardShortcuts);
    m_helpMenu->addAction(m_shortcutsAction);

    // Hide the native menu bar - actions are accessible via toolbar/sidebar
    // Keyboard shortcuts still work even with the menu bar hidden
    menuBar()->hide();
}

void MainWindow::createStatusBar()
{
    m_statusBar = statusBar();

    // Connection indicator (green/red dot)
    m_connectionIndicator = new QLabel(this);
    m_connectionIndicator->setObjectName("ConnectionIndicator");
    m_connectionIndicator->setFixedSize(10, 10);
    m_connectionIndicator->setStyleSheet(
        "QLabel { background-color: #E0E0E0; border-radius: 5px; }"
    );
    m_statusBar->addWidget(m_connectionIndicator);

    // Connection label
    m_connectionLabel = new QLabel("Disconnected");
    m_connectionLabel->setObjectName("ConnectionLabel");
    m_connectionLabel->setStyleSheet("QLabel { color: #666666; margin-right: 16px; }");
    m_statusBar->addWidget(m_connectionLabel);

    // Status label (stretch)
    m_statusLabel = new QLabel("Ready");
    m_statusLabel->setObjectName("StatusLabel");
    m_statusBar->addWidget(m_statusLabel, 1);

    // Progress bar
    m_progressBar = new QProgressBar();
    m_progressBar->setObjectName("StatusProgressBar");
    m_progressBar->setMaximumWidth(200);
    m_progressBar->setVisible(false);
    m_statusBar->addWidget(m_progressBar);

    // Upload speed
    m_uploadSpeedLabel = new QLabel(this);
    m_uploadSpeedLabel->setObjectName("UploadSpeedLabel");
    m_uploadSpeedLabel->setText(QString::fromUtf8("↑ 0 B/s"));
    m_uploadSpeedLabel->setStyleSheet("QLabel { color: #666666; margin-right: 8px; }");
    m_statusBar->addPermanentWidget(m_uploadSpeedLabel);

    // Download speed
    m_downloadSpeedLabel = new QLabel(this);
    m_downloadSpeedLabel->setObjectName("DownloadSpeedLabel");
    m_downloadSpeedLabel->setText(QString::fromUtf8("↓ 0 B/s"));
    m_downloadSpeedLabel->setStyleSheet("QLabel { color: #666666; margin-right: 16px; }");
    m_statusBar->addPermanentWidget(m_downloadSpeedLabel);

    // User label
    m_userLabel = new QLabel("Not logged in");
    m_userLabel->setObjectName("UserLabel");
    m_userLabel->setStyleSheet("QLabel { color: #333333; font-weight: 500; }");
    m_statusBar->addPermanentWidget(m_userLabel);
}

void MainWindow::updateActions()
{
    // Update action states based on login status
    m_loginAction->setEnabled(!m_isLoggedIn);
    m_logoutAction->setEnabled(m_isLoggedIn);
    m_uploadFileAction->setEnabled(m_isLoggedIn);
    m_uploadFolderAction->setEnabled(m_isLoggedIn);
    m_downloadAction->setEnabled(m_isLoggedIn);
    m_newFolderAction->setEnabled(m_isLoggedIn);
    m_deleteAction->setEnabled(m_isLoggedIn);
    m_renameAction->setEnabled(m_isLoggedIn);
}

void MainWindow::connectSignals()
{
    // Remote file explorer signals
    if (m_remoteExplorer) {
        connect(m_remoteExplorer, &FileExplorer::filesDropped,
                this, [this](const QStringList& files) {
                    onFilesDropped(files, "remote");
                });
        connect(m_remoteExplorer, &FileExplorer::pathChanged,
                this, [this](const QString& path) {
                    // Update breadcrumb when remote explorer navigates
                    if (m_topToolbar) {
                        m_topToolbar->setCurrentPath(path);
                    }
                });

        // Cross-account transfer signals
        connect(m_remoteExplorer, &FileExplorer::crossAccountCopyRequested,
                this, &MainWindow::onCrossAccountCopy);
        connect(m_remoteExplorer, &FileExplorer::crossAccountMoveRequested,
                this, &MainWindow::onCrossAccountMove);
    }

    // ========================================
    // INSTANT SEARCH PANEL SIGNALS
    // ========================================
    if (m_searchPanel && m_topToolbar) {
        // Live search - text changes trigger instant search
        connect(m_topToolbar, &TopToolbar::searchTextChanged,
                m_searchPanel, &SearchResultsPanel::setQuery);

        // Show panel when user starts typing in search field
        connect(m_topToolbar, &TopToolbar::searchTextChanged,
                this, [this](const QString& text) {
                    // Only show panel if logged in and index is ready (not building)
                    if (!text.isEmpty() && m_isLoggedIn &&
                        m_searchIndex && !m_searchIndex->isBuilding()) {
                        showSearchPanel();
                    } else if (text.isEmpty()) {
                        hideSearchPanel();
                    }
                });

        // Hide panel when search field loses focus
        connect(m_topToolbar, &TopToolbar::searchFocusLost,
                this, &MainWindow::hideSearchPanel);

        // Navigate to selected result
        connect(m_searchPanel, &SearchResultsPanel::resultActivated,
                this, &MainWindow::onSearchResultActivated);

        // Note: SearchResultsPanel handles keyboard via keyPressEvent when it has focus
    }

    // Transfer controller speed updates
    if (m_transferController) {
        connect(m_transferController, &TransferController::globalSpeedUpdate,
                this, [this](qint64 uploadSpeed, qint64 downloadSpeed) {
                    // Helper function to format bytes
                    auto formatBytes = [](qint64 bytes) -> QString {
                        const qint64 KB = 1024;
                        const qint64 MB = KB * 1024;
                        const qint64 GB = MB * 1024;
                        if (bytes >= GB) {
                            return QString("%1 GB").arg(bytes / GB);
                        } else if (bytes >= MB) {
                            return QString("%1 MB").arg(bytes / MB);
                        } else if (bytes >= KB) {
                            return QString("%1 KB").arg(bytes / KB);
                        } else {
                            return QString("%1 B").arg(bytes);
                        }
                    };

                    if (m_uploadSpeedLabel) {
                        m_uploadSpeedLabel->setText(QString::fromUtf8("↑ %1/s").arg(formatBytes(uploadSpeed)));
                    }
                    if (m_downloadSpeedLabel) {
                        m_downloadSpeedLabel->setText(QString::fromUtf8("↓ %1/s").arg(formatBytes(downloadSpeed)));
                    }
                });
    }
}

void MainWindow::loadSettings()
{
    QSettings settings;
    settings.beginGroup("MainWindow");

    // Restore geometry
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("state").toByteArray());

    // Restore splitter state
    if (m_centralSplitter) {
        m_centralSplitter->restoreState(settings.value("splitter").toByteArray());
    }

    settings.endGroup();
}

void MainWindow::saveSettings()
{
    QSettings settings;
    settings.beginGroup("MainWindow");

    // Save geometry
    settings.setValue("geometry", saveGeometry());
    settings.setValue("state", saveState());

    // Save splitter state
    if (m_centralSplitter) {
        settings.setValue("splitter", m_centralSplitter->saveState());
    }

    settings.endGroup();
}

bool MainWindow::checkUnsavedChanges()
{
    if (m_transferController && m_transferController->hasActiveTransfers()) {
        int ret = QMessageBox::question(
            this,
            "Active Transfers",
            "There are active transfers. Do you want to quit anyway?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );

        return ret == QMessageBox::Yes;
    }

    return true;
}

// Slot implementations
void MainWindow::onNewFolder()
{
    // Create new folder in the remote explorer
    if (m_remoteExplorer && m_isLoggedIn) {
        m_remoteExplorer->createNewFolder();
    }
}

void MainWindow::onCreateFile()
{
    // Create new file in the remote explorer
    if (m_remoteExplorer && m_isLoggedIn) {
        m_remoteExplorer->createNewFile();
    }
}

void MainWindow::onUploadFile()
{
    showUploadDialog();
}

void MainWindow::onUploadFolder()
{
    QString folder = QFileDialog::getExistingDirectory(
        this,
        "Select Folder to Upload",
        Settings::instance().lastLocalPath()
    );

    if (!folder.isEmpty() && m_transferController) {
        QString remotePath = m_remoteExplorer ? m_remoteExplorer->currentPath() : "/";
        m_transferController->uploadFolder(folder, remotePath);
    }
}

void MainWindow::onDownload()
{
    showDownloadDialog();
}

void MainWindow::onDelete()
{
    if (m_remoteExplorer && m_remoteExplorer->hasSelection()) {
        QStringList selected = m_remoteExplorer->selectedFiles();
        int count = selected.count();

        QString message = count == 1
            ? QString("Are you sure you want to delete '%1'?").arg(QFileInfo(selected.first()).fileName())
            : QString("Are you sure you want to delete %1 items?").arg(count);

        QMessageBox::StandardButton reply = QMessageBox::question(
            this, "Confirm Delete", message,
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

        if (reply == QMessageBox::Yes) {
            m_remoteExplorer->deleteSelected();
        }
    }
}

void MainWindow::onRename()
{
    if (m_remoteExplorer && m_remoteExplorer->hasSelection()) {
        m_remoteExplorer->renameSelected();
    }
}

void MainWindow::onExit()
{
    close();
}

void MainWindow::onRefresh()
{
    if (m_remoteExplorer && m_isLoggedIn) {
        m_remoteExplorer->refresh();
    }
}

void MainWindow::onFilesDropped(const QStringList& files, const QString& target)
{
    if (m_transferController && m_isLoggedIn) {
        for (const QString& file : files) {
            m_transferController->uploadFile(file, target);
        }
    }
}

void MainWindow::onTransferStarted(const QString& file)
{
    updateStatus(QString("Transferring: %1").arg(file));
}

void MainWindow::onTransferProgress(const QString& file, qint64 bytesTransferred, qint64 totalBytes)
{
    if (totalBytes > 0) {
        int progress = static_cast<int>((bytesTransferred * 100) / totalBytes);
        updateTransferProgress(progress);
    }
}

void MainWindow::onTransferCompleted(const QString& file)
{
    updateStatus(QString("Completed: %1").arg(file));
    updateTransferProgress(100);
}

void MainWindow::onTransferFailed(const QString& file, const QString& error)
{
    showError("Transfer Failed", QString("%1: %2").arg(file, error));
    updateTransferProgress(0);
}

// ========================================
// MEGA REDESIGN: Navigation Slots
// ========================================

void MainWindow::onNavigationItemClicked(int item)
{
    // Map NavigationItem enum to content stack index
    // CloudDrive=0, FolderMapper=1, MultiUploader=2, CloudCopier=3, SmartSync=4,
    // MemberRegistry=5, Distribution=6, Settings=7, Transfers=8
    int stackIndex = item;

    // Switch content
    if (m_contentStack && stackIndex >= 0 && stackIndex < m_contentStack->count()) {
        m_contentStack->setCurrentIndex(stackIndex);
    }

    // Show/hide TopToolbar based on context - only show for Cloud Drive
    if (m_topToolbar) {
        bool isCloudDrive = (item == static_cast<int>(MegaSidebar::NavigationItem::CloudDrive));
        m_topToolbar->setVisible(isCloudDrive);
    }

    qDebug() << "Navigation item clicked:" << item;
}

void MainWindow::onBreadcrumbPathClicked(const QString& path)
{
    // Navigate to path from breadcrumb
    if (m_remoteExplorer && m_isLoggedIn) {
        m_remoteExplorer->navigateTo(path);
        // Update the breadcrumb to reflect the new path
        if (m_topToolbar) {
            m_topToolbar->setCurrentPath(path);
        }
    }

    qDebug() << "Breadcrumb path clicked:" << path;
}

void MainWindow::onSearchTextChanged(const QString& text)
{
    // Apply local search filter to the remote explorer
    if (m_remoteExplorer && m_isLoggedIn) {
        m_remoteExplorer->setSearchFilter(text);
    }

    if (text.isEmpty()) {
        updateStatus("Ready");
    } else {
        updateStatus(QString("Filtering: %1").arg(text));
    }
    qDebug() << "Search filter applied:" << text;
}

void MainWindow::onGlobalSearchRequested(const QString& text)
{
    if (text.isEmpty()) {
        return;
    }

    // Perform global search on MEGA cloud
    if (m_fileController && m_isLoggedIn) {
        updateStatus(QString("Searching for: %1...").arg(text));
        m_fileController->searchRemote(text);
    } else {
        updateStatus("Login required for global search");
    }
    qDebug() << "Global search requested:" << text;
}

void MainWindow::onSearchResultsReceived(const QVariantList& results)
{
    qDebug() << "Search results received:" << results.size() << "items";

    // Display results in the remote explorer
    if (m_remoteExplorer) {
        m_remoteExplorer->showSearchResults(results);
    }

    if (results.isEmpty()) {
        updateStatus("No results found");
    } else {
        updateStatus(QString("Found %1 result(s)").arg(results.size()));
    }
}

void MainWindow::onSettings()
{
    // Switch to settings panel
    if (m_sidebar) {
        m_sidebar->setActiveItem(MegaSidebar::NavigationItem::Settings);
    }
    if (m_contentStack) {
        m_contentStack->setCurrentIndex(static_cast<int>(MegaSidebar::NavigationItem::Settings));
    }
    if (m_topToolbar) {
        m_topToolbar->setVisible(false);
    }
}

void MainWindow::onAdvancedSearch()
{
    // Switch to advanced search panel (Tools menu only, no sidebar highlight)
    if (m_sidebar) {
        m_sidebar->clearActiveItem();  // No sidebar item for this panel
    }
    if (m_contentStack && m_advancedSearchPanel) {
        m_contentStack->setCurrentWidget(m_advancedSearchPanel);
    }
    if (m_topToolbar) {
        m_topToolbar->setVisible(false);  // Hide toolbar for this panel
    }
}

void MainWindow::onLoginStatusChanged(bool loggedIn)
{
    m_isLoggedIn = loggedIn;
    updateActions();

    // Update sidebar login state
    if (m_sidebar) {
        m_sidebar->setLoggedIn(loggedIn);
    }

    // Update toolbar actions
    if (m_topToolbar) {
        m_topToolbar->setActionsEnabled(loggedIn);
    }

    if (loggedIn) {
        // Update user label
        if (m_authController) {
            m_userLabel->setText(m_authController->currentUser());
        }
        m_connectionLabel->setText("Connected");
        m_connectionLabel->setStyleSheet("QLabel { color: #22C55E; }");
        m_connectionIndicator->setStyleSheet(
            "QLabel { background-color: #22C55E; border-radius: 5px; }"
        );

        // Enable remote explorer
        if (m_remoteExplorer) {
            m_remoteExplorer->setEnabled(true);
            m_remoteExplorer->refresh();
        }

        // Fetch storage info for sidebar
        if (m_fileController) {
            m_fileController->getStorageInfo();

            // Build search index after login for instant search
            if (m_searchIndex) {
                m_fileController->buildSearchIndex(m_searchIndex);
            }
        }
    } else {
        m_userLabel->setText("Not logged in");
        m_connectionLabel->setText("Disconnected");
        m_connectionLabel->setStyleSheet("QLabel { color: #EF4444; }");
        m_connectionIndicator->setStyleSheet(
            "QLabel { background-color: #E0E0E0; border-radius: 5px; }"
        );

        // Disable remote explorer
        if (m_remoteExplorer) {
            m_remoteExplorer->setEnabled(false);
            m_remoteExplorer->clear();
        }

        // Clear search index on logout
        if (m_searchIndex) {
            m_searchIndex->clear();
        }
    }
}

void MainWindow::onStorageInfoReceived(qint64 usedBytes, qint64 totalBytes)
{
    qDebug() << "Storage info received - used:" << usedBytes << "total:" << totalBytes;
    if (m_sidebar) {
        m_sidebar->setStorageInfo(usedBytes, totalBytes);
    }
}

// ========================================
// INSTANT SEARCH PANEL SLOTS
// ========================================

void MainWindow::showSearchPanel()
{
    if (!m_searchPanel || !m_topToolbar) {
        return;
    }

    // Position panel below search field
    QPoint globalPos = m_topToolbar->searchWidgetGlobalPos();

    // Set minimum width to match search field or a reasonable default
    QRect searchGeom = m_topToolbar->searchWidgetGeometry();
    int panelWidth = qMax(400, searchGeom.width());
    m_searchPanel->setMinimumWidth(panelWidth);

    // Move and show the panel
    m_searchPanel->move(globalPos);
    m_searchPanel->show();
    m_searchPanel->raise();
}

void MainWindow::hideSearchPanel()
{
    if (m_searchPanel) {
        // Small delay to allow click events to process first
        QTimer::singleShot(150, this, [this]() {
            if (m_searchPanel && !m_searchPanel->underMouse()) {
                m_searchPanel->hide();
            }
        });
    }
}

void MainWindow::onSearchResultActivated(const QString& handle, const QString& path, bool isFolder)
{
    qDebug() << "Search result activated - handle:" << handle << "path:" << path << "isFolder:" << isFolder;

    // Hide the search panel
    if (m_searchPanel) {
        m_searchPanel->hide();
    }

    // Navigate to the result
    if (m_remoteExplorer && m_isLoggedIn) {
        if (isFolder) {
            // Navigate into the folder
            m_remoteExplorer->navigateTo(path);
            if (m_topToolbar) {
                m_topToolbar->setCurrentPath(path);
            }
        } else {
            // Navigate to parent folder and select the file
            QString parentPath = path.left(path.lastIndexOf('/'));
            if (parentPath.isEmpty()) {
                parentPath = "/";
            }
            m_remoteExplorer->navigateTo(parentPath);
            if (m_topToolbar) {
                m_topToolbar->setCurrentPath(parentPath);
            }
            // Note: File selection by handle could be added to FileExplorer
            // For now, navigation to the parent folder is sufficient
            Q_UNUSED(handle);
        }
    }

    updateStatus(QString("Navigated to: %1").arg(path));
}

// ========================================
// ACCOUNT MANAGEMENT
// ========================================

void MainWindow::setupAccountShortcuts()
{
    // Ctrl+Tab - cycle to next account
    QShortcut* nextAccount = new QShortcut(QKeySequence("Ctrl+Tab"), this);
    connect(nextAccount, &QShortcut::activated, this, &MainWindow::cycleToNextAccount);

    // Ctrl+Shift+Tab - cycle to previous account
    QShortcut* prevAccount = new QShortcut(QKeySequence("Ctrl+Shift+Tab"), this);
    connect(prevAccount, &QShortcut::activated, this, &MainWindow::cycleToPreviousAccount);

    // Ctrl+Shift+A - show account switcher
    QShortcut* showSwitcher = new QShortcut(QKeySequence("Ctrl+Shift+A"), this);
    connect(showSwitcher, &QShortcut::activated, this, &MainWindow::showAccountSwitcher);

    // Connect to AccountManager signals
    AccountManager& mgr = AccountManager::instance();
    connect(&mgr, &AccountManager::accountSwitched, this, &MainWindow::onAccountSwitched);
    connect(&mgr, &AccountManager::loginRequired, this, &MainWindow::onLoginRequired);

    // Check if there's already an active logged-in account (session might have been restored
    // before MainWindow was constructed, so we missed the accountSwitched signal)
    // Use QTimer::singleShot to defer until after all UI is constructed
    QTimer::singleShot(0, this, [this]() {
        AccountManager& mgr = AccountManager::instance();
        QString activeId = mgr.activeAccountId();
        if (!activeId.isEmpty() && mgr.isLoggedIn(activeId)) {
            qDebug() << "MainWindow: Found already-active account" << activeId << "- initializing UI state";
            // Manually trigger the account switched handler to initialize UI state
            onAccountSwitched(activeId);
        }
    });
}

void MainWindow::cycleToNextAccount()
{
    AccountManager& mgr = AccountManager::instance();
    QList<MegaAccount> accounts = mgr.allAccounts();

    if (accounts.size() < 2) {
        return; // No point in cycling with 0 or 1 accounts
    }

    QString currentId = mgr.activeAccountId();
    int currentIndex = -1;

    for (int i = 0; i < accounts.size(); ++i) {
        if (accounts[i].id == currentId) {
            currentIndex = i;
            break;
        }
    }

    int nextIndex = (currentIndex + 1) % accounts.size();
    mgr.switchToAccount(accounts[nextIndex].id);
}

void MainWindow::cycleToPreviousAccount()
{
    AccountManager& mgr = AccountManager::instance();
    QList<MegaAccount> accounts = mgr.allAccounts();

    if (accounts.size() < 2) {
        return;
    }

    QString currentId = mgr.activeAccountId();
    int currentIndex = -1;

    for (int i = 0; i < accounts.size(); ++i) {
        if (accounts[i].id == currentId) {
            currentIndex = i;
            break;
        }
    }

    int prevIndex = (currentIndex - 1 + accounts.size()) % accounts.size();
    mgr.switchToAccount(accounts[prevIndex].id);
}

void MainWindow::showAccountSwitcher()
{
    if (m_sidebar) {
        m_sidebar->showAccountSwitcher();
    }
}

void MainWindow::onAccountSwitchRequested(const QString& accountId)
{
    // Show switching feedback
    MegaAccount account = AccountManager::instance().getAccount(accountId);
    QString accountName = account.displayName.isEmpty() ? account.email : account.displayName;

    updateStatus(QString("Switching to %1...").arg(accountName));
    m_progressBar->setRange(0, 0);  // Indeterminate progress
    m_progressBar->setVisible(true);

    // Perform the switch
    AccountManager::instance().switchToAccount(accountId);
}

void MainWindow::onAccountSwitched(const QString& accountId)
{
    qDebug() << "MainWindow: Switched to account" << accountId;

    // Hide the switching progress indicator
    m_progressBar->setRange(0, 100);  // Reset to determinate mode
    m_progressBar->setVisible(false);

    // Check if the account is logged in via AccountManager
    bool isLoggedIn = AccountManager::instance().isLoggedIn(accountId);

    // Update the global logged-in state if this account is logged in
    if (isLoggedIn && !m_isLoggedIn) {
        m_isLoggedIn = true;
        updateActions();
    }

    // Update sidebar display and login state
    if (m_sidebar) {
        m_sidebar->updateAccountDisplay();
        m_sidebar->setLoggedIn(isLoggedIn);
    }

    // Update toolbar actions
    if (m_topToolbar) {
        m_topToolbar->setActionsEnabled(isLoggedIn);
    }

    // Get the account info
    const MegaAccount* account = AccountManager::instance().activeAccount();
    if (account) {
        // Update window title
        QString title = "MegaCustom";
        if (!account->displayName.isEmpty()) {
            title += QString(" - %1").arg(account->displayName);
        } else {
            title += QString(" - %1").arg(account->email);
        }
        setWindowTitle(title);

        // Update user label in status bar
        if (m_userLabel) {
            m_userLabel->setText(account->email);
        }
    }

    // Refresh file explorer with new account's data
    if (m_remoteExplorer && isLoggedIn) {
        qDebug() << "MainWindow: Enabling and refreshing file explorer for account" << accountId;
        m_remoteExplorer->setEnabled(true);  // Enable the explorer (was disabled by default)
        m_remoteExplorer->refresh();
    }

    // Update sidebar storage info from account data
    if (account && m_sidebar) {
        m_sidebar->setStorageInfo(account->storageUsed, account->storageTotal);
    }

    QString accountName = account ? (account->displayName.isEmpty() ? account->email : account->displayName) : accountId;
    updateStatus(QString("Switched to %1").arg(accountName));
}

void MainWindow::onLoginRequired(const QString& accountId)
{
    qDebug() << "MainWindow: Login required for account" << accountId;

    // Guard against multiple login dialogs
    if (m_loginDialogShowing) {
        qDebug() << "MainWindow: Login dialog already showing, skipping";
        return;
    }

    // Hide progress bar since we're waiting for user input
    m_progressBar->setVisible(false);

    // Get account info
    MegaAccount account = AccountManager::instance().getAccount(accountId);
    QString accountEmail = account.email;

    updateStatus(QString("Login required for %1").arg(accountEmail));

    // Show login dialog with pre-filled email
    m_loginDialogShowing = true;
    m_pendingLoginAccountId = accountId;

    LoginDialog dialog(this);
    dialog.setEmail(accountEmail);
    dialog.setWindowTitle(QString("Login - %1").arg(accountEmail));

    if (dialog.exec() == QDialog::Accepted) {
        QString email = dialog.email();
        QString password = dialog.password();
        bool rememberMe = dialog.rememberMe();

        // Store remember preference
        Settings::instance().setRememberLogin(rememberMe);
        if (rememberMe) {
            Settings::instance().setLastEmail(email);
        }
        Settings::instance().save();

        // Login via AuthController (same as showLoginDialog)
        if (m_authController) {
            m_authController->login(email, password);
            updateStatus("Logging in...");
        }
    }

    m_loginDialogShowing = false;
    m_pendingLoginAccountId.clear();
}

void MainWindow::onAddAccountRequested()
{
    // Show login dialog for adding a new account
    showLoginDialog();
}

void MainWindow::onManageAccountsRequested()
{
    AccountManagerDialog dialog(this);
    dialog.exec();

    // Refresh UI after dialog closes
    if (m_sidebar) {
        m_sidebar->updateAccountDisplay();
    }
}

// ========================================
// CROSS-ACCOUNT TRANSFERS
// ========================================

void MainWindow::onCrossAccountCopy(const QStringList& paths, const QString& targetAccountId)
{
    if (!m_crossAccountTransferManager) {
        showError("Error", "Cross-account transfer manager not initialized");
        return;
    }

    QString sourceAccountId = AccountManager::instance().activeAccountId();

    // Get target account info and MegaApi for browsing
    MegaAccount targetAccount = AccountManager::instance().getAccount(targetAccountId);
    QString targetAccountName = targetAccount.displayName.isEmpty() ? targetAccount.email : targetAccount.displayName;
    mega::MegaApi* targetApi = AccountManager::instance().getApi(targetAccountId);

    if (!targetApi) {
        showError("Error", QString("Cannot access %1. Please ensure the account is logged in.").arg(targetAccountName));
        return;
    }

    // Use RemoteFolderBrowserDialog to browse the target account
    RemoteFolderBrowserDialog dialog(this);
    dialog.setMegaApi(targetApi, targetAccountName);
    dialog.setSelectionMode(RemoteFolderBrowserDialog::SingleFolder);
    dialog.setInitialPath("/");
    dialog.setTitle(QString("Select Destination in %1").arg(targetAccountName));

    if (dialog.exec() != QDialog::Accepted) {
        return;  // User cancelled
    }

    QString targetPath = dialog.selectedPath();
    if (targetPath.isEmpty()) {
        return;
    }

    QString transferId = m_crossAccountTransferManager->copyToAccount(
        paths, sourceAccountId, targetAccountId, targetPath);

    if (!transferId.isEmpty()) {
        updateStatus(QString("Cross-account copy started: %1 item(s) to %2").arg(paths.size()).arg(targetPath));
    } else {
        showError("Error", "Failed to start cross-account copy");
    }
}

void MainWindow::onCrossAccountMove(const QStringList& paths, const QString& targetAccountId)
{
    if (!m_crossAccountTransferManager) {
        showError("Error", "Cross-account transfer manager not initialized");
        return;
    }

    QString sourceAccountId = AccountManager::instance().activeAccountId();

    // Get target account info and MegaApi for browsing
    MegaAccount targetAccount = AccountManager::instance().getAccount(targetAccountId);
    QString targetAccountName = targetAccount.displayName.isEmpty() ? targetAccount.email : targetAccount.displayName;
    mega::MegaApi* targetApi = AccountManager::instance().getApi(targetAccountId);

    if (!targetApi) {
        showError("Error", QString("Cannot access %1. Please ensure the account is logged in.").arg(targetAccountName));
        return;
    }

    // Use RemoteFolderBrowserDialog to browse the target account
    RemoteFolderBrowserDialog dialog(this);
    dialog.setMegaApi(targetApi, targetAccountName);
    dialog.setSelectionMode(RemoteFolderBrowserDialog::SingleFolder);
    dialog.setInitialPath("/");
    dialog.setTitle(QString("Select Destination in %1").arg(targetAccountName));

    if (dialog.exec() != QDialog::Accepted) {
        return;  // User cancelled
    }

    QString targetPath = dialog.selectedPath();
    if (targetPath.isEmpty()) {
        return;
    }

    // Confirm move since it deletes from source
    int ret = QMessageBox::question(
        this,
        "Confirm Move",
        QString("Move %1 item(s) to %2 in %3?\n\n"
                "This will delete the files from the current account after copying.")
            .arg(paths.size())
            .arg(targetPath)
            .arg(targetAccountName),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );

    if (ret != QMessageBox::Yes) {
        return;
    }

    QString transferId = m_crossAccountTransferManager->moveToAccount(
        paths, sourceAccountId, targetAccountId, targetPath);

    if (!transferId.isEmpty()) {
        updateStatus(QString("Cross-account move started: %1 item(s) to %2").arg(paths.size()).arg(targetPath));
    } else {
        showError("Error", "Failed to start cross-account move");
    }
}

void MainWindow::onShowTransferLog()
{
    // Switch to transfer log panel
    if (m_sidebar) {
        m_sidebar->clearActiveItem();  // No sidebar item for this panel
    }
    if (m_contentStack && m_crossAccountLogPanel) {
        m_contentStack->setCurrentWidget(m_crossAccountLogPanel);
        m_crossAccountLogPanel->refresh();
    }
    if (m_topToolbar) {
        m_topToolbar->setVisible(false);  // Hide toolbar for this panel
    }
}

void MainWindow::onCrossAccountTransferCompleted(const MegaCustom::CrossAccountTransfer& transfer)
{
    // Extract filename from path for display
    QString fileName = transfer.sourcePath;
    if (fileName.contains("/")) {
        fileName = fileName.mid(fileName.lastIndexOf("/") + 1);
    }
    if (fileName.contains(";")) {
        // Multiple files - just show count
        int fileCount = transfer.sourcePath.split(";", Qt::SkipEmptyParts).count();
        fileName = QString("%1 file(s)").arg(fileCount);
    }

    QString message = QString("Cross-account %1 completed: %2")
        .arg(transfer.operation == CrossAccountTransfer::Copy ? "copy" : "move")
        .arg(fileName);

    updateStatus(message);

    // Refresh the file explorer if we're viewing an account involved in the transfer
    // For moves: source files are deleted, so refresh if viewing source account
    // For copies: target has new files, but we typically stay on source, so refresh anyway
    QString currentAccountId = AccountManager::instance().activeAccountId();
    if (currentAccountId == transfer.sourceAccountId || currentAccountId == transfer.targetAccountId) {
        if (m_remoteExplorer) {
            qDebug() << "MainWindow: Refreshing file explorer after cross-account transfer";
            m_remoteExplorer->refresh();
        }
    }

    // Show a message box for visibility
    QMessageBox::information(this, "Transfer Complete", message);
}

void MainWindow::onCrossAccountTransferFailed(const MegaCustom::CrossAccountTransfer& transfer)
{
    // Extract filename from path for display
    QString fileName = transfer.sourcePath;
    if (fileName.contains("/")) {
        fileName = fileName.mid(fileName.lastIndexOf("/") + 1);
    }
    if (fileName.contains(";")) {
        // Multiple files - just show count
        int fileCount = transfer.sourcePath.split(";", Qt::SkipEmptyParts).count();
        fileName = QString("%1 file(s)").arg(fileCount);
    }

    QString message = QString("Cross-account %1 failed: %2\n\nError: %3")
        .arg(transfer.operation == CrossAccountTransfer::Copy ? "copy" : "move")
        .arg(fileName)
        .arg(transfer.errorMessage);

    updateStatus(QString("Transfer failed: %1").arg(transfer.errorMessage));

    // Show error message box
    QMessageBox::warning(this, "Transfer Failed", message);
}

void MainWindow::onSharedLinksWillBreak(const QStringList& sourcePaths,
                                        const QStringList& pathsWithLinks,
                                        const QString& sourceAccountId,
                                        const QString& targetAccountId,
                                        const QString& targetPath)
{
    // Build a message showing which files have shared links
    QString fileList;
    for (const QString& path : pathsWithLinks) {
        QString fileName = path;
        if (fileName.contains("/")) {
            fileName = fileName.mid(fileName.lastIndexOf("/") + 1);
        }
        fileList += QString("  • %1\n").arg(fileName);
    }

    QString message = QString(
        "The following items have active shared links that will STOP WORKING after the move:\n\n"
        "%1\n"
        "Anyone with these links will no longer be able to access the files.\n\n"
        "Do you want to continue with the move?")
        .arg(fileList);

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Shared Links Warning");
    msgBox.setText("Some items have shared links");
    msgBox.setInformativeText(message);
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);

    int result = msgBox.exec();

    if (result == QMessageBox::Yes) {
        // User confirmed - proceed with move, skipping the warning check
        QString transferId = m_crossAccountTransferManager->moveToAccount(
            sourcePaths, sourceAccountId, targetAccountId, targetPath, true);

        if (!transferId.isEmpty()) {
            updateStatus(QString("Moving %1 item(s) to another account...")
                .arg(sourcePaths.count()));
        }
    } else {
        updateStatus("Move cancelled - shared links preserved");
    }
}

// ========================================
// QUICK PEEK PANEL
// ========================================

void MainWindow::onQuickPeekRequested(const QString& accountId)
{
    if (!m_quickPeekPanel) {
        return;
    }

    // Get account info
    MegaAccount account = AccountManager::instance().getAccount(accountId);
    if (account.id.isEmpty()) {
        showError("Error", "Account not found");
        return;
    }

    // Show the quick peek panel with this account
    m_quickPeekPanel->showForAccount(account);

    // Make sure the panel is visible in the splitter
    if (m_centralSplitter) {
        QList<int> sizes = m_centralSplitter->sizes();
        if (sizes.size() >= 3 && sizes[2] == 0) {
            // Panel was hidden, show it with reasonable width
            sizes[2] = 380;
            sizes[1] -= 380; // Take space from the content area
            m_centralSplitter->setSizes(sizes);
        }
    }

    updateStatus(QString("Quick peek: %1").arg(account.email));
}

void MainWindow::onQuickPeekCopyToActive(const QStringList& paths, const QString& sourceAccountId)
{
    if (!m_crossAccountTransferManager) {
        showError("Error", "Cross-account transfer manager not initialized");
        return;
    }

    QString targetAccountId = AccountManager::instance().activeAccountId();
    QString targetPath = "/"; // Copy to root of active account

    if (sourceAccountId == targetAccountId) {
        showError("Error", "Source and target accounts are the same");
        return;
    }

    QString transferId = m_crossAccountTransferManager->copyToAccount(
        paths, sourceAccountId, targetAccountId, targetPath);

    if (!transferId.isEmpty()) {
        updateStatus(QString("Copying %1 item(s) to active account...").arg(paths.size()));
    } else {
        showError("Error", "Failed to start copy to active account");
    }
}

// ========================================
// HELP MENU
// ========================================

void MainWindow::onKeyboardShortcuts()
{
    QString shortcuts = R"(
<style>
    table { border-collapse: collapse; width: 100%; }
    th, td { padding: 6px 12px; text-align: left; border-bottom: 1px solid #EFEFF0; }
    th { background-color: #F7F7F7; color: #616366; font-weight: 600; }
    td:first-child { font-weight: 600; color: #303233; }
    h3 { color: #DD1405; margin-top: 16px; margin-bottom: 8px; }
</style>

<h3>Account Shortcuts</h3>
<table>
<tr><th>Shortcut</th><th>Action</th></tr>
<tr><td>Ctrl+Tab</td><td>Switch to next account</td></tr>
<tr><td>Ctrl+Shift+Tab</td><td>Switch to previous account</td></tr>
<tr><td>Ctrl+Shift+A</td><td>Open account switcher</td></tr>
</table>

<h3>File Operations</h3>
<table>
<tr><th>Shortcut</th><th>Action</th></tr>
<tr><td>Ctrl+U</td><td>Upload files</td></tr>
<tr><td>Ctrl+D</td><td>Download selected</td></tr>
<tr><td>Ctrl+Shift+N</td><td>New folder</td></tr>
<tr><td>Delete</td><td>Delete selected</td></tr>
<tr><td>F2</td><td>Rename selected</td></tr>
<tr><td>F5</td><td>Refresh</td></tr>
</table>

<h3>Edit</h3>
<table>
<tr><th>Shortcut</th><th>Action</th></tr>
<tr><td>Ctrl+X</td><td>Cut</td></tr>
<tr><td>Ctrl+C</td><td>Copy</td></tr>
<tr><td>Ctrl+V</td><td>Paste</td></tr>
<tr><td>Ctrl+A</td><td>Select all</td></tr>
<tr><td>Ctrl+F</td><td>Find</td></tr>
</table>

<h3>Navigation</h3>
<table>
<tr><th>Shortcut</th><th>Action</th></tr>
<tr><td>Ctrl+H</td><td>Show/hide hidden files</td></tr>
<tr><td>Ctrl+Shift+F</td><td>Advanced search</td></tr>
<tr><td>Ctrl+Shift+L</td><td>Cross-account transfer log</td></tr>
<tr><td>Ctrl+,</td><td>Settings</td></tr>
<tr><td>F1</td><td>Keyboard shortcuts (this dialog)</td></tr>
</table>

<h3>Application</h3>
<table>
<tr><th>Shortcut</th><th>Action</th></tr>
<tr><td>Ctrl+Q</td><td>Quit application</td></tr>
</table>
)";

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Keyboard Shortcuts");
    msgBox.setTextFormat(Qt::RichText);
    msgBox.setText(shortcuts);
    msgBox.setIcon(QMessageBox::NoIcon);
    msgBox.setMinimumWidth(500);
    msgBox.exec();
}

} // namespace MegaCustom