#ifndef CONFLICT_RESOLUTION_DIALOG_H
#define CONFLICT_RESOLUTION_DIALOG_H

#include <QDialog>
#include <QLabel>
#include <QRadioButton>
#include <QPushButton>
#include <QCheckBox>
#include <QDateTime>

namespace MegaCustom {

/**
 * Dialog for resolving sync conflicts in SmartSync
 */
class ConflictResolutionDialog : public QDialog {
    Q_OBJECT

public:
    enum class Resolution {
        KEEP_LOCAL,
        KEEP_REMOTE,
        KEEP_BOTH,
        SKIP
    };

    struct FileInfo {
        QString path;
        qint64 size;
        QDateTime modifiedTime;
    };

    explicit ConflictResolutionDialog(QWidget* parent = nullptr);

    // Set conflict details
    void setConflict(const QString& fileName, const FileInfo& localInfo, const FileInfo& remoteInfo);

    // Get resolution
    Resolution resolution() const;
    bool applyToAll() const;

private:
    void setupUI();
    QString formatSize(qint64 bytes) const;

private:
    QLabel* m_fileNameLabel;

    // Local file info
    QLabel* m_localSizeLabel;
    QLabel* m_localDateLabel;

    // Remote file info
    QLabel* m_remoteSizeLabel;
    QLabel* m_remoteDateLabel;

    // Resolution options
    QRadioButton* m_keepLocalRadio;
    QRadioButton* m_keepRemoteRadio;
    QRadioButton* m_keepBothRadio;
    QRadioButton* m_skipRadio;

    QCheckBox* m_applyToAllCheck;

    QPushButton* m_okBtn;
    QPushButton* m_cancelBtn;
};

} // namespace MegaCustom

#endif // CONFLICT_RESOLUTION_DIALOG_H
