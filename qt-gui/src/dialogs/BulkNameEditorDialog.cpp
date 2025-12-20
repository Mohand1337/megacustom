#include "BulkNameEditorDialog.h"
#include "widgets/ButtonFactory.h"
#include "utils/DpiScaler.h"
#include "styles/ThemeManager.h"
#include <QGridLayout>
#include <QScrollArea>
#include <QMessageBox>
#include <QDebug>
#include <QSet>
#include <algorithm>

namespace MegaCustom {

BulkNameEditorDialog::BulkNameEditorDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Bulk Rename");
    setMinimumSize(DpiScaler::scale(600), DpiScaler::scale(500));
    resize(DpiScaler::scale(700), DpiScaler::scale(550));
    setupUI();
}

void BulkNameEditorDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(16, 16, 16, 16);

    // ========================================
    // Pattern Detection Group
    // ========================================
    m_patternGroup = new QGroupBox("Detected Patterns", this);
    m_patternGroup->setToolTip("Automatically detects common text patterns across all selected filenames.\n"
                               "Use the dropdown to quickly select a pattern for replacement.");
    QVBoxLayout* patternLayout = new QVBoxLayout(m_patternGroup);

    m_patternInfoLabel = new QLabel("Analyzing names...", this);
    m_patternInfoLabel->setWordWrap(true);
    m_patternInfoLabel->setStyleSheet(QString("color: %1;")
        .arg(ThemeManager::instance().textSecondary().name()));
    m_patternInfoLabel->setToolTip("Shows common patterns detected in your selected filenames.\n"
                                   "Common patterns can be quickly replaced across all files.");
    patternLayout->addWidget(m_patternInfoLabel);

    QHBoxLayout* patternSelectLayout = new QHBoxLayout();
    QLabel* quickLabel = new QLabel("Quick replace:", this);
    quickLabel->setToolTip("Select a detected pattern to auto-fill the Find field below.");
    patternSelectLayout->addWidget(quickLabel);
    m_patternCombo = new QComboBox(this);
    m_patternCombo->setMinimumWidth(200);
    m_patternCombo->addItem("-- Select a pattern --");
    m_patternCombo->setToolTip("Patterns found in ALL selected filenames.\n"
                               "Click one to quickly set it as the Find text.\n\n"
                               "Example: If files are 'Report_2024_Q1.pdf', 'Report_2024_Q2.pdf',\n"
                               "the pattern '2024' will be detected for easy replacement.");
    connect(m_patternCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BulkNameEditorDialog::onPatternSelected);
    patternSelectLayout->addWidget(m_patternCombo, 1);
    patternSelectLayout->addStretch();
    patternLayout->addLayout(patternSelectLayout);

    mainLayout->addWidget(m_patternGroup);

    // ========================================
    // Find/Replace Group
    // ========================================
    m_findReplaceGroup = new QGroupBox("Find and Replace", this);
    m_findReplaceGroup->setToolTip("Enter text to find and replace in filenames.\n"
                                   "Supports plain text or regular expressions.");
    QGridLayout* frLayout = new QGridLayout(m_findReplaceGroup);
    frLayout->setHorizontalSpacing(12);
    frLayout->setVerticalSpacing(8);

    QLabel* findLabel = new QLabel("Find:", this);
    findLabel->setToolTip("Enter the text you want to find and replace in filenames.");
    frLayout->addWidget(findLabel, 0, 0);
    m_findEdit = new QLineEdit(this);
    m_findEdit->setPlaceholderText("Text or pattern to find");
    m_findEdit->setToolTip("Enter text to search for in filenames.\n\n"
                           "Examples:\n"
                           "  • '2024' - finds the year 2024\n"
                           "  • '_old' - finds '_old' suffix\n"
                           "  • 'draft' - finds 'draft' anywhere in the name\n\n"
                           "Tip: Select a pattern from the dropdown above for quick setup.");
    connect(m_findEdit, &QLineEdit::textChanged, this, &BulkNameEditorDialog::onFindTextChanged);
    frLayout->addWidget(m_findEdit, 0, 1);

    QLabel* replaceLabel = new QLabel("Replace:", this);
    replaceLabel->setToolTip("Enter the replacement text (or leave empty to delete the found text).");
    frLayout->addWidget(replaceLabel, 1, 0);
    m_replaceEdit = new QLineEdit(this);
    m_replaceEdit->setPlaceholderText("Replacement text (leave empty to delete)");
    m_replaceEdit->setToolTip("Enter the replacement text.\n\n"
                              "Examples:\n"
                              "  • '2025' - replaces with 2025\n"
                              "  • '_new' - replaces with '_new'\n"
                              "  • (empty) - deletes the found text\n\n"
                              "With Regex enabled, you can use:\n"
                              "  • $1, $2 - captured groups\n"
                              "  • \\U$1 - uppercase captured group");
    connect(m_replaceEdit, &QLineEdit::textChanged, this, &BulkNameEditorDialog::onReplaceTextChanged);
    frLayout->addWidget(m_replaceEdit, 1, 1);

    QHBoxLayout* optionsLayout = new QHBoxLayout();
    m_regexCheck = new QCheckBox("Use Regular Expression", this);
    m_regexCheck->setToolTip("Enable regular expression (regex) pattern matching.\n\n"
                             "Regex examples:\n"
                             "  • '\\d+' - matches any number\n"
                             "  • '^old_' - matches 'old_' at start\n"
                             "  • '_v\\d+$' - matches '_v1', '_v2' at end\n"
                             "  • '(\\w+)_(\\d+)' - captures word and number\n\n"
                             "Replace with $1, $2 to use captured groups.");
    connect(m_regexCheck, &QCheckBox::toggled, this, &BulkNameEditorDialog::onRegexToggled);
    optionsLayout->addWidget(m_regexCheck);

    m_caseSensitiveCheck = new QCheckBox("Case Sensitive", this);
    m_caseSensitiveCheck->setChecked(true);
    m_caseSensitiveCheck->setToolTip("When enabled, 'Report' and 'report' are treated as different.\n"
                                     "When disabled, both will match 'Report', 'REPORT', 'report', etc.");
    connect(m_caseSensitiveCheck, &QCheckBox::toggled, this, &BulkNameEditorDialog::onCaseSensitiveToggled);
    optionsLayout->addWidget(m_caseSensitiveCheck);
    optionsLayout->addStretch();

    frLayout->addLayout(optionsLayout, 2, 0, 1, 2);

    mainLayout->addWidget(m_findReplaceGroup);

    // ========================================
    // Preview Group
    // ========================================
    m_previewGroup = new QGroupBox("Preview", this);
    m_previewGroup->setToolTip("Live preview of all rename operations.\n"
                               "Green text indicates files that will be renamed.\n"
                               "Gray text indicates files with no changes.");
    QVBoxLayout* previewLayout = new QVBoxLayout(m_previewGroup);

    m_changesLabel = new QLabel("0 files will be renamed", this);
    m_changesLabel->setStyleSheet("font-weight: bold;");
    m_changesLabel->setToolTip("Shows how many files will be affected by the rename operation.");
    previewLayout->addWidget(m_changesLabel);

    m_previewList = new QListWidget(this);
    m_previewList->setAlternatingRowColors(true);
    m_previewList->setToolTip("Preview of rename operations:\n"
                              "  • Green: File will be renamed (shows old -> new)\n"
                              "  • Gray: No changes for this file\n\n"
                              "Hover over individual items for more details.");
    m_previewList->setStyleSheet(R"(
        QListWidget {
            font-family: 'Consolas', 'Monaco', monospace;
            font-size: 11px;
        }
        QListWidget::item {
            padding: 4px;
        }
    )");
    previewLayout->addWidget(m_previewList, 1);

    mainLayout->addWidget(m_previewGroup, 1);

    // ========================================
    // Buttons
    // ========================================
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_cancelBtn = ButtonFactory::createOutline("Cancel", this);
    m_cancelBtn->setToolTip("Close without renaming any files.");
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(m_cancelBtn);

    m_applyBtn = ButtonFactory::createPrimary("Apply Rename", this);
    m_applyBtn->setEnabled(false);
    m_applyBtn->setToolTip("Apply the rename operation to all affected files.\n"
                           "Only files shown in green in the preview will be renamed.");
    connect(m_applyBtn, &QPushButton::clicked, this, &BulkNameEditorDialog::onApplyClicked);
    buttonLayout->addWidget(m_applyBtn);

    mainLayout->addLayout(buttonLayout);
}

void BulkNameEditorDialog::setItems(const QStringList& paths, const QStringList& names, const QList<bool>& isFolders)
{
    m_originalPaths = paths;
    m_originalNames = names;
    m_isFolders = isFolders;

    // Initialize results
    m_results.clear();
    for (int i = 0; i < names.size(); ++i) {
        RenameResult result;
        result.originalPath = i < paths.size() ? paths[i] : QString();
        result.originalName = names[i];
        result.newName = names[i];
        result.isFolder = i < isFolders.size() ? isFolders[i] : false;
        result.willChange = false;
        m_results.append(result);
    }

    analyzeNames();
    updatePreview();
}

void BulkNameEditorDialog::analyzeNames()
{
    if (m_originalNames.isEmpty()) return;

    m_detectedPatterns.clear();
    m_commonPrefix.clear();
    m_commonSuffix.clear();

    // Find common prefix
    if (!m_originalNames.isEmpty()) {
        m_commonPrefix = m_originalNames.first();
        for (const QString& name : m_originalNames) {
            int i = 0;
            while (i < m_commonPrefix.length() && i < name.length() &&
                   m_commonPrefix[i] == name[i]) {
                ++i;
            }
            m_commonPrefix = m_commonPrefix.left(i);
        }
    }

    // Find common suffix (before extension for files)
    if (!m_originalNames.isEmpty()) {
        // Get names without extensions for comparison
        QStringList basenames;
        QStringList extensions;
        for (const QString& name : m_originalNames) {
            int dotPos = name.lastIndexOf('.');
            if (dotPos > 0) {
                basenames.append(name.left(dotPos));
                extensions.append(name.mid(dotPos));
            } else {
                basenames.append(name);
                extensions.append(QString());
            }
        }

        // Check if all extensions are the same
        bool sameExtension = true;
        QString commonExt = extensions.isEmpty() ? QString() : extensions.first();
        for (const QString& ext : extensions) {
            if (ext != commonExt) {
                sameExtension = false;
                break;
            }
        }

        // Find common suffix in basenames
        QString reversed = basenames.first();
        std::reverse(reversed.begin(), reversed.end());
        m_commonSuffix = reversed;

        for (const QString& basename : basenames) {
            QString rev = basename;
            std::reverse(rev.begin(), rev.end());
            int i = 0;
            while (i < m_commonSuffix.length() && i < rev.length() &&
                   m_commonSuffix[i] == rev[i]) {
                ++i;
            }
            m_commonSuffix = m_commonSuffix.left(i);
        }
        std::reverse(m_commonSuffix.begin(), m_commonSuffix.end());

        // Build pattern info
        QString info;
        info = QString("<b>%1 items selected</b><br>").arg(m_originalNames.size());

        if (!m_commonPrefix.isEmpty() && m_commonPrefix.length() > 2) {
            info += QString("Common prefix: <code>%1</code><br>").arg(m_commonPrefix.toHtmlEscaped());
        }
        if (!m_commonSuffix.isEmpty() && m_commonSuffix.length() > 2) {
            info += QString("Common suffix: <code>%1</code><br>").arg(m_commonSuffix.toHtmlEscaped());
        }
        if (sameExtension && !commonExt.isEmpty()) {
            info += QString("Common extension: <code>%1</code>").arg(commonExt.toHtmlEscaped());
        }

        m_patternInfoLabel->setText(info);

        // Detect common substrings that appear in all names
        detectCommonPatterns();
    }
}

void BulkNameEditorDialog::detectCommonPatterns()
{
    if (m_originalNames.size() < 2) return;

    // Find substrings that appear in ALL names (minimum 3 chars)
    QSet<QString> candidates;
    const QString& first = m_originalNames.first();

    // Generate candidate substrings from first name
    for (int len = 3; len <= first.length(); ++len) {
        for (int start = 0; start <= first.length() - len; ++start) {
            QString sub = first.mid(start, len);
            // Skip if it's just whitespace or punctuation
            if (sub.trimmed().length() < 2) continue;
            candidates.insert(sub);
        }
    }

    // Filter to only keep those in ALL names
    for (int i = 1; i < m_originalNames.size(); ++i) {
        QSet<QString> toRemove;
        for (const QString& candidate : candidates) {
            if (!m_originalNames[i].contains(candidate)) {
                toRemove.insert(candidate);
            }
        }
        candidates -= toRemove;
    }

    // Sort by length (longer = more specific)
    QList<QString> sorted = candidates.values();
    std::sort(sorted.begin(), sorted.end(), [](const QString& a, const QString& b) {
        return a.length() > b.length();
    });

    // Keep top patterns, removing substrings of longer patterns
    m_detectedPatterns.clear();
    for (const QString& pattern : sorted) {
        bool isSubstring = false;
        for (const QString& existing : m_detectedPatterns) {
            if (existing.contains(pattern)) {
                isSubstring = true;
                break;
            }
        }
        if (!isSubstring && m_detectedPatterns.size() < 10) {
            m_detectedPatterns.append(pattern);
        }
    }

    // Update combo box
    m_patternCombo->clear();
    m_patternCombo->addItem("-- Select a pattern to replace --");
    for (const QString& pattern : m_detectedPatterns) {
        m_patternCombo->addItem(QString("\"%1\"").arg(pattern), pattern);
    }

    if (m_detectedPatterns.isEmpty()) {
        m_patternCombo->addItem("(No common patterns detected)");
        m_patternCombo->setEnabled(false);
    } else {
        m_patternCombo->setEnabled(true);
    }
}

void BulkNameEditorDialog::onPatternSelected(int index)
{
    if (index <= 0) return;

    QString pattern = m_patternCombo->itemData(index).toString();
    if (!pattern.isEmpty()) {
        m_findEdit->setText(pattern);
        m_replaceEdit->setFocus();
        m_replaceEdit->selectAll();
    }
}

void BulkNameEditorDialog::onFindTextChanged(const QString& text)
{
    Q_UNUSED(text)
    updatePreview();
}

void BulkNameEditorDialog::onReplaceTextChanged(const QString& text)
{
    Q_UNUSED(text)
    updatePreview();
}

void BulkNameEditorDialog::onRegexToggled(bool checked)
{
    Q_UNUSED(checked)
    updatePreview();
}

void BulkNameEditorDialog::onCaseSensitiveToggled(bool checked)
{
    Q_UNUSED(checked)
    updatePreview();
}

QString BulkNameEditorDialog::applyReplacement(const QString& name) const
{
    QString findText = m_findEdit->text();
    QString replaceText = m_replaceEdit->text();

    if (findText.isEmpty()) {
        return name;
    }

    if (m_regexCheck->isChecked()) {
        // Regex replacement
        QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
        if (!m_caseSensitiveCheck->isChecked()) {
            options |= QRegularExpression::CaseInsensitiveOption;
        }

        QRegularExpression regex(findText, options);
        if (!regex.isValid()) {
            return name; // Invalid regex, return unchanged
        }

        QString result = name;
        result.replace(regex, replaceText);
        return result;
    } else {
        // Simple string replacement
        QString result = name;
        if (m_caseSensitiveCheck->isChecked()) {
            result.replace(findText, replaceText);
        } else {
            // Case-insensitive replace
            int index = 0;
            while ((index = result.indexOf(findText, index, Qt::CaseInsensitive)) != -1) {
                result.replace(index, findText.length(), replaceText);
                index += replaceText.length();
            }
        }
        return result;
    }
}

void BulkNameEditorDialog::updatePreview()
{
    m_previewList->clear();

    int changeCount = 0;
    QString findText = m_findEdit->text();

    // Check regex validity if using regex
    bool validRegex = true;
    if (m_regexCheck->isChecked() && !findText.isEmpty()) {
        QRegularExpression regex(findText);
        validRegex = regex.isValid();
        if (!validRegex) {
            m_changesLabel->setText(QString("<span style='color: #E31B57;'>Invalid regex: %1</span>")
                                    .arg(regex.errorString().toHtmlEscaped()));
            m_applyBtn->setEnabled(false);
            return;
        }
    }

    for (int i = 0; i < m_results.size(); ++i) {
        QString originalName = m_results[i].originalName;
        QString newName = applyReplacement(originalName);
        m_results[i].newName = newName;
        m_results[i].willChange = (newName != originalName);

        if (m_results[i].willChange) {
            changeCount++;
        }

        // Add to preview list
        QListWidgetItem* item = new QListWidgetItem();
        if (m_results[i].willChange) {
            item->setText(QString("%1  ->  %2").arg(originalName, newName));
            item->setForeground(QColor(34, 139, 34)); // Green for changes
            item->setToolTip(QString("Original: %1\nNew: %2").arg(originalName, newName));
        } else {
            item->setText(originalName);
            item->setForeground(QColor(128, 128, 128)); // Gray for no change
            item->setToolTip("No change");
        }
        m_previewList->addItem(item);
    }

    m_changesLabel->setText(QString("<b>%1</b> of %2 items will be renamed")
                            .arg(changeCount)
                            .arg(m_results.size()));

    m_applyBtn->setEnabled(changeCount > 0);
}

bool BulkNameEditorDialog::hasChanges() const
{
    for (const RenameResult& result : m_results) {
        if (result.willChange) {
            return true;
        }
    }
    return false;
}

QList<RenameResult> BulkNameEditorDialog::getRenameResults() const
{
    QList<RenameResult> changedResults;
    for (const RenameResult& result : m_results) {
        if (result.willChange) {
            changedResults.append(result);
        }
    }
    return changedResults;
}

void BulkNameEditorDialog::onApplyClicked()
{
    if (!hasChanges()) {
        QMessageBox::information(this, "No Changes", "No files will be renamed.");
        return;
    }

    accept();
}

} // namespace MegaCustom
