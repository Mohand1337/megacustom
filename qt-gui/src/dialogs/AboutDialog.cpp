#include "AboutDialog.h"
#include "utils/Constants.h"
#include "utils/DpiScaler.h"
#include "widgets/ButtonFactory.h"
#include "styles/ThemeManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QScrollArea>
#include <QSysInfo>

namespace MegaCustom {

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent) {

    setWindowTitle("About MegaCustom");
    setFixedSize(DpiScaler::scale(520), DpiScaler::scale(620));

    auto& tm = ThemeManager::instance();

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(DpiScaler::scale(12));
    mainLayout->setContentsMargins(DpiScaler::scale(30), DpiScaler::scale(25),
                                    DpiScaler::scale(30), DpiScaler::scale(25));

    // Logo and Title section
    QFrame* headerFrame = new QFrame(this);
    QHBoxLayout* headerLayout = new QHBoxLayout(headerFrame);
    headerLayout->setAlignment(Qt::AlignCenter);

    // MEGA-style logo
    QLabel* logoLabel = new QLabel(this);
    logoLabel->setText("M");
    logoLabel->setFixedSize(DpiScaler::scale(64), DpiScaler::scale(64));
    logoLabel->setAlignment(Qt::AlignCenter);
    logoLabel->setStyleSheet(
        QString("QLabel {"
        "  background-color: %1;"
        "  color: #FFFFFF;"
        "  font-size: %2px;"
        "  font-weight: bold;"
        "  border-radius: %3px;"
        "}")
        .arg(tm.brandDefault().name())
        .arg(DpiScaler::scale(32))
        .arg(DpiScaler::scale(12))
    );
    headerLayout->addWidget(logoLabel);
    mainLayout->addWidget(headerFrame);

    // App name
    QLabel* titleLabel = new QLabel(Constants::APP_NAME, this);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet(QString("QLabel { font-size: %1px; font-weight: bold; color: %2; }")
        .arg(DpiScaler::scale(22))
        .arg(tm.textPrimary().name()));
    mainLayout->addWidget(titleLabel);

    // Version and build info
    QString versionText = QString("Version %1").arg(Constants::APP_VERSION);
    QLabel* versionLabel = new QLabel(versionText, this);
    versionLabel->setAlignment(Qt::AlignCenter);
    versionLabel->setStyleSheet(QString("QLabel { font-size: %1px; color: %2; }")
        .arg(DpiScaler::scale(13))
        .arg(tm.textSecondary().name()));
    mainLayout->addWidget(versionLabel);

    // Build info (Qt version and system)
    QString buildInfo = QString("Built with Qt %1 | %2")
        .arg(QT_VERSION_STR)
        .arg(QSysInfo::prettyProductName());
    QLabel* buildLabel = new QLabel(buildInfo, this);
    buildLabel->setAlignment(Qt::AlignCenter);
    buildLabel->setStyleSheet(QString("QLabel { font-size: %1px; color: %2; }")
        .arg(DpiScaler::scale(11))
        .arg(tm.textSecondary().name()));
    mainLayout->addWidget(buildLabel);

    // Separator
    QFrame* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet(QString("QFrame { background-color: %1; max-height: 1px; }")
        .arg(tm.borderSubtle().name()));
    mainLayout->addWidget(separator);

    // Description
    QLabel* descLabel = new QLabel(
        "A powerful desktop client for MEGA cloud storage.\n"
        "Manage files, sync folders, and automate uploads with ease.",
        this);
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet(QString("QLabel { font-size: %1px; color: %2; line-height: 1.4; }")
        .arg(DpiScaler::scale(13))
        .arg(tm.textSecondary().name()));
    mainLayout->addWidget(descLabel);

    mainLayout->addSpacing(DpiScaler::scale(4));

    // Features section
    QLabel* featuresTitle = new QLabel("Features", this);
    featuresTitle->setStyleSheet(QString("QLabel { font-size: %1px; font-weight: bold; color: %2; }")
        .arg(DpiScaler::scale(14))
        .arg(tm.textPrimary().name()));
    mainLayout->addWidget(featuresTitle);

    // Feature list using scroll area
    QScrollArea* featureScrollArea = new QScrollArea(this);
    featureScrollArea->setWidgetResizable(true);
    featureScrollArea->setFrameShape(QFrame::NoFrame);
    featureScrollArea->setMaximumHeight(DpiScaler::scale(200));
    featureScrollArea->setStyleSheet(QString("QScrollArea { background: %1; }")
        .arg(tm.surfacePrimary().name()));

    QWidget* featureWidget = new QWidget();
    QVBoxLayout* featureLayout = new QVBoxLayout(featureWidget);
    featureLayout->setSpacing(DpiScaler::scale(4));
    featureLayout->setContentsMargins(0, 0, 0, 0);

    // Features with descriptions
    struct FeatureInfo {
        QString icon;
        QString title;
        QString description;
    };

    QList<FeatureInfo> features = {
        {"folder", "Cloud Drive", "Browse, upload, download, and manage your MEGA cloud files with a modern interface"},
        {"link", "Folder Mapper", "Create persistent mappings between local folders and cloud destinations for quick access"},
        {"upload", "Multi Uploader", "Upload files to multiple cloud locations simultaneously with customizable rules"},
        {"sync", "Smart Sync", "Keep local and cloud folders synchronized with intelligent conflict resolution"},
        {"copy", "Cloud Copier", "Copy or move files between cloud locations without downloading"},
        {"queue", "Transfer Queue", "Monitor all uploads and downloads with pause, resume, and priority controls"},
        {"search", "Global Search", "Find files anywhere in your cloud storage instantly"},
        {"session", "Auto Login", "Securely save your session for automatic login on app restart"}
    };

    for (const FeatureInfo& feature : features) {
        QFrame* featureFrame = new QFrame(featureWidget);
        QHBoxLayout* featureItemLayout = new QHBoxLayout(featureFrame);
        featureItemLayout->setContentsMargins(0, DpiScaler::scale(2), 0, DpiScaler::scale(2));
        featureItemLayout->setSpacing(DpiScaler::scale(8));

        // Feature bullet
        QLabel* bulletLabel = new QLabel(QString::fromUtf8("\u2022"), featureFrame);
        bulletLabel->setStyleSheet(QString("QLabel { font-size: %1px; color: %2; }")
            .arg(DpiScaler::scale(14))
            .arg(tm.brandDefault().name()));
        bulletLabel->setFixedWidth(DpiScaler::scale(12));
        featureItemLayout->addWidget(bulletLabel);

        // Feature text
        QLabel* featureLabel = new QLabel(QString("<b>%1</b> - %2").arg(feature.title, feature.description), featureFrame);
        featureLabel->setWordWrap(true);
        featureLabel->setStyleSheet(QString("QLabel { font-size: %1px; color: %2; }")
            .arg(DpiScaler::scale(11))
            .arg(tm.textSecondary().name()));
        featureItemLayout->addWidget(featureLabel, 1);

        featureLayout->addWidget(featureFrame);
    }
    featureLayout->addStretch();

    featureScrollArea->setWidget(featureWidget);
    mainLayout->addWidget(featureScrollArea);

    mainLayout->addStretch();

    // Separator
    QFrame* separator2 = new QFrame(this);
    separator2->setFrameShape(QFrame::HLine);
    separator2->setStyleSheet(QString("QFrame { background-color: %1; max-height: 1px; }")
        .arg(tm.borderSubtle().name()));
    mainLayout->addWidget(separator2);

    // Technical info
    QLabel* techLabel = new QLabel(
        QString("Platform: %1 | Architecture: %2")
            .arg(QSysInfo::productType())
            .arg(QSysInfo::currentCpuArchitecture()),
        this);
    techLabel->setAlignment(Qt::AlignCenter);
    techLabel->setStyleSheet(QString("QLabel { font-size: %1px; color: %2; }")
        .arg(DpiScaler::scale(10))
        .arg(tm.textSecondary().name()));
    mainLayout->addWidget(techLabel);

    // Copyright and links
    QLabel* copyrightLabel = new QLabel(
        QString::fromUtf8("\u00A9 2024 %1 \u2022 Powered by MEGA SDK").arg(Constants::APP_ORGANIZATION),
        this);
    copyrightLabel->setAlignment(Qt::AlignCenter);
    copyrightLabel->setStyleSheet(QString("QLabel { font-size: %1px; color: %2; }")
        .arg(DpiScaler::scale(11))
        .arg(tm.textSecondary().name()));
    mainLayout->addWidget(copyrightLabel);

    // OK button - using ButtonFactory for consistent styling
    QPushButton* okButton = ButtonFactory::createPrimary("OK", this);
    okButton->setFixedWidth(DpiScaler::scale(100));
    connect(okButton, &QPushButton::clicked, this, &QDialog::accept);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);
    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);

    // Dialog styling
    setStyleSheet(QString("QDialog { background-color: %1; }")
        .arg(tm.surfacePrimary().name()));
}

} // namespace MegaCustom

#include "moc_AboutDialog.cpp"
