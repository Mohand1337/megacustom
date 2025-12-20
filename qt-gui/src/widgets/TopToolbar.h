#ifndef TOPTOOLBAR_H
#define TOPTOOLBAR_H

#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QFrame>
#include <QButtonGroup>
#include <QKeyEvent>

namespace MegaCustom {

class BreadcrumbWidget;
class IconButton;

/**
 * @brief MEGA-style top toolbar widget
 *
 * Provides horizontal toolbar with:
 * - Breadcrumb navigation (left)
 * - Search field (center)
 * - Action buttons: Upload, Download, New Folder, Delete (right)
 * - View mode toggle: Grid/List/Detail
 */
class TopToolbar : public QWidget
{
    Q_OBJECT

public:
    explicit TopToolbar(QWidget* parent = nullptr);
    ~TopToolbar() override = default;

    // Path management
    void setCurrentPath(const QString& path);
    QString currentPath() const { return m_currentPath; }

    // Enable/disable based on context
    void setActionsEnabled(bool enabled);
    void setUploadEnabled(bool enabled);
    void setDownloadEnabled(bool enabled);
    void setDeleteEnabled(bool enabled);

    // Search widget geometry for positioning dropdown panel
    QRect searchWidgetGeometry() const;
    QPoint searchWidgetGlobalPos() const;

signals:
    // Breadcrumb navigation
    void pathSegmentClicked(const QString& path);

    // Search - for instant search panel
    void searchTextChanged(const QString& text);
    void searchRequested(const QString& text);
    void searchFocusGained();
    void searchFocusLost();
    void searchKeyPressed(QKeyEvent* event);

    // Actions
    void uploadClicked();
    void downloadClicked();
    void newFolderClicked();
    void createFileClicked();
    void deleteClicked();
    void refreshClicked();

private slots:
    void onSearchTextChanged(const QString& text);
    void onSearchReturnPressed();

private:
    void setupUI();
    void setupNavigationSection();
    void setupSearchSection();
    void setupActionsSection();

    // Helper methods removed - now using ButtonFactory

    // Layout
    QHBoxLayout* m_mainLayout;

    // Navigation section
    BreadcrumbWidget* m_breadcrumb;

    // Search section
    QLineEdit* m_searchEdit;

    // Actions section
    QPushButton* m_uploadBtn;          // Text+icon button (primary action)
    IconButton* m_downloadBtn;          // Icon-only buttons
    IconButton* m_newFolderBtn;
    IconButton* m_createFileBtn;
    IconButton* m_deleteBtn;
    IconButton* m_refreshBtn;

    // State
    QString m_currentPath;
};

} // namespace MegaCustom

#endif // TOPTOOLBAR_H
