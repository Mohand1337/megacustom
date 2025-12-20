#include "DistributionRuleDialog.h"
#include "widgets/ButtonFactory.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QGroupBox>

namespace MegaCustom {

DistributionRuleDialog::DistributionRuleDialog(const QStringList& destinations, QWidget* parent)
    : QDialog(parent)
    , m_destinations(destinations)
{
    setWindowTitle("Distribution Rule");
    setMinimumWidth(400);
    setupUI();
}

void DistributionRuleDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);

    // Form group
    auto* formGroup = new QGroupBox("Rule Configuration", this);
    auto* formLayout = new QFormLayout(formGroup);

    // Rule type
    m_ruleTypeCombo = new QComboBox(this);
    m_ruleTypeCombo->addItem("By Extension", static_cast<int>(RuleType::BY_EXTENSION));
    m_ruleTypeCombo->addItem("By Size Range", static_cast<int>(RuleType::BY_SIZE));
    m_ruleTypeCombo->addItem("By Name Pattern", static_cast<int>(RuleType::BY_NAME));
    m_ruleTypeCombo->addItem("Default", static_cast<int>(RuleType::DEFAULT));
    formLayout->addRow("Rule Type:", m_ruleTypeCombo);

    // Pattern
    m_patternEdit = new QLineEdit(this);
    formLayout->addRow("Pattern:", m_patternEdit);

    // Destination
    m_destinationCombo = new QComboBox(this);
    m_destinationCombo->addItems(m_destinations);
    formLayout->addRow("Destination:", m_destinationCombo);

    mainLayout->addWidget(formGroup);

    // Help text
    auto* helpLabel = new QLabel(this);
    helpLabel->setWordWrap(true);
    helpLabel->setStyleSheet("color: gray; font-size: 11px;");
    mainLayout->addWidget(helpLabel);

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
    connect(m_ruleTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DistributionRuleDialog::onRuleTypeChanged);
    connect(m_patternEdit, &QLineEdit::textChanged,
            this, &DistributionRuleDialog::validateInput);
    connect(m_okBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    onRuleTypeChanged(0);
    validateInput();
}

void DistributionRuleDialog::setRuleData(RuleType type, const QString& pattern,
                                          const QString& destination)
{
    int index = m_ruleTypeCombo->findData(static_cast<int>(type));
    if (index >= 0) {
        m_ruleTypeCombo->setCurrentIndex(index);
    }
    m_patternEdit->setText(pattern);

    int destIndex = m_destinationCombo->findText(destination);
    if (destIndex >= 0) {
        m_destinationCombo->setCurrentIndex(destIndex);
    }
}

DistributionRuleDialog::RuleType DistributionRuleDialog::ruleType() const
{
    return static_cast<RuleType>(m_ruleTypeCombo->currentData().toInt());
}

QString DistributionRuleDialog::pattern() const
{
    return m_patternEdit->text().trimmed();
}

QString DistributionRuleDialog::destination() const
{
    return m_destinationCombo->currentText();
}

void DistributionRuleDialog::onRuleTypeChanged(int index)
{
    RuleType type = static_cast<RuleType>(m_ruleTypeCombo->itemData(index).toInt());

    switch (type) {
        case RuleType::BY_EXTENSION:
            m_patternEdit->setPlaceholderText("jpg, png, gif");
            m_patternEdit->setEnabled(true);
            break;
        case RuleType::BY_SIZE:
            m_patternEdit->setPlaceholderText("0-10 (size range in MB)");
            m_patternEdit->setEnabled(true);
            break;
        case RuleType::BY_NAME:
            m_patternEdit->setPlaceholderText("report_* or *_backup*");
            m_patternEdit->setEnabled(true);
            break;
        case RuleType::DEFAULT:
            m_patternEdit->setPlaceholderText("(matches all files)");
            m_patternEdit->setEnabled(false);
            m_patternEdit->clear();
            break;
    }

    validateInput();
}

void DistributionRuleDialog::validateInput()
{
    RuleType type = ruleType();
    bool valid = m_destinationCombo->count() > 0;

    if (type != RuleType::DEFAULT) {
        valid = valid && !m_patternEdit->text().trimmed().isEmpty();
    }

    m_okBtn->setEnabled(valid);
}

} // namespace MegaCustom
