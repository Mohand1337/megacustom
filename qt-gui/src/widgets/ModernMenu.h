// ModernMenu.h - Modern context menu with shadows and rounded corners
#ifndef MODERNMENU_H
#define MODERNMENU_H

#include <QMenu>
#include <QColor>

class QGraphicsDropShadowEffect;

namespace MegaCustom {

/**
 * @brief A modern QMenu with drop shadows and rounded corners.
 *
 * Features:
 * - 8px rounded corners
 * - Subtle drop shadow effect
 * - Theme-aware colors via ThemeManager
 * - Smooth hover transitions
 * - Consistent styling across the application
 *
 * Usage:
 *   ModernMenu* menu = new ModernMenu(this);
 *   menu->addAction("Open", this, &MyClass::onOpen);
 *   menu->addAction("Delete", this, &MyClass::onDelete);
 *   menu->exec(QCursor::pos());
 */
class ModernMenu : public QMenu
{
    Q_OBJECT
    Q_PROPERTY(int borderRadius READ borderRadius WRITE setBorderRadius)
    Q_PROPERTY(int shadowRadius READ shadowRadius WRITE setShadowRadius)
    Q_PROPERTY(QColor shadowColor READ shadowColor WRITE setShadowColor)

public:
    explicit ModernMenu(QWidget* parent = nullptr);
    explicit ModernMenu(const QString& title, QWidget* parent = nullptr);
    ~ModernMenu() override = default;

    // Border radius (default: 8px)
    int borderRadius() const;
    void setBorderRadius(int radius);

    // Shadow properties
    int shadowRadius() const;
    void setShadowRadius(int radius);

    QColor shadowColor() const;
    void setShadowColor(const QColor& color);

    // Enable/disable shadow effect
    bool shadowEnabled() const;
    void setShadowEnabled(bool enabled);

    // Add a separator with optional label
    QAction* addLabeledSeparator(const QString& label);

    // Add action with icon from theme
    QAction* addThemedAction(const QString& iconPath, const QString& text,
                             const QObject* receiver = nullptr, const char* member = nullptr);

protected:
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void setupUI();
    void updateStyleSheet();
    void onThemeChanged();

    QGraphicsDropShadowEffect* m_shadowEffect;
    int m_borderRadius;
    int m_shadowRadius;
    QColor m_shadowColor;
    bool m_shadowEnabled;
};

} // namespace MegaCustom

#endif // MODERNMENU_H
