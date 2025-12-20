// FileExplorerSlots.cpp - Additional slot implementations for FileExplorer
#include "FileExplorer.h"
#include <QItemSelection>
#include <QResizeEvent>
#include <QDragMoveEvent>
#include <QMimeData>
#include <QDebug>

namespace MegaCustom {

void FileExplorer::onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected) {
    Q_UNUSED(selected);
    Q_UNUSED(deselected);

    // Update status when selection changes
    updateStatus();

    // Emit signal with selected files
    QStringList selectedPaths = selectedFiles();
    emit selectionChanged(selectedPaths);
}

void FileExplorer::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // Debug output removed - was causing noise during dropdown animations
}

void FileExplorer::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

} // namespace MegaCustom