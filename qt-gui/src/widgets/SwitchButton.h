#ifndef MEGACUSTOM_SWITCHBUTTON_H
#define MEGACUSTOM_SWITCHBUTTON_H

#include <QWidget>
#include <QColor>
#include <QPropertyAnimation>

namespace MegaCustom {

/**
 * iOS-style animated toggle switch widget
 * Provides a smooth animated transition between on/off states
 */
class SwitchButton : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int offset READ offset WRITE setOffset)
    Q_PROPERTY(bool checked READ isChecked WRITE setChecked NOTIFY toggled)

public:
    explicit SwitchButton(QWidget* parent = nullptr);
    ~SwitchButton();

    /**
     * Get the checked state
     */
    bool isChecked() const { return m_checked; }

    /**
     * Set the checked state
     */
    void setChecked(bool checked);

    /**
     * Get current animation offset
     */
    int offset() const { return m_offset; }

    /**
     * Set animation offset (used by animation system)
     */
    void setOffset(int offset);

    /**
     * Set the color when switch is ON
     */
    void setOnColor(const QColor& color);

    /**
     * Set the color when switch is OFF
     */
    void setOffColor(const QColor& color);

    /**
     * Set the thumb (circle) color
     */
    void setThumbColor(const QColor& color);

    /**
     * Set animation duration in milliseconds
     */
    void setAnimationDuration(int duration);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    /**
     * Emitted when the switch state changes
     */
    void toggled(bool checked);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    bool m_checked;
    bool m_hovered;
    int m_offset;
    QPropertyAnimation* m_animation;
    QColor m_onColor;
    QColor m_offColor;
    QColor m_thumbColor;
    int m_animationDuration;

    void updateOffset();
};

} // namespace MegaCustom

#endif // MEGACUSTOM_SWITCHBUTTON_H
