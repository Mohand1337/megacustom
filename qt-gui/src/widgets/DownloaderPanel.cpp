#include "DownloaderPanel.h"
#include "styles/ThemeManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QScrollArea>
#include <QMenu>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QCoreApplication>

namespace MegaCustom {

// ==================== DownloadWorker ====================

DownloadWorker::DownloadWorker(QObject* parent)
    : QObject(parent)
{
}

DownloadWorker::~DownloadWorker() {
    if (m_process) {
        m_process->kill();
        m_process->waitForFinished(1000);
        delete m_process;
    }
}

void DownloadWorker::setUrls(const QStringList& urls) {
    m_urls = urls;
}

void DownloadWorker::setOutputDir(const QString& dir) {
    m_outputDir = dir;
}

void DownloadWorker::setConfig(int maxParallel, const QString& quality, bool skipExisting,
                                bool downloadSubtitles, const QString& docsFormat) {
    m_maxParallel = maxParallel;
    m_quality = quality;
    m_skipExisting = skipExisting;
    m_downloadSubtitles = downloadSubtitles;
    m_docsFormat = docsFormat;
}

void DownloadWorker::cancel() {
    m_cancelled = true;
    if (m_process && m_process->state() == QProcess::Running) {
        m_process->terminate();
        if (!m_process->waitForFinished(3000)) {
            m_process->kill();
        }
    }
}

QString DownloadWorker::findPythonScript() const {
    // Check adjacent to executable
    QString appDir = QCoreApplication::applicationDirPath();
    QString scriptPath = appDir + "/scripts/download_manager.py";
    if (QFile::exists(scriptPath)) {
        return scriptPath;
    }

    // Check source tree (development)
    scriptPath = appDir + "/../../scripts/download_manager.py";
    if (QFile::exists(scriptPath)) {
        return QFileInfo(scriptPath).canonicalFilePath();
    }

    // Check project root
    scriptPath = appDir + "/../../../scripts/download_manager.py";
    if (QFile::exists(scriptPath)) {
        return QFileInfo(scriptPath).canonicalFilePath();
    }

    return QString();
}

void DownloadWorker::process() {
    emit started();

    m_cancelled = false;
    int successCount = 0;
    int failCount = 0;
    int total = m_urls.size();

    QString scriptPath = findPythonScript();
    if (scriptPath.isEmpty()) {
        emit logMessage("ERROR: download_manager.py not found!");
        emit finished(0, total);
        return;
    }

    emit logMessage("Using script: " + scriptPath);

    for (int i = 0; i < total && !m_cancelled; ++i) {
        m_currentIndex = i;
        m_lastCompletedPath.clear();
        m_itemCompletedEmitted = false;
        QString url = m_urls[i];

        emit progress(i, total, url, 0, "", "");

        // Build command
        QStringList args;
        args << scriptPath;
        args << "--url" << url;
        args << "--output" << m_outputDir;
        args << "--json-progress";

        if (m_skipExisting) {
            args << "--skip-existing";
        }
        if (m_downloadSubtitles) {
            args << "--subtitles";
        }
        if (!m_quality.isEmpty() && m_quality != "highest") {
            args << "--quality" << m_quality;
        }
        if (!m_docsFormat.isEmpty()) {
            args << "--docs-format" << m_docsFormat;
        }

        m_process = new QProcess();
        m_process->setProcessChannelMode(QProcess::MergedChannels);

        // Connect to read output as it comes
        connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
            while (m_process->canReadLine()) {
                QString line = QString::fromUtf8(m_process->readLine()).trimmed();
                parseProgressLine(line);
            }
        });

        // Determine Python executable path
        QString pythonExe = "python3";
#ifdef Q_OS_WIN
        // On Windows, check for bundled Python first (portable mode)
        QString appDir = QCoreApplication::applicationDirPath();
        QString bundledPython = appDir + "/python/python.exe";
        if (QFile::exists(bundledPython)) {
            pythonExe = bundledPython;
            emit logMessage("Using bundled Python: " + bundledPython);
        } else {
            pythonExe = "python";  // Windows uses 'python' not 'python3'
        }
#endif
        m_process->start(pythonExe, args);

        if (!m_process->waitForStarted(5000)) {
            emit logMessage("Failed to start Python (" + pythonExe + ") for: " + url);
            emit itemCompleted(i, false, "", "Failed to start Python process");
            failCount++;
            delete m_process;
            m_process = nullptr;
            continue;
        }

        // Wait for completion (with timeout of 30 minutes per file)
        if (!m_process->waitForFinished(1800000)) {
            emit logMessage("Download timeout for: " + url);
            emit itemCompleted(i, false, "", "Download timeout");
            failCount++;
            m_process->kill();
            delete m_process;
            m_process = nullptr;
            continue;
        }

        // Process any remaining output
        QString remaining = QString::fromUtf8(m_process->readAll());
        for (const QString& line : remaining.split('\n')) {
            if (!line.trimmed().isEmpty()) {
                parseProgressLine(line.trimmed());
            }
        }

        int exitCode = m_process->exitCode();
        delete m_process;
        m_process = nullptr;

        // Only emit completion if not already done via JSON parsing
        if (!m_itemCompletedEmitted) {
            if (exitCode == 0) {
                // Use path from JSON if available, otherwise use output directory
                QString outputPath = m_lastCompletedPath.isEmpty() ? m_outputDir : m_lastCompletedPath;
                emit itemCompleted(i, true, outputPath, "");
                successCount++;
            } else {
                emit itemCompleted(i, false, "", QString("Exit code: %1").arg(exitCode));
                failCount++;
            }
        } else {
            // Already emitted via JSON, just count
            if (exitCode == 0) {
                successCount++;
            } else {
                failCount++;
            }
        }
    }

    emit finished(successCount, failCount);
}

void DownloadWorker::parseProgressLine(const QString& line) {
    emit logMessage(line);

    // Try to parse as JSON
    QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        QString type = obj["type"].toString();

        if (type == "progress") {
            QString file = obj["file"].toString();
            int percent = obj["percent"].toInt();
            QString speed = obj["speed"].toString();
            QString eta = obj["eta"].toString();
            emit progress(m_currentIndex, m_urls.size(), file, percent, speed, eta);
        }
        else if (type == "complete") {
            QString path = obj["path"].toString();
            m_lastCompletedPath = path;
            m_itemCompletedEmitted = true;
            emit itemCompleted(m_currentIndex, true, path, "");
        }
        else if (type == "error") {
            QString error = obj["error"].toString();
            emit itemCompleted(m_currentIndex, false, "", error);
        }
    }
    else {
        // Try to parse text-based progress (e.g., "[filename] 45% - 2.5MB/s ETA: 00:02")
        QRegularExpression re(R"(\[(.+?)\]\s*(\d+)%.*?(\d+\.?\d*\s*[KMG]?B/s)?.*?ETA:\s*(\d+:\d+:\d+)?)");
        QRegularExpressionMatch match = re.match(line);
        if (match.hasMatch()) {
            QString file = match.captured(1);
            int percent = match.captured(2).toInt();
            QString speed = match.captured(3);
            QString eta = match.captured(4);
            emit progress(m_currentIndex, m_urls.size(), file, percent, speed, eta);
        }
    }
}

// ==================== DownloaderPanel ====================

DownloaderPanel::DownloaderPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    updateButtonStates();
}

DownloaderPanel::~DownloaderPanel() {
    if (m_workerThread) {
        if (m_worker) {
            m_worker->cancel();
        }
        m_workerThread->quit();
        m_workerThread->wait();
        delete m_workerThread;
    }
}

void DownloaderPanel::setupUI() {
    auto& tm = ThemeManager::instance();
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(16);

    // Title
    QLabel* titleLabel = new QLabel("Downloader Tool");
    titleLabel->setStyleSheet(QString("font-size: 18px; font-weight: bold; color: %1;")
        .arg(tm.textPrimary().name()));
    mainLayout->addWidget(titleLabel);

    // Description
    QLabel* descLabel = new QLabel("Download content from BunnyCDN, Google Drive, Dropbox, and more. First step in the content pipeline.");
    descLabel->setStyleSheet(QString("color: %1; margin-bottom: 8px;").arg(tm.textSecondary().name()));
    descLabel->setWordWrap(true);
    mainLayout->addWidget(descLabel);

    // === URL Input Section ===
    QGroupBox* inputGroup = new QGroupBox("URL Input");
    QVBoxLayout* inputLayout = new QVBoxLayout(inputGroup);

    m_urlInput = new QPlainTextEdit();
    m_urlInput->setPlaceholderText("Paste URLs here (one per line, or paste text containing URLs)\n\nSupported sources:\n- BunnyCDN (iframe.mediadelivery.net/embed/...)\n- Google Drive (drive.google.com/file/d/...)\n- Google Docs/Sheets/Slides\n- Dropbox (dropbox.com/...)\n- Direct HTTP links (.mp4, .pdf, etc.)");
    m_urlInput->setMaximumHeight(120);
    m_urlInput->setStyleSheet(QString(R"(
        QPlainTextEdit {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 4px;
            color: %3;
            padding: 8px;
        }
    )")
        .arg(tm.surfacePrimary().name())
        .arg(tm.borderSubtle().name())
        .arg(tm.textPrimary().name()));
    inputLayout->addWidget(m_urlInput);

    QHBoxLayout* inputActionsLayout = new QHBoxLayout();
    m_parseBtn = new QPushButton("Parse URLs");
    m_parseBtn->setIcon(QIcon(":/icons/search.svg"));
    connect(m_parseBtn, &QPushButton::clicked, this, &DownloaderPanel::onParseUrls);

    m_clearInputBtn = new QPushButton("Clear");
    connect(m_clearInputBtn, &QPushButton::clicked, this, &DownloaderPanel::onClearInput);

    inputActionsLayout->addWidget(m_parseBtn);
    inputActionsLayout->addWidget(m_clearInputBtn);
    inputActionsLayout->addStretch();

    inputLayout->addLayout(inputActionsLayout);
    mainLayout->addWidget(inputGroup);

    // === Download Queue Section ===
    QGroupBox* queueGroup = new QGroupBox("Download Queue");
    QVBoxLayout* queueLayout = new QVBoxLayout(queueGroup);

    m_downloadTable = new QTableWidget();
    m_downloadTable->setColumnCount(6);
    m_downloadTable->setHorizontalHeaderLabels({"File Name", "Source", "Status", "Progress", "Speed", "ETA"});
    m_downloadTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_downloadTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_downloadTable->setAlternatingRowColors(true);
    m_downloadTable->verticalHeader()->setVisible(false);
    m_downloadTable->setContextMenuPolicy(Qt::CustomContextMenu);

    m_downloadTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_downloadTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_downloadTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_downloadTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_downloadTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_downloadTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    m_downloadTable->setColumnWidth(1, 100);
    m_downloadTable->setColumnWidth(2, 100);
    m_downloadTable->setColumnWidth(3, 100);
    m_downloadTable->setColumnWidth(4, 80);
    m_downloadTable->setColumnWidth(5, 80);

    m_downloadTable->setStyleSheet(QString(R"(
        QTableWidget {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 4px;
            gridline-color: %3;
        }
        QTableWidget::item {
            padding: 4px;
        }
        QTableWidget::item:selected {
            background-color: %4;
        }
        QHeaderView::section {
            background-color: %5;
            color: %6;
            padding: 6px;
            border: none;
            border-bottom: 1px solid %2;
        }
    )")
        .arg(tm.surfacePrimary().name())
        .arg(tm.borderSubtle().name())
        .arg(tm.borderSubtle().darker(120).name())
        .arg(tm.brandDefault().name())
        .arg(tm.surface2().name())
        .arg(tm.textPrimary().name()));

    connect(m_downloadTable, &QTableWidget::itemSelectionChanged,
            this, &DownloaderPanel::onTableSelectionChanged);
    connect(m_downloadTable, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        menu.addAction("Remove Selected", this, &DownloaderPanel::onRemoveSelected);
        menu.addAction("Clear Completed", this, &DownloaderPanel::onClearCompleted);
        menu.addAction("Clear All", this, &DownloaderPanel::onClearAll);
        menu.exec(m_downloadTable->viewport()->mapToGlobal(pos));
    });

    queueLayout->addWidget(m_downloadTable, 1);

    // Queue action buttons
    QHBoxLayout* queueActionsLayout = new QHBoxLayout();
    m_removeBtn = new QPushButton("Remove");
    m_removeBtn->setIcon(QIcon(":/icons/trash-2.svg"));
    m_removeBtn->setEnabled(false);
    connect(m_removeBtn, &QPushButton::clicked, this, &DownloaderPanel::onRemoveSelected);

    m_clearCompletedBtn = new QPushButton("Clear Completed");
    connect(m_clearCompletedBtn, &QPushButton::clicked, this, &DownloaderPanel::onClearCompleted);

    m_clearAllBtn = new QPushButton("Clear All");
    connect(m_clearAllBtn, &QPushButton::clicked, this, &DownloaderPanel::onClearAll);

    queueActionsLayout->addWidget(m_removeBtn);
    queueActionsLayout->addWidget(m_clearCompletedBtn);
    queueActionsLayout->addWidget(m_clearAllBtn);
    queueActionsLayout->addStretch();

    queueLayout->addLayout(queueActionsLayout);
    mainLayout->addWidget(queueGroup, 1);

    // === Settings Section ===
    QGroupBox* settingsGroup = new QGroupBox("Settings");
    QVBoxLayout* settingsLayout = new QVBoxLayout(settingsGroup);

    // Output directory
    QHBoxLayout* outputLayout = new QHBoxLayout();
    outputLayout->addWidget(new QLabel("Output:"));
    m_outputDirEdit = new QLineEdit();
    m_outputDirEdit->setText(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/mega-downloads");
    outputLayout->addWidget(m_outputDirEdit, 1);
    m_browseOutputBtn = new QPushButton("Browse...");
    connect(m_browseOutputBtn, &QPushButton::clicked, this, &DownloaderPanel::onBrowseOutput);
    outputLayout->addWidget(m_browseOutputBtn);
    settingsLayout->addLayout(outputLayout);

    // Download options
    QHBoxLayout* optionsLayout = new QHBoxLayout();
    optionsLayout->setSpacing(16);

    optionsLayout->addWidget(new QLabel("Quality:"));
    m_qualityCombo = new QComboBox();
    m_qualityCombo->addItems({"highest", "1080", "720", "480", "lowest"});
    m_qualityCombo->setToolTip("Video quality preference");
    optionsLayout->addWidget(m_qualityCombo);

    optionsLayout->addWidget(new QLabel("Parallel:"));
    m_parallelSpin = new QSpinBox();
    m_parallelSpin->setRange(1, 5);
    m_parallelSpin->setValue(3);
    m_parallelSpin->setToolTip("Maximum parallel downloads");
    optionsLayout->addWidget(m_parallelSpin);

    optionsLayout->addWidget(new QLabel("Docs Format:"));
    m_docsFormatCombo = new QComboBox();
    m_docsFormatCombo->addItems({"pdf", "docx", "xlsx", "pptx"});
    m_docsFormatCombo->setToolTip("Export format for Google Docs/Sheets/Slides");
    optionsLayout->addWidget(m_docsFormatCombo);

    optionsLayout->addStretch();
    settingsLayout->addLayout(optionsLayout);

    // Checkboxes
    QHBoxLayout* checksLayout = new QHBoxLayout();
    m_skipExistingCheck = new QCheckBox("Skip existing files");
    m_skipExistingCheck->setChecked(true);
    checksLayout->addWidget(m_skipExistingCheck);

    m_downloadSubtitlesCheck = new QCheckBox("Download subtitles");
    m_downloadSubtitlesCheck->setChecked(true);
    checksLayout->addWidget(m_downloadSubtitlesCheck);

    checksLayout->addStretch();
    settingsLayout->addLayout(checksLayout);

    mainLayout->addWidget(settingsGroup);

    // === Watermark Integration Section ===
    QGroupBox* wmGroup = new QGroupBox("Watermark Integration");
    QHBoxLayout* wmLayout = new QHBoxLayout(wmGroup);

    m_autoSendCheck = new QCheckBox("Auto-send completed downloads to Watermark");
    m_autoSendCheck->setToolTip("Automatically send completed downloads to the Watermark panel");
    connect(m_autoSendCheck, &QCheckBox::toggled, this, &DownloaderPanel::onAutoSendToggled);
    wmLayout->addWidget(m_autoSendCheck);

    wmLayout->addStretch();

    m_sendToWatermarkBtn = new QPushButton("Send Selected to Watermark");
    m_sendToWatermarkBtn->setIcon(QIcon(":/icons/share.svg"));
    m_sendToWatermarkBtn->setEnabled(false);
    connect(m_sendToWatermarkBtn, &QPushButton::clicked, this, &DownloaderPanel::onSendToWatermark);
    wmLayout->addWidget(m_sendToWatermarkBtn);

    mainLayout->addWidget(wmGroup);

    // === Progress Section ===
    QHBoxLayout* progressLayout = new QHBoxLayout();
    m_progressBar = new QProgressBar();
    m_progressBar->setMinimum(0);
    m_progressBar->setMaximum(100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    progressLayout->addWidget(m_progressBar, 1);
    mainLayout->addLayout(progressLayout);

    // Status
    m_statusLabel = new QLabel("Ready");
    m_statusLabel->setStyleSheet("color: #888;");
    mainLayout->addWidget(m_statusLabel);

    // === Action Buttons ===
    QHBoxLayout* actionsLayout = new QHBoxLayout();
    actionsLayout->setSpacing(8);

    m_checkDepsBtn = new QPushButton("Check Dependencies");
    m_checkDepsBtn->setToolTip("Check if Python, yt-dlp, and ffmpeg are available");
    connect(m_checkDepsBtn, &QPushButton::clicked, this, &DownloaderPanel::onCheckDependencies);
    actionsLayout->addWidget(m_checkDepsBtn);

    actionsLayout->addStretch();

    m_startBtn = new QPushButton("Start Downloads");
    m_startBtn->setIcon(QIcon(":/icons/download.svg"));
    m_startBtn->setEnabled(false);
    m_startBtn->setStyleSheet(QString("QPushButton { background-color: %1; } QPushButton:hover { background-color: %2; }")
        .arg(tm.supportSuccess().name())
        .arg(tm.supportSuccess().darker(110).name()));
    connect(m_startBtn, &QPushButton::clicked, this, &DownloaderPanel::onStartDownloads);
    actionsLayout->addWidget(m_startBtn);

    m_stopBtn = new QPushButton("Stop");
    m_stopBtn->setIcon(QIcon(":/icons/stop.svg"));
    m_stopBtn->setEnabled(false);
    m_stopBtn->setStyleSheet(QString("QPushButton { background-color: %1; } QPushButton:hover { background-color: %2; }")
        .arg(tm.supportError().name())
        .arg(tm.supportError().darker(110).name()));
    connect(m_stopBtn, &QPushButton::clicked, this, &DownloaderPanel::onStopDownloads);
    actionsLayout->addWidget(m_stopBtn);

    mainLayout->addLayout(actionsLayout);

    // Stats
    m_statsLabel = new QLabel();
    m_statsLabel->setStyleSheet(QString("color: %1;").arg(tm.textSecondary().name()));
    mainLayout->addWidget(m_statsLabel);

    updateStats();
}

void DownloaderPanel::refresh() {
    updateStats();
}

void DownloaderPanel::onParseUrls() {
    QString text = m_urlInput->toPlainText();
    if (text.isEmpty()) {
        return;
    }

    QStringList urls = extractUrlsFromText(text);
    if (urls.isEmpty()) {
        QMessageBox::warning(this, "No URLs Found", "No valid URLs were found in the input text.");
        return;
    }

    for (const QString& url : urls) {
        // Check if already in list
        bool exists = false;
        for (const DownloadItemInfo& info : m_items) {
            if (info.url == url) {
                exists = true;
                break;
            }
        }
        if (exists) continue;

        DownloadItemInfo info;
        info.url = url;
        info.sourceType = detectUrlType(url);
        info.fileName = extractFileName(url, info.sourceType);
        info.isValid = isValidUrl(url);
        info.status = info.isValid ? "pending" : "invalid";

        m_items.append(info);
    }

    populateTable();
    updateStats();
    updateButtonStates();

    m_statusLabel->setText(QString("Added %1 URL(s) to queue").arg(urls.size()));
}

void DownloaderPanel::onClearInput() {
    m_urlInput->clear();
}

void DownloaderPanel::onRemoveSelected() {
    QList<int> selectedRows;
    for (const QModelIndex& index : m_downloadTable->selectionModel()->selectedRows()) {
        selectedRows.append(index.row());
    }

    std::sort(selectedRows.begin(), selectedRows.end(), std::greater<int>());

    for (int row : selectedRows) {
        if (row >= 0 && row < m_items.size()) {
            m_items.removeAt(row);
        }
    }

    populateTable();
    updateStats();
    updateButtonStates();
}

void DownloaderPanel::onClearCompleted() {
    QList<DownloadItemInfo> remaining;
    for (const DownloadItemInfo& info : m_items) {
        if (info.status != "complete") {
            remaining.append(info);
        }
    }
    m_items = remaining;

    populateTable();
    updateStats();
    updateButtonStates();
}

void DownloaderPanel::onClearAll() {
    m_items.clear();
    m_completedFiles.clear();
    populateTable();
    updateStats();
    updateButtonStates();
}

void DownloaderPanel::onBrowseOutput() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Output Directory");
    if (!dir.isEmpty()) {
        m_outputDirEdit->setText(dir);
    }
}

void DownloaderPanel::onStartDownloads() {
    if (m_items.isEmpty()) {
        QMessageBox::warning(this, "No URLs", "Please add URLs to download.");
        return;
    }

    // Create output directory if needed
    QString outputDir = m_outputDirEdit->text();
    QDir().mkpath(outputDir);

    // Collect URLs
    QStringList urls;
    for (const DownloadItemInfo& info : m_items) {
        if (info.status == "pending" && info.isValid) {
            urls.append(info.url);
        }
    }

    if (urls.isEmpty()) {
        QMessageBox::warning(this, "No Valid URLs", "No valid pending URLs to download.");
        return;
    }

    // Reset statuses
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].status == "pending") {
            m_items[i].progressPercent = 0;
            m_items[i].error.clear();
            m_items[i].speed.clear();
            m_items[i].eta.clear();
        }
    }
    populateTable();

    // Clear completed files for new batch
    m_completedFiles.clear();

    // Create worker thread
    m_workerThread = new QThread();
    m_worker = new DownloadWorker();
    m_worker->moveToThread(m_workerThread);

    m_worker->setUrls(urls);
    m_worker->setOutputDir(outputDir);
    m_worker->setConfig(
        m_parallelSpin->value(),
        m_qualityCombo->currentText(),
        m_skipExistingCheck->isChecked(),
        m_downloadSubtitlesCheck->isChecked(),
        m_docsFormatCombo->currentText()
    );

    connect(m_workerThread, &QThread::started, m_worker, &DownloadWorker::process);
    connect(m_worker, &DownloadWorker::progress, this, &DownloaderPanel::onWorkerProgress);
    connect(m_worker, &DownloadWorker::itemCompleted, this, &DownloaderPanel::onWorkerItemCompleted);
    connect(m_worker, &DownloadWorker::finished, this, &DownloaderPanel::onWorkerFinished);
    connect(m_worker, &DownloadWorker::logMessage, this, &DownloaderPanel::onWorkerLog);
    connect(m_worker, &DownloadWorker::finished, m_workerThread, &QThread::quit);
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater);
    connect(m_workerThread, &QThread::finished, this, [this]() {
        m_workerThread = nullptr;
        m_worker = nullptr;
    });

    m_isRunning = true;
    updateButtonStates();
    m_progressBar->setValue(0);
    m_statusLabel->setText("Starting downloads...");

    emit downloadStarted();
    m_workerThread->start();
}

void DownloaderPanel::onStopDownloads() {
    if (m_worker) {
        m_worker->cancel();
        m_statusLabel->setText("Cancelling...");
    }
}

void DownloaderPanel::onCheckDependencies() {
    QString status;

    // Check Python
    QProcess pythonCheck;
    pythonCheck.start("python3", {"--version"});
    pythonCheck.waitForFinished(3000);
    bool pythonOk = (pythonCheck.exitCode() == 0);
    QString pythonVersion = QString::fromUtf8(pythonCheck.readAllStandardOutput()).trimmed();

    if (pythonOk) {
        status += "Python: " + pythonVersion + "\n";
    } else {
        status += "Python: NOT FOUND\n";
        status += "  Install: sudo apt install python3\n";
    }

    // Check yt-dlp
    QProcess ytdlpCheck;
    ytdlpCheck.start("yt-dlp", {"--version"});
    ytdlpCheck.waitForFinished(3000);
    bool ytdlpOk = (ytdlpCheck.exitCode() == 0);
    QString ytdlpVersion = QString::fromUtf8(ytdlpCheck.readAllStandardOutput()).trimmed();

    if (ytdlpOk) {
        status += "yt-dlp: " + ytdlpVersion + "\n";
    } else {
        status += "yt-dlp: NOT FOUND (required for BunnyCDN DRM)\n";
        status += "  Install: pip install yt-dlp\n";
    }

    // Check ffmpeg
    QProcess ffmpegCheck;
    ffmpegCheck.start("ffmpeg", {"-version"});
    ffmpegCheck.waitForFinished(3000);
    bool ffmpegOk = (ffmpegCheck.exitCode() == 0);

    if (ffmpegOk) {
        status += "FFmpeg: Available\n";
    } else {
        status += "FFmpeg: NOT FOUND (required for video processing)\n";
        status += "  Install: sudo apt install ffmpeg\n";
    }

    // Check requests module
    QProcess requestsCheck;
    requestsCheck.start("python3", {"-c", "import requests; print(requests.__version__)"});
    requestsCheck.waitForFinished(3000);
    bool requestsOk = (requestsCheck.exitCode() == 0);
    QString requestsVersion = QString::fromUtf8(requestsCheck.readAllStandardOutput()).trimmed();

    if (requestsOk) {
        status += "Python requests: " + requestsVersion + "\n";
    } else {
        status += "Python requests: NOT FOUND\n";
        status += "  Install: pip install requests\n";
    }

    // Check download script
    DownloadWorker tempWorker;
    QString scriptPath = tempWorker.findPythonScript();
    if (!scriptPath.isEmpty()) {
        status += "Download Script: " + scriptPath + "\n";
    } else {
        status += "Download Script: NOT FOUND\n";
        status += "  Expected at: scripts/download_manager.py\n";
    }

    QMessageBox::information(this, "Dependency Check", status);
}

void DownloaderPanel::onSendToWatermark() {
    QStringList filesToSend;

    // Get selected completed files
    for (const QModelIndex& index : m_downloadTable->selectionModel()->selectedRows()) {
        int row = index.row();
        if (row >= 0 && row < m_items.size()) {
            const DownloadItemInfo& info = m_items[row];
            if (info.status == "complete" && !info.outputPath.isEmpty()) {
                filesToSend.append(info.outputPath);
            }
        }
    }

    if (filesToSend.isEmpty()) {
        // If nothing selected, send all completed
        filesToSend = m_completedFiles;
    }

    if (filesToSend.isEmpty()) {
        QMessageBox::information(this, "No Files", "No completed downloads to send to Watermark.");
        return;
    }

    emit sendToWatermark(filesToSend);
    m_statusLabel->setText(QString("Sent %1 file(s) to Watermark panel").arg(filesToSend.size()));
}

void DownloaderPanel::onAutoSendToggled(bool checked) {
    if (checked) {
        m_statusLabel->setText("Auto-send enabled: completed downloads will be sent to Watermark");
    }
}

void DownloaderPanel::onTableSelectionChanged() {
    updateButtonStates();
}

void DownloaderPanel::onWorkerProgress(int itemIndex, int totalItems, const QString& file,
                                        int percent, const QString& speed, const QString& eta) {
    // Find the item by index in pending items
    int pendingIndex = 0;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].status == "pending" || m_items[i].status == "downloading") {
            if (pendingIndex == itemIndex) {
                m_items[i].status = "downloading";
                m_items[i].progressPercent = percent;
                m_items[i].speed = speed;
                m_items[i].eta = eta;
                if (!file.isEmpty()) {
                    m_items[i].fileName = file;
                }
                break;
            }
            pendingIndex++;
        }
    }

    populateTable();

    int overallPercent = (itemIndex * 100 + percent) / totalItems;
    m_progressBar->setValue(overallPercent);
    m_statusLabel->setText(QString("Downloading %1 (%2%)").arg(file).arg(percent));

    emit downloadProgress(itemIndex + 1, totalItems, file);
}

void DownloaderPanel::onWorkerItemCompleted(int itemIndex, bool success,
                                             const QString& outputPath, const QString& error) {
    // Find the item
    int pendingIndex = 0;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].status == "pending" || m_items[i].status == "downloading") {
            if (pendingIndex == itemIndex) {
                m_items[i].status = success ? "complete" : "error";
                m_items[i].outputPath = outputPath;
                m_items[i].error = error;
                m_items[i].progressPercent = success ? 100 : 0;

                if (success && !outputPath.isEmpty()) {
                    m_completedFiles.append(outputPath);
                    emit downloadCompleted(outputPath, m_items[i].url);
                }
                break;
            }
            pendingIndex++;
        }
    }

    populateTable();
}

void DownloaderPanel::onWorkerFinished(int successCount, int failCount) {
    m_isRunning = false;
    updateButtonStates();

    m_progressBar->setValue(100);
    m_statusLabel->setText(QString("Completed: %1 success, %2 failed").arg(successCount).arg(failCount));

    emit allDownloadsCompleted(successCount, failCount);
    emit downloadsCompleted(m_completedFiles);

    // Check for auto-send
    checkAndAutoSend();

    if (failCount == 0 && successCount > 0) {
        QMessageBox::information(this, "Complete",
            QString("Successfully downloaded %1 file(s).").arg(successCount));
    } else if (failCount > 0) {
        QMessageBox::warning(this, "Complete with Errors",
            QString("Completed: %1 success, %2 failed.\n\nCheck the table for error details.")
                .arg(successCount).arg(failCount));
    }
}

void DownloaderPanel::onWorkerLog(const QString& message) {
    qDebug() << "Downloader:" << message;
}

void DownloaderPanel::checkAndAutoSend() {
    if (m_autoSendCheck->isChecked() && !m_completedFiles.isEmpty()) {
        emit sendToWatermark(m_completedFiles);
        m_statusLabel->setText(m_statusLabel->text() + QString(" | Auto-sent %1 file(s) to Watermark").arg(m_completedFiles.size()));
    }
}

void DownloaderPanel::populateTable() {
    auto& tm = ThemeManager::instance();
    m_downloadTable->setRowCount(m_items.size());

    for (int row = 0; row < m_items.size(); ++row) {
        const DownloadItemInfo& info = m_items[row];

        // File name
        QTableWidgetItem* nameItem = new QTableWidgetItem(info.fileName);
        nameItem->setToolTip(info.url);
        m_downloadTable->setItem(row, 0, nameItem);

        // Source - use supportInfo for all source types as primary color
        QTableWidgetItem* sourceItem = new QTableWidgetItem(sourceTypeToString(info.sourceType));
        sourceItem->setTextAlignment(Qt::AlignCenter);
        switch (info.sourceType) {
            case DownloadSourceType::BunnyCDN:
                sourceItem->setForeground(tm.supportWarning()); // Orange-ish
                break;
            case DownloadSourceType::GoogleDriveFile:
            case DownloadSourceType::GoogleDriveFolder:
            case DownloadSourceType::GoogleDocs:
            case DownloadSourceType::Dropbox:
            case DownloadSourceType::GenericHTTP:
                sourceItem->setForeground(tm.supportInfo()); // Blue
                break;
            default:
                sourceItem->setForeground(tm.textSecondary());
        }
        m_downloadTable->setItem(row, 1, sourceItem);

        // Status
        QTableWidgetItem* statusItem = new QTableWidgetItem();
        if (info.status == "pending") {
            statusItem->setText("Pending");
            statusItem->setForeground(tm.textSecondary());
        } else if (info.status == "downloading") {
            statusItem->setText(QString("Downloading"));
            statusItem->setForeground(tm.supportWarning()); // Yellow
        } else if (info.status == "complete") {
            statusItem->setText("Complete");
            statusItem->setForeground(tm.supportSuccess()); // Green
        } else if (info.status == "error") {
            statusItem->setText("Error");
            statusItem->setForeground(tm.supportError()); // Red
            statusItem->setToolTip(info.error);
        } else if (info.status == "invalid") {
            statusItem->setText("Invalid URL");
            statusItem->setForeground(tm.supportError());
        }
        statusItem->setTextAlignment(Qt::AlignCenter);
        m_downloadTable->setItem(row, 2, statusItem);

        // Progress
        QTableWidgetItem* progressItem = new QTableWidgetItem();
        if (info.status == "downloading") {
            progressItem->setText(QString("%1%").arg(info.progressPercent));
        } else if (info.status == "complete") {
            progressItem->setText("100%");
        } else {
            progressItem->setText("-");
        }
        progressItem->setTextAlignment(Qt::AlignCenter);
        m_downloadTable->setItem(row, 3, progressItem);

        // Speed
        QTableWidgetItem* speedItem = new QTableWidgetItem(info.speed.isEmpty() ? "-" : info.speed);
        speedItem->setTextAlignment(Qt::AlignCenter);
        m_downloadTable->setItem(row, 4, speedItem);

        // ETA
        QTableWidgetItem* etaItem = new QTableWidgetItem(info.eta.isEmpty() ? "-" : info.eta);
        etaItem->setTextAlignment(Qt::AlignCenter);
        m_downloadTable->setItem(row, 5, etaItem);
    }
}

void DownloaderPanel::updateStats() {
    int pending = 0, downloading = 0, complete = 0, failed = 0;

    for (const DownloadItemInfo& info : m_items) {
        if (info.status == "pending") pending++;
        else if (info.status == "downloading") downloading++;
        else if (info.status == "complete") complete++;
        else if (info.status == "error" || info.status == "invalid") failed++;
    }

    m_statsLabel->setText(QString("Queue: %1 total | %2 pending | %3 downloading | %4 complete | %5 failed")
        .arg(m_items.size())
        .arg(pending)
        .arg(downloading)
        .arg(complete)
        .arg(failed));
}

void DownloaderPanel::updateButtonStates() {
    bool hasItems = !m_items.isEmpty();
    bool hasSelection = m_downloadTable->selectionModel()->hasSelection();
    bool hasPending = false;
    bool hasCompleted = false;

    for (const DownloadItemInfo& info : m_items) {
        if (info.status == "pending" && info.isValid) hasPending = true;
        if (info.status == "complete") hasCompleted = true;
    }

    m_removeBtn->setEnabled(hasSelection && !m_isRunning);
    m_clearCompletedBtn->setEnabled(hasCompleted && !m_isRunning);
    m_clearAllBtn->setEnabled(hasItems && !m_isRunning);
    m_startBtn->setEnabled(hasPending && !m_isRunning);
    m_stopBtn->setEnabled(m_isRunning);
    m_sendToWatermarkBtn->setEnabled(hasCompleted && !m_isRunning);

    m_parseBtn->setEnabled(!m_isRunning);
    m_qualityCombo->setEnabled(!m_isRunning);
    m_parallelSpin->setEnabled(!m_isRunning);
    m_docsFormatCombo->setEnabled(!m_isRunning);
    m_skipExistingCheck->setEnabled(!m_isRunning);
    m_downloadSubtitlesCheck->setEnabled(!m_isRunning);
    m_browseOutputBtn->setEnabled(!m_isRunning);
    m_outputDirEdit->setEnabled(!m_isRunning);
}

DownloadSourceType DownloaderPanel::detectUrlType(const QString& url) const {
    // BunnyCDN: iframe.mediadelivery.net/embed/{libraryId}/{videoId}
    if (url.contains("iframe.mediadelivery.net/embed/")) {
        QRegularExpression re("/embed/(\\d+)/([a-f0-9-]+)");
        if (re.match(url).hasMatch()) {
            return DownloadSourceType::BunnyCDN;
        }
    }

    // Google Drive file: drive.google.com/file/d/{fileId}
    QRegularExpression gdriveFile("drive\\.google\\.com/file/d/([a-zA-Z0-9_-]+)");
    if (gdriveFile.match(url).hasMatch()) {
        return DownloadSourceType::GoogleDriveFile;
    }

    // Google Drive folder: drive.google.com/drive/folders/{folderId}
    QRegularExpression gdriveFolder("drive\\.google\\.com/drive/folders/([a-zA-Z0-9_-]+)");
    if (gdriveFolder.match(url).hasMatch()) {
        return DownloadSourceType::GoogleDriveFolder;
    }

    // Google Docs/Sheets/Slides
    QRegularExpression gdocs("docs\\.google\\.com/(document|spreadsheets|presentation)/d/");
    if (gdocs.match(url).hasMatch()) {
        return DownloadSourceType::GoogleDocs;
    }

    // Dropbox
    if (url.contains("dropbox.com", Qt::CaseInsensitive)) {
        return DownloadSourceType::Dropbox;
    }

    // Generic media URLs
    QRegularExpression media("\\.(mp4|mp3|pdf|zip|mov|avi|mkv|webm|m4v|doc|docx|xls|xlsx|ppt|pptx)(\\?|$)",
                             QRegularExpression::CaseInsensitiveOption);
    if (media.match(url).hasMatch()) {
        return DownloadSourceType::GenericHTTP;
    }

    return DownloadSourceType::Unknown;
}

QStringList DownloaderPanel::extractUrlsFromText(const QString& text) const {
    QStringList urls;

    // Match URLs
    QRegularExpression urlRegex(
        R"((https?://[^\s<>"']+))",
        QRegularExpression::CaseInsensitiveOption
    );

    QRegularExpressionMatchIterator it = urlRegex.globalMatch(text);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString url = match.captured(1);

        // Clean up trailing punctuation
        while (url.endsWith(',') || url.endsWith('.') || url.endsWith(')') || url.endsWith(']')) {
            url.chop(1);
        }

        // Only add supported URLs
        DownloadSourceType type = detectUrlType(url);
        if (type != DownloadSourceType::Unknown && !urls.contains(url)) {
            urls.append(url);
        }
    }

    return urls;
}

QString DownloaderPanel::sourceTypeToString(DownloadSourceType type) const {
    switch (type) {
        case DownloadSourceType::BunnyCDN: return "BunnyCDN";
        case DownloadSourceType::GoogleDriveFile: return "GDrive";
        case DownloadSourceType::GoogleDriveFolder: return "GDrive Folder";
        case DownloadSourceType::GoogleDocs: return "GDocs";
        case DownloadSourceType::Dropbox: return "Dropbox";
        case DownloadSourceType::GenericHTTP: return "HTTP";
        default: return "Unknown";
    }
}

QString DownloaderPanel::extractFileName(const QString& url, DownloadSourceType type) const {
    QString fileName;

    switch (type) {
        case DownloadSourceType::BunnyCDN: {
            QRegularExpression re("/embed/(\\d+)/([a-f0-9-]+)");
            QRegularExpressionMatch match = re.match(url);
            if (match.hasMatch()) {
                fileName = QString("video_%1.mp4").arg(match.captured(2).left(8));
            }
            break;
        }
        case DownloadSourceType::GoogleDriveFile: {
            QRegularExpression re("/file/d/([a-zA-Z0-9_-]+)");
            QRegularExpressionMatch match = re.match(url);
            if (match.hasMatch()) {
                fileName = QString("gdrive_%1").arg(match.captured(1).left(8));
            }
            break;
        }
        case DownloadSourceType::GoogleDocs: {
            QRegularExpression re("/(document|spreadsheets|presentation)/d/([a-zA-Z0-9_-]+)");
            QRegularExpressionMatch match = re.match(url);
            if (match.hasMatch()) {
                QString docType = match.captured(1);
                QString ext = "pdf";
                if (docType == "spreadsheets") ext = "xlsx";
                else if (docType == "presentation") ext = "pptx";
                fileName = QString("gdocs_%1.%2").arg(match.captured(2).left(8)).arg(ext);
            }
            break;
        }
        case DownloadSourceType::Dropbox:
        case DownloadSourceType::GenericHTTP: {
            // Extract filename from URL path
            QUrl qurl(url);
            QString path = qurl.path();
            if (!path.isEmpty()) {
                fileName = path.section('/', -1);
                // Remove query string remnants
                if (fileName.contains('?')) {
                    fileName = fileName.section('?', 0, 0);
                }
            }
            break;
        }
        default:
            break;
    }

    if (fileName.isEmpty()) {
        fileName = QString("download_%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    }

    return fileName;
}

bool DownloaderPanel::isValidUrl(const QString& url) const {
    QUrl qurl(url);
    if (!qurl.isValid() || qurl.scheme().isEmpty()) {
        return false;
    }

    DownloadSourceType type = detectUrlType(url);
    return type != DownloadSourceType::Unknown;
}

QString DownloaderPanel::formatFileSize(qint64 bytes) const {
    if (bytes < 1024) {
        return QString::number(bytes) + " B";
    } else if (bytes < 1024 * 1024) {
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    } else if (bytes < 1024 * 1024 * 1024) {
        return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    } else {
        return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
    }
}

} // namespace MegaCustom
