#include "TopToolbar.h"
#include "BreadcrumbWidget.h"
#include "ButtonFactory.h"
#include "IconButton.h"
#include "utils/DpiScaler.h"
#include <QStyle>
#include <QIcon>

namespace MegaCustom {

TopToolbar::TopToolbar(QWidget* parent)
    : QWidget(parent)
    , m_mainLayout(nullptr)
    , m_breadcrumb(nullptr)
    , m_searchEdit(nullptr)
    , m_uploadBtn(nullptr)
    , m_downloadBtn(nullptr)
    , m_newFolderBtn(nullptr)
    , m_createFileBtn(nullptr)
    , m_deleteBtn(nullptr)
    , m_refreshBtn(nullptr)
    , m_currentPath("/")
{
    setupUI();
}

void TopToolbar::setupUI()
{
    setObjectName("TopToolbar");
    setFixedHeight(DpiScaler::scale(48));

    m_mainLayout = new QHBoxLayout(this);
    m_mainLayout->setContentsMargins(DpiScaler::scale(8), DpiScaler::scale(4),
                                     DpiScaler::scale(8), DpiScaler::scale(4));
    m_mainLayout->setSpacing(DpiScaler::scale(8));

    setupNavigationSection();
    setupSearchSection();
    setupActionsSection();
}

void TopToolbar::setupNavigationSection()
{
    // Folder icon before breadcrumb
    QLabel* folderIcon = new QLabel(this);
    folderIcon->setObjectName("BreadcrumbFolderIcon");
    folderIcon->setPixmap(QIcon(":/icons/folder.svg").pixmap(18, 18));
    folderIcon->setFixedSize(24, 24);
    folderIcon->setAlignment(Qt::AlignCenter);
    m_mainLayout->addWidget(folderIcon);

    // Breadcrumb for path navigation
    m_breadcrumb = new BreadcrumbWidget(this);
    m_breadcrumb->setObjectName("Breadcrumb");
    connect(m_breadcrumb, &BreadcrumbWidget::pathClicked,
            this, &TopToolbar::pathSegmentClicked);
    m_mainLayout->addWidget(m_breadcrumb, 1);
}

void TopToolbar::setupSearchSection()
{
    m_mainLayout->addSpacing(16);

    // Search field with search icon action
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName("SearchEdit");
    m_searchEdit->setPlaceholderText("Search...");
    m_searchEdit->setToolTip("Search files (press Enter to search globally)");
    m_searchEdit->setMinimumWidth(180);
    m_searchEdit->setMaximumWidth(280);
    m_searchEdit->setClearButtonEnabled(true);
    // Add search icon as leading action
    m_searchEdit->addAction(QIcon(":/icons/search.svg"), QLineEdit::LeadingPosition);
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &TopToolbar::onSearchTextChanged);
    connect(m_searchEdit, &QLineEdit::returnPressed,
            this, &TopToolbar::onSearchReturnPressed);
    m_mainLayout->addWidget(m_searchEdit);
}

void TopToolbar::setupActionsSection()
{
    m_mainLayout->addSpacing(16);

    // Primary action - Upload (with icon and text) using ButtonFactory
    m_uploadBtn = ButtonFactory::createWithIcon(":/icons/upload.svg", "Upload", this);
    m_uploadBtn->setObjectName("UploadButton");
    m_uploadBtn->setToolTip("Upload files or folders to the cloud");
    connect(m_uploadBtn, &QPushButton::clicked, this, &TopToolbar::uploadClicked);
    m_mainLayout->addWidget(m_uploadBtn);

    // Secondary actions - icon-only using ButtonFactory (creates IconButton)
    m_newFolderBtn = ButtonFactory::createIconButton(":/icons/folder-plus.svg", this);
    m_newFolderBtn->setObjectName("NewFolderButton");
    m_newFolderBtn->setToolTip("Create new folder in current directory");
    connect(m_newFolderBtn, &QPushButton::clicked, this, &TopToolbar::newFolderClicked);
    m_mainLayout->addWidget(m_newFolderBtn);

    m_createFileBtn = ButtonFactory::createIconButton(":/icons/file-plus.svg", this);
    m_createFileBtn->setObjectName("CreateFileButton");
    m_createFileBtn->setToolTip("Create new empty file");
    connect(m_createFileBtn, &QPushButton::clicked, this, &TopToolbar::createFileClicked);
    m_mainLayout->addWidget(m_createFileBtn);

    m_downloadBtn = ButtonFactory::createIconButton(":/icons/download.svg", this);
    m_downloadBtn->setObjectName("DownloadButton");
    m_downloadBtn->setToolTip("Download selected files to your computer");
    connect(m_downloadBtn, &QPushButton::clicked, this, &TopToolbar::downloadClicked);
    m_mainLayout->addWidget(m_downloadBtn);

    m_deleteBtn = ButtonFactory::createIconButton(":/icons/trash-2.svg", this);
    m_deleteBtn->setObjectName("DeleteButton");
    m_deleteBtn->setToolTip("Move selected items to trash");
    connect(m_deleteBtn, &QPushButton::clicked, this, &TopToolbar::deleteClicked);
    m_mainLayout->addWidget(m_deleteBtn);

    m_refreshBtn = ButtonFactory::createIconButton(":/icons/refresh-cw.svg", this);
    m_refreshBtn->setObjectName("RefreshButton");
    m_refreshBtn->setToolTip("Refresh folder listing (F5)");
    connect(m_refreshBtn, &QPushButton::clicked, this, &TopToolbar::refreshClicked);
    m_mainLayout->addWidget(m_refreshBtn);
}

// Old helper methods removed - now using ButtonFactory

void TopToolbar::setCurrentPath(const QString& path)
{
    m_currentPath = path;
    if (m_breadcrumb) {
        m_breadcrumb->setPath(path);
    }
}

void TopToolbar::setActionsEnabled(bool enabled)
{
    m_uploadBtn->setEnabled(enabled);
    m_downloadBtn->setEnabled(enabled);
    m_newFolderBtn->setEnabled(enabled);
    m_deleteBtn->setEnabled(enabled);
    m_refreshBtn->setEnabled(enabled);
}

void TopToolbar::setUploadEnabled(bool enabled)
{
    m_uploadBtn->setEnabled(enabled);
}

void TopToolbar::setDownloadEnabled(bool enabled)
{
    m_downloadBtn->setEnabled(enabled);
}

void TopToolbar::setDeleteEnabled(bool enabled)
{
    m_deleteBtn->setEnabled(enabled);
}

void TopToolbar::onSearchTextChanged(const QString& text)
{
    emit searchTextChanged(text);
}

void TopToolbar::onSearchReturnPressed()
{
    emit searchRequested(m_searchEdit->text());
}

QRect TopToolbar::searchWidgetGeometry() const
{
    return m_searchEdit ? m_searchEdit->geometry() : QRect();
}

QPoint TopToolbar::searchWidgetGlobalPos() const
{
    if (!m_searchEdit) return QPoint();
    return m_searchEdit->mapToGlobal(QPoint(0, m_searchEdit->height()));
}

} // namespace MegaCustom
