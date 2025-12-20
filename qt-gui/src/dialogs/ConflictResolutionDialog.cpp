#include "ConflictResolutionDialog.h"
#include "widgets/ButtonFactory.h"
#include "utils/DpiScaler.h"
#include "styles/ThemeManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QGridLayout>

namespace MegaCustom {

ConflictResolutionDialog::ConflictResolutionDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Resolve Conflict");
    setMinimumWidth(DpiScaler::scale(500));
    setupUI();
}

void ConflictResolutionDialog::setupUI()
{
    auto& tm = ThemeManager::instance();
    auto* mainLayout = new QVBoxLayout(this);

    // File name
    m_fileNameLabel = new QLabel(this);
    m_fileNameLabel->setStyleSheet(QString("font-weight: bold; font-size: %1px; color: %2;")
        .arg(DpiScaler::scale(14))
        .arg(tm.textPrimary().name()));
    m_fileNameLabel->setWordWrap(true);
    mainLayout->addWidget(m_fileNameLabel);

    // Comparison layout
    auto* comparisonLayout = new QHBoxLayout();

    // Local file info
    auto* localGroup = new QGroupBox("Local File", this);
    auto* localLayout = new QVBoxLayout(localGroup);
    m_localSizeLabel = new QLabel("Size: --", this);
    m_localDateLabel = new QLabel("Modified: --", this);
    localLayout->addWidget(m_localSizeLabel);
    localLayout->addWidget(m_localDateLabel);
    comparisonLayout->addWidget(localGroup);

    // Remote file info
    auto* remoteGroup = new QGroupBox("Remote File", this);
    auto* remoteLayout = new QVBoxLayout(remoteGroup);
    m_remoteSizeLabel = new QLabel("Size: --", this);
    m_remoteDateLabel = new QLabel("Modified: --", this);
    remoteLayout->addWidget(m_remoteSizeLabel);
    remoteLayout->addWidget(m_remoteDateLabel);
    comparisonLayout->addWidget(remoteGroup);

    mainLayout->addLayout(comparisonLayout);

    // Resolution options
    auto* resolutionGroup = new QGroupBox("Choose Resolution", this);
    auto* resolutionLayout = new QVBoxLayout(resolutionGroup);

    m_keepLocalRadio = new QRadioButton("Keep local file (upload to cloud)", this);
    m_keepRemoteRadio = new QRadioButton("Keep remote file (download to local)", this);
    m_keepBothRadio = new QRadioButton("Keep both (rename local copy)", this);
    m_skipRadio = new QRadioButton("Skip this file", this);

    m_keepLocalRadio->setChecked(true);

    resolutionLayout->addWidget(m_keepLocalRadio);
    resolutionLayout->addWidget(m_keepRemoteRadio);
    resolutionLayout->addWidget(m_keepBothRadio);
    resolutionLayout->addWidget(m_skipRadio);

    mainLayout->addWidget(resolutionGroup);

    // Apply to all checkbox
    m_applyToAllCheck = new QCheckBox("Apply this choice to all remaining conflicts", this);
    mainLayout->addWidget(m_applyToAllCheck);

    mainLayout->addStretch();

    // Buttons
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    m_okBtn = ButtonFactory::createPrimary("OK", this);
    m_okBtn->setDefault(true);
    m_cancelBtn = ButtonFactory::createOutline("Cancel", this);
    buttonLayout->addWidget(m_okBtn);
    buttonLayout->addWidget(m_cancelBtn);
    mainLayout->addLayout(buttonLayout);

    // Connections
    connect(m_okBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void ConflictResolutionDialog::setConflict(const QString& fileName,
                                            const FileInfo& localInfo,
                                            const FileInfo& remoteInfo)
{
    m_fileNameLabel->setText(QString("Conflict: %1").arg(fileName));

    m_localSizeLabel->setText(QString("Size: %1").arg(formatSize(localInfo.size)));
    m_localDateLabel->setText(QString("Modified: %1").arg(
        localInfo.modifiedTime.toString("yyyy-MM-dd hh:mm:ss")));

    m_remoteSizeLabel->setText(QString("Size: %1").arg(formatSize(remoteInfo.size)));
    m_remoteDateLabel->setText(QString("Modified: %1").arg(
        remoteInfo.modifiedTime.toString("yyyy-MM-dd hh:mm:ss")));

    // Auto-select newer file
    if (localInfo.modifiedTime > remoteInfo.modifiedTime) {
        m_keepLocalRadio->setChecked(true);
    } else if (remoteInfo.modifiedTime > localInfo.modifiedTime) {
        m_keepRemoteRadio->setChecked(true);
    }
}

ConflictResolutionDialog::Resolution ConflictResolutionDialog::resolution() const
{
    if (m_keepLocalRadio->isChecked()) return Resolution::KEEP_LOCAL;
    if (m_keepRemoteRadio->isChecked()) return Resolution::KEEP_REMOTE;
    if (m_keepBothRadio->isChecked()) return Resolution::KEEP_BOTH;
    return Resolution::SKIP;
}

bool ConflictResolutionDialog::applyToAll() const
{
    return m_applyToAllCheck->isChecked();
}

QString ConflictResolutionDialog::formatSize(qint64 bytes) const
{
    if (bytes < 1024) {
        return QString("%1 B").arg(bytes);
    } else if (bytes < 1024 * 1024) {
        return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    } else if (bytes < 1024 * 1024 * 1024) {
        return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
    } else {
        return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
    }
}

} // namespace MegaCustom
