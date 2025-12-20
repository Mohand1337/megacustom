#include "dialogs/CopyConflictDialog.h"
#include "widgets/ButtonFactory.h"
#include "utils/DpiScaler.h"
#include "styles/ThemeManager.h"
#include <QHBoxLayout>
#include <QGroupBox>
#include <QStyle>

namespace MegaCustom {

CopyConflictDialog::CopyConflictDialog(const ConflictInfo& conflict, QWidget* parent)
    : QDialog(parent)
    , m_conflict(conflict)
{
    setWindowTitle("Conflict Detected");
    setModal(true);
    setMinimumWidth(DpiScaler::scale(450));
    setupUI();
}

void CopyConflictDialog::setupUI() {
    auto& tm = ThemeManager::instance();

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(DpiScaler::scale(16));
    mainLayout->setContentsMargins(DpiScaler::scale(20), DpiScaler::scale(20),
                                    DpiScaler::scale(20), DpiScaler::scale(20));

    // Header with icon and message
    QHBoxLayout* headerLayout = new QHBoxLayout();

    m_iconLabel = new QLabel(this);
    m_iconLabel->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxWarning)
        .pixmap(DpiScaler::scale(48), DpiScaler::scale(48)));
    headerLayout->addWidget(m_iconLabel);

    QVBoxLayout* msgLayout = new QVBoxLayout();
    QLabel* titleLabel = new QLabel("<b>Item already exists at destination</b>", this);
    titleLabel->setStyleSheet(QString("font-size: %1px; color: %2;")
        .arg(DpiScaler::scale(14))
        .arg(tm.textPrimary().name()));
    msgLayout->addWidget(titleLabel);

    m_messageLabel = new QLabel(this);
    m_messageLabel->setText(QString("\"%1\" already exists at:\n%2")
                           .arg(m_conflict.itemName)
                           .arg(m_conflict.destinationPath));
    m_messageLabel->setWordWrap(true);
    m_messageLabel->setStyleSheet(QString("color: %1;").arg(tm.textSecondary().name()));
    msgLayout->addWidget(m_messageLabel);

    headerLayout->addLayout(msgLayout);
    headerLayout->addStretch();
    mainLayout->addLayout(headerLayout);

    // Comparison section
    QGroupBox* compareGroup = new QGroupBox("Compare", this);
    QVBoxLayout* compareLayout = new QVBoxLayout(compareGroup);

    // Info label style
    QString infoLabelStyle = QString(
        "background-color: %1; padding: %2px; border: 1px solid %3; border-radius: %4px;")
        .arg(tm.surfacePrimary().name())
        .arg(DpiScaler::scale(8))
        .arg(tm.borderSubtle().name())
        .arg(DpiScaler::scale(4));

    // Existing item info
    QString existingText = QString("<b>Existing:</b> %1").arg(m_conflict.itemName);
    if (!m_conflict.isFolder) {
        existingText += QString("<br>Size: %1").arg(formatSize(m_conflict.existingSize));
    }
    if (m_conflict.existingModTime.isValid()) {
        existingText += QString("<br>Modified: %1")
                       .arg(m_conflict.existingModTime.toString("yyyy-MM-dd hh:mm:ss"));
    }
    m_existingInfoLabel = new QLabel(existingText, this);
    m_existingInfoLabel->setStyleSheet(infoLabelStyle);
    compareLayout->addWidget(m_existingInfoLabel);

    // Source item info
    QString sourceText = QString("<b>Source:</b> %1").arg(m_conflict.itemName);
    if (!m_conflict.isFolder) {
        sourceText += QString("<br>Size: %1").arg(formatSize(m_conflict.sourceSize));
    }
    if (m_conflict.sourceModTime.isValid()) {
        sourceText += QString("<br>Modified: %1")
                     .arg(m_conflict.sourceModTime.toString("yyyy-MM-dd hh:mm:ss"));
    }
    m_sourceInfoLabel = new QLabel(sourceText, this);
    m_sourceInfoLabel->setStyleSheet(infoLabelStyle);
    compareLayout->addWidget(m_sourceInfoLabel);

    mainLayout->addWidget(compareGroup);

    // Apply to all checkbox
    m_applyToAllCheck = new QCheckBox("Apply to all future conflicts", this);
    mainLayout->addWidget(m_applyToAllCheck);

    // Button row
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(DpiScaler::scale(8));

    QPushButton* skipBtn = ButtonFactory::createOutline("Skip", this);
    skipBtn->setMinimumWidth(DpiScaler::scale(80));
    connect(skipBtn, &QPushButton::clicked, this, &CopyConflictDialog::onSkipClicked);
    buttonLayout->addWidget(skipBtn);

    QPushButton* overwriteBtn = ButtonFactory::createSecondary("Overwrite", this);
    overwriteBtn->setMinimumWidth(DpiScaler::scale(80));
    connect(overwriteBtn, &QPushButton::clicked, this, &CopyConflictDialog::onOverwriteClicked);
    buttonLayout->addWidget(overwriteBtn);

    QPushButton* renameBtn = ButtonFactory::createSecondary("Rename", this);
    renameBtn->setMinimumWidth(DpiScaler::scale(80));
    connect(renameBtn, &QPushButton::clicked, this, &CopyConflictDialog::onRenameClicked);
    buttonLayout->addWidget(renameBtn);

    buttonLayout->addStretch();

    QPushButton* cancelBtn = ButtonFactory::createDestructive("Cancel All", this);
    cancelBtn->setMinimumWidth(DpiScaler::scale(100));
    connect(cancelBtn, &QPushButton::clicked, this, &CopyConflictDialog::onCancelClicked);
    buttonLayout->addWidget(cancelBtn);

    mainLayout->addLayout(buttonLayout);
}

QString CopyConflictDialog::formatSize(qint64 bytes) {
    if (bytes < 1024) {
        return QString("%1 B").arg(bytes);
    } else if (bytes < 1024 * 1024) {
        return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    } else if (bytes < 1024 * 1024 * 1024) {
        return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    } else {
        return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
    }
}

void CopyConflictDialog::onSkipClicked() {
    m_resolution = m_applyToAllCheck->isChecked() ? Resolution::SKIP_ALL : Resolution::SKIP;
    accept();
}

void CopyConflictDialog::onOverwriteClicked() {
    m_resolution = m_applyToAllCheck->isChecked() ? Resolution::OVERWRITE_ALL : Resolution::OVERWRITE;
    accept();
}

void CopyConflictDialog::onRenameClicked() {
    m_resolution = Resolution::RENAME;
    accept();
}

void CopyConflictDialog::onCancelClicked() {
    m_resolution = Resolution::CANCEL;
    reject();
}

} // namespace MegaCustom
