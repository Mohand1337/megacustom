#ifndef MAPPING_EDIT_DIALOG_H
#define MAPPING_EDIT_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>

namespace MegaCustom {

class FileController;

/**
 * Dialog for creating/editing folder mappings
 */
class MappingEditDialog : public QDialog {
    Q_OBJECT

public:
    explicit MappingEditDialog(QWidget* parent = nullptr);

    /**
     * Set file controller for remote folder browsing
     */
    void setFileController(FileController* controller);

    // For editing existing mapping
    void setMappingData(const QString& name, const QString& localPath,
                        const QString& remotePath, bool enabled);

    // Get data
    QString mappingName() const;
    QString localPath() const;
    QString remotePath() const;
    bool isEnabled() const;

private slots:
    void onBrowseLocalClicked();
    void onBrowseRemoteClicked();
    void validateInput();

private:
    void setupUI();

private:
    FileController* m_fileController = nullptr;
    QLineEdit* m_nameEdit;
    QLineEdit* m_localPathEdit;
    QLineEdit* m_remotePathEdit;
    QPushButton* m_browseLocalBtn;
    QPushButton* m_browseRemoteBtn;
    QCheckBox* m_enabledCheck;
    QPushButton* m_okBtn;
    QPushButton* m_cancelBtn;
};

} // namespace MegaCustom

#endif // MAPPING_EDIT_DIALOG_H
