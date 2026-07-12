#ifndef MEGACUSTOM_SETTINGSPANEL_H
#define MEGACUSTOM_SETTINGSPANEL_H

#include <QWidget>
#include <QStackedWidget>
#include <QListWidget>
#include <QCheckBox>
#include <QSpinBox>
#include <QSlider>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QGroupBox>
#include <QLabel>

namespace MegaCustom {

/**
 * Settings panel with sidebar navigation
 * Replaces modal SettingsDialog with in-app panel
 */
class SettingsPanel : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPanel(QWidget* parent = nullptr);

    enum Section {
        SECTION_GENERAL = 0,
        SECTION_SYNC,
        SECTION_ADVANCED,
        SECTION_ABOUT
    };

public slots:
    void loadSettings();
    void saveSettings();
    void setCurrentSection(Section section);

signals:
    void settingsSaved();
    void settingsChanged();

private slots:
    void onNavigationItemClicked(int index);
    void onSchedulerToggled(bool enabled);
    void onBrowseCachePath();
    void onClearCache();
    void onSettingChanged();
    void onSaveClicked();
    void onResetClicked();

private:
    void setupUI();
    void setupNavigation();
    void setupGeneralPage();
    void setupSyncPage();
    void setupAdvancedPage();
    void setupAboutPage();
    void addNavigationItem(const QString& icon, const QString& text);
    QWidget* createCard(const QString& title, QWidget* content);

private:
    // Layout
    QWidget* m_navigationWidget = nullptr;
    QListWidget* m_navigationList = nullptr;
    QStackedWidget* m_contentStack = nullptr;
    QPushButton* m_saveButton = nullptr;
    QPushButton* m_resetButton = nullptr;

    // General settings
    QCheckBox* m_startAtLoginCheck = nullptr;
    QCheckBox* m_showTrayIconCheck = nullptr;
    QCheckBox* m_darkModeCheck = nullptr;
    QCheckBox* m_showNotificationsCheck = nullptr;
    QComboBox* m_languageCombo = nullptr;

    // Sync settings
    QCheckBox* m_schedulerEnabledCheck = nullptr;
    QSpinBox* m_schedulerIntervalSpin = nullptr;
    QCheckBox* m_syncOnStartupCheck = nullptr;
    QCheckBox* m_syncOnFileChangeCheck = nullptr;
    QCheckBox* m_autoResolveConflictsCheck = nullptr;
    QComboBox* m_conflictResolutionCombo = nullptr;

    // Advanced settings
    QSpinBox* m_uploadLimitSpin = nullptr;
    QSpinBox* m_downloadLimitSpin = nullptr;
    QSlider* m_parallelTransfersSlider = nullptr;
    QSpinBox* m_parallelTransfersSpin = nullptr;
    QLineEdit* m_excludePatternsEdit = nullptr;
    QSpinBox* m_maxFileSizeSpin = nullptr;
    QCheckBox* m_skipHiddenCheck = nullptr;
    QCheckBox* m_skipTempCheck = nullptr;
    QLineEdit* m_cachePathEdit = nullptr;
    QSpinBox* m_cacheSizeSpin = nullptr;
    QCheckBox* m_enableLoggingCheck = nullptr;
    QComboBox* m_logLevelCombo = nullptr;

    // About page
    QLabel* m_versionLabel = nullptr;
    QLabel* m_buildDateLabel = nullptr;

    // State
    bool m_hasUnsavedChanges;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_SETTINGSPANEL_H
