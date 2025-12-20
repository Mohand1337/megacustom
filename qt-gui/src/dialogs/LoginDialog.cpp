#include "LoginDialog.h"
#include "utils/Settings.h"
#include "utils/DpiScaler.h"
#include "widgets/ButtonFactory.h"
#include "styles/ThemeManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QProgressBar>
#include <QKeyEvent>
#include <QPixmap>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QTimer>
#include <QFrame>
#include <QPalette>

namespace MegaCustom {

LoginDialog::LoginDialog(QWidget* parent)
    : QDialog(parent)
    , m_loading(false)
{
    setupUI();
    applyStyles();
    loadSavedCredentials();

    // Set focus to email field
    m_emailEdit->setFocus();
}

QString LoginDialog::email() const
{
    return m_emailEdit->text().trimmed();
}

QString LoginDialog::password() const
{
    return m_passwordEdit->text();
}

bool LoginDialog::rememberMe() const
{
    return m_rememberCheck->isChecked();
}

void LoginDialog::setEmail(const QString& email)
{
    m_emailEdit->setText(email);
}

void LoginDialog::showError(const QString& message)
{
    m_errorLabel->setText(message);
    m_errorLabel->setVisible(true);

    // Auto-hide error after 5 seconds
    QTimer::singleShot(5000, [this]() {
        m_errorLabel->setVisible(false);
    });
}

void LoginDialog::setLoading(bool loading)
{
    m_loading = loading;
    m_emailEdit->setEnabled(!loading);
    m_passwordEdit->setEnabled(!loading);
    m_rememberCheck->setEnabled(!loading);
    m_loginButton->setEnabled(!loading);
    m_progressBar->setVisible(loading);

    if (loading) {
        m_loginButton->setText("Logging in...");
        m_progressBar->setRange(0, 0); // Indeterminate progress
    } else {
        m_loginButton->setText("Login");
        m_progressBar->setRange(0, 100);
    }
}

void LoginDialog::onLoginClicked()
{
    if (!validateInput()) {
        return;
    }

    // Accept dialog - actual login handled by caller
    accept();
}

void LoginDialog::onCancelClicked()
{
    reject();
}

bool LoginDialog::validateInput()
{
    // Clear any previous error
    m_errorLabel->setVisible(false);

    // Validate email
    QString emailText = email();
    if (emailText.isEmpty()) {
        showError("Please enter your email address");
        m_emailEdit->setFocus();
        return false;
    }

    // Basic email validation
    QRegularExpression emailRegex("^[\\w\\.-]+@[\\w\\.-]+\\.\\w+$");
    if (!emailRegex.match(emailText).hasMatch()) {
        showError("Please enter a valid email address");
        m_emailEdit->setFocus();
        m_emailEdit->selectAll();
        return false;
    }

    // Validate password
    if (password().isEmpty()) {
        showError("Please enter your password");
        m_passwordEdit->setFocus();
        return false;
    }

    if (password().length() < 8) {
        showError("Password must be at least 8 characters");
        m_passwordEdit->setFocus();
        m_passwordEdit->selectAll();
        return false;
    }

    return true;
}

void LoginDialog::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (m_emailEdit->hasFocus() && !email().isEmpty()) {
            m_passwordEdit->setFocus();
        } else if (m_passwordEdit->hasFocus() && !password().isEmpty()) {
            onLoginClicked();
        }
    }
    QDialog::keyPressEvent(event);
}

void LoginDialog::setupUI()
{
    setWindowTitle("Login to MegaCustom");
    setModal(true);
    setFixedSize(DpiScaler::scale(400), DpiScaler::scale(450));

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(DpiScaler::scale(20));
    mainLayout->setContentsMargins(DpiScaler::scale(40), DpiScaler::scale(30), DpiScaler::scale(40), DpiScaler::scale(30));

    // Logo - Red square with "M"
    QFrame* logoFrame = new QFrame(this);
    logoFrame->setObjectName("LoginLogoFrame");
    QHBoxLayout* logoFrameLayout = new QHBoxLayout(logoFrame);
    logoFrameLayout->setAlignment(Qt::AlignCenter);

    m_logoLabel = new QLabel(this);
    m_logoLabel->setObjectName("LoginLogoIcon");
    m_logoLabel->setText("M");
    m_logoLabel->setFixedSize(DpiScaler::scale(64), DpiScaler::scale(64));
    m_logoLabel->setAlignment(Qt::AlignCenter);
    m_logoLabel->setStyleSheet(
        QString("QLabel#LoginLogoIcon {"
        "  background-color: %1;"
        "  color: #FFFFFF;"
        "  font-size: %2px;"
        "  font-weight: bold;"
        "  border-radius: %3px;"
        "}")
        .arg(ThemeManager::instance().brandDefault().name())
        .arg(DpiScaler::scale(32))
        .arg(DpiScaler::scale(12))
    );
    logoFrameLayout->addWidget(m_logoLabel);
    mainLayout->addWidget(logoFrame);

    // Title
    QLabel* titleLabel = new QLabel("MegaCustom Login", this);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet(QString("QLabel { font-size: %1px; font-weight: bold; color: %2; }")
        .arg(DpiScaler::scale(20))
        .arg(ThemeManager::instance().textPrimary().name()));
    mainLayout->addWidget(titleLabel);

    // Subtitle
    QLabel* subtitleLabel = new QLabel("Sign in to your MEGA account", this);
    subtitleLabel->setAlignment(Qt::AlignCenter);
    subtitleLabel->setStyleSheet(QString("QLabel { font-size: %1px; color: %2; }")
        .arg(DpiScaler::scale(13))
        .arg(ThemeManager::instance().textSecondary().name()));
    mainLayout->addWidget(subtitleLabel);

    // Error label (hidden by default)
    m_errorLabel = new QLabel(this);
    // Create a light error background by using supportError with reduced opacity
    QColor errorBg = ThemeManager::instance().supportError();
    errorBg.setAlpha(25);  // Very light tint
    m_errorLabel->setStyleSheet(QString("QLabel { color: %1; padding: %2px; background-color: rgba(%3, %4, %5, 25); border-radius: %6px; }")
        .arg(ThemeManager::instance().supportError().name())
        .arg(DpiScaler::scale(5))
        .arg(errorBg.red()).arg(errorBg.green()).arg(errorBg.blue())
        .arg(DpiScaler::scale(4)));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setVisible(false);
    mainLayout->addWidget(m_errorLabel);

    // Form layout
    QGridLayout* formLayout = new QGridLayout();
    formLayout->setVerticalSpacing(DpiScaler::scale(15));

    // Email field
    QLabel* emailLabel = new QLabel("Email:", this);
    m_emailEdit = new QLineEdit(this);
    m_emailEdit->setPlaceholderText("your@email.com");

    // Set email validator
    QRegularExpression emailRegex("^[\\w\\.-]+@[\\w\\.-]+\\.\\w+$");
    m_emailEdit->setValidator(new QRegularExpressionValidator(emailRegex, this));

    formLayout->addWidget(emailLabel, 0, 0);
    formLayout->addWidget(m_emailEdit, 0, 1);

    // Password field
    QLabel* passwordLabel = new QLabel("Password:", this);
    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setPlaceholderText("••••••••");
    m_passwordEdit->setEchoMode(QLineEdit::Password);

    formLayout->addWidget(passwordLabel, 1, 0);
    formLayout->addWidget(m_passwordEdit, 1, 1);

    mainLayout->addLayout(formLayout);

    // Remember me checkbox - checked by default for convenience
    m_rememberCheck = new QCheckBox("Remember me", this);
    m_rememberCheck->setChecked(true);
    mainLayout->addWidget(m_rememberCheck);

    // Progress bar (hidden by default)
    m_progressBar = new QProgressBar(this);
    m_progressBar->setTextVisible(false);
    m_progressBar->setMaximumHeight(DpiScaler::scale(3));
    m_progressBar->setVisible(false);
    mainLayout->addWidget(m_progressBar);

    // Spacer
    mainLayout->addStretch();

    // Button layout
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(DpiScaler::scale(10));

    // Use ButtonFactory for consistent styling
    m_cancelButton = ButtonFactory::createOutline("Cancel", this);
    m_cancelButton->setFixedWidth(DpiScaler::scale(100));
    connect(m_cancelButton, &QPushButton::clicked, this, &LoginDialog::onCancelClicked);

    m_loginButton = ButtonFactory::createPrimary("Login", this);
    m_loginButton->setFixedWidth(DpiScaler::scale(100));
    m_loginButton->setDefault(true);
    connect(m_loginButton, &QPushButton::clicked, this, &LoginDialog::onLoginClicked);

    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_loginButton);

    mainLayout->addLayout(buttonLayout);

    // Links
    QLabel* linksLabel = new QLabel(this);
    linksLabel->setText("<a href='#'>Forgot password?</a> | <a href='#'>Create account</a>");
    linksLabel->setAlignment(Qt::AlignCenter);
    linksLabel->setOpenExternalLinks(false);
    linksLabel->setStyleSheet(QString("QLabel { color: %1; } QLabel a { color: %2; text-decoration: none; }")
        .arg(ThemeManager::instance().textSecondary().name())
        .arg(ThemeManager::instance().brandDefault().name()));
    mainLayout->addWidget(linksLabel);

    // Connect link clicks (for future implementation)
    connect(linksLabel, &QLabel::linkActivated, [](const QString& link) {
        // Handle forgot password and create account
    });
}

void LoginDialog::loadSavedCredentials()
{
    Settings& settings = Settings::instance();

    // Load saved email if remember me was checked
    if (settings.rememberLogin()) {
        QString savedEmail = settings.lastEmail();
        if (!savedEmail.isEmpty()) {
            m_emailEdit->setText(savedEmail);
            m_rememberCheck->setChecked(true);
            // Focus on password field if email is pre-filled
            m_passwordEdit->setFocus();
        }
    }
}

void LoginDialog::applyStyles()
{
    auto& tm = ThemeManager::instance();

    // Apply modern MEGA-themed styling to dialog (NOT QLineEdit or buttons - those get widget-specific styles)
    // Button styling is now handled by ButtonFactory
    QString dialogStyleSheet = QString(
        "QDialog {"
        "  background-color: %1;"
        "}"
        "QCheckBox {"
        "  font-size: %2px;"
        "  color: %3;"
        "}"
        "QProgressBar {"
        "  background-color: %4;"
        "  border: none;"
        "  border-radius: %5px;"
        "}"
        "QProgressBar::chunk {"
        "  background-color: %6;"
        "  border-radius: %5px;"
        "}"
    ).arg(tm.surfacePrimary().name())
     .arg(DpiScaler::scale(13))
     .arg(tm.textSecondary().name())
     .arg(tm.borderSubtle().name())
     .arg(DpiScaler::scale(2))
     .arg(tm.brandDefault().name());

    setStyleSheet(dialogStyleSheet);

    // IMPORTANT: Widget-specific stylesheets have highest priority in Qt6
    // QPalette is ignored when ANY stylesheet is set on parent, so we use widget-specific QSS
    QString lineEditStyle = QString(
        "QLineEdit {"
        "  padding: %1px;"
        "  border: 1px solid %2;"
        "  border-radius: %3px;"
        "  font-size: %4px;"
        "  min-width: %5px;"
        "  background-color: %6;"
        "  selection-background-color: rgba(217, 0, 7, 80);"
        "  selection-color: %7;"
        "}"
        "QLineEdit:focus {"
        "  border: 2px solid %8;"
        "  padding: %9px;"
        "}"
    ).arg(DpiScaler::scale(12))
     .arg(tm.borderSubtle().name())
     .arg(DpiScaler::scale(6))
     .arg(DpiScaler::scale(14))
     .arg(DpiScaler::scale(250))
     .arg(tm.surfacePrimary().name())
     .arg(tm.textPrimary().name())
     .arg(tm.brandDefault().name())
     .arg(DpiScaler::scale(11));

    m_emailEdit->setStyleSheet(lineEditStyle);
    m_passwordEdit->setStyleSheet(lineEditStyle);
}

// TwoFactorDialog Implementation

TwoFactorDialog::TwoFactorDialog(QWidget* parent)
    : QDialog(parent)
{
    setupUI();
    m_codeEdit->setFocus();
}

QString TwoFactorDialog::code() const
{
    return m_codeEdit->text().trimmed();
}

void TwoFactorDialog::showError(const QString& message)
{
    m_errorLabel->setText(message);
    m_errorLabel->setVisible(true);
}

void TwoFactorDialog::setupUI()
{
    setWindowTitle("Two-Factor Authentication");
    setModal(true);
    setFixedSize(DpiScaler::scale(350), DpiScaler::scale(250));

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(DpiScaler::scale(15));
    mainLayout->setContentsMargins(DpiScaler::scale(30), DpiScaler::scale(30), DpiScaler::scale(30), DpiScaler::scale(30));

    // Icon and title
    QLabel* iconLabel = new QLabel(this);
    iconLabel->setPixmap(QIcon(":/icons/2fa.png").pixmap(DpiScaler::scale(48), DpiScaler::scale(48)));
    iconLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(iconLabel);

    // Instruction
    m_instructionLabel = new QLabel("Enter the 6-digit code from your authenticator app:", this);
    m_instructionLabel->setWordWrap(true);
    m_instructionLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_instructionLabel);

    // Error label (hidden by default)
    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet(QString("QLabel { color: %1; }")
        .arg(ThemeManager::instance().supportError().name()));
    m_errorLabel->setVisible(false);
    mainLayout->addWidget(m_errorLabel);

    // Code input - use widget-specific stylesheet for selection colors (Qt6 priority)
    m_codeEdit = new QLineEdit(this);
    m_codeEdit->setPlaceholderText("000000");
    m_codeEdit->setMaxLength(6);
    m_codeEdit->setAlignment(Qt::AlignCenter);
    m_codeEdit->setStyleSheet(
        QString("QLineEdit {"
        "  font-size: %1px;"
        "  letter-spacing: %2px;"
        "  selection-background-color: rgba(217, 0, 7, 80);"
        "  selection-color: %3;"
        "}")
        .arg(DpiScaler::scale(20))
        .arg(DpiScaler::scale(5))
        .arg(ThemeManager::instance().textPrimary().name())
    );

    // Only allow digits
    m_codeEdit->setValidator(new QRegularExpressionValidator(QRegularExpression("\\d{6}"), this));

    mainLayout->addWidget(m_codeEdit);

    // Spacer
    mainLayout->addStretch();

    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();

    m_cancelButton = ButtonFactory::createOutline("Cancel", this);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    m_verifyButton = ButtonFactory::createPrimary("Verify", this);
    m_verifyButton->setDefault(true);
    connect(m_verifyButton, &QPushButton::clicked, this, &QDialog::accept);

    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_verifyButton);

    mainLayout->addLayout(buttonLayout);

    // Auto-submit when 6 digits entered
    connect(m_codeEdit, &QLineEdit::textChanged, [this](const QString& text) {
        if (text.length() == 6) {
            QTimer::singleShot(500, this, &QDialog::accept);
        }
    });
}

} // namespace MegaCustom