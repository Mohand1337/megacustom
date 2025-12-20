#ifndef MEGACUSTOM_STATUSINDICATOR_H
#define MEGACUSTOM_STATUSINDICATOR_H

#include <QWidget>
#include <QTimer>
#include <QColor>

namespace MegaCustom {

/**
 * Small circular status indicator widget
 * Displays a colored dot to indicate various application states
 * Supports animation for syncing state
 */
class StatusIndicator : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(Status status READ status WRITE setStatus NOTIFY statusChanged)
    Q_PROPERTY(int size READ size WRITE setSize)

public:
    /**
     * Status types with corresponding colors
     */
    enum class Status {
        Online,     // Green - Connected and ready
        Offline,    // Gray - Disconnected
        Syncing,    // Blue (animated) - Active sync in progress
        Error,      // Red - Error state
        Warning     // Orange - Warning state
    };
    Q_ENUM(Status)

    explicit StatusIndicator(QWidget* parent = nullptr);
    ~StatusIndicator() override;

    /**
     * Get current status
     */
    Status status() const { return m_status; }

    /**
     * Set the status and update visual appearance
     * Syncing status will automatically start pulsing animation
     */
    void setStatus(Status status);

    /**
     * Get indicator size (diameter in pixels)
     */
    int size() const { return m_size; }

    /**
     * Set indicator size (diameter in pixels)
     * Default is 12 pixels
     */
    void setSize(int size);

    /**
     * Enable or disable pulsing animation for Syncing state
     * Default is true
     */
    void setPulsingEnabled(bool enabled);

    /**
     * Check if pulsing animation is enabled
     */
    bool isPulsingEnabled() const { return m_pulsingEnabled; }

    /**
     * Get the color for a specific status
     */
    static QColor colorForStatus(Status status);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void statusChanged(Status status);

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onPulseTimer();

private:
    void startPulsing();
    void stopPulsing();
    void updateAnimation();

    Status m_status;
    int m_size;
    bool m_pulsingEnabled;

    QTimer* m_pulseTimer;
    int m_pulsePhase;           // Animation phase (0-100)
    bool m_pulseGrowing;        // Direction of pulse animation

    static constexpr int DEFAULT_SIZE = 12;
    static constexpr int PULSE_INTERVAL = 30;  // ~33 FPS
    static constexpr int PULSE_STEP = 5;        // Phase increment per frame
};

} // namespace MegaCustom

#endif // MEGACUSTOM_STATUSINDICATOR_H
