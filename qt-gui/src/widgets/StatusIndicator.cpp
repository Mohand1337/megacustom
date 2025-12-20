#include "StatusIndicator.h"
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

namespace MegaCustom {

// Status color definitions
static const QColor COLOR_ONLINE("#4CAF50");   // Green
static const QColor COLOR_OFFLINE("#9E9E9E");  // Gray
static const QColor COLOR_SYNCING("#2196F3");  // Blue
static const QColor COLOR_ERROR("#F44336");    // Red
static const QColor COLOR_WARNING("#FF9800");  // Orange

StatusIndicator::StatusIndicator(QWidget* parent)
    : QWidget(parent)
    , m_status(Status::Offline)
    , m_size(DEFAULT_SIZE)
    , m_pulsingEnabled(true)
    , m_pulseTimer(new QTimer(this))
    , m_pulsePhase(0)
    , m_pulseGrowing(true)
{
    // Set up pulse animation timer
    connect(m_pulseTimer, &QTimer::timeout, this, &StatusIndicator::onPulseTimer);

    // Set fixed size based on default size
    setFixedSize(m_size, m_size);

    // Enable antialiasing for smooth circles
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

StatusIndicator::~StatusIndicator()
{
    stopPulsing();
}

void StatusIndicator::setStatus(Status status)
{
    if (m_status == status) {
        return;
    }

    m_status = status;

    // Start or stop pulsing based on status
    if (m_status == Status::Syncing && m_pulsingEnabled) {
        startPulsing();
    } else {
        stopPulsing();
    }

    update();
    emit statusChanged(m_status);
}

void StatusIndicator::setSize(int size)
{
    if (m_size == size || size < 4) {
        return;
    }

    m_size = size;
    setFixedSize(m_size, m_size);
    update();
}

void StatusIndicator::setPulsingEnabled(bool enabled)
{
    if (m_pulsingEnabled == enabled) {
        return;
    }

    m_pulsingEnabled = enabled;

    // Update pulsing state if we're in Syncing status
    if (m_status == Status::Syncing) {
        if (m_pulsingEnabled) {
            startPulsing();
        } else {
            stopPulsing();
        }
    }
}

QColor StatusIndicator::colorForStatus(Status status)
{
    switch (status) {
        case Status::Online:
            return COLOR_ONLINE;
        case Status::Offline:
            return COLOR_OFFLINE;
        case Status::Syncing:
            return COLOR_SYNCING;
        case Status::Error:
            return COLOR_ERROR;
        case Status::Warning:
            return COLOR_WARNING;
        default:
            return COLOR_OFFLINE;
    }
}

QSize StatusIndicator::sizeHint() const
{
    return QSize(m_size, m_size);
}

QSize StatusIndicator::minimumSizeHint() const
{
    return QSize(8, 8);
}

void StatusIndicator::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Calculate center and radius
    qreal centerX = width() / 2.0;
    qreal centerY = height() / 2.0;
    qreal baseRadius = qMin(width(), height()) / 2.0 - 1.0;

    // Apply pulsing effect for Syncing status
    qreal radius = baseRadius;
    qreal opacity = 1.0;

    if (m_status == Status::Syncing && m_pulsingEnabled && m_pulseTimer->isActive()) {
        // Calculate pulse scale factor (0.8 to 1.0)
        qreal pulseFactor = 0.8 + (m_pulsePhase / 100.0) * 0.2;
        radius = baseRadius * pulseFactor;

        // Calculate opacity (0.6 to 1.0)
        opacity = 0.6 + (m_pulsePhase / 100.0) * 0.4;
    }

    // Get color for current status
    QColor statusColor = colorForStatus(m_status);

    // Create gradient for depth effect
    QRadialGradient gradient(centerX, centerY, radius);
    QColor lightColor = statusColor.lighter(120);
    QColor darkColor = statusColor.darker(110);

    gradient.setColorAt(0.0, lightColor);
    gradient.setColorAt(0.7, statusColor);
    gradient.setColorAt(1.0, darkColor);

    // Set opacity
    painter.setOpacity(opacity);

    // Draw the indicator circle
    painter.setPen(Qt::NoPen);
    painter.setBrush(QBrush(gradient));
    painter.drawEllipse(QPointF(centerX, centerY), radius, radius);

    // Draw subtle border for better visibility
    painter.setOpacity(opacity * 0.5);
    QPen borderPen(darkColor.darker(120), 0.5);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(centerX, centerY), radius, radius);
}

void StatusIndicator::onPulseTimer()
{
    updateAnimation();
    update();
}

void StatusIndicator::startPulsing()
{
    if (!m_pulseTimer->isActive()) {
        m_pulsePhase = 0;
        m_pulseGrowing = true;
        m_pulseTimer->start(PULSE_INTERVAL);
    }
}

void StatusIndicator::stopPulsing()
{
    if (m_pulseTimer->isActive()) {
        m_pulseTimer->stop();
        m_pulsePhase = 0;
        m_pulseGrowing = true;
        update();
    }
}

void StatusIndicator::updateAnimation()
{
    // Update pulse phase
    if (m_pulseGrowing) {
        m_pulsePhase += PULSE_STEP;
        if (m_pulsePhase >= 100) {
            m_pulsePhase = 100;
            m_pulseGrowing = false;
        }
    } else {
        m_pulsePhase -= PULSE_STEP;
        if (m_pulsePhase <= 0) {
            m_pulsePhase = 0;
            m_pulseGrowing = true;
        }
    }
}

} // namespace MegaCustom
