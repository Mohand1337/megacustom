#ifndef MEGACUSTOM_LOGINDIALOG_H
#define MEGACUSTOM_LOGINDIALOG_H

#include <QDialog>
#include <QString>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QPushButton;
class QCheckBox;
class QLabel;
class QProgressBar;
QT_END_NAMESPACE

namespace MegaCustom {

/**
 * Login dialog for user authentication
 */
class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * Constructor
     * @param parent Parent widget
     */
    explicit LoginDialog(QWidget* parent = nullptr);

    /**
     * Destructor
     */
    virtual ~LoginDialog() = default;

    /**
     * Get entered email
     * @return Email address
     */
    QString email() const;

    /**
     * Get entered password
     * @return Password
     */
    QString password() const;

    /**
     * Check if remember me is checked
     * @return true if should remember login
     */
    bool rememberMe() const;

    /**
     * Set email field
     * @param email Email to set
     */
    void setEmail(const QString& email);

    /**
     * Show error message
     * @param message Error message to display
     */
    void showError(const QString& message);

    /**
     * Show loading state
     * @param loading true to show loading
     */
    void setLoading(bool loading);

signals:
    /**
     * Emitted when 2FA code is needed
     */
    void twoFactorRequired();

public slots:
    /**
     * Handle login button click
     */
    void onLoginClicked();

    /**
     * Handle cancel button click
     */
    void onCancelClicked();

    /**
     * Validate input fields
     */
    bool validateInput();

protected:
    /**
     * Handle key press events
     * @param event Key event
     */
    void keyPressEvent(QKeyEvent* event) override;

private:
    /**
     * Set up the UI
     */
    void setupUI();

    /**
     * Load saved credentials if any
     */
    void loadSavedCredentials();

    /**
     * Apply styles
     */
    void applyStyles();

private:
    // UI elements
    QLineEdit* m_emailEdit;
    QLineEdit* m_passwordEdit;
    QCheckBox* m_rememberCheck;
    QPushButton* m_loginButton;
    QPushButton* m_cancelButton;
    QLabel* m_errorLabel;
    QProgressBar* m_progressBar;
    QLabel* m_logoLabel;

    // State
    bool m_loading;
};

/**
 * Two-factor authentication dialog
 */
class TwoFactorDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * Constructor
     * @param parent Parent widget
     */
    explicit TwoFactorDialog(QWidget* parent = nullptr);

    /**
     * Get entered code
     * @return 2FA code
     */
    QString code() const;

    /**
     * Show error message
     * @param message Error message
     */
    void showError(const QString& message);

private:
    /**
     * Set up the UI
     */
    void setupUI();

private:
    QLineEdit* m_codeEdit;
    QPushButton* m_verifyButton;
    QPushButton* m_cancelButton;
    QLabel* m_errorLabel;
    QLabel* m_instructionLabel;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_LOGINDIALOG_H