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
    QWidget* m_navigationWidget;
    QListWidget* m_navigationList;
    QStackedWidget* m_contentStack;
    QPushButton* m_saveButton;
    QPushButton* m_resetButton;

    // General settings
    QCheckBox* m_startAtLoginCheck;
    QCheckBox* m_showTrayIconCheck;
    QCheckBox* m_darkModeCheck;
    QCheckBox* m_showNotificationsCheck;
    QComboBox* m_languageCombo;

    // Sync settings
    QCheckBox* m_schedulerEnabledCheck;
    QSpinBox* m_schedulerIntervalSpin;
    QCheckBox* m_syncOnStartupCheck;
    QCheckBox* m_syncOnFileChangeCheck;
    QCheckBox* m_autoResolveConflictsCheck;
    QComboBox* m_conflictResolutionCombo;

    // Advanced settings
    QSpinBox* m_uploadLimitSpin;
    QSpinBox* m_downloadLimitSpin;
    QSlider* m_parallelTransfersSlider;
    QSpinBox* m_parallelTransfersSpin;
    QLineEdit* m_excludePatternsEdit;
    QSpinBox* m_maxFileSizeSpin;
    QCheckBox* m_skipHiddenCheck;
    QCheckBox* m_skipTempCheck;
    QLineEdit* m_cachePathEdit;
    QSpinBox* m_cacheSizeSpin;
    QCheckBox* m_enableLoggingCheck;
    QComboBox* m_logLevelCombo;

    // About page
    QLabel* m_versionLabel;
    QLabel* m_buildDateLabel;

    // State
    bool m_hasUnsavedChanges;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_SETTINGSPANEL_H
