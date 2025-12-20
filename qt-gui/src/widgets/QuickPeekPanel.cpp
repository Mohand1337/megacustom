#include "QuickPeekPanel.h"
#include "ButtonFactory.h"
#include "accounts/SessionPool.h"
#include "accounts/AccountManager.h"
#include "styles/ThemeManager.h"
#include <megaapi.h>
#include <QApplication>
#include <QFileIconProvider>
#include <QMenu>
#include <QMessageBox>
#include <QFileDialog>
#include <QClipboard>
#include <QDebug>

namespace MegaCustom {

QuickPeekPanel::QuickPeekPanel(QWidget* parent)
    : QFrame(parent)
    , m_sessionPool(nullptr)
{
    setupUI();
    setVisible(false);
}

void QuickPeekPanel::setSessionPool(SessionPool* sessionPool)
{
    m_sessionPool = sessionPool;
}

void QuickPeekPanel::setupUI()
{
    setObjectName("QuickPeekPanel");
    setFrameShape(QFrame::StyledPanel);
    setMinimumWidth(350);
    setMaximumWidth(450);

    auto& tm = ThemeManager::instance();

    // Panel styling - use theme colors
    setStyleSheet(QString(
        "#QuickPeekPanel {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 8px;"
        "}")
        .arg(tm.surfacePrimary().name())
        .arg(tm.borderSubtle().name()));

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ========================================
    // Header
    // ========================================
    QWidget* headerWidget = new QWidget(this);
    headerWidget->setObjectName("QuickPeekHeader");
    headerWidget->setStyleSheet(QString(
        "#QuickPeekHeader {"
        "  background-color: %1;"
        "  border-bottom: 1px solid %2;"
        "  border-radius: 8px 8px 0 0;"
        "  padding: 12px;"
        "}")
        .arg(tm.surface2().name())
        .arg(tm.borderSubtle().name()));

    QVBoxLayout* headerLayout = new QVBoxLayout(headerWidget);
    headerLayout->setContentsMargins(12, 12, 12, 12);
    headerLayout->setSpacing(4);

    // Title row
    QHBoxLayout* titleRow = new QHBoxLayout();
    m_titleLabel = new QLabel("QUICK VIEW", headerWidget);
    m_titleLabel->setStyleSheet(QString("font-size: 11px; font-weight: 600; color: %1;")
        .arg(tm.textSecondary().name()));
    titleRow->addWidget(m_titleLabel);
    titleRow->addStretch();

    m_closeBtn = new QPushButton(headerWidget);
    m_closeBtn->setIcon(QIcon(":/icons/x.svg"));
    m_closeBtn->setFixedSize(24, 24);
    m_closeBtn->setFlat(true);
    m_closeBtn->setToolTip("Close");
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_closeBtn, &QPushButton::clicked, this, &QuickPeekPanel::closePanel);
    titleRow->addWidget(m_closeBtn);

    headerLayout->addLayout(titleRow);

    // Email
    m_emailLabel = new QLabel(headerWidget);
    m_emailLabel->setStyleSheet(QString("font-size: 13px; font-weight: 600; color: %1;")
        .arg(tm.textPrimary().name()));
    headerLayout->addWidget(m_emailLabel);

    mainLayout->addWidget(headerWidget);

    // ========================================
    // Navigation bar
    // ========================================
    QWidget* navWidget = new QWidget(this);
    navWidget->setStyleSheet(QString("background-color: %1; border-bottom: 1px solid %2;")
        .arg(tm.surface2().name())
        .arg(tm.borderSubtle().name()));
    QHBoxLayout* navLayout = new QHBoxLayout(navWidget);
    navLayout->setContentsMargins(8, 4, 8, 4);
    navLayout->setSpacing(4);

    m_upBtn = new QPushButton(navWidget);
    m_upBtn->setIcon(QIcon(":/icons/arrow-up.svg"));
    m_upBtn->setFixedSize(28, 28);
    m_upBtn->setFlat(true);
    m_upBtn->setToolTip("Go up");
    connect(m_upBtn, &QPushButton::clicked, this, &QuickPeekPanel::onNavigateUp);
    navLayout->addWidget(m_upBtn);

    m_pathLabel = new QLabel("/", navWidget);
    m_pathLabel->setStyleSheet(QString("font-size: 12px; color: %1; padding: 0 8px;")
        .arg(tm.textSecondary().name()));
    navLayout->addWidget(m_pathLabel, 1);

    m_refreshBtn = new QPushButton(navWidget);
    m_refreshBtn->setIcon(QIcon(":/icons/refresh-cw.svg"));
    m_refreshBtn->setFixedSize(28, 28);
    m_refreshBtn->setFlat(true);
    m_refreshBtn->setToolTip("Refresh");
    connect(m_refreshBtn, &QPushButton::clicked, this, &QuickPeekPanel::refresh);
    navLayout->addWidget(m_refreshBtn);

    mainLayout->addWidget(navWidget);

    // ========================================
    // File tree
    // ========================================
    m_treeWidget = new QTreeWidget(this);
    m_treeWidget->setObjectName("QuickPeekTree");
    m_treeWidget->setHeaderLabels({"Name", "Size"});
    m_treeWidget->setColumnWidth(0, 200);
    m_treeWidget->setColumnWidth(1, 80);
    m_treeWidget->setRootIsDecorated(false);
    m_treeWidget->setAlternatingRowColors(true);
    m_treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    QColor brandColor = tm.brandDefault();
    m_treeWidget->setStyleSheet(QString(
        "QTreeWidget {"
        "  border: none;"
        "  background-color: %1;"
        "}"
        "QTreeWidget::item {"
        "  padding: 4px 0;"
        "}"
        "QTreeWidget::item:selected {"
        "  background-color: rgba(%2, %3, %4, 25);"
        "  color: %5;"
        "}"
        "QTreeWidget::item:hover {"
        "  background-color: %6;"
        "}")
        .arg(tm.surfacePrimary().name())
        .arg(brandColor.red()).arg(brandColor.green()).arg(brandColor.blue())
        .arg(tm.textPrimary().name())
        .arg(tm.surface2().name()));

    connect(m_treeWidget, &QTreeWidget::itemDoubleClicked,
            this, &QuickPeekPanel::onItemDoubleClicked);
    connect(m_treeWidget, &QTreeWidget::customContextMenuRequested,
            this, &QuickPeekPanel::onItemContextMenu);

    mainLayout->addWidget(m_treeWidget, 1);

    // ========================================
    // Footer
    // ========================================
    QWidget* footerWidget = new QWidget(this);
    footerWidget->setStyleSheet(QString("background-color: %1; border-top: 1px solid %2;")
        .arg(tm.surface2().name())
        .arg(tm.borderSubtle().name()));
    QVBoxLayout* footerLayout = new QVBoxLayout(footerWidget);
    footerLayout->setContentsMargins(12, 8, 12, 12);
    footerLayout->setSpacing(8);

    // Status
    m_statusLabel = new QLabel("Right-click to copy to active account", footerWidget);
    m_statusLabel->setStyleSheet(QString("font-size: 11px; color: %1;")
        .arg(tm.textSecondary().name()));
    footerLayout->addWidget(m_statusLabel);

    // Switch button - use ButtonFactory for consistent MEGA brand styling
    m_switchBtn = ButtonFactory::createPrimary("Switch to this account", footerWidget);
    m_switchBtn->setObjectName("QuickPeekSwitchBtn");
    connect(m_switchBtn, &QPushButton::clicked, this, &QuickPeekPanel::onSwitchToAccount);
    footerLayout->addWidget(m_switchBtn);

    mainLayout->addWidget(footerWidget);
}

void QuickPeekPanel::showForAccount(const MegaAccount& account)
{
    m_accountId = account.id;
    m_accountEmail = account.email;
    m_currentPath = "/";

    m_emailLabel->setText(account.email);
    m_pathLabel->setText("/");

    // Clear and populate tree
    m_treeWidget->clear();

    if (!m_sessionPool) {
        m_statusLabel->setText("Session pool not available");
        return;
    }

    // Check if session is active
    if (!m_sessionPool->isSessionActive(m_accountId)) {
        m_statusLabel->setText("Session not active - login required");
        return;
    }

    navigateTo("/");
    show();
}

void QuickPeekPanel::closePanel()
{
    hide();
    m_accountId.clear();
    m_treeWidget->clear();
    emit panelClosed();
}

void QuickPeekPanel::refresh()
{
    if (!m_accountId.isEmpty()) {
        navigateTo(m_currentPath);
    }
}

void QuickPeekPanel::navigateTo(const QString& path)
{
    if (!m_sessionPool) return;

    mega::MegaApi* api = m_sessionPool->getSession(m_accountId);
    if (!api) {
        m_statusLabel->setText("Could not get session");
        return;
    }

    m_currentPath = path;
    m_pathLabel->setText(path);
    m_upBtn->setEnabled(path != "/");

    // Get the node for this path
    mega::MegaNode* node = api->getNodeByPath(path.toUtf8().constData());
    if (!node) {
        m_statusLabel->setText("Path not found");
        return;
    }

    populateTree(api, node);
    delete node;

    m_statusLabel->setText("Right-click to copy to active account");
}

void QuickPeekPanel::populateTree(mega::MegaApi* api, mega::MegaNode* parentNode)
{
    m_treeWidget->clear();

    mega::MegaNodeList* children = api->getChildren(parentNode);
    if (!children) return;

    QFileIconProvider iconProvider;

    for (int i = 0; i < children->size(); ++i) {
        mega::MegaNode* child = children->get(i);

        QTreeWidgetItem* item = new QTreeWidgetItem();
        item->setText(0, QString::fromUtf8(child->getName()));

        // Store path and type in item data
        QString childPath = m_currentPath;
        if (!childPath.endsWith("/")) childPath += "/";
        childPath += QString::fromUtf8(child->getName());
        item->setData(0, Qt::UserRole, childPath);
        item->setData(0, Qt::UserRole + 1, child->isFolder());

        if (child->isFolder()) {
            item->setIcon(0, iconProvider.icon(QFileIconProvider::Folder));
            item->setText(1, "");
        } else {
            item->setIcon(0, iconProvider.icon(QFileIconProvider::File));
            item->setText(1, formatBytes(child->getSize()));
        }

        m_treeWidget->addTopLevelItem(item);
    }

    delete children;

    // Sort: folders first, then by name
    m_treeWidget->sortItems(0, Qt::AscendingOrder);
}

void QuickPeekPanel::onItemDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column);
    if (!item) return;

    bool isFolder = item->data(0, Qt::UserRole + 1).toBool();
    if (isFolder) {
        QString path = item->data(0, Qt::UserRole).toString();
        navigateTo(path);
    }
}

void QuickPeekPanel::onNavigateUp()
{
    if (m_currentPath == "/") return;

    QString parentPath = m_currentPath;
    int lastSlash = parentPath.lastIndexOf('/');
    if (lastSlash > 0) {
        parentPath = parentPath.left(lastSlash);
    } else {
        parentPath = "/";
    }

    navigateTo(parentPath);
}

void QuickPeekPanel::onItemContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_treeWidget->itemAt(pos);
    if (!item) return;

    QMenu menu(this);

    QAction* copyAction = menu.addAction(QIcon(":/icons/copy.svg"), "Copy to Active Account");
    connect(copyAction, &QAction::triggered, this, &QuickPeekPanel::onCopyToActive);

    menu.addSeparator();

    QAction* linkAction = menu.addAction(QIcon(":/icons/link.svg"), "Get Public Link");
    connect(linkAction, &QAction::triggered, this, &QuickPeekPanel::onGetLink);

    QAction* downloadAction = menu.addAction(QIcon(":/icons/download.svg"), "Download");
    connect(downloadAction, &QAction::triggered, this, &QuickPeekPanel::onDownload);

    menu.exec(m_treeWidget->mapToGlobal(pos));
}

void QuickPeekPanel::onSwitchToAccount()
{
    if (!m_accountId.isEmpty()) {
        emit switchToAccountRequested(m_accountId);
        closePanel();
    }
}

void QuickPeekPanel::onCopyToActive()
{
    QList<QTreeWidgetItem*> selected = m_treeWidget->selectedItems();
    if (selected.isEmpty()) return;

    QStringList paths;
    for (QTreeWidgetItem* item : selected) {
        paths << item->data(0, Qt::UserRole).toString();
    }

    emit copyToActiveRequested(paths, m_accountId);

    m_statusLabel->setText(QString("Copying %1 item(s)...").arg(paths.size()));
}

void QuickPeekPanel::onGetLink()
{
    QList<QTreeWidgetItem*> selected = m_treeWidget->selectedItems();
    if (selected.isEmpty()) return;

    // For now, just get link for first selected item
    QTreeWidgetItem* item = selected.first();
    QString path = item->data(0, Qt::UserRole).toString();

    if (!m_sessionPool) return;

    mega::MegaApi* api = m_sessionPool->getSession(m_accountId);
    if (!api) return;

    mega::MegaNode* node = api->getNodeByPath(path.toUtf8().constData());
    if (!node) {
        m_statusLabel->setText("Could not get node");
        return;
    }

    // Check if already exported
    if (node->isExported()) {
        QString link = QString::fromUtf8(node->getPublicLink());
        QApplication::clipboard()->setText(link);
        m_statusLabel->setText("Link copied to clipboard");
    } else {
        m_statusLabel->setText("Node not exported - use main account to create link");
    }

    delete node;
}

void QuickPeekPanel::onDownload()
{
    QList<QTreeWidgetItem*> selected = m_treeWidget->selectedItems();
    if (selected.isEmpty()) return;

    QString downloadPath = QFileDialog::getExistingDirectory(
        this, "Select Download Folder", QDir::homePath());

    if (downloadPath.isEmpty()) return;

    if (!m_sessionPool) {
        m_statusLabel->setText("Session pool not available");
        return;
    }

    mega::MegaApi* api = m_sessionPool->getSession(m_accountId);
    if (!api) {
        m_statusLabel->setText("Could not get session for account");
        return;
    }

    int downloadCount = 0;
    int errorCount = 0;

    for (QTreeWidgetItem* item : selected) {
        QString nodePath = item->data(0, Qt::UserRole).toString();
        bool isFolder = item->data(0, Qt::UserRole + 1).toBool();
        QString nodeName = item->text(0);

        mega::MegaNode* node = api->getNodeByPath(nodePath.toUtf8().constData());
        if (!node) {
            qWarning() << "QuickPeekPanel: Could not find node at path:" << nodePath;
            errorCount++;
            continue;
        }

        // Build local destination path
        QString localPath = downloadPath;
        if (!localPath.endsWith('/') && !localPath.endsWith('\\')) {
            localPath += '/';
        }
        localPath += nodeName;

        if (isFolder) {
            // For folders, create the directory and use startDownload for each file recursively
            // For simplicity, we'll download the whole folder
            QDir().mkpath(localPath);

            // MEGA SDK handles folder downloads - it will recreate the structure
            api->startDownload(node, localPath.toUtf8().constData(),
                              nullptr,  // customName
                              nullptr,  // appData
                              false,    // startFirst
                              nullptr,  // cancelToken
                              mega::MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                              mega::MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N,
                              false,    // undelete
                              nullptr); // listener
            downloadCount++;
        } else {
            // For files, download directly
            api->startDownload(node, localPath.toUtf8().constData(),
                              nullptr,  // customName
                              nullptr,  // appData
                              false,    // startFirst
                              nullptr,  // cancelToken
                              mega::MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                              mega::MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N,
                              false,    // undelete
                              nullptr); // listener
            downloadCount++;
        }

        delete node;
    }

    if (downloadCount > 0) {
        QString msg = QString("Started downloading %1 item(s) to %2")
            .arg(downloadCount).arg(downloadPath);
        if (errorCount > 0) {
            msg += QString(" (%1 errors)").arg(errorCount);
        }
        m_statusLabel->setText(msg);

        QMessageBox::information(this, "Download Started",
            QString("Started downloading %1 item(s) from %2's account.\n\n"
                    "Downloads run in the background. Check your local folder:\n%3")
                .arg(downloadCount).arg(m_accountEmail).arg(downloadPath));
    } else {
        m_statusLabel->setText("No items were downloaded");
        if (errorCount > 0) {
            QMessageBox::warning(this, "Download Failed",
                QString("Could not find %1 selected item(s) for download.")
                    .arg(errorCount));
        }
    }
}

QString QuickPeekPanel::formatBytes(qint64 bytes) const
{
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
}

} // namespace MegaCustom
