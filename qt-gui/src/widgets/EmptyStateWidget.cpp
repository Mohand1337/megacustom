#include "EmptyStateWidget.h"
#include "styles/ThemeManager.h"
#include "utils/DpiScaler.h"
#include <QSvgRenderer>
#include <QPainter>
#include <QPixmap>

using namespace MegaCustom;

EmptyStateWidget::EmptyStateWidget(const QString& iconPath,
                                   const QString& title,
                                   const QString& description,
                                   const QString& ctaText,
                                   QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(DpiScaler::scale(12));

    auto& tm = ThemeManager::instance();

    // Icon
    m_iconLabel = new QLabel(this);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    setIcon(iconPath);
    layout->addStretch();
    layout->addWidget(m_iconLabel);

    // Title
    m_titleLabel = new QLabel(title, this);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setStyleSheet(
        QString("font-size: %1px; font-weight: 600; color: %2;")
            .arg(DpiScaler::scale(18))
            .arg(tm.textPrimary().name()));
    layout->addWidget(m_titleLabel);

    // Description
    m_descLabel = new QLabel(description, this);
    m_descLabel->setAlignment(Qt::AlignCenter);
    m_descLabel->setWordWrap(true);
    m_descLabel->setMaximumWidth(DpiScaler::scale(360));
    m_descLabel->setStyleSheet(
        QString("font-size: %1px; color: %2;")
            .arg(DpiScaler::scale(13))
            .arg(tm.textSecondary().name()));
    layout->addWidget(m_descLabel, 0, Qt::AlignHCenter);

    // CTA button (optional)
    m_ctaButton = nullptr;
    if (!ctaText.isEmpty()) {
        m_ctaButton = new QPushButton(ctaText, this);
        m_ctaButton->setObjectName("EmptyStateCTA");
        m_ctaButton->setCursor(Qt::PointingHandCursor);
        m_ctaButton->setStyleSheet(
            QString("QPushButton { background-color: %1; color: white; border: none; "
                    "border-radius: %2px; padding: %3px %4px; font-size: %5px; font-weight: 500; }"
                    "QPushButton:hover { background-color: %6; }")
                .arg(tm.brandDefault().name())
                .arg(DpiScaler::scale(6))
                .arg(DpiScaler::scale(8))
                .arg(DpiScaler::scale(20))
                .arg(DpiScaler::scale(13))
                .arg(tm.brandDefault().darker(110).name()));
        connect(m_ctaButton, &QPushButton::clicked, this, &EmptyStateWidget::actionClicked);
        layout->addSpacing(DpiScaler::scale(8));
        layout->addWidget(m_ctaButton, 0, Qt::AlignHCenter);
    }

    layout->addStretch();

    // Update colors on theme change
    connect(&tm, &ThemeManager::themeChanged, this, [this]() {
        auto& t = ThemeManager::instance();
        m_titleLabel->setStyleSheet(
            QString("font-size: %1px; font-weight: 600; color: %2;")
                .arg(DpiScaler::scale(18))
                .arg(t.textPrimary().name()));
        m_descLabel->setStyleSheet(
            QString("font-size: %1px; color: %2;")
                .arg(DpiScaler::scale(13))
                .arg(t.textSecondary().name()));
        if (m_ctaButton) {
            m_ctaButton->setStyleSheet(
                QString("QPushButton { background-color: %1; color: white; border: none; "
                        "border-radius: %2px; padding: %3px %4px; font-size: %5px; font-weight: 500; }"
                        "QPushButton:hover { background-color: %6; }")
                    .arg(t.brandDefault().name())
                    .arg(DpiScaler::scale(6))
                    .arg(DpiScaler::scale(8))
                    .arg(DpiScaler::scale(20))
                    .arg(DpiScaler::scale(13))
                    .arg(t.brandDefault().darker(110).name()));
        }
        setIcon(m_iconLabel->property("iconPath").toString());
    });
}

void EmptyStateWidget::setTitle(const QString& title)
{
    m_titleLabel->setText(title);
}

void EmptyStateWidget::setDescription(const QString& description)
{
    m_descLabel->setText(description);
}

void EmptyStateWidget::setIcon(const QString& iconPath)
{
    m_iconLabel->setProperty("iconPath", iconPath);
    int iconSize = DpiScaler::scale(64);

    if (iconPath.endsWith(".svg")) {
        QSvgRenderer renderer(iconPath);
        if (renderer.isValid()) {
            QPixmap pixmap(iconSize, iconSize);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            renderer.render(&painter);
            m_iconLabel->setPixmap(pixmap);
        }
    } else {
        QPixmap pixmap(iconPath);
        if (!pixmap.isNull()) {
            m_iconLabel->setPixmap(pixmap.scaled(iconSize, iconSize,
                                                  Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }
}
