#include "AddDestinationDialog.h"
#include "RemoteFolderBrowserDialog.h"
#include "widgets/ButtonFactory.h"
#include "utils/PathUtils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QGroupBox>

namespace MegaCustom {

AddDestinationDialog::AddDestinationDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Add Upload Destination");
    setMinimumWidth(450);
    setupUI();
}

void AddDestinationDialog::setFileController(FileController* controller)
{
    m_fileController = controller;
}

void AddDestinationDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);

    // Form group
    auto* formGroup = new QGroupBox("Destination Configuration", this);
    auto* formLayout = new QFormLayout(formGroup);

    // Remote path with browse button
    auto* pathLayout = new QHBoxLayout();
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setPlaceholderText("/Cloud/Photos");
    m_browseBtn = ButtonFactory::createSecondary("Select...", this);
    pathLayout->addWidget(m_pathEdit, 1);
    pathLayout->addWidget(m_browseBtn);
    formLayout->addRow("Remote Path:", pathLayout);

    // Alias (optional)
    m_aliasEdit = new QLineEdit(this);
    m_aliasEdit->setPlaceholderText("Optional friendly name (e.g., 'Photos')");
    formLayout->addRow("Alias:", m_aliasEdit);

    // Create if missing option
    m_createCheck = new QCheckBox("Create folder if it doesn't exist", this);
    m_createCheck->setChecked(true);
    formLayout->addRow("", m_createCheck);

    mainLayout->addWidget(formGroup);

    // Help text
    auto* helpLabel = new QLabel(
        "Tip: Use distribution rules to automatically route files to different "
        "destinations based on file type, size, or name patterns.", this);
    helpLabel->setWordWrap(true);
    helpLabel->setStyleSheet("color: gray; font-size: 11px;");
    mainLayout->addWidget(helpLabel);

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
    connect(m_browseBtn, &QPushButton::clicked,
            this, &AddDestinationDialog::onBrowseClicked);
    connect(m_pathEdit, &QLineEdit::textChanged,
            this, &AddDestinationDialog::validateInput);
    connect(m_okBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    validateInput();
}

void AddDestinationDialog::setDestinationData(const QString& path, const QString& alias, bool createIfMissing)
{
    m_pathEdit->setText(path);
    m_aliasEdit->setText(alias);
    m_createCheck->setChecked(createIfMissing);
}

QString AddDestinationDialog::remotePath() const
{
    return PathUtils::normalizeRemotePath(m_pathEdit->text());
}

QString AddDestinationDialog::alias() const
{
    return m_aliasEdit->text().trimmed();
}

bool AddDestinationDialog::createIfMissing() const
{
    return m_createCheck->isChecked();
}

void AddDestinationDialog::onBrowseClicked()
{
    RemoteFolderBrowserDialog dialog(this);
    dialog.setFileController(m_fileController);
    dialog.setSelectionMode(RemoteFolderBrowserDialog::SingleFolder);
    dialog.setInitialPath(m_pathEdit->text().isEmpty() ? "/" : m_pathEdit->text());
    dialog.setTitle("Select Upload Destination");

    if (dialog.exec() == QDialog::Accepted) {
        QString path = dialog.selectedPath();
        if (!path.isEmpty()) {
            m_pathEdit->setText(path);
        }
    }
}

void AddDestinationDialog::validateInput()
{
    bool valid = !PathUtils::isPathEmpty(m_pathEdit->text());
    m_okBtn->setEnabled(valid);
}

} // namespace MegaCustom
