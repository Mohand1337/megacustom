#include "SwitchButton.h"
#include "utils/DpiScaler.h"
#include "styles/ThemeManager.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>

namespace MegaCustom {

SwitchButton::SwitchButton(QWidget* parent)
    : QWidget(parent)
    , m_checked(false)
    , m_hovered(false)
    , m_offset(0)
    , m_animation(new QPropertyAnimation(this, "offset", this))
    , m_onColor(ThemeManager::instance().supportSuccess())
    , m_offColor(ThemeManager::instance().borderSubtle())
    , m_thumbColor(Qt::white)
    , m_animationDuration(150)
{
    // Configure animation
    m_animation->setDuration(m_animationDuration);
    m_animation->setEasingCurve(QEasingCurve::InOutCubic);

    // Set size
    setFixedSize(DpiScaler::scale(44), DpiScaler::scale(24));

    // Enable mouse tracking for hover effects
    setMouseTracking(true);

    // Set cursor
    setCursor(Qt::PointingHandCursor);
}

SwitchButton::~SwitchButton()
{
    if (m_animation) {
        m_animation->stop();
    }
}

void SwitchButton::setChecked(bool checked)
{
    if (m_checked != checked) {
        m_checked = checked;
        updateOffset();
        emit toggled(m_checked);
    }
}

void SwitchButton::setOffset(int offset)
{
    m_offset = offset;
    update();
}

void SwitchButton::setOnColor(const QColor& color)
{
    m_onColor = color;
    update();
}

void SwitchButton::setOffColor(const QColor& color)
{
    m_offColor = color;
    update();
}

void SwitchButton::setThumbColor(const QColor& color)
{
    m_thumbColor = color;
    update();
}

void SwitchButton::setAnimationDuration(int duration)
{
    m_animationDuration = duration;
    m_animation->setDuration(duration);
}

QSize SwitchButton::sizeHint() const
{
    return QSize(DpiScaler::scale(44), DpiScaler::scale(24));
}

QSize SwitchButton::minimumSizeHint() const
{
    return QSize(DpiScaler::scale(36), DpiScaler::scale(20));
}

void SwitchButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Calculate dimensions
    int w = width();
    int h = height();
    qreal trackHeight = h * 0.6;
    qreal trackRadius = trackHeight / 2.0;
    qreal thumbSize = h * 0.75;
    qreal thumbRadius = thumbSize / 2.0;

    // Calculate positions
    qreal trackY = (h - trackHeight) / 2.0;
    qreal trackWidth = w;

    // Calculate thumb position based on animation offset
    qreal thumbX = m_offset + (h - thumbSize) / 2.0;
    qreal thumbY = (h - thumbSize) / 2.0;

    // Interpolate background color based on position
    qreal progress = static_cast<qreal>(m_offset) / (w - h);
    QColor backgroundColor;
    if (progress <= 0.0) {
        backgroundColor = m_offColor;
    } else if (progress >= 1.0) {
        backgroundColor = m_onColor;
    } else {
        // Interpolate between colors
        backgroundColor = QColor(
            m_offColor.red() + (m_onColor.red() - m_offColor.red()) * progress,
            m_offColor.green() + (m_onColor.green() - m_offColor.green()) * progress,
            m_offColor.blue() + (m_onColor.blue() - m_offColor.blue()) * progress
        );
    }

    // Add hover effect - slightly lighten the color
    if (m_hovered) {
        backgroundColor = backgroundColor.lighter(110);
    }

    // Draw track (rounded rectangle background)
    painter.setPen(Qt::NoPen);
    painter.setBrush(backgroundColor);
    painter.drawRoundedRect(QRectF(0, trackY, trackWidth, trackHeight), trackRadius, trackRadius);

    // Draw thumb (circle)
    painter.setBrush(m_thumbColor);

    // Add subtle shadow to thumb
    painter.setPen(QPen(QColor(0, 0, 0, 30), 1));
    painter.drawEllipse(QRectF(thumbX, thumbY, thumbSize, thumbSize));

    // Draw inner white circle (slightly smaller for clean look)
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_thumbColor);
    qreal innerSize = thumbSize - 2;
    painter.drawEllipse(QRectF(thumbX + 1, thumbY + 1, innerSize, innerSize));
}

void SwitchButton::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        setChecked(!m_checked);
    }
    QWidget::mouseReleaseEvent(event);
}

void SwitchButton::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void SwitchButton::leaveEvent(QEvent* event)
{
    m_hovered = false;
    update();
    QWidget::leaveEvent(event);
}

void SwitchButton::updateOffset()
{
    // Stop any running animation
    if (m_animation->state() == QAbstractAnimation::Running) {
        m_animation->stop();
    }

    // Calculate target position
    int targetOffset = m_checked ? (width() - height()) : 0;

    // Animate to target
    m_animation->setStartValue(m_offset);
    m_animation->setEndValue(targetOffset);
    m_animation->start();
}

} // namespace MegaCustom
