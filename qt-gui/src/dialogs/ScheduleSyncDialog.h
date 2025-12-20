#ifndef SCHEDULE_SYNC_DIALOG_H
#define SCHEDULE_SYNC_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QCheckBox>
#include <QRadioButton>
#include <QLabel>

namespace MegaCustom {

/**
 * Dialog for scheduling one-time or recurring syncs
 */
class ScheduleSyncDialog : public QDialog {
    Q_OBJECT

public:
    enum class ScheduleType {
        ONCE,
        HOURLY,
        DAILY,
        WEEKLY
    };

    explicit ScheduleSyncDialog(QWidget* parent = nullptr);

    // For editing existing schedule
    void setScheduleData(const QString& taskName, ScheduleType type,
                         const QDateTime& startTime, int repeatInterval);

    // Get data
    QString taskName() const;
    ScheduleType scheduleType() const;
    QDateTime startTime() const;
    int repeatInterval() const;  // For hourly: hours, for daily: days, for weekly: weeks
    bool isEnabled() const;

private slots:
    void onScheduleTypeChanged();
    void validateInput();

private:
    void setupUI();

private:
    QLineEdit* m_nameEdit;

    QRadioButton* m_onceRadio;
    QRadioButton* m_hourlyRadio;
    QRadioButton* m_dailyRadio;
    QRadioButton* m_weeklyRadio;

    QDateTimeEdit* m_dateTimeEdit;
    QSpinBox* m_intervalSpin;
    QLabel* m_intervalLabel;

    QCheckBox* m_enabledCheck;

    QPushButton* m_okBtn;
    QPushButton* m_cancelBtn;
};

} // namespace MegaCustom

#endif // SCHEDULE_SYNC_DIALOG_H
