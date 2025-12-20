// IconButton.cpp - Icon-only button implementation
#include "IconButton.h"
#include "SvgIcon.h"
#include "../styles/ThemeManager.h"
#include "../utils/DpiScaler.h"
#include <QHBoxLayout>
#include <QEnterEvent>

namespace MegaCustom {

IconButton::IconButton(QWidget* parent)
    : QPushButton(parent)
    , m_icon(new SvgIcon(this))
    , m_iconSize(DpiScaler::scale(20))
    , m_hovered(false)
    , m_pressed(false)
{
    setupUI();

    // Default colors from theme
    auto& theme = ThemeManager::instance();
    m_iconColor = theme.iconPrimary();
    m_iconColorHover = theme.iconSecondary();
    m_iconColorPressed = theme.iconPrimary();
    m_iconColorDisabled = theme.color("icon-disabled");

    updateIconColor();

    // Connect to theme changes
    connect(&theme, &ThemeManager::themeChanged, this, [this]() {
        auto& tm = ThemeManager::instance();
        m_iconColor = tm.iconPrimary();
        m_iconColorHover = tm.iconSecondary();
        m_iconColorPressed = tm.iconPrimary();
        m_iconColorDisabled = tm.color("icon-disabled");
        updateIconColor();
    });
}

IconButton::IconButton(const QString& iconPath, QWidget* parent)
    : IconButton(parent)
{
    setIconPath(iconPath);
}

void IconButton::setupUI()
{
    // Style the button
    setObjectName("IconButton");
    setCursor(Qt::PointingHandCursor);
    setFlat(true);

    // Default size: 36x36 with 20x20 icon (DPI-scaled)
    setFixedSize(DpiScaler::scale(36), DpiScaler::scale(36));
    m_icon->setSize(m_iconSize, m_iconSize);

    // Center the icon
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_icon);

    // Transparent background style
    setStyleSheet(QString(
        "IconButton {"
        "  background-color: transparent;"
        "  border: none;"
        "  border-radius: %1px;"
        "  padding: 0px;"
        "}"
        "IconButton:hover {"
        "  background-color: rgba(0, 0, 0, 0.05);"
        "}"
        "IconButton:pressed {"
        "  background-color: rgba(0, 0, 0, 0.1);"
        "}"
        "IconButton:disabled {"
        "  background-color: transparent;"
        "}"
    ).arg(DpiScaler::scale(6)));
}

QString IconButton::iconPath() const
{
    return m_iconPath;
}

void IconButton::setIconPath(const QString& path)
{
    if (m_iconPath != path) {
        m_iconPath = path;
        m_icon->setIcon(path);
        emit iconPathChanged(path);
    }
}

QColor IconButton::iconColor() const
{
    return m_iconColor;
}

void IconButton::setIconColor(const QColor& color)
{
    if (m_iconColor != color) {
        m_iconColor = color;
        emit iconColorChanged(color);
        updateIconColor();
    }
}

QColor IconButton::iconColorHover() const
{
    return m_iconColorHover;
}

void IconButton::setIconColorHover(const QColor& color)
{
    m_iconColorHover = color;
    updateIconColor();
}

QColor IconButton::iconColorPressed() const
{
    return m_iconColorPressed;
}

void IconButton::setIconColorPressed(const QColor& color)
{
    m_iconColorPressed = color;
    updateIconColor();
}

QColor IconButton::iconColorDisabled() const
{
    return m_iconColorDisabled;
}

void IconButton::setIconColorDisabled(const QColor& color)
{
    m_iconColorDisabled = color;
    updateIconColor();
}

void IconButton::setIconColors(const QColor& normal, const QColor& hover,
                                const QColor& pressed, const QColor& disabled)
{
    m_iconColor = normal;
    m_iconColorHover = hover;
    m_iconColorPressed = pressed;
    m_iconColorDisabled = disabled;
    updateIconColor();
}

int IconButton::iconSize() const
{
    return m_iconSize;
}

void IconButton::setIconSize(int size)
{
    if (m_iconSize != size) {
        m_iconSize = size;
        m_icon->setSize(size, size);
        emit iconSizeChanged(size);
    }
}

QSize IconButton::sizeHint() const
{
    return QSize(DpiScaler::scale(36), DpiScaler::scale(36));
}

void IconButton::enterEvent(QEnterEvent* event)
{
    QPushButton::enterEvent(event);
    m_hovered = true;
    updateIconColor();
}

void IconButton::leaveEvent(QEvent* event)
{
    QPushButton::leaveEvent(event);
    m_hovered = false;
    updateIconColor();
}

void IconButton::mousePressEvent(QMouseEvent* event)
{
    QPushButton::mousePressEvent(event);
    m_pressed = true;
    updateIconColor();
}

void IconButton::mouseReleaseEvent(QMouseEvent* event)
{
    QPushButton::mouseReleaseEvent(event);
    m_pressed = false;
    updateIconColor();
}

void IconButton::changeEvent(QEvent* event)
{
    QPushButton::changeEvent(event);
    if (event->type() == QEvent::EnabledChange) {
        updateIconColor();
    }
}

void IconButton::updateIconColor()
{
    if (!isEnabled()) {
        m_icon->setColor(m_iconColorDisabled);
    } else if (m_pressed) {
        m_icon->setColor(m_iconColorPressed);
    } else if (m_hovered) {
        m_icon->setColor(m_iconColorHover);
    } else {
        m_icon->setColor(m_iconColor);
    }
}

} // namespace MegaCustom
