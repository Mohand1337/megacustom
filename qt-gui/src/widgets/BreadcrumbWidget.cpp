#include "BreadcrumbWidget.h"
#include <QStyle>

namespace MegaCustom {

BreadcrumbWidget::BreadcrumbWidget(QWidget* parent)
    : QWidget(parent)
    , m_layout(nullptr)
    , m_path("/")
    , m_rootName("Cloud Drive")
    , m_separator(">")
{
    setObjectName("BreadcrumbWidget");

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(4);

    // Add stretch at the end to left-align
    m_layout->addStretch();

    rebuildBreadcrumb();
}

void BreadcrumbWidget::setPath(const QString& path)
{
    if (m_path != path) {
        m_path = path;
        rebuildBreadcrumb();
    }
}

void BreadcrumbWidget::setRootName(const QString& name)
{
    if (m_rootName != name) {
        m_rootName = name;
        rebuildBreadcrumb();
    }
}

void BreadcrumbWidget::setSeparator(const QString& separator)
{
    if (m_separator != separator) {
        m_separator = separator;
        rebuildBreadcrumb();
    }
}

void BreadcrumbWidget::clearSegments()
{
    // Remove all segment buttons
    for (QPushButton* btn : m_segmentButtons) {
        m_layout->removeWidget(btn);
        btn->deleteLater();
    }
    m_segmentButtons.clear();

    // Remove all separator labels
    for (QLabel* lbl : m_separatorLabels) {
        m_layout->removeWidget(lbl);
        lbl->deleteLater();
    }
    m_separatorLabels.clear();
}

void BreadcrumbWidget::rebuildBreadcrumb()
{
    clearSegments();

    // Always show root
    QPushButton* rootBtn = createSegmentButton(m_rootName, "/");
    m_segmentButtons.append(rootBtn);
    m_layout->insertWidget(m_layout->count() - 1, rootBtn);  // Before stretch

    // Parse path into segments
    QString cleanPath = m_path;
    if (cleanPath.startsWith('/')) {
        cleanPath = cleanPath.mid(1);
    }
    if (cleanPath.endsWith('/')) {
        cleanPath.chop(1);
    }

    if (!cleanPath.isEmpty()) {
        QStringList segments = cleanPath.split('/');
        QString currentPath = "";

        for (const QString& segment : segments) {
            if (segment.isEmpty()) continue;

            // Add separator
            QLabel* sep = createSeparatorLabel();
            m_separatorLabels.append(sep);
            m_layout->insertWidget(m_layout->count() - 1, sep);

            // Build full path up to this segment
            currentPath += "/" + segment;

            // Add segment button
            QPushButton* btn = createSegmentButton(segment, currentPath);
            m_segmentButtons.append(btn);
            m_layout->insertWidget(m_layout->count() - 1, btn);
        }
    }

    // Style the last button differently (current location)
    if (!m_segmentButtons.isEmpty()) {
        QPushButton* lastBtn = m_segmentButtons.last();
        lastBtn->setObjectName("BreadcrumbCurrent");
        lastBtn->setEnabled(false);  // Can't click current location
    }
}

QPushButton* BreadcrumbWidget::createSegmentButton(const QString& text, const QString& fullPath)
{
    QPushButton* btn = new QPushButton(text, this);
    btn->setObjectName("BreadcrumbSegment");
    btn->setFlat(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setProperty("fullPath", fullPath);

    connect(btn, &QPushButton::clicked, this, &BreadcrumbWidget::onSegmentClicked);

    return btn;
}

QLabel* BreadcrumbWidget::createSeparatorLabel()
{
    QLabel* lbl = new QLabel(m_separator, this);
    lbl->setObjectName("BreadcrumbSeparator");
    return lbl;
}

void BreadcrumbWidget::onSegmentClicked()
{
    QPushButton* btn = qobject_cast<QPushButton*>(sender());
    if (btn) {
        QString fullPath = btn->property("fullPath").toString();
        emit pathClicked(fullPath);
    }
}

} // namespace MegaCustom
