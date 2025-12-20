#ifndef MEGACUSTOM_BULKPATHEDITORDIALOG_H
#define MEGACUSTOM_BULKPATHEDITORDIALOG_H

#include <QDialog>
#include <QStringList>
#include <QVector>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QListWidget>

namespace MegaCustom {

/**
 * @brief Data structure for a path segment analysis
 */
struct PathSegment {
    QString value;          // The segment value
    bool isVariable;        // True if this segment varies across paths
    QStringList variants;   // All unique values if variable
};

/**
 * @brief Dialog for bulk editing multiple destination paths
 *
 * Analyzes multiple paths to find common/variable segments and allows
 * editing the common parts while preserving the variable parts (like member names).
 *
 * Example paths:
 *   /Alen Sultanic - NHB+ - EGBs/0. Nothing Held Back+/Fast Forward/November.
 *   /Alen Sultanic - NHB+ - EGBs/3. Icekkk/Fast Forward/November.
 *   /Alen Sultanic - NHB+ - EGBs/5. David/Fast Forward/November.
 *
 * Will detect:
 *   Segment 0: "Alen Sultanic - NHB+ - EGBs" (common - editable)
 *   Segment 1: "0. Nothing Held Back+", "3. Icekkk", "5. David" (variable - preserved)
 *   Segment 2: "Fast Forward" (common - editable)
 *   Segment 3: "November. " (common - editable)
 */
class BulkPathEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit BulkPathEditorDialog(QWidget* parent = nullptr);
    ~BulkPathEditorDialog() = default;

    /**
     * @brief Set the paths to analyze and edit
     * @param paths List of remote paths
     */
    void setPaths(const QStringList& paths);

    /**
     * @brief Get the modified paths after editing
     * @return List of updated paths
     */
    QStringList getModifiedPaths() const;

private slots:
    void onSegmentEdited(int segmentIndex, const QString& newValue);
    void onApplyClicked();
    void onPreviewClicked();
    void updatePreview();

private:
    void setupUI();
    void analyzePaths();
    void buildSegmentEditors();
    QStringList splitPath(const QString& path) const;
    QString joinPath(const QStringList& segments) const;

private:
    QStringList m_originalPaths;
    QStringList m_modifiedPaths;
    QVector<PathSegment> m_segments;
    int m_maxSegments = 0;

    // UI components
    QVBoxLayout* m_segmentLayout = nullptr;
    QVector<QLineEdit*> m_segmentEdits;
    QListWidget* m_previewList = nullptr;
    QLabel* m_infoLabel = nullptr;
    QPushButton* m_applyBtn = nullptr;
    QPushButton* m_previewBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_BULKPATHEDITORDIALOG_H
