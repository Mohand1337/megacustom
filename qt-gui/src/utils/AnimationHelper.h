#ifndef MEGACUSTOM_ANIMATIONHELPER_H
#define MEGACUSTOM_ANIMATIONHELPER_H

#include <QWidget>
#include <QProgressBar>

namespace MegaCustom {
namespace AnimationHelper {

// Fade a widget in (opacity 0→1) over given duration
void fadeIn(QWidget* widget, int durationMs = 200);

// Fade a widget out (opacity 1→0) over given duration, optionally hide when done
void fadeOut(QWidget* widget, int durationMs = 200, bool hideWhenDone = true);

// Smoothly show a widget by animating maximumHeight from 0 to content size
void smoothShow(QWidget* widget, int durationMs = 200);

// Smoothly hide a widget by animating maximumHeight to 0, then hide
void smoothHide(QWidget* widget, int durationMs = 200);

// Smoothly animate a progress bar to a target value
void animateProgress(QProgressBar* bar, int targetValue, int durationMs = 100);

} // namespace AnimationHelper
} // namespace MegaCustom

#endif // MEGACUSTOM_ANIMATIONHELPER_H
