#ifndef MEGACUSTOM_LOADINGSPINNER_H
#define MEGACUSTOM_LOADINGSPINNER_H

#include <QWidget>
#include <QTimer>
#include <QColor>

namespace MegaCustom {

/**
 * Animated loading spinner widget
 * Shows a rotating circle to indicate loading state
 */
class LoadingSpinner : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int angle READ angle WRITE setAngle)

public:
    explicit LoadingSpinner(QWidget* parent = nullptr);
    ~LoadingSpinner();

    /**
     * Start the spinner animation
     */
    void start();

    /**
     * Stop the spinner animation
     */
    void stop();

    /**
     * Check if spinner is running
     */
    bool isRunning() const;

    /**
     * Set the spinner color
     */
    void setColor(const QColor& color);

    /**
     * Set the line width
     */
    void setLineWidth(int width);

    /**
     * Get current rotation angle
     */
    int angle() const { return m_angle; }

    /**
     * Set rotation angle
     */
    void setAngle(int angle);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QTimer* m_timer;
    int m_angle;
    QColor m_color;
    int m_lineWidth;
    bool m_running;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_LOADINGSPINNER_H
