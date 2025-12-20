#ifndef MEGACUSTOM_ICONPROVIDER_H
#define MEGACUSTOM_ICONPROVIDER_H

#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QHash>
#include <QColor>

namespace MegaCustom {

/**
 * @brief Singleton utility class for loading and managing icon states
 *
 * IconProvider provides centralized icon management with automatic generation
 * of hover and disabled state variants from base SVG icons. It caches generated
 * icons for performance.
 *
 * Features:
 * - Load SVG icons from resources
 * - Automatically generate hover state (slightly lighter/highlighted)
 * - Automatically generate disabled state (grayed out)
 * - Cache generated icons for performance
 * - Support for custom icon paths and colors
 *
 * Example usage:
 * @code
 * // Get normal state icon
 * QIcon settingsIcon = IconProvider::instance().icon("settings");
 *
 * // Get hover state icon
 * QIcon settingsHover = IconProvider::instance().icon("settings", IconProvider::Hover);
 *
 * // Get disabled state icon
 * QIcon settingsDisabled = IconProvider::instance().icon("settings", IconProvider::Disabled);
 *
 * // Set custom icon base path
 * IconProvider::instance().setIconBasePath(":/custom/icons/");
 * @endcode
 */
class IconProvider
{
public:
    /**
     * @brief Icon state enumeration
     */
    enum State {
        Normal,     ///< Normal/default icon state
        Hover,      ///< Hover state (highlighted/lighter)
        Disabled    ///< Disabled state (grayed out)
    };

    /**
     * @brief Get the singleton instance
     * @return Reference to the IconProvider instance
     */
    static IconProvider& instance();

    /**
     * @brief Load an icon with the specified state
     * @param name Icon name (without extension, e.g., "settings")
     * @param state Icon state (Normal, Hover, or Disabled)
     * @return QIcon with the requested state
     */
    QIcon icon(const QString& name, State state = Normal);

    /**
     * @brief Set the base path for icon resources
     * @param path Base path for icons (default: ":/icons/")
     */
    void setIconBasePath(const QString& path);

    /**
     * @brief Get the current icon base path
     * @return Current base path for icons
     */
    QString iconBasePath() const;

    /**
     * @brief Set the hover state color tint
     * @param color Color to tint hover icons (default: lighter variant)
     */
    void setHoverColor(const QColor& color);

    /**
     * @brief Set the disabled state color
     * @param color Color for disabled icons (default: gray)
     */
    void setDisabledColor(const QColor& color);

    /**
     * @brief Clear the icon cache
     */
    void clearCache();

    /**
     * @brief Check if an icon exists
     * @param name Icon name
     * @return true if the icon file exists, false otherwise
     */
    bool iconExists(const QString& name) const;

private:
    // Private constructor for singleton
    IconProvider();
    ~IconProvider() = default;

    // Prevent copying
    IconProvider(const IconProvider&) = delete;
    IconProvider& operator=(const IconProvider&) = delete;

    /**
     * @brief Load SVG icon and convert to pixmap
     * @param name Icon name
     * @return Base pixmap from SVG
     */
    QPixmap loadSvgIcon(const QString& name) const;

    /**
     * @brief Generate hover state variant
     * @param base Base pixmap
     * @return Hover state pixmap
     */
    QPixmap generateHoverIcon(const QPixmap& base) const;

    /**
     * @brief Generate disabled state variant
     * @param base Base pixmap
     * @return Disabled state pixmap
     */
    QPixmap generateDisabledIcon(const QPixmap& base) const;

    /**
     * @brief Apply color tint to pixmap
     * @param pixmap Source pixmap
     * @param color Tint color
     * @param strength Tint strength (0.0 to 1.0)
     * @return Tinted pixmap
     */
    QPixmap applyColorTint(const QPixmap& pixmap, const QColor& color, qreal strength) const;

    /**
     * @brief Create cache key for icon state
     * @param name Icon name
     * @param state Icon state
     * @return Cache key string
     */
    QString cacheKey(const QString& name, State state) const;

    QString m_basePath;              ///< Base path for icon resources
    QColor m_hoverColor;             ///< Color tint for hover state
    QColor m_disabledColor;          ///< Color for disabled state
    QHash<QString, QIcon> m_cache;   ///< Icon cache
    int m_defaultSize;               ///< Default icon size
};

} // namespace MegaCustom

#endif // MEGACUSTOM_ICONPROVIDER_H
