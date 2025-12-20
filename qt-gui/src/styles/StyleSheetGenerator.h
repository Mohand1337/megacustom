// StyleSheetGenerator.h - Generate QSS from DesignTokens
#ifndef STYLESHEETGENERATOR_H
#define STYLESHEETGENERATOR_H

#include <QString>
#include <QColor>
#include "ThemeManager.h"

namespace MegaCustom {

/**
 * @brief Generates QSS stylesheets from DesignTokens.
 *
 * This class reduces duplication between mega_light.qss and mega_dark.qss
 * by programmatically generating common widget styles from design tokens.
 *
 * The generated styles can supplement or replace static QSS files, providing:
 * - Single source of truth (DesignTokens)
 * - Automatic theme consistency
 * - Easier maintenance
 *
 * Usage:
 *   // Generate complete stylesheet for current theme
 *   QString qss = StyleSheetGenerator::generate();
 *   qApp->setStyleSheet(qss);
 *
 *   // Generate specific component styles
 *   QString buttonQss = StyleSheetGenerator::generateButtonStyles();
 *   QString menuQss = StyleSheetGenerator::generateMenuStyles();
 */
class StyleSheetGenerator
{
public:
    /**
     * @brief Generate complete stylesheet for current theme.
     * @return Complete QSS string
     */
    static QString generate();

    /**
     * @brief Generate stylesheet for a specific theme.
     * @param theme Theme to generate for
     * @return Complete QSS string
     */
    static QString generate(ThemeManager::Theme theme);

    // Component-specific generators
    static QString generateButtonStyles();
    static QString generateMenuStyles();
    static QString generateScrollBarStyles();
    static QString generateInputStyles();
    static QString generateTableStyles();
    static QString generateTreeViewStyles();
    static QString generateTabStyles();
    static QString generateToolTipStyles();
    static QString generateProgressBarStyles();
    static QString generateDialogStyles();
    static QString generateSidebarStyles();
    static QString generateStatusBadgeStyles();
    static QString generateNotificationStyles();

private:
    // Helper to get color as hex string
    static QString colorHex(const QColor& color);
    static QString colorRgba(const QColor& color);

    // Get theme manager reference for generation
    static const ThemeManager& tm();
};

} // namespace MegaCustom

#endif // STYLESHEETGENERATOR_H
