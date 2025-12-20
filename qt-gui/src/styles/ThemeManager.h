// ThemeManager.h - Centralized theme management for MegaCustom
#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QObject>
#include <QColor>
#include <QFont>
#include <QHash>
#include <functional>

namespace MegaCustom {

class ThemeManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Theme currentTheme READ currentTheme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(bool isDarkMode READ isDarkMode NOTIFY themeChanged)

public:
    enum Theme {
        Light,
        Dark,
        System  // Auto-detect from OS
    };
    Q_ENUM(Theme)

    // Singleton access
    static ThemeManager& instance();

    // Theme control
    void setTheme(Theme theme);
    Theme currentTheme() const;
    bool isDarkMode() const;

    // Color accessors - get color by token name
    QColor color(const QString& tokenName) const;

    // Convenience color accessors for common tokens
    QColor brandDefault() const;
    QColor brandHover() const;
    QColor brandPressed() const;
    QColor buttonPrimary() const;
    QColor buttonPrimaryHover() const;
    QColor buttonPrimaryPressed() const;
    QColor buttonSecondary() const;
    QColor buttonSecondaryHover() const;
    QColor buttonSecondaryPressed() const;
    QColor buttonBrand() const;
    QColor buttonBrandHover() const;
    QColor buttonBrandPressed() const;
    QColor buttonDisabled() const;
    QColor textPrimary() const;
    QColor textInverse() const;
    QColor textSecondary() const;
    QColor textDisabled() const;
    QColor pageBackground() const;
    QColor surfacePrimary() const;  // Alias for surface1
    QColor surface1() const;
    QColor surface2() const;
    QColor surface3() const;
    QColor borderStrong() const;
    QColor borderSubtle() const;
    QColor iconPrimary() const;
    QColor iconSecondary() const;
    QColor supportError() const;
    QColor supportSuccess() const;
    QColor supportWarning() const;
    QColor supportInfo() const;

    // Apply theme to application
    void applyTheme();

    // Check system theme preference
    static bool systemPrefersDark();

signals:
    void themeChanged(Theme newTheme);
    void themeApplied();

private:
    // Private constructor for singleton
    explicit ThemeManager(QObject *parent = nullptr);
    ~ThemeManager() = default;

    // Prevent copying
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

    // Internal
    void initializeColorMaps();
    Theme resolveSystemTheme() const;

    Theme m_currentTheme;
    QHash<QString, std::function<QColor()>> m_lightColors;
    QHash<QString, std::function<QColor()>> m_darkColors;
};

} // namespace MegaCustom

#endif // THEMEMANAGER_H
