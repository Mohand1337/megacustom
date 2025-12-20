#ifndef MEGACUSTOM_REMOTEFOLDERBROWSERDIALOG_H
#define MEGACUSTOM_REMOTEFOLDERBROWSERDIALOG_H

#include <QDialog>
#include <QTreeWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QStringList>
#include <QTimer>

namespace MegaCustom {
class LoadingSpinner;
}

namespace mega {
class MegaApi;
}

namespace MegaCustom {

class FileController;

/**
 * Dialog for browsing and selecting files/folders in MEGA cloud
 * Supports single or multiple selection of files and/or folders
 */
class RemoteFolderBrowserDialog : public QDialog {
    Q_OBJECT

public:
    enum SelectionMode {
        SingleFolder,      // Select one folder only
        SingleFile,        // Select one file only
        SingleItem,        // Select one file or folder
        MultipleFolders,   // Select multiple folders
        MultipleFiles,     // Select multiple files
        MultipleItems      // Select multiple files and/or folders
    };

    explicit RemoteFolderBrowserDialog(QWidget* parent = nullptr);
    ~RemoteFolderBrowserDialog();

    /**
     * Set file controller for fetching remote file list
     */
    void setFileController(FileController* controller);

    /**
     * Set MegaApi directly for browsing a specific account
     * Creates an internal FileController
     * @param api The MegaApi instance for the target account
     * @param accountName Display name shown in title (e.g., "Photos Account")
     */
    void setMegaApi(mega::MegaApi* api, const QString& accountName = QString());

    /**
     * Set selection mode
     */
    void setSelectionMode(SelectionMode mode);

    /**
     * Set initial path to navigate to
     */
    void setInitialPath(const QString& path);

    /**
     * Get selected paths
     */
    QStringList selectedPaths() const;

    /**
     * Get single selected path (for single selection modes)
     */
    QString selectedPath() const;

    /**
     * Set dialog title
     */
    void setTitle(const QString& title);

    /**
     * Refresh the current folder
     */
    void refresh();

public slots:
    /**
     * Navigate to path
     */
    void navigateTo(const QString& path);

private slots:
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onItemSelectionChanged();
    void onUpClicked();
    void onRefreshClicked();
    void onPathEditReturnPressed();
    void onFileListReceived(const QStringList& files);
    void onOkClicked();
    void onSelectCurrentFolderClicked();
    void onSearchTextChanged(const QString& text);
    void onGlobalSearchToggled(bool checked);
    void onSearchResultsReceived(const QVariantList& results);

private:
    void setupUI();
    void updateButtonStates();
    void loadPath(const QString& path);
    QTreeWidgetItem* createItem(const QString& name, bool isFolder, qint64 size = 0);
    QString formatSize(qint64 bytes) const;

private:
    FileController* m_fileController = nullptr;
    FileController* m_ownedFileController = nullptr;  // Owned when using setMegaApi
    SelectionMode m_selectionMode = SingleFolder;
    QString m_currentPath = "/";
    QStringList m_selectedPaths;
    QString m_accountName;
    bool m_isGlobalSearchMode = true;  // Global search is default
    QTimer* m_searchTimer = nullptr;

    // UI elements
    QLineEdit* m_pathEdit = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QCheckBox* m_globalSearchCheck = nullptr;
    QPushButton* m_upBtn = nullptr;
    QPushButton* m_refreshBtn = nullptr;
    QTreeWidget* m_treeWidget = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_selectCurrentBtn = nullptr;
    QPushButton* m_okBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    LoadingSpinner* m_loadingSpinner = nullptr;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_REMOTEFOLDERBROWSERDIALOG_H
