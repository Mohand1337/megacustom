#ifndef ABOUT_DIALOG_H
#define ABOUT_DIALOG_H

#include <QDialog>

namespace MegaCustom {

/**
 * @brief Application "About" dialog
 *
 * Displays application information including:
 * - Application name and version
 * - MEGA branding and logo
 * - Feature highlights
 * - Copyright and license information
 * - Links to documentation and support
 *
 * The dialog follows MEGA's visual design guidelines with
 * the characteristic red (#D90007) accent color.
 *
 * @see Constants::APP_VERSION for version string
 */
class AboutDialog : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief Construct the About dialog
     * @param parent Parent widget (typically MainWindow)
     *
     * Creates a modal dialog with fixed size displaying
     * application information. The dialog is styled to
     * match MEGA's design language.
     */
    explicit AboutDialog(QWidget* parent = nullptr);
};

} // namespace MegaCustom

#endif // ABOUT_DIALOG_H