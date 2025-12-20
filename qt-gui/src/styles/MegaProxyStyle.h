#ifndef MEGAPROXYSTYLE_H
#define MEGAPROXYSTYLE_H

#include <QProxyStyle>
#include <QPalette>
#include <QColor>
#include <QStyleOption>
#include <QPainter>

namespace MegaCustom {

/**
 * @brief Custom proxy style that enforces MEGA brand colors for selection highlights.
 *
 * This is necessary because Qt6's Fusion style has a built-in C++ palette that
 * can override QSS stylesheets for certain widgets like QMenu and QMenuBar.
 * By using a QProxyStyle, we intercept the rendering at the C++ level.
 *
 * Now uses DesignTokens for consistent colors across the application.
 */
class MegaProxyStyle : public QProxyStyle
{
    Q_OBJECT

public:
    explicit MegaProxyStyle(QStyle* style = nullptr);
    explicit MegaProxyStyle(const QString& key);

    /**
     * @brief Returns a palette with MEGA brand colors for highlights.
     */
    QPalette standardPalette() const override;

    /**
     * @brief Custom drawing for menu items to ensure correct selection color.
     */
    void drawControl(ControlElement element, const QStyleOption* option,
                     QPainter* painter, const QWidget* widget = nullptr) const override;

    /**
     * @brief Custom drawing for primitive elements (menu bar items).
     */
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                       QPainter* painter, const QWidget* widget = nullptr) const override;

public slots:
    /**
     * @brief Updates colors when theme changes.
     */
    void onThemeChanged();

private:
    void updateColorsFromTheme();

    // Highlight alpha for menu selection (~31% opacity)
    static constexpr int HIGHLIGHT_ALPHA = 80;

    QColor m_highlightColor;
    QColor m_highlightTextColor;
};

} // namespace MegaCustom

#endif // MEGAPROXYSTYLE_H
