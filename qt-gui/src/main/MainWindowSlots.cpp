// MainWindowSlots.cpp - Slot implementations for MainWindow
#include "MainWindow.h"
#include "widgets/FileExplorer.h"
#include <QMessageBox>
#include <QFileDialog>
#include <QMenu>
#include <QDebug>

namespace MegaCustom {

// File menu slots
void MainWindow::onProperties() {
    qDebug() << "Properties action triggered";
}

// Edit menu slots
void MainWindow::onCut() {
    if (m_remoteExplorer) {
        m_remoteExplorer->cutSelected();
    }
}

void MainWindow::onCopy() {
    if (m_remoteExplorer) {
        m_remoteExplorer->copySelected();
    }
}

void MainWindow::onPaste() {
    if (m_remoteExplorer) {
        m_remoteExplorer->paste();
    }
}

void MainWindow::onSelectAll() {
    if (m_remoteExplorer) {
        m_remoteExplorer->selectAll();
    }
}

void MainWindow::onFind() {
    qDebug() << "Find action triggered";
}

// View menu slots
void MainWindow::onShowHidden() {
    if (m_remoteExplorer && m_showHiddenAction) {
        bool show = m_showHiddenAction->isChecked();
        m_remoteExplorer->setShowHidden(show);
    }
}

void MainWindow::onSortByName() {
    if (m_remoteExplorer) {
        m_remoteExplorer->sortByColumn(0, Qt::AscendingOrder);
    }
}

void MainWindow::onSortBySize() {
    if (m_remoteExplorer) {
        m_remoteExplorer->sortByColumn(1, Qt::DescendingOrder);
    }
}

void MainWindow::onSortByDate() {
    if (m_remoteExplorer) {
        m_remoteExplorer->sortByColumn(2, Qt::DescendingOrder);
    }
}

// Tools menu slots
void MainWindow::onRegexRename() {
    qDebug() << "Regex rename action triggered";
}

// Help menu slots
void MainWindow::onHelp() {
    qDebug() << "Help action triggered";
}

void MainWindow::onAbout() {
    QMessageBox::about(this, "About MegaCustom",
        "MegaCustom Qt6 GUI\nVersion 1.0.0\n\nA modern desktop client for Mega cloud storage.");
}

// File explorer slots
void MainWindow::onContextMenuRequested(const QPoint& pos) {
    if (!m_remoteExplorer) return;

    // Get the global position for the menu
    QPoint globalPos = m_remoteExplorer->mapToGlobal(pos);

    // Check if there's a selection
    bool hasSelection = m_remoteExplorer->hasSelection();

    // Create context menu
    QMenu contextMenu(this);

    // File operations (enabled only with selection)
    QAction* downloadAction = contextMenu.addAction(QIcon(":/icons/download.svg"), "Download");
    downloadAction->setEnabled(hasSelection);
    connect(downloadAction, &QAction::triggered, this, &MainWindow::onDownload);

    contextMenu.addSeparator();

    // Edit operations
    QAction* cutAction = contextMenu.addAction(QIcon(":/icons/scissors.svg"), "Cut");
    cutAction->setShortcut(QKeySequence::Cut);
    cutAction->setEnabled(hasSelection);
    connect(cutAction, &QAction::triggered, this, &MainWindow::onCut);

    QAction* copyAction = contextMenu.addAction(QIcon(":/icons/copy.svg"), "Copy");
    copyAction->setShortcut(QKeySequence::Copy);
    copyAction->setEnabled(hasSelection);
    connect(copyAction, &QAction::triggered, this, &MainWindow::onCopy);

    QAction* pasteAction = contextMenu.addAction(QIcon(":/icons/clipboard.svg"), "Paste");
    pasteAction->setShortcut(QKeySequence::Paste);
    pasteAction->setEnabled(m_remoteExplorer->hasClipboard());
    connect(pasteAction, &QAction::triggered, this, &MainWindow::onPaste);

    contextMenu.addSeparator();

    // Rename and delete
    QAction* renameAction = contextMenu.addAction(QIcon(":/icons/edit.svg"), "Rename");
    renameAction->setEnabled(hasSelection);
    connect(renameAction, &QAction::triggered, this, &MainWindow::onRename);

    QAction* deleteAction = contextMenu.addAction(QIcon(":/icons/trash-2.svg"), "Delete");
    deleteAction->setShortcut(QKeySequence::Delete);
    deleteAction->setEnabled(hasSelection);
    connect(deleteAction, &QAction::triggered, this, &MainWindow::onDelete);

    contextMenu.addSeparator();

    // Create operations (always available)
    QAction* newFolderAction = contextMenu.addAction(QIcon(":/icons/folder-plus.svg"), "New Folder");
    connect(newFolderAction, &QAction::triggered, this, &MainWindow::onNewFolder);

    QAction* uploadAction = contextMenu.addAction(QIcon(":/icons/upload.svg"), "Upload Files...");
    connect(uploadAction, &QAction::triggered, this, &MainWindow::onUploadFile);

    contextMenu.addSeparator();

    // Refresh
    QAction* refreshAction = contextMenu.addAction(QIcon(":/icons/refresh-cw.svg"), "Refresh");
    refreshAction->setShortcut(QKeySequence::Refresh);
    connect(refreshAction, &QAction::triggered, this, &MainWindow::onRefresh);

    // Properties (if selection)
    if (hasSelection) {
        contextMenu.addSeparator();
        QAction* propertiesAction = contextMenu.addAction("Properties");
        connect(propertiesAction, &QAction::triggered, this, &MainWindow::onProperties);
    }

    // Show the menu
    contextMenu.exec(globalPos);
}

void MainWindow::onLocalFileSelected(const QString& file) {
    qDebug() << "Local file selected:" << file;
}

void MainWindow::onRemoteFileSelected(const QString& file) {
    qDebug() << "Remote file selected:" << file;
}

void MainWindow::onLocalPathChanged(const QString& path) {
    qDebug() << "Local path changed to:" << path;
}

void MainWindow::onRemotePathChanged(const QString& path) {
    qDebug() << "Remote path changed to:" << path;
}

// State management
QByteArray MainWindow::saveState() const {
    return QByteArray(); // Return empty for now
}

bool MainWindow::restoreState(const QByteArray& state) {
    Q_UNUSED(state);
    return true; // Always succeed for now
}

} // namespace MegaCustom