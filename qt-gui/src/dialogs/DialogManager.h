// DialogManager.h - Centralized dialog lifecycle management
#ifndef DIALOGMANAGER_H
#define DIALOGMANAGER_H

#include <QObject>
#include <QDialog>
#include <QHash>
#include <QPointer>
#include <functional>
#include <memory>

namespace MegaCustom {

/**
 * @brief Singleton class for centralized dialog lifecycle management.
 *
 * Inspired by MEGAsync's DialogOpener pattern, this class provides:
 * - Single instance tracking per dialog type
 * - Automatic cleanup on close
 * - Signal emission for dialog events
 * - Optional modality control
 *
 * Usage:
 *   // Open a dialog (creates new if none exists, or brings existing to front)
 *   auto* loginDialog = DialogManager::instance().openDialog<LoginDialog>(parentWidget);
 *
 *   // Connect to signals
 *   connect(loginDialog, &LoginDialog::accepted, this, &MyClass::onLoginAccepted);
 *
 *   // Show the dialog
 *   loginDialog->show();
 *
 *   // Or execute modal
 *   if (loginDialog->exec() == QDialog::Accepted) { ... }
 *
 *   // Check if dialog is already open
 *   if (DialogManager::instance().isDialogOpen<LoginDialog>()) { ... }
 *
 *   // Get existing dialog (returns nullptr if not open)
 *   auto* existing = DialogManager::instance().getDialog<LoginDialog>();
 */
class DialogManager : public QObject
{
    Q_OBJECT

public:
    // Singleton access
    static DialogManager& instance();

    /**
     * @brief Open a dialog of the specified type.
     *
     * If a dialog of this type is already open, it will be raised and focused.
     * Otherwise, a new dialog is created.
     *
     * @tparam T Dialog class (must inherit from QDialog)
     * @param parent Parent widget for the dialog
     * @param args Constructor arguments for the dialog (forwarded)
     * @return Pointer to the dialog (new or existing)
     */
    template<typename T, typename... Args>
    T* openDialog(QWidget* parent, Args&&... args);

    /**
     * @brief Get an existing dialog of the specified type.
     *
     * @tparam T Dialog class
     * @return Pointer to the dialog, or nullptr if not open
     */
    template<typename T>
    T* getDialog() const;

    /**
     * @brief Check if a dialog of the specified type is currently open.
     *
     * @tparam T Dialog class
     * @return true if dialog is open
     */
    template<typename T>
    bool isDialogOpen() const;

    /**
     * @brief Close a specific dialog type.
     *
     * @tparam T Dialog class
     * @return true if dialog was closed
     */
    template<typename T>
    bool closeDialog();

    /**
     * @brief Close all open dialogs.
     *
     * Useful when logging out or shutting down.
     */
    void closeAllDialogs();

    /**
     * @brief Get count of currently open dialogs.
     */
    int openDialogCount() const;

    /**
     * @brief Get list of open dialog type names (for debugging).
     */
    QStringList openDialogTypeNames() const;

signals:
    /**
     * @brief Emitted when a dialog is opened.
     * @param dialogType Type name of the dialog (e.g., "LoginDialog")
     */
    void dialogOpened(const QString& dialogType);

    /**
     * @brief Emitted when a dialog is closed.
     * @param dialogType Type name of the dialog
     */
    void dialogClosed(const QString& dialogType);

    /**
     * @brief Emitted when all dialogs are closed.
     */
    void allDialogsClosed();

private:
    // Private constructor for singleton
    explicit DialogManager(QObject* parent = nullptr);
    ~DialogManager() = default;

    // Prevent copying
    DialogManager(const DialogManager&) = delete;
    DialogManager& operator=(const DialogManager&) = delete;

    // Internal dialog tracking
    void registerDialog(const QString& typeName, QDialog* dialog);
    void unregisterDialog(const QString& typeName);

    // Map of type name to dialog pointer
    QHash<QString, QPointer<QDialog>> m_dialogs;
};

// Template implementations
template<typename T, typename... Args>
T* DialogManager::openDialog(QWidget* parent, Args&&... args)
{
    static_assert(std::is_base_of<QDialog, T>::value,
                  "T must inherit from QDialog");

    const QString typeName = T::staticMetaObject.className();

    // Check if dialog already exists
    auto it = m_dialogs.find(typeName);
    if (it != m_dialogs.end() && !it.value().isNull()) {
        T* existing = qobject_cast<T*>(it.value().data());
        if (existing) {
            // Bring to front
            existing->raise();
            existing->activateWindow();
            return existing;
        }
    }

    // Create new dialog
    T* dialog = new T(std::forward<Args>(args)..., parent);
    registerDialog(typeName, dialog);

    // Auto-remove on close
    connect(dialog, &QDialog::finished, this, [this, typeName]() {
        unregisterDialog(typeName);
    });

    // Handle destruction without finish signal
    connect(dialog, &QObject::destroyed, this, [this, typeName]() {
        if (m_dialogs.contains(typeName)) {
            m_dialogs.remove(typeName);
        }
    });

    emit dialogOpened(typeName);
    return dialog;
}

template<typename T>
T* DialogManager::getDialog() const
{
    const QString typeName = T::staticMetaObject.className();
    auto it = m_dialogs.find(typeName);
    if (it != m_dialogs.end() && !it.value().isNull()) {
        return qobject_cast<T*>(it.value().data());
    }
    return nullptr;
}

template<typename T>
bool DialogManager::isDialogOpen() const
{
    return getDialog<T>() != nullptr;
}

template<typename T>
bool DialogManager::closeDialog()
{
    T* dialog = getDialog<T>();
    if (dialog) {
        dialog->close();
        return true;
    }
    return false;
}

} // namespace MegaCustom

#endif // DIALOGMANAGER_H
