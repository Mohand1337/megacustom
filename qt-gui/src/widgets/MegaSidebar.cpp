#include "MegaSidebar.h"
#include "AccountSwitcherWidget.h"
#include "accounts/AccountManager.h"
#include "styles/ThemeManager.h"
#include "utils/DpiScaler.h"
#include <QFont>
#include <QIcon>
#include <QStyle>

namespace MegaCustom {

MegaSidebar::MegaSidebar(QWidget* parent)
    : QWidget(parent)
    , m_mainLayout(nullptr)
    , m_accountSwitcher(nullptr)
    , m_logoLabel(nullptr)
    , m_cloudFrame(nullptr)
    , m_cloudDriveBtn(nullptr)
    , m_toolsFrame(nullptr)
    , m_toolsLabel(nullptr)
    , m_folderMapperBtn(nullptr)
    , m_multiUploaderBtn(nullptr)
    , m_cloudCopierBtn(nullptr)
    , m_smartSyncBtn(nullptr)
    , m_memberRegistryBtn(nullptr)
    , m_distributionBtn(nullptr)
    , m_downloaderBtn(nullptr)
    , m_watermarkBtn(nullptr)
    , m_logViewerBtn(nullptr)
    , m_transfersBtn(nullptr)
    , m_settingsBtn(nullptr)
    , m_storageFrame(nullptr)
    , m_storageLabel(nullptr)
    , m_storageBar(nullptr)
    , m_storageDetails(nullptr)
    , m_logoIcon(nullptr)
    , m_brandLabel(nullptr)
    , m_activeItem(NavigationItem::CloudDrive)
    , m_isLoggedIn(false)
{
    setupUI();
    setLoggedIn(false);

    // Connect to ThemeManager for theme-aware updates
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, [this]() {
        // Force style refresh when theme changes
        // The new stylesheet from ThemeManager will be applied automatically
        style()->unpolish(this);
        style()->polish(this);
        update();
    });
}

void MegaSidebar::setupUI()
{
    setObjectName("MegaSidebar");
    setMinimumWidth(DpiScaler::scale(200));
    setMaximumWidth(DpiScaler::scale(280));

    // Prevent sidebar from expanding when dropdown opens
    // This stops layout propagation to the splitter and FileExplorer
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // Account switcher at the very top
    setupAccountSection();

    setupLogoSection();
    setupCloudSection();
    setupToolsSection();

    // Add stretch to push bottom sections down
    m_mainLayout->addStretch(1);

    setupBottomSection();
    setupStorageSection();
}

void MegaSidebar::setupAccountSection()
{
    m_accountSwitcher = new AccountSwitcherWidget(this);
    m_mainLayout->addWidget(m_accountSwitcher);

    // Connect account switcher signals
    connect(m_accountSwitcher, &AccountSwitcherWidget::accountSwitchRequested,
            this, &MegaSidebar::accountSwitchRequested);
    connect(m_accountSwitcher, &AccountSwitcherWidget::addAccountRequested,
            this, &MegaSidebar::addAccountRequested);
    connect(m_accountSwitcher, &AccountSwitcherWidget::manageAccountsRequested,
            this, &MegaSidebar::manageAccountsRequested);
    connect(m_accountSwitcher, &AccountSwitcherWidget::quickPeekRequested,
            this, &MegaSidebar::quickPeekRequested);

    // Add separator after account switcher
    QFrame* separator = new QFrame(this);
    separator->setObjectName("AccountSeparator");
    separator->setFrameShape(QFrame::HLine);
    separator->setFixedHeight(DpiScaler::scale(1));
    m_mainLayout->addWidget(separator);
}

void MegaSidebar::setupLogoSection()
{
    QFrame* logoFrame = new QFrame(this);
    logoFrame->setObjectName("LogoFrame");
    logoFrame->setFixedHeight(DpiScaler::scale(64));

    QHBoxLayout* logoLayout = new QHBoxLayout(logoFrame);
    logoLayout->setContentsMargins(DpiScaler::scale(16), DpiScaler::scale(12),
                                   DpiScaler::scale(16), DpiScaler::scale(12));
    logoLayout->setSpacing(DpiScaler::scale(12));

    // Red square logo with "M"
    m_logoIcon = new QLabel(logoFrame);
    m_logoIcon->setObjectName("LogoIcon");
    m_logoIcon->setText("M");
    m_logoIcon->setFixedSize(DpiScaler::scale(40), DpiScaler::scale(40));
    m_logoIcon->setAlignment(Qt::AlignCenter);
    QFont iconFont = m_logoIcon->font();
    iconFont.setPointSize(20);
    iconFont.setBold(true);
    m_logoIcon->setFont(iconFont);
    // Style: red background, white text, rounded corners (applied via QSS)

    // Brand name
    m_brandLabel = new QLabel("MegaCustom", logoFrame);
    m_brandLabel->setObjectName("BrandLabel");
    QFont brandFont = m_brandLabel->font();
    brandFont.setPointSize(16);
    brandFont.setBold(true);
    m_brandLabel->setFont(brandFont);

    logoLayout->addWidget(m_logoIcon);
    logoLayout->addWidget(m_brandLabel);
    logoLayout->addStretch();

    m_mainLayout->addWidget(logoFrame);
}

void MegaSidebar::setupCloudSection()
{
    m_cloudFrame = new QFrame(this);
    m_cloudFrame->setObjectName("CloudFrame");

    QVBoxLayout* cloudLayout = new QVBoxLayout(m_cloudFrame);
    cloudLayout->setContentsMargins(DpiScaler::scale(8), DpiScaler::scale(8),
                                    DpiScaler::scale(8), DpiScaler::scale(8));
    cloudLayout->setSpacing(DpiScaler::scale(4));

    // Cloud Drive button - navigates to file explorer
    m_cloudDriveBtn = createNavButton("Cloud Drive", ":/icons/cloud.svg");
    m_cloudDriveBtn->setObjectName("CloudDriveButton");
    m_cloudDriveBtn->setToolTip("Browse and manage your MEGA cloud files");
    connect(m_cloudDriveBtn, &QPushButton::clicked, this, &MegaSidebar::onCloudDriveClicked);
    cloudLayout->addWidget(m_cloudDriveBtn);

    m_mainLayout->addWidget(m_cloudFrame);
}

void MegaSidebar::setupToolsSection()
{
    m_toolsFrame = new QFrame(this);
    m_toolsFrame->setObjectName("ToolsFrame");

    QVBoxLayout* toolsLayout = new QVBoxLayout(m_toolsFrame);
    toolsLayout->setContentsMargins(DpiScaler::scale(8), DpiScaler::scale(16),
                                    DpiScaler::scale(8), DpiScaler::scale(8));
    toolsLayout->setSpacing(DpiScaler::scale(4));

    // Section header
    m_toolsLabel = new QLabel("TOOLS", m_toolsFrame);
    m_toolsLabel->setObjectName("SectionLabel");
    QFont sectionFont = m_toolsLabel->font();
    sectionFont.setPointSize(10);
    sectionFont.setBold(true);
    m_toolsLabel->setFont(sectionFont);
    toolsLayout->addWidget(m_toolsLabel);

    // Tool buttons with Lucide icons
    m_folderMapperBtn = createNavButton("Folder Mapper", ":/icons/folder-sync.svg");
    m_folderMapperBtn->setObjectName("FolderMapperButton");
    m_folderMapperBtn->setToolTip("Map local folders to cloud destinations for quick access");
    connect(m_folderMapperBtn, &QPushButton::clicked, this, &MegaSidebar::onFolderMapperClicked);
    toolsLayout->addWidget(m_folderMapperBtn);

    m_multiUploaderBtn = createNavButton("Multi Uploader", ":/icons/upload.svg");
    m_multiUploaderBtn->setObjectName("MultiUploaderButton");
    m_multiUploaderBtn->setToolTip("Upload files to multiple cloud locations with rules");
    connect(m_multiUploaderBtn, &QPushButton::clicked, this, &MegaSidebar::onMultiUploaderClicked);
    toolsLayout->addWidget(m_multiUploaderBtn);

    m_cloudCopierBtn = createNavButton("Cloud Copier", ":/icons/copy.svg");
    m_cloudCopierBtn->setObjectName("CloudCopierButton");
    m_cloudCopierBtn->setToolTip("Copy or move files between cloud locations without downloading");
    connect(m_cloudCopierBtn, &QPushButton::clicked, this, &MegaSidebar::onCloudCopierClicked);
    toolsLayout->addWidget(m_cloudCopierBtn);

    m_smartSyncBtn = createNavButton("Smart Sync", ":/icons/zap.svg");
    m_smartSyncBtn->setObjectName("SmartSyncButton");
    m_smartSyncBtn->setToolTip("Keep local and cloud folders synchronized automatically");
    connect(m_smartSyncBtn, &QPushButton::clicked, this, &MegaSidebar::onSmartSyncClicked);
    toolsLayout->addWidget(m_smartSyncBtn);

    m_memberRegistryBtn = createNavButton("Members", ":/icons/users.svg");
    m_memberRegistryBtn->setObjectName("MemberRegistryButton");
    m_memberRegistryBtn->setToolTip("Manage member registry and distribution paths");
    connect(m_memberRegistryBtn, &QPushButton::clicked, this, &MegaSidebar::onMemberRegistryClicked);
    toolsLayout->addWidget(m_memberRegistryBtn);

    m_distributionBtn = createNavButton("Distribution", ":/icons/share.svg");
    m_distributionBtn->setObjectName("DistributionButton");
    m_distributionBtn->setToolTip("Distribute watermarked content to members");
    connect(m_distributionBtn, &QPushButton::clicked, this, &MegaSidebar::onDistributionClicked);
    toolsLayout->addWidget(m_distributionBtn);

    m_downloaderBtn = createNavButton("Downloader", ":/icons/download.svg");
    m_downloaderBtn->setObjectName("DownloaderButton");
    m_downloaderBtn->setToolTip("Download content from BunnyCDN, Google Drive, Dropbox, and more");
    connect(m_downloaderBtn, &QPushButton::clicked, this, &MegaSidebar::onDownloaderClicked);
    toolsLayout->addWidget(m_downloaderBtn);

    m_watermarkBtn = createNavButton("Watermark", ":/icons/droplets.svg");
    m_watermarkBtn->setObjectName("WatermarkButton");
    m_watermarkBtn->setToolTip("Watermark videos and PDFs with custom text");
    connect(m_watermarkBtn, &QPushButton::clicked, this, &MegaSidebar::onWatermarkClicked);
    toolsLayout->addWidget(m_watermarkBtn);

    m_logViewerBtn = createNavButton("Activity Log", ":/icons/file-text.svg");
    m_logViewerBtn->setObjectName("LogViewerButton");
    m_logViewerBtn->setToolTip("View activity logs and distribution history");
    connect(m_logViewerBtn, &QPushButton::clicked, this, &MegaSidebar::onLogViewerClicked);
    toolsLayout->addWidget(m_logViewerBtn);

    m_mainLayout->addWidget(m_toolsFrame);
}

void MegaSidebar::setupBottomSection()
{
    QFrame* bottomFrame = new QFrame(this);
    bottomFrame->setObjectName("BottomFrame");

    QVBoxLayout* bottomLayout = new QVBoxLayout(bottomFrame);
    bottomLayout->setContentsMargins(DpiScaler::scale(8), DpiScaler::scale(8),
                                     DpiScaler::scale(8), DpiScaler::scale(16));
    bottomLayout->setSpacing(DpiScaler::scale(4));

    // Separator line
    QFrame* separator = new QFrame(this);
    separator->setObjectName("Separator");
    separator->setFrameShape(QFrame::HLine);
    separator->setFixedHeight(DpiScaler::scale(1));
    bottomLayout->addWidget(separator);

    // System section header
    QLabel* systemLabel = new QLabel("SYSTEM", bottomFrame);
    systemLabel->setObjectName("SectionLabel");
    QFont sectionFont = systemLabel->font();
    sectionFont.setPointSize(10);
    sectionFont.setBold(true);
    systemLabel->setFont(sectionFont);
    bottomLayout->addWidget(systemLabel);

    // Transfers button
    m_transfersBtn = createNavButton("Transfers", ":/icons/hard-drive.svg");
    m_transfersBtn->setObjectName("TransfersButton");
    m_transfersBtn->setToolTip("View and manage all uploads and downloads");
    connect(m_transfersBtn, &QPushButton::clicked, this, &MegaSidebar::onTransfersClicked);
    bottomLayout->addWidget(m_transfersBtn);

    // Settings button
    m_settingsBtn = createNavButton("Settings", ":/icons/settings.svg");
    m_settingsBtn->setObjectName("SettingsButton");
    m_settingsBtn->setToolTip("Configure application preferences");
    connect(m_settingsBtn, &QPushButton::clicked, this, &MegaSidebar::onSettingsClicked);
    bottomLayout->addWidget(m_settingsBtn);

    m_mainLayout->addWidget(bottomFrame);
}

QPushButton* MegaSidebar::createNavButton(const QString& text, const QString& iconPath)
{
    QPushButton* btn = new QPushButton(this);
    btn->setText(text);
    btn->setObjectName("NavButton");
    btn->setCheckable(true);
    btn->setFlat(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setMinimumHeight(DpiScaler::scale(36));

    if (!iconPath.isEmpty()) {
        btn->setIcon(QIcon(iconPath));
        btn->setIconSize(DpiScaler::scale(18, 18));
    }

    return btn;
}

void MegaSidebar::updateButtonStyles()
{
    // Reset all buttons (with null checks for safety)
    if (m_cloudDriveBtn) m_cloudDriveBtn->setChecked(false);
    if (m_folderMapperBtn) m_folderMapperBtn->setChecked(false);
    if (m_multiUploaderBtn) m_multiUploaderBtn->setChecked(false);
    if (m_cloudCopierBtn) m_cloudCopierBtn->setChecked(false);
    if (m_smartSyncBtn) m_smartSyncBtn->setChecked(false);
    if (m_memberRegistryBtn) m_memberRegistryBtn->setChecked(false);
    if (m_distributionBtn) m_distributionBtn->setChecked(false);
    if (m_downloaderBtn) m_downloaderBtn->setChecked(false);
    if (m_watermarkBtn) m_watermarkBtn->setChecked(false);
    if (m_logViewerBtn) m_logViewerBtn->setChecked(false);
    if (m_transfersBtn) m_transfersBtn->setChecked(false);
    if (m_settingsBtn) m_settingsBtn->setChecked(false);

    // Set active button
    switch (m_activeItem) {
    case NavigationItem::CloudDrive:
        m_cloudDriveBtn->setChecked(true);
        break;
    case NavigationItem::FolderMapper:
        m_folderMapperBtn->setChecked(true);
        break;
    case NavigationItem::MultiUploader:
        m_multiUploaderBtn->setChecked(true);
        break;
    case NavigationItem::CloudCopier:
        m_cloudCopierBtn->setChecked(true);
        break;
    case NavigationItem::SmartSync:
        m_smartSyncBtn->setChecked(true);
        break;
    case NavigationItem::MemberRegistry:
        m_memberRegistryBtn->setChecked(true);
        break;
    case NavigationItem::Distribution:
        m_distributionBtn->setChecked(true);
        break;
    case NavigationItem::Downloader:
        if (m_downloaderBtn) m_downloaderBtn->setChecked(true);
        break;
    case NavigationItem::Watermark:
        m_watermarkBtn->setChecked(true);
        break;
    case NavigationItem::LogViewer:
        if (m_logViewerBtn) m_logViewerBtn->setChecked(true);
        break;
    case NavigationItem::Transfers:
        m_transfersBtn->setChecked(true);
        break;
    case NavigationItem::Settings:
        m_settingsBtn->setChecked(true);
        break;
    }
}

void MegaSidebar::setActiveItem(NavigationItem item)
{
    m_activeItem = item;
    updateButtonStyles();
}

void MegaSidebar::clearActiveItem()
{
    // Set to invalid item to unhighlight all buttons
    m_activeItem = static_cast<NavigationItem>(-1);
    updateButtonStyles();
}

void MegaSidebar::setLoggedIn(bool loggedIn)
{
    m_isLoggedIn = loggedIn;

    // Enable/disable cloud-related items
    m_cloudDriveBtn->setEnabled(loggedIn);
    m_folderMapperBtn->setEnabled(loggedIn);
    m_multiUploaderBtn->setEnabled(loggedIn);
    m_cloudCopierBtn->setEnabled(loggedIn);
    m_smartSyncBtn->setEnabled(loggedIn);
    m_distributionBtn->setEnabled(loggedIn);
    m_transfersBtn->setEnabled(loggedIn);
}

// Slot implementations
void MegaSidebar::onCloudDriveClicked()
{
    setActiveItem(NavigationItem::CloudDrive);
    emit navigationItemClicked(NavigationItem::CloudDrive);
}

void MegaSidebar::onFolderMapperClicked()
{
    setActiveItem(NavigationItem::FolderMapper);
    emit navigationItemClicked(NavigationItem::FolderMapper);
}

void MegaSidebar::onMultiUploaderClicked()
{
    setActiveItem(NavigationItem::MultiUploader);
    emit navigationItemClicked(NavigationItem::MultiUploader);
}

void MegaSidebar::onCloudCopierClicked()
{
    setActiveItem(NavigationItem::CloudCopier);
    emit navigationItemClicked(NavigationItem::CloudCopier);
}

void MegaSidebar::onSmartSyncClicked()
{
    setActiveItem(NavigationItem::SmartSync);
    emit navigationItemClicked(NavigationItem::SmartSync);
}

void MegaSidebar::onMemberRegistryClicked()
{
    setActiveItem(NavigationItem::MemberRegistry);
    emit navigationItemClicked(NavigationItem::MemberRegistry);
}

void MegaSidebar::onDistributionClicked()
{
    setActiveItem(NavigationItem::Distribution);
    emit navigationItemClicked(NavigationItem::Distribution);
}

void MegaSidebar::onDownloaderClicked()
{
    setActiveItem(NavigationItem::Downloader);
    emit navigationItemClicked(NavigationItem::Downloader);
}

void MegaSidebar::onWatermarkClicked()
{
    setActiveItem(NavigationItem::Watermark);
    emit navigationItemClicked(NavigationItem::Watermark);
}

void MegaSidebar::onLogViewerClicked()
{
    setActiveItem(NavigationItem::LogViewer);
    emit navigationItemClicked(NavigationItem::LogViewer);
}

void MegaSidebar::onSettingsClicked()
{
    setActiveItem(NavigationItem::Settings);
    emit navigationItemClicked(NavigationItem::Settings);
}

void MegaSidebar::onTransfersClicked()
{
    setActiveItem(NavigationItem::Transfers);
    emit navigationItemClicked(NavigationItem::Transfers);
}

void MegaSidebar::setupStorageSection()
{
    m_storageFrame = new QFrame(this);
    m_storageFrame->setObjectName("StorageFrame");

    QVBoxLayout* storageLayout = new QVBoxLayout(m_storageFrame);
    storageLayout->setContentsMargins(DpiScaler::scale(16), DpiScaler::scale(8),
                                      DpiScaler::scale(16), DpiScaler::scale(16));
    storageLayout->setSpacing(DpiScaler::scale(8));

    // Storage header
    m_storageLabel = new QLabel("Storage Used", m_storageFrame);
    m_storageLabel->setObjectName("StorageLabel");
    QFont labelFont = m_storageLabel->font();
    labelFont.setPointSize(10);
    m_storageLabel->setFont(labelFont);
    storageLayout->addWidget(m_storageLabel);

    // Progress bar
    m_storageBar = new QProgressBar(m_storageFrame);
    m_storageBar->setObjectName("StorageBar");
    m_storageBar->setMinimum(0);
    m_storageBar->setMaximum(100);
    m_storageBar->setValue(0);
    m_storageBar->setTextVisible(false);
    m_storageBar->setFixedHeight(DpiScaler::scale(8));
    storageLayout->addWidget(m_storageBar);

    // Storage details
    m_storageDetails = new QLabel("0 B of 0 B", m_storageFrame);
    m_storageDetails->setObjectName("StorageDetails");
    QFont detailsFont = m_storageDetails->font();
    detailsFont.setPointSize(10);
    m_storageDetails->setFont(detailsFont);
    storageLayout->addWidget(m_storageDetails);

    m_mainLayout->addWidget(m_storageFrame);
}

void MegaSidebar::setStorageInfo(qint64 usedBytes, qint64 totalBytes)
{
    // Safety check for null pointers
    if (!m_storageBar || !m_storageDetails) {
        qDebug() << "MegaSidebar::setStorageInfo - storage widgets not initialized";
        return;
    }

    if (totalBytes > 0) {
        int percentage = static_cast<int>((usedBytes * 100) / totalBytes);
        m_storageBar->setValue(percentage);
        m_storageDetails->setText(QString("%1 of %2")
            .arg(formatBytes(usedBytes))
            .arg(formatBytes(totalBytes)));
    } else {
        m_storageBar->setValue(0);
        m_storageDetails->setText("Storage info unavailable");
    }
}

QString MegaSidebar::formatBytes(qint64 bytes) const
{
    const qint64 KB = 1024;
    const qint64 MB = KB * 1024;
    const qint64 GB = MB * 1024;
    const qint64 TB = GB * 1024;

    if (bytes >= TB) {
        return QString::number(bytes / static_cast<double>(TB), 'f', 2) + " TB";
    } else if (bytes >= GB) {
        return QString::number(bytes / static_cast<double>(GB), 'f', 2) + " GB";
    } else if (bytes >= MB) {
        return QString::number(bytes / static_cast<double>(MB), 'f', 2) + " MB";
    } else if (bytes >= KB) {
        return QString::number(bytes / static_cast<double>(KB), 'f', 2) + " KB";
    } else {
        return QString::number(bytes) + " B";
    }
}

void MegaSidebar::showAccountSwitcher()
{
    if (m_accountSwitcher) {
        m_accountSwitcher->focusSearch();
    }
}

void MegaSidebar::updateAccountDisplay()
{
    if (m_accountSwitcher) {
        m_accountSwitcher->refresh();
    }
}

} // namespace MegaCustom
