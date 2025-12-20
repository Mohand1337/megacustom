// ThemeManager.cpp - Centralized theme management implementation
#include "ThemeManager.h"
#include "DesignTokens.h"
#include <QApplication>
#include <QStyleHints>
#include <QDebug>

#ifdef Q_OS_WIN
#include <QSettings>
#endif

namespace MegaCustom {

ThemeManager& ThemeManager::instance()
{
    static ThemeManager instance;
    return instance;
}

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
    , m_currentTheme(Light)
{
    initializeColorMaps();

    // Connect to system theme changes if available
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, [this]() {
        if (m_currentTheme == System) {
            emit themeChanged(System);
            applyTheme();
        }
    });
#endif
}

void ThemeManager::initializeColorMaps()
{
    using namespace DesignTokens;

    // Light theme color map
    m_lightColors = {
        {"brand-default", Light::BrandDefault},
        {"brand-hover", Light::BrandHover},
        {"brand-pressed", Light::BrandPressed},
        {"button-primary", Light::ButtonPrimary},
        {"button-primary-hover", Light::ButtonPrimaryHover},
        {"button-primary-pressed", Light::ButtonPrimaryPressed},
        {"button-secondary", Light::ButtonSecondary},
        {"button-secondary-hover", Light::ButtonSecondaryHover},
        {"button-secondary-pressed", Light::ButtonSecondaryPressed},
        {"button-brand", Light::ButtonBrand},
        {"button-brand-hover", Light::ButtonBrandHover},
        {"button-brand-pressed", Light::ButtonBrandPressed},
        {"button-disabled", Light::ButtonDisabled},
        {"button-error", Light::ButtonError},
        {"button-outline", Light::ButtonOutline},
        {"text-primary", Light::TextPrimary},
        {"text-secondary", Light::TextSecondary},
        {"text-disabled", Light::TextDisabled},
        {"text-accent", Light::TextAccent},
        {"text-brand", Light::TextBrand},
        {"text-error", Light::TextError},
        {"text-success", Light::TextSuccess},
        {"text-warning", Light::TextWarning},
        {"text-info", Light::TextInfo},
        {"text-inverse", Light::TextInverse},
        {"text-placeholder", Light::TextPlaceholder},
        {"page-background", Light::PageBackground},
        {"surface-1", Light::Surface1},
        {"surface-2", Light::Surface2},
        {"surface-3", Light::Surface3},
        {"border-brand", Light::BorderBrand},
        {"border-strong", Light::BorderStrong},
        {"border-subtle", Light::BorderSubtle},
        {"border-disabled", Light::BorderDisabled},
        {"icon-primary", Light::IconPrimary},
        {"icon-secondary", Light::IconSecondary},
        {"icon-brand", Light::IconBrand},
        {"icon-disabled", Light::IconDisabled},
        {"icon-inverse", Light::IconInverse},
        {"support-error", Light::SupportError},
        {"support-success", Light::SupportSuccess},
        {"support-warning", Light::SupportWarning},
        {"support-info", Light::SupportInfo},
        {"notification-error", Light::NotificationError},
        {"notification-success", Light::NotificationSuccess},
        {"notification-warning", Light::NotificationWarning},
        {"notification-info", Light::NotificationInfo},
        {"indicator-blue", Light::IndicatorBlue},
        {"indicator-green", Light::IndicatorGreen},
        {"indicator-orange", Light::IndicatorOrange},
        {"indicator-pink", Light::IndicatorPink},
        {"indicator-yellow", Light::IndicatorYellow},
        {"link-primary", Light::LinkPrimary},
        {"link-visited", Light::LinkVisited},
        {"focus-color", Light::FocusColor},
        {"selection-control", Light::SelectionControl},
        {"neutral-default", Light::NeutralDefault},
        {"neutral-hover", Light::NeutralHover},
        {"neutral-pressed", Light::NeutralPressed},
    };

    // Dark theme color map
    m_darkColors = {
        {"brand-default", Dark::BrandDefault},
        {"brand-hover", Dark::BrandHover},
        {"brand-pressed", Dark::BrandPressed},
        {"button-primary", Dark::ButtonPrimary},
        {"button-primary-hover", Dark::ButtonPrimaryHover},
        {"button-primary-pressed", Dark::ButtonPrimaryPressed},
        {"button-secondary", Dark::ButtonSecondary},
        {"button-secondary-hover", Dark::ButtonSecondaryHover},
        {"button-secondary-pressed", Dark::ButtonSecondaryPressed},
        {"button-brand", Dark::ButtonBrand},
        {"button-brand-hover", Dark::ButtonBrandHover},
        {"button-brand-pressed", Dark::ButtonBrandPressed},
        {"button-disabled", Dark::ButtonDisabled},
        {"button-error", Dark::ButtonError},
        {"button-outline", Dark::ButtonOutline},
        {"text-primary", Dark::TextPrimary},
        {"text-secondary", Dark::TextSecondary},
        {"text-disabled", Dark::TextDisabled},
        {"text-accent", Dark::TextAccent},
        {"text-brand", Dark::TextBrand},
        {"text-error", Dark::TextError},
        {"text-success", Dark::TextSuccess},
        {"text-warning", Dark::TextWarning},
        {"text-info", Dark::TextInfo},
        {"text-inverse", Dark::TextInverse},
        {"text-placeholder", Dark::TextPlaceholder},
        {"page-background", Dark::PageBackground},
        {"surface-1", Dark::Surface1},
        {"surface-2", Dark::Surface2},
        {"surface-3", Dark::Surface3},
        {"border-brand", Dark::BorderBrand},
        {"border-strong", Dark::BorderStrong},
        {"border-subtle", Dark::BorderSubtle},
        {"border-disabled", Dark::BorderDisabled},
        {"icon-primary", Dark::IconPrimary},
        {"icon-secondary", Dark::IconSecondary},
        {"icon-brand", Dark::IconBrand},
        {"icon-disabled", Dark::IconDisabled},
        {"icon-inverse", Dark::IconInverse},
        {"support-error", Dark::SupportError},
        {"support-success", Dark::SupportSuccess},
        {"support-warning", Dark::SupportWarning},
        {"support-info", Dark::SupportInfo},
        {"notification-error", Dark::NotificationError},
        {"notification-success", Dark::NotificationSuccess},
        {"notification-warning", Dark::NotificationWarning},
        {"notification-info", Dark::NotificationInfo},
        {"indicator-blue", Dark::IndicatorBlue},
        {"indicator-green", Dark::IndicatorGreen},
        {"indicator-orange", Dark::IndicatorOrange},
        {"indicator-pink", Dark::IndicatorPink},
        {"indicator-yellow", Dark::IndicatorYellow},
        {"link-primary", Dark::LinkPrimary},
        {"link-visited", Dark::LinkVisited},
        {"focus-color", Dark::FocusColor},
        {"selection-control", Dark::SelectionControl},
        {"neutral-default", Dark::NeutralDefault},
        {"neutral-hover", Dark::NeutralHover},
        {"neutral-pressed", Dark::NeutralPressed},
    };
}

void ThemeManager::setTheme(Theme theme)
{
    if (m_currentTheme != theme) {
        m_currentTheme = theme;
        emit themeChanged(theme);
        applyTheme();
    }
}

ThemeManager::Theme ThemeManager::currentTheme() const
{
    return m_currentTheme;
}

bool ThemeManager::isDarkMode() const
{
    if (m_currentTheme == System) {
        return systemPrefersDark();
    }
    return m_currentTheme == Dark;
}

ThemeManager::Theme ThemeManager::resolveSystemTheme() const
{
    return systemPrefersDark() ? Dark : Light;
}

bool ThemeManager::systemPrefersDark()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    auto scheme = QGuiApplication::styleHints()->colorScheme();
    return scheme == Qt::ColorScheme::Dark;
#elif defined(Q_OS_WIN)
    QSettings settings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                       QSettings::NativeFormat);
    return settings.value("AppsUseLightTheme", 1).toInt() == 0;
#else
    return false;
#endif
}

QColor ThemeManager::color(const QString& tokenName) const
{
    const auto& colorMap = isDarkMode() ? m_darkColors : m_lightColors;
    auto it = colorMap.find(tokenName);
    if (it != colorMap.end()) {
        return (*it)();
    }
    qWarning() << "ThemeManager: Unknown color token:" << tokenName;
    return QColor();
}

void ThemeManager::applyTheme()
{
    // This can be expanded to reload stylesheets
    // For now, just emit signal
    emit themeApplied();
}

// Convenience accessors
QColor ThemeManager::brandDefault() const { return color("brand-default"); }
QColor ThemeManager::brandHover() const { return color("brand-hover"); }
QColor ThemeManager::brandPressed() const { return color("brand-pressed"); }
QColor ThemeManager::buttonPrimary() const { return color("button-primary"); }
QColor ThemeManager::buttonPrimaryHover() const { return color("button-primary-hover"); }
QColor ThemeManager::buttonPrimaryPressed() const { return color("button-primary-pressed"); }
QColor ThemeManager::buttonSecondary() const { return color("button-secondary"); }
QColor ThemeManager::buttonSecondaryHover() const { return color("button-secondary-hover"); }
QColor ThemeManager::buttonSecondaryPressed() const { return color("button-secondary-pressed"); }
QColor ThemeManager::buttonBrand() const { return color("button-brand"); }
QColor ThemeManager::buttonBrandHover() const { return color("button-brand-hover"); }
QColor ThemeManager::buttonBrandPressed() const { return color("button-brand-pressed"); }
QColor ThemeManager::buttonDisabled() const { return color("button-disabled"); }
QColor ThemeManager::textPrimary() const { return color("text-primary"); }
QColor ThemeManager::textInverse() const { return color("text-inverse"); }
QColor ThemeManager::textSecondary() const { return color("text-secondary"); }
QColor ThemeManager::textDisabled() const { return color("text-disabled"); }
QColor ThemeManager::pageBackground() const { return color("page-background"); }
QColor ThemeManager::surfacePrimary() const { return color("surface-1"); }  // Alias for surface1
QColor ThemeManager::surface1() const { return color("surface-1"); }
QColor ThemeManager::surface2() const { return color("surface-2"); }
QColor ThemeManager::surface3() const { return color("surface-3"); }
QColor ThemeManager::borderStrong() const { return color("border-strong"); }
QColor ThemeManager::borderSubtle() const { return color("border-subtle"); }
QColor ThemeManager::iconPrimary() const { return color("icon-primary"); }
QColor ThemeManager::iconSecondary() const { return color("icon-secondary"); }
QColor ThemeManager::supportError() const { return color("support-error"); }
QColor ThemeManager::supportSuccess() const { return color("support-success"); }
QColor ThemeManager::supportWarning() const { return color("support-warning"); }
QColor ThemeManager::supportInfo() const { return color("support-info"); }

} // namespace MegaCustom
