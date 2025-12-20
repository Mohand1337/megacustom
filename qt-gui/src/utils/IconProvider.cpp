#include "IconProvider.h"
#include <QPainter>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QIcon>

namespace MegaCustom {

IconProvider::IconProvider()
    : m_basePath(":/icons/")
    , m_hoverColor(QColor(100, 150, 255)) // Light blue highlight
    , m_disabledColor(QColor(150, 150, 150)) // Gray
    , m_defaultSize(24)
{
}

IconProvider& IconProvider::instance()
{
    static IconProvider instance;
    return instance;
}

QIcon IconProvider::icon(const QString& name, State state)
{
    // Check cache first
    QString key = cacheKey(name, state);
    if (m_cache.contains(key)) {
        return m_cache.value(key);
    }

    // Load base SVG icon
    QPixmap basePixmap = loadSvgIcon(name);
    if (basePixmap.isNull()) {
        // Return empty icon if loading failed
        return QIcon();
    }

    QPixmap statePixmap;
    switch (state) {
        case Normal:
            statePixmap = basePixmap;
            break;
        case Hover:
            statePixmap = generateHoverIcon(basePixmap);
            break;
        case Disabled:
            statePixmap = generateDisabledIcon(basePixmap);
            break;
    }

    // Create QIcon and cache it
    QIcon resultIcon(statePixmap);
    m_cache.insert(key, resultIcon);

    return resultIcon;
}

void IconProvider::setIconBasePath(const QString& path)
{
    if (m_basePath != path) {
        m_basePath = path;
        if (!m_basePath.endsWith('/')) {
            m_basePath += '/';
        }
        // Clear cache when base path changes
        clearCache();
    }
}

QString IconProvider::iconBasePath() const
{
    return m_basePath;
}

void IconProvider::setHoverColor(const QColor& color)
{
    if (m_hoverColor != color) {
        m_hoverColor = color;
        // Clear hover state cache
        QStringList keysToRemove;
        for (auto it = m_cache.constBegin(); it != m_cache.constEnd(); ++it) {
            if (it.key().contains(":hover")) {
                keysToRemove.append(it.key());
            }
        }
        for (const QString& key : keysToRemove) {
            m_cache.remove(key);
        }
    }
}

void IconProvider::setDisabledColor(const QColor& color)
{
    if (m_disabledColor != color) {
        m_disabledColor = color;
        // Clear disabled state cache
        QStringList keysToRemove;
        for (auto it = m_cache.constBegin(); it != m_cache.constEnd(); ++it) {
            if (it.key().contains(":disabled")) {
                keysToRemove.append(it.key());
            }
        }
        for (const QString& key : keysToRemove) {
            m_cache.remove(key);
        }
    }
}

void IconProvider::clearCache()
{
    m_cache.clear();
}

bool IconProvider::iconExists(const QString& name) const
{
    QString svgPath = m_basePath + name + ".svg";
    return QFile::exists(svgPath);
}

QPixmap IconProvider::loadSvgIcon(const QString& name) const
{
    QString svgPath = m_basePath + name + ".svg";

    // Check if file exists
    if (!QFile::exists(svgPath)) {
        qWarning("IconProvider: Icon not found: %s", qPrintable(svgPath));
        return QPixmap();
    }

    // Load SVG using QIcon (Qt Widgets can handle SVG without Qt6::Svg module)
    QIcon svgIcon(svgPath);
    if (svgIcon.isNull()) {
        qWarning("IconProvider: Failed to load: %s", qPrintable(svgPath));
        return QPixmap();
    }

    // Get pixmap at default size
    return svgIcon.pixmap(m_defaultSize, m_defaultSize);
}

QPixmap IconProvider::generateHoverIcon(const QPixmap& base) const
{
    if (base.isNull()) {
        return QPixmap();
    }

    // Create a lighter, highlighted version
    QImage image = base.toImage().convertToFormat(QImage::Format_ARGB32);

    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() > 0) {
                // Make the icon slightly lighter and add blue tint
                int r = qMin(255, pixel.red() + 30);
                int g = qMin(255, pixel.green() + 30);
                int b = qMin(255, pixel.blue() + 50);
                pixel.setRed(r);
                pixel.setGreen(g);
                pixel.setBlue(b);
                image.setPixelColor(x, y, pixel);
            }
        }
    }

    return QPixmap::fromImage(image);
}

QPixmap IconProvider::generateDisabledIcon(const QPixmap& base) const
{
    if (base.isNull()) {
        return QPixmap();
    }

    // Convert to grayscale
    QImage image = base.toImage().convertToFormat(QImage::Format_ARGB32);

    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() > 0) {
                // Convert to grayscale using standard formula
                int gray = qGray(pixel.rgb());
                // Apply disabled color tint
                int r = (gray + m_disabledColor.red()) / 2;
                int g = (gray + m_disabledColor.green()) / 2;
                int b = (gray + m_disabledColor.blue()) / 2;
                // Reduce opacity for disabled state
                int alpha = pixel.alpha() * 0.5;
                pixel.setRgb(r, g, b, alpha);
                image.setPixelColor(x, y, pixel);
            }
        }
    }

    return QPixmap::fromImage(image);
}

QPixmap IconProvider::applyColorTint(const QPixmap& pixmap, const QColor& color, qreal strength) const
{
    if (pixmap.isNull()) {
        return QPixmap();
    }

    QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);

    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() > 0) {
                // Blend with tint color
                int r = pixel.red() * (1.0 - strength) + color.red() * strength;
                int g = pixel.green() * (1.0 - strength) + color.green() * strength;
                int b = pixel.blue() * (1.0 - strength) + color.blue() * strength;
                pixel.setRgb(r, g, b, pixel.alpha());
                image.setPixelColor(x, y, pixel);
            }
        }
    }

    return QPixmap::fromImage(image);
}

QString IconProvider::cacheKey(const QString& name, State state) const
{
    QString stateStr;
    switch (state) {
        case Normal:
            stateStr = "normal";
            break;
        case Hover:
            stateStr = "hover";
            break;
        case Disabled:
            stateStr = "disabled";
            break;
    }
    return QString("%1:%2").arg(name, stateStr);
}

} // namespace MegaCustom
