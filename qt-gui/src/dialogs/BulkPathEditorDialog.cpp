#include "BulkPathEditorDialog.h"
#include "widgets/ButtonFactory.h"
#include "utils/PathUtils.h"
#include "utils/DpiScaler.h"
#include "styles/ThemeManager.h"
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFrame>
#include <QSet>
#include <QMessageBox>
#include <QFont>

namespace MegaCustom {

BulkPathEditorDialog::BulkPathEditorDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Smart Bulk Path Editor");
    setMinimumSize(DpiScaler::scale(700), DpiScaler::scale(550));
    setupUI();
}

void BulkPathEditorDialog::setupUI()
{
    auto& tm = ThemeManager::instance();
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(DpiScaler::scale(12));

    // Info label at top
    m_infoLabel = new QLabel(this);
    m_infoLabel->setWordWrap(true);
    m_infoLabel->setStyleSheet(QString("color: %1; padding: %2px; background: %3; border-radius: %4px;")
        .arg(tm.textSecondary().name())
        .arg(DpiScaler::scale(8))
        .arg(tm.surfacePrimary().name())
        .arg(DpiScaler::scale(4)));
    mainLayout->addWidget(m_infoLabel);

    // Segment editors group
    QGroupBox* segmentGroup = new QGroupBox("Path Segments", this);
    segmentGroup->setStyleSheet(QString(
        "QGroupBox { font-weight: bold; border: 1px solid %1; "
        "border-radius: 6px; margin-top: 12px; padding-top: 16px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; }")
        .arg(tm.borderSubtle().name()));

    QScrollArea* scrollArea = new QScrollArea(segmentGroup);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setMinimumHeight(150);

    QWidget* scrollContent = new QWidget();
    m_segmentLayout = new QVBoxLayout(scrollContent);
    m_segmentLayout->setSpacing(8);
    m_segmentLayout->addStretch();

    scrollArea->setWidget(scrollContent);

    QVBoxLayout* segmentGroupLayout = new QVBoxLayout(segmentGroup);
    segmentGroupLayout->addWidget(scrollArea);

    mainLayout->addWidget(segmentGroup);

    // Preview group
    QGroupBox* previewGroup = new QGroupBox("Preview (Modified Paths)", this);
    previewGroup->setStyleSheet(QString(
        "QGroupBox { font-weight: bold; border: 1px solid %1; "
        "border-radius: 6px; margin-top: 12px; padding-top: 16px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; }")
        .arg(tm.borderSubtle().name()));

    QVBoxLayout* previewLayout = new QVBoxLayout(previewGroup);

    m_previewList = new QListWidget(this);
    m_previewList->setAlternatingRowColors(true);
    m_previewList->setFont(QFont("Courier New", 9));
    m_previewList->setMinimumHeight(150);
    previewLayout->addWidget(m_previewList);

    mainLayout->addWidget(previewGroup);

    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    m_previewBtn = ButtonFactory::createSecondary("Refresh Preview", this);
    m_previewBtn->setToolTip("Update preview with current edits");
    connect(m_previewBtn, &QPushButton::clicked, this, &BulkPathEditorDialog::onPreviewClicked);
    btnLayout->addWidget(m_previewBtn);

    m_cancelBtn = ButtonFactory::createOutline("Cancel", this);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(m_cancelBtn);

    m_applyBtn = ButtonFactory::createPrimary("Apply Changes", this);
    connect(m_applyBtn, &QPushButton::clicked, this, &BulkPathEditorDialog::onApplyClicked);
    btnLayout->addWidget(m_applyBtn);

    mainLayout->addLayout(btnLayout);
}

void BulkPathEditorDialog::setPaths(const QStringList& paths)
{
    m_originalPaths = paths;
    m_modifiedPaths = paths;

    if (paths.isEmpty()) {
        m_infoLabel->setText("No paths to edit.");
        return;
    }

    analyzePaths();
    buildSegmentEditors();
    updatePreview();
}

QStringList BulkPathEditorDialog::getModifiedPaths() const
{
    return m_modifiedPaths;
}

QStringList BulkPathEditorDialog::splitPath(const QString& path) const
{
    // Split path by '/' but preserve trailing spaces in segments
    QStringList segments;
    QString normalized = path;

    // Remove leading slash for splitting
    if (normalized.startsWith('/')) {
        normalized = normalized.mid(1);
    }

    // Remove trailing slash for splitting (but we'll track if there was one)
    bool hadTrailingSlash = normalized.endsWith('/');
    if (hadTrailingSlash) {
        normalized.chop(1);
    }

    // Split by '/'
    segments = normalized.split('/');

    return segments;
}

QString BulkPathEditorDialog::joinPath(const QStringList& segments) const
{
    if (segments.isEmpty()) {
        return "/";
    }

    QString result = "/" + segments.join("/");
    return result;
}

void BulkPathEditorDialog::analyzePaths()
{
    m_segments.clear();
    m_maxSegments = 0;

    if (m_originalPaths.isEmpty()) {
        return;
    }

    // Split all paths into segments
    QVector<QStringList> allSegments;
    for (const QString& path : m_originalPaths) {
        QStringList segs = splitPath(path);
        allSegments.append(segs);
        m_maxSegments = qMax(m_maxSegments, segs.size());
    }

    // Analyze each segment position
    for (int i = 0; i < m_maxSegments; ++i) {
        PathSegment seg;
        QSet<QString> uniqueValues;

        for (const QStringList& pathSegs : allSegments) {
            if (i < pathSegs.size()) {
                uniqueValues.insert(pathSegs[i]);
            } else {
                uniqueValues.insert("");  // Missing segment
            }
        }

        seg.variants = uniqueValues.values();
        seg.isVariable = (uniqueValues.size() > 1);

        if (seg.isVariable) {
            seg.value = QString("[%1 variations]").arg(uniqueValues.size());
        } else {
            seg.value = seg.variants.isEmpty() ? "" : seg.variants.first();
        }

        m_segments.append(seg);
    }

    // Update info label
    int variableCount = 0;
    int editableCount = 0;
    for (const PathSegment& seg : m_segments) {
        if (seg.isVariable) variableCount++;
        else editableCount++;
    }

    QString info = QString(
        "<b>%1 path(s) analyzed</b><br>"
        "Found <b>%2</b> editable segments (same across all paths) and "
        "<b>%3</b> variable segments (preserved as-is, like member names).<br>"
        "<i>Edit the green fields to change all paths at once. Yellow fields show variable segments that will be kept unique.</i>")
        .arg(m_originalPaths.size())
        .arg(editableCount)
        .arg(variableCount);
    m_infoLabel->setText(info);
}

void BulkPathEditorDialog::buildSegmentEditors()
{
    auto& tm = ThemeManager::instance();

    // Clear existing editors
    qDeleteAll(m_segmentEdits);
    m_segmentEdits.clear();

    // Remove all widgets from layout except the stretch
    QLayoutItem* item;
    while ((item = m_segmentLayout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            delete item->widget();
        }
        delete item;
    }

    // Build editor for each segment
    for (int i = 0; i < m_segments.size(); ++i) {
        const PathSegment& seg = m_segments[i];

        QFrame* segFrame = new QFrame();
        segFrame->setFrameShape(QFrame::StyledPanel);

        QHBoxLayout* segLayout = new QHBoxLayout(segFrame);
        segLayout->setContentsMargins(8, 4, 8, 4);

        // Segment index label
        QLabel* indexLabel = new QLabel(QString("/%1").arg(i + 1), segFrame);
        indexLabel->setFixedWidth(30);
        indexLabel->setStyleSheet(QString("color: %1; font-weight: bold;")
            .arg(tm.textDisabled().name()));
        segLayout->addWidget(indexLabel);

        if (seg.isVariable) {
            // Variable segment - show as read-only with variations tooltip
            QLabel* varLabel = new QLabel(segFrame);
            varLabel->setText(QString("[VARIABLE: %1 unique values]").arg(seg.variants.size()));
            varLabel->setStyleSheet(QString(
                "background-color: %1; padding: 6px 12px; border-radius: 4px; "
                "color: %2; font-style: italic;")
                .arg(tm.supportWarning().lighter(170).name())
                .arg(tm.supportWarning().darker(120).name()));

            // Build tooltip with all variations
            QString tooltip = "Unique values (preserved as-is):\n";
            int shown = 0;
            for (const QString& var : seg.variants) {
                if (shown++ < 10) {
                    tooltip += QString("  - %1\n").arg(var.isEmpty() ? "(empty)" : var);
                }
            }
            if (seg.variants.size() > 10) {
                tooltip += QString("  ... and %1 more").arg(seg.variants.size() - 10);
            }
            varLabel->setToolTip(tooltip);

            segLayout->addWidget(varLabel, 1);

            // Add placeholder to keep alignment (no edit for variable segments)
            m_segmentEdits.append(nullptr);
        } else {
            // Editable segment - use widget-specific stylesheet for selection colors (Qt6 priority)
            QLineEdit* edit = new QLineEdit(seg.value, segFrame);
            edit->setStyleSheet(QString(
                "QLineEdit {"
                "  background-color: %1;"
                "  padding: 6px;"
                "  border: 1px solid %2;"
                "  border-radius: 4px;"
                "  color: %3;"
                "  selection-background-color: %4;"
                "  selection-color: %5;"
                "}")
                .arg(tm.supportSuccess().lighter(170).name())
                .arg(tm.supportSuccess().name())
                .arg(tm.supportSuccess().darker(150).name())
                .arg(tm.brandDefault().name())
                .arg(tm.textPrimary().name())
            );
            edit->setToolTip("Edit this segment - changes apply to ALL paths");

            // Connect to update preview when edited
            int segIndex = i;
            connect(edit, &QLineEdit::textChanged, this, [this, segIndex](const QString& text) {
                onSegmentEdited(segIndex, text);
            });

            segLayout->addWidget(edit, 1);
            m_segmentEdits.append(edit);
        }

        m_segmentLayout->insertWidget(m_segmentLayout->count() - 1, segFrame);
    }

    m_segmentLayout->addStretch();
}

void BulkPathEditorDialog::onSegmentEdited(int segmentIndex, const QString& newValue)
{
    if (segmentIndex < 0 || segmentIndex >= m_segments.size()) {
        return;
    }

    // Update the segment value (only for non-variable segments)
    if (!m_segments[segmentIndex].isVariable) {
        m_segments[segmentIndex].value = newValue;
    }

    // Auto-update preview after a short delay
    updatePreview();
}

void BulkPathEditorDialog::onPreviewClicked()
{
    updatePreview();
}

void BulkPathEditorDialog::updatePreview()
{
    auto& tm = ThemeManager::instance();
    m_previewList->clear();
    m_modifiedPaths.clear();

    // Split all original paths
    QVector<QStringList> allSegments;
    for (const QString& path : m_originalPaths) {
        allSegments.append(splitPath(path));
    }

    // Rebuild paths with edited segments
    for (int pathIdx = 0; pathIdx < m_originalPaths.size(); ++pathIdx) {
        QStringList newSegs;

        for (int segIdx = 0; segIdx < m_segments.size(); ++segIdx) {
            const PathSegment& seg = m_segments[segIdx];

            if (seg.isVariable) {
                // Keep original value from this path
                if (segIdx < allSegments[pathIdx].size()) {
                    newSegs.append(allSegments[pathIdx][segIdx]);
                }
            } else {
                // Use edited value
                newSegs.append(seg.value);
            }
        }

        QString newPath = joinPath(newSegs);
        m_modifiedPaths.append(newPath);

        // Show in preview with change highlighting
        QString original = m_originalPaths[pathIdx];
        if (newPath != original) {
            QListWidgetItem* item = new QListWidgetItem(newPath);
            item->setForeground(tm.supportSuccess());  // Green for modified
            item->setToolTip(QString("Original: %1").arg(original));
            m_previewList->addItem(item);
        } else {
            m_previewList->addItem(newPath);
        }
    }
}

void BulkPathEditorDialog::onApplyClicked()
{
    // Validate that we have valid paths
    bool hasEmpty = false;
    for (const QString& path : m_modifiedPaths) {
        if (PathUtils::isPathEmpty(path)) {
            hasEmpty = true;
            break;
        }
    }

    if (hasEmpty) {
        QMessageBox::warning(this, "Invalid Path",
            "One or more paths would become empty. Please check your edits.");
        return;
    }

    accept();
}

} // namespace MegaCustom
