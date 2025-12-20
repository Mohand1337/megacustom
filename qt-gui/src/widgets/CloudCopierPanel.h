#ifndef MEGACUSTOM_CLOUDCOPIERPANEL_H
#define MEGACUSTOM_CLOUDCOPIERPANEL_H

#include <QWidget>
#include <QTableWidget>
#include <QListWidget>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialog>
#include <QTextEdit>
#include <QLineEdit>
#include <QRadioButton>
#include <QButtonGroup>
#include <QShortcut>

namespace MegaCustom {

struct CopyPreviewItem;
struct PathValidationResult;
struct MemberInfo;
struct TemplateExpansionPreview;
class CloudCopierController;
class FileController;

/**
 * Panel for cloud-to-cloud copy operations
 * Copy files/folders within MEGA to multiple destinations
 */
class CloudCopierPanel : public QWidget {
    Q_OBJECT

public:
    explicit CloudCopierPanel(QWidget* parent = nullptr);
    ~CloudCopierPanel();

    void setController(CloudCopierController* controller);
    void setFileController(FileController* fileController);

public slots:
    // Data updates from controller
    void onSourcesChanged(const QStringList& sources);
    void onDestinationsChanged(const QStringList& destinations);
    void onTemplatesChanged();
    void onTasksClearing();

    // Task updates
    void onTaskCreated(int taskId, const QString& source, const QString& destination);
    void onTaskProgress(int taskId, int progress);
    void onTaskStatusChanged(int taskId, const QString& status);

    // Copy operation updates
    void onCopyStarted(int totalTasks);
    void onCopyProgress(int completed, int total, const QString& currentItem, const QString& currentDest);
    void onCopyCompleted(int successful, int failed, int skipped);
    void onCopyPaused();
    void onCopyCancelled();

    // Error handling
    void onError(const QString& operation, const QString& message);

    // Preview handling
    void onPreviewReady(const QVector<CopyPreviewItem>& previewItems);

    // Validation results
    void onSourcesValidated(const QVector<PathValidationResult>& results);
    void onDestinationsValidated(const QVector<PathValidationResult>& results);

    // Member mode updates
    void onMemberModeChanged(bool enabled);
    void onAvailableMembersChanged(const QList<MemberInfo>& members);
    void onSelectedMemberChanged(const QString& memberId, const QString& memberName);
    void onAllMembersSelectionChanged(bool allSelected);
    void onDestinationTemplateChanged(const QString& templatePath);
    void onTemplateExpansionReady(const TemplateExpansionPreview& preview);
    void onMemberTaskCreated(int taskId, const QString& source, const QString& dest,
                             const QString& memberId, const QString& memberName);

signals:
    // User actions - sources
    void addSourceRequested(const QString& remotePath);
    void removeSourceRequested(const QString& remotePath);
    void clearSourcesRequested();

    // User actions - destinations
    void addDestinationRequested(const QString& remotePath);
    void removeDestinationRequested(const QString& remotePath);
    void clearDestinationsRequested();

    // User actions - templates
    void saveTemplateRequested(const QString& name);
    void loadTemplateRequested(const QString& name);
    void deleteTemplateRequested(const QString& name);

    // User actions - import/export
    void importDestinationsRequested(const QString& filePath);
    void exportDestinationsRequested(const QString& filePath);

    // User actions - copy control
    void previewCopyRequested(bool copyContentsOnly);
    void startCopyRequested(bool copyContentsOnly, bool skipExisting, bool moveMode);
    void pauseCopyRequested();
    void cancelCopyRequested();
    void clearCompletedRequested();

    // User actions - validation
    void validateSourcesRequested();
    void validateDestinationsRequested();

    // User actions - member mode
    void memberModeRequested(bool enabled);
    void selectMemberRequested(const QString& memberId);
    void selectAllMembersRequested(bool selectAll);
    void destinationTemplateChanged(const QString& templatePath);
    void previewTemplateExpansionRequested();
    void startMemberCopyRequested(bool copyContentsOnly, bool skipExisting);

private slots:
    // Source section
    void onAddSourceClicked();
    void onPasteSourcesClicked();
    void onEditSourcesClicked();
    void onRemoveSourceClicked();
    void onClearSourcesClicked();
    void onSourceSelectionChanged();

    // Destination section
    void onAddDestinationClicked();
    void onPasteDestinationsClicked();
    void onEditDestinationsClicked();
    void onRemoveDestinationClicked();
    void onClearDestinationsClicked();
    void onDestinationSelectionChanged();
    void onValidateDestinationsClicked();

    // Template section
    void onSaveTemplateClicked();
    void onLoadTemplateClicked();
    void onDeleteTemplateClicked();
    void onTemplateComboChanged(int index);

    // Import/Export
    void onImportClicked();
    void onExportClicked();

    // Copy control
    void onPreviewCopyClicked();
    void onStartCopyClicked();
    void onPauseCopyClicked();
    void onCancelCopyClicked();
    void onClearCompletedClicked();
    void onClearAllTasksClicked();

    // Task table
    void onTaskSelectionChanged();

    // Operation mode
    void onOperationModeChanged();

    // Member mode UI
    void onDestinationModeChanged();
    void onMemberComboChanged(int index);
    void onAllMembersCheckChanged(bool checked);
    void onTemplatePathChanged();
    void onPreviewExpansionClicked();
    void onManageMembersClicked();
    void onVariableHelpClicked();

    // Keyboard shortcut handlers
    void onPasteShortcut();
    void onDeleteShortcut();
    void onSelectAllShortcut();

private:
    void setupUI();
    void setupShortcuts();
    void setupSourceSection(QVBoxLayout* mainLayout);
    void setupDestinationSection(QVBoxLayout* mainLayout);
    void setupMemberSection(QVBoxLayout* mainLayout);
    void setupTemplateSection(QVBoxLayout* mainLayout);
    void setupTaskTable(QVBoxLayout* mainLayout);
    void setupProgressSection(QVBoxLayout* mainLayout);
    void setupControlButtons(QVBoxLayout* mainLayout);
    void updateButtonStates();
    void updateTemplateCombo();
    void updateMemberCombo();
    void updateMemberModeUI();
    int findTaskRow(int taskId);
    QString shortenPath(const QString& path, int maxLength = 40);
    QStringList showPastePathsDialog(const QString& title, const QString& instruction,
                                      const QString& placeholder, const QString& buttonText);
    void setupErrorLogSection(QVBoxLayout* mainLayout);
    void logError(const QString& message, const QString& details = QString());
    void logWarning(const QString& message, const QString& details = QString());
    void clearErrorLog();
    void filterTasks(const QString& status);
    void updateTaskCounts();

private:
    CloudCopierController* m_controller = nullptr;
    FileController* m_fileController = nullptr;

    // Source section
    QListWidget* m_sourceList = nullptr;
    QPushButton* m_addSourceBtn = nullptr;
    QPushButton* m_pasteSourcesBtn = nullptr;
    QPushButton* m_editSourcesBtn = nullptr;
    QPushButton* m_removeSourceBtn = nullptr;
    QPushButton* m_clearSourcesBtn = nullptr;
    QLabel* m_sourceSummaryLabel = nullptr;

    // Destination section
    QListWidget* m_destinationList = nullptr;
    QPushButton* m_addDestBtn = nullptr;
    QPushButton* m_pasteDestsBtn = nullptr;
    QPushButton* m_editDestsBtn = nullptr;
    QPushButton* m_removeDestBtn = nullptr;
    QPushButton* m_clearDestsBtn = nullptr;
    QPushButton* m_validateDestsBtn = nullptr;
    QLabel* m_destSummaryLabel = nullptr;

    // Template section
    QComboBox* m_templateCombo = nullptr;
    QPushButton* m_saveTemplateBtn = nullptr;
    QPushButton* m_loadTemplateBtn = nullptr;
    QPushButton* m_deleteTemplateBtn = nullptr;
    QPushButton* m_importBtn = nullptr;
    QPushButton* m_exportBtn = nullptr;

    // Task table
    QTableWidget* m_taskTable = nullptr;
    QComboBox* m_taskFilterCombo = nullptr;
    QLabel* m_taskCountLabel = nullptr;

    // Progress section
    QGroupBox* m_progressGroup = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_currentItemLabel = nullptr;
    QLabel* m_statsLabel = nullptr;

    // Control buttons
    QPushButton* m_previewBtn = nullptr;
    QPushButton* m_startBtn = nullptr;
    QPushButton* m_pauseBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QPushButton* m_clearCompletedBtn = nullptr;
    QPushButton* m_clearAllTasksBtn = nullptr;

    // Options checkboxes
    QCheckBox* m_copyContentsOnlyCheck = nullptr;
    QCheckBox* m_skipExistingCheck = nullptr;

    // Operation mode (Copy vs Move)
    QGroupBox* m_operationModeGroup = nullptr;
    QRadioButton* m_copyModeRadio = nullptr;
    QRadioButton* m_moveModeRadio = nullptr;
    QButtonGroup* m_operationModeButtonGroup = nullptr;

    // Member mode section
    QGroupBox* m_memberGroup = nullptr;
    QRadioButton* m_manualDestRadio = nullptr;
    QRadioButton* m_memberDestRadio = nullptr;
    QButtonGroup* m_destModeGroup = nullptr;
    QComboBox* m_memberCombo = nullptr;
    QCheckBox* m_allMembersCheck = nullptr;
    QLineEdit* m_templatePathEdit = nullptr;
    QPushButton* m_previewExpansionBtn = nullptr;
    QPushButton* m_manageMembersBtn = nullptr;
    QPushButton* m_variableHelpBtn = nullptr;
    QLabel* m_memberCountLabel = nullptr;
    QLabel* m_expansionPreviewLabel = nullptr;
    QWidget* m_memberSelectionWidget = nullptr;  // Container for member selection UI

    // Error log section
    QGroupBox* m_errorLogGroup = nullptr;
    QTextEdit* m_errorLogEdit = nullptr;
    QPushButton* m_clearErrorLogBtn = nullptr;
    int m_errorCount = 0;

    // State
    bool m_isCopying = false;
    bool m_memberModeEnabled = false;

    // Table column indices
    enum TaskColumns {
        COL_SOURCE = 0,
        COL_DESTINATION,
        COL_STATUS,
        COL_PROGRESS,
        COL_COUNT
    };
};

} // namespace MegaCustom

#endif // MEGACUSTOM_CLOUDCOPIERPANEL_H
