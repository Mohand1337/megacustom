#ifndef MEGACUSTOM_QUICKPEEKPANEL_H
#define MEGACUSTOM_QUICKPEEKPANEL_H

#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include "accounts/AccountModels.h"

namespace mega {
class MegaApi;
class MegaNode;
}

namespace MegaCustom {

class SessionPool;

/**
 * @brief Slide-out panel for browsing another account without switching
 *
 * Features:
 * - Browse files in another account
 * - Copy files to active account
 * - Get public links
 * - Download files
 * - Switch to the browsed account
 */
class QuickPeekPanel : public QFrame
{
    Q_OBJECT

public:
    explicit QuickPeekPanel(QWidget* parent = nullptr);
    ~QuickPeekPanel() override = default;

    /**
     * @brief Set the session pool for accessing other accounts
     */
    void setSessionPool(SessionPool* sessionPool);

    /**
     * @brief Show the panel for a specific account
     * @param account The account to browse
     */
    void showForAccount(const MegaAccount& account);

    /**
     * @brief Get the currently browsed account ID
     */
    QString accountId() const { return m_accountId; }

    /**
     * @brief Check if panel is showing an account
     */
    bool isActive() const { return !m_accountId.isEmpty(); }

signals:
    /**
     * @brief Emitted when user wants to switch to this account
     */
    void switchToAccountRequested(const QString& accountId);

    /**
     * @brief Emitted when user wants to copy files to active account
     */
    void copyToActiveRequested(const QStringList& paths, const QString& sourceAccountId);

    /**
     * @brief Emitted when panel is closed
     */
    void panelClosed();

public slots:
    /**
     * @brief Close the panel
     */
    void closePanel();

    /**
     * @brief Refresh current directory
     */
    void refresh();

private slots:
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onItemContextMenu(const QPoint& pos);
    void onNavigateUp();
    void onSwitchToAccount();
    void onCopyToActive();
    void onGetLink();
    void onDownload();

private:
    void setupUI();
    void navigateTo(const QString& path);
    void populateTree(mega::MegaApi* api, mega::MegaNode* parentNode);
    QString formatBytes(qint64 bytes) const;

    // UI components
    QLabel* m_titleLabel;
    QLabel* m_emailLabel;
    QLabel* m_pathLabel;
    QPushButton* m_closeBtn;
    QPushButton* m_upBtn;
    QPushButton* m_refreshBtn;
    QTreeWidget* m_treeWidget;
    QPushButton* m_switchBtn;
    QLabel* m_statusLabel;

    // State
    SessionPool* m_sessionPool;
    QString m_accountId;
    QString m_accountEmail;
    QString m_currentPath;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_QUICKPEEKPANEL_H
