#include "dialogs/RemoteFolderBrowserDialog.h"
#include "controllers/FileController.h"
#include "widgets/ButtonFactory.h"
#include "widgets/IconButton.h"
#include "widgets/LoadingSpinner.h"
#include "utils/PathUtils.h"
#include "utils/DpiScaler.h"
#include "styles/ThemeManager.h"
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QApplication>

namespace MegaCustom {

RemoteFolderBrowserDialog::RemoteFolderBrowserDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Browse MEGA Cloud");
    setMinimumSize(DpiScaler::scale(600), DpiScaler::scale(500));
    setupUI();
}

RemoteFolderBrowserDialog::~RemoteFolderBrowserDialog()
{
    delete m_ownedFileController;
}

void RemoteFolderBrowserDialog::setMegaApi(mega::MegaApi* api, const QString& accountName)
{
    // Clean up any owned controller
    if (m_ownedFileController) {
        delete m_ownedFileController;
        m_ownedFileController = nullptr;
    }

    if (api) {
        // Create a new FileController for this specific account
        m_ownedFileController = new FileController(api);
        setFileController(m_ownedFileController);

        m_accountName = accountName;
        if (!accountName.isEmpty()) {
            setWindowTitle(QString("Browse MEGA Cloud - %1").arg(accountName));
        }
    }
}

void RemoteFolderBrowserDialog::setFileController(FileController* controller) {
    if (m_fileController) {
        disconnect(m_fileController, nullptr, this, nullptr);
    }

    m_fileController = controller;

    if (m_fileController) {
        connect(m_fileController, &FileController::fileListReceived,
                this, [this](const QVariantList& files) {
                    m_treeWidget->clear();

                    // Add parent folder item if not at root
                    if (m_currentPath != "/") {
                        QTreeWidgetItem* parentItem = new QTreeWidgetItem(m_treeWidget);
                        parentItem->setText(0, "..");
                        parentItem->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
                        parentItem->setData(0, Qt::UserRole, "parent");
                        parentItem->setData(0, Qt::UserRole + 1, true);  // isFolder
                    }

                    for (const auto& file : files) {
                        QVariantMap fileInfo = file.toMap();
                        QString name = fileInfo["name"].toString();
                        bool isFolder = fileInfo["isFolder"].toBool();
                        qint64 size = fileInfo["size"].toLongLong();
                        QString path = fileInfo["path"].toString();

                        // Filter based on selection mode
                        bool include = true;
                        switch (m_selectionMode) {
                            case SingleFolder:
                            case MultipleFolders:
                                include = isFolder;
                                break;
                            case SingleFile:
                            case MultipleFiles:
                                // Show folders for navigation but mark files as selectable
                                break;
                            default:
                                break;
                        }

                        QTreeWidgetItem* item = new QTreeWidgetItem(m_treeWidget);
                        item->setText(0, name);
                        item->setText(1, isFolder ? "Folder" : formatSize(size));
                        item->setData(0, Qt::UserRole, path);
                        item->setData(0, Qt::UserRole + 1, isFolder);

                        if (isFolder) {
                            item->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
                        } else {
                            item->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
                        }

                        // Dim non-selectable items based on mode
                        bool selectable = true;
                        switch (m_selectionMode) {
                            case SingleFolder:
                            case MultipleFolders:
                                selectable = isFolder;
                                break;
                            case SingleFile:
                            case MultipleFiles:
                                selectable = !isFolder;
                                break;
                            default:
                                break;
                        }

                        if (!selectable && !isFolder) {
                            item->setForeground(0, Qt::gray);
                            item->setForeground(1, Qt::gray);
                        }
                    }

                    m_statusLabel->setText(QString("%1 item(s)").arg(files.size()));
                    updateButtonStates();
                });

        connect(m_fileController, &FileController::loadingStarted,
                this, [this](const QString&) {
                    m_statusLabel->setText("Loading...");
                    m_treeWidget->setEnabled(false);
                    // Center and show spinner
                    if (m_loadingSpinner) {
                        m_loadingSpinner->move(
                            (m_treeWidget->width() - m_loadingSpinner->width()) / 2,
                            (m_treeWidget->height() - m_loadingSpinner->height()) / 2);
                        m_loadingSpinner->start();
                        m_loadingSpinner->show();
                    }
                    QApplication::processEvents();
                });

        connect(m_fileController, &FileController::loadingFinished,
                this, [this]() {
                    m_treeWidget->setEnabled(true);
                    // Hide spinner
                    if (m_loadingSpinner) {
                        m_loadingSpinner->stop();
                        m_loadingSpinner->hide();
                    }
                });

        connect(m_fileController, &FileController::loadingError,
                this, [this](const QString& error) {
                    m_statusLabel->setText("Error: " + error);
                });

        // Connect global search results
        connect(m_fileController, &FileController::searchResultsReceived,
                this, &RemoteFolderBrowserDialog::onSearchResultsReceived);
    }
}

void RemoteFolderBrowserDialog::setSelectionMode(SelectionMode mode) {
    m_selectionMode = mode;

    // Update tree selection mode
    switch (mode) {
        case SingleFolder:
        case SingleFile:
        case SingleItem:
            m_treeWidget->setSelectionMode(QAbstractItemView::SingleSelection);
            break;
        case MultipleFolders:
        case MultipleFiles:
        case MultipleItems:
            m_treeWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
            break;
    }

    // Update title based on mode
    switch (mode) {
        case SingleFolder:
            setWindowTitle("Select Folder");
            break;
        case SingleFile:
            setWindowTitle("Select File");
            break;
        case SingleItem:
            setWindowTitle("Select Item");
            break;
        case MultipleFolders:
            setWindowTitle("Select Folders");
            break;
        case MultipleFiles:
            setWindowTitle("Select Files");
            break;
        case MultipleItems:
            setWindowTitle("Select Items");
            break;
    }
}

void RemoteFolderBrowserDialog::setInitialPath(const QString& path) {
    m_currentPath = path.isEmpty() ? "/" : path;
    m_pathEdit->setText(m_currentPath);
}

QStringList RemoteFolderBrowserDialog::selectedPaths() const {
    return m_selectedPaths;
}

QString RemoteFolderBrowserDialog::selectedPath() const {
    return m_selectedPaths.isEmpty() ? QString() : m_selectedPaths.first();
}

void RemoteFolderBrowserDialog::setTitle(const QString& title) {
    setWindowTitle(title);
}

void RemoteFolderBrowserDialog::refresh() {
    loadPath(m_currentPath);
}

void RemoteFolderBrowserDialog::navigateTo(const QString& path) {
    m_currentPath = path;
    m_pathEdit->setText(m_currentPath);
    loadPath(m_currentPath);
}

void RemoteFolderBrowserDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(12, 12, 12, 12);

    // Navigation bar
    QHBoxLayout* navLayout = new QHBoxLayout();

    m_upBtn = ButtonFactory::createIconButton(":/icons/arrow-up.svg", this);
    m_upBtn->setToolTip("Go to parent folder");
    connect(m_upBtn, &QPushButton::clicked, this, &RemoteFolderBrowserDialog::onUpClicked);
    navLayout->addWidget(m_upBtn);

    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setText(m_currentPath);
    m_pathEdit->setPlaceholderText("/path/to/folder");
    connect(m_pathEdit, &QLineEdit::returnPressed,
            this, &RemoteFolderBrowserDialog::onPathEditReturnPressed);
    navLayout->addWidget(m_pathEdit, 1);

    m_refreshBtn = ButtonFactory::createIconButton(":/icons/refresh-cw.svg", this);
    m_refreshBtn->setToolTip("Refresh");
    connect(m_refreshBtn, &QPushButton::clicked, this, &RemoteFolderBrowserDialog::onRefreshClicked);
    navLayout->addWidget(m_refreshBtn);

    mainLayout->addLayout(navLayout);

    // Search/filter bar
    QHBoxLayout* searchLayout = new QHBoxLayout();
    QLabel* searchIcon = new QLabel(this);
    searchIcon->setPixmap(QIcon(":/icons/search.svg").pixmap(DpiScaler::scale(16), DpiScaler::scale(16)));
    searchIcon->setStyleSheet("padding-left: 4px;");
    searchLayout->addWidget(searchIcon);

    auto& tm = ThemeManager::instance();

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Search ALL folders in MEGA cloud...");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setStyleSheet(
        QString("QLineEdit { padding: 6px 8px; border: 1px solid %1; border-radius: 4px; }"
        "QLineEdit:focus { border-color: %2; }")
        .arg(tm.borderSubtle().name())
        .arg(tm.brandDefault().name()));
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &RemoteFolderBrowserDialog::onSearchTextChanged);
    searchLayout->addWidget(m_searchEdit, 1);

    // Global search toggle (checked by default for cloud-wide search)
    // Uses global QSS styling for checkbox indicator
    m_globalSearchCheck = new QCheckBox("Global", this);
    m_globalSearchCheck->setToolTip("Uncheck to filter only items in current folder");
    m_globalSearchCheck->setChecked(true);  // Global search is default
    m_isGlobalSearchMode = true;  // Match checkbox state
    connect(m_globalSearchCheck, &QCheckBox::toggled,
            this, &RemoteFolderBrowserDialog::onGlobalSearchToggled);
    searchLayout->addWidget(m_globalSearchCheck);

    mainLayout->addLayout(searchLayout);

    // Setup search debounce timer
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(400);  // 400ms debounce for global search

    // Tree widget
    m_treeWidget = new QTreeWidget(this);
    m_treeWidget->setHeaderLabels({"Name", "Size/Type"});
    m_treeWidget->setRootIsDecorated(false);
    m_treeWidget->setAlternatingRowColors(true);
    m_treeWidget->setSortingEnabled(true);
    m_treeWidget->sortByColumn(0, Qt::AscendingOrder);
    m_treeWidget->header()->setStretchLastSection(true);
    m_treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_treeWidget->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_treeWidget->setSelectionMode(QAbstractItemView::SingleSelection);

    connect(m_treeWidget, &QTreeWidget::itemDoubleClicked,
            this, &RemoteFolderBrowserDialog::onItemDoubleClicked);
    connect(m_treeWidget, &QTreeWidget::itemSelectionChanged,
            this, &RemoteFolderBrowserDialog::onItemSelectionChanged);

    mainLayout->addWidget(m_treeWidget, 1);

    // Loading spinner (overlay on tree widget)
    m_loadingSpinner = new LoadingSpinner(m_treeWidget);
    m_loadingSpinner->setFixedSize(DpiScaler::scale(48), DpiScaler::scale(48));
    m_loadingSpinner->hide();

    // Status bar
    m_statusLabel = new QLabel("Ready", this);
    m_statusLabel->setStyleSheet(QString("color: %1;").arg(tm.textSecondary().name()));
    mainLayout->addWidget(m_statusLabel);

    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();

    // "Select This Folder" button - for folder modes, allows selecting current path directly
    m_selectCurrentBtn = ButtonFactory::createSecondary("Select This Folder", this);
    m_selectCurrentBtn->setToolTip("Select the current folder without selecting items inside");
    connect(m_selectCurrentBtn, &QPushButton::clicked, this, &RemoteFolderBrowserDialog::onSelectCurrentFolderClicked);
    buttonLayout->addWidget(m_selectCurrentBtn);

    buttonLayout->addStretch();

    m_okBtn = ButtonFactory::createPrimary("Select", this);
    m_okBtn->setEnabled(false);
    m_okBtn->setDefault(true);
    connect(m_okBtn, &QPushButton::clicked, this, &RemoteFolderBrowserDialog::onOkClicked);
    buttonLayout->addWidget(m_okBtn);

    m_cancelBtn = ButtonFactory::createOutline("Cancel", this);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(m_cancelBtn);

    mainLayout->addLayout(buttonLayout);
}

void RemoteFolderBrowserDialog::updateButtonStates() {
    QList<QTreeWidgetItem*> selected = m_treeWidget->selectedItems();
    bool hasValidSelection = false;

    for (auto* item : selected) {
        QString path = item->data(0, Qt::UserRole).toString();
        bool isFolder = item->data(0, Qt::UserRole + 1).toBool();

        // Skip special items (parent, return to browse)
        if (path == "parent" || path == "return_to_browse") continue;

        switch (m_selectionMode) {
            case SingleFolder:
            case MultipleFolders:
                if (isFolder) hasValidSelection = true;
                break;
            case SingleFile:
            case MultipleFiles:
                if (!isFolder) hasValidSelection = true;
                break;
            case SingleItem:
            case MultipleItems:
                hasValidSelection = true;
                break;
        }
    }

    m_okBtn->setEnabled(hasValidSelection);

    // Navigation controls - disabled in global search mode
    if (m_isGlobalSearchMode) {
        m_upBtn->setEnabled(false);
    } else {
        m_upBtn->setEnabled(m_currentPath != "/");
    }

    // Show "Select This Folder" button only for folder/item selection modes
    bool showSelectCurrent = (m_selectionMode == SingleFolder ||
                              m_selectionMode == MultipleFolders ||
                              m_selectionMode == SingleItem ||
                              m_selectionMode == MultipleItems);
    m_selectCurrentBtn->setVisible(showSelectCurrent);

    // Enable if we have a valid path (not just root for single selection)
    bool canSelectCurrent = !m_currentPath.isEmpty() && m_currentPath != "/";
    m_selectCurrentBtn->setEnabled(canSelectCurrent);

    // Update button text to show what will be selected
    if (showSelectCurrent && canSelectCurrent) {
        // Show shortened path in button
        QString shortPath = m_currentPath;
        if (shortPath.length() > 30) {
            shortPath = "..." + shortPath.right(27);
        }
        m_selectCurrentBtn->setText(QString("Select: %1").arg(shortPath));
    } else {
        m_selectCurrentBtn->setText("Select This Folder");
    }
}

void RemoteFolderBrowserDialog::loadPath(const QString& path) {
    // Clear search filter when navigating to a new folder
    if (m_searchEdit) {
        m_searchEdit->clear();
    }

    if (m_fileController) {
        m_fileController->refreshRemote(path);
    }
}

QTreeWidgetItem* RemoteFolderBrowserDialog::createItem(const QString& name, bool isFolder, qint64 size) {
    QTreeWidgetItem* item = new QTreeWidgetItem();
    item->setText(0, name);
    item->setText(1, isFolder ? "Folder" : formatSize(size));

    if (isFolder) {
        item->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
    } else {
        item->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
    }

    return item;
}

QString RemoteFolderBrowserDialog::formatSize(qint64 bytes) const {
    if (bytes < 1024) {
        return QString("%1 B").arg(bytes);
    } else if (bytes < 1024 * 1024) {
        return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    } else if (bytes < 1024 * 1024 * 1024) {
        return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    } else {
        return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
    }
}

void RemoteFolderBrowserDialog::onItemDoubleClicked(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column)

    QString path = item->data(0, Qt::UserRole).toString();
    bool isFolder = item->data(0, Qt::UserRole + 1).toBool();

    if (path == "parent") {
        // Go to parent
        onUpClicked();
        return;
    }

    if (path == "return_to_browse") {
        // Return to folder browsing mode
        m_globalSearchCheck->setChecked(false);
        m_searchEdit->clear();
        loadPath(m_currentPath);
        return;
    }

    if (isFolder) {
        // Navigate into folder
        // If in global search mode, disable global search first
        if (m_isGlobalSearchMode) {
            m_globalSearchCheck->setChecked(false);
            m_searchEdit->clear();
        }
        navigateTo(path);
    } else {
        // Double-click on file = select it (if files are selectable)
        if (m_selectionMode == SingleFile || m_selectionMode == MultipleFiles ||
            m_selectionMode == SingleItem || m_selectionMode == MultipleItems) {
            onOkClicked();
        }
    }
}

void RemoteFolderBrowserDialog::onItemSelectionChanged() {
    updateButtonStates();
}

void RemoteFolderBrowserDialog::onUpClicked() {
    if (m_currentPath == "/") return;

    int lastSlash = m_currentPath.lastIndexOf('/');
    QString parentPath;
    if (lastSlash > 0) {
        parentPath = m_currentPath.left(lastSlash);
    } else {
        parentPath = "/";
    }

    navigateTo(parentPath);
}

void RemoteFolderBrowserDialog::onRefreshClicked() {
    loadPath(m_currentPath);
}

void RemoteFolderBrowserDialog::onPathEditReturnPressed() {
    QString path = PathUtils::normalizeRemotePath(m_pathEdit->text());
    navigateTo(path);
}

void RemoteFolderBrowserDialog::onFileListReceived(const QStringList& files) {
    // This is now handled via lambda in setFileController
    Q_UNUSED(files)
}

void RemoteFolderBrowserDialog::onOkClicked() {
    m_selectedPaths.clear();

    QList<QTreeWidgetItem*> selected = m_treeWidget->selectedItems();

    for (auto* item : selected) {
        QString path = item->data(0, Qt::UserRole).toString();
        bool isFolder = item->data(0, Qt::UserRole + 1).toBool();

        // Skip special items
        if (path == "parent" || path == "return_to_browse") continue;

        // Validate selection based on mode
        bool valid = false;
        switch (m_selectionMode) {
            case SingleFolder:
            case MultipleFolders:
                valid = isFolder;
                break;
            case SingleFile:
            case MultipleFiles:
                valid = !isFolder;
                break;
            case SingleItem:
            case MultipleItems:
                valid = true;
                break;
        }

        if (valid) {
            m_selectedPaths.append(path);
        }
    }

    // For folder-only modes, also allow selecting the current folder
    // (but not when in global search mode - require explicit selection)
    if (m_selectedPaths.isEmpty() && !m_isGlobalSearchMode &&
        (m_selectionMode == SingleFolder || m_selectionMode == MultipleFolders)) {
        m_selectedPaths.append(m_currentPath);
    }

    if (!m_selectedPaths.isEmpty()) {
        accept();
    }
}

void RemoteFolderBrowserDialog::onSelectCurrentFolderClicked() {
    // Directly select the current path without needing to select an item
    if (m_currentPath.isEmpty() || m_currentPath == "/") {
        return;
    }

    m_selectedPaths.clear();
    m_selectedPaths.append(m_currentPath);
    accept();
}

void RemoteFolderBrowserDialog::onSearchTextChanged(const QString& text) {
    QString searchText = text.trimmed();

    // If global search is enabled, use the FileController's search
    if (m_isGlobalSearchMode) {
        // Stop any pending timer
        if (m_searchTimer) {
            m_searchTimer->stop();
        }

        if (searchText.isEmpty()) {
            // Clear search, go back to current folder view
            loadPath(m_currentPath);
            return;
        }

        // Debounce global search - wait for user to stop typing
        if (m_searchTimer && m_fileController) {
            m_searchTimer->disconnect();
            connect(m_searchTimer, &QTimer::timeout, this, [this, searchText]() {
                m_statusLabel->setText("Searching...");
                m_fileController->searchRemote(searchText);
            });
            m_searchTimer->start();
        }
        return;
    }

    // Local filter mode - filter items in current view
    QString lowerSearchText = searchText.toLower();
    int visibleCount = 0;
    for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_treeWidget->topLevelItem(i);
        if (!item) continue;

        QString itemName = item->text(0).toLower();

        // Always show ".." (parent directory)
        if (item->data(0, Qt::UserRole).toString() == "parent") {
            item->setHidden(false);
            visibleCount++;
            continue;
        }

        // Show/hide based on search
        bool matches = lowerSearchText.isEmpty() || itemName.contains(lowerSearchText);
        item->setHidden(!matches);
        if (matches) visibleCount++;
    }

    // Update status with filter info
    if (lowerSearchText.isEmpty()) {
        m_statusLabel->setText(QString("%1 item(s)").arg(m_treeWidget->topLevelItemCount()));
    } else {
        m_statusLabel->setText(QString("%1 of %2 item(s) matching \"%3\"")
                               .arg(visibleCount)
                               .arg(m_treeWidget->topLevelItemCount())
                               .arg(text));
    }
}

void RemoteFolderBrowserDialog::onGlobalSearchToggled(bool checked) {
    m_isGlobalSearchMode = checked;

    // Update placeholder text
    if (checked) {
        m_searchEdit->setPlaceholderText("Search ALL folders in MEGA cloud...");
        // Disable navigation while in global search mode
        m_upBtn->setEnabled(false);
        m_pathEdit->setEnabled(false);
    } else {
        m_searchEdit->setPlaceholderText("Filter items in this folder...");
        m_upBtn->setEnabled(m_currentPath != "/");
        m_pathEdit->setEnabled(true);
    }

    // If search field has text, re-trigger search with new mode
    if (!m_searchEdit->text().trimmed().isEmpty()) {
        onSearchTextChanged(m_searchEdit->text());
    } else if (!checked) {
        // Switching back to local mode with empty search - refresh current folder
        loadPath(m_currentPath);
    }
}

void RemoteFolderBrowserDialog::onSearchResultsReceived(const QVariantList& results) {
    m_treeWidget->clear();

    // Add "Return to browsing" item at top
    QTreeWidgetItem* returnItem = new QTreeWidgetItem(m_treeWidget);
    returnItem->setText(0, "← Return to folder browsing");
    returnItem->setIcon(0, style()->standardIcon(QStyle::SP_ArrowBack));
    returnItem->setData(0, Qt::UserRole, "return_to_browse");
    returnItem->setData(0, Qt::UserRole + 1, false);
    returnItem->setForeground(0, ThemeManager::instance().brandDefault());
    QFont font = returnItem->font(0);
    font.setBold(true);
    returnItem->setFont(0, font);

    for (const auto& result : results) {
        QVariantMap fileInfo = result.toMap();
        QString name = fileInfo["name"].toString();
        QString path = fileInfo["path"].toString();
        bool isFolder = fileInfo["isFolder"].toBool();
        qint64 size = fileInfo["size"].toLongLong();

        // Filter based on selection mode
        bool include = true;
        switch (m_selectionMode) {
            case SingleFolder:
            case MultipleFolders:
                include = isFolder;
                break;
            case SingleFile:
            case MultipleFiles:
                include = !isFolder;
                break;
            default:
                break;
        }

        if (!include) continue;

        QTreeWidgetItem* item = new QTreeWidgetItem(m_treeWidget);
        item->setText(0, name);
        // Show full path in second column for search results
        item->setText(1, path);
        item->setData(0, Qt::UserRole, path);
        item->setData(0, Qt::UserRole + 1, isFolder);
        item->setToolTip(0, path);  // Full path on hover

        if (isFolder) {
            item->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
        } else {
            item->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
        }
    }

    m_statusLabel->setText(QString("%1 search result(s)").arg(results.size()));
    updateButtonStates();
}

} // namespace MegaCustom
