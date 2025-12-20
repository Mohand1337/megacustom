#include "CircularProgressBar.h"
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <algorithm>

namespace MegaCustom {

// Default color scheme
static const QColor DEFAULT_BACKGROUND_COLOR("#E5E5E5");
static const QColor DEFAULT_PROGRESS_COLOR("#009985");
static const QColor DEFAULT_PROGRESS_COLOR_LIGHT("#00BEA4");
static const QColor DEFAULT_TEXT_COLOR("#000000");
static const QColor DEFAULT_CIRCLE_FILL_COLOR(Qt::transparent);

static constexpr double DEFAULT_LINE_WIDTH_FACTOR = 0.0625;  // 6.25% of widget size
static constexpr int PADDING_PIXELS = 4;
static constexpr double GRADIENT_ANGLE = 90.0;

CircularProgressBar::CircularProgressBar(QWidget* parent)
    : QWidget(parent)
    , m_value(0)
    , m_backgroundColor(DEFAULT_BACKGROUND_COLOR)
    , m_progressColor(DEFAULT_PROGRESS_COLOR)
    , m_progressColorLight(DEFAULT_PROGRESS_COLOR_LIGHT)
    , m_textColor(DEFAULT_TEXT_COLOR)
    , m_circleFillColor(DEFAULT_CIRCLE_FILL_COLOR)
    , m_useGradient(true)
    , m_customText("")
    , m_lineWidthFactor(DEFAULT_LINE_WIDTH_FACTOR)
{
    // Initialize gradient
    m_gradient.setAngle(GRADIENT_ANGLE);
    updateGradient();

    // Set up pens
    m_backgroundPen.setCapStyle(Qt::FlatCap);
    m_backgroundPen.setColor(m_backgroundColor);

    m_progressPen.setCapStyle(Qt::FlatCap);

    // Set reasonable default size
    setMinimumSize(50, 50);
}

void CircularProgressBar::setValue(int value)
{
    // Clamp value to valid range
    int newValue = std::max(MIN_VALUE, std::min(MAX_VALUE, value));

    if (newValue != m_value) {
        m_value = newValue;
        emit valueChanged(m_value);
        update();
    }
}

void CircularProgressBar::setBackgroundColor(const QColor& color)
{
    if (m_backgroundColor != color) {
        m_backgroundColor = color;
        m_backgroundPen.setColor(color);
        update();
    }
}

void CircularProgressBar::setProgressColor(const QColor& color)
{
    if (m_progressColor != color) {
        m_progressColor = color;
        updateGradient();
        update();
    }
}

void CircularProgressBar::setProgressColorLight(const QColor& color)
{
    if (m_progressColorLight != color) {
        m_progressColorLight = color;
        updateGradient();
        update();
    }
}

void CircularProgressBar::setTextColor(const QColor& color)
{
    if (m_textColor != color) {
        m_textColor = color;
        update();
    }
}

void CircularProgressBar::setCircleFillColor(const QColor& color)
{
    if (m_circleFillColor != color) {
        m_circleFillColor = color;
        update();
    }
}

void CircularProgressBar::setUseGradient(bool enabled)
{
    if (m_useGradient != enabled) {
        m_useGradient = enabled;
        updateGradient();
        update();
    }
}

void CircularProgressBar::setCustomText(const QString& text)
{
    if (m_customText != text) {
        m_customText = text;
        update();
    }
}

void CircularProgressBar::setLineWidthFactor(double factor)
{
    // Clamp factor to reasonable range
    double newFactor = std::max(0.01, std::min(0.3, factor));

    if (m_lineWidthFactor != newFactor) {
        m_lineWidthFactor = newFactor;
        update();
    }
}

void CircularProgressBar::resetColors()
{
    m_backgroundColor = DEFAULT_BACKGROUND_COLOR;
    m_progressColor = DEFAULT_PROGRESS_COLOR;
    m_progressColorLight = DEFAULT_PROGRESS_COLOR_LIGHT;
    m_textColor = DEFAULT_TEXT_COLOR;
    m_circleFillColor = DEFAULT_CIRCLE_FILL_COLOR;
    m_useGradient = true;

    m_backgroundPen.setColor(m_backgroundColor);
    updateGradient();
    update();
}

QSize CircularProgressBar::sizeHint() const
{
    return QSize(100, 100);
}

QSize CircularProgressBar::minimumSizeHint() const
{
    return QSize(50, 50);
}

void CircularProgressBar::updateGradient()
{
    if (m_useGradient) {
        m_gradient.setColorAt(0.0, m_progressColor);
        m_gradient.setColorAt(1.0, m_progressColorLight);
    }
}

void CircularProgressBar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing |
                          QPainter::TextAntialiasing |
                          QPainter::SmoothPixmapTransform);

    // Calculate dimensions based on widget size
    const int widgetSize = std::min(width(), height());
    const double outerRadius = widgetSize - PADDING_PIXELS;
    const double penWidth = outerRadius * m_lineWidthFactor;

    // Calculate base rectangle for drawing arcs
    QRectF baseRect;
    baseRect.setX(penWidth / 2.0);
    baseRect.setY(penWidth / 2.0 + PADDING_PIXELS / 2.0);
    baseRect.setWidth(outerRadius - penWidth);
    baseRect.setHeight(outerRadius - penWidth);

    // Update pen widths
    m_backgroundPen.setWidth(static_cast<int>(penWidth));
    m_progressPen.setWidth(static_cast<int>(penWidth));

    // Set pen color or gradient for progress
    if (m_useGradient) {
        m_gradient.setCenter(baseRect.center());
        m_progressPen.setBrush(m_gradient);
    } else {
        m_progressPen.setColor(m_progressColor);
    }

    // Draw circle background fill
    if (m_circleFillColor != Qt::transparent) {
        painter.setBrush(m_circleFillColor);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(baseRect);
    }

    // Draw background arc (full circle)
    painter.setPen(m_backgroundPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(baseRect, START_ANGLE, -(MAX_VALUE * 360 * 16) / 100);

    // Draw progress arc
    if (m_value > 0) {
        painter.setPen(m_progressPen);
        const int spanAngle = -(m_value * 360 * 16) / 100;
        painter.drawArc(baseRect, START_ANGLE, spanAngle);
    }

    // Draw text in center
    const double innerRadius = outerRadius - penWidth / 2.0;
    const double delta = (outerRadius - innerRadius) / 2.0;
    QRectF textRect(delta, delta + PADDING_PIXELS / 2.0, innerRadius, innerRadius);

    drawText(painter, textRect);
}

void CircularProgressBar::drawText(QPainter& painter, const QRectF& rect)
{
    // Determine text to display
    QString displayText;
    if (!m_customText.isEmpty()) {
        displayText = m_customText;
    } else {
        displayText = QString("%1%").arg(m_value);
    }

    // Calculate font size based on rectangle size
    QFont font = this->font();
    font.setFamily("Lato");

    // Start with a reasonable base size
    double basePixelSize = rect.height() * 0.3;

    // Adjust font size based on text length
    int textLength = displayText.length();
    double scaleFactor = 1.0;

    if (textLength >= 4) {
        scaleFactor = 0.7;
    } else if (textLength >= 3) {
        scaleFactor = 0.85;
    }

    int pixelSize = std::max(8, static_cast<int>(basePixelSize * scaleFactor));
    font.setPixelSize(pixelSize);

    painter.setFont(font);
    painter.setPen(m_textColor);
    painter.drawText(rect, Qt::AlignCenter, displayText);
}

} // namespace MegaCustom
