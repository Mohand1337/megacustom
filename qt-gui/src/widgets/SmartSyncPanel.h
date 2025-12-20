#ifndef MEGACUSTOM_SMARTSYNCPANEL_H
#define MEGACUSTOM_SMARTSYNCPANEL_H

#include <QWidget>
#include <QTableWidget>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QComboBox>
#include <QGroupBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace MegaCustom {

class SmartSyncController;
class FileController;

/**
 * Panel for bidirectional sync with conflict resolution
 * Placeholder implementation - full functionality coming in Phase 6
 */
class SmartSyncPanel : public QWidget {
    Q_OBJECT

public:
    explicit SmartSyncPanel(QWidget* parent = nullptr);
    ~SmartSyncPanel();

    void setController(SmartSyncController* controller);
    void setFileController(FileController* controller);

signals:
    // User actions
    void createProfileRequested(const QString& name, const QString& localPath,
                                const QString& remotePath);
    void editProfileRequested(const QString& profileId);
    void deleteProfileRequested(const QString& profileId);
    void analyzeRequested(const QString& profileId);
    void startSyncRequested(const QString& profileId);
    void pauseSyncRequested(const QString& profileId);
    void stopSyncRequested(const QString& profileId);
    void scheduleRequested(const QString& profileId);
    void resolveConflictRequested(const QString& conflictId, const QString& resolution);

private slots:
    void onNewProfileClicked();
    void onEditProfileClicked();
    void onDeleteProfileClicked();
    void onAnalyzeClicked();
    void onStartSyncClicked();
    void onPauseSyncClicked();
    void onStopSyncClicked();
    void onScheduleClicked();
    void onProfileSelectionChanged();

private:
    void setupUI();
    void setupProfileSection(QVBoxLayout* mainLayout);
    void setupConfigSection(QVBoxLayout* mainLayout);
    void setupActionSection(QVBoxLayout* mainLayout);
    void setupDetailTabs(QVBoxLayout* mainLayout);
    void updateButtonStates();
    QWidget* createActionBadge(const QString& action);

private:
    SmartSyncController* m_controller = nullptr;

    // Profile section
    QTableWidget* m_profileTable = nullptr;
    QPushButton* m_newProfileBtn = nullptr;
    QPushButton* m_editProfileBtn = nullptr;
    QPushButton* m_deleteProfileBtn = nullptr;
    QPushButton* m_importBtn = nullptr;
    QPushButton* m_exportBtn = nullptr;

    // Config section
    QComboBox* m_directionCombo = nullptr;
    QComboBox* m_conflictCombo = nullptr;
    QLineEdit* m_includePatternEdit = nullptr;
    QLineEdit* m_excludePatternEdit = nullptr;
    QCheckBox* m_syncHiddenCheck = nullptr;
    QCheckBox* m_syncTempCheck = nullptr;
    QCheckBox* m_autoSyncCheck = nullptr;
    QSpinBox* m_autoSyncIntervalSpin = nullptr;
    QCheckBox* m_deleteOrphansCheck = nullptr;
    QCheckBox* m_verifyCheck = nullptr;

    // Action section
    QPushButton* m_analyzeBtn = nullptr;
    QPushButton* m_startSyncBtn = nullptr;
    QPushButton* m_pauseSyncBtn = nullptr;
    QPushButton* m_stopSyncBtn = nullptr;
    QPushButton* m_scheduleBtn = nullptr;

    // Detail tabs
    QTabWidget* m_detailTabs = nullptr;
    QTableWidget* m_previewTable = nullptr;
    QTableWidget* m_conflictsTable = nullptr;
    QWidget* m_progressWidget = nullptr;
    QProgressBar* m_syncProgressBar = nullptr;
    QLabel* m_syncStatusLabel = nullptr;
    QTableWidget* m_historyTable = nullptr;

    // State
    bool m_isSyncing = false;
    QString m_currentProfileId;

    FileController* m_fileController = nullptr;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_SMARTSYNCPANEL_H
