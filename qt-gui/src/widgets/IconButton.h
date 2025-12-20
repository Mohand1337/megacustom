// IconButton.h - Icon-only button with dynamic color support
#ifndef ICONBUTTON_H
#define ICONBUTTON_H

#include <QPushButton>
#include <QColor>

namespace MegaCustom {

class SvgIcon;

/**
 * @brief A button widget optimized for icon-only display with hover/pressed states.
 *
 * Features:
 * - Dynamic SVG icon coloring based on state (normal, hover, pressed, disabled)
 * - Consistent 36x36px default sizing
 * - Transparent background with subtle hover effect
 * - Theme-aware color support via ThemeManager
 */
class IconButton : public QPushButton
{
    Q_OBJECT
    Q_PROPERTY(QString iconPath READ iconPath WRITE setIconPath NOTIFY iconPathChanged)
    Q_PROPERTY(QColor iconColor READ iconColor WRITE setIconColor NOTIFY iconColorChanged)
    Q_PROPERTY(QColor iconColorHover READ iconColorHover WRITE setIconColorHover)
    Q_PROPERTY(QColor iconColorPressed READ iconColorPressed WRITE setIconColorPressed)
    Q_PROPERTY(QColor iconColorDisabled READ iconColorDisabled WRITE setIconColorDisabled)
    Q_PROPERTY(int iconSize READ iconSize WRITE setIconSize NOTIFY iconSizeChanged)

public:
    explicit IconButton(QWidget* parent = nullptr);
    explicit IconButton(const QString& iconPath, QWidget* parent = nullptr);
    ~IconButton() override = default;

    // Icon path
    QString iconPath() const;
    void setIconPath(const QString& path);

    // Icon colors for different states
    QColor iconColor() const;
    void setIconColor(const QColor& color);

    QColor iconColorHover() const;
    void setIconColorHover(const QColor& color);

    QColor iconColorPressed() const;
    void setIconColorPressed(const QColor& color);

    QColor iconColorDisabled() const;
    void setIconColorDisabled(const QColor& color);

    // Set all colors at once
    void setIconColors(const QColor& normal, const QColor& hover,
                       const QColor& pressed, const QColor& disabled);

    // Icon size (default 20x20)
    int iconSize() const;
    void setIconSize(int size);

    // Size hint
    QSize sizeHint() const override;

signals:
    void iconPathChanged(const QString& path);
    void iconColorChanged(const QColor& color);
    void iconSizeChanged(int size);

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void setupUI();
    void updateIconColor();

    SvgIcon* m_icon;
    QString m_iconPath;
    QColor m_iconColor;
    QColor m_iconColorHover;
    QColor m_iconColorPressed;
    QColor m_iconColorDisabled;
    int m_iconSize;
    bool m_hovered;
    bool m_pressed;
};

} // namespace MegaCustom

#endif // ICONBUTTON_H
