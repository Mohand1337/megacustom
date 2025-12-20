// ButtonFactory.cpp - Factory for creating consistently styled buttons
#include "ButtonFactory.h"
#include "IconButton.h"
#include "../styles/ThemeManager.h"
#include <QIcon>

namespace MegaCustom {

QPushButton* ButtonFactory::createPrimary(const QString& text, QWidget* parent, Size size)
{
    QPushButton* button = new QPushButton(text, parent);
    setupButton(button, size);

    // Use MEGA brand red colors (not the dark "primary" from MEGAsync tokens)
    // Original MEGA red: #D90007 (we use ButtonBrand which is #DD1405, very close)
    auto& theme = ThemeManager::instance();
    QColor bgColor = theme.buttonBrand();
    QColor bgHover = theme.buttonBrandHover();
    QColor bgPressed = theme.buttonBrandPressed();
    QColor textColor = Qt::white;  // White text on red background
    QColor disabledBg = theme.buttonDisabled();
    QColor disabledText = theme.textDisabled();

    QString styleSheet = QString(R"(
        QPushButton {
            background-color: %1;
            color: %2;
            border: none;
            border-radius: 6px;
            font-weight: 600;
            %3
        }
        QPushButton:hover {
            background-color: %4;
        }
        QPushButton:pressed {
            background-color: %5;
        }
        QPushButton:disabled {
            background-color: %6;
            color: %7;
        }
    )")
    .arg(bgColor.name())
    .arg(textColor.name())
    .arg(paddingForSize(size))
    .arg(bgHover.name())
    .arg(bgPressed.name())
    .arg(disabledBg.name())
    .arg(disabledText.name());

    button->setStyleSheet(styleSheet);

    // Connect to theme changes
    QObject::connect(&theme, &ThemeManager::themeChanged, button, [button]() {
        auto& tm = ThemeManager::instance();
        QColor bg = tm.buttonBrand();
        QColor bgHov = tm.buttonBrandHover();
        QColor bgPress = tm.buttonBrandPressed();
        QColor disBg = tm.buttonDisabled();
        QColor disTxt = tm.textDisabled();

        QString ss = QString(R"(
            QPushButton {
                background-color: %1;
                color: white;
                border: none;
                border-radius: 6px;
                font-weight: 600;
            }
            QPushButton:hover {
                background-color: %2;
            }
            QPushButton:pressed {
                background-color: %3;
            }
            QPushButton:disabled {
                background-color: %4;
                color: %5;
            }
        )")
        .arg(bg.name())
        .arg(bgHov.name())
        .arg(bgPress.name())
        .arg(disBg.name())
        .arg(disTxt.name());

        button->setStyleSheet(ss);
        button->update();
    });

    return button;
}

QPushButton* ButtonFactory::createSecondary(const QString& text, QWidget* parent, Size size)
{
    QPushButton* button = new QPushButton(text, parent);
    setupButton(button, size);

    auto& theme = ThemeManager::instance();
    QColor bgColor = theme.buttonSecondary();
    QColor bgHover = theme.buttonSecondaryHover();
    QColor bgPressed = theme.buttonSecondaryPressed();
    QColor textColor = theme.textPrimary();
    QColor disabledBg = theme.buttonDisabled();
    QColor disabledText = theme.textDisabled();

    QString styleSheet = QString(R"(
        QPushButton {
            background-color: %1;
            color: %2;
            border: none;
            border-radius: 6px;
            font-weight: 500;
            %3
        }
        QPushButton:hover {
            background-color: %4;
        }
        QPushButton:pressed {
            background-color: %5;
        }
        QPushButton:disabled {
            background-color: %6;
            color: %7;
        }
    )")
    .arg(bgColor.name(QColor::HexArgb))
    .arg(textColor.name())
    .arg(paddingForSize(size))
    .arg(bgHover.name(QColor::HexArgb))
    .arg(bgPressed.name(QColor::HexArgb))
    .arg(disabledBg.name(QColor::HexArgb))
    .arg(disabledText.name());

    button->setStyleSheet(styleSheet);

    return button;
}

QPushButton* ButtonFactory::createOutline(const QString& text, QWidget* parent, Size size)
{
    QPushButton* button = new QPushButton(text, parent);
    setupButton(button, size);

    auto& theme = ThemeManager::instance();
    QColor borderColor = theme.borderStrong();
    QColor textColor = theme.textPrimary();
    QColor hoverBg = theme.isDarkMode()
        ? QColor(255, 255, 255, 20)
        : QColor(0, 0, 0, 13);
    QColor pressedBg = theme.isDarkMode()
        ? QColor(255, 255, 255, 30)
        : QColor(0, 0, 0, 20);
    QColor disabledBorder = theme.borderSubtle();
    QColor disabledText = theme.textDisabled();

    QString styleSheet = QString(R"(
        QPushButton {
            background-color: transparent;
            color: %1;
            border: 1px solid %2;
            border-radius: 6px;
            font-weight: 500;
            %3
        }
        QPushButton:hover {
            background-color: %4;
            border-color: %2;
        }
        QPushButton:pressed {
            background-color: %5;
        }
        QPushButton:disabled {
            border-color: %6;
            color: %7;
        }
    )")
    .arg(textColor.name())
    .arg(borderColor.name())
    .arg(paddingForSize(size))
    .arg(hoverBg.name(QColor::HexArgb))
    .arg(pressedBg.name(QColor::HexArgb))
    .arg(disabledBorder.name())
    .arg(disabledText.name());

    button->setStyleSheet(styleSheet);

    return button;
}

QPushButton* ButtonFactory::createDestructive(const QString& text, QWidget* parent, Size size)
{
    QPushButton* button = new QPushButton(text, parent);
    setupButton(button, size);

    auto& theme = ThemeManager::instance();
    QColor bgColor = theme.supportError();
    // Darken for hover/pressed states
    QColor bgHover = bgColor.darker(110);
    QColor bgPressed = bgColor.darker(120);
    QColor textColor = Qt::white;
    QColor disabledBg = theme.buttonDisabled();
    QColor disabledText = theme.textDisabled();

    QString styleSheet = QString(R"(
        QPushButton {
            background-color: %1;
            color: %2;
            border: none;
            border-radius: 6px;
            font-weight: 600;
            %3
        }
        QPushButton:hover {
            background-color: %4;
        }
        QPushButton:pressed {
            background-color: %5;
        }
        QPushButton:disabled {
            background-color: %6;
            color: %7;
        }
    )")
    .arg(bgColor.name())
    .arg(textColor.name())
    .arg(paddingForSize(size))
    .arg(bgHover.name())
    .arg(bgPressed.name())
    .arg(disabledBg.name())
    .arg(disabledText.name());

    button->setStyleSheet(styleSheet);

    return button;
}

QPushButton* ButtonFactory::createText(const QString& text, QWidget* parent, Size size)
{
    QPushButton* button = new QPushButton(text, parent);
    setupButton(button, size);

    auto& theme = ThemeManager::instance();
    QColor textColor = theme.brandDefault();
    QColor textHover = theme.brandHover();
    QColor textPressed = theme.brandPressed();
    QColor disabledText = theme.textDisabled();

    QString styleSheet = QString(R"(
        QPushButton {
            background-color: transparent;
            color: %1;
            border: none;
            border-radius: 6px;
            font-weight: 500;
            %2
        }
        QPushButton:hover {
            color: %3;
            background-color: rgba(0, 0, 0, 0.03);
        }
        QPushButton:pressed {
            color: %4;
        }
        QPushButton:disabled {
            color: %5;
        }
    )")
    .arg(textColor.name())
    .arg(paddingForSize(size))
    .arg(textHover.name())
    .arg(textPressed.name())
    .arg(disabledText.name());

    button->setStyleSheet(styleSheet);

    return button;
}

IconButton* ButtonFactory::createIconButton(const QString& iconPath, QWidget* parent, Size size)
{
    IconButton* button = new IconButton(iconPath, parent);

    int btnSize;
    int iconSize;

    switch (size) {
        case Small:
            btnSize = 28;
            iconSize = 16;
            break;
        case Large:
            btnSize = 44;
            iconSize = 24;
            break;
        case Medium:
        default:
            btnSize = 36;
            iconSize = 20;
            break;
    }

    button->setFixedSize(btnSize, btnSize);
    button->setIconSize(iconSize);

    return button;
}

QPushButton* ButtonFactory::createWithIcon(const QString& iconPath, const QString& text,
                                            QWidget* parent, Size size)
{
    QPushButton* button = createPrimary(text, parent, size);

    if (!iconPath.isEmpty()) {
        button->setIcon(QIcon(iconPath));
        button->setIconSize(QSize(fontSizeForSize(size) + 2, fontSizeForSize(size) + 2));
    }

    return button;
}

void ButtonFactory::setupButton(QPushButton* button, Size size)
{
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedHeight(heightForSize(size));
    button->setMinimumWidth(heightForSize(size) * 2);  // Minimum 2x height ratio

    // Set font
    QFont font = button->font();
    font.setPointSize(fontSizeForSize(size));
    button->setFont(font);
}

int ButtonFactory::heightForSize(Size size)
{
    switch (size) {
        case Small:  return 28;
        case Large:  return 44;
        case Medium:
        default:     return 36;
    }
}

QString ButtonFactory::paddingForSize(Size size)
{
    switch (size) {
        case Small:  return "padding: 4px 12px;";
        case Large:  return "padding: 10px 24px;";
        case Medium:
        default:     return "padding: 6px 16px;";
    }
}

int ButtonFactory::fontSizeForSize(Size size)
{
    switch (size) {
        case Small:  return 11;
        case Large:  return 14;
        case Medium:
        default:     return 12;
    }
}

} // namespace MegaCustom
