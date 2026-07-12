#include "ContentManagerPanel.h"
#include "EmptyStateWidget.h"
#include "utils/MemberRegistry.h"
#include "utils/DpiScaler.h"
#include "utils/CopyHelper.h"
#include "widgets/ButtonFactory.h"
#include "styles/ThemeManager.h"
#include "features/CloudCopier.h"

#include <megaapi.h>
#include <QtConcurrent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QScrollBar>

namespace MegaCustom {

ContentManagerPanel::ContentManagerPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

ContentManagerPanel::~ContentManagerPanel() {
    if (m_scanFuture.isRunning()) {
        m_scanFuture.waitForFinished();
    }
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(3000);
    }
}

void ContentManagerPanel::setMegaApi(mega::MegaApi* api) {
    if (m_isScanning) {
        return;
    }
    m_megaApi = api;
}

void ContentManagerPanel::setMemberRegistry(MemberRegistry* registry) {
    m_registry = registry;

    // Populate reference member and group combos
    if (m_registry && m_referenceMemberCombo) {
        m_referenceMemberCombo->clear();
        m_referenceMemberCombo->addItem("-- Select Reference --", QString());
        QList<MemberInfo> members = m_registry->getAllMembers();
        for (const MemberInfo& m : members) {
            QString display = m.displayName.isEmpty() ? m.id : m.displayName;
            m_referenceMemberCombo->addItem(display, m.id);
        }
    }

    if (m_registry && m_groupFilterCombo) {
        m_groupFilterCombo->clear();
        m_groupFilterCombo->addItem("All Members", QString());
        QStringList groups = m_registry->getGroupNames();
        for (const QString& g : groups) {
            m_groupFilterCombo->addItem(g, "GROUP:" + g);
        }
    }
}

void ContentManagerPanel::setCloudCopier(CloudCopier* copier) {
    m_cloudCopier = copier;
}

// ==================== UI Setup ====================

void ContentManagerPanel::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_tabWidget = new QTabWidget();
    m_tabWidget->setObjectName("ContentManagerTabs");

    auto* auditTab = new QWidget();
    setupAuditTab(auditTab);
    m_tabWidget->addTab(auditTab, "Content Audit");

    auto* organizeTab = new QWidget();
    setupOrganizeTab(organizeTab);
    m_tabWidget->addTab(organizeTab, "Organize");

    mainLayout->addWidget(m_tabWidget);
}

void ContentManagerPanel::setupAuditTab(QWidget* tab) {
    auto& tm = ThemeManager::instance();
    auto* layout = new QVBoxLayout(tab);
    layout->setSpacing(DpiScaler::scale(8));
    layout->setContentsMargins(DpiScaler::scale(12), DpiScaler::scale(8),
                                DpiScaler::scale(12), DpiScaler::scale(8));

    // Config section
    auto* configGroup = new QGroupBox("Scan Configuration");
    auto* configLayout = new QGridLayout(configGroup);
    configLayout->setSpacing(DpiScaler::scale(6));

    configLayout->addWidget(new QLabel("Base Path:"), 0, 0);
    m_basePathEdit = new QLineEdit();
    m_basePathEdit->setPlaceholderText("/2026/Latest");
    m_basePathEdit->setToolTip("MEGA cloud path containing member subfolders");
    configLayout->addWidget(m_basePathEdit, 0, 1);

    m_browseBtn = new QPushButton("Scan Path");
    m_browseBtn->setToolTip("Scan the base path to detect member folders");
    connect(m_browseBtn, &QPushButton::clicked, this, &ContentManagerPanel::onScanBasePath);
    configLayout->addWidget(m_browseBtn, 0, 2);

    configLayout->addWidget(new QLabel("Reference:"), 1, 0);
    m_referenceMemberCombo = new QComboBox();
    m_referenceMemberCombo->setToolTip("Member with the most complete content — used as baseline");
    configLayout->addWidget(m_referenceMemberCombo, 1, 1);

    configLayout->addWidget(new QLabel("Group:"), 1, 2);
    m_groupFilterCombo = new QComboBox();
    m_groupFilterCombo->setMinimumWidth(DpiScaler::scale(150));
    m_groupFilterCombo->setToolTip("Filter to a specific member group");
    configLayout->addWidget(m_groupFilterCombo, 1, 3);

    m_scanBtn = ButtonFactory::createPrimary("Scan && Compare", this);
    m_scanBtn->setToolTip("Scan all member folders and compare against reference");
    connect(m_scanBtn, &QPushButton::clicked, this, &ContentManagerPanel::onScanClicked);
    configLayout->addWidget(m_scanBtn, 1, 4);

    layout->addWidget(configGroup);

    // Progress
    m_auditProgressBar = new QProgressBar();
    m_auditProgressBar->setVisible(false);
    m_auditProgressBar->setTextVisible(true);
    layout->addWidget(m_auditProgressBar);

    // Summary
    m_auditSummaryLabel = new QLabel("Select a base path and reference member, then click Scan.");
    m_auditSummaryLabel->setStyleSheet(QString("font-weight: bold; color: %1; padding: 4px;")
        .arg(tm.textSecondary().name()));
    layout->addWidget(m_auditSummaryLabel);

    // Results table — rows = members, columns = calls
    m_auditTable = new QTableWidget();
    m_auditTable->setObjectName("ContentAuditTable");
    m_auditTable->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_auditTable->setAlternatingRowColors(true);
    m_auditTable->verticalHeader()->setVisible(false);
    m_auditTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    CopyHelper::installTableCopyMenu(m_auditTable);
    layout->addWidget(m_auditTable, 1);

    // Bottom buttons
    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->addStretch();

    m_exportBtn = ButtonFactory::createOutline("Export Missing List", this);
    m_exportBtn->setEnabled(false);
    connect(m_exportBtn, &QPushButton::clicked, this, &ContentManagerPanel::onExportMissing);
    bottomLayout->addWidget(m_exportBtn);

    layout->addLayout(bottomLayout);
}

void ContentManagerPanel::setupOrganizeTab(QWidget* tab) {
    auto& tm = ThemeManager::instance();
    auto* layout = new QVBoxLayout(tab);
    layout->setSpacing(DpiScaler::scale(8));
    layout->setContentsMargins(DpiScaler::scale(12), DpiScaler::scale(8),
                                DpiScaler::scale(12), DpiScaler::scale(8));

    // Rename section
    auto* renameGroup = new QGroupBox("Rename Queue");
    auto* renameLayout = new QVBoxLayout(renameGroup);

    m_renameSummaryLabel = new QLabel("Run an audit first to detect oddly-named files.");
    m_renameSummaryLabel->setStyleSheet(QString("color: %1;").arg(tm.textSecondary().name()));
    renameLayout->addWidget(m_renameSummaryLabel);

    m_renameTable = new QTableWidget();
    m_renameTable->setColumnCount(4);
    m_renameTable->setHorizontalHeaderLabels({"", "Member", "Current Name", "New Name"});
    m_renameTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_renameTable->setAlternatingRowColors(true);
    m_renameTable->verticalHeader()->setVisible(false);
    m_renameTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_renameTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_renameTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_renameTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_renameTable->setColumnWidth(0, DpiScaler::scale(30));
    m_renameTable->setColumnWidth(1, DpiScaler::scale(100));
    m_renameTable->setColumnWidth(2, DpiScaler::scale(300));
    CopyHelper::installTableCopyMenu(m_renameTable);
    renameLayout->addWidget(m_renameTable, 1);

    auto* renameBtnLayout = new QHBoxLayout();
    renameBtnLayout->addStretch();
    m_applyRenameBtn = ButtonFactory::createPrimary("Apply Renames", this);
    m_applyRenameBtn->setEnabled(false);
    connect(m_applyRenameBtn, &QPushButton::clicked, this, &ContentManagerPanel::onApplyRenames);
    renameBtnLayout->addWidget(m_applyRenameBtn);
    renameLayout->addLayout(renameBtnLayout);

    layout->addWidget(renameGroup, 1);

    // Reorganize section
    auto* reorgGroup = new QGroupBox("Reorganize Queue (FF → Subfolders)");
    auto* reorgLayout = new QVBoxLayout(reorgGroup);

    m_reorganizeSummaryLabel = new QLabel("Run an audit first to detect flat FF files.");
    m_reorganizeSummaryLabel->setStyleSheet(QString("color: %1;").arg(tm.textSecondary().name()));
    reorgLayout->addWidget(m_reorganizeSummaryLabel);

    m_reorganizeTable = new QTableWidget();
    m_reorganizeTable->setColumnCount(4);
    m_reorganizeTable->setHorizontalHeaderLabels({"", "Member", "Files", "Target Folder"});
    m_reorganizeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_reorganizeTable->setAlternatingRowColors(true);
    m_reorganizeTable->verticalHeader()->setVisible(false);
    m_reorganizeTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_reorganizeTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_reorganizeTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_reorganizeTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_reorganizeTable->setColumnWidth(0, DpiScaler::scale(30));
    m_reorganizeTable->setColumnWidth(1, DpiScaler::scale(100));
    m_reorganizeTable->setColumnWidth(2, DpiScaler::scale(300));
    CopyHelper::installTableCopyMenu(m_reorganizeTable);
    reorgLayout->addWidget(m_reorganizeTable, 1);

    auto* reorgBtnLayout = new QHBoxLayout();
    reorgBtnLayout->addStretch();
    m_applyReorganizeBtn = ButtonFactory::createPrimary("Apply Reorganize", this);
    m_applyReorganizeBtn->setEnabled(false);
    connect(m_applyReorganizeBtn, &QPushButton::clicked, this, &ContentManagerPanel::onApplyReorganize);
    reorgBtnLayout->addWidget(m_applyReorganizeBtn);
    reorgLayout->addLayout(reorgBtnLayout);

    layout->addWidget(reorgGroup, 1);

    // Progress
    m_organizeProgressBar = new QProgressBar();
    m_organizeProgressBar->setVisible(false);
    layout->addWidget(m_organizeProgressBar);

    m_organizeStatusLabel = new QLabel();
    m_organizeStatusLabel->setVisible(false);
    layout->addWidget(m_organizeStatusLabel);
}

// ==================== Audit Logic ====================

void ContentManagerPanel::onScanBasePath() {
    QString basePath = m_basePathEdit->text().trimmed();
    if (basePath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Enter a base path first.");
        return;
    }
    if (!m_megaApi) {
        QMessageBox::warning(this, "Error", "Not connected to MEGA.");
        return;
    }

    // Scan base path to find member subfolders and populate reference combo
    std::unique_ptr<mega::MegaNode> baseNode(m_megaApi->getNodeByPath(basePath.toUtf8().constData()));
    if (!baseNode) {
        QMessageBox::warning(this, "Error", "Path not found: " + basePath);
        return;
    }

    std::unique_ptr<mega::MegaNodeList> children(m_megaApi->getChildren(baseNode.get()));
    if (!children) return;

    m_referenceMemberCombo->clear();
    m_referenceMemberCombo->addItem("-- Select Reference --", QString());

    for (int i = 0; i < children->size(); ++i) {
        mega::MegaNode* child = children->get(i);
        if (child && child->isFolder()) {
            QString name = QString::fromUtf8(child->getName());
            m_referenceMemberCombo->addItem(name, name);
        }
    }

    m_auditSummaryLabel->setText(QString("Found %1 member folders. Select a reference and click Scan.")
        .arg(m_referenceMemberCombo->count() - 1));
}

void ContentManagerPanel::onScanClicked() {
    if (m_isScanning) return;

    QString basePath = m_basePathEdit->text().trimmed();
    m_referenceMemberId = m_referenceMemberCombo->currentData().toString();

    if (basePath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Enter a base path.");
        return;
    }
    if (m_referenceMemberId.isEmpty()) {
        QMessageBox::warning(this, "Error", "Select a reference member.");
        return;
    }
    if (!m_megaApi) {
        QMessageBox::warning(this, "Error", "Not connected to MEGA.");
        return;
    }

    m_isScanning = true;
    m_scanBtn->setEnabled(false);
    m_auditProgressBar->setVisible(true);
    m_auditSummaryLabel->setText("Scanning...");
    m_auditResults.clear();
    m_masterCallList.clear();

    emit scanStarted();

    // Run scan in background
    mega::MegaApi* api = m_megaApi;
    QString refId = m_referenceMemberId;
    QString groupFilter = m_groupFilterCombo->currentData().toString();

    m_scanFuture = QtConcurrent::run([this, api, basePath, refId, groupFilter]() {
        // Get base folder
        std::unique_ptr<mega::MegaNode> baseNode(api->getNodeByPath(basePath.toUtf8().constData()));
        if (!baseNode) {
            QMetaObject::invokeMethod(this, [this]() {
                m_isScanning = false;
                m_scanBtn->setEnabled(true);
                m_auditProgressBar->setVisible(false);
                m_auditSummaryLabel->setText("Error: Base path not found.");
            }, Qt::QueuedConnection);
            return;
        }

        // List member folders
        std::unique_ptr<mega::MegaNodeList> members(api->getChildren(baseNode.get()));
        if (!members) {
            QMetaObject::invokeMethod(this, [this]() {
                m_isScanning = false;
                m_scanBtn->setEnabled(true);
                m_auditProgressBar->setVisible(false);
                m_auditSummaryLabel->setText("Error: Could not list member folders.");
            }, Qt::QueuedConnection);
            return;
        }

        QList<QPair<QString, QString>> memberFolders; // (id, fullPath)
        for (int i = 0; i < members->size(); ++i) {
            mega::MegaNode* m = members->get(i);
            if (m && m->isFolder()) {
                QString name = QString::fromUtf8(m->getName());
                QString path = basePath + "/" + name;

                // Filter by group if selected
                if (groupFilter.startsWith("GROUP:") && m_registry) {
                    QString groupName = groupFilter.mid(6);
                    QStringList groupMembers = m_registry->getGroupMemberIds(groupName);
                    if (!groupMembers.contains(name)) continue;
                }

                memberFolders.append({name, path});
            }
        }

        int total = memberFolders.size();

        // Scan reference member first
        QMap<QString, CallInfo> refCalls;
        for (const auto& [id, path] : memberFolders) {
            if (id == refId) {
                refCalls = scanMemberFolder(path);
                m_masterCallList = refCalls.keys();
                std::sort(m_masterCallList.begin(), m_masterCallList.end());
                break;
            }
        }

        // Scan all members
        QList<MemberAudit> results;
        int current = 0;
        for (const auto& [id, path] : memberFolders) {
            current++;
            QMetaObject::invokeMethod(this, [this, id, current, total]() {
                m_auditProgressBar->setMaximum(total);
                m_auditProgressBar->setValue(current);
                m_auditSummaryLabel->setText(QString("Scanning %1 (%2/%3)...").arg(id).arg(current).arg(total));
                emit scanProgress(id, current, total);
            }, Qt::QueuedConnection);

            MemberAudit audit;
            audit.memberId = id;
            audit.displayName = id;
            audit.folderPath = path;
            audit.calls = scanMemberFolder(path);
            audit.totalFiles = 0;

            // Count missing items vs reference
            for (const QString& callName : m_masterCallList) {
                if (audit.calls.contains(callName)) {
                    const CallInfo& ref = refCalls[callName];
                    CallInfo& member = audit.calls[callName];
                    if (ref.hasVideo && !member.hasVideo) audit.missingCount++;
                    if (ref.hasAudio && !member.hasAudio) audit.missingCount++;
                    if (ref.hasDoc && !member.hasDoc) audit.missingCount++;
                    if (ref.hasAiSummary && !member.hasAiSummary) audit.missingCount++;
                    audit.totalFiles += member.actualFiles.size();
                } else {
                    // Entire call missing
                    const CallInfo& ref = refCalls[callName];
                    int refFileCount = (ref.hasVideo ? 1 : 0) + (ref.hasAudio ? 1 : 0)
                        + (ref.hasDoc ? 1 : 0) + (ref.hasAiSummary ? 1 : 0);
                    audit.missingCount += refFileCount;
                }
            }

            // Count oddly named files
            for (const auto& call : audit.calls) {
                audit.oddNameCount += call.oddFiles.size();
            }

            results.append(audit);
        }

        // Update UI on main thread
        QMetaObject::invokeMethod(this, [this, results]() {
            m_auditResults = results;
            m_isScanning = false;
            m_scanBtn->setEnabled(true);
            m_auditProgressBar->setVisible(false);
            populateAuditTable();
            updateAuditSummary();
            m_exportBtn->setEnabled(!m_auditResults.isEmpty());

            // Also populate organize tab
            detectOddNames();
            detectFFToReorganize();
            populateRenameTable();
            populateReorganizeTable();
        }, Qt::QueuedConnection);
    });
}

QMap<QString, CallInfo> ContentManagerPanel::scanMemberFolder(const QString& folderPath) {
    QMap<QString, CallInfo> calls;
    if (!m_megaApi) return calls;

    std::unique_ptr<mega::MegaNode> folderNode(m_megaApi->getNodeByPath(folderPath.toUtf8().constData()));
    if (!folderNode) return calls;

    std::unique_ptr<mega::MegaNodeList> children(m_megaApi->getChildren(folderNode.get()));
    if (!children) return calls;

    for (int i = 0; i < children->size(); ++i) {
        mega::MegaNode* child = children->get(i);
        if (!child) continue;

        QString name = QString::fromUtf8(child->getName());

        if (child->isFolder()) {
            // Subfolder — also scan inside it
            QString subPath = folderPath + "/" + name;
            std::unique_ptr<mega::MegaNodeList> subChildren(m_megaApi->getChildren(child));
            if (subChildren) {
                for (int j = 0; j < subChildren->size(); ++j) {
                    mega::MegaNode* subChild = subChildren->get(j);
                    if (subChild && !subChild->isFolder()) {
                        QString subName = QString::fromUtf8(subChild->getName());
                        QString baseName = extractCallBaseName(subName);
                        if (!baseName.isEmpty()) {
                            CallInfo& info = calls[baseName];
                            info.callName = baseName;
                            info.date = baseName.left(10);
                            info.actualFiles.append(subPath + "/" + subName);
                            if (subName.endsWith(".mp4")) info.hasVideo = true;
                            else if (subName.endsWith(".mp3")) info.hasAudio = true;
                            else if (subName.contains("AI Summary") && subName.endsWith(".pdf")) info.hasAiSummary = true;
                            else if (subName.endsWith(".pdf")) info.hasDoc = true;
                        }
                    }
                }
            }
            continue;
        }

        // File at root level
        if (isOddlyNamed(name)) {
            // Try to match to a known call
            QString baseName = extractCallBaseName(name);
            if (baseName.isEmpty()) baseName = "UNKNOWN";
            calls[baseName].oddFiles.append(folderPath + "/" + name);
            continue;
        }

        QString baseName = extractCallBaseName(name);
        if (baseName.isEmpty()) continue;

        CallInfo& info = calls[baseName];
        info.callName = baseName;
        info.date = baseName.left(10);
        info.actualFiles.append(folderPath + "/" + name);

        if (name.endsWith(".mp4")) info.hasVideo = true;
        else if (name.endsWith(".mp3")) info.hasAudio = true;
        else if (name.contains("AI Summary") && name.endsWith(".pdf")) info.hasAiSummary = true;
        else if (name.endsWith(".pdf")) info.hasDoc = true;
    }

    return calls;
}

QString ContentManagerPanel::extractCallBaseName(const QString& filename) const {
    // Extract base call name from a filename
    // e.g., "03-25-2026 NHB Pro Theory Call - Amazing Secret.mp4"
    //    -> "03-25-2026 NHB Pro Theory Call - Amazing Secret"

    QString name = filename;

    // Remove extension
    int lastDot = name.lastIndexOf('.');
    if (lastDot > 0) name = name.left(lastDot);

    // Remove " AI Summary" suffix
    name.replace(" AI Summary", "");

    // Remove trailing whitespace
    name = name.trimmed();

    // Must start with date pattern MM-DD-YYYY
    QRegularExpression dateRe("^\\d{2}-\\d{2}-\\d{4}");
    if (!dateRe.match(name).hasMatch()) return QString();

    return name;
}

bool ContentManagerPanel::isOddlyNamed(const QString& filename) const {
    // Oddly named files don't follow MM-DD-YYYY prefix convention
    QRegularExpression dateRe("^\\d{2}-\\d{2}-\\d{4}");
    if (dateRe.match(filename).hasMatch()) return false;

    // Known odd patterns
    if (filename.startsWith("CallNotes")) return true;
    if (filename.startsWith("NHBPro-")) return true;
    if (filename.startsWith("UGC")) return true;
    if (QRegularExpression("^\\d{2}-\\d{2}-\\d{2}[A-Z]").match(filename).hasMatch()) return true; // e.g., 03-26-26NHBPro...

    return false;
}

// ==================== Audit Table ====================

void ContentManagerPanel::populateAuditTable() {
    auto& tm = ThemeManager::instance();

    if (m_masterCallList.isEmpty() || m_auditResults.isEmpty()) {
        m_auditTable->setRowCount(0);
        m_auditTable->setColumnCount(0);
        return;
    }

    // Columns: Member | call1 | call2 | ...
    int colCount = 1 + m_masterCallList.size();
    m_auditTable->setColumnCount(colCount);

    QStringList headers;
    headers << "Member";
    for (const QString& call : m_masterCallList) {
        // Shorten call name for header: "03-25 Amazing..."
        QString shortName = call.left(5) + " " + call.mid(35).left(15) + "...";
        if (call.length() < 50) shortName = call.left(30);
        headers << shortName;
    }
    m_auditTable->setHorizontalHeaderLabels(headers);

    m_auditTable->setRowCount(m_auditResults.size());

    for (int row = 0; row < m_auditResults.size(); ++row) {
        const MemberAudit& audit = m_auditResults[row];

        // Member name
        auto* memberItem = new QTableWidgetItem(audit.displayName);
        if (audit.memberId == m_referenceMemberId) {
            QFont boldFont = memberItem->font();
            boldFont.setBold(true);
            memberItem->setFont(boldFont);
            memberItem->setText(audit.displayName + " (ref)");
        }
        m_auditTable->setItem(row, 0, memberItem);

        // Each call column
        for (int col = 0; col < m_masterCallList.size(); ++col) {
            const QString& callName = m_masterCallList[col];

            QString status;
            QColor bgColor;

            if (audit.calls.contains(callName)) {
                const CallInfo& info = audit.calls[callName];
                // Build status: V=video, A=audio, D=doc, S=summary
                status += info.hasVideo ? QString::fromUtf8("\xe2\x9c\x93") : QString::fromUtf8("\xe2\x9c\x97");
                status += info.hasAudio ? QString::fromUtf8("\xe2\x9c\x93") : QString::fromUtf8("\xe2\x9c\x97");
                status += info.hasDoc ? QString::fromUtf8("\xe2\x9c\x93") : QString::fromUtf8("\xe2\x9c\x97");
                status += info.hasAiSummary ? QString::fromUtf8("\xe2\x9c\x93") : QString::fromUtf8("\xe2\x9c\x97");

                bool complete = info.hasVideo && info.hasAudio && info.hasDoc && info.hasAiSummary;
                bool partial = info.hasVideo || info.hasAudio || info.hasDoc || info.hasAiSummary;

                if (complete) bgColor = QColor(200, 255, 200, 80);
                else if (partial) bgColor = QColor(255, 255, 200, 80);
                else bgColor = QColor(255, 200, 200, 80);
            } else {
                status = QString::fromUtf8("\xe2\x9c\x97\xe2\x9c\x97\xe2\x9c\x97\xe2\x9c\x97");
                bgColor = QColor(255, 200, 200, 80);
            }

            auto* item = new QTableWidgetItem(status);
            item->setTextAlignment(Qt::AlignCenter);
            item->setBackground(bgColor);
            item->setToolTip(QString("V A D S\n%1").arg(callName));
            m_auditTable->setItem(row, 1 + col, item);
        }
    }

    // Resize first column
    m_auditTable->resizeColumnToContents(0);
    for (int c = 1; c < colCount; ++c) {
        m_auditTable->setColumnWidth(c, DpiScaler::scale(80));
    }
}

void ContentManagerPanel::updateAuditSummary() {
    int totalMissing = 0;
    int membersWithIssues = 0;
    for (const MemberAudit& a : m_auditResults) {
        if (a.memberId == m_referenceMemberId) continue;
        totalMissing += a.missingCount;
        if (a.missingCount > 0) membersWithIssues++;
    }

    m_auditSummaryLabel->setText(
        QString("Audit complete: %1 missing items across %2 members | %3 calls scanned | Legend: V=Video A=Audio D=Doc S=AI Summary")
            .arg(totalMissing).arg(membersWithIssues).arg(m_masterCallList.size()));

    emit scanCompleted(totalMissing);
}

// ==================== Export ====================

void ContentManagerPanel::onExportMissing() {
    QString filePath = QFileDialog::getSaveFileName(this, "Export Missing List",
        "missing_content.md", "Markdown (*.md);;All Files (*)");
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Error", "Cannot write to file: " + filePath);
        return;
    }

    QTextStream out(&file);
    out << "# Missing Content Report\n\n";
    out << "Reference: " << m_referenceMemberId << "\n\n";

    for (const MemberAudit& audit : m_auditResults) {
        if (audit.memberId == m_referenceMemberId) continue;
        if (audit.missingCount == 0) continue;

        out << "## " << audit.displayName << " (" << audit.missingCount << " missing)\n\n";

        for (const QString& callName : m_masterCallList) {
            if (!audit.calls.contains(callName)) {
                out << "- **" << callName << "** — ENTIRELY MISSING\n";
                continue;
            }
            const CallInfo& info = audit.calls[callName];
            QStringList missing;
            if (!info.hasVideo) missing << "VIDEO";
            if (!info.hasAudio) missing << "AUDIO";
            if (!info.hasDoc) missing << "DOC";
            if (!info.hasAiSummary) missing << "AI SUMMARY";
            if (!missing.isEmpty()) {
                out << "- " << callName << " — missing: " << missing.join(", ") << "\n";
            }
        }
        out << "\n";
    }

    file.close();
    QMessageBox::information(this, "Exported", "Missing list saved to:\n" + filePath);
}

// ==================== Organize Logic ====================

void ContentManagerPanel::detectOddNames() {
    m_renameQueue.clear();

    for (const MemberAudit& audit : m_auditResults) {
        for (auto it = audit.calls.constBegin(); it != audit.calls.constEnd(); ++it) {
            for (const QString& oddFile : it.value().oddFiles) {
                RenameItem item;
                item.memberId = audit.memberId;
                item.currentPath = oddFile;
                item.currentName = oddFile.section('/', -1);
                item.newName = suggestProperName(item.currentName, audit);
                item.selected = true;
                m_renameQueue.append(item);
            }
        }
    }
}

void ContentManagerPanel::detectFFToReorganize() {
    m_reorganizeQueue.clear();

    for (const MemberAudit& audit : m_auditResults) {
        // Find flat FF files (not already in subfolders)
        QMap<QString, QStringList> ffGroups;
        for (auto it = audit.calls.constBegin(); it != audit.calls.constEnd(); ++it) {
            const CallInfo& call = it.value();
            for (const QString& file : call.actualFiles) {
                QString name = file.section('/', -1);
                // Check if it's an FF file at root level (not in subfolder)
                QString parentDir = file.section('/', -2, -2);
                if (parentDir == audit.memberId &&
                    (name.contains("FF Hot Seat") || name.contains("FF Theory Call"))) {
                    QString baseName = extractCallBaseName(name);
                    if (!baseName.isEmpty()) {
                        ffGroups[baseName].append(file);
                    }
                }
            }
        }

        for (auto it = ffGroups.constBegin(); it != ffGroups.constEnd(); ++it) {
            ReorganizeItem item;
            item.memberId = audit.memberId;
            item.baseName = it.key();
            item.filePaths = it.value();
            item.targetFolder = audit.folderPath + "/" + it.key();
            item.selected = true;
            m_reorganizeQueue.append(item);
        }
    }
}

QString ContentManagerPanel::suggestProperName(const QString& oddName, const MemberAudit& audit) const {
    Q_UNUSED(audit);
    // Return empty for now — proper name suggestion requires call context
    // This will be enhanced with the rename rules from the plan
    return oddName; // Placeholder
}

void ContentManagerPanel::populateRenameTable() {
    m_renameTable->setRowCount(m_renameQueue.size());

    for (int i = 0; i < m_renameQueue.size(); ++i) {
        const RenameItem& item = m_renameQueue[i];

        auto* check = new QCheckBox();
        check->setChecked(item.selected);
        connect(check, &QCheckBox::toggled, this, [this, i](bool checked) {
            if (i < m_renameQueue.size()) m_renameQueue[i].selected = checked;
        });
        auto* checkWidget = new QWidget();
        auto* checkLayout = new QHBoxLayout(checkWidget);
        checkLayout->addWidget(check);
        checkLayout->setAlignment(Qt::AlignCenter);
        checkLayout->setContentsMargins(0, 0, 0, 0);
        m_renameTable->setCellWidget(i, 0, checkWidget);

        m_renameTable->setItem(i, 1, new QTableWidgetItem(item.memberId));
        m_renameTable->setItem(i, 2, new QTableWidgetItem(item.currentName));

        auto* newNameItem = new QTableWidgetItem(item.newName);
        // Editable — user can change the proposed name
        m_renameTable->setItem(i, 3, newNameItem);
    }

    m_renameSummaryLabel->setText(QString("%1 oddly-named files detected.").arg(m_renameQueue.size()));
    m_applyRenameBtn->setEnabled(!m_renameQueue.isEmpty());
}

void ContentManagerPanel::populateReorganizeTable() {
    m_reorganizeTable->setRowCount(m_reorganizeQueue.size());

    for (int i = 0; i < m_reorganizeQueue.size(); ++i) {
        const ReorganizeItem& item = m_reorganizeQueue[i];

        auto* check = new QCheckBox();
        check->setChecked(item.selected);
        connect(check, &QCheckBox::toggled, this, [this, i](bool checked) {
            if (i < m_reorganizeQueue.size()) m_reorganizeQueue[i].selected = checked;
        });
        auto* checkWidget = new QWidget();
        auto* checkLayout = new QHBoxLayout(checkWidget);
        checkLayout->addWidget(check);
        checkLayout->setAlignment(Qt::AlignCenter);
        checkLayout->setContentsMargins(0, 0, 0, 0);
        m_reorganizeTable->setCellWidget(i, 0, checkWidget);

        m_reorganizeTable->setItem(i, 1, new QTableWidgetItem(item.memberId));
        m_reorganizeTable->setItem(i, 2, new QTableWidgetItem(
            QString("%1 files").arg(item.filePaths.size())));

        auto* targetItem = new QTableWidgetItem(item.targetFolder.section('/', -1));
        targetItem->setToolTip(item.targetFolder);
        m_reorganizeTable->setItem(i, 3, targetItem);
    }

    m_reorganizeSummaryLabel->setText(QString("%1 FF file groups to reorganize.").arg(m_reorganizeQueue.size()));
    m_applyReorganizeBtn->setEnabled(!m_reorganizeQueue.isEmpty());
}

// ==================== Apply Operations ====================

void ContentManagerPanel::onApplyRenames() {
    if (!m_megaApi) return;

    int count = 0;
    for (const RenameItem& item : m_renameQueue) {
        if (!item.selected || item.newName == item.currentName) continue;
        count++;
    }

    if (count == 0) {
        QMessageBox::information(this, "No Changes", "No renames to apply.");
        return;
    }

    if (QMessageBox::question(this, "Confirm Rename",
            QString("Rename %1 files?").arg(count),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    m_organizeProgressBar->setVisible(true);
    m_organizeProgressBar->setMaximum(count);
    m_organizeStatusLabel->setVisible(true);

    int done = 0, failed = 0;
    for (int i = 0; i < m_renameQueue.size(); ++i) {
        const RenameItem& item = m_renameQueue[i];
        if (!item.selected || item.newName == item.currentName) continue;

        // Read back edited name from table
        QTableWidgetItem* newNameItem = m_renameTable->item(i, 3);
        QString newName = newNameItem ? newNameItem->text().trimmed() : item.newName;
        if (newName.isEmpty() || newName == item.currentName) continue;

        std::unique_ptr<mega::MegaNode> node(m_megaApi->getNodeByPath(item.currentPath.toUtf8().constData()));
        if (!node) { failed++; continue; }

        auto* listener = new mega::SynchronousRequestListener();
        m_megaApi->renameNode(node.get(), newName.toUtf8().constData(), listener);
        listener->wait();
        if (listener->getError()->getErrorCode() == mega::MegaError::API_OK) {
            done++;
        } else {
            failed++;
        }
        delete listener;

        m_organizeProgressBar->setValue(done + failed);
        m_organizeStatusLabel->setText(QString("Renaming: %1/%2 done, %3 failed")
            .arg(done).arg(count).arg(failed));
        QApplication::processEvents();
    }

    m_organizeProgressBar->setVisible(false);
    m_organizeStatusLabel->setText(QString("Rename complete: %1 succeeded, %2 failed").arg(done).arg(failed));

    QMessageBox::information(this, "Rename Complete",
        QString("Renamed: %1\nFailed: %2").arg(done).arg(failed));
}

void ContentManagerPanel::onApplyReorganize() {
    if (!m_megaApi) return;

    int count = 0;
    for (const ReorganizeItem& item : m_reorganizeQueue) {
        if (item.selected) count++;
    }

    if (count == 0) {
        QMessageBox::information(this, "No Changes", "No reorganization to apply.");
        return;
    }

    if (QMessageBox::question(this, "Confirm Reorganize",
            QString("Move files into %1 subfolders?").arg(count),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    m_organizeProgressBar->setVisible(true);
    m_organizeProgressBar->setMaximum(count);
    m_organizeStatusLabel->setVisible(true);

    int done = 0, failed = 0;
    for (const ReorganizeItem& item : m_reorganizeQueue) {
        if (!item.selected) continue;

        // Get parent folder
        QString parentPath = item.targetFolder.section('/', 0, -2);
        std::unique_ptr<mega::MegaNode> parentNode(m_megaApi->getNodeByPath(parentPath.toUtf8().constData()));
        if (!parentNode) { failed++; continue; }

        // Create subfolder
        QString folderName = item.targetFolder.section('/', -1);
        auto* listener = new mega::SynchronousRequestListener();
        m_megaApi->createFolder(folderName.toUtf8().constData(), parentNode.get(), listener);
        listener->wait();
        delete listener;

        // Get the created folder
        std::unique_ptr<mega::MegaNode> subFolder(
            m_megaApi->getChildNode(parentNode.get(), folderName.toUtf8().constData()));
        if (!subFolder) { failed++; continue; }

        // Move each file
        bool allMoved = true;
        for (const QString& filePath : item.filePaths) {
            std::unique_ptr<mega::MegaNode> fileNode(m_megaApi->getNodeByPath(filePath.toUtf8().constData()));
            if (!fileNode) { allMoved = false; continue; }

            auto* mvListener = new mega::SynchronousRequestListener();
            m_megaApi->moveNode(fileNode.get(), subFolder.get(), mvListener);
            mvListener->wait();
            if (mvListener->getError()->getErrorCode() != mega::MegaError::API_OK) {
                allMoved = false;
            }
            delete mvListener;
        }

        if (allMoved) done++;
        else failed++;

        m_organizeProgressBar->setValue(done + failed);
        m_organizeStatusLabel->setText(QString("Reorganizing: %1/%2 done, %3 failed")
            .arg(done).arg(count).arg(failed));
        QApplication::processEvents();
    }

    m_organizeProgressBar->setVisible(false);
    m_organizeStatusLabel->setText(QString("Reorganize complete: %1 succeeded, %2 failed").arg(done).arg(failed));

    QMessageBox::information(this, "Reorganize Complete",
        QString("Organized: %1 groups\nFailed: %2").arg(done).arg(failed));
}

} // namespace MegaCustom
