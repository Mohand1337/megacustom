// ButtonFactory.h - Factory for creating consistently styled buttons
#ifndef BUTTONFACTORY_H
#define BUTTONFACTORY_H

#include <QPushButton>
#include <QString>

namespace MegaCustom {

class IconButton;

/**
 * @brief Factory class for creating consistently styled buttons.
 *
 * Creates buttons that automatically follow the application's design system
 * and respond to theme changes. All buttons are styled using ThemeManager
 * colors and follow MEGAsync's button patterns.
 *
 * Button Types:
 * - Primary: Solid dark background, used for main actions
 * - Secondary: Lighter background, used for secondary actions
 * - Outline: Transparent with border, used for tertiary actions
 * - Destructive: Red background, used for delete/remove actions
 * - Text: No background, just colored text
 * - Icon: Icon-only button with hover states
 *
 * Usage:
 *   QPushButton* saveBtn = ButtonFactory::createPrimary("Save", this);
 *   QPushButton* cancelBtn = ButtonFactory::createSecondary("Cancel", this);
 *   IconButton* closeBtn = ButtonFactory::createIconButton(":/icons/close.svg", this);
 */
class ButtonFactory
{
public:
    // Button size presets
    enum Size {
        Small,      // 28px height
        Medium,     // 36px height (default)
        Large       // 44px height
    };

    /**
     * @brief Create a primary button (main action style)
     * @param text Button text
     * @param parent Parent widget
     * @param size Button size preset
     * @return Styled QPushButton
     *
     * Primary buttons have a solid dark background (ButtonPrimary token)
     * with light text. Used for the main action in a dialog or form.
     */
    static QPushButton* createPrimary(const QString& text, QWidget* parent = nullptr,
                                       Size size = Medium);

    /**
     * @brief Create a secondary button (secondary action style)
     * @param text Button text
     * @param parent Parent widget
     * @param size Button size preset
     * @return Styled QPushButton
     *
     * Secondary buttons have a lighter background with darker text.
     * Used for secondary actions that are less prominent than primary.
     */
    static QPushButton* createSecondary(const QString& text, QWidget* parent = nullptr,
                                         Size size = Medium);

    /**
     * @brief Create an outline button (tertiary action style)
     * @param text Button text
     * @param parent Parent widget
     * @param size Button size preset
     * @return Styled QPushButton
     *
     * Outline buttons have a transparent background with a visible border.
     * Used for tertiary actions or when buttons need to be less prominent.
     */
    static QPushButton* createOutline(const QString& text, QWidget* parent = nullptr,
                                       Size size = Medium);

    /**
     * @brief Create a destructive button (danger action style)
     * @param text Button text
     * @param parent Parent widget
     * @param size Button size preset
     * @return Styled QPushButton
     *
     * Destructive buttons have a red background to indicate danger.
     * Used for delete, remove, or other destructive actions.
     */
    static QPushButton* createDestructive(const QString& text, QWidget* parent = nullptr,
                                           Size size = Medium);

    /**
     * @brief Create a text-only button (minimal style)
     * @param text Button text
     * @param parent Parent widget
     * @param size Button size preset
     * @return Styled QPushButton
     *
     * Text buttons have no background, just colored text.
     * Used for links or very subtle actions.
     */
    static QPushButton* createText(const QString& text, QWidget* parent = nullptr,
                                    Size size = Medium);

    /**
     * @brief Create an icon-only button
     * @param iconPath Path to SVG icon (resource path)
     * @param parent Parent widget
     * @param size Button size preset
     * @return Styled IconButton
     *
     * Icon buttons display only an icon with hover/pressed states.
     * The icon color changes based on button state.
     */
    static IconButton* createIconButton(const QString& iconPath, QWidget* parent = nullptr,
                                         Size size = Medium);

    /**
     * @brief Create a button with both icon and text
     * @param iconPath Path to SVG icon (resource path)
     * @param text Button text
     * @param parent Parent widget
     * @param size Button size preset
     * @return Styled QPushButton with icon
     *
     * Creates a standard button with an icon on the left side.
     */
    static QPushButton* createWithIcon(const QString& iconPath, const QString& text,
                                        QWidget* parent = nullptr, Size size = Medium);

private:
    // Apply common button setup
    static void setupButton(QPushButton* button, Size size);

    // Get height for size preset
    static int heightForSize(Size size);

    // Get padding for size preset
    static QString paddingForSize(Size size);

    // Get font size for size preset
    static int fontSizeForSize(Size size);
};

} // namespace MegaCustom

#endif // BUTTONFACTORY_H
