// SvgIcon.cpp - Dynamic SVG icon widget implementation
#include "SvgIcon.h"
#include <QPainter>
#include <QDebug>

namespace MegaCustom {

SvgIcon::SvgIcon(QWidget* parent)
    : QWidget(parent)
    , m_color(Qt::black)
    , m_iconSize(24, 24)
    , m_pixmapDirty(true)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

SvgIcon::SvgIcon(const QString& iconPath, QWidget* parent)
    : SvgIcon(parent)
{
    setIcon(iconPath);
}

QString SvgIcon::iconPath() const
{
    return m_iconPath;
}

void SvgIcon::setIcon(const QString& path)
{
    if (m_iconPath != path) {
        m_iconPath = path;

        if (!path.isEmpty()) {
            if (!m_renderer.load(path)) {
                qWarning() << "SvgIcon: Failed to load SVG:" << path;
            }
        }

        m_pixmapDirty = true;
        emit iconChanged(path);
        update();
    }
}

QColor SvgIcon::color() const
{
    return m_color;
}

void SvgIcon::setColor(const QColor& color)
{
    if (m_color != color) {
        m_color = color;
        m_pixmapDirty = true;
        emit colorChanged(color);
        update();
    }
}

QSize SvgIcon::iconSize() const
{
    return m_iconSize;
}

void SvgIcon::setSize(const QSize& size)
{
    if (m_iconSize != size) {
        m_iconSize = size;
        m_pixmapDirty = true;
        setFixedSize(size);
        emit sizeChanged(size);
        update();
    }
}

void SvgIcon::setSize(int width, int height)
{
    setSize(QSize(width, height));
}

QSize SvgIcon::sizeHint() const
{
    return m_iconSize;
}

QSize SvgIcon::minimumSizeHint() const
{
    return m_iconSize;
}

QPixmap SvgIcon::pixmap() const
{
    // Force update if dirty
    if (m_pixmapDirty) {
        const_cast<SvgIcon*>(this)->updatePixmap();
    }
    return m_cachedPixmap;
}

void SvgIcon::updatePixmap()
{
    if (!m_renderer.isValid()) {
        m_cachedPixmap = QPixmap();
        m_pixmapDirty = false;
        return;
    }

    // Create pixmap at the desired size
    QPixmap svgPixmap(m_iconSize);
    svgPixmap.fill(Qt::transparent);

    // Render SVG to pixmap
    QPainter svgPainter(&svgPixmap);
    svgPainter.setRenderHint(QPainter::Antialiasing);
    svgPainter.setRenderHint(QPainter::SmoothPixmapTransform);
    m_renderer.render(&svgPainter);
    svgPainter.end();

    // Create a colored version using composition
    // This works by:
    // 1. Creating a solid color pixmap
    // 2. Using the SVG as a mask via CompositionMode_DestinationIn
    QPixmap coloredPixmap(m_iconSize);
    coloredPixmap.fill(Qt::transparent);

    QPainter painter(&coloredPixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // Draw solid color
    painter.fillRect(coloredPixmap.rect(), m_color);

    // Use SVG alpha channel as mask
    painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    painter.drawPixmap(0, 0, svgPixmap);
    painter.end();

    m_cachedPixmap = coloredPixmap;
    m_pixmapDirty = false;
}

void SvgIcon::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (m_pixmapDirty) {
        updatePixmap();
    }

    if (!m_cachedPixmap.isNull()) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        // Center the icon in the widget
        int x = (width() - m_cachedPixmap.width()) / 2;
        int y = (height() - m_cachedPixmap.height()) / 2;
        painter.drawPixmap(x, y, m_cachedPixmap);
    }
}

} // namespace MegaCustom
