#ifndef MEGACUSTOM_CIRCULARPROGRESSBAR_H
#define MEGACUSTOM_CIRCULARPROGRESSBAR_H

#include <QWidget>
#include <QColor>
#include <QPen>
#include <QConicalGradient>

namespace MegaCustom {

/**
 * Circular progress bar widget with gradient support
 * Displays a radial progress indicator showing percentage completion
 * Supports customizable colors, gradients, and text display
 */
class CircularProgressBar : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int value READ value WRITE setValue NOTIFY valueChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor)
    Q_PROPERTY(QColor progressColor READ progressColor WRITE setProgressColor)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor)
    Q_PROPERTY(bool useGradient READ useGradient WRITE setUseGradient)
    Q_PROPERTY(QString customText READ customText WRITE setCustomText)

public:
    explicit CircularProgressBar(QWidget* parent = nullptr);
    ~CircularProgressBar() override = default;

    /**
     * Get current progress value (0-100)
     */
    int value() const { return m_value; }

    /**
     * Set progress value (0-100)
     */
    void setValue(int value);

    /**
     * Get background arc color
     */
    QColor backgroundColor() const { return m_backgroundColor; }

    /**
     * Set background arc color
     */
    void setBackgroundColor(const QColor& color);

    /**
     * Get progress arc color (used for single color or gradient dark color)
     */
    QColor progressColor() const { return m_progressColor; }

    /**
     * Set progress arc color
     */
    void setProgressColor(const QColor& color);

    /**
     * Get progress arc light color (used when gradient is enabled)
     */
    QColor progressColorLight() const { return m_progressColorLight; }

    /**
     * Set progress arc light color for gradient
     */
    void setProgressColorLight(const QColor& color);

    /**
     * Get text color
     */
    QColor textColor() const { return m_textColor; }

    /**
     * Set text color for percentage display
     */
    void setTextColor(const QColor& color);

    /**
     * Get circle background fill color
     */
    QColor circleFillColor() const { return m_circleFillColor; }

    /**
     * Set circle background fill color
     */
    void setCircleFillColor(const QColor& color);

    /**
     * Check if gradient is enabled
     */
    bool useGradient() const { return m_useGradient; }

    /**
     * Enable/disable gradient for progress arc
     */
    void setUseGradient(bool enabled);

    /**
     * Get custom text (empty if showing percentage)
     */
    QString customText() const { return m_customText; }

    /**
     * Set custom text to display instead of percentage
     * Set to empty string to show percentage
     */
    void setCustomText(const QString& text);

    /**
     * Get line width as percentage of widget size (0.0-1.0)
     */
    double lineWidthFactor() const { return m_lineWidthFactor; }

    /**
     * Set line width as percentage of widget size
     * Default is 0.0625 (6.25% of widget size)
     */
    void setLineWidthFactor(double factor);

    /**
     * Reset to default colors
     */
    void resetColors();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void valueChanged(int value);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void updateGradient();
    void drawText(QPainter& painter, const QRectF& rect);

    int m_value;
    QColor m_backgroundColor;
    QColor m_progressColor;
    QColor m_progressColorLight;
    QColor m_textColor;
    QColor m_circleFillColor;
    bool m_useGradient;
    QString m_customText;
    double m_lineWidthFactor;

    QConicalGradient m_gradient;
    QPen m_backgroundPen;
    QPen m_progressPen;

    static constexpr int MIN_VALUE = 0;
    static constexpr int MAX_VALUE = 100;
    static constexpr int START_ANGLE = 90 * 16;  // 12 o'clock position
};

} // namespace MegaCustom

#endif // MEGACUSTOM_CIRCULARPROGRESSBAR_H
