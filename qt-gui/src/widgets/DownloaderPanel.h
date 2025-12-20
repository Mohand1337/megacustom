#ifndef MEGACUSTOM_DOWNLOADERPANEL_H
#define MEGACUSTOM_DOWNLOADERPANEL_H

#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QLabel>
#include <QProgressBar>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QGroupBox>
#include <QThread>
#include <QProcess>
#include <memory>

namespace MegaCustom {

/**
 * Download source types (matches Python detect_url_type)
 */
enum class DownloadSourceType {
    BunnyCDN,           // iframe.mediadelivery.net/embed/
    GoogleDriveFile,    // drive.google.com/file/d/
    GoogleDriveFolder,  // drive.google.com/drive/folders/
    GoogleDocs,         // docs.google.com/(document|spreadsheets|presentation)/
    Dropbox,            // dropbox.com
    GenericHTTP,        // Direct URLs ending in .mp4, .pdf, etc.
    Unknown
};

/**
 * Info about a download item in the queue
 */
struct DownloadItemInfo {
    QString url;
    QString fileName;              // Detected or extracted filename
    DownloadSourceType sourceType;
    bool isValid = true;           // URL validation result
    QString status;                // "pending", "downloading", "complete", "error", "skipped"
    int progressPercent = 0;
    QString outputPath;            // Final downloaded file path
    QString error;
    qint64 bytesDownloaded = 0;
    qint64 totalBytes = 0;
    QString speed;                 // e.g., "2.5 MB/s"
    QString eta;                   // e.g., "00:05:32"
};

/**
 * Worker thread for download operations
 * Spawns Python subprocess and parses progress
 */
class DownloadWorker : public QObject {
    Q_OBJECT
public:
    explicit DownloadWorker(QObject* parent = nullptr);
    ~DownloadWorker();

    void setUrls(const QStringList& urls);
    void setOutputDir(const QString& dir);
    void setConfig(int maxParallel, const QString& quality, bool skipExisting,
                   bool downloadSubtitles, const QString& docsFormat);
    QString findPythonScript() const;

public slots:
    void process();
    void cancel();

signals:
    void started();
    void progress(int itemIndex, int totalItems, const QString& file,
                  int percent, const QString& speed, const QString& eta);
    void itemCompleted(int itemIndex, bool success, const QString& outputPath,
                       const QString& error);
    void finished(int successCount, int failCount);
    void logMessage(const QString& message);

private:
    void downloadSingle(int index, const QString& url);
    void parseProgressLine(const QString& line);

    QStringList m_urls;
    QString m_outputDir;
    int m_maxParallel = 3;
    QString m_quality;
    bool m_skipExisting = true;
    bool m_downloadSubtitles = true;
    QString m_docsFormat;
    bool m_cancelled = false;
    int m_currentIndex = 0;
    QProcess* m_process = nullptr;
};

/**
 * Panel for downloading content from multiple sources
 * Supports: BunnyCDN (with DRM), Google Drive, Google Docs, Dropbox, generic HTTP
 * First step in content pipeline: Download -> Watermark -> Upload -> Distribute
 */
class DownloaderPanel : public QWidget {
    Q_OBJECT

public:
    explicit DownloaderPanel(QWidget* parent = nullptr);
    ~DownloaderPanel();

signals:
    // Pipeline integration - send files to WatermarkPanel
    void downloadCompleted(const QString& filePath, const QString& sourceUrl);
    void downloadsCompleted(const QStringList& filePaths);
    void sendToWatermark(const QStringList& filePaths);

    // Status signals for MainWindow
    void downloadStarted();
    void downloadProgress(int current, int total, const QString& file);
    void allDownloadsCompleted(int success, int failed);

public slots:
    void refresh();

private slots:
    // URL input
    void onParseUrls();
    void onClearInput();

    // Queue management
    void onRemoveSelected();
    void onClearCompleted();
    void onClearAll();

    // Settings
    void onBrowseOutput();

    // Actions
    void onStartDownloads();
    void onStopDownloads();
    void onCheckDependencies();

    // Watermark integration
    void onSendToWatermark();
    void onAutoSendToggled(bool checked);

    // Table
    void onTableSelectionChanged();

    // Worker signals
    void onWorkerProgress(int itemIndex, int totalItems, const QString& file,
                          int percent, const QString& speed, const QString& eta);
    void onWorkerItemCompleted(int itemIndex, bool success,
                               const QString& outputPath, const QString& error);
    void onWorkerFinished(int successCount, int failCount);
    void onWorkerLog(const QString& message);

private:
    void setupUI();
    void populateTable();
    void updateStats();
    void updateButtonStates();
    void checkAndAutoSend();

    // URL utilities (C++ for immediate UI feedback)
    DownloadSourceType detectUrlType(const QString& url) const;
    QStringList extractUrlsFromText(const QString& text) const;
    QString sourceTypeToString(DownloadSourceType type) const;
    QString extractFileName(const QString& url, DownloadSourceType type) const;
    bool isValidUrl(const QString& url) const;

    // Formatting
    QString formatFileSize(qint64 bytes) const;

    // UI Components - URL Input Section
    QPlainTextEdit* m_urlInput;
    QPushButton* m_parseBtn;
    QPushButton* m_clearInputBtn;

    // UI Components - Download Queue
    QTableWidget* m_downloadTable;
    QPushButton* m_removeBtn;
    QPushButton* m_clearCompletedBtn;
    QPushButton* m_clearAllBtn;

    // UI Components - Settings
    QLineEdit* m_outputDirEdit;
    QPushButton* m_browseOutputBtn;
    QComboBox* m_qualityCombo;
    QSpinBox* m_parallelSpin;
    QComboBox* m_docsFormatCombo;
    QCheckBox* m_skipExistingCheck;
    QCheckBox* m_downloadSubtitlesCheck;

    // UI Components - Watermark Integration
    QCheckBox* m_autoSendCheck;
    QPushButton* m_sendToWatermarkBtn;

    // UI Components - Actions
    QPushButton* m_startBtn;
    QPushButton* m_stopBtn;
    QPushButton* m_checkDepsBtn;

    // UI Components - Progress
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;
    QLabel* m_statsLabel;

    // Data
    QList<DownloadItemInfo> m_items;
    QStringList m_completedFiles;  // Files ready to send to watermark
    bool m_isRunning = false;

    // Worker thread
    QThread* m_workerThread = nullptr;
    DownloadWorker* m_worker = nullptr;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_DOWNLOADERPANEL_H
