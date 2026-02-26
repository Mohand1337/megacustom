#include "CopyHelper.h"
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QAction>
#include <QShortcut>
#include <QKeySequence>
#include <QHeaderView>
#include <QComboBox>
#include <QCheckBox>

namespace MegaCustom {

// === QTableWidget Support ===

// Helper: get text from a cell, handling cell widgets (QComboBox, QCheckBox)
static QString getCellText(QTableWidget* table, int row, int col) {
    QWidget* cellWidget = table->cellWidget(row, col);
    if (auto* combo = qobject_cast<QComboBox*>(cellWidget)) {
        return combo->currentText();
    }
    if (auto* check = qobject_cast<QCheckBox*>(cellWidget)) {
        return check->isChecked() ? "Yes" : "No";
    }
    // Check for QCheckBox inside a container widget (e.g., centered checkbox)
    if (cellWidget) {
        if (auto* check = cellWidget->findChild<QCheckBox*>()) {
            return check->isChecked() ? "Yes" : "No";
        }
    }
    if (QTableWidgetItem* item = table->item(row, col)) {
        return item->text();
    }
    return {};
}

void CopyHelper::installTableCopyMenu(QTableWidget* table) {
    if (!table) return;

    table->setContextMenuPolicy(Qt::CustomContextMenu);

    QObject::connect(table, &QTableWidget::customContextMenuRequested,
        table, [table](const QPoint& pos) {
            QTableWidgetItem* item = table->itemAt(pos);
            if (!item && table->selectedItems().isEmpty()) return;

            QMenu menu(table);

            // Copy Cell — only if clicked on a valid cell
            QAction* copyCellAction = nullptr;
            if (item) {
                copyCellAction = menu.addAction("Copy Cell");
            }

            // Copy Row
            QAction* copyRowAction = nullptr;
            int row = item ? item->row() : -1;
            if (row >= 0) {
                copyRowAction = menu.addAction("Copy Row");
            }

            // Copy Column
            QAction* copyColAction = nullptr;
            int col = item ? item->column() : -1;
            if (col >= 0) {
                QString headerText;
                if (table->horizontalHeaderItem(col)) {
                    headerText = table->horizontalHeaderItem(col)->text();
                }
                copyColAction = menu.addAction(
                    headerText.isEmpty() ? "Copy Column" : QString("Copy Column \"%1\"").arg(headerText));
            }

            // Copy All Selected (only if multiple items selected)
            QAction* copySelectedAction = nullptr;
            if (table->selectedItems().size() > 1) {
                menu.addSeparator();
                copySelectedAction = menu.addAction("Copy All Selected");
            }

            QAction* selected = menu.exec(table->viewport()->mapToGlobal(pos));
            if (!selected) return;

            if (selected == copyCellAction && item) {
                QApplication::clipboard()->setText(getCellText(table, item->row(), item->column()));
            } else if (selected == copyRowAction && row >= 0) {
                copyRow(table, row);
            } else if (selected == copyColAction && col >= 0) {
                copyColumn(table, col);
            } else if (selected == copySelectedAction) {
                copySelectedCells(table);
            }
        });

    // Ctrl+C shortcut (parented to table — auto-deleted when table is destroyed)
    auto* shortcut = new QShortcut(QKeySequence::Copy, table, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    QObject::connect(shortcut, &QShortcut::activated, table, [table]() {
        copySelectedCells(table);
    });
}

void CopyHelper::copySelectedCells(QTableWidget* table) {
    if (!table) return;

    QList<QTableWidgetItem*> items = table->selectedItems();
    if (items.isEmpty()) return;

    // Single item — simple copy
    if (items.size() == 1) {
        QApplication::clipboard()->setText(
            getCellText(table, items.first()->row(), items.first()->column()));
        return;
    }

    // Multiple items — build tab-separated grid
    int minRow = INT_MAX, maxRow = 0, minCol = INT_MAX, maxCol = 0;
    for (const auto* item : items) {
        minRow = qMin(minRow, item->row());
        maxRow = qMax(maxRow, item->row());
        minCol = qMin(minCol, item->column());
        maxCol = qMax(maxCol, item->column());
    }

    // Build a grid of selected cell texts (skipping hidden columns)
    QStringList lines;
    for (int r = minRow; r <= maxRow; ++r) {
        QStringList cols;
        bool rowHasSelection = false;
        for (int c = minCol; c <= maxCol; ++c) {
            if (table->isColumnHidden(c)) continue;
            QTableWidgetItem* item = table->item(r, c);
            if (item && item->isSelected()) {
                rowHasSelection = true;
                cols.append(getCellText(table, r, c));
            } else {
                cols.append("");
            }
        }
        if (rowHasSelection) {
            lines.append(cols.join("\t"));
        }
    }

    QApplication::clipboard()->setText(lines.join("\n"));
}

void CopyHelper::copyRow(QTableWidget* table, int row) {
    if (!table || row < 0 || row >= table->rowCount()) return;

    QStringList cells;
    for (int c = 0; c < table->columnCount(); ++c) {
        if (table->isColumnHidden(c)) continue;
        cells.append(getCellText(table, row, c));
    }

    QApplication::clipboard()->setText(cells.join("\t"));
}

void CopyHelper::copyColumn(QTableWidget* table, int col) {
    if (!table || col < 0 || col >= table->columnCount()) return;

    QStringList values;
    for (int r = 0; r < table->rowCount(); ++r) {
        values.append(getCellText(table, r, col));
    }

    QApplication::clipboard()->setText(values.join("\n"));
}

// === QListWidget Support ===

void CopyHelper::installListCopyMenu(QListWidget* list) {
    if (!list) return;

    list->setContextMenuPolicy(Qt::CustomContextMenu);

    QObject::connect(list, &QListWidget::customContextMenuRequested,
        list, [list](const QPoint& pos) {
            QListWidgetItem* item = list->itemAt(pos);

            QMenu menu(list);

            QAction* copyAction = nullptr;
            if (item) {
                copyAction = menu.addAction("Copy");
            }

            QAction* copyAllAction = nullptr;
            if (list->count() > 0) {
                copyAllAction = menu.addAction("Copy All");
            }

            if (!copyAction && !copyAllAction) return;

            QAction* selected = menu.exec(list->viewport()->mapToGlobal(pos));
            if (!selected) return;

            if (selected == copyAction && item) {
                QApplication::clipboard()->setText(item->text());
            } else if (selected == copyAllAction) {
                QStringList allItems;
                for (int i = 0; i < list->count(); ++i) {
                    allItems.append(list->item(i)->text());
                }
                QApplication::clipboard()->setText(allItems.join("\n"));
            }
        });

    // Ctrl+C shortcut (parented to list — auto-deleted when list is destroyed)
    auto* shortcut = new QShortcut(QKeySequence::Copy, list, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    QObject::connect(shortcut, &QShortcut::activated, list, [list]() {
        QList<QListWidgetItem*> items = list->selectedItems();
        if (items.isEmpty()) return;
        QStringList texts;
        for (auto* item : items) {
            texts.append(item->text());
        }
        QApplication::clipboard()->setText(texts.join("\n"));
    });
}

// === QLabel Support ===

void CopyHelper::makeSelectable(QLabel* label) {
    if (!label) return;
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    label->setCursor(Qt::IBeamCursor);
}

} // namespace MegaCustom
