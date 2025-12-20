#ifndef MEGACUSTOM_ELIDEDLABEL_H
#define MEGACUSTOM_ELIDEDLABEL_H

#include <QLabel>
#include <QString>
#include <QSize>

namespace MegaCustom {

/**
 * Label widget with automatic text elision (truncation with ellipsis)
 *
 * Automatically truncates text that doesn't fit and shows the full text
 * as a tooltip when hovering over truncated text.
 *
 * Supports three elide modes:
 * - Qt::ElideLeft: "...end of text"
 * - Qt::ElideMiddle: "start...end" (default)
 * - Qt::ElideRight: "start of text..."
 */
class ElidedLabel : public QLabel
{
    Q_OBJECT
    Q_PROPERTY(Qt::TextElideMode elideMode READ elideMode WRITE setElideMode)

public:
    explicit ElidedLabel(QWidget* parent = nullptr);
    explicit ElidedLabel(const QString& text, QWidget* parent = nullptr);
    ~ElidedLabel() override = default;

    /**
     * Set the text to display
     * Text will be automatically elided if it doesn't fit
     */
    void setText(const QString& text);

    /**
     * Get the elide mode
     */
    Qt::TextElideMode elideMode() const { return m_elideMode; }

    /**
     * Set the elide mode (Left, Middle, or Right)
     */
    void setElideMode(Qt::TextElideMode mode);

    /**
     * Check if the text is currently elided
     */
    bool isElided() const { return m_isElided; }

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

signals:
    /**
     * Emitted when the elision state changes
     */
    void elisionChanged(bool elided);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateElision();

    Qt::TextElideMode m_elideMode;
    bool m_isElided;
    QString m_fullText;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_ELIDEDLABEL_H
