#include "MegaProxyStyle.h"
#include "DesignTokens.h"
#include "ThemeManager.h"
#include <QStyleFactory>
#include <QStyleOptionMenuItem>
#include <QMenu>
#include <QMenuBar>
#include <QDebug>

namespace MegaCustom {

MegaProxyStyle::MegaProxyStyle(QStyle* style)
    : QProxyStyle(style ? style : QStyleFactory::create("Fusion"))
{
    updateColorsFromTheme();

    // Connect to theme changes
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &MegaProxyStyle::onThemeChanged);

    qDebug() << "MegaProxyStyle: Initialized with MEGA Red highlight color (from DesignTokens)";
}

MegaProxyStyle::MegaProxyStyle(const QString& key)
    : QProxyStyle(key.isEmpty() ? QStyleFactory::create("Fusion") : QStyleFactory::create(key))
{
    updateColorsFromTheme();

    // Connect to theme changes
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &MegaProxyStyle::onThemeChanged);

    qDebug() << "MegaProxyStyle: Initialized with key" << key;
}

void MegaProxyStyle::updateColorsFromTheme()
{
    // Get brand color from DesignTokens based on current theme
    QColor brandColor = ThemeManager::instance().brandDefault();

    // Apply highlight alpha for menu selection
    m_highlightColor = QColor(brandColor.red(), brandColor.green(), brandColor.blue(), HIGHLIGHT_ALPHA);

    // Text color from theme
    m_highlightTextColor = ThemeManager::instance().textPrimary();
}

void MegaProxyStyle::onThemeChanged()
{
    updateColorsFromTheme();
}

QPalette MegaProxyStyle::standardPalette() const
{
    QPalette palette = QProxyStyle::standardPalette();

    // Override highlight colors with MEGA brand colors
    palette.setColor(QPalette::Highlight, m_highlightColor);
    palette.setColor(QPalette::HighlightedText, m_highlightTextColor);

    // Also set for all color groups to ensure consistency
    palette.setColor(QPalette::Active, QPalette::Highlight, m_highlightColor);
    palette.setColor(QPalette::Active, QPalette::HighlightedText, m_highlightTextColor);
    palette.setColor(QPalette::Inactive, QPalette::Highlight, m_highlightColor);
    palette.setColor(QPalette::Inactive, QPalette::HighlightedText, m_highlightTextColor);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, m_highlightColor);
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, m_highlightTextColor);

    return palette;
}

void MegaProxyStyle::drawControl(ControlElement element, const QStyleOption* option,
                                  QPainter* painter, const QWidget* widget) const
{
    // Custom drawing for menu items - FULLY override when selected
    if (element == CE_MenuItem) {
        const QStyleOptionMenuItem* menuOption = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
        if (menuOption) {
            bool selected = menuOption->state & State_Selected;
            bool enabled = menuOption->state & State_Enabled;

            if (selected && enabled) {
                // FULLY CUSTOM DRAWING - don't delegate to base style at all for selection
                painter->save();
                painter->setRenderHint(QPainter::Antialiasing, true);

                // Draw the highlight background
                QRect highlightRect = option->rect;
                highlightRect.adjust(4, 2, -4, -2);

                painter->setPen(Qt::NoPen);
                painter->setBrush(m_highlightColor);
                painter->drawRoundedRect(highlightRect, 4, 4);

                // Now draw the menu item text/icon ourselves
                QRect textRect = option->rect;
                textRect.adjust(24, 0, -8, 0);  // Leave room for icon/checkmark

                // Draw icon if present
                if (!menuOption->icon.isNull()) {
                    QRect iconRect = option->rect;
                    iconRect.setWidth(20);
                    iconRect.adjust(4, 4, 0, -4);
                    QIcon::Mode mode = enabled ? QIcon::Normal : QIcon::Disabled;
                    menuOption->icon.paint(painter, iconRect, Qt::AlignCenter, mode);
                }

                // Draw text
                painter->setPen(m_highlightTextColor);
                QString text = menuOption->text;

                // Handle shortcut (after tab character)
                int tabIndex = text.indexOf('\t');
                if (tabIndex >= 0) {
                    QString mainText = text.left(tabIndex);
                    QString shortcut = text.mid(tabIndex + 1);

                    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, mainText);
                    painter->drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, shortcut);
                } else {
                    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);
                }

                painter->restore();
                return;
            }
        }
    }
    // Custom drawing for menu bar items - FULLY override when selected
    else if (element == CE_MenuBarItem) {
        const QStyleOptionMenuItem* menuOption = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
        if (menuOption) {
            bool selected = menuOption->state & State_Selected;
            bool sunken = menuOption->state & State_Sunken;

            if (selected || sunken) {
                // FULLY CUSTOM DRAWING
                painter->save();
                painter->setRenderHint(QPainter::Antialiasing, true);

                // Draw highlight background
                QRect highlightRect = option->rect;
                highlightRect.adjust(2, 2, -2, -2);

                painter->setPen(Qt::NoPen);
                painter->setBrush(m_highlightColor);
                painter->drawRoundedRect(highlightRect, 4, 4);

                // Draw text
                painter->setPen(m_highlightTextColor);
                painter->drawText(option->rect, Qt::AlignCenter, menuOption->text);

                painter->restore();
                return;
            }
        }
    }

    // Default handling for other elements
    QProxyStyle::drawControl(element, option, painter, widget);
}

void MegaProxyStyle::drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                                    QPainter* painter, const QWidget* widget) const
{
    // Handle panel menu item - this draws the background!
    if (element == PE_PanelMenuBar) {
        // Let base style draw menu bar background
        QProxyStyle::drawPrimitive(element, option, painter, widget);
        return;
    }

    // Handle frame focus for menu items
    if (element == PE_FrameFocusRect) {
        // Check if this is for a menu - if so, don't draw focus rect
        if (qobject_cast<const QMenu*>(widget) || qobject_cast<const QMenuBar*>(widget)) {
            return;  // Skip drawing focus rectangle for menus
        }
    }

    // Default handling
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

} // namespace MegaCustom
