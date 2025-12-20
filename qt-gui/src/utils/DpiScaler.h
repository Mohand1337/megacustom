#ifndef MEGACUSTOM_DPISCALER_H
#define MEGACUSTOM_DPISCALER_H

#include <QGuiApplication>
#include <QScreen>
#include <QSize>

namespace MegaCustom {

/**
 * DPI scaling utility for HiDPI display support
 * Scales pixel values based on device pixel ratio
 */
namespace DpiScaler {

/**
 * Get the device pixel ratio
 * @return Device pixel ratio (1.0 for standard, 2.0 for retina, etc.)
 */
inline qreal ratio() {
    if (QGuiApplication::primaryScreen()) {
        return QGuiApplication::primaryScreen()->devicePixelRatio();
    }
    return 1.0;
}

/**
 * Scale a single integer value
 * @param baseSize Size in logical pixels
 * @return Size in device pixels
 */
inline int scale(int baseSize) {
    return qRound(baseSize * ratio());
}

/**
 * Scale a QSize
 * @param w Width in logical pixels
 * @param h Height in logical pixels
 * @return Scaled QSize
 */
inline QSize scale(int w, int h) {
    return QSize(scale(w), scale(h));
}

/**
 * Scale a QSize
 * @param size Size in logical pixels
 * @return Scaled QSize
 */
inline QSize scale(const QSize& size) {
    return QSize(scale(size.width()), scale(size.height()));
}

/**
 * Scale a float value
 * @param baseSize Size in logical pixels
 * @return Size in device pixels
 */
inline qreal scaleF(qreal baseSize) {
    return baseSize * ratio();
}

} // namespace DpiScaler

} // namespace MegaCustom

#endif // MEGACUSTOM_DPISCALER_H
