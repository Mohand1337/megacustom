#include "CloudPathValidator.h"
#include <megaapi.h>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QPushButton>
#include <QFont>
#include <QDebug>

namespace MegaCustom {

QVector<CloudPathValidationResult> CloudPathValidator::validatePaths(
    mega::MegaApi* api, const QStringList& paths)
{
    QVector<CloudPathValidationResult> results;
    results.reserve(paths.size());

    for (const QString& path : paths) {
        CloudPathValidationResult result;
        result.path = path;

        if (path.isEmpty()) {
            result.exists = false;
            result.errorMessage = "Empty path";
            results.append(result);
            continue;
        }

        if (!api) {
            result.exists = false;
            result.errorMessage = "No MEGA session";
            results.append(result);
            continue;
        }

        mega::MegaNode* node = api->getNodeByPath(path.toUtf8().constData());
        if (node) {
            result.exists = true;
            result.isFolder = node->isFolder();
            if (!node->isFolder()) {
                result.errorMessage = "Path exists but is not a folder";
            }
            delete node;
        } else {
            result.exists = false;
            result.errorMessage = "Path not found on MEGA cloud";
        }

        results.append(result);
    }

    qDebug() << "CloudPathValidator: Validated" << results.size() << "paths,"
             << invalidCount(results) << "invalid";
    return results;
}

bool CloudPathValidator::allValid(const QVector<CloudPathValidationResult>& results) {
    for (const auto& r : results) {
        if (!r.exists || !r.isFolder) return false;
    }
    return !results.isEmpty();
}

int CloudPathValidator::invalidCount(const QVector<CloudPathValidationResult>& results) {
    int count = 0;
    for (const auto& r : results) {
        if (!r.exists || !r.errorMessage.isEmpty()) count++;
    }
    return count;
}

QStringList CloudPathValidator::validPaths(const QVector<CloudPathValidationResult>& results) {
    QStringList paths;
    for (const auto& r : results) {
        if (r.exists && r.isFolder) paths.append(r.path);
    }
    return paths;
}

QStringList CloudPathValidator::invalidPaths(const QVector<CloudPathValidationResult>& results) {
    QStringList paths;
    for (const auto& r : results) {
        if (!r.exists || !r.isFolder) paths.append(r.path);
    }
    return paths;
}

CloudPathValidator::UserAction CloudPathValidator::showValidationDialog(
    QWidget* parent,
    const QVector<CloudPathValidationResult>& results,
    const QString& operationName)
{
    int valid = 0;
    int invalid = 0;
    for (const auto& r : results) {
        if (r.exists && r.errorMessage.isEmpty()) valid++;
        else invalid++;
    }

    // If all valid, no dialog needed
    if (invalid == 0) return ProceedValidOnly;

    QDialog dialog(parent);
    dialog.setWindowTitle(QString("%1 — Path Validation").arg(operationName));
    dialog.setMinimumSize(700, 450);

    auto* layout = new QVBoxLayout(&dialog);

    // Warning banner
    auto* bannerLabel = new QLabel(
        QString("<b>%1 of %2 destination paths do not exist on MEGA cloud.</b><br>"
                "If you proceed, these paths will be auto-created — which may not be what you intended.")
            .arg(invalid).arg(results.size()),
        &dialog);
    bannerLabel->setWordWrap(true);
    bannerLabel->setStyleSheet(
        "background: #FFF3CD; color: #856404; border: 1px solid #FFEEBA; "
        "border-radius: 4px; padding: 10px; font-size: 13px;");
    layout->addWidget(bannerLabel);

    // Summary line
    auto* summaryLabel = new QLabel(
        QString("<b>%1</b> valid &nbsp;&nbsp; <span style='color:red;'><b>%2</b> missing/invalid</span>")
            .arg(valid).arg(invalid),
        &dialog);
    layout->addWidget(summaryLabel);

    // Detailed results
    auto* resultText = new QTextEdit(&dialog);
    resultText->setReadOnly(true);
    resultText->setFont(QFont("Courier New", 9));

    QString content;
    for (const auto& r : results) {
        QString status;
        if (!r.exists) {
            status = "NOT FOUND";
        } else if (!r.errorMessage.isEmpty()) {
            status = "ERROR";
        } else {
            status = "OK";
        }

        QString line = QString("[%1] %2").arg(status, -9).arg(r.path);
        if (!r.errorMessage.isEmpty() && status != "OK") {
            line += QString("  (%1)").arg(r.errorMessage);
        }
        line += "\n";

        if (!r.exists || !r.errorMessage.isEmpty()) {
            content += QString("<span style='color:red;'>%1</span>")
                .arg(line.toHtmlEscaped().replace("\n", "<br>"));
        } else {
            content += QString("<span style='color:green;'>%1</span>")
                .arg(line.toHtmlEscaped().replace("\n", "<br>"));
        }
    }
    resultText->setHtml(QString("<pre style='white-space: pre-wrap;'>%1</pre>").arg(content));
    layout->addWidget(resultText);

    // Action buttons
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    UserAction chosenAction = Cancel;

    auto* cancelBtn = new QPushButton("Cancel", &dialog);
    cancelBtn->setObjectName("PanelDangerButton");
    QObject::connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);

    QPushButton* validOnlyBtn = nullptr;
    if (valid > 0) {
        validOnlyBtn = new QPushButton(
            QString("Proceed with %1 valid %2 only").arg(valid).arg(valid == 1 ? "path" : "paths"), &dialog);
        validOnlyBtn->setObjectName("PanelSecondaryButton");
        QObject::connect(validOnlyBtn, &QPushButton::clicked, &dialog, [&]() {
            chosenAction = ProceedValidOnly;
            dialog.accept();
        });
        btnLayout->addWidget(validOnlyBtn);
    }

    auto* createBtn = new QPushButton(
        QString("Create %1 missing & proceed").arg(invalid), &dialog);
    createBtn->setObjectName("PanelPrimaryButton");
    QObject::connect(createBtn, &QPushButton::clicked, &dialog, [&]() {
        chosenAction = CreateAndProceed;
        dialog.accept();
    });
    btnLayout->addWidget(createBtn);

    layout->addLayout(btnLayout);

    dialog.exec();
    return chosenAction;
}

} // namespace MegaCustom
