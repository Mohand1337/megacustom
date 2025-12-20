#include "SyncProfileDialog.h"
#include "RemoteFolderBrowserDialog.h"
#include "widgets/ButtonFactory.h"
#include "utils/PathUtils.h"
#include "utils/DpiScaler.h"
#include "styles/ThemeManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QTabWidget>
#include <QGroupBox>
#include <QFileDialog>

namespace MegaCustom {

SyncProfileDialog::SyncProfileDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Sync Profile");
    setMinimumSize(DpiScaler::scale(500), DpiScaler::scale(400));
    setupUI();
}

void SyncProfileDialog::setFileController(FileController* controller)
{
    m_fileController = controller;
}

void SyncProfileDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);

    // Tab widget
    auto* tabWidget = new QTabWidget(this);

    // Basic tab
    auto* basicTab = new QWidget(this);
    setupBasicTab(basicTab);
    tabWidget->addTab(basicTab, "Basic");

    // Filters tab
    auto* filtersTab = new QWidget(this);
    setupFiltersTab(filtersTab);
    tabWidget->addTab(filtersTab, "Filters");

    // Schedule tab
    auto* scheduleTab = new QWidget(this);
    setupScheduleTab(scheduleTab);
    tabWidget->addTab(scheduleTab, "Schedule");

    mainLayout->addWidget(tabWidget);

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

    validateInput();
}

void SyncProfileDialog::setupBasicTab(QWidget* tab)
{
    auto* layout = new QVBoxLayout(tab);
    auto* formLayout = new QFormLayout();

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText("Enter profile name");
    formLayout->addRow("Name:", m_nameEdit);

    // Local path
    auto* localLayout = new QHBoxLayout();
    m_localPathEdit = new QLineEdit(this);
    m_localPathEdit->setPlaceholderText("/path/to/local/folder");
    auto* browseLocalBtn = ButtonFactory::createSecondary("Browse...", this);
    localLayout->addWidget(m_localPathEdit, 1);
    localLayout->addWidget(browseLocalBtn);
    formLayout->addRow("Local Path:", localLayout);
    connect(browseLocalBtn, &QPushButton::clicked,
            this, &SyncProfileDialog::onBrowseLocalClicked);

    // Remote path
    auto* remoteLayout = new QHBoxLayout();
    m_remotePathEdit = new QLineEdit(this);
    m_remotePathEdit->setPlaceholderText("/Cloud/folder");
    auto* browseRemoteBtn = ButtonFactory::createSecondary("Select...", this);
    remoteLayout->addWidget(m_remotePathEdit, 1);
    remoteLayout->addWidget(browseRemoteBtn);
    formLayout->addRow("Remote Path:", remoteLayout);
    connect(browseRemoteBtn, &QPushButton::clicked,
            this, &SyncProfileDialog::onBrowseRemoteClicked);

    // Direction
    m_directionCombo = new QComboBox(this);
    m_directionCombo->addItem("Bidirectional", static_cast<int>(SyncDirection::BIDIRECTIONAL));
    m_directionCombo->addItem("Local to Remote (Upload only)", static_cast<int>(SyncDirection::LOCAL_TO_REMOTE));
    m_directionCombo->addItem("Remote to Local (Download only)", static_cast<int>(SyncDirection::REMOTE_TO_LOCAL));
    formLayout->addRow("Direction:", m_directionCombo);

    // Conflict resolution
    m_conflictCombo = new QComboBox(this);
    m_conflictCombo->addItem("Ask User", static_cast<int>(ConflictResolution::ASK_USER));
    m_conflictCombo->addItem("Keep Newer", static_cast<int>(ConflictResolution::KEEP_NEWER));
    m_conflictCombo->addItem("Keep Larger", static_cast<int>(ConflictResolution::KEEP_LARGER));
    m_conflictCombo->addItem("Keep Local", static_cast<int>(ConflictResolution::KEEP_LOCAL));
    m_conflictCombo->addItem("Keep Remote", static_cast<int>(ConflictResolution::KEEP_REMOTE));
    m_conflictCombo->addItem("Keep Both", static_cast<int>(ConflictResolution::KEEP_BOTH));
    formLayout->addRow("Conflict Resolution:", m_conflictCombo);

    layout->addLayout(formLayout);
    layout->addStretch();

    // Validation connections
    connect(m_nameEdit, &QLineEdit::textChanged, this, &SyncProfileDialog::validateInput);
    connect(m_localPathEdit, &QLineEdit::textChanged, this, &SyncProfileDialog::validateInput);
    connect(m_remotePathEdit, &QLineEdit::textChanged, this, &SyncProfileDialog::validateInput);
}

void SyncProfileDialog::setupFiltersTab(QWidget* tab)
{
    auto* layout = new QVBoxLayout(tab);
    auto* formLayout = new QFormLayout();

    m_includeEdit = new QLineEdit(this);
    m_includeEdit->setPlaceholderText("*.doc, *.pdf (comma separated, empty = all)");
    formLayout->addRow("Include Patterns:", m_includeEdit);

    m_excludeEdit = new QLineEdit(this);
    m_excludeEdit->setPlaceholderText("*.tmp, .git, node_modules (comma separated)");
    formLayout->addRow("Exclude Patterns:", m_excludeEdit);

    layout->addLayout(formLayout);

    // Options group
    auto* optionsGroup = new QGroupBox("Options", this);
    auto* optionsLayout = new QVBoxLayout(optionsGroup);

    m_syncHiddenCheck = new QCheckBox("Sync hidden files", this);
    optionsLayout->addWidget(m_syncHiddenCheck);

    m_deleteOrphansCheck = new QCheckBox("Delete orphan files (files not in source)", this);
    optionsLayout->addWidget(m_deleteOrphansCheck);

    m_verifyCheck = new QCheckBox("Verify file integrity after transfer", this);
    m_verifyCheck->setChecked(true);
    optionsLayout->addWidget(m_verifyCheck);

    layout->addWidget(optionsGroup);
    layout->addStretch();
}

void SyncProfileDialog::setupScheduleTab(QWidget* tab)
{
    auto* layout = new QVBoxLayout(tab);

    auto* scheduleGroup = new QGroupBox("Automatic Sync", this);
    auto* scheduleLayout = new QVBoxLayout(scheduleGroup);

    m_autoSyncCheck = new QCheckBox("Enable automatic sync", this);
    scheduleLayout->addWidget(m_autoSyncCheck);

    auto* intervalLayout = new QHBoxLayout();
    intervalLayout->addWidget(new QLabel("Sync every:", this));
    m_intervalSpin = new QSpinBox(this);
    m_intervalSpin->setRange(5, 1440);
    m_intervalSpin->setValue(60);
    m_intervalSpin->setSuffix(" minutes");
    m_intervalSpin->setEnabled(false);
    intervalLayout->addWidget(m_intervalSpin);
    intervalLayout->addStretch();
    scheduleLayout->addLayout(intervalLayout);

    connect(m_autoSyncCheck, &QCheckBox::toggled,
            m_intervalSpin, &QSpinBox::setEnabled);

    layout->addWidget(scheduleGroup);

    auto* noteLabel = new QLabel(
        "Note: Automatic sync will run in the background when you are logged in. "
        "You can also schedule specific sync times from the Scheduler.", this);
    noteLabel->setWordWrap(true);
    noteLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
        .arg(ThemeManager::instance().textSecondary().name())
        .arg(DpiScaler::scale(11)));
    layout->addWidget(noteLabel);

    layout->addStretch();
}

void SyncProfileDialog::setProfileData(const QString& name, const QString& localPath,
                                        const QString& remotePath, SyncDirection direction,
                                        ConflictResolution resolution)
{
    m_nameEdit->setText(name);
    m_localPathEdit->setText(localPath);
    m_remotePathEdit->setText(remotePath);

    int dirIndex = m_directionCombo->findData(static_cast<int>(direction));
    if (dirIndex >= 0) m_directionCombo->setCurrentIndex(dirIndex);

    int resIndex = m_conflictCombo->findData(static_cast<int>(resolution));
    if (resIndex >= 0) m_conflictCombo->setCurrentIndex(resIndex);
}

QString SyncProfileDialog::profileName() const
{
    return m_nameEdit->text().trimmed();
}

QString SyncProfileDialog::localPath() const
{
    return PathUtils::normalizeLocalPath(m_localPathEdit->text());
}

QString SyncProfileDialog::remotePath() const
{
    return PathUtils::normalizeRemotePath(m_remotePathEdit->text());
}

SyncProfileDialog::SyncDirection SyncProfileDialog::direction() const
{
    return static_cast<SyncDirection>(m_directionCombo->currentData().toInt());
}

SyncProfileDialog::ConflictResolution SyncProfileDialog::conflictResolution() const
{
    return static_cast<ConflictResolution>(m_conflictCombo->currentData().toInt());
}

QString SyncProfileDialog::includePatterns() const
{
    return m_includeEdit->text().trimmed();
}

QString SyncProfileDialog::excludePatterns() const
{
    return m_excludeEdit->text().trimmed();
}

bool SyncProfileDialog::syncHiddenFiles() const
{
    return m_syncHiddenCheck->isChecked();
}

bool SyncProfileDialog::deleteOrphans() const
{
    return m_deleteOrphansCheck->isChecked();
}

bool SyncProfileDialog::autoSyncEnabled() const
{
    return m_autoSyncCheck->isChecked();
}

int SyncProfileDialog::autoSyncIntervalMinutes() const
{
    return m_intervalSpin->value();
}

void SyncProfileDialog::onBrowseLocalClicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Select Local Folder",
                                                    m_localPathEdit->text());
    if (!dir.isEmpty()) {
        m_localPathEdit->setText(dir);
    }
}

void SyncProfileDialog::onBrowseRemoteClicked()
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

void SyncProfileDialog::validateInput()
{
    bool valid = !m_nameEdit->text().trimmed().isEmpty() &&
                 !PathUtils::normalizeLocalPath(m_localPathEdit->text()).isEmpty() &&
                 !PathUtils::isPathEmpty(m_remotePathEdit->text());
    m_okBtn->setEnabled(valid);
}

} // namespace MegaCustom
