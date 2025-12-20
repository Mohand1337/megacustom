#ifndef MEGACUSTOM_ACCOUNTSWITCHERWIDGET_H
#define MEGACUSTOM_ACCOUNTSWITCHERWIDGET_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollArea>
#include <QPropertyAnimation>
#include <QProgressBar>
#include <QMap>
#include "accounts/AccountModels.h"

namespace MegaCustom {

class AccountManager;

/**
 * @brief Widget showing the current account with quick-switch dropdown
 *
 * Displays the active account at the top of the sidebar with:
 * - Account avatar (colored circle with initial)
 * - Email and display name
 * - Dropdown to switch between accounts
 * - Search box to filter accounts
 * - Quick add account button
 */
class AccountSwitcherWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(bool expanded READ isExpanded WRITE setExpanded NOTIFY expandedChanged)

public:
    explicit AccountSwitcherWidget(QWidget* parent = nullptr);
    ~AccountSwitcherWidget() override = default;

    bool isExpanded() const { return m_expanded; }
    void setExpanded(bool expanded);
    void toggleExpanded();
    void refresh();
    void focusSearch();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

signals:
    void expandedChanged(bool expanded);
    void accountSwitchRequested(const QString& accountId);
    void addAccountRequested();
    void manageAccountsRequested();
    void quickPeekRequested(const QString& accountId);

private slots:
    void onAccountSwitched(const QString& accountId);
    void onAccountAdded(const MegaAccount& account);
    void onAccountRemoved(const QString& accountId);
    void onAccountUpdated(const MegaAccount& account);
    void onStorageInfoUpdated(const QString& accountId);
    void onSearchTextChanged(const QString& text);
    void onAccountItemClicked(QListWidgetItem* item);
    void onExpandButtonClicked();

private:
    void setupUI();
    void setupHeaderSection();
    void setupDropdownSection();
    void connectSignals();
    void populateAccountList(const QString& filter = QString());
    void updateActiveAccountDisplay();
    void animateDropdown(bool show);

    QWidget* createAccountListItem(const MegaAccount& account, bool isActive);
    QWidget* createGroupHeader(const QString& groupName, const QColor& color, int accountCount);
    QString formatBytes(qint64 bytes) const;
    QString getInitials(const QString& email, const QString& displayName) const;
    QColor getAccountColor(const MegaAccount& account) const;

    // Header section (always visible)
    QFrame* m_headerFrame;
    QLabel* m_avatarLabel;  // Simple QLabel avatar
    QLabel* m_emailLabel;
    QLabel* m_nameLabel;
    QPushButton* m_expandButton;
    QProgressBar* m_storageBar;  // Storage indicator in header

    // Dropdown section (expandable)
    QFrame* m_dropdownFrame;
    QLineEdit* m_searchBox;
    QListWidget* m_accountList;
    QPushButton* m_addAccountBtn;
    QPushButton* m_manageAccountsBtn;

    // Animation
    QPropertyAnimation* m_dropdownAnimation;

    // State
    bool m_expanded;
    QString m_currentFilter;

    // Layout
    QVBoxLayout* m_mainLayout;
};

/**
 * @brief Custom widget for a single account item in the list
 */
class AccountListItemWidget : public QFrame
{
    Q_OBJECT

public:
    explicit AccountListItemWidget(const MegaAccount& account, bool isActive, QWidget* parent = nullptr);

    QString accountId() const { return m_accountId; }

signals:
    void clicked();
    void quickPeekClicked(const QString& accountId);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void setupUI(const MegaAccount& account, bool isActive);

    QString m_accountId;
    QPushButton* m_peekButton;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_ACCOUNTSWITCHERWIDGET_H
