#ifndef SYNC_PROFILE_DIALOG_H
#define SYNC_PROFILE_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QPushButton>

namespace MegaCustom {

class FileController;

/**
 * Dialog for creating/editing sync profiles in SmartSync
 */
class SyncProfileDialog : public QDialog {
    Q_OBJECT

public:
    enum class SyncDirection {
        BIDIRECTIONAL,
        LOCAL_TO_REMOTE,
        REMOTE_TO_LOCAL
    };

    enum class ConflictResolution {
        ASK_USER,
        KEEP_NEWER,
        KEEP_LARGER,
        KEEP_LOCAL,
        KEEP_REMOTE,
        KEEP_BOTH
    };

    explicit SyncProfileDialog(QWidget* parent = nullptr);

    /**
     * Set file controller for remote folder browsing
     */
    void setFileController(FileController* controller);

    // For editing existing profile
    void setProfileData(const QString& name, const QString& localPath,
                        const QString& remotePath, SyncDirection direction,
                        ConflictResolution resolution);

    // Get data
    QString profileName() const;
    QString localPath() const;
    QString remotePath() const;
    SyncDirection direction() const;
    ConflictResolution conflictResolution() const;
    QString includePatterns() const;
    QString excludePatterns() const;
    bool syncHiddenFiles() const;
    bool deleteOrphans() const;
    bool autoSyncEnabled() const;
    int autoSyncIntervalMinutes() const;

private slots:
    void onBrowseLocalClicked();
    void onBrowseRemoteClicked();
    void validateInput();

private:
    void setupUI();
    void setupBasicTab(QWidget* tab);
    void setupFiltersTab(QWidget* tab);
    void setupScheduleTab(QWidget* tab);

private:
    // Basic tab
    QLineEdit* m_nameEdit;
    QLineEdit* m_localPathEdit;
    QLineEdit* m_remotePathEdit;
    QComboBox* m_directionCombo;
    QComboBox* m_conflictCombo;

    // Filters tab
    QLineEdit* m_includeEdit;
    QLineEdit* m_excludeEdit;
    QCheckBox* m_syncHiddenCheck;
    QCheckBox* m_deleteOrphansCheck;
    QCheckBox* m_verifyCheck;

    // Schedule tab
    QCheckBox* m_autoSyncCheck;
    QSpinBox* m_intervalSpin;

    QPushButton* m_okBtn;
    QPushButton* m_cancelBtn;

    FileController* m_fileController = nullptr;
};

} // namespace MegaCustom

#endif // SYNC_PROFILE_DIALOG_H
