#ifndef MEGACUSTOM_COPYHELPER_H
#define MEGACUSTOM_COPYHELPER_H

#include <QTableWidget>
#include <QListWidget>
#include <QLabel>

namespace MegaCustom {

/**
 * Centralized copy-to-clipboard support for all panels.
 *
 * Adds right-click context menus ("Copy Cell", "Copy Row", etc.) to QTableWidget
 * and QListWidget instances, and makes QLabels text-selectable.
 *
 * Usage: Call the appropriate static method in each panel's setupUI().
 * For tables that already have custom context menus, use addCopyActionsToMenu()
 * instead to append copy actions to the existing menu.
 */
class CopyHelper {
public:
    /**
     * Install a right-click context menu with Copy Cell / Copy Row / Copy Column
     * on a QTableWidget. Also installs Ctrl+C shortcut.
     *
     * WARNING: This sets the context menu policy to Qt::CustomContextMenu.
     * Do NOT call this on tables that already have a custom context menu —
     * use addCopyActionsToMenu() for those instead.
     */
    static void installTableCopyMenu(QTableWidget* table);

    /**
     * Install a right-click context menu with Copy / Copy All on a QListWidget.
     * Also installs Ctrl+C shortcut.
     */
    static void installListCopyMenu(QListWidget* list);

    /**
     * Make a QLabel text-selectable with mouse and keyboard (Ctrl+C).
     */
    static void makeSelectable(QLabel* label);

    /**
     * Copy the currently selected cells from a QTableWidget to clipboard.
     * Called by both context menu and Ctrl+C shortcut.
     * - Single cell selected: copies that cell's text
     * - Multiple cells: copies tab-separated with newlines between rows
     */
    static void copySelectedCells(QTableWidget* table);

    /**
     * Copy an entire row's visible text from a QTableWidget.
     * Columns are tab-separated.
     */
    static void copyRow(QTableWidget* table, int row);

    /**
     * Copy all values in a column from a QTableWidget.
     * Values are newline-separated.
     */
    static void copyColumn(QTableWidget* table, int col);
};

} // namespace MegaCustom

#endif // MEGACUSTOM_COPYHELPER_H
