#ifndef BREADCRUMBWIDGET_H
#define BREADCRUMBWIDGET_H

#include <QWidget>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QVector>

namespace MegaCustom {

/**
 * @brief Clickable breadcrumb navigation widget
 *
 * Displays path as clickable segments:
 * Cloud Drive > Folder1 > Folder2 > CurrentFolder
 *
 * Clicking any segment navigates to that path.
 */
class BreadcrumbWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BreadcrumbWidget(QWidget* parent = nullptr);
    ~BreadcrumbWidget() override = default;

    // Set the current path
    void setPath(const QString& path);
    QString path() const { return m_path; }

    // Customization
    void setRootName(const QString& name);
    void setSeparator(const QString& separator);

signals:
    // Emitted when a path segment is clicked
    void pathClicked(const QString& path);

private slots:
    void onSegmentClicked();

private:
    void rebuildBreadcrumb();
    void clearSegments();
    QPushButton* createSegmentButton(const QString& text, const QString& fullPath);
    QLabel* createSeparatorLabel();

    QHBoxLayout* m_layout;
    QString m_path;
    QString m_rootName;
    QString m_separator;

    QVector<QPushButton*> m_segmentButtons;
    QVector<QLabel*> m_separatorLabels;
};

} // namespace MegaCustom

#endif // BREADCRUMBWIDGET_H
