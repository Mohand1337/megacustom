// SvgIcon.h - Dynamic SVG icon widget with color support
#ifndef SVGICON_H
#define SVGICON_H

#include <QWidget>
#include <QColor>
#include <QSize>
#include <QSvgRenderer>
#include <QPixmap>

namespace MegaCustom {

/**
 * @brief A widget that displays SVG icons with dynamic color support.
 *
 * Similar to MEGAsync's SvgImage component, this widget allows SVG icons
 * to be recolored at runtime without needing multiple icon files.
 *
 * Usage:
 *   SvgIcon* icon = new SvgIcon(this);
 *   icon->setIcon(":/icons/upload.svg");
 *   icon->setColor(ThemeManager::instance().iconPrimary());
 *   icon->setSize(QSize(24, 24));
 */
class SvgIcon : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QString iconPath READ iconPath WRITE setIcon NOTIFY iconChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(QSize iconSize READ iconSize WRITE setSize NOTIFY sizeChanged)

public:
    explicit SvgIcon(QWidget* parent = nullptr);
    explicit SvgIcon(const QString& iconPath, QWidget* parent = nullptr);
    ~SvgIcon() override = default;

    // Icon path
    QString iconPath() const;
    void setIcon(const QString& path);

    // Color
    QColor color() const;
    void setColor(const QColor& color);

    // Size
    QSize iconSize() const;
    void setSize(const QSize& size);
    void setSize(int width, int height);

    // Override size hint
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // Get the rendered pixmap (useful for other widgets)
    QPixmap pixmap() const;

signals:
    void iconChanged(const QString& path);
    void colorChanged(const QColor& color);
    void sizeChanged(const QSize& size);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void updatePixmap();

    QString m_iconPath;
    QColor m_color;
    QSize m_iconSize;
    QSvgRenderer m_renderer;
    QPixmap m_cachedPixmap;
    bool m_pixmapDirty;
};

} // namespace MegaCustom

#endif // SVGICON_H
