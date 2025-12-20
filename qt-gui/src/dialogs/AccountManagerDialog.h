#ifndef MEGACUSTOM_ACCOUNTMANAGERDIALOG_H
#define MEGACUSTOM_ACCOUNTMANAGERDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QListWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QTextEdit>
#include <QProgressBar>
#include <QColorDialog>
#include <QStackedWidget>
#include "accounts/AccountModels.h"

namespace MegaCustom {

class AccountManager;

/**
 * @brief Full account management dialog
 *
 * Provides complete account management with:
 * - Account list grouped by AccountGroup
 * - Account details editing (display name, labels, color, notes)
 * - Group management (create, rename, delete, reorder)
 * - Re-authentication flow for expired sessions
 * - Account removal with confirmation
 */
class AccountManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AccountManagerDialog(QWidget* parent = nullptr);
    ~AccountManagerDialog() override = default;

    /**
     * @brief Refresh the account list from AccountManager
     */
    void refresh();

signals:
    void accountSelected(const QString& accountId);

private slots:
    // Account list
    void onAccountItemClicked(QTreeWidgetItem* item, int column);
    void onAccountFilterChanged(const QString& text);
    void onAddAccountClicked();
    void onRemoveAccountClicked();

    // Account details
    void onDisplayNameChanged(const QString& text);
    void onGroupChanged(int index);
    void onColorButtonClicked();
    void onClearColorClicked();
    void onNotesChanged();
    void onReauthenticateClicked();
    void onSetDefaultClicked();

    // Labels
    void onAddLabelClicked();
    void onRemoveLabelClicked();

    // Groups
    void onAddGroupClicked();
    void onEditGroupClicked();
    void onDeleteGroupClicked();

    // AccountManager signals
    void onAccountAdded(const MegaAccount& account);
    void onAccountRemoved(const QString& accountId);
    void onAccountUpdated(const MegaAccount& account);
    void onSessionReady(const QString& accountId);
    void onSessionError(const QString& accountId, const QString& error);

private:
    void setupUI();
    void setupAccountListPanel();
    void setupAccountDetailsPanel();
    void setupGroupsPanel();
    void connectSignals();

    void populateAccountTree(const QString& filter = QString());
    void populateGroupCombo();
    void showAccountDetails(const QString& accountId);
    void clearAccountDetails();
    void saveCurrentAccountChanges();

    QTreeWidgetItem* createAccountItem(const MegaAccount& account);
    QTreeWidgetItem* createGroupItem(const AccountGroup& group);
    QString formatBytes(qint64 bytes) const;
    QColor getStatusColor(const QString& accountId) const;

    // Main layout
    QSplitter* m_splitter;

    // Left panel - Account list
    QWidget* m_listPanel;
    QLineEdit* m_filterEdit;
    QTreeWidget* m_accountTree;
    QPushButton* m_addAccountBtn;
    QPushButton* m_removeAccountBtn;

    // Right panel - Account details (stacked)
    QStackedWidget* m_detailsStack;

    // Details page
    QWidget* m_detailsPage;
    QLabel* m_avatarLabel;
    QLabel* m_emailLabel;
    QLabel* m_statusLabel;
    QLabel* m_storageValueLabel;
    QProgressBar* m_storageBar;
    QLabel* m_lastLoginLabel;

    QLineEdit* m_displayNameEdit;
    QComboBox* m_groupCombo;
    QPushButton* m_colorButton;
    QPushButton* m_clearColorBtn;
    QColor m_selectedColor;

    QListWidget* m_labelsList;
    QLineEdit* m_newLabelEdit;
    QPushButton* m_addLabelBtn;
    QPushButton* m_removeLabelBtn;

    QTextEdit* m_notesEdit;

    QPushButton* m_reauthBtn;
    QPushButton* m_setDefaultBtn;

    // Empty state page
    QWidget* m_emptyPage;

    // Groups management section
    QWidget* m_groupsPanel;
    QListWidget* m_groupsList;
    QPushButton* m_addGroupBtn;
    QPushButton* m_editGroupBtn;
    QPushButton* m_deleteGroupBtn;

    // Dialog buttons
    QPushButton* m_closeBtn;

    // State
    QString m_currentAccountId;
    bool m_ignoreChanges;
};

/**
 * @brief Dialog for creating/editing a group
 */
class GroupEditDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GroupEditDialog(QWidget* parent = nullptr);
    GroupEditDialog(const AccountGroup& group, QWidget* parent = nullptr);

    AccountGroup getGroup() const;

private slots:
    void onColorButtonClicked();
    void validate();

private:
    void setupUI();

    QLineEdit* m_nameEdit;
    QPushButton* m_colorButton;
    QColor m_selectedColor;
    QPushButton* m_okButton;
    QPushButton* m_cancelButton;

    QString m_groupId; // Empty for new group
};

} // namespace MegaCustom

#endif // MEGACUSTOM_ACCOUNTMANAGERDIALOG_H
