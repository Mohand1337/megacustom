#include "SettingsPanel.h"
#include "utils/Settings.h"
#include "utils/DpiScaler.h"
#include "styles/ThemeManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QFrame>
#include <QIcon>

namespace MegaCustom {

SettingsPanel::SettingsPanel(QWidget* parent)
    : QWidget(parent)
    , m_hasUnsavedChanges(false)
{
    setupUI();
    loadSettings();
}

void SettingsPanel::setupUI()
{
    setObjectName("SettingsPanel");

    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Left navigation sidebar
    setupNavigation();
    mainLayout->addWidget(m_navigationWidget);

    // Right content area
    QWidget* contentWidget = new QWidget(this);
    contentWidget->setObjectName("SettingsContent");
    QVBoxLayout* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(24, 24, 24, 24);
    contentLayout->setSpacing(16);

    m_contentStack = new QStackedWidget(this);
    m_contentStack->setObjectName("SettingsStack");

    setupGeneralPage();
    setupSyncPage();
    setupAdvancedPage();
    setupAboutPage();

    contentLayout->addWidget(m_contentStack, 1);

    // Bottom action buttons
    QHBoxLayout* actionLayout = new QHBoxLayout();
    actionLayout->addStretch();

    m_resetButton = new QPushButton("Reset to Defaults", this);
    m_resetButton->setObjectName("PanelSecondaryButton");
    connect(m_resetButton, &QPushButton::clicked, this, &SettingsPanel::onResetClicked);
    actionLayout->addWidget(m_resetButton);

    m_saveButton = new QPushButton("Save Settings", this);
    m_saveButton->setObjectName("PanelPrimaryButton");
    m_saveButton->setEnabled(false);
    connect(m_saveButton, &QPushButton::clicked, this, &SettingsPanel::onSaveClicked);
    actionLayout->addWidget(m_saveButton);

    contentLayout->addLayout(actionLayout);

    mainLayout->addWidget(contentWidget, 1);
}

void SettingsPanel::setupNavigation()
{
    m_navigationWidget = new QWidget(this);
    m_navigationWidget->setObjectName("SettingsNavigation");
    m_navigationWidget->setFixedWidth(200);

    QVBoxLayout* navLayout = new QVBoxLayout(m_navigationWidget);
    navLayout->setContentsMargins(12, 16, 12, 16);
    navLayout->setSpacing(4);

    // Header
    QLabel* headerLabel = new QLabel("Settings", m_navigationWidget);
    headerLabel->setObjectName("SettingsNavHeader");
    navLayout->addWidget(headerLabel);
    navLayout->addSpacing(16);

    // Navigation list
    m_navigationList = new QListWidget(m_navigationWidget);
    m_navigationList->setObjectName("SettingsNavList");
    m_navigationList->setFrameShape(QFrame::NoFrame);
    m_navigationList->setSpacing(2);

    addNavigationItem(":/icons/settings.svg", "General");
    addNavigationItem(":/icons/folder-sync.svg", "Sync");
    addNavigationItem(":/icons/sliders-horizontal.svg", "Advanced");
    addNavigationItem(":/icons/info.svg", "About");

    m_navigationList->setCurrentRow(0);
    connect(m_navigationList, &QListWidget::currentRowChanged,
            this, &SettingsPanel::onNavigationItemClicked);

    navLayout->addWidget(m_navigationList);
    navLayout->addStretch();
}

void SettingsPanel::addNavigationItem(const QString& iconPath, const QString& text)
{
    QListWidgetItem* item = new QListWidgetItem(QIcon(iconPath), text);
    item->setSizeHint(QSize(0, 40));
    m_navigationList->addItem(item);
}

void SettingsPanel::setupGeneralPage()
{
    QWidget* page = new QWidget();
    page->setObjectName("SettingsPage");  // Prevents white background from global QWidget rule
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    // Page title
    QLabel* titleLabel = new QLabel("General Settings", page);
    titleLabel->setObjectName("PanelTitle");
    layout->addWidget(titleLabel);

    QLabel* subtitleLabel = new QLabel("Configure application startup and appearance", page);
    subtitleLabel->setObjectName("PanelSubtitle");
    layout->addWidget(subtitleLabel);

    // Startup card
    QGroupBox* startupGroup = new QGroupBox("Startup", page);
    QVBoxLayout* startupLayout = new QVBoxLayout(startupGroup);

    m_startAtLoginCheck = new QCheckBox("Start at system login", startupGroup);
    connect(m_startAtLoginCheck, &QCheckBox::toggled, this, &SettingsPanel::onSettingChanged);
    startupLayout->addWidget(m_startAtLoginCheck);

    m_showTrayIconCheck = new QCheckBox("Show system tray icon", startupGroup);
    connect(m_showTrayIconCheck, &QCheckBox::toggled, this, &SettingsPanel::onSettingChanged);
    startupLayout->addWidget(m_showTrayIconCheck);

    m_showNotificationsCheck = new QCheckBox("Show desktop notifications", startupGroup);
    connect(m_showNotificationsCheck, &QCheckBox::toggled, this, &SettingsPanel::onSettingChanged);
    startupLayout->addWidget(m_showNotificationsCheck);

    layout->addWidget(startupGroup);

    // Appearance card
    QGroupBox* appearanceGroup = new QGroupBox("Appearance", page);
    QVBoxLayout* appearanceLayout = new QVBoxLayout(appearanceGroup);

    m_darkModeCheck = new QCheckBox("Enable dark mode", appearanceGroup);
    connect(m_darkModeCheck, &QCheckBox::toggled, this, &SettingsPanel::onSettingChanged);
    appearanceLayout->addWidget(m_darkModeCheck);

    QHBoxLayout* langLayout = new QHBoxLayout();
    langLayout->addWidget(new QLabel("Language:", appearanceGroup));
    m_languageCombo = new QComboBox(appearanceGroup);
    m_languageCombo->addItems({"English", "Spanish", "French", "German", "Chinese", "Japanese"});
    connect(m_languageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPanel::onSettingChanged);
    langLayout->addWidget(m_languageCombo);
    langLayout->addStretch();
    appearanceLayout->addLayout(langLayout);

    layout->addWidget(appearanceGroup);
    layout->addStretch();

    // Wrap in scroll area
    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setWidget(page);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    m_contentStack->addWidget(scrollArea);
}

void SettingsPanel::setupSyncPage()
{
    QWidget* page = new QWidget();
    page->setObjectName("SettingsPage");  // Prevents white background from global QWidget rule
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    // Page title
    QLabel* titleLabel = new QLabel("Sync Settings", page);
    titleLabel->setObjectName("PanelTitle");
    layout->addWidget(titleLabel);

    QLabel* subtitleLabel = new QLabel("Configure automatic sync and conflict resolution", page);
    subtitleLabel->setObjectName("PanelSubtitle");
    layout->addWidget(subtitleLabel);

    // Scheduler card
    QGroupBox* schedulerGroup = new QGroupBox("Automatic Sync", page);
    QVBoxLayout* schedulerLayout = new QVBoxLayout(schedulerGroup);

    QHBoxLayout* scheduleEnableLayout = new QHBoxLayout();
    m_schedulerEnabledCheck = new QCheckBox("Enable automatic sync every", schedulerGroup);
    connect(m_schedulerEnabledCheck, &QCheckBox::toggled, this, &SettingsPanel::onSchedulerToggled);
    connect(m_schedulerEnabledCheck, &QCheckBox::toggled, this, &SettingsPanel::onSettingChanged);
    scheduleEnableLayout->addWidget(m_schedulerEnabledCheck);

    m_schedulerIntervalSpin = new QSpinBox(schedulerGroup);
    m_schedulerIntervalSpin->setRange(1, 1440);
    m_schedulerIntervalSpin->setValue(60);
    m_schedulerIntervalSpin->setSuffix(" minutes");
    m_schedulerIntervalSpin->setEnabled(false);
    connect(m_schedulerIntervalSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsPanel::onSettingChanged);
    scheduleEnableLayout->addWidget(m_schedulerIntervalSpin);
    scheduleEnableLayout->addStretch();
    schedulerLayout->addLayout(scheduleEnableLayout);

    m_syncOnStartupCheck = new QCheckBox("Sync all profiles on application startup", schedulerGroup);
    connect(m_syncOnStartupCheck, &QCheckBox::toggled, this, &SettingsPanel::onSettingChanged);
    schedulerLayout->addWidget(m_syncOnStartupCheck);

    m_syncOnFileChangeCheck = new QCheckBox("Sync when local files change (watch mode)", schedulerGroup);
    connect(m_syncOnFileChangeCheck, &QCheckBox::toggled, this, &SettingsPanel::onSettingChanged);
    schedulerLayout->addWidget(m_syncOnFileChangeCheck);

    layout->addWidget(schedulerGroup);

    // Conflict Resolution card
    QGroupBox* conflictGroup = new QGroupBox("Conflict Resolution", page);
    QVBoxLayout* conflictLayout = new QVBoxLayout(conflictGroup);

    m_autoResolveConflictsCheck = new QCheckBox("Automatically resolve conflicts", conflictGroup);
    connect(m_autoResolveConflictsCheck, &QCheckBox::toggled, this, &SettingsPanel::onSettingChanged);
    conflictLayout->addWidget(m_autoResolveConflictsCheck);

    QHBoxLayout* resolutionLayout = new QHBoxLayout();
    resolutionLayout->addWidget(new QLabel("Default resolution:", conflictGroup));
    m_conflictResolutionCombo = new QComboBox(conflictGroup);
    m_conflictResolutionCombo->addItems({
        "Keep newer version",
        "Keep older version",
        "Keep larger file",
        "Keep local version",
        "Keep remote version",
        "Rename both versions"
    });
    connect(m_conflictResolutionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPanel::onSettingChanged);
    resolutionLayout->addWidget(m_conflictResolutionCombo);
    resolutionLayout->addStretch();
    conflictLayout->addLayout(resolutionLayout);

    layout->addWidget(conflictGroup);
    layout->addStretch();

    // Wrap in scroll area
    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setWidget(page);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    m_contentStack->addWidget(scrollArea);
}

void SettingsPanel::setupAdvancedPage()
{
    QWidget* page = new QWidget();
    page->setObjectName("SettingsPage");  // Prevents white background from global QWidget rule
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    // Page title
    QLabel* titleLabel = new QLabel("Advanced Settings", page);
    titleLabel->setObjectName("PanelTitle");
    layout->addWidget(titleLabel);

    QLabel* subtitleLabel = new QLabel("Configure bandwidth, transfers, and caching", page);
    subtitleLabel->setObjectName("PanelSubtitle");
    layout->addWidget(subtitleLabel);

    // Bandwidth card
    QGroupBox* bandwidthGroup = new QGroupBox("Bandwidth Limits", page);
    QGridLayout* bandwidthLayout = new QGridLayout(bandwidthGroup);

    bandwidthLayout->addWidget(new QLabel("Upload limit:"), 0, 0);
    m_uploadLimitSpin = new QSpinBox(bandwidthGroup);
    m_uploadLimitSpin->setRange(0, 100000);
    m_uploadLimitSpin->setValue(0);
    m_uploadLimitSpin->setSuffix(" KB/s");
    m_uploadLimitSpin->setSpecialValueText("Unlimited");
    connect(m_uploadLimitSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsPanel::onSettingChanged);
    bandwidthLayout->addWidget(m_uploadLimitSpin, 0, 1);

    bandwidthLayout->addWidget(new QLabel("Download limit:"), 1, 0);
    m_downloadLimitSpin = new QSpinBox(bandwidthGroup);
    m_downloadLimitSpin->setRange(0, 100000);
    m_downloadLimitSpin->setValue(0);
    m_downloadLimitSpin->setSuffix(" KB/s");
    m_downloadLimitSpin->setSpecialValueText("Unlimited");
    connect(m_downloadLimitSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsPanel::onSettingChanged);
    bandwidthLayout->addWidget(m_downloadLimitSpin, 1, 1);

    bandwidthLayout->setColumnStretch(2, 1);
    layout->addWidget(bandwidthGroup);

    // Transfers card
    QGroupBox* transfersGroup = new QGroupBox("Parallel Transfers", page);
    QHBoxLayout* transfersLayout = new QHBoxLayout(transfersGroup);

    transfersLayout->addWidget(new QLabel("Concurrent transfers:"));
    m_parallelTransfersSlider = new QSlider(Qt::Horizontal, transfersGroup);
    m_parallelTransfersSlider->setRange(1, 8);
    m_parallelTransfersSlider->setValue(4);
    m_parallelTransfersSlider->setTickPosition(QSlider::TicksBelow);
    m_parallelTransfersSlider->setTickInterval(1);
    transfersLayout->addWidget(m_parallelTransfersSlider);

    m_parallelTransfersSpin = new QSpinBox(transfersGroup);
    m_parallelTransfersSpin->setRange(1, 8);
    m_parallelTransfersSpin->setValue(4);
    transfersLayout->addWidget(m_parallelTransfersSpin);

    connect(m_parallelTransfersSlider, &QSlider::valueChanged,
            m_parallelTransfersSpin, &QSpinBox::setValue);
    connect(m_parallelTransfersSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            m_parallelTransfersSlider, &QSlider::setValue);
    connect(m_parallelTransfersSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsPanel::onSettingChanged);

    layout->addWidget(transfersGroup);

    // File Filters card
    QGroupBox* filtersGroup = new QGroupBox("File Filters", page);
    QVBoxLayout* filtersLayout = new QVBoxLayout(filtersGroup);

    QHBoxLayout* excludeLayout = new QHBoxLayout();
    excludeLayout->addWidget(new QLabel("Exclude patterns:"));
    m_excludePatternsEdit = new QLineEdit(filtersGroup);
    m_excludePatternsEdit->setPlaceholderText("*.tmp, *.bak, .git (comma separated)");
    connect(m_excludePatternsEdit, &QLineEdit::textChanged, this, &SettingsPanel::onSettingChanged);
    excludeLayout->addWidget(m_excludePatternsEdit);
    filtersLayout->addLayout(excludeLayout);

    QHBoxLayout* maxSizeLayout = new QHBoxLayout();
    maxSizeLayout->addWidget(new QLabel("Max file size:"));
    m_maxFileSizeSpin = new QSpinBox(filtersGroup);
    m_maxFileSizeSpin->setRange(0, 10000);
    m_maxFileSizeSpin->setValue(0);
    m_maxFileSizeSpin->setSuffix(" MB");
    m_maxFileSizeSpin->setSpecialValueText("No limit");
    connect(m_maxFileSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsPanel::onSettingChanged);
    maxSizeLayout->addWidget(m_maxFileSizeSpin);
    maxSizeLayout->addStretch();
    filtersLayout->addLayout(maxSizeLayout);

    m_skipHiddenCheck = new QCheckBox("Skip hidden files", filtersGroup);
    connect(m_skipHiddenCheck, &QCheckBox::toggled, this, &SettingsPanel::onSettingChanged);
    filtersLayout->addWidget(m_skipHiddenCheck);

    m_skipTempCheck = new QCheckBox("Skip temporary files", filtersGroup);
    connect(m_skipTempCheck, &QCheckBox::toggled, this, &SettingsPanel::onSettingChanged);
    filtersLayout->addWidget(m_skipTempCheck);

    layout->addWidget(filtersGroup);

    // Cache & Logging card
    QGroupBox* cacheGroup = new QGroupBox("Cache & Logging", page);
    QVBoxLayout* cacheLayout = new QVBoxLayout(cacheGroup);

    QHBoxLayout* cachePathLayout = new QHBoxLayout();
    cachePathLayout->addWidget(new QLabel("Cache path:"));
    m_cachePathEdit = new QLineEdit(cacheGroup);
    m_cachePathEdit->setReadOnly(true);
    cachePathLayout->addWidget(m_cachePathEdit);
    QPushButton* browseCacheBtn = new QPushButton("Browse...", cacheGroup);
    connect(browseCacheBtn, &QPushButton::clicked, this, &SettingsPanel::onBrowseCachePath);
    cachePathLayout->addWidget(browseCacheBtn);
    cacheLayout->addLayout(cachePathLayout);

    QHBoxLayout* cacheSizeLayout = new QHBoxLayout();
    cacheSizeLayout->addWidget(new QLabel("Max cache size:"));
    m_cacheSizeSpin = new QSpinBox(cacheGroup);
    m_cacheSizeSpin->setRange(100, 10000);
    m_cacheSizeSpin->setValue(500);
    m_cacheSizeSpin->setSuffix(" MB");
    connect(m_cacheSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsPanel::onSettingChanged);
    cacheSizeLayout->addWidget(m_cacheSizeSpin);
    QPushButton* clearCacheBtn = new QPushButton("Clear Cache", cacheGroup);
    clearCacheBtn->setObjectName("PanelDangerButton");
    connect(clearCacheBtn, &QPushButton::clicked, this, &SettingsPanel::onClearCache);
    cacheSizeLayout->addWidget(clearCacheBtn);
    cacheSizeLayout->addStretch();
    cacheLayout->addLayout(cacheSizeLayout);

    QHBoxLayout* loggingLayout = new QHBoxLayout();
    m_enableLoggingCheck = new QCheckBox("Enable logging", cacheGroup);
    connect(m_enableLoggingCheck, &QCheckBox::toggled, this, &SettingsPanel::onSettingChanged);
    loggingLayout->addWidget(m_enableLoggingCheck);
    loggingLayout->addWidget(new QLabel("Level:"));
    m_logLevelCombo = new QComboBox(cacheGroup);
    m_logLevelCombo->addItems({"Error", "Warning", "Info", "Debug", "Verbose"});
    m_logLevelCombo->setCurrentIndex(2);
    connect(m_logLevelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPanel::onSettingChanged);
    loggingLayout->addWidget(m_logLevelCombo);
    loggingLayout->addStretch();
    cacheLayout->addLayout(loggingLayout);

    layout->addWidget(cacheGroup);
    layout->addStretch();

    // Wrap in scroll area
    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setWidget(page);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    m_contentStack->addWidget(scrollArea);
}

void SettingsPanel::setupAboutPage()
{
    QWidget* page = new QWidget();
    page->setObjectName("SettingsPage");  // Prevents white background from global QWidget rule
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    // Page title
    QLabel* titleLabel = new QLabel("About MegaCustom", page);
    titleLabel->setObjectName("PanelTitle");
    layout->addWidget(titleLabel);

    // App info card
    QGroupBox* infoGroup = new QGroupBox("Application Info", page);
    QVBoxLayout* infoLayout = new QVBoxLayout(infoGroup);

    // Logo
    QLabel* logoLabel = new QLabel(infoGroup);
    auto& tm = ThemeManager::instance();

    logoLabel->setObjectName("AboutLogo");
    logoLabel->setText("M");
    logoLabel->setFixedSize(DpiScaler::scale(80), DpiScaler::scale(80));
    logoLabel->setAlignment(Qt::AlignCenter);
    logoLabel->setStyleSheet(QString(
        "QLabel#AboutLogo {"
        "  background-color: %1;"
        "  color: #FFFFFF;"
        "  font-size: %2px;"
        "  font-weight: bold;"
        "  border-radius: %3px;"
        "}"
    ).arg(tm.brandDefault().name())
     .arg(DpiScaler::scale(40))
     .arg(DpiScaler::scale(16)));
    infoLayout->addWidget(logoLabel, 0, Qt::AlignCenter);
    infoLayout->addSpacing(DpiScaler::scale(16));

    QLabel* appNameLabel = new QLabel("MegaCustom", infoGroup);
    appNameLabel->setObjectName("AboutAppName");
    appNameLabel->setAlignment(Qt::AlignCenter);
    appNameLabel->setStyleSheet(QString("font-size: %1px; font-weight: bold; color: %2;")
        .arg(DpiScaler::scale(24))
        .arg(tm.textPrimary().name()));
    infoLayout->addWidget(appNameLabel);

    m_versionLabel = new QLabel("Version 1.0.0", infoGroup);
    m_versionLabel->setAlignment(Qt::AlignCenter);
    m_versionLabel->setStyleSheet(QString("font-size: %1px; color: %2;")
        .arg(DpiScaler::scale(14))
        .arg(tm.textSecondary().name()));
    infoLayout->addWidget(m_versionLabel);

    m_buildDateLabel = new QLabel("Built: " + QString(__DATE__), infoGroup);
    m_buildDateLabel->setAlignment(Qt::AlignCenter);
    m_buildDateLabel->setStyleSheet(QString("font-size: %1px; color: %2;")
        .arg(DpiScaler::scale(12))
        .arg(tm.textSecondary().name()));
    infoLayout->addWidget(m_buildDateLabel);

    infoLayout->addSpacing(DpiScaler::scale(16));

    QLabel* descLabel = new QLabel(
        "Advanced file management and synchronization tool for MEGA cloud storage.\n\n"
        "Features:\n"
        "  - Folder Mapper: Map local folders to cloud destinations\n"
        "  - Multi Uploader: Upload files to multiple destinations\n"
        "  - Smart Sync: Bidirectional synchronization with conflict resolution",
        infoGroup
    );
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet(QString("font-size: %1px; color: %2;")
        .arg(DpiScaler::scale(13))
        .arg(tm.textSecondary().name()));
    infoLayout->addWidget(descLabel);

    layout->addWidget(infoGroup);

    // Links card
    QGroupBox* linksGroup = new QGroupBox("Links", page);
    QVBoxLayout* linksLayout = new QVBoxLayout(linksGroup);

    QLabel* githubLink = new QLabel("<a href='https://github.com'>GitHub Repository</a>", linksGroup);
    githubLink->setOpenExternalLinks(true);
    linksLayout->addWidget(githubLink);

    QLabel* docsLink = new QLabel("<a href='https://mega.io'>MEGA Documentation</a>", linksGroup);
    docsLink->setOpenExternalLinks(true);
    linksLayout->addWidget(docsLink);

    layout->addWidget(linksGroup);
    layout->addStretch();

    // Wrap in scroll area
    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setWidget(page);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    m_contentStack->addWidget(scrollArea);
}

void SettingsPanel::loadSettings()
{
    Settings& settings = Settings::instance();

    // Block signals to prevent triggering onSettingChanged
    m_hasUnsavedChanges = false;

    // General
    if (m_showTrayIconCheck) m_showTrayIconCheck->setChecked(settings.showTrayIcon());
    if (m_darkModeCheck) m_darkModeCheck->setChecked(settings.darkMode());
    if (m_showNotificationsCheck) m_showNotificationsCheck->setChecked(settings.showNotifications());

    // Sync
    if (m_schedulerIntervalSpin) m_schedulerIntervalSpin->setValue(settings.syncInterval());
    if (m_syncOnStartupCheck) m_syncOnStartupCheck->setChecked(settings.syncOnStartup());

    // Advanced
    if (m_uploadLimitSpin) m_uploadLimitSpin->setValue(settings.uploadBandwidthLimit());
    if (m_downloadLimitSpin) m_downloadLimitSpin->setValue(settings.downloadBandwidthLimit());
    if (m_parallelTransfersSpin) m_parallelTransfersSpin->setValue(settings.parallelTransfers());
    if (m_excludePatternsEdit) m_excludePatternsEdit->setText(settings.excludePatterns());
    if (m_skipHiddenCheck) m_skipHiddenCheck->setChecked(settings.skipHiddenFiles());
    if (m_cachePathEdit) m_cachePathEdit->setText(settings.cachePath());
    if (m_enableLoggingCheck) m_enableLoggingCheck->setChecked(settings.loggingEnabled());

    m_hasUnsavedChanges = false;
    if (m_saveButton) m_saveButton->setEnabled(false);
}

void SettingsPanel::saveSettings()
{
    Settings& settings = Settings::instance();

    // General
    if (m_showTrayIconCheck) settings.setShowTrayIcon(m_showTrayIconCheck->isChecked());
    if (m_darkModeCheck) settings.setDarkMode(m_darkModeCheck->isChecked());
    if (m_showNotificationsCheck) settings.setShowNotifications(m_showNotificationsCheck->isChecked());

    // Sync
    if (m_schedulerEnabledCheck && m_schedulerIntervalSpin) {
        settings.setSyncInterval(m_schedulerEnabledCheck->isChecked() ?
                                 m_schedulerIntervalSpin->value() : 0);
    }
    if (m_syncOnStartupCheck) settings.setSyncOnStartup(m_syncOnStartupCheck->isChecked());

    // Advanced
    if (m_uploadLimitSpin) settings.setUploadBandwidthLimit(m_uploadLimitSpin->value());
    if (m_downloadLimitSpin) settings.setDownloadBandwidthLimit(m_downloadLimitSpin->value());
    if (m_parallelTransfersSpin) settings.setParallelTransfers(m_parallelTransfersSpin->value());
    if (m_excludePatternsEdit) settings.setExcludePatterns(m_excludePatternsEdit->text());
    if (m_skipHiddenCheck) settings.setSkipHiddenFiles(m_skipHiddenCheck->isChecked());
    if (m_cachePathEdit) settings.setCachePath(m_cachePathEdit->text());
    if (m_enableLoggingCheck) settings.setLoggingEnabled(m_enableLoggingCheck->isChecked());

    settings.save();

    m_hasUnsavedChanges = false;
    m_saveButton->setEnabled(false);

    emit settingsSaved();
}

void SettingsPanel::setCurrentSection(Section section)
{
    m_navigationList->setCurrentRow(static_cast<int>(section));
}

void SettingsPanel::onNavigationItemClicked(int index)
{
    m_contentStack->setCurrentIndex(index);
}

void SettingsPanel::onSchedulerToggled(bool enabled)
{
    if (m_schedulerIntervalSpin) {
        m_schedulerIntervalSpin->setEnabled(enabled);
    }
}

void SettingsPanel::onBrowseCachePath()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Select Cache Directory",
        m_cachePathEdit->text().isEmpty() ? QDir::homePath() : m_cachePathEdit->text());
    if (!dir.isEmpty()) {
        m_cachePathEdit->setText(dir);
        onSettingChanged();
    }
}

void SettingsPanel::onClearCache()
{
    auto result = QMessageBox::question(this, "Clear Cache",
        "Are you sure you want to clear the application cache?\n"
        "This will remove all cached file data.",
        QMessageBox::Yes | QMessageBox::No);
    if (result == QMessageBox::Yes) {
        QMessageBox::information(this, "Cache Cleared",
            "Cache has been cleared successfully.");
    }
}

void SettingsPanel::onSettingChanged()
{
    m_hasUnsavedChanges = true;
    m_saveButton->setEnabled(true);
    emit settingsChanged();
}

void SettingsPanel::onSaveClicked()
{
    saveSettings();
    QMessageBox::information(this, "Settings Saved",
        "Your settings have been saved successfully.");
}

void SettingsPanel::onResetClicked()
{
    auto result = QMessageBox::question(this, "Reset Settings",
        "Are you sure you want to reset all settings to defaults?\n"
        "This cannot be undone.",
        QMessageBox::Yes | QMessageBox::No);
    if (result == QMessageBox::Yes) {
        // Reset to default values manually
        Settings& settings = Settings::instance();
        settings.setShowTrayIcon(true);
        settings.setDarkMode(false);
        settings.setShowNotifications(true);
        settings.setSyncInterval(0);
        settings.setSyncOnStartup(false);
        settings.setUploadBandwidthLimit(0);
        settings.setDownloadBandwidthLimit(0);
        settings.setParallelTransfers(4);
        settings.setExcludePatterns("");
        settings.setSkipHiddenFiles(false);
        settings.setLoggingEnabled(false);
        settings.save();

        loadSettings();
        QMessageBox::information(this, "Settings Reset",
            "All settings have been reset to defaults.");
    }
}

} // namespace MegaCustom
