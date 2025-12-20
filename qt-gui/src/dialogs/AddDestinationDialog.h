#ifndef ADD_DESTINATION_DIALOG_H
#define ADD_DESTINATION_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>

namespace MegaCustom {

class FileController;

/**
 * Dialog for adding upload destinations in MultiUploader
 */
class AddDestinationDialog : public QDialog {
    Q_OBJECT

public:
    explicit AddDestinationDialog(QWidget* parent = nullptr);

    /**
     * Set file controller for remote folder browsing
     */
    void setFileController(FileController* controller);

    // For editing existing destination
    void setDestinationData(const QString& path, const QString& alias, bool createIfMissing);

    // Get data
    QString remotePath() const;
    QString alias() const;
    bool createIfMissing() const;

private slots:
    void onBrowseClicked();
    void validateInput();

private:
    void setupUI();

private:
    QLineEdit* m_pathEdit;
    QLineEdit* m_aliasEdit;
    QPushButton* m_browseBtn;
    QCheckBox* m_createCheck;
    QPushButton* m_okBtn;
    QPushButton* m_cancelBtn;

    FileController* m_fileController = nullptr;
};

} // namespace MegaCustom

#endif // ADD_DESTINATION_DIALOG_H
