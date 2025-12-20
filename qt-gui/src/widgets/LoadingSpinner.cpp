#include "LoadingSpinner.h"
#include "utils/DpiScaler.h"
#include "styles/ThemeManager.h"
#include <QPainter>
#include <QPainterPath>
#include <QConicalGradient>

namespace MegaCustom {

LoadingSpinner::LoadingSpinner(QWidget* parent)
    : QWidget(parent)
    , m_timer(new QTimer(this))
    , m_angle(0)
    , m_color(ThemeManager::instance().brandDefault())
    , m_lineWidth(DpiScaler::scale(3))
    , m_running(false)
{
    // Set up animation timer (60 FPS rotation)
    connect(m_timer, &QTimer::timeout, this, [this]() {
        m_angle = (m_angle + 10) % 360;
        update();
    });

    // Make widget transparent
    setAttribute(Qt::WA_TranslucentBackground);

    // Default size
    setFixedSize(DpiScaler::scale(32), DpiScaler::scale(32));
}

LoadingSpinner::~LoadingSpinner()
{
    stop();
}

void LoadingSpinner::start()
{
    if (!m_running) {
        m_running = true;
        m_timer->start(16);  // ~60 FPS
        show();
    }
}

void LoadingSpinner::stop()
{
    if (m_running) {
        m_running = false;
        m_timer->stop();
        hide();
    }
}

bool LoadingSpinner::isRunning() const
{
    return m_running;
}

void LoadingSpinner::setColor(const QColor& color)
{
    m_color = color;
    update();
}

void LoadingSpinner::setLineWidth(int width)
{
    m_lineWidth = width;
    update();
}

void LoadingSpinner::setAngle(int angle)
{
    m_angle = angle;
    update();
}

QSize LoadingSpinner::sizeHint() const
{
    return QSize(DpiScaler::scale(32), DpiScaler::scale(32));
}

QSize LoadingSpinner::minimumSizeHint() const
{
    return QSize(DpiScaler::scale(16), DpiScaler::scale(16));
}

void LoadingSpinner::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (!m_running) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Calculate dimensions
    int size = qMin(width(), height());
    int radius = (size - m_lineWidth) / 2;
    QRectF rect(m_lineWidth / 2.0, m_lineWidth / 2.0,
                size - m_lineWidth, size - m_lineWidth);

    // Create gradient for fading effect
    QConicalGradient gradient(size / 2.0, size / 2.0, m_angle);
    gradient.setColorAt(0, m_color);
    gradient.setColorAt(0.5, m_color.lighter(150));
    gradient.setColorAt(1, QColor(m_color.red(), m_color.green(), m_color.blue(), 50));

    // Draw the arc
    QPen pen(QBrush(gradient), m_lineWidth, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(pen);

    // Draw arc (270 degrees, leaving a gap)
    painter.drawArc(rect, m_angle * 16, 270 * 16);
}

} // namespace MegaCustom
