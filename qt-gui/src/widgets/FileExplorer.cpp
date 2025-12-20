#include "FileExplorer.h"
#include "LoadingSpinner.h"
#include "ModernMenu.h"
#include "ButtonFactory.h"
#include "controllers/FileController.h"
#include "accounts/AccountManager.h"
#include "styles/ThemeManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeView>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QFileSystemModel>
#include <QStandardItemModel>
#include <QMenu>
#include <QAction>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QDir>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QInputDialog>
#include <QMessageBox>
#include <QDebug>
#include <QThread>
#include <QCoreApplication>

namespace MegaCustom {

FileExplorer::FileExplorer(ExplorerType type, QWidget* parent)
    : QWidget(parent)
    , m_type(type)
    , m_fileController(nullptr)
    , m_loadingSpinner(nullptr)
    , m_isLoading(false)
    , m_treeView(nullptr)
    , m_emptyStateWidget(nullptr)
    , m_emptyStateUploadBtn(nullptr)
    , m_localModel(nullptr)
    , m_remoteModel(nullptr)
    , m_copyToAccountMenu(nullptr)
    , m_moveToAccountMenu(nullptr)
    , m_historyIndex(-1)
    , m_showHidden(false)
    , m_clipboardCut(false)
    , m_fileCount(0)
    , m_folderCount(0)
    , m_totalSize(0)
{
    setupUI();
    createContextMenu();
    initializeModel();

    // Set initial path
    if (m_type == Local) {
        goHome();
    } else {
        m_currentPath = "/";
        setEnabled(false); // Disabled until logged in
    }

    updateNavigationButtons();
    updateStatus();
}

FileExplorer::~FileExplorer()
{
    // Cleanup handled by Qt parent-child relationship
}

void FileExplorer::setFileController(FileController* controller)
{
    // Disconnect existing connections to avoid duplicates if called multiple times
    if (m_fileController) {
        disconnect(m_fileController, nullptr, this, nullptr);
    }

    m_fileController = controller;

    // Connect to file list received signal for remote explorer
    if (m_fileController && m_type == Remote) {
        connect(m_fileController, &FileController::fileListReceived,
                this, &FileExplorer::onRemoteFileListReceived);

        // Connect to loading state signals for responsive UI
        connect(m_fileController, &FileController::loadingStarted,
                this, &FileExplorer::onLoadingStarted);
        connect(m_fileController, &FileController::loadingFinished,
                this, &FileExplorer::onLoadingFinished);
        connect(m_fileController, &FileController::loadingError,
                this, &FileExplorer::onLoadingError);

        qDebug() << "FileExplorer: Connected to FileController signals";
    }
}

QString FileExplorer::currentPath() const
{
    return m_currentPath;
}

void FileExplorer::navigateTo(const QString& path)
{
    if (path == m_currentPath) {
        return;
    }

    m_currentPath = path;
    addToHistory(path);

    if (m_type == Local && m_localModel) {
        QModelIndex index = m_localModel->index(path);

        // Don't change root - keep full hierarchy visible
        // Instead, expand path to target and select it
        m_treeView->setCurrentIndex(index);
        m_treeView->scrollTo(index, QAbstractItemView::PositionAtCenter);

        // Expand all parents to make target visible
        QModelIndex parent = index.parent();
        while (parent.isValid()) {
            m_treeView->expand(parent);
            parent = parent.parent();
        }

        // Also expand the target folder itself to show its contents
        m_treeView->expand(index);

        // Count local files and folders
        m_fileCount = 0;
        m_folderCount = 0;
        m_totalSize = 0;
        QDir dir(path);
        QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
        for (const QFileInfo& info : entries) {
            if (info.isDir()) {
                m_folderCount++;
            } else {
                m_fileCount++;
                m_totalSize += info.size();
            }
        }
    } else if (m_type == Remote && m_fileController) {
        // Request file list for the new path from MEGA
        m_fileController->refreshRemote(path);
    }

    updateStatus();

    emit pathChanged(path);
}

QStringList FileExplorer::selectedFiles() const
{
    QStringList files;

    if (!m_treeView || !m_treeView->selectionModel()) {
        return files;
    }

    QModelIndexList indexes = m_treeView->selectionModel()->selectedIndexes();

    for (const QModelIndex& index : indexes) {
        if (index.column() == 0) { // Only process first column
            if (m_type == Local && m_localModel) {
                files << m_localModel->filePath(index);
            } else if (m_remoteModel) {
                // For remote files, construct the full path from current path + filename
                QString fileName = index.data(Qt::DisplayRole).toString();
                QString fullPath;
                if (m_currentPath == "/" || m_currentPath.isEmpty()) {
                    fullPath = "/" + fileName;
                } else {
                    fullPath = m_currentPath + "/" + fileName;
                }
                files << fullPath;
            }
        }
    }

    return files;
}

void FileExplorer::setShowHidden(bool show)
{
    m_showHidden = show;

    if (m_localModel) {
        QDir::Filters filters = QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot;
        if (show) {
            filters |= QDir::Hidden;
        }
        m_localModel->setFilter(filters);
    }
}

void FileExplorer::setSearchFilter(const QString& filter)
{
    m_searchFilter = filter.trimmed().toLower();

    if (m_type == Local && m_localModel) {
        // For local filesystem, use name filters
        if (m_searchFilter.isEmpty()) {
            m_localModel->setNameFilters(QStringList());
            m_localModel->setNameFilterDisables(false);
        } else {
            m_localModel->setNameFilters(QStringList() << QString("*%1*").arg(m_searchFilter));
            m_localModel->setNameFilterDisables(false);
        }
    } else if (m_type == Remote && m_remoteModel && m_treeView) {
        // For remote files, show/hide rows based on filter
        for (int row = 0; row < m_remoteModel->rowCount(); ++row) {
            QStandardItem* item = m_remoteModel->item(row, 0);
            if (item) {
                bool matches = m_searchFilter.isEmpty() ||
                              item->text().toLower().contains(m_searchFilter);
                m_treeView->setRowHidden(row, QModelIndex(), !matches);
            }
        }
    }

    updateStatus();
}

void FileExplorer::clearSearchFilter()
{
    setSearchFilter(QString());
}

void FileExplorer::clear()
{
    m_currentPath.clear();
    m_history.clear();
    m_historyIndex = -1;

    if (m_localModel) {
        m_localModel->setRootPath("");
    }

    updateNavigationButtons();
    updateStatus();
}

void FileExplorer::refresh()
{
    if (m_type == Local && m_localModel) {
        // Force refresh of current directory
        m_localModel->setRootPath("");
        m_localModel->setRootPath(m_currentPath);

        // Recount local files and folders
        m_fileCount = 0;
        m_folderCount = 0;
        m_totalSize = 0;
        QDir dir(m_currentPath);
        QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
        for (const QFileInfo& info : entries) {
            if (info.isDir()) {
                m_folderCount++;
            } else {
                m_fileCount++;
                m_totalSize += info.size();
            }
        }
    } else if (m_fileController) {
        // Request refresh from controller for remote
        m_fileController->refreshRemote(m_currentPath);
    }

    updateStatus();
}

void FileExplorer::goBack()
{
    if (m_historyIndex > 0) {
        m_historyIndex--;
        QString path = m_history[m_historyIndex];
        m_currentPath = path;
        navigateTo(path);
    }
}

void FileExplorer::goForward()
{
    if (m_historyIndex < m_history.size() - 1) {
        m_historyIndex++;
        QString path = m_history[m_historyIndex];
        m_currentPath = path;
        navigateTo(path);
    }
}

void FileExplorer::goUp()
{
    QDir dir(m_currentPath);
    if (dir.cdUp()) {
        navigateTo(dir.absolutePath());
    }
}

void FileExplorer::goHome()
{
    QString homePath = QDir::homePath();
    if (m_type == Remote) {
        homePath = "/";
    }
    navigateTo(homePath);
}

void FileExplorer::createNewFolder()
{
    bool ok;
    QString folderName = QInputDialog::getText(
        this,
        "New Folder",
        "Enter folder name:",
        QLineEdit::Normal,
        "New Folder",
        &ok
    );

    if (ok && !folderName.isEmpty()) {
        QString newPath = m_currentPath + "/" + folderName;

        if (m_type == Local) {
            QDir dir(m_currentPath);
            if (dir.mkdir(folderName)) {
                refresh();
            } else {
                QMessageBox::critical(this, "Error", "Failed to create folder");
            }
        } else if (m_fileController) {
            m_fileController->createRemoteFolder(newPath);
        }
    }
}

void FileExplorer::createNewFile()
{
    bool ok;
    QString fileName = QInputDialog::getText(
        this,
        "New File",
        "Enter file name:",
        QLineEdit::Normal,
        "New File.txt",
        &ok
    );

    if (ok && !fileName.isEmpty()) {
        if (m_type == Local) {
            QString newPath = m_currentPath + "/" + fileName;
            QFile file(newPath);
            if (file.open(QIODevice::WriteOnly)) {
                file.close();
                refresh();
            } else {
                QMessageBox::critical(this, "Error", "Failed to create file");
            }
        } else if (m_fileController) {
            m_fileController->createRemoteFile(fileName);
        }
    }
}

void FileExplorer::deleteSelected()
{
    QStringList files = selectedFiles();
    if (files.isEmpty()) {
        return;
    }

    int ret = QMessageBox::question(
        this,
        "Confirm Delete",
        QString("Delete %1 selected item(s)?").arg(files.size()),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );

    if (ret == QMessageBox::Yes) {
        for (const QString& file : files) {
            if (m_type == Local) {
                QFileInfo info(file);
                if (info.isDir()) {
                    QDir(file).removeRecursively();
                } else {
                    QFile::remove(file);
                }
            } else if (m_fileController) {
                m_fileController->deleteRemote(file);
            }
        }
        refresh();
    }
}

void FileExplorer::renameSelected()
{
    QStringList files = selectedFiles();
    if (files.isEmpty()) {
        return;
    }

    QString oldName = QFileInfo(files.first()).fileName();

    bool ok;
    QString newName = QInputDialog::getText(
        this,
        "Rename",
        "Enter new name:",
        QLineEdit::Normal,
        oldName,
        &ok
    );

    if (ok && !newName.isEmpty() && newName != oldName) {
        QString oldPath = files.first();

        if (m_type == Local) {
            QString newPath = QFileInfo(oldPath).absolutePath() + "/" + newName;
            if (!QFile::rename(oldPath, newPath)) {
                QMessageBox::critical(this, "Error", "Failed to rename file");
            }
            refresh();
        } else if (m_fileController) {
            // For remote files, pass the old path and just the new name
            // FileController::renameRemote expects (fullOldPath, newNameOnly)
            m_fileController->renameRemote(oldPath, newName);

            // For remote files, we need to wait for the refresh to complete
            // before the user can interact with the renamed file.
            // Otherwise, selection will still have the old path.

            // Store the new name to select after refresh
            m_pendingSelectAfterRefresh = newName;

            // Use a flag to track we're waiting for refresh after rename
            m_waitingForRenameRefresh = true;
            refresh();

            // Wait for the refresh to complete (with timeout)
            // This ensures the model is updated before user can select files
            int waited = 0;
            while (m_waitingForRenameRefresh && waited < 5000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                QThread::msleep(50);
                waited += 50;
            }
            m_waitingForRenameRefresh = false;

            // Select the renamed file
            selectFileByName(newName);
        }
    }
}

void FileExplorer::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void FileExplorer::dropEvent(QDropEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        QStringList files;

        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                files << url.toLocalFile();
            }
        }

        if (!files.isEmpty()) {
            emit filesDropped(files);

            // If dropping on remote explorer, initiate upload
            if (m_type == Remote && m_fileController) {
                for (const QString& file : files) {
                    emit uploadRequested(file);
                }
            }
        }

        event->acceptProposedAction();
    }
}

void FileExplorer::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Main tree view - single full-width list with columns
    m_treeView = new QTreeView(this);
    m_treeView->setObjectName("FileListView");
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_treeView->setDragDropMode(QAbstractItemView::DragDrop);
    m_treeView->setHeaderHidden(false);  // Show column headers: Name, Size, Modified
    m_treeView->setRootIsDecorated(true);  // Enable tree expansion arrows for folder navigation
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setEditTriggers(QAbstractItemView::EditKeyPressed);
    m_treeView->setSortingEnabled(true);
    connect(m_treeView, &QTreeView::doubleClicked, this, &FileExplorer::onItemDoubleClicked);
    connect(m_treeView, &QTreeView::customContextMenuRequested, this, &FileExplorer::onCustomContextMenu);

    // Create loading spinner (centered over the view area)
    m_loadingSpinner = new LoadingSpinner(this);
    m_loadingSpinner->setFixedSize(48, 48);
    m_loadingSpinner->hide();  // Hidden by default

    // Empty state widget (shown when no files)
    m_emptyStateWidget = new QWidget(this);
    m_emptyStateWidget->setObjectName("EmptyStateWidget");
    QVBoxLayout* emptyLayout = new QVBoxLayout(m_emptyStateWidget);
    emptyLayout->setAlignment(Qt::AlignCenter);
    emptyLayout->setSpacing(16);

    // Cloud icon
    QLabel* emptyIcon = new QLabel(m_emptyStateWidget);
    emptyIcon->setPixmap(QIcon(":/icons/cloud.svg").pixmap(64, 64));
    emptyIcon->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyIcon);

    // Title
    auto& tm = ThemeManager::instance();
    QLabel* emptyTitle = new QLabel("No files yet", m_emptyStateWidget);
    emptyTitle->setStyleSheet(QString("font-size: 18px; font-weight: bold; color: %1;")
        .arg(tm.textPrimary().name()));
    emptyTitle->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyTitle);

    // Description
    QLabel* emptyDesc = new QLabel("Drag files here or click Upload to start storing your data.", m_emptyStateWidget);
    emptyDesc->setStyleSheet(QString("font-size: 13px; color: %1;")
        .arg(tm.textSecondary().name()));
    emptyDesc->setAlignment(Qt::AlignCenter);
    emptyDesc->setWordWrap(true);
    emptyLayout->addWidget(emptyDesc);

    // Upload button - use ButtonFactory for consistent MEGA brand styling
    m_emptyStateUploadBtn = ButtonFactory::createPrimary("Upload", m_emptyStateWidget);
    m_emptyStateUploadBtn->setObjectName("EmptyStateUploadButton");
    m_emptyStateUploadBtn->setFixedWidth(120);
    connect(m_emptyStateUploadBtn, &QPushButton::clicked, this, [this]() {
        emit uploadRequested(m_currentPath);
    });
    emptyLayout->addWidget(m_emptyStateUploadBtn, 0, Qt::AlignCenter);

    // Empty state is initially hidden
    m_emptyStateWidget->hide();

    // Add tree view as main content
    mainLayout->addWidget(m_treeView, 1);
    mainLayout->addWidget(m_emptyStateWidget, 1);

    // Status bar
    m_statusLabel = new QLabel("0 items", this);
    m_statusLabel->setStyleSheet(QString("QLabel { padding: 5px; background-color: %1; }")
        .arg(tm.surface2().name()));
    mainLayout->addWidget(m_statusLabel);

    // Enable drag and drop
    setAcceptDrops(true);
}

void FileExplorer::createContextMenu()
{
    // Use ModernMenu for rounded corners and drop shadow styling
    m_contextMenu = new ModernMenu(this);

    m_copyAction = m_contextMenu->addAction(QIcon(":/icons/copy.png"), "Copy");
    connect(m_copyAction, &QAction::triggered, this, &FileExplorer::copySelected);

    m_cutAction = m_contextMenu->addAction(QIcon(":/icons/cut.png"), "Cut");
    connect(m_cutAction, &QAction::triggered, this, &FileExplorer::cutSelected);

    m_pasteAction = m_contextMenu->addAction(QIcon(":/icons/paste.png"), "Paste");
    connect(m_pasteAction, &QAction::triggered, this, &FileExplorer::paste);

    m_contextMenu->addSeparator();

    // Cross-account operations (only for remote explorer)
    if (m_type == Remote) {
        // Create submenus as ModernMenu for consistent styling
        m_copyToAccountMenu = new ModernMenu("Copy to Account...", this);
        m_copyToAccountMenu->setIcon(QIcon(":/icons/copy.png"));
        m_contextMenu->addMenu(m_copyToAccountMenu);

        m_moveToAccountMenu = new ModernMenu("Move to Account...", this);
        m_moveToAccountMenu->setIcon(QIcon(":/icons/move.png"));
        m_contextMenu->addMenu(m_moveToAccountMenu);

        m_contextMenu->addSeparator();
    }

    m_deleteAction = m_contextMenu->addAction(QIcon(":/icons/delete.png"), "Delete");
    connect(m_deleteAction, &QAction::triggered, this, &FileExplorer::deleteSelected);

    m_renameAction = m_contextMenu->addAction(QIcon(":/icons/rename.png"), "Rename");
    connect(m_renameAction, &QAction::triggered, this, &FileExplorer::renameSelected);

    m_contextMenu->addSeparator();

    m_newFolderAction = m_contextMenu->addAction(QIcon(":/icons/folder_new.png"), "New Folder");
    connect(m_newFolderAction, &QAction::triggered, this, &FileExplorer::createNewFolder);
}

void FileExplorer::updateCrossAccountMenus()
{
    if (!m_copyToAccountMenu || !m_moveToAccountMenu) {
        return;
    }

    // Clear existing actions
    m_copyToAccountMenu->clear();
    m_moveToAccountMenu->clear();

    // Get all accounts from AccountManager
    QList<MegaAccount> accounts = AccountManager::instance().allAccounts();
    QString currentAccountId = AccountManager::instance().activeAccountId();

    // Add menu items for each account except the current one
    for (const MegaAccount& account : accounts) {
        if (account.id == currentAccountId) {
            continue; // Skip current account
        }

        QString displayText = account.displayName.isEmpty()
            ? account.email
            : QString("%1 (%2)").arg(account.displayName, account.email);

        // Copy to account action
        QAction* copyAction = m_copyToAccountMenu->addAction(displayText);
        connect(copyAction, &QAction::triggered, this, [this, accountId = account.id]() {
            QStringList paths = selectedFiles();
            if (!paths.isEmpty()) {
                emit crossAccountCopyRequested(paths, accountId);
            }
        });

        // Move to account action
        QAction* moveAction = m_moveToAccountMenu->addAction(displayText);
        connect(moveAction, &QAction::triggered, this, [this, accountId = account.id]() {
            QStringList paths = selectedFiles();
            if (!paths.isEmpty()) {
                emit crossAccountMoveRequested(paths, accountId);
            }
        });
    }

    // If no other accounts, show disabled placeholder
    if (m_copyToAccountMenu->isEmpty()) {
        QAction* noAccountsAction = m_copyToAccountMenu->addAction("No other accounts");
        noAccountsAction->setEnabled(false);

        QAction* noAccountsAction2 = m_moveToAccountMenu->addAction("No other accounts");
        noAccountsAction2->setEnabled(false);
    }
}

void FileExplorer::initializeModel()
{
    if (m_type == Local) {
        m_localModel = new QFileSystemModel(this);
        m_localModel->setRootPath(QDir::rootPath());

        QDir::Filters filters = QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot;
        if (m_showHidden) {
            filters |= QDir::Hidden;
        }
        m_localModel->setFilter(filters);

        m_treeView->setModel(m_localModel);

        // Set root to home directory to show reasonable starting hierarchy
        QModelIndex homeIndex = m_localModel->index(QDir::homePath());
        m_treeView->setRootIndex(homeIndex.parent());  // Show home's parent for context

        // Show columns: Name, Size, Type, Modified
        // Resize columns to fit content
        m_treeView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_treeView->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        m_treeView->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        m_treeView->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    } else {
        // Create a QStandardItemModel for remote files
        m_remoteModel = new QStandardItemModel(this);
        m_remoteModel->setHorizontalHeaderLabels({"Name", "Size", "Modified"});

        m_treeView->setModel(m_remoteModel);

        // Resize columns
        m_treeView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_treeView->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        m_treeView->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

        qDebug() << "FileExplorer: Remote model initialized";
    }
}

void FileExplorer::onItemDoubleClicked(const QModelIndex& index)
{
    if (!index.isValid()) return;

    if (m_type == Local && m_localModel) {
        QFileInfo info = m_localModel->fileInfo(index);
        if (info.isDir()) {
            navigateTo(info.absoluteFilePath());
        } else {
            emit fileDoubleClicked(info.absoluteFilePath());
        }
    } else if (m_type == Remote && m_remoteModel) {
        // Get the first column (name) which has the path data
        QModelIndex nameIndex = index.sibling(index.row(), 0);
        QString path = nameIndex.data(Qt::UserRole).toString();
        bool isFolder = nameIndex.data(Qt::UserRole + 1).toBool();

        if (isFolder) {
            navigateTo(path);
        } else {
            emit fileDoubleClicked(path);
        }
    }
}

void FileExplorer::onCustomContextMenu(const QPoint& pos)
{
    QPoint globalPos = mapToGlobal(pos);

    // Update action states
    bool hasSelection = !selectedFiles().isEmpty();
    m_copyAction->setEnabled(hasSelection);
    m_cutAction->setEnabled(hasSelection);
    m_deleteAction->setEnabled(hasSelection);
    m_renameAction->setEnabled(hasSelection && selectedFiles().size() == 1);
    m_pasteAction->setEnabled(!m_clipboard.isEmpty());

    // Update cross-account menus
    if (m_type == Remote) {
        updateCrossAccountMenus();
        if (m_copyToAccountMenu) {
            m_copyToAccountMenu->setEnabled(hasSelection);
        }
        if (m_moveToAccountMenu) {
            m_moveToAccountMenu->setEnabled(hasSelection);
        }
    }

    m_contextMenu->exec(globalPos);
}

void FileExplorer::updateNavigationButtons()
{
    // Navigation buttons are now handled by TopToolbar
    // This method is kept for API compatibility but does nothing
}

void FileExplorer::updateStatus()
{
    int totalItems = m_fileCount + m_folderCount;
    QString status = QString("%1 items").arg(totalItems);
    if (m_totalSize > 0) {
        status += QString(", %1").arg(formatFileSize(m_totalSize));
    }
    m_statusLabel->setText(status);

    // Toggle visibility between tree view and empty state
    bool isEmpty = (totalItems == 0) && !m_isLoading;
    if (m_treeView && m_emptyStateWidget) {
        m_treeView->setVisible(!isEmpty);
        m_emptyStateWidget->setVisible(isEmpty);
    }
}

void FileExplorer::addToHistory(const QString& path)
{
    // Remove forward history
    while (m_history.size() > m_historyIndex + 1) {
        m_history.removeLast();
    }

    // Add new path
    m_history.append(path);
    m_historyIndex = m_history.size() - 1;

    // Limit history size
    if (m_history.size() > 50) {
        m_history.removeFirst();
        m_historyIndex--;
    }
}

QString FileExplorer::formatFileSize(qint64 bytes) const
{
    const qint64 kb = 1024;
    const qint64 mb = kb * 1024;
    const qint64 gb = mb * 1024;

    if (bytes >= gb) {
        return QString("%1 GB").arg(bytes / gb);
    } else if (bytes >= mb) {
        return QString("%1 MB").arg(bytes / mb);
    } else if (bytes >= kb) {
        return QString("%1 KB").arg(bytes / kb);
    } else {
        return QString("%1 bytes").arg(bytes);
    }
}

void FileExplorer::copySelected()
{
    m_clipboard = selectedFiles();
    m_clipboardCut = false;
}

void FileExplorer::cutSelected()
{
    m_clipboard = selectedFiles();
    m_clipboardCut = true;
}

void FileExplorer::paste()
{
    if (m_clipboard.isEmpty()) {
        return;
    }

    for (const QString& source : m_clipboard) {
        if (m_clipboardCut) {
            emit moveRequested(source, m_currentPath);
        } else {
            emit copyRequested(source, m_currentPath);
        }
    }

    if (m_clipboardCut) {
        m_clipboard.clear();
        m_clipboardCut = false;
    }
}

void FileExplorer::sortByColumn(int column, Qt::SortOrder order)
{
    if (m_treeView) {
        m_treeView->sortByColumn(column, order);
    }
}

void FileExplorer::selectAll()
{
    if (m_treeView) {
        m_treeView->selectAll();
    }
}

bool FileExplorer::hasSelection() const
{
    if (m_treeView && m_treeView->selectionModel()) {
        return m_treeView->selectionModel()->hasSelection();
    }
    return false;
}

bool FileExplorer::hasClipboard() const
{
    return !m_clipboard.isEmpty();
}

void FileExplorer::onRemoteFileListReceived(const QVariantList& files)
{
    qDebug() << "FileExplorer: Received" << files.size() << "remote files";

    if (!m_remoteModel) {
        qDebug() << "FileExplorer: No remote model!";
        return;
    }

    // Clear existing items
    m_remoteModel->removeRows(0, m_remoteModel->rowCount());

    m_fileCount = 0;
    m_folderCount = 0;
    m_totalSize = 0;

    // Add files to model
    for (const QVariant& fileVar : files) {
        QVariantMap fileInfo = fileVar.toMap();

        QString name = fileInfo["name"].toString();
        qint64 size = fileInfo["size"].toLongLong();
        qint64 modified = fileInfo["modified"].toLongLong();
        bool isFolder = fileInfo["isFolder"].toBool();

        QList<QStandardItem*> row;

        // Name with icon
        QStandardItem* nameItem = new QStandardItem(name);
        QFileIconProvider iconProvider;
        if (isFolder) {
            nameItem->setIcon(iconProvider.icon(QFileIconProvider::Folder));
            m_folderCount++;
        } else {
            nameItem->setIcon(iconProvider.icon(QFileIconProvider::File));
            m_fileCount++;
            m_totalSize += size;
        }
        nameItem->setData(fileInfo["path"].toString(), Qt::UserRole);
        nameItem->setData(isFolder, Qt::UserRole + 1);
        row.append(nameItem);

        // Size
        QStandardItem* sizeItem = new QStandardItem(isFolder ? "" : formatFileSize(size));
        row.append(sizeItem);

        // Modified date
        QDateTime modTime = QDateTime::fromSecsSinceEpoch(modified);
        QStandardItem* modifiedItem = new QStandardItem(modTime.toString("yyyy-MM-dd hh:mm"));
        row.append(modifiedItem);

        m_remoteModel->appendRow(row);
    }

    // Resize columns to content
    m_treeView->resizeColumnToContents(0);
    m_treeView->resizeColumnToContents(1);
    m_treeView->resizeColumnToContents(2);

    updateStatus();
    qDebug() << "FileExplorer: Model updated with" << m_remoteModel->rowCount() << "rows";

    // Clear rename wait flag - model is now up to date
    m_waitingForRenameRefresh = false;
}

void FileExplorer::onLoadingStarted(const QString& path)
{
    Q_UNUSED(path);
    m_isLoading = true;

    // Show loading spinner centered in the view
    if (m_loadingSpinner) {
        // Position spinner in center of widget
        int x = (width() - m_loadingSpinner->width()) / 2;
        int y = (height() - m_loadingSpinner->height()) / 2;
        m_loadingSpinner->move(x, y);
        m_loadingSpinner->start();
        m_loadingSpinner->raise();  // Bring to front
    }

    // Update status bar
    m_statusLabel->setText("Loading...");

    qDebug() << "FileExplorer: Loading started";
}

void FileExplorer::onLoadingFinished()
{
    m_isLoading = false;

    // Hide loading spinner
    if (m_loadingSpinner) {
        m_loadingSpinner->stop();
    }

    // Status will be updated by updateStatus() after file list is received
    qDebug() << "FileExplorer: Loading finished";
}

void FileExplorer::onLoadingError(const QString& error)
{
    m_isLoading = false;

    // Hide loading spinner
    if (m_loadingSpinner) {
        m_loadingSpinner->stop();
    }

    // Show error in status bar - use theme-aware error colors
    auto& tm = ThemeManager::instance();
    QColor errorColor = tm.supportError();
    QColor errorBg = errorColor;
    errorBg.setAlpha(25);  // Light tint for background
    m_statusLabel->setText(QString("Error: %1").arg(error));
    m_statusLabel->setStyleSheet(QString("QLabel { padding: 5px; background-color: rgba(%1, %2, %3, 25); color: %4; }")
        .arg(errorBg.red()).arg(errorBg.green()).arg(errorBg.blue())
        .arg(errorColor.name()));

    qDebug() << "FileExplorer: Loading error -" << error;
}

void FileExplorer::showSearchResults(const QVariantList& results)
{
    qDebug() << "FileExplorer: Showing" << results.size() << "search results";

    if (!m_remoteModel) {
        qDebug() << "FileExplorer: No remote model!";
        return;
    }

    // Clear existing items
    m_remoteModel->removeRows(0, m_remoteModel->rowCount());

    m_fileCount = 0;
    m_folderCount = 0;
    m_totalSize = 0;

    // Add search results to model
    for (const QVariant& fileVar : results) {
        QVariantMap fileInfo = fileVar.toMap();

        QString name = fileInfo["name"].toString();
        QString path = fileInfo["path"].toString();
        qint64 size = fileInfo["size"].toLongLong();
        qint64 modified = fileInfo["modified"].toLongLong();
        bool isFolder = fileInfo["isFolder"].toBool();

        QList<QStandardItem*> row;

        // Name with icon - show full path for search results
        QStandardItem* nameItem = new QStandardItem(path);
        QFileIconProvider iconProvider;
        if (isFolder) {
            nameItem->setIcon(iconProvider.icon(QFileIconProvider::Folder));
            m_folderCount++;
        } else {
            nameItem->setIcon(iconProvider.icon(QFileIconProvider::File));
            m_fileCount++;
            m_totalSize += size;
        }
        nameItem->setData(path, Qt::UserRole);
        nameItem->setData(isFolder, Qt::UserRole + 1);
        row.append(nameItem);

        // Size
        QStandardItem* sizeItem = new QStandardItem(isFolder ? "" : formatFileSize(size));
        row.append(sizeItem);

        // Modified date
        QDateTime modTime = QDateTime::fromSecsSinceEpoch(modified);
        QStandardItem* modifiedItem = new QStandardItem(modTime.toString("yyyy-MM-dd hh:mm"));
        row.append(modifiedItem);

        m_remoteModel->appendRow(row);
    }

    // Resize columns to content
    m_treeView->resizeColumnToContents(0);
    m_treeView->resizeColumnToContents(1);
    m_treeView->resizeColumnToContents(2);

    // Update status to show search results count - use theme-aware info colors
    auto& tm = ThemeManager::instance();
    QColor infoColor = tm.supportInfo();
    QColor infoBg = infoColor;
    infoBg.setAlpha(25);  // Light tint for background
    m_statusLabel->setText(QString("Search results: %1 item(s)").arg(results.size()));
    m_statusLabel->setStyleSheet(QString("QLabel { padding: 5px; background-color: rgba(%1, %2, %3, 25); color: %4; }")
        .arg(infoBg.red()).arg(infoBg.green()).arg(infoBg.blue())
        .arg(infoColor.name()));

    qDebug() << "FileExplorer: Model updated with" << m_remoteModel->rowCount() << "search results";
}

void FileExplorer::selectFileByName(const QString& fileName)
{
    if (!m_treeView || fileName.isEmpty()) {
        return;
    }

    if (m_type == Local && m_localModel) {
        // For local filesystem, construct full path
        QString fullPath = m_currentPath + "/" + fileName;
        QModelIndex index = m_localModel->index(fullPath);
        if (index.isValid()) {
            m_treeView->selectionModel()->clearSelection();
            m_treeView->selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
            m_treeView->scrollTo(index);
        }
    } else if (m_remoteModel) {
        // For remote files, search through the model
        for (int row = 0; row < m_remoteModel->rowCount(); ++row) {
            QStandardItem* item = m_remoteModel->item(row, 0);
            if (item && item->text() == fileName) {
                QModelIndex index = m_remoteModel->index(row, 0);
                m_treeView->selectionModel()->clearSelection();
                m_treeView->selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
                m_treeView->scrollTo(index);
                qDebug() << "FileExplorer: Selected renamed file:" << fileName;
                break;
            }
        }
    }
}

} // namespace MegaCustom