#ifndef MEGACUSTOM_MULTIUPLOADERPANEL_H
#define MEGACUSTOM_MULTIUPLOADERPANEL_H

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
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace MegaCustom {

class MultiUploaderController;
class FileController;

/**
 * Panel for multi-destination uploads with distribution rules
 * Placeholder implementation - full functionality coming in Phase 5
 */
class MultiUploaderPanel : public QWidget {
    Q_OBJECT

public:
    explicit MultiUploaderPanel(QWidget* parent = nullptr);
    ~MultiUploaderPanel();

    void setController(MultiUploaderController* controller);
    void setFileController(FileController* controller);

signals:
    // User actions
    void addFilesRequested();
    void addFolderRequested();
    void clearFilesRequested();
    void addDestinationRequested(const QString& remotePath);
    void removeDestinationRequested(const QString& remotePath);
    void addRuleRequested(const QString& ruleType, const QString& pattern, const QString& destination);
    void startUploadRequested();
    void pauseUploadRequested();
    void cancelUploadRequested();

private slots:
    void onAddFilesClicked();
    void onAddFolderClicked();
    void onClearFilesClicked();
    void onAddDestinationClicked();
    void onRemoveDestinationClicked();
    void onStartClicked();
    void onPauseClicked();
    void onCancelClicked();

private:
    void setupUI();
    void setupSourceSection(QVBoxLayout* mainLayout);
    void setupDestinationSection(QVBoxLayout* mainLayout);
    void setupRulesSection(QVBoxLayout* mainLayout);
    void setupTaskSection(QVBoxLayout* mainLayout);
    void updateButtonStates();

private:
    MultiUploaderController* m_controller = nullptr;
    FileController* m_fileController = nullptr;

    // Source section
    QListWidget* m_sourceList = nullptr;
    QPushButton* m_addFilesBtn = nullptr;
    QPushButton* m_addFolderBtn = nullptr;
    QPushButton* m_clearFilesBtn = nullptr;
    QLabel* m_sourceSummaryLabel = nullptr;

    // Destination section
    QListWidget* m_destinationList = nullptr;
    QPushButton* m_addDestBtn = nullptr;
    QPushButton* m_removeDestBtn = nullptr;

    // Rules section
    QComboBox* m_ruleTypeCombo = nullptr;
    QTableWidget* m_rulesTable = nullptr;
    QPushButton* m_addRuleBtn = nullptr;
    QPushButton* m_removeRuleBtn = nullptr;

    // Task section
    QTableWidget* m_taskTable = nullptr;
    QPushButton* m_startBtn = nullptr;
    QPushButton* m_pauseBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QPushButton* m_clearCompletedBtn = nullptr;

    // State
    bool m_isUploading = false;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_MULTIUPLOADERPANEL_H
