#ifndef MEGACUSTOM_BANNERWIDGET_H
#define MEGACUSTOM_BANNERWIDGET_H

#include <QWidget>
#include <QTimer>

class QLabel;
class QPushButton;
class QHBoxLayout;

namespace MegaCustom {

/**
 * @brief Notification banner widget with support for different message types
 *
 * BannerWidget displays notification messages with contextual styling based on
 * the message type (Info, Warning, Error, Success). It supports:
 * - Type-specific icons and colors
 * - Optional action button
 * - Auto-dismiss with configurable timer
 * - Title and description text
 *
 * Example usage:
 * @code
 * auto banner = new BannerWidget(this);
 * banner->setType(BannerWidget::Type::Success);
 * banner->setTitle("Upload Complete");
 * banner->setMessage("Your files have been uploaded successfully.");
 * banner->setActionButton("View Files");
 * banner->setAutoDismiss(5000); // Auto-hide after 5 seconds
 * banner->show();
 * @endcode
 */
class BannerWidget : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief Banner notification types with associated styling
     */
    enum class Type
    {
        Info,       ///< Informational message (blue theme)
        Warning,    ///< Warning message (yellow/orange theme)
        Error,      ///< Error message (red theme)
        Success     ///< Success message (green theme)
    };

    explicit BannerWidget(QWidget* parent = nullptr);
    ~BannerWidget() override = default;

    /**
     * @brief Set the banner type (affects icon and colors)
     * @param type The banner type
     * @param showIcon Whether to show the type-specific icon (default: true)
     */
    void setType(Type type, bool showIcon = true);

    /**
     * @brief Set the main title text (optional, shown in bold)
     * @param text Title text (empty to hide title)
     */
    void setTitle(const QString& text);

    /**
     * @brief Set the description/message text
     * @param text Message text
     */
    void setMessage(const QString& text);

    /**
     * @brief Set an optional action button
     * @param text Button text (empty to hide button)
     */
    void setActionButton(const QString& text);

    /**
     * @brief Enable auto-dismiss with a timer
     * @param milliseconds Time in milliseconds before auto-hiding (0 to disable)
     */
    void setAutoDismiss(int milliseconds);

    /**
     * @brief Get the current banner type
     * @return Current type
     */
    Type type() const { return m_type; }

signals:
    /**
     * @brief Emitted when the action button is clicked
     */
    void actionButtonClicked();

    /**
     * @brief Emitted when the banner is dismissed (manually or auto)
     */
    void dismissed();

private slots:
    void onAutoDismissTimeout();

private:
    void setupUI();
    void updateStyle();
    QString getIconPath() const;
    QString getBackgroundColor() const;
    QString getIconColor() const;

    // UI Components
    QWidget* m_contentWidget;
    QLabel* m_iconLabel;
    QLabel* m_titleLabel;
    QLabel* m_messageLabel;
    QPushButton* m_actionButton;
    QTimer* m_autoDismissTimer;

    // State
    Type m_type;
    bool m_showIcon;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_BANNERWIDGET_H
