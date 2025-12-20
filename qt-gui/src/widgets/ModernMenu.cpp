// ModernMenu.cpp - Modern context menu implementation
#include "ModernMenu.h"
#include "../styles/ThemeManager.h"
#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QPainterPath>
#include <QShowEvent>
#include <QLabel>
#include <QWidgetAction>

namespace MegaCustom {

ModernMenu::ModernMenu(QWidget* parent)
    : QMenu(parent)
    , m_shadowEffect(nullptr)
    , m_borderRadius(8)
    , m_shadowRadius(16)
    , m_shadowColor(QColor(0, 0, 0, 40))
    , m_shadowEnabled(true)
{
    setupUI();
}

ModernMenu::ModernMenu(const QString& title, QWidget* parent)
    : QMenu(title, parent)
    , m_shadowEffect(nullptr)
    , m_borderRadius(8)
    , m_shadowRadius(16)
    , m_shadowColor(QColor(0, 0, 0, 40))
    , m_shadowEnabled(true)
{
    setupUI();
}

void ModernMenu::setupUI()
{
    // Enable transparency for rounded corners
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);

    // Create drop shadow effect
    m_shadowEffect = new QGraphicsDropShadowEffect(this);
    m_shadowEffect->setBlurRadius(m_shadowRadius);
    m_shadowEffect->setColor(m_shadowColor);
    m_shadowEffect->setOffset(0, 4);

    if (m_shadowEnabled) {
        setGraphicsEffect(m_shadowEffect);
    }

    // Apply initial stylesheet
    updateStyleSheet();

    // Connect to theme changes
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &ModernMenu::onThemeChanged);
}

void ModernMenu::onThemeChanged()
{
    updateStyleSheet();
}

void ModernMenu::updateStyleSheet()
{
    auto& theme = ThemeManager::instance();

    QColor bgColor = theme.surfacePrimary();
    QColor borderColor = theme.borderStrong();
    QColor textColor = theme.textPrimary();
    QColor textSecondary = theme.textSecondary();
    QColor hoverBg = theme.isDarkMode()
        ? QColor(255, 255, 255, 20)
        : QColor(0, 0, 0, 13);  // ~5% opacity
    QColor separatorColor = theme.borderSubtle();

    // Build stylesheet with theme colors
    QString styleSheet = QString(R"(
        ModernMenu {
            background-color: %1;
            border: 1px solid %2;
            border-radius: %3px;
            padding: 4px 0px;
        }
        ModernMenu::item {
            background-color: transparent;
            color: %4;
            padding: 8px 16px 8px 12px;
            margin: 0px 4px;
            border-radius: 4px;
            min-width: 120px;
        }
        ModernMenu::item:selected {
            background-color: %5;
            color: %4;
        }
        ModernMenu::item:disabled {
            color: %6;
        }
        ModernMenu::separator {
            height: 1px;
            background-color: %7;
            margin: 4px 8px;
        }
        ModernMenu::icon {
            padding-left: 8px;
        }
        ModernMenu::indicator {
            width: 16px;
            height: 16px;
            margin-left: 4px;
        }
    )")
    .arg(bgColor.name(QColor::HexArgb))
    .arg(borderColor.name(QColor::HexArgb))
    .arg(m_borderRadius)
    .arg(textColor.name())
    .arg(hoverBg.name(QColor::HexArgb))
    .arg(textSecondary.name())
    .arg(separatorColor.name(QColor::HexArgb));

    setStyleSheet(styleSheet);
}

int ModernMenu::borderRadius() const
{
    return m_borderRadius;
}

void ModernMenu::setBorderRadius(int radius)
{
    if (m_borderRadius != radius) {
        m_borderRadius = radius;
        updateStyleSheet();
        update();
    }
}

int ModernMenu::shadowRadius() const
{
    return m_shadowRadius;
}

void ModernMenu::setShadowRadius(int radius)
{
    if (m_shadowRadius != radius) {
        m_shadowRadius = radius;
        if (m_shadowEffect) {
            m_shadowEffect->setBlurRadius(radius);
        }
    }
}

QColor ModernMenu::shadowColor() const
{
    return m_shadowColor;
}

void ModernMenu::setShadowColor(const QColor& color)
{
    if (m_shadowColor != color) {
        m_shadowColor = color;
        if (m_shadowEffect) {
            m_shadowEffect->setColor(color);
        }
    }
}

bool ModernMenu::shadowEnabled() const
{
    return m_shadowEnabled;
}

void ModernMenu::setShadowEnabled(bool enabled)
{
    if (m_shadowEnabled != enabled) {
        m_shadowEnabled = enabled;
        setGraphicsEffect(enabled ? m_shadowEffect : nullptr);
    }
}

QAction* ModernMenu::addLabeledSeparator(const QString& label)
{
    if (label.isEmpty()) {
        return addSeparator();
    }

    // Create a widget action with a label
    QWidgetAction* action = new QWidgetAction(this);

    QLabel* labelWidget = new QLabel(label);
    labelWidget->setContentsMargins(12, 6, 12, 4);

    auto& theme = ThemeManager::instance();
    QString labelStyle = QString("QLabel { color: %1; font-size: 11px; font-weight: 600; }")
        .arg(theme.textSecondary().name());
    labelWidget->setStyleSheet(labelStyle);

    action->setDefaultWidget(labelWidget);
    addAction(action);

    return action;
}

QAction* ModernMenu::addThemedAction(const QString& iconPath, const QString& text,
                                      const QObject* receiver, const char* member)
{
    QAction* action = new QAction(text, this);

    if (!iconPath.isEmpty()) {
        action->setIcon(QIcon(iconPath));
    }

    if (receiver && member) {
        connect(action, SIGNAL(triggered()), receiver, member);
    }

    addAction(action);
    return action;
}

void ModernMenu::showEvent(QShowEvent* event)
{
    QMenu::showEvent(event);

    // Ensure shadow effect is properly applied
    if (m_shadowEnabled && m_shadowEffect) {
        // Add padding for shadow to be visible
        setContentsMargins(m_shadowRadius / 2, m_shadowRadius / 4,
                          m_shadowRadius / 2, m_shadowRadius / 2);
    }
}

void ModernMenu::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Get colors from theme
    auto& theme = ThemeManager::instance();
    QColor bgColor = theme.surfacePrimary();
    QColor borderColor = theme.borderStrong();

    // Calculate the drawing rect (accounting for shadow padding)
    QRect drawRect = rect();
    if (m_shadowEnabled) {
        int padding = m_shadowRadius / 2;
        drawRect.adjust(padding, padding / 2, -padding, -padding);
    }

    // Draw rounded background
    QPainterPath path;
    path.addRoundedRect(drawRect, m_borderRadius, m_borderRadius);

    painter.fillPath(path, bgColor);

    // Draw border
    painter.setPen(QPen(borderColor, 1));
    painter.drawPath(path);
}

} // namespace MegaCustom
