#ifndef DISTRIBUTION_RULE_DIALOG_H
#define DISTRIBUTION_RULE_DIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>

namespace MegaCustom {

/**
 * Dialog for creating/editing distribution rules in MultiUploader
 */
class DistributionRuleDialog : public QDialog {
    Q_OBJECT

public:
    enum class RuleType {
        BY_EXTENSION,
        BY_SIZE,
        BY_NAME,
        DEFAULT
    };

    explicit DistributionRuleDialog(const QStringList& destinations, QWidget* parent = nullptr);

    // For editing existing rule
    void setRuleData(RuleType type, const QString& pattern, const QString& destination);

    // Get data
    RuleType ruleType() const;
    QString pattern() const;
    QString destination() const;

private slots:
    void onRuleTypeChanged(int index);
    void validateInput();

private:
    void setupUI();

private:
    QStringList m_destinations;

    QComboBox* m_ruleTypeCombo;
    QLineEdit* m_patternEdit;
    QComboBox* m_destinationCombo;
    QPushButton* m_okBtn;
    QPushButton* m_cancelBtn;
};

} // namespace MegaCustom

#endif // DISTRIBUTION_RULE_DIALOG_H
