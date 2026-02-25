#include "utils/AnimationHelper.h"

#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QEasingCurve>

namespace MegaCustom {
namespace AnimationHelper {

void fadeIn(QWidget* widget, int durationMs)
{
    if (!widget) return;

    auto* effect = new QGraphicsOpacityEffect(widget);
    widget->setGraphicsEffect(effect);
    widget->show();

    auto* anim = new QPropertyAnimation(effect, "opacity", widget);
    anim->setDuration(durationMs);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);

    QObject::connect(anim, &QPropertyAnimation::finished, widget, [widget]() {
        // Remove the effect when done to avoid rendering overhead
        widget->setGraphicsEffect(nullptr);
    });

    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void fadeOut(QWidget* widget, int durationMs, bool hideWhenDone)
{
    if (!widget) return;

    auto* effect = new QGraphicsOpacityEffect(widget);
    widget->setGraphicsEffect(effect);

    auto* anim = new QPropertyAnimation(effect, "opacity", widget);
    anim->setDuration(durationMs);
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);

    QObject::connect(anim, &QPropertyAnimation::finished, widget, [widget, hideWhenDone]() {
        if (hideWhenDone) {
            widget->hide();
        }
        widget->setGraphicsEffect(nullptr);
    });

    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void smoothShow(QWidget* widget, int durationMs)
{
    if (!widget) return;

    widget->setMaximumHeight(0);
    widget->show();

    // Calculate the target height from size hint
    int targetHeight = widget->sizeHint().height();
    if (targetHeight <= 0) targetHeight = 100; // Sensible fallback

    auto* anim = new QPropertyAnimation(widget, "maximumHeight", widget);
    anim->setDuration(durationMs);
    anim->setStartValue(0);
    anim->setEndValue(targetHeight);
    anim->setEasingCurve(QEasingCurve::OutCubic);

    QObject::connect(anim, &QPropertyAnimation::finished, widget, [widget]() {
        // Remove height constraint so widget can resize naturally
        widget->setMaximumHeight(16777215); // QWIDGETSIZE_MAX
    });

    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void smoothHide(QWidget* widget, int durationMs)
{
    if (!widget) return;

    int currentHeight = widget->height();

    auto* anim = new QPropertyAnimation(widget, "maximumHeight", widget);
    anim->setDuration(durationMs);
    anim->setStartValue(currentHeight);
    anim->setEndValue(0);
    anim->setEasingCurve(QEasingCurve::OutCubic);

    QObject::connect(anim, &QPropertyAnimation::finished, widget, [widget]() {
        widget->hide();
        widget->setMaximumHeight(16777215); // Restore for next show
    });

    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void animateProgress(QProgressBar* bar, int targetValue, int durationMs)
{
    if (!bar) return;

    auto* anim = new QPropertyAnimation(bar, "value", bar);
    anim->setDuration(durationMs);
    anim->setStartValue(bar->value());
    anim->setEndValue(targetValue);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

} // namespace AnimationHelper
} // namespace MegaCustom
