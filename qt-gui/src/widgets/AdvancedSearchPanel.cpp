#include "AdvancedSearchPanel.h"
#include "IconProvider.h"
#include "LoadingSpinner.h"
#include "dialogs/BulkNameEditorDialog.h"
#include <algorithm>
#include <QPainter>
#include <QApplication>
#include <QScrollBar>
#include <QMouseEvent>
#include <QDebug>
#include <QStyle>
#include <QHeaderView>
#include <QElapsedTimer>
#include <QMessageBox>

namespace MegaCustom {

// ============================================================================
// AdvancedSearchResultDelegate
// ============================================================================

AdvancedSearchResultDelegate::AdvancedSearchResultDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

QRect AdvancedSearchResultDelegate::checkboxRect(const QStyleOptionViewItem& option) const
{
    return QRect(option.rect.left() + 8, option.rect.top() + (option.rect.height() - 18) / 2, 18, 18);
}

void AdvancedSearchResultDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                          const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    QRect rect = option.rect;
    bool isSelected = option.state & QStyle::State_Selected;
    bool isHovered = option.state & QStyle::State_MouseOver;
    bool isChecked = index.data(CheckedRole).toBool();

    // Background
    if (isSelected) {
        painter->fillRect(rect, QColor(221, 20, 5, 26)); // MEGA Red 10%
    } else if (isHovered) {
        painter->fillRect(rect, QColor(0, 0, 0, 13)); // Light hover
    }

    // Checkbox
    QRect cbRect = checkboxRect(option);
    painter->setPen(QColor(220, 221, 221));
    painter->setBrush(isChecked ? QColor(221, 20, 5) : Qt::white);
    painter->drawRoundedRect(cbRect, 3, 3);
    if (isChecked) {
        painter->setPen(QPen(Qt::white, 2));
        painter->drawLine(cbRect.left() + 4, cbRect.center().y(),
                          cbRect.center().x(), cbRect.bottom() - 4);
        painter->drawLine(cbRect.center().x(), cbRect.bottom() - 4,
                          cbRect.right() - 3, cbRect.top() + 5);
    }

    // Get data
    QString name = index.data(NameRole).toString();
    QString path = index.data(PathRole).toString();
    qint64 size = index.data(SizeRole).toLongLong();
    qint64 date = index.data(DateRole).toLongLong();
    bool isFolder = index.data(IsFolderRole).toBool();

    // Icon area (32x32) - after checkbox
    QRect iconRect = rect;
    iconRect.setLeft(cbRect.right() + 8);
    iconRect.setWidth(32);
    iconRect.adjust(0, 10, 0, -10);

    // Draw icon
    QIcon icon = isFolder ? IconProvider::instance().icon("folder")
                          : IconProvider::instance().icon("file");
    icon.paint(painter, iconRect, Qt::AlignCenter);

    // Text area
    QRect textRect = rect;
    textRect.setLeft(iconRect.right() + 8);
    textRect.setRight(rect.right() - 160); // Leave room for size/date

    // Name (bold) with highlighting
    QFont nameFont = option.font;
    nameFont.setBold(true);
    painter->setFont(nameFont);
    QColor nameColor = isSelected ? QColor(123, 33, 24) : QColor(50, 50, 50);

    QRect nameRect = textRect;
    nameRect.setHeight(textRect.height() / 2);

    // Get match spans for highlighting
    QVariantList matchList = index.data(NameMatchesRole).toList();

    if (matchList.isEmpty()) {
        // No highlights, just draw normally
        painter->setPen(nameColor);
        painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignBottom, name);
    } else {
        // Draw name with highlighted matches
        QFontMetrics fm(nameFont);
        int x = nameRect.left();
        int y = nameRect.bottom() - fm.descent();

        // Sort matches by start position
        QVector<QPair<int, int>> spans;
        for (const QVariant& v : matchList) {
            QVariantMap m = v.toMap();
            spans.append({m["start"].toInt(), m["length"].toInt()});
        }
        std::sort(spans.begin(), spans.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });

        // Draw character by character with highlights
        int pos = 0;
        for (const auto& span : spans) {
            // Draw non-highlighted part before this match
            if (span.first > pos) {
                QString before = name.mid(pos, span.first - pos);
                painter->setPen(nameColor);
                painter->drawText(x, y, before);
                x += fm.horizontalAdvance(before);
            }

            // Draw highlighted match
            QString matchText = name.mid(span.first, span.second);
            int matchWidth = fm.horizontalAdvance(matchText);

            // Draw yellow background
            QRect highlightRect(x, nameRect.top() + (nameRect.height() - fm.height()) / 2 + fm.height() / 4,
                                matchWidth, fm.height());
            painter->fillRect(highlightRect, QColor(255, 245, 157)); // Yellow highlight

            // Draw text
            painter->setPen(nameColor);
            painter->drawText(x, y, matchText);
            x += matchWidth;

            pos = span.first + span.second;
        }

        // Draw remaining text after last match
        if (pos < name.length()) {
            QString after = name.mid(pos);
            painter->setPen(nameColor);
            painter->drawText(x, y, after);
        }
    }

    // Path (smaller, gray)
    QFont pathFont = option.font;
    pathFont.setPointSize(pathFont.pointSize() - 1);
    painter->setFont(pathFont);
    painter->setPen(QColor(128, 128, 128));

    QRect pathRect = textRect;
    pathRect.setTop(nameRect.bottom());
    QString displayPath = path;
    if (displayPath.length() > 70) {
        displayPath = "..." + displayPath.right(67);
    }
    painter->drawText(pathRect, Qt::AlignLeft | Qt::AlignTop, displayPath);

    // Size and date area (right side)
    QRect infoRect = rect;
    infoRect.setLeft(textRect.right() + 8);
    infoRect.setRight(rect.right() - 12);

    painter->setFont(pathFont);
    painter->setPen(QColor(100, 100, 100));

    // Size
    QRect sizeRect = infoRect;
    sizeRect.setHeight(infoRect.height() / 2);
    QString sizeStr = isFolder ? "--" : formatSize(size);
    painter->drawText(sizeRect, Qt::AlignRight | Qt::AlignBottom, sizeStr);

    // Date
    QRect dateRect = infoRect;
    dateRect.setTop(sizeRect.bottom());
    painter->drawText(dateRect, Qt::AlignRight | Qt::AlignTop, formatDate(date));

    painter->restore();
}

QSize AdvancedSearchResultDelegate::sizeHint(const QStyleOptionViewItem& option,
                                              const QModelIndex& index) const
{
    Q_UNUSED(option)
    Q_UNUSED(index)
    return QSize(0, 56); // Slightly taller to accommodate checkbox
}

bool AdvancedSearchResultDelegate::editorEvent(QEvent* event, QAbstractItemModel* model,
                                                const QStyleOptionViewItem& option,
                                                const QModelIndex& index)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        QRect cbRect = checkboxRect(option);
        if (cbRect.contains(mouseEvent->pos())) {
            bool currentValue = index.data(CheckedRole).toBool();
            model->setData(index, !currentValue, CheckedRole);
            return true;
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

QString AdvancedSearchResultDelegate::formatSize(qint64 bytes) const
{
    if (bytes < 1024) {
        return QString("%1 B").arg(bytes);
    } else if (bytes < 1024 * 1024) {
        return QString("%1 KB").arg(bytes / 1024);
    } else if (bytes < 1024LL * 1024 * 1024) {
        return QString("%1 MB").arg(bytes / (1024 * 1024));
    } else {
        double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
        return QString("%1 GB").arg(gb, 0, 'f', 1);
    }
}

QString AdvancedSearchResultDelegate::formatDate(qint64 timestamp) const
{
    if (timestamp <= 0) return "--";
    QDateTime dt = QDateTime::fromSecsSinceEpoch(timestamp);
    QDateTime now = QDateTime::currentDateTime();

    if (dt.date() == now.date()) {
        return dt.toString("h:mm AP");
    } else if (dt.date().year() == now.date().year()) {
        return dt.toString("MMM d");
    } else {
        return dt.toString("MMM d, yyyy");
    }
}

// ============================================================================
// AdvancedSearchPanel
// ============================================================================

AdvancedSearchPanel::AdvancedSearchPanel(QWidget* parent)
    : QWidget(parent)
    , m_searchIndex(nullptr)
    , m_indexingSpinner(nullptr)
    , m_sortField(SortField::Relevance)
    , m_sortOrder(SortOrder::Descending)
{
    setupUI();
    applyStyles();

    // Debounce timer for search
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    connect(m_searchTimer, &QTimer::timeout, this, &AdvancedSearchPanel::executeSearch);
}

AdvancedSearchPanel::~AdvancedSearchPanel() = default;

void AdvancedSearchPanel::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(24, 24, 24, 24);
    m_mainLayout->setSpacing(16);

    // Title
    QLabel* titleLabel = new QLabel("Advanced Search", this);
    titleLabel->setObjectName("panelTitle");
    m_mainLayout->addWidget(titleLabel);

    setupSearchSection();
    setupFiltersSection();
    setupSortSection();
    setupResultsSection();
    setupActionsSection();
    setupStatusSection();
}

void AdvancedSearchPanel::setupSearchSection()
{
    QHBoxLayout* searchLayout = new QHBoxLayout();
    searchLayout->setSpacing(8);

    QLabel* searchLabel = new QLabel("Search:", this);
    searchLayout->addWidget(searchLabel);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Enter search terms, e.g., report.pdf, *.mp4, ext:pdf");
    m_searchEdit->setClearButtonEnabled(true);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &AdvancedSearchPanel::onSearchTextChanged);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &AdvancedSearchPanel::executeSearch);
    searchLayout->addWidget(m_searchEdit, 1);

    m_searchBtn = new QPushButton("Search", this);
    m_searchBtn->setProperty("type", "primary");
    m_searchBtn->setProperty("dimension", "medium");
    connect(m_searchBtn, &QPushButton::clicked, this, &AdvancedSearchPanel::onSearchButtonClicked);
    searchLayout->addWidget(m_searchBtn);

    m_mainLayout->addLayout(searchLayout);
}

void AdvancedSearchPanel::setupFiltersSection()
{
    m_filtersGroup = new QGroupBox("Filters", this);
    QGridLayout* filtersLayout = new QGridLayout(m_filtersGroup);
    filtersLayout->setHorizontalSpacing(16);
    filtersLayout->setVerticalSpacing(12);

    int row = 0;

    // Type filter
    QLabel* typeLabel = new QLabel("Type:", this);
    filtersLayout->addWidget(typeLabel, row, 0);

    QHBoxLayout* typeLayout = new QHBoxLayout();
    m_typeFilterGroup = new QButtonGroup(this);
    m_typeAllRadio = new QRadioButton("All", this);
    m_typeFilesRadio = new QRadioButton("Files", this);
    m_typeFoldersRadio = new QRadioButton("Folders", this);
    m_typeAllRadio->setChecked(true);
    m_typeFilterGroup->addButton(m_typeAllRadio, 0);
    m_typeFilterGroup->addButton(m_typeFilesRadio, 1);
    m_typeFilterGroup->addButton(m_typeFoldersRadio, 2);
    typeLayout->addWidget(m_typeAllRadio);
    typeLayout->addWidget(m_typeFilesRadio);
    typeLayout->addWidget(m_typeFoldersRadio);
    typeLayout->addStretch();
    connect(m_typeFilterGroup, &QButtonGroup::idClicked, this, &AdvancedSearchPanel::onTypeFilterChanged);
    filtersLayout->addLayout(typeLayout, row, 1, 1, 3);
    row++;

    // Extension filter
    QLabel* extLabel = new QLabel("Extension:", this);
    filtersLayout->addWidget(extLabel, row, 0);

    m_extensionEdit = new QLineEdit(this);
    m_extensionEdit->setPlaceholderText("e.g., pdf,docx,xlsx (comma-separated)");
    connect(m_extensionEdit, &QLineEdit::textChanged, this, &AdvancedSearchPanel::onExtensionFilterChanged);
    filtersLayout->addWidget(m_extensionEdit, row, 1, 1, 3);
    row++;

    // Size filter
    QLabel* sizeLabel = new QLabel("Size:", this);
    filtersLayout->addWidget(sizeLabel, row, 0);

    QHBoxLayout* sizeLayout = new QHBoxLayout();
    m_sizeMinSpin = new QSpinBox(this);
    m_sizeMinSpin->setRange(0, 999999);
    m_sizeMinSpin->setSpecialValueText("Min");
    connect(m_sizeMinSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &AdvancedSearchPanel::onSizeFilterChanged);
    sizeLayout->addWidget(m_sizeMinSpin);

    m_sizeMinUnitCombo = new QComboBox(this);
    m_sizeMinUnitCombo->addItems({"B", "KB", "MB", "GB"});
    m_sizeMinUnitCombo->setCurrentIndex(2); // MB default
    connect(m_sizeMinUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AdvancedSearchPanel::onSizeFilterChanged);
    sizeLayout->addWidget(m_sizeMinUnitCombo);

    sizeLayout->addWidget(new QLabel("to", this));

    m_sizeMaxSpin = new QSpinBox(this);
    m_sizeMaxSpin->setRange(0, 999999);
    m_sizeMaxSpin->setSpecialValueText("Max");
    connect(m_sizeMaxSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &AdvancedSearchPanel::onSizeFilterChanged);
    sizeLayout->addWidget(m_sizeMaxSpin);

    m_sizeMaxUnitCombo = new QComboBox(this);
    m_sizeMaxUnitCombo->addItems({"B", "KB", "MB", "GB"});
    m_sizeMaxUnitCombo->setCurrentIndex(3); // GB default
    connect(m_sizeMaxUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AdvancedSearchPanel::onSizeFilterChanged);
    sizeLayout->addWidget(m_sizeMaxUnitCombo);
    sizeLayout->addStretch();

    filtersLayout->addLayout(sizeLayout, row, 1, 1, 3);
    row++;

    // Date filter
    QLabel* dateLabel = new QLabel("Modified:", this);
    filtersLayout->addWidget(dateLabel, row, 0);

    QHBoxLayout* dateLayout = new QHBoxLayout();
    m_datePresetCombo = new QComboBox(this);
    m_datePresetCombo->addItems({"Any time", "Today", "Yesterday", "This week", "This month", "This year", "Custom range..."});
    connect(m_datePresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AdvancedSearchPanel::onDateFilterChanged);
    dateLayout->addWidget(m_datePresetCombo);

    m_dateFromEdit = new QDateEdit(this);
    m_dateFromEdit->setCalendarPopup(true);
    m_dateFromEdit->setDisplayFormat("yyyy-MM-dd");
    m_dateFromEdit->setVisible(false);
    connect(m_dateFromEdit, &QDateEdit::dateChanged, this, &AdvancedSearchPanel::onDateFilterChanged);
    dateLayout->addWidget(m_dateFromEdit);

    dateLayout->addWidget(new QLabel("to", this));

    m_dateToEdit = new QDateEdit(this);
    m_dateToEdit->setCalendarPopup(true);
    m_dateToEdit->setDisplayFormat("yyyy-MM-dd");
    m_dateToEdit->setDate(QDate::currentDate());
    m_dateToEdit->setVisible(false);
    connect(m_dateToEdit, &QDateEdit::dateChanged, this, &AdvancedSearchPanel::onDateFilterChanged);
    dateLayout->addWidget(m_dateToEdit);
    dateLayout->addStretch();

    filtersLayout->addLayout(dateLayout, row, 1, 1, 3);
    row++;

    // Path filter
    QLabel* pathLabel = new QLabel("Path contains:", this);
    filtersLayout->addWidget(pathLabel, row, 0);

    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setPlaceholderText("e.g., Documents/Work");
    connect(m_pathEdit, &QLineEdit::textChanged, this, &AdvancedSearchPanel::onPathFilterChanged);
    filtersLayout->addWidget(m_pathEdit, row, 1, 1, 2);

    // Regex toggle
    m_regexCheck = new QCheckBox("Use Regex", this);
    connect(m_regexCheck, &QCheckBox::toggled, this, &AdvancedSearchPanel::onRegexToggled);
    filtersLayout->addWidget(m_regexCheck, row, 3);

    m_mainLayout->addWidget(m_filtersGroup);
}

void AdvancedSearchPanel::setupSortSection()
{
    QHBoxLayout* sortLayout = new QHBoxLayout();
    sortLayout->setSpacing(12);

    QLabel* sortLabel = new QLabel("Sort by:", this);
    sortLayout->addWidget(sortLabel);

    m_sortCombo = new QComboBox(this);
    m_sortCombo->addItem("Relevance", static_cast<int>(SortField::Relevance));
    m_sortCombo->addItem("Name", static_cast<int>(SortField::Name));
    m_sortCombo->addItem("Size", static_cast<int>(SortField::Size));
    m_sortCombo->addItem("Date Modified", static_cast<int>(SortField::DateModified));
    m_sortCombo->addItem("Type", static_cast<int>(SortField::Type));
    m_sortCombo->setFixedWidth(140);
    connect(m_sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AdvancedSearchPanel::onSortFieldChanged);
    sortLayout->addWidget(m_sortCombo);

    m_sortOrderBtn = new QPushButton(this);
    m_sortOrderBtn->setFixedSize(32, 32);
    m_sortOrderBtn->setIcon(QIcon(":/icons/arrow-down.svg"));
    m_sortOrderBtn->setIconSize(QSize(18, 18));
    m_sortOrderBtn->setToolTip("Sort Descending");
    connect(m_sortOrderBtn, &QPushButton::clicked, this, &AdvancedSearchPanel::onSortOrderToggled);
    sortLayout->addWidget(m_sortOrderBtn);

    sortLayout->addStretch();

    m_resultsCountLabel = new QLabel("Ready", this);
    m_resultsCountLabel->setStyleSheet("color: #616366;");
    sortLayout->addWidget(m_resultsCountLabel);

    m_mainLayout->addLayout(sortLayout);
}

void AdvancedSearchPanel::setupResultsSection()
{
    m_resultsList = new QListView(this);
    m_model = new QStandardItemModel(this);
    m_delegate = new AdvancedSearchResultDelegate(this);

    m_resultsList->setModel(m_model);
    m_resultsList->setItemDelegate(m_delegate);
    m_resultsList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_resultsList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_resultsList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_resultsList->setMouseTracking(true);
    m_resultsList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_resultsList->setAlternatingRowColors(true);

    connect(m_resultsList, &QListView::doubleClicked, this, &AdvancedSearchPanel::onResultDoubleClicked);
    connect(m_resultsList, &QListView::customContextMenuRequested, this, &AdvancedSearchPanel::onResultContextMenu);
    connect(m_resultsList->selectionModel(), &QItemSelectionModel::selectionChanged, this, &AdvancedSearchPanel::onSelectionChanged);

    m_mainLayout->addWidget(m_resultsList, 1);
}

void AdvancedSearchPanel::setupActionsSection()
{
    QHBoxLayout* actionsLayout = new QHBoxLayout();
    actionsLayout->setSpacing(8);

    m_selectAllBtn = new QPushButton("Select All", this);
    m_selectAllBtn->setProperty("type", "outline");
    m_selectAllBtn->setProperty("dimension", "small");
    connect(m_selectAllBtn, &QPushButton::clicked, this, &AdvancedSearchPanel::onSelectAll);
    actionsLayout->addWidget(m_selectAllBtn);

    m_deselectAllBtn = new QPushButton("Deselect All", this);
    m_deselectAllBtn->setProperty("type", "outline");
    m_deselectAllBtn->setProperty("dimension", "small");
    connect(m_deselectAllBtn, &QPushButton::clicked, this, &AdvancedSearchPanel::onDeselectAll);
    actionsLayout->addWidget(m_deselectAllBtn);

    actionsLayout->addStretch();

    m_copyPathsBtn = new QPushButton("Copy Paths", this);
    m_copyPathsBtn->setProperty("type", "outline");
    m_copyPathsBtn->setProperty("dimension", "small");
    m_copyPathsBtn->setEnabled(false);
    connect(m_copyPathsBtn, &QPushButton::clicked, this, &AdvancedSearchPanel::onCopyPaths);
    actionsLayout->addWidget(m_copyPathsBtn);

    m_bulkRenameBtn = new QPushButton("Bulk Rename...", this);
    m_bulkRenameBtn->setProperty("type", "outline");
    m_bulkRenameBtn->setProperty("dimension", "small");
    m_bulkRenameBtn->setEnabled(false);
    connect(m_bulkRenameBtn, &QPushButton::clicked, this, &AdvancedSearchPanel::onBulkRename);
    actionsLayout->addWidget(m_bulkRenameBtn);

    m_goToLocationBtn = new QPushButton("Go to Location", this);
    m_goToLocationBtn->setProperty("type", "primary");
    m_goToLocationBtn->setProperty("dimension", "small");
    m_goToLocationBtn->setEnabled(false);
    connect(m_goToLocationBtn, &QPushButton::clicked, this, &AdvancedSearchPanel::onGoToLocation);
    actionsLayout->addWidget(m_goToLocationBtn);

    m_mainLayout->addLayout(actionsLayout);
}

void AdvancedSearchPanel::setupStatusSection()
{
    QHBoxLayout* statusLayout = new QHBoxLayout();

    // Indexing spinner
    m_indexingSpinner = new LoadingSpinner(this);
    m_indexingSpinner->setFixedSize(16, 16);
    m_indexingSpinner->hide();
    statusLayout->addWidget(m_indexingSpinner);

    m_indexStatusLabel = new QLabel("Index: Not loaded", this);
    m_indexStatusLabel->setStyleSheet("color: #999; font-size: 11px;");
    statusLayout->addWidget(m_indexStatusLabel);

    statusLayout->addStretch();

    m_mainLayout->addLayout(statusLayout);
}

void AdvancedSearchPanel::applyStyles()
{
    setStyleSheet(R"(
        QLabel#panelTitle {
            font-size: 24px;
            font-weight: bold;
            color: #303233;
            margin-bottom: 8px;
        }
        QGroupBox {
            font-weight: bold;
            border: 1px solid #DCDDDD;
            border-radius: 8px;
            margin-top: 12px;
            padding-top: 8px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 4px;
        }
        QListView {
            background-color: #FFFFFF;
            border: 1px solid #DCDDDD;
            border-radius: 8px;
        }
        QListView::item {
            border-bottom: 1px solid #F0F0F0;
        }
        QListView::item:selected {
            background-color: rgba(221, 20, 5, 0.1);
        }
        QListView::item:alternate {
            background-color: #FAFAFA;
        }
    )");
}

void AdvancedSearchPanel::setSearchIndex(CloudSearchIndex* index)
{
    if (m_searchIndex) {
        disconnect(m_searchIndex, nullptr, this, nullptr);
    }

    m_searchIndex = index;

    if (m_searchIndex) {
        connect(m_searchIndex, &CloudSearchIndex::indexingStarted,
                this, &AdvancedSearchPanel::updateIndexStatus);
        connect(m_searchIndex, &CloudSearchIndex::indexingFinished,
                this, &AdvancedSearchPanel::updateIndexStatus);
        connect(m_searchIndex, &CloudSearchIndex::indexingProgress,
                this, &AdvancedSearchPanel::updateIndexStatus);
    }

    updateIndexStatus();
}

void AdvancedSearchPanel::onSearchTextChanged(const QString& text)
{
    m_currentQuery = text.trimmed();

    // Start debounce timer
    m_searchTimer->stop();
    if (!m_currentQuery.isEmpty()) {
        m_searchTimer->start(SEARCH_DEBOUNCE_MS);
    } else {
        clearResults();
    }
}

void AdvancedSearchPanel::onSearchButtonClicked()
{
    m_searchTimer->stop();
    executeSearch();
}

void AdvancedSearchPanel::executeSearch()
{
    if (!m_searchIndex) {
        m_resultsCountLabel->setText("Index not available");
        return;
    }

    QString queryString = buildQueryString();
    if (queryString.isEmpty()) {
        clearResults();
        return;
    }

    QElapsedTimer timer;
    timer.start();

    // Execute search
    QVector<SearchResult> results = m_searchIndex->searchWithSort(
        queryString, m_sortField, m_sortOrder, MAX_RESULTS);

    // Populate results
    populateResults(results);

    // Update status
    m_resultsCountLabel->setText(QString("%1 results in %2 ms")
                                  .arg(results.size())
                                  .arg(timer.elapsed()));
}

QString AdvancedSearchPanel::buildQueryString() const
{
    QStringList parts;

    // Main search text
    if (!m_currentQuery.isEmpty()) {
        // If regex is enabled, wrap in regex: prefix
        if (m_regexCheck->isChecked()) {
            parts << QString("regex:%1").arg(m_currentQuery);
        } else {
            parts << m_currentQuery;
        }
    }

    // Type filter
    if (m_typeFilesRadio->isChecked()) {
        parts << "type:file";
    } else if (m_typeFoldersRadio->isChecked()) {
        parts << "type:folder";
    }

    // Extension filter
    QString extensions = m_extensionEdit->text().trimmed();
    if (!extensions.isEmpty()) {
        parts << QString("ext:%1").arg(extensions);
    }

    // Size filter
    if (m_sizeMinSpin->value() > 0) {
        QString unit = m_sizeMinUnitCombo->currentText().toLower();
        parts << QString("size:>%1%2").arg(m_sizeMinSpin->value()).arg(unit);
    }
    if (m_sizeMaxSpin->value() > 0) {
        QString unit = m_sizeMaxUnitCombo->currentText().toLower();
        parts << QString("size:<%1%2").arg(m_sizeMaxSpin->value()).arg(unit);
    }

    // Date filter
    int dateIndex = m_datePresetCombo->currentIndex();
    switch (dateIndex) {
    case 1: parts << "dm:today"; break;
    case 2: parts << "dm:yesterday"; break;
    case 3: parts << "dm:thisweek"; break;
    case 4: parts << "dm:thismonth"; break;
    case 5: parts << "dm:thisyear"; break;
    case 6: // Custom range
        if (m_dateFromEdit->isVisible()) {
            parts << QString("dm:>%1").arg(m_dateFromEdit->date().toString("yyyy-MM-dd"));
            parts << QString("dm:<%1").arg(m_dateToEdit->date().toString("yyyy-MM-dd"));
        }
        break;
    }

    // Path filter
    QString pathFilter = m_pathEdit->text().trimmed();
    if (!pathFilter.isEmpty()) {
        parts << QString("path:%1").arg(pathFilter);
    }

    return parts.join(" ");
}

void AdvancedSearchPanel::populateResults(const QVector<SearchResult>& results)
{
    m_model->clear();

    for (const SearchResult& result : results) {
        if (result.handle.isEmpty()) continue;

        QStandardItem* item = new QStandardItem();
        item->setData(result.name, AdvancedSearchResultDelegate::NameRole);
        item->setData(result.path, AdvancedSearchResultDelegate::PathRole);
        item->setData(result.size, AdvancedSearchResultDelegate::SizeRole);
        item->setData(result.modificationTime, AdvancedSearchResultDelegate::DateRole);
        item->setData(result.handle, AdvancedSearchResultDelegate::HandleRole);
        item->setData(result.isFolder, AdvancedSearchResultDelegate::IsFolderRole);
        item->setData(result.extension, AdvancedSearchResultDelegate::ExtensionRole);
        item->setData(result.relevanceScore, AdvancedSearchResultDelegate::RelevanceRole);
        item->setData(false, AdvancedSearchResultDelegate::CheckedRole);

        // Store match spans for highlighting
        QVariantList matchList;
        for (const MatchSpan& span : result.nameMatches) {
            QVariantMap match;
            match["start"] = span.start;
            match["length"] = span.length;
            matchList.append(match);
        }
        item->setData(matchList, AdvancedSearchResultDelegate::NameMatchesRole);

        item->setEditable(false);
        m_model->appendRow(item);
    }

    updateActionButtons();
}

void AdvancedSearchPanel::clearResults()
{
    m_model->clear();
    m_resultsCountLabel->setText("Ready");
    updateActionButtons();
}

void AdvancedSearchPanel::onTypeFilterChanged()
{
    m_searchTimer->stop();
    m_searchTimer->start(SEARCH_DEBOUNCE_MS);
}

void AdvancedSearchPanel::onExtensionFilterChanged()
{
    m_searchTimer->stop();
    m_searchTimer->start(SEARCH_DEBOUNCE_MS);
}

void AdvancedSearchPanel::onSizeFilterChanged()
{
    m_searchTimer->stop();
    m_searchTimer->start(SEARCH_DEBOUNCE_MS);
}

void AdvancedSearchPanel::onDateFilterChanged()
{
    int index = m_datePresetCombo->currentIndex();
    bool showCustom = (index == 6); // "Custom range..."
    m_dateFromEdit->setVisible(showCustom);
    m_dateToEdit->setVisible(showCustom);

    // Find the "to" label between the date edits and toggle its visibility
    // (This is a bit hacky - in production, store a pointer to the label)

    m_searchTimer->stop();
    m_searchTimer->start(SEARCH_DEBOUNCE_MS);
}

void AdvancedSearchPanel::onPathFilterChanged()
{
    m_searchTimer->stop();
    m_searchTimer->start(SEARCH_DEBOUNCE_MS);
}

void AdvancedSearchPanel::onRegexToggled(bool checked)
{
    Q_UNUSED(checked)
    m_searchTimer->stop();
    m_searchTimer->start(SEARCH_DEBOUNCE_MS);
}

void AdvancedSearchPanel::onSortFieldChanged(int index)
{
    m_sortField = static_cast<SortField>(m_sortCombo->itemData(index).toInt());
    executeSearch();
}

void AdvancedSearchPanel::onSortOrderToggled()
{
    m_sortOrder = (m_sortOrder == SortOrder::Ascending)
                  ? SortOrder::Descending : SortOrder::Ascending;

    if (m_sortOrder == SortOrder::Ascending) {
        m_sortOrderBtn->setIcon(QIcon(":/icons/arrow-up.svg"));
        m_sortOrderBtn->setToolTip("Sort Ascending");
    } else {
        m_sortOrderBtn->setIcon(QIcon(":/icons/arrow-down.svg"));
        m_sortOrderBtn->setToolTip("Sort Descending");
    }

    executeSearch();
}

void AdvancedSearchPanel::onResultDoubleClicked(const QModelIndex& index)
{
    if (!index.isValid()) return;

    QString handle = index.data(AdvancedSearchResultDelegate::HandleRole).toString();
    QString path = index.data(AdvancedSearchResultDelegate::PathRole).toString();
    bool isFolder = index.data(AdvancedSearchResultDelegate::IsFolderRole).toBool();

    emit navigateToPath(handle, path, isFolder);
}

void AdvancedSearchPanel::onResultContextMenu(const QPoint& pos)
{
    QModelIndex index = m_resultsList->indexAt(pos);
    if (!index.isValid()) return;

    QMenu menu(this);

    QAction* copyPathAction = menu.addAction("Copy Path");
    QAction* copyNameAction = menu.addAction("Copy Name");
    menu.addSeparator();
    QAction* goToAction = menu.addAction("Go to Location");

    QAction* selected = menu.exec(m_resultsList->viewport()->mapToGlobal(pos));

    if (selected == copyPathAction) {
        QString path = index.data(AdvancedSearchResultDelegate::PathRole).toString();
        QApplication::clipboard()->setText(path);
    } else if (selected == copyNameAction) {
        QString name = index.data(AdvancedSearchResultDelegate::NameRole).toString();
        QApplication::clipboard()->setText(name);
    } else if (selected == goToAction) {
        onResultDoubleClicked(index);
    }
}

void AdvancedSearchPanel::onSelectionChanged()
{
    updateActionButtons();
}

void AdvancedSearchPanel::updateActionButtons()
{
    int checkedCount = 0;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        if (m_model->item(i)->data(AdvancedSearchResultDelegate::CheckedRole).toBool()) {
            checkedCount++;
        }
    }

    bool hasResults = m_model->rowCount() > 0;
    bool hasChecked = checkedCount > 0;
    bool hasSingleSelection = m_resultsList->selectionModel()->selectedIndexes().size() == 1;

    m_selectAllBtn->setEnabled(hasResults);
    m_deselectAllBtn->setEnabled(hasChecked);
    m_copyPathsBtn->setEnabled(hasChecked);
    m_bulkRenameBtn->setEnabled(hasChecked && checkedCount >= 2);
    m_goToLocationBtn->setEnabled(hasSingleSelection || (hasChecked && checkedCount == 1));
}

void AdvancedSearchPanel::onSelectAll()
{
    for (int i = 0; i < m_model->rowCount(); ++i) {
        m_model->item(i)->setData(true, AdvancedSearchResultDelegate::CheckedRole);
    }
    m_resultsList->viewport()->update();
    updateActionButtons();
}

void AdvancedSearchPanel::onDeselectAll()
{
    for (int i = 0; i < m_model->rowCount(); ++i) {
        m_model->item(i)->setData(false, AdvancedSearchResultDelegate::CheckedRole);
    }
    m_resultsList->viewport()->update();
    updateActionButtons();
}

void AdvancedSearchPanel::onCopyPaths()
{
    QStringList paths = getSelectedPaths();
    if (!paths.isEmpty()) {
        QApplication::clipboard()->setText(paths.join("\n"));
    }
}

void AdvancedSearchPanel::onBulkRename()
{
    // Gather selected items' data
    QStringList paths;
    QStringList names;
    QList<bool> isFolders;

    for (int i = 0; i < m_model->rowCount(); ++i) {
        QStandardItem* item = m_model->item(i);
        if (item->data(AdvancedSearchResultDelegate::CheckedRole).toBool()) {
            paths << item->data(AdvancedSearchResultDelegate::PathRole).toString();
            names << item->data(AdvancedSearchResultDelegate::NameRole).toString();
            isFolders << item->data(AdvancedSearchResultDelegate::IsFolderRole).toBool();
        }
    }

    if (names.size() < 2) {
        QMessageBox::information(this, "Bulk Rename",
            "Please select at least 2 items to use bulk rename.");
        return;
    }

    // Open the BulkNameEditorDialog
    BulkNameEditorDialog dialog(this);
    dialog.setItems(paths, names, isFolders);

    if (dialog.exec() == QDialog::Accepted && dialog.hasChanges()) {
        QList<RenameResult> results = dialog.getRenameResults();

        // Confirm the rename operation
        QString summary;
        int changeCount = 0;
        for (const RenameResult& result : results) {
            if (result.willChange) {
                summary += QString("%1 → %2\n").arg(result.originalName, result.newName);
                changeCount++;
            }
        }

        if (changeCount == 0) {
            QMessageBox::information(this, "Bulk Rename", "No changes to apply.");
            return;
        }

        auto confirm = QMessageBox::question(this, "Confirm Bulk Rename",
            QString("Rename %1 item(s)?\n\n%2")
            .arg(changeCount)
            .arg(summary.left(500) + (summary.length() > 500 ? "\n..." : "")),
            QMessageBox::Yes | QMessageBox::No);

        if (confirm != QMessageBox::Yes) {
            return;
        }

        // Emit rename signals for each item that needs renaming
        int successCount = 0;
        int failCount = 0;

        for (const RenameResult& result : results) {
            if (result.willChange) {
                qDebug() << "AdvancedSearchPanel: Renaming" << result.originalPath
                         << "from" << result.originalName << "to" << result.newName;

                // Emit signal for each rename - MainWindow should connect this to FileController
                emit renameRequested(result.originalPath, result.newName);
                successCount++;  // Count as success (actual success depends on FileController response)
            }
        }

        // Notify completion
        emit batchRenameCompleted(successCount, failCount);
        emit bulkRenameRequested(paths);  // For any listeners that want to know about the rename operation

        QMessageBox::information(this, "Bulk Rename",
            QString("Rename requests sent for %1 item(s).\n"
                    "Check the file explorer to verify results.")
            .arg(successCount));

        // Refresh search results after rename
        QTimer::singleShot(500, this, &AdvancedSearchPanel::executeSearch);
    }
}

void AdvancedSearchPanel::onGoToLocation()
{
    // Find first checked or selected item
    for (int i = 0; i < m_model->rowCount(); ++i) {
        QStandardItem* item = m_model->item(i);
        if (item->data(AdvancedSearchResultDelegate::CheckedRole).toBool()) {
            QString handle = item->data(AdvancedSearchResultDelegate::HandleRole).toString();
            QString path = item->data(AdvancedSearchResultDelegate::PathRole).toString();
            bool isFolder = item->data(AdvancedSearchResultDelegate::IsFolderRole).toBool();
            emit navigateToPath(handle, path, isFolder);
            return;
        }
    }

    // Fall back to selected item
    QModelIndex current = m_resultsList->currentIndex();
    if (current.isValid()) {
        onResultDoubleClicked(current);
    }
}

QStringList AdvancedSearchPanel::getSelectedPaths() const
{
    QStringList paths;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        QStandardItem* item = m_model->item(i);
        if (item->data(AdvancedSearchResultDelegate::CheckedRole).toBool()) {
            paths << item->data(AdvancedSearchResultDelegate::PathRole).toString();
        }
    }
    return paths;
}

QStringList AdvancedSearchPanel::getSelectedHandles() const
{
    QStringList handles;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        QStandardItem* item = m_model->item(i);
        if (item->data(AdvancedSearchResultDelegate::CheckedRole).toBool()) {
            handles << item->data(AdvancedSearchResultDelegate::HandleRole).toString();
        }
    }
    return handles;
}

void AdvancedSearchPanel::updateIndexStatus()
{
    if (m_searchIndex) {
        QString indexStatus = QString("Index: %1 files, %2 folders")
                              .arg(m_searchIndex->fileCount())
                              .arg(m_searchIndex->folderCount());
        if (m_searchIndex->isBuilding()) {
            indexStatus += " (building...)";
            // Show spinner during indexing
            if (m_indexingSpinner) {
                m_indexingSpinner->start();
                m_indexingSpinner->show();
            }
        } else {
            // Hide spinner when indexing complete
            if (m_indexingSpinner) {
                m_indexingSpinner->stop();
                m_indexingSpinner->hide();
            }
        }
        m_indexStatusLabel->setText(indexStatus);
    } else {
        m_indexStatusLabel->setText("Index: Not loaded");
        if (m_indexingSpinner) {
            m_indexingSpinner->stop();
            m_indexingSpinner->hide();
        }
    }
}

} // namespace MegaCustom
