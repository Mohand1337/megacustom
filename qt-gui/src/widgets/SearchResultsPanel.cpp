#include "SearchResultsPanel.h"
#include "IconProvider.h"
#include <algorithm>
#include <QPainter>
#include <QApplication>
#include <QScrollBar>
#include <QKeyEvent>
#include <QDebug>
#include <QStyle>

namespace MegaCustom {

// ============================================================================
// SearchResultDelegate
// ============================================================================

SearchResultDelegate::SearchResultDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void SearchResultDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    QRect rect = option.rect;
    bool isSelected = option.state & QStyle::State_Selected;
    bool isHovered = option.state & QStyle::State_MouseOver;

    // Background
    if (isSelected) {
        painter->fillRect(rect, QColor(221, 20, 5, 26)); // MEGA Red 10%
    } else if (isHovered) {
        painter->fillRect(rect, QColor(0, 0, 0, 13)); // Light hover
    }

    // Get data
    QString name = index.data(NameRole).toString();
    QString path = index.data(PathRole).toString();
    qint64 size = index.data(SizeRole).toLongLong();
    qint64 date = index.data(DateRole).toLongLong();
    bool isFolder = index.data(IsFolderRole).toBool();
    QString extension = index.data(ExtensionRole).toString();

    // Icon area (32x32)
    QRect iconRect = rect;
    iconRect.setWidth(40);
    iconRect.adjust(8, 8, -4, -8);

    // Draw icon
    QIcon icon = isFolder ? IconProvider::instance().icon("folder")
                          : IconProvider::instance().icon("file");
    icon.paint(painter, iconRect, Qt::AlignCenter);

    // Text area
    QRect textRect = rect;
    textRect.setLeft(iconRect.right() + 8);
    textRect.setRight(rect.right() - 150); // Leave room for size/date

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
    if (displayPath.length() > 60) {
        displayPath = "..." + displayPath.right(57);
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

QSize SearchResultDelegate::sizeHint(const QStyleOptionViewItem& option,
                                      const QModelIndex& index) const
{
    Q_UNUSED(option)
    Q_UNUSED(index)
    return QSize(0, 52); // Fixed height for each item
}

QString SearchResultDelegate::formatSize(qint64 bytes) const
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

QString SearchResultDelegate::formatDate(qint64 timestamp) const
{
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
// SearchResultsPanel
// ============================================================================

SearchResultsPanel::SearchResultsPanel(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus)
    , m_searchIndex(nullptr)
    , m_sortField(SortField::Relevance)
    , m_sortOrder(SortOrder::Descending)
{
    // Prevent this widget from stealing focus from the search field
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_ShowWithoutActivating);

    setupUI();

    // Debounce timer for search
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    connect(m_searchTimer, &QTimer::timeout, this, &SearchResultsPanel::executeSearch);

    // Styling
    setStyleSheet(R"(
        SearchResultsPanel {
            background-color: #FFFFFF;
            border: 1px solid #DCDDDD;
            border-radius: 8px;
        }
        QListView {
            background-color: transparent;
            border: none;
            outline: none;
        }
        QListView::item {
            border-bottom: 1px solid #F0F0F0;
        }
        QListView::item:selected {
            background-color: rgba(221, 20, 5, 0.1);
        }
        QComboBox {
            border: 1px solid #DCDDDD;
            border-radius: 4px;
            padding: 4px 8px;
            background: white;
        }
        QPushButton {
            border: 1px solid #DCDDDD;
            border-radius: 4px;
            padding: 4px 8px;
            background: white;
        }
        QPushButton:hover {
            background: #F5F5F5;
        }
    )");

    setAttribute(Qt::WA_TranslucentBackground, false);

    // Install event filter on list view
    m_resultsList->installEventFilter(this);
}

SearchResultsPanel::~SearchResultsPanel() = default;

void SearchResultsPanel::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(8, 8, 8, 8);
    m_mainLayout->setSpacing(4);

    // Header bar with query display and sort controls
    m_headerBar = new QWidget(this);
    QHBoxLayout* headerLayout = new QHBoxLayout(m_headerBar);
    headerLayout->setContentsMargins(4, 4, 4, 8);
    headerLayout->setSpacing(8);

    m_queryLabel = new QLabel("Search Results", this);
    m_queryLabel->setStyleSheet("font-weight: bold; color: #333;");
    headerLayout->addWidget(m_queryLabel);

    headerLayout->addStretch();

    // Sort field combo
    m_sortCombo = new QComboBox(this);
    m_sortCombo->addItem("Relevance", static_cast<int>(SortField::Relevance));
    m_sortCombo->addItem("Name", static_cast<int>(SortField::Name));
    m_sortCombo->addItem("Size", static_cast<int>(SortField::Size));
    m_sortCombo->addItem("Date Modified", static_cast<int>(SortField::DateModified));
    m_sortCombo->addItem("Type", static_cast<int>(SortField::Type));
    m_sortCombo->setFixedWidth(120);
    m_sortCombo->setFocusPolicy(Qt::NoFocus);  // Don't steal focus from search field
    connect(m_sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SearchResultsPanel::onSortFieldChanged);
    headerLayout->addWidget(m_sortCombo);

    // Sort order button
    m_sortOrderBtn = new QPushButton(this);
    m_sortOrderBtn->setFixedSize(28, 28);
    m_sortOrderBtn->setIconSize(QSize(16, 16));
    m_sortOrderBtn->setFocusPolicy(Qt::NoFocus);  // Don't steal focus from search field
    updateSortButton();
    connect(m_sortOrderBtn, &QPushButton::clicked, this, &SearchResultsPanel::onSortOrderToggled);
    headerLayout->addWidget(m_sortOrderBtn);

    m_mainLayout->addWidget(m_headerBar);

    // Results list
    m_resultsList = new QListView(this);
    m_model = new QStandardItemModel(this);
    m_delegate = new SearchResultDelegate(this);

    m_resultsList->setModel(m_model);
    m_resultsList->setItemDelegate(m_delegate);
    m_resultsList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_resultsList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_resultsList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_resultsList->setMouseTracking(true);
    m_resultsList->setFocusPolicy(Qt::NoFocus);

    connect(m_resultsList, &QListView::activated, this, &SearchResultsPanel::onItemActivated);
    connect(m_resultsList, &QListView::clicked, this, &SearchResultsPanel::onItemActivated);

    m_mainLayout->addWidget(m_resultsList, 1);

    // Status bar
    m_statusBar = new QWidget(this);
    QHBoxLayout* statusLayout = new QHBoxLayout(m_statusBar);
    statusLayout->setContentsMargins(4, 8, 4, 4);
    statusLayout->setSpacing(8);

    m_statusLabel = new QLabel("Ready", this);
    m_statusLabel->setStyleSheet("color: #666; font-size: 11px;");
    statusLayout->addWidget(m_statusLabel);

    statusLayout->addStretch();

    m_indexStatusLabel = new QLabel("", this);
    m_indexStatusLabel->setStyleSheet("color: #999; font-size: 11px;");
    statusLayout->addWidget(m_indexStatusLabel);

    m_bulkRenameBtn = new QPushButton("Bulk Rename...", this);
    m_bulkRenameBtn->setFocusPolicy(Qt::NoFocus);  // Don't steal focus from search field
    m_bulkRenameBtn->setVisible(false); // Show when results available
    connect(m_bulkRenameBtn, &QPushButton::clicked, [this]() {
        QStringList handles;
        for (int i = 0; i < m_model->rowCount(); ++i) {
            handles.append(m_model->item(i)->data(SearchResultDelegate::HandleRole).toString());
        }
        if (!handles.isEmpty()) {
            emit bulkRenameRequested(handles);
        }
    });
    statusLayout->addWidget(m_bulkRenameBtn);

    m_mainLayout->addWidget(m_statusBar);

    setMinimumWidth(400);
    setMaximumHeight(PANEL_MAX_HEIGHT);
}

void SearchResultsPanel::setSearchIndex(CloudSearchIndex* index)
{
    // Disconnect old index signals if present
    if (m_searchIndex) {
        disconnect(m_searchIndex, nullptr, this, nullptr);
    }

    m_searchIndex = index;

    if (m_searchIndex) {
        connect(m_searchIndex, &CloudSearchIndex::indexingFinished,
                this, &SearchResultsPanel::updateStatusBar);
    }

    updateStatusBar();
}

void SearchResultsPanel::setQuery(const QString& query)
{
    m_currentQuery = query.trimmed();

    // Update query label
    if (m_currentQuery.isEmpty()) {
        m_queryLabel->setText("Search Results");
    } else {
        m_queryLabel->setText(QString("Search: %1").arg(m_currentQuery));
    }

    // Debounce search
    m_searchTimer->stop();
    if (!m_currentQuery.isEmpty()) {
        m_searchTimer->start(SEARCH_DEBOUNCE_MS);
    } else {
        clearResults();
    }
}

void SearchResultsPanel::executeSearch()
{
    if (!m_searchIndex || m_currentQuery.isEmpty()) {
        clearResults();
        return;
    }

    QElapsedTimer timer;
    timer.start();

    // Execute search
    QVector<SearchResult> results = m_searchIndex->searchWithSort(
        m_currentQuery, m_sortField, m_sortOrder, MAX_VISIBLE_RESULTS);

    // Populate results
    populateResults(results);

    // Update status
    m_statusLabel->setText(QString("%1 results in %2 ms")
                           .arg(results.size())
                           .arg(timer.elapsed()));

    m_bulkRenameBtn->setVisible(!results.isEmpty());
}

void SearchResultsPanel::populateResults(const QVector<SearchResult>& results)
{
    m_model->clear();

    for (const SearchResult& result : results) {
        // Skip invalid results (empty handle indicates invalid data)
        if (result.handle.isEmpty()) continue;

        QStandardItem* item = new QStandardItem();
        // Use by-value fields from SearchResult (safe after mutex release)
        item->setData(result.name, SearchResultDelegate::NameRole);
        item->setData(result.path, SearchResultDelegate::PathRole);
        item->setData(result.size, SearchResultDelegate::SizeRole);
        item->setData(result.modificationTime, SearchResultDelegate::DateRole);
        item->setData(result.handle, SearchResultDelegate::HandleRole);
        item->setData(result.isFolder, SearchResultDelegate::IsFolderRole);
        item->setData(result.extension, SearchResultDelegate::ExtensionRole);
        item->setData(result.relevanceScore, SearchResultDelegate::RelevanceRole);

        // Store match spans for highlighting
        QVariantList matchList;
        for (const MatchSpan& span : result.nameMatches) {
            QVariantMap match;
            match["start"] = span.start;
            match["length"] = span.length;
            matchList.append(match);
        }
        item->setData(matchList, SearchResultDelegate::NameMatchesRole);

        item->setEditable(false);
        m_model->appendRow(item);
    }

    // Select first item
    if (m_model->rowCount() > 0) {
        m_resultsList->setCurrentIndex(m_model->index(0, 0));
    }

    // Adjust height based on results
    int rowHeight = 52;
    int contentHeight = std::min(static_cast<int>(results.size()) * rowHeight + 100, PANEL_MAX_HEIGHT);
    setFixedHeight(contentHeight);
}

void SearchResultsPanel::clearResults()
{
    m_model->clear();
    m_statusLabel->setText("Ready");
    m_bulkRenameBtn->setVisible(false);
}

void SearchResultsPanel::selectNext()
{
    QModelIndex current = m_resultsList->currentIndex();
    int nextRow = current.isValid() ? current.row() + 1 : 0;
    if (nextRow < m_model->rowCount()) {
        m_resultsList->setCurrentIndex(m_model->index(nextRow, 0));
    }
}

void SearchResultsPanel::selectPrevious()
{
    QModelIndex current = m_resultsList->currentIndex();
    int prevRow = current.isValid() ? current.row() - 1 : m_model->rowCount() - 1;
    if (prevRow >= 0) {
        m_resultsList->setCurrentIndex(m_model->index(prevRow, 0));
    }
}

void SearchResultsPanel::activateSelected()
{
    QModelIndex current = m_resultsList->currentIndex();
    if (current.isValid()) {
        onItemActivated(current);
    }
}

void SearchResultsPanel::showAtPosition(const QPoint& pos, int width)
{
    setFixedWidth(std::max(width, 400));

    // Adjust position to stay within screen bounds
    QPoint adjustedPos = pos;
    QScreen* screen = QGuiApplication::screenAt(pos);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen) {
        QRect availGeom = screen->availableGeometry();

        // Adjust horizontal position if needed
        if (adjustedPos.x() + this->width() > availGeom.right()) {
            adjustedPos.setX(qMax(availGeom.left(), availGeom.right() - this->width()));
        }

        // Adjust vertical position - show above search field if below goes off-screen
        if (adjustedPos.y() + this->height() > availGeom.bottom()) {
            // Position above the search field (subtract height + search field height)
            adjustedPos.setY(qMax(availGeom.top(), pos.y() - this->height() - 48));
        }
    }

    move(adjustedPos);
    show();
}

void SearchResultsPanel::updatePosition(const QPoint& pos, int width)
{
    if (isVisible()) {
        setFixedWidth(std::max(width, 400));
        move(pos);
    }
}

void SearchResultsPanel::setSortField(SortField field)
{
    if (m_sortField != field) {
        m_sortField = field;
        int index = m_sortCombo->findData(static_cast<int>(field));
        if (index >= 0) {
            m_sortCombo->setCurrentIndex(index);
        }
        executeSearch();
    }
}

void SearchResultsPanel::setSortOrder(SortOrder order)
{
    if (m_sortOrder != order) {
        m_sortOrder = order;
        updateSortButton();
        executeSearch();
    }
}

void SearchResultsPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    emit visibilityChanged(true);
    updateStatusBar();
}

void SearchResultsPanel::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    m_searchTimer->stop();  // Stop debounce timer when hidden
    emit visibilityChanged(false);
}

void SearchResultsPanel::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Down:
        selectNext();
        event->accept();
        return;

    case Qt::Key_Up:
        selectPrevious();
        event->accept();
        return;

    case Qt::Key_Return:
    case Qt::Key_Enter:
        activateSelected();
        event->accept();
        return;

    case Qt::Key_Escape:
        hide();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

bool SearchResultsPanel::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_resultsList && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        // Handle keys directly here instead of calling keyPressEvent to avoid double handling
        switch (keyEvent->key()) {
        case Qt::Key_Down:
            selectNext();
            return true;
        case Qt::Key_Up:
            selectPrevious();
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            activateSelected();
            return true;
        case Qt::Key_Escape:
            hide();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void SearchResultsPanel::onItemActivated(const QModelIndex& index)
{
    if (!index.isValid()) return;

    QString handle = index.data(SearchResultDelegate::HandleRole).toString();
    QString path = index.data(SearchResultDelegate::PathRole).toString();
    bool isFolder = index.data(SearchResultDelegate::IsFolderRole).toBool();

    emit resultActivated(handle, path, isFolder);
    hide();
}

void SearchResultsPanel::onSortFieldChanged(int index)
{
    SortField field = static_cast<SortField>(m_sortCombo->itemData(index).toInt());
    if (m_sortField != field) {
        m_sortField = field;
        executeSearch();
    }
}

void SearchResultsPanel::onSortOrderToggled()
{
    m_sortOrder = (m_sortOrder == SortOrder::Ascending)
                  ? SortOrder::Descending : SortOrder::Ascending;
    updateSortButton();
    executeSearch();
}

void SearchResultsPanel::updateSortButton()
{
    if (m_sortOrder == SortOrder::Ascending) {
        m_sortOrderBtn->setIcon(QIcon(":/icons/arrow-up.svg"));
        m_sortOrderBtn->setToolTip("Sort Ascending");
    } else {
        m_sortOrderBtn->setIcon(QIcon(":/icons/arrow-down.svg"));
        m_sortOrderBtn->setToolTip("Sort Descending");
    }
}

void SearchResultsPanel::updateStatusBar()
{
    if (m_searchIndex) {
        QString indexStatus = QString("Index: %1 files, %2 folders")
                              .arg(m_searchIndex->fileCount())
                              .arg(m_searchIndex->folderCount());
        if (m_searchIndex->isBuilding()) {
            indexStatus += " (building...)";
        }
        m_indexStatusLabel->setText(indexStatus);
    } else {
        m_indexStatusLabel->setText("Index: Not loaded");
    }
}

} // namespace MegaCustom
