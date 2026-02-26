#ifndef MEGACUSTOM_CLOUDPATHVALIDATOR_H
#define MEGACUSTOM_CLOUDPATHVALIDATOR_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

namespace mega {
    class MegaApi;
}

namespace MegaCustom {

/**
 * Result of validating a single cloud path against MEGA.
 */
struct CloudPathValidationResult {
    QString path;
    bool exists = false;
    bool isFolder = false;
    QString errorMessage;  // Empty if valid
};

/**
 * Centralized MEGA cloud path validation.
 *
 * Validates that destination paths actually exist on MEGA before copy/move
 * operations begin. Prevents silent auto-creation of wrong folder structures.
 *
 * Usage:
 *   auto results = CloudPathValidator::validatePaths(megaApi, paths);
 *   if (!CloudPathValidator::allValid(results)) {
 *       auto action = CloudPathValidator::showValidationDialog(parent, results, "Distribution");
 *       if (action == CloudPathValidator::Cancel) return;
 *   }
 */
class CloudPathValidator {
public:
    enum UserAction {
        Cancel = 0,         // User cancelled the operation
        ProceedValidOnly,   // Proceed with only valid paths (skip invalid)
        CreateAndProceed    // Create missing folders and proceed
    };

    /**
     * Validate a list of paths against MEGA cloud.
     * Checks each path exists and is a folder.
     * @param api Active MEGA API instance
     * @param paths List of cloud paths to validate
     * @return Validation result for each path
     */
    static QVector<CloudPathValidationResult> validatePaths(
        mega::MegaApi* api, const QStringList& paths);

    /**
     * Check if all results are valid (all exist and are folders).
     */
    static bool allValid(const QVector<CloudPathValidationResult>& results);

    /**
     * Count how many paths are invalid/missing.
     */
    static int invalidCount(const QVector<CloudPathValidationResult>& results);

    /**
     * Get only the valid paths from results.
     */
    static QStringList validPaths(const QVector<CloudPathValidationResult>& results);

    /**
     * Get only the invalid paths from results.
     */
    static QStringList invalidPaths(const QVector<CloudPathValidationResult>& results);

    /**
     * Show a pre-flight validation dialog when invalid paths are detected.
     *
     * Dialog shows a summary (X valid, Y missing), detailed per-path status,
     * and offers three choices:
     *   - "Proceed with valid only" — skip invalid paths
     *   - "Create missing & proceed" — create missing folders then proceed
     *   - "Cancel" — abort the operation
     *
     * @param parent Parent widget for the dialog
     * @param results Validation results from validatePaths()
     * @param operationName Name shown in the dialog (e.g., "Distribution", "Cloud Copy")
     * @return The user's chosen action
     */
    static UserAction showValidationDialog(
        QWidget* parent,
        const QVector<CloudPathValidationResult>& results,
        const QString& operationName);
};

} // namespace MegaCustom

#endif // MEGACUSTOM_CLOUDPATHVALIDATOR_H
