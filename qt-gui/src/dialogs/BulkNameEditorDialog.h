#ifndef MEGACUSTOM_BULKNAMEEDITORDIALOG_H
#define MEGACUSTOM_BULKNAMEEDITORDIALOG_H

#include <QDialog>
#include <QStringList>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QRadioButton>
#include <QRegularExpression>

namespace MegaCustom {

/**
 * @brief Result structure for a renamed item
 */
struct RenameResult {
    QString originalPath;   // Full path to the item
    QString originalName;   // Original filename
    QString newName;        // New filename after rename
    bool isFolder;
    bool willChange;        // True if name will actually change
};

/**
 * @brief Dialog for bulk renaming files/folders with pattern detection
 *
 * Features:
 * - Auto-detects common patterns in selected names
 * - Find/Replace mode with optional regex
 * - Live preview of changes
 * - Similar UX to BulkPathEditorDialog
 *
 * Example:
 *   Selected files: "Report_2024_Q1.pdf", "Report_2024_Q2.pdf", "Report_2024_Q3.pdf"
 *   Auto-detects: "Report_2024_Q" is common, "1/2/3" varies
 *   User can replace "2024" with "2025" to get: "Report_2025_Q1.pdf", etc.
 */
class BulkNameEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit BulkNameEditorDialog(QWidget* parent = nullptr);
    ~BulkNameEditorDialog() = default;

    /**
     * @brief Set the items to rename
     * @param paths Full paths to the files/folders
     * @param names File/folder names
     * @param isFolders Whether each item is a folder
     */
    void setItems(const QStringList& paths, const QStringList& names, const QList<bool>& isFolders);

    /**
     * @brief Get the rename results
     * @return List of rename operations to perform
     */
    QList<RenameResult> getRenameResults() const;

    /**
     * @brief Check if any renames will happen
     */
    bool hasChanges() const;

private slots:
    void onFindTextChanged(const QString& text);
    void onReplaceTextChanged(const QString& text);
    void onRegexToggled(bool checked);
    void onCaseSensitiveToggled(bool checked);
    void onPatternSelected(int index);
    void updatePreview();
    void onApplyClicked();

private:
    void setupUI();
    void analyzeNames();
    void detectCommonPatterns();
    QString applyReplacement(const QString& name) const;

private:
    // Original data
    QStringList m_originalPaths;
    QStringList m_originalNames;
    QList<bool> m_isFolders;

    // Detected patterns
    QStringList m_detectedPatterns;
    QString m_commonPrefix;
    QString m_commonSuffix;

    // UI components - Pattern detection
    QGroupBox* m_patternGroup;
    QLabel* m_patternInfoLabel;
    QComboBox* m_patternCombo;

    // UI components - Find/Replace
    QGroupBox* m_findReplaceGroup;
    QLineEdit* m_findEdit;
    QLineEdit* m_replaceEdit;
    QCheckBox* m_regexCheck;
    QCheckBox* m_caseSensitiveCheck;

    // UI components - Preview
    QGroupBox* m_previewGroup;
    QListWidget* m_previewList;
    QLabel* m_changesLabel;

    // Buttons
    QPushButton* m_applyBtn;
    QPushButton* m_cancelBtn;

    // State
    QList<RenameResult> m_results;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_BULKNAMEEDITORDIALOG_H
