#ifndef MEGASIDEBAR_H
#define MEGASIDEBAR_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QScrollArea>
#include <QProgressBar>

namespace MegaCustom {

class AccountSwitcherWidget;

/**
 * @brief MEGA-style sidebar navigation widget
 *
 * Provides left-side navigation with:
 * - MEGA logo at top
 * - Cloud Drive folder tree
 * - Feature tools (FolderMapper, MultiUploader, SmartSync)
 * - Settings at bottom
 */
class MegaSidebar : public QWidget
{
    Q_OBJECT

public:
    enum class NavigationItem {
        CloudDrive,
        FolderMapper,
        MultiUploader,
        CloudCopier,
        SmartSync,
        MemberRegistry,
        Distribution,
        Watermark,
        LogViewer,
        Settings,
        Transfers,
        Downloader
    };
    Q_ENUM(NavigationItem)

    explicit MegaSidebar(QWidget* parent = nullptr);
    ~MegaSidebar() override = default;

    // Highlight active navigation item
    void setActiveItem(NavigationItem item);
    void clearActiveItem();  // Unhighlight all items (for Tools menu panels)
    NavigationItem activeItem() const { return m_activeItem; }

    // Enable/disable items based on login state
    void setLoggedIn(bool loggedIn);

    // Update storage info
    void setStorageInfo(qint64 usedBytes, qint64 totalBytes);

    // Account switcher
    void showAccountSwitcher();
    void updateAccountDisplay();

signals:
    // Emitted when a navigation item is clicked
    void navigationItemClicked(MegaSidebar::NavigationItem item);

    // Account management signals
    void addAccountRequested();
    void manageAccountsRequested();
    void accountSwitchRequested(const QString& accountId);
    void quickPeekRequested(const QString& accountId);

private slots:
    void onCloudDriveClicked();
    void onFolderMapperClicked();
    void onMultiUploaderClicked();
    void onCloudCopierClicked();
    void onSmartSyncClicked();
    void onMemberRegistryClicked();
    void onDistributionClicked();
    void onDownloaderClicked();
    void onWatermarkClicked();
    void onLogViewerClicked();
    void onSettingsClicked();
    void onTransfersClicked();

private:
    void setupUI();
    void setupAccountSection();
    void setupLogoSection();
    void setupCloudSection();
    void setupToolsSection();
    void setupBottomSection();
    void setupStorageSection();

    QPushButton* createNavButton(const QString& text, const QString& iconPath = QString());
    void updateButtonStyles();
    QString formatBytes(qint64 bytes) const;

    // UI Components
    QVBoxLayout* m_mainLayout;

    // Account switcher section
    AccountSwitcherWidget* m_accountSwitcher;

    // Logo section
    QLabel* m_logoLabel;

    // Cloud Drive section
    QFrame* m_cloudFrame;
    QPushButton* m_cloudDriveBtn;

    // Tools section
    QFrame* m_toolsFrame;
    QLabel* m_toolsLabel;
    QPushButton* m_folderMapperBtn;
    QPushButton* m_multiUploaderBtn;
    QPushButton* m_cloudCopierBtn;
    QPushButton* m_smartSyncBtn;
    QPushButton* m_memberRegistryBtn;
    QPushButton* m_distributionBtn;
    QPushButton* m_downloaderBtn;
    QPushButton* m_watermarkBtn;
    QPushButton* m_logViewerBtn;

    // Bottom section
    QPushButton* m_transfersBtn;
    QPushButton* m_settingsBtn;

    // Storage section
    QFrame* m_storageFrame;
    QLabel* m_storageLabel;
    QProgressBar* m_storageBar;
    QLabel* m_storageDetails;

    // Logo section
    QLabel* m_logoIcon;
    QLabel* m_brandLabel;

    // State
    NavigationItem m_activeItem;
    bool m_isLoggedIn;
};

} // namespace MegaCustom

#endif // MEGASIDEBAR_H
