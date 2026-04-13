#include "SmartRouteReviewDialog.h"
#include "widgets/DistributionPanel.h"
#include "utils/ContentRouter.h"
#include "utils/MemberRegistry.h"
#include "utils/TemplateExpander.h"
#include "utils/DpiScaler.h"
#include "utils/CopyHelper.h"
#include "widgets/ButtonFactory.h"
#include "styles/ThemeManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QFont>
#include <QPalette>

namespace MegaCustom {

SmartRouteReviewDialog::SmartRouteReviewDialog(QWidget* parent)
    : QDialog(parent)
{
    setupUI();
}

void SmartRouteReviewDialog::setupUI() {
    setWindowTitle("Smart Route Review");
    setMinimumSize(DpiScaler::scale(900), DpiScaler::scale(500));
    resize(DpiScaler::scale(1000), DpiScaler::scale(600));

    auto& tm = ThemeManager::instance();

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(DpiScaler::scale(8));
    mainLayout->setContentsMargins(DpiScaler::scale(16), DpiScaler::scale(12),
                                    DpiScaler::scale(16), DpiScaler::scale(12));

    // Summary label
    m_summaryLabel = new QLabel("Scanning routes...");
    m_summaryLabel->setStyleSheet(QString("font-size: %1px; font-weight: bold; color: %2;")
        .arg(DpiScaler::scale(14))
        .arg(tm.textPrimary().name()));
    mainLayout->addWidget(m_summaryLabel);

    // Bulk actions group
    auto* bulkGroup = new QGroupBox("Bulk Actions");
    bulkGroup->setStyleSheet(QString(
        "QGroupBox { font-weight: bold; border: 1px solid %1; "
        "border-radius: 6px; margin-top: 12px; padding-top: 16px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; }")
        .arg(tm.borderSubtle().name()));
    auto* bulkLayout = new QHBoxLayout(bulkGroup);
    bulkLayout->setSpacing(DpiScaler::scale(6));

    auto addBulkBtn = [&](const QString& label, ContentType type, const QColor& color) {
        auto* btn = new QPushButton(label);
        btn->setStyleSheet(QString("QPushButton { border: 1px solid %1; border-radius: 4px; "
            "padding: 4px 10px; color: %1; } "
            "QPushButton:hover { background: %1; color: white; }")
            .arg(color.name()));
        connect(btn, &QPushButton::clicked, this, [this, type]() {
            onBulkSetDestination(type);
        });
        bulkLayout->addWidget(btn);
    };

    addBulkBtn("Set All Hot Seats", ContentType::HOT_SEATS, tm.supportSuccess());
    addBulkBtn("Set All Theory Calls", ContentType::THEORY_CALLS, tm.supportSuccess());
    addBulkBtn("Set All NHB Files", ContentType::NHB_ROOT_FILES, tm.supportInfo());
    addBulkBtn("Set All Fast Forward", ContentType::FAST_FORWARD, tm.supportWarning());
    bulkLayout->addStretch();

    auto* selectAllBtn = new QPushButton("Select All");
    connect(selectAllBtn, &QPushButton::clicked, this, &SmartRouteReviewDialog::onSelectAll);
    bulkLayout->addWidget(selectAllBtn);

    auto* deselectAllBtn = new QPushButton("Deselect All");
    connect(deselectAllBtn, &QPushButton::clicked, this, &SmartRouteReviewDialog::onDeselectAll);
    bulkLayout->addWidget(deselectAllBtn);

    mainLayout->addWidget(bulkGroup);

    // Route table
    m_table = new QTableWidget();
    m_table->setObjectName("SmartRouteTable");
    m_table->setColumnCount(COL_COUNT);
    m_table->setHorizontalHeaderLabels({"", "Member / Item", "Content Type", "Source", "Destination"});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(COL_CHECK, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(COL_ITEM, QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(COL_TYPE, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(COL_SOURCE, QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(COL_DESTINATION, QHeaderView::Stretch);
    m_table->setColumnWidth(COL_CHECK, DpiScaler::scale(30));
    m_table->setColumnWidth(COL_ITEM, DpiScaler::scale(200));
    m_table->setColumnWidth(COL_TYPE, DpiScaler::scale(110));
    m_table->setColumnWidth(COL_SOURCE, DpiScaler::scale(200));
    CopyHelper::installTableCopyMenu(m_table);
    mainLayout->addWidget(m_table, 1);

    // Warning label
    m_warningLabel = new QLabel();
    m_warningLabel->setStyleSheet(QString("color: %1; font-weight: bold; padding: 4px;")
        .arg(tm.supportWarning().name()));
    m_warningLabel->setVisible(false);
    mainLayout->addWidget(m_warningLabel);

    // Buttons
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    auto* cancelBtn = ButtonFactory::createOutline("Cancel", this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(cancelBtn);

    m_applyBtn = ButtonFactory::createPrimary("Apply to Distribution", this);
    connect(m_applyBtn, &QPushButton::clicked, this, [this]() {
        readBackEdits();
        accept();
    });
    buttonLayout->addWidget(m_applyBtn);

    mainLayout->addLayout(buttonLayout);
}

void SmartRouteReviewDialog::setRoutes(const QList<WmFolderInfo>& folders,
                                        MemberRegistry* registry) {
    m_folders = folders;
    m_registry = registry;
    populateTable();
    updateSummary();
}

QList<WmFolderInfo> SmartRouteReviewDialog::getReviewedFolders() const {
    return m_folders;
}

void SmartRouteReviewDialog::populateTable() {
    m_table->setRowCount(0);
    m_rowMap.clear();

    auto& tm = ThemeManager::instance();
    QFont boldFont = m_table->font();
    boldFont.setBold(true);

    // Count total rows needed
    int totalRows = 0;
    for (const WmFolderInfo& info : m_folders) {
        if (!info.smartRouted || !info.matched) continue;
        totalRows += 1 + info.routes.size(); // header + children
    }
    m_table->setRowCount(totalRows);

    int row = 0;
    for (int fi = 0; fi < m_folders.size(); ++fi) {
        const WmFolderInfo& info = m_folders[fi];
        if (!info.smartRouted || !info.matched) continue;

        // === Header row ===
        m_rowMap[row] = {fi, -1};

        // Checkbox that toggles all children
        auto* headerCheck = new QCheckBox();
        headerCheck->setChecked(info.selected);
        int headerRow = row;
        connect(headerCheck, &QCheckBox::toggled, this, [this, fi, headerRow](bool checked) {
            if (fi >= m_folders.size()) return;
            m_folders[fi].selected = checked;
            for (int r = 0; r < m_folders[fi].routes.size(); ++r) {
                m_folders[fi].routes[r].selected = checked;
                int childRow = headerRow + 1 + r;
                QWidget* w = m_table->cellWidget(childRow, COL_CHECK);
                if (w) {
                    QCheckBox* c = w->findChild<QCheckBox*>();
                    if (c) { c->blockSignals(true); c->setChecked(checked); c->blockSignals(false); }
                }
            }
        });
        auto* hCheckWidget = new QWidget();
        hCheckWidget->setAutoFillBackground(true);
        QPalette hPal = hCheckWidget->palette();
        hPal.setColor(QPalette::Window, tm.surface2());
        hCheckWidget->setPalette(hPal);
        auto* hCheckLayout = new QHBoxLayout(hCheckWidget);
        hCheckLayout->addWidget(headerCheck);
        hCheckLayout->setAlignment(Qt::AlignCenter);
        hCheckLayout->setContentsMargins(0, 0, 0, 0);
        m_table->setCellWidget(row, COL_CHECK, hCheckWidget);

        // Member name
        QString displayName = info.memberId;
        if (m_registry) {
            MemberInfo mi = m_registry->getMember(info.memberId);
            if (!mi.displayName.isEmpty()) displayName = mi.displayName;
        }
        auto* nameItem = new QTableWidgetItem(displayName);
        nameItem->setFont(boldFont);
        nameItem->setBackground(tm.surface2());
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, COL_ITEM, nameItem);

        // Type summary
        auto* typeItem = new QTableWidgetItem(QString("%1 routes").arg(info.routes.size()));
        typeItem->setFont(boldFont);
        typeItem->setBackground(tm.surface2());
        typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, COL_TYPE, typeItem);

        // Source
        auto* srcItem = new QTableWidgetItem(info.fullPath.section('/', -1));
        srcItem->setToolTip(info.fullPath);
        srcItem->setBackground(tm.surface2());
        srcItem->setFlags(srcItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, COL_SOURCE, srcItem);

        // Dest (header — not editable)
        auto* hDestItem = new QTableWidgetItem("");
        hDestItem->setBackground(tm.surface2());
        hDestItem->setFlags(hDestItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, COL_DESTINATION, hDestItem);

        row++;

        // === Child rows ===
        for (int ri = 0; ri < info.routes.size(); ++ri) {
            const ContentRoute& route = info.routes[ri];
            m_rowMap[row] = {fi, ri};

            // Child checkbox
            auto* childCheck = new QCheckBox();
            childCheck->setChecked(route.selected);
            connect(childCheck, &QCheckBox::toggled, this, [this, fi, ri](bool checked) {
                if (fi < m_folders.size() && ri < m_folders[fi].routes.size())
                    m_folders[fi].routes[ri].selected = checked;
            });
            auto* cCheckWidget = new QWidget();
            auto* cCheckLayout = new QHBoxLayout(cCheckWidget);
            cCheckLayout->addWidget(childCheck);
            cCheckLayout->setAlignment(Qt::AlignCenter);
            cCheckLayout->setContentsMargins(0, 0, 0, 0);
            m_table->setCellWidget(row, COL_CHECK, cCheckWidget);

            // Child name (indented)
            QString childDisplay = QString::fromUtf8("  \xe2\x86\xb3 ") + route.childName;
            if (route.isFolder) childDisplay += "/";
            auto* cNameItem = new QTableWidgetItem(childDisplay);
            cNameItem->setFlags(cNameItem->flags() & ~Qt::ItemIsEditable);
            if (!route.filePaths.isEmpty()) {
                cNameItem->setToolTip(route.filePaths.join("\n"));
            } else {
                cNameItem->setToolTip(route.sourcePath);
            }
            m_table->setItem(row, COL_ITEM, cNameItem);

            // Content type (color-coded)
            auto* cTypeItem = new QTableWidgetItem(route.contentTypeLabel);
            cTypeItem->setTextAlignment(Qt::AlignCenter);
            cTypeItem->setFlags(cTypeItem->flags() & ~Qt::ItemIsEditable);
            switch (route.contentType) {
                case ContentType::HOT_SEATS:
                case ContentType::THEORY_CALLS:
                    cTypeItem->setForeground(tm.supportSuccess());
                    break;
                case ContentType::NHB_ROOT_FILES:
                    cTypeItem->setForeground(tm.supportInfo());
                    break;
                case ContentType::FAST_FORWARD:
                case ContentType::UNKNOWN:
                    cTypeItem->setForeground(tm.supportWarning());
                    break;
            }
            m_table->setItem(row, COL_TYPE, cTypeItem);

            // Source (truncated)
            QString srcDisplay = route.sourcePath.section('/', -2);
            auto* cSrcItem = new QTableWidgetItem(srcDisplay);
            cSrcItem->setToolTip(route.sourcePath);
            cSrcItem->setFlags(cSrcItem->flags() & ~Qt::ItemIsEditable);
            m_table->setItem(row, COL_SOURCE, cSrcItem);

            // Destination (editable!)
            auto* cDestItem = new QTableWidgetItem(route.destinationPath);
            cDestItem->setToolTip(route.destinationPath);
            if (route.destinationPath.isEmpty()) {
                cDestItem->setBackground(QColor(255, 200, 200, 60));
                cDestItem->setToolTip("No destination — member paths not configured");
            }
            m_table->setItem(row, COL_DESTINATION, cDestItem);

            row++;
        }
    }
}

void SmartRouteReviewDialog::updateSummary() {
    int totalRoutes = 0;
    int memberCount = 0;
    int emptyDests = 0;

    for (const WmFolderInfo& info : m_folders) {
        if (!info.smartRouted || !info.matched) continue;
        memberCount++;
        for (const ContentRoute& route : info.routes) {
            totalRoutes++;
            if (route.destinationPath.isEmpty()) emptyDests++;
        }
    }

    m_summaryLabel->setText(QString("Detected %1 content items across %2 members")
        .arg(totalRoutes).arg(memberCount));

    if (emptyDests > 0) {
        m_warningLabel->setText(QString("%1 %2 empty destinations (member paths not configured)")
            .arg(emptyDests).arg(emptyDests == 1 ? "route has" : "routes have"));
        m_warningLabel->setVisible(true);
    } else {
        m_warningLabel->setVisible(false);
    }
}

void SmartRouteReviewDialog::readBackEdits() {
    for (auto it = m_rowMap.constBegin(); it != m_rowMap.constEnd(); ++it) {
        int row = it.key();
        const RowMapping& mapping = it.value();
        if (mapping.routeIdx < 0) continue; // Skip header rows

        // Read destination from table cell
        QTableWidgetItem* destItem = m_table->item(row, COL_DESTINATION);
        if (destItem && mapping.folderIdx < m_folders.size()) {
            auto& routes = m_folders[mapping.folderIdx].routes;
            if (mapping.routeIdx < routes.size()) {
                routes[mapping.routeIdx].destinationPath = destItem->text().trimmed();
            }
        }

        // Read checkbox state
        QWidget* w = m_table->cellWidget(row, COL_CHECK);
        if (w && mapping.folderIdx < m_folders.size()) {
            QCheckBox* c = w->findChild<QCheckBox*>();
            if (c && mapping.routeIdx < m_folders[mapping.folderIdx].routes.size()) {
                m_folders[mapping.folderIdx].routes[mapping.routeIdx].selected = c->isChecked();
            }
        }
    }
}

void SmartRouteReviewDialog::onBulkSetDestination(ContentType type) {
    QString typeLabel = ContentRouter::contentTypeLabel(type);

    // Build a hint showing the template variables
    QString hint = "Enter destination path. Variables: {member}, {archive_root}, {month}\n"
                   "Example: {archive_root}/Fast Forward\xe2\x8f\xa9/3- Hotseats";

    bool ok;
    QString templatePath = QInputDialog::getText(this,
        QString("Set All %1 Destinations").arg(typeLabel),
        hint, QLineEdit::Normal, QString(), &ok);

    if (!ok || templatePath.trimmed().isEmpty()) return;
    templatePath = templatePath.trimmed();

    int updated = 0;
    for (int fi = 0; fi < m_folders.size(); ++fi) {
        if (!m_folders[fi].smartRouted) continue;

        MemberInfo member;
        if (m_registry) member = m_registry->getMember(m_folders[fi].memberId);

        for (int ri = 0; ri < m_folders[fi].routes.size(); ++ri) {
            ContentRoute& route = m_folders[fi].routes[ri];
            if (route.contentType != type) continue;
            if (!route.selected) continue;

            // Expand template for this member
            QString expanded = templatePath;
            expanded.replace("{member}", member.id);
            expanded.replace("{member_name}", member.displayName.isEmpty() ? member.id : member.displayName);
            expanded.replace("{archive_root}", member.paths.archiveRoot);
            expanded.replace("{fast_forward}", member.paths.fastForwardPath);
            expanded.replace("{hot_seats}", member.paths.hotSeatsPath);
            expanded.replace("{theory_calls}", member.paths.theoryCallsPath);
            expanded.replace("{nhb_calls}", member.paths.nhbCallsPath);
            expanded.replace("{month}", ""); // Month handled elsewhere

            route.destinationPath = expanded;
            updated++;
        }
    }

    // Refresh table to show new destinations
    populateTable();
    updateSummary();

    auto& tm = ThemeManager::instance();
    m_summaryLabel->setText(QString("Updated %1 %2 destinations").arg(updated).arg(typeLabel));
}

void SmartRouteReviewDialog::onSelectAll() {
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QWidget* w = m_table->cellWidget(row, COL_CHECK);
        if (w) {
            QCheckBox* c = w->findChild<QCheckBox*>();
            if (c) c->setChecked(true);
        }
    }
}

void SmartRouteReviewDialog::onDeselectAll() {
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QWidget* w = m_table->cellWidget(row, COL_CHECK);
        if (w) {
            QCheckBox* c = w->findChild<QCheckBox*>();
            if (c) c->setChecked(false);
        }
    }
}

} // namespace MegaCustom
