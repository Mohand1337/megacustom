#include "ScheduleSyncDialog.h"
#include "widgets/ButtonFactory.h"
#include "utils/DpiScaler.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>

namespace MegaCustom {

ScheduleSyncDialog::ScheduleSyncDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Schedule Sync");
    setMinimumWidth(DpiScaler::scale(400));
    setupUI();
}

void ScheduleSyncDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);

    // Task name
    auto* nameLayout = new QFormLayout();
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText("Enter task name");
    nameLayout->addRow("Task Name:", m_nameEdit);
    mainLayout->addLayout(nameLayout);

    // Schedule type group
    auto* typeGroup = new QGroupBox("Schedule Type", this);
    auto* typeLayout = new QVBoxLayout(typeGroup);

    m_onceRadio = new QRadioButton("Run once at specified time", this);
    m_hourlyRadio = new QRadioButton("Repeat every X hours", this);
    m_dailyRadio = new QRadioButton("Repeat every X days", this);
    m_weeklyRadio = new QRadioButton("Repeat every X weeks", this);

    m_onceRadio->setChecked(true);

    typeLayout->addWidget(m_onceRadio);
    typeLayout->addWidget(m_hourlyRadio);
    typeLayout->addWidget(m_dailyRadio);
    typeLayout->addWidget(m_weeklyRadio);

    mainLayout->addWidget(typeGroup);

    // Time configuration group
    auto* timeGroup = new QGroupBox("Time Configuration", this);
    auto* timeLayout = new QFormLayout(timeGroup);

    m_dateTimeEdit = new QDateTimeEdit(QDateTime::currentDateTime().addSecs(3600), this);
    m_dateTimeEdit->setDisplayFormat("yyyy-MM-dd hh:mm");
    m_dateTimeEdit->setCalendarPopup(true);
    m_dateTimeEdit->setMinimumDateTime(QDateTime::currentDateTime());
    timeLayout->addRow("Start Time:", m_dateTimeEdit);

    auto* intervalLayout = new QHBoxLayout();
    m_intervalSpin = new QSpinBox(this);
    m_intervalSpin->setRange(1, 168);  // Up to 1 week in hours
    m_intervalSpin->setValue(1);
    m_intervalSpin->setEnabled(false);
    m_intervalLabel = new QLabel("hours", this);
    intervalLayout->addWidget(m_intervalSpin);
    intervalLayout->addWidget(m_intervalLabel);
    intervalLayout->addStretch();
    timeLayout->addRow("Repeat Every:", intervalLayout);

    mainLayout->addWidget(timeGroup);

    // Enabled checkbox
    m_enabledCheck = new QCheckBox("Enable this scheduled task", this);
    m_enabledCheck->setChecked(true);
    mainLayout->addWidget(m_enabledCheck);

    mainLayout->addStretch();

    // Buttons
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    m_okBtn = ButtonFactory::createPrimary("OK", this);
    m_okBtn->setDefault(true);
    m_cancelBtn = ButtonFactory::createOutline("Cancel", this);
    buttonLayout->addWidget(m_okBtn);
    buttonLayout->addWidget(m_cancelBtn);
    mainLayout->addLayout(buttonLayout);

    // Connections
    connect(m_onceRadio, &QRadioButton::toggled, this, &ScheduleSyncDialog::onScheduleTypeChanged);
    connect(m_hourlyRadio, &QRadioButton::toggled, this, &ScheduleSyncDialog::onScheduleTypeChanged);
    connect(m_dailyRadio, &QRadioButton::toggled, this, &ScheduleSyncDialog::onScheduleTypeChanged);
    connect(m_weeklyRadio, &QRadioButton::toggled, this, &ScheduleSyncDialog::onScheduleTypeChanged);
    connect(m_nameEdit, &QLineEdit::textChanged, this, &ScheduleSyncDialog::validateInput);
    connect(m_okBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    onScheduleTypeChanged();
    validateInput();
}

void ScheduleSyncDialog::setScheduleData(const QString& taskName, ScheduleType type,
                                          const QDateTime& startTime, int repeatInterval)
{
    m_nameEdit->setText(taskName);
    m_dateTimeEdit->setDateTime(startTime);
    m_intervalSpin->setValue(repeatInterval);

    switch (type) {
        case ScheduleType::ONCE:
            m_onceRadio->setChecked(true);
            break;
        case ScheduleType::HOURLY:
            m_hourlyRadio->setChecked(true);
            break;
        case ScheduleType::DAILY:
            m_dailyRadio->setChecked(true);
            break;
        case ScheduleType::WEEKLY:
            m_weeklyRadio->setChecked(true);
            break;
    }
}

QString ScheduleSyncDialog::taskName() const
{
    return m_nameEdit->text().trimmed();
}

ScheduleSyncDialog::ScheduleType ScheduleSyncDialog::scheduleType() const
{
    if (m_hourlyRadio->isChecked()) return ScheduleType::HOURLY;
    if (m_dailyRadio->isChecked()) return ScheduleType::DAILY;
    if (m_weeklyRadio->isChecked()) return ScheduleType::WEEKLY;
    return ScheduleType::ONCE;
}

QDateTime ScheduleSyncDialog::startTime() const
{
    return m_dateTimeEdit->dateTime();
}

int ScheduleSyncDialog::repeatInterval() const
{
    return m_intervalSpin->value();
}

bool ScheduleSyncDialog::isEnabled() const
{
    return m_enabledCheck->isChecked();
}

void ScheduleSyncDialog::onScheduleTypeChanged()
{
    bool isRecurring = !m_onceRadio->isChecked();
    m_intervalSpin->setEnabled(isRecurring);

    if (m_hourlyRadio->isChecked()) {
        m_intervalLabel->setText("hours");
        m_intervalSpin->setRange(1, 168);
    } else if (m_dailyRadio->isChecked()) {
        m_intervalLabel->setText("days");
        m_intervalSpin->setRange(1, 30);
    } else if (m_weeklyRadio->isChecked()) {
        m_intervalLabel->setText("weeks");
        m_intervalSpin->setRange(1, 12);
    } else {
        m_intervalLabel->setText("hours");
    }
}

void ScheduleSyncDialog::validateInput()
{
    bool valid = !m_nameEdit->text().trimmed().isEmpty();
    m_okBtn->setEnabled(valid);
}

} // namespace MegaCustom
