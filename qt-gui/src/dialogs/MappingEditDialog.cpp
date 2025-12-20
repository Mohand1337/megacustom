#include "MappingEditDialog.h"
#include "RemoteFolderBrowserDialog.h"
#include "widgets/ButtonFactory.h"
#include "utils/PathUtils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QFileDialog>
#include <QGroupBox>

namespace MegaCustom {

MappingEditDialog::MappingEditDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Edit Folder Mapping");
    setMinimumWidth(500);
    setupUI();
}

void MappingEditDialog::setFileController(FileController* controller)
{
    m_fileController = controller;
}

void MappingEditDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);

    // Form group
    auto* formGroup = new QGroupBox("Mapping Configuration", this);
    auto* formLayout = new QFormLayout(formGroup);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText("Enter a descriptive name");
    formLayout->addRow("Name:", m_nameEdit);

    // Local path with browse button
    auto* localLayout = new QHBoxLayout();
    m_localPathEdit = new QLineEdit(this);
    m_localPathEdit->setPlaceholderText("/path/to/local/folder");
    m_browseLocalBtn = ButtonFactory::createSecondary("Browse...", this);
    localLayout->addWidget(m_localPathEdit, 1);
    localLayout->addWidget(m_browseLocalBtn);
    formLayout->addRow("Local Path:", localLayout);

    // Remote path with browse button
    auto* remoteLayout = new QHBoxLayout();
    m_remotePathEdit = new QLineEdit(this);
    m_remotePathEdit->setPlaceholderText("/Cloud/folder");
    m_browseRemoteBtn = ButtonFactory::createSecondary("Select...", this);
    remoteLayout->addWidget(m_remotePathEdit, 1);
    remoteLayout->addWidget(m_browseRemoteBtn);
    formLayout->addRow("Remote Path:", remoteLayout);

    m_enabledCheck = new QCheckBox("Enable this mapping", this);
    m_enabledCheck->setChecked(true);
    formLayout->addRow("", m_enabledCheck);

    mainLayout->addWidget(formGroup);

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
    connect(m_browseLocalBtn, &QPushButton::clicked,
            this, &MappingEditDialog::onBrowseLocalClicked);
    connect(m_browseRemoteBtn, &QPushButton::clicked,
            this, &MappingEditDialog::onBrowseRemoteClicked);
    connect(m_nameEdit, &QLineEdit::textChanged,
            this, &MappingEditDialog::validateInput);
    connect(m_localPathEdit, &QLineEdit::textChanged,
            this, &MappingEditDialog::validateInput);
    connect(m_remotePathEdit, &QLineEdit::textChanged,
            this, &MappingEditDialog::validateInput);
    connect(m_okBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    validateInput();
}

void MappingEditDialog::setMappingData(const QString& name, const QString& localPath,
                                        const QString& remotePath, bool enabled)
{
    m_nameEdit->setText(name);
    m_localPathEdit->setText(localPath);
    m_remotePathEdit->setText(remotePath);
    m_enabledCheck->setChecked(enabled);
}

QString MappingEditDialog::mappingName() const
{
    return m_nameEdit->text().trimmed();
}

QString MappingEditDialog::localPath() const
{
    return PathUtils::normalizeLocalPath(m_localPathEdit->text());
}

QString MappingEditDialog::remotePath() const
{
    return PathUtils::normalizeRemotePath(m_remotePathEdit->text());
}

bool MappingEditDialog::isEnabled() const
{
    return m_enabledCheck->isChecked();
}

void MappingEditDialog::onBrowseLocalClicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Select Local Folder",
                                                    m_localPathEdit->text());
    if (!dir.isEmpty()) {
        m_localPathEdit->setText(dir);
    }
}

void MappingEditDialog::onBrowseRemoteClicked()
{
    RemoteFolderBrowserDialog dialog(this);
    dialog.setFileController(m_fileController);
    dialog.setSelectionMode(RemoteFolderBrowserDialog::SingleFolder);
    dialog.setInitialPath(m_remotePathEdit->text().isEmpty() ? "/" : m_remotePathEdit->text());
    dialog.setTitle("Select Remote Folder");

    if (dialog.exec() == QDialog::Accepted) {
        QString path = dialog.selectedPath();
        if (!path.isEmpty()) {
            m_remotePathEdit->setText(path);
        }
    }
}

void MappingEditDialog::validateInput()
{
    bool valid = !m_nameEdit->text().trimmed().isEmpty() &&
                 !PathUtils::normalizeLocalPath(m_localPathEdit->text()).isEmpty() &&
                 !PathUtils::isPathEmpty(m_remotePathEdit->text());
    m_okBtn->setEnabled(valid);
}

} // namespace MegaCustom
