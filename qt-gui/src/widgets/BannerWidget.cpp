#include "BannerWidget.h"
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTimer>

namespace MegaCustom {

BannerWidget::BannerWidget(QWidget* parent)
    : QWidget(parent)
    , m_contentWidget(nullptr)
    , m_iconLabel(nullptr)
    , m_titleLabel(nullptr)
    , m_messageLabel(nullptr)
    , m_actionButton(nullptr)
    , m_autoDismissTimer(nullptr)
    , m_type(Type::Info)
    , m_showIcon(true)
{
    setupUI();
    setType(Type::Info, true);
}

void BannerWidget::setupUI()
{
    setObjectName("BannerWidget");
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    // Main layout with no margins
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Content widget with rounded corners and padding
    m_contentWidget = new QWidget(this);
    m_contentWidget->setObjectName("BannerContent");
    m_contentWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    QHBoxLayout* contentLayout = new QHBoxLayout(m_contentWidget);
    contentLayout->setContentsMargins(12, 12, 12, 12);
    contentLayout->setSpacing(8);

    // Icon label (16x16)
    m_iconLabel = new QLabel(m_contentWidget);
    m_iconLabel->setObjectName("BannerIcon");
    m_iconLabel->setFixedSize(16, 16);
    m_iconLabel->setScaledContents(true);
    contentLayout->addWidget(m_iconLabel, 0, Qt::AlignTop);

    // Text container (title + message)
    QWidget* textContainer = new QWidget(m_contentWidget);
    textContainer->setObjectName("BannerTextContainer");
    textContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    QVBoxLayout* textLayout = new QVBoxLayout(textContainer);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(4);

    // Title label (semibold, hidden by default)
    m_titleLabel = new QLabel(textContainer);
    m_titleLabel->setObjectName("BannerTitle");
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_titleLabel->setTextFormat(Qt::PlainText);
    m_titleLabel->hide();
    textLayout->addWidget(m_titleLabel);

    // Message label (regular weight)
    m_messageLabel = new QLabel(textContainer);
    m_messageLabel->setObjectName("BannerMessage");
    m_messageLabel->setWordWrap(true);
    m_messageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_messageLabel->setTextFormat(Qt::PlainText);
    textLayout->addWidget(m_messageLabel);

    contentLayout->addWidget(textContainer, 1);

    // Action button (hidden by default)
    m_actionButton = new QPushButton(m_contentWidget);
    m_actionButton->setObjectName("BannerActionButton");
    m_actionButton->setCursor(Qt::PointingHandCursor);
    m_actionButton->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_actionButton->setMinimumHeight(26);
    m_actionButton->hide();
    contentLayout->addWidget(m_actionButton, 0, Qt::AlignTop);

    connect(m_actionButton, &QPushButton::clicked, this, [this]() {
        emit actionButtonClicked();
    });

    mainLayout->addWidget(m_contentWidget);

    // Auto-dismiss timer (created but not started)
    m_autoDismissTimer = new QTimer(this);
    m_autoDismissTimer->setSingleShot(true);
    connect(m_autoDismissTimer, &QTimer::timeout, this, &BannerWidget::onAutoDismissTimeout);
}

void BannerWidget::setType(Type type, bool showIcon)
{
    m_type = type;
    m_showIcon = showIcon;
    m_iconLabel->setVisible(showIcon);
    updateStyle();
}

void BannerWidget::setTitle(const QString& text)
{
    m_titleLabel->setText(text);
    m_titleLabel->setVisible(!text.isEmpty());
}

void BannerWidget::setMessage(const QString& text)
{
    m_messageLabel->setText(text);
    m_messageLabel->setVisible(!text.isEmpty());
}

void BannerWidget::setActionButton(const QString& text)
{
    m_actionButton->setText(text);
    m_actionButton->setVisible(!text.isEmpty());
}

void BannerWidget::setAutoDismiss(int milliseconds)
{
    if (m_autoDismissTimer->isActive())
    {
        m_autoDismissTimer->stop();
    }

    if (milliseconds > 0)
    {
        m_autoDismissTimer->start(milliseconds);
    }
}

void BannerWidget::onAutoDismissTimeout()
{
    hide();
    emit dismissed();
}

void BannerWidget::updateStyle()
{
    QString backgroundColor = getBackgroundColor();
    QString iconColor = getIconColor();

    // Set icon based on type
    QString iconStyle;
    switch (m_type)
    {
        case Type::Info:
            iconStyle = QString(
                "QLabel#BannerIcon {"
                "    background-color: %1;"
                "    border-radius: 8px;"
                "    border: 2px solid %1;"
                "}"
            ).arg(iconColor);
            break;

        case Type::Warning:
            iconStyle = QString(
                "QLabel#BannerIcon {"
                "    background-color: transparent;"
                "    border: 2px solid %1;"
                "    border-radius: 8px;"
                "}"
            ).arg(iconColor);
            break;

        case Type::Error:
            iconStyle = QString(
                "QLabel#BannerIcon {"
                "    background-color: transparent;"
                "    border: 2px solid %1;"
                "    border-radius: 8px;"
                "}"
            ).arg(iconColor);
            break;

        case Type::Success:
            iconStyle = QString(
                "QLabel#BannerIcon {"
                "    background-color: %1;"
                "    border-radius: 8px;"
                "    border: 2px solid %1;"
                "}"
            ).arg(iconColor);
            break;
    }

    // Apply styles
    QString styleSheet = QString(
        "QWidget#BannerContent {"
        "    background-color: %1;"
        "    border-radius: 8px;"
        "}"
        "QWidget#BannerTextContainer {"
        "    background-color: transparent;"
        "}"
        "QLabel#BannerTitle {"
        "    font-size: 12px;"
        "    font-weight: 600;"
        "    color: #303233;"
        "    background-color: transparent;"
        "}"
        "QLabel#BannerMessage {"
        "    font-size: 12px;"
        "    font-weight: 400;"
        "    color: #303233;"
        "    background-color: transparent;"
        "}"
        "%2"
        "QPushButton#BannerActionButton {"
        "    font-size: 12px;"
        "    font-weight: 500;"
        "    color: #FFFFFF;"
        "    background-color: #04101E;"
        "    border: none;"
        "    border-radius: 6px;"
        "    padding: 4px 12px;"
        "    min-height: 26px;"
        "}"
        "QPushButton#BannerActionButton:hover {"
        "    background-color: #1a2638;"
        "}"
        "QPushButton#BannerActionButton:pressed {"
        "    background-color: #000000;"
        "}"
    ).arg(backgroundColor, iconStyle);

    setStyleSheet(styleSheet);
}

QString BannerWidget::getBackgroundColor() const
{
    switch (m_type)
    {
        case Type::Info:
            return "#DFF4FE"; // Light blue (from MEGAsync)
        case Type::Warning:
            return "#FEF4C6"; // Light yellow (from MEGAsync)
        case Type::Error:
            return "#FFE4E8"; // Light red/pink (from MEGAsync)
        case Type::Success:
            return "#D1FAE5"; // Light green (matching success theme)
        default:
            return "#DFF4FE";
    }
}

QString BannerWidget::getIconColor() const
{
    switch (m_type)
    {
        case Type::Info:
            return "#0891B2"; // Cyan/blue
        case Type::Warning:
            return "#F59E0B"; // Orange
        case Type::Error:
            return "#EF4444"; // Red
        case Type::Success:
            return "#22C55E"; // Green
        default:
            return "#0891B2";
    }
}

QString BannerWidget::getIconPath() const
{
    // This method is kept for future icon resource integration
    // For now, we're using colored circles/shapes via CSS
    switch (m_type)
    {
        case Type::Info:
            return ":/icons/info.svg";
        case Type::Warning:
            return ":/icons/warning.svg";
        case Type::Error:
            return ":/icons/error.svg";
        case Type::Success:
            return ":/icons/success.svg";
        default:
            return "";
    }
}

} // namespace MegaCustom
