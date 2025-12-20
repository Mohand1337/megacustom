#include "ElidedLabel.h"
#include <QPainter>
#include <QStyleOption>
#include <QFontMetrics>

namespace MegaCustom {

ElidedLabel::ElidedLabel(QWidget* parent)
    : QLabel(parent)
    , m_elideMode(Qt::ElideMiddle)
    , m_isElided(false)
{
    // Allow the label to shrink smaller than the text
    setMinimumWidth(0);
    setTextFormat(Qt::PlainText);
    setWordWrap(false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

ElidedLabel::ElidedLabel(const QString& text, QWidget* parent)
    : ElidedLabel(parent)
{
    setText(text);
}

void ElidedLabel::setText(const QString& text)
{
    m_fullText = text;
    QLabel::setText(text);

    // Clear tooltip when setting new text
    setToolTip(QString());
    m_isElided = false;

    update();
}

void ElidedLabel::setElideMode(Qt::TextElideMode mode)
{
    if (m_elideMode != mode) {
        m_elideMode = mode;
        update();
    }
}

QSize ElidedLabel::minimumSizeHint() const
{
    // Allow the widget to be very small
    QSize size = QLabel::minimumSizeHint();
    size.setWidth(0);
    return size;
}

QSize ElidedLabel::sizeHint() const
{
    // Prefer to show the full text if possible
    return QLabel::sizeHint();
}

void ElidedLabel::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);

    // Paint background if styled
    if (testAttribute(Qt::WA_StyledBackground)) {
        QStyleOption opt;
        opt.initFrom(this);
        style()->drawPrimitive(QStyle::PE_Widget, &opt, &painter, this);
    }

    // Get available space for text
    QRect contentRect = contentsRect();
    QFontMetrics fm(font());

    // Elide the text to fit
    QString elidedText = fm.elidedText(m_fullText, m_elideMode, contentRect.width());

    // Update tooltip and elision state
    bool wasElided = m_isElided;
    m_isElided = (elidedText != m_fullText);

    if (m_isElided && toolTip().isEmpty()) {
        // Text is elided and no tooltip set - show full text as tooltip
        setToolTip(m_fullText);
    } else if (!m_isElided && !toolTip().isEmpty()) {
        // Text is not elided but tooltip is set - clear it
        setToolTip(QString());
    }

    // Emit signal if elision state changed
    if (wasElided != m_isElided) {
        emit elisionChanged(m_isElided);
    }

    // Draw the elided text
    style()->drawItemText(&painter,
                          contentRect,
                          static_cast<int>(alignment()),
                          palette(),
                          isEnabled(),
                          elidedText,
                          foregroundRole());
}

void ElidedLabel::resizeEvent(QResizeEvent* event)
{
    QLabel::resizeEvent(event);
    // Text elision may have changed due to resize
    update();
}

} // namespace MegaCustom
