#ifndef MEGACUSTOM_COPYCONFLICTDIALOG_H
#define MEGACUSTOM_COPYCONFLICTDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QDateTime>

namespace MegaCustom {

/**
 * Dialog for resolving copy conflicts when a file/folder already exists at destination
 */
class CopyConflictDialog : public QDialog {
    Q_OBJECT

public:
    enum class Resolution {
        SKIP,
        OVERWRITE,
        RENAME,
        SKIP_ALL,
        OVERWRITE_ALL,
        CANCEL
    };

    struct ConflictInfo {
        QString itemName;
        QString sourcePath;
        QString destinationPath;
        qint64 existingSize = 0;
        qint64 sourceSize = 0;
        QDateTime existingModTime;
        QDateTime sourceModTime;
        bool isFolder = false;
    };

    explicit CopyConflictDialog(const ConflictInfo& conflict, QWidget* parent = nullptr);
    ~CopyConflictDialog() = default;

    Resolution getResolution() const { return m_resolution; }

private slots:
    void onSkipClicked();
    void onOverwriteClicked();
    void onRenameClicked();
    void onCancelClicked();

private:
    void setupUI();
    QString formatSize(qint64 bytes);

private:
    ConflictInfo m_conflict;
    Resolution m_resolution = Resolution::SKIP;

    QLabel* m_iconLabel = nullptr;
    QLabel* m_messageLabel = nullptr;
    QLabel* m_existingInfoLabel = nullptr;
    QLabel* m_sourceInfoLabel = nullptr;
    QCheckBox* m_applyToAllCheck = nullptr;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_COPYCONFLICTDIALOG_H
