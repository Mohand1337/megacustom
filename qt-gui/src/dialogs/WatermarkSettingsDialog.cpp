#include "WatermarkSettingsDialog.h"
#include "features/Watermarker.h"
#include "widgets/ButtonFactory.h"
#include "utils/DpiScaler.h"
#include "utils/Settings.h"
#include "styles/ThemeManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QColorDialog>
#include <QMessageBox>
#include <QSettings>
#include <QScrollArea>
#include <QDir>

namespace MegaCustom {

static QString formatCacheBytes(qint64 bytes) {
    if (bytes < 1024LL * 1024LL) {
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    }
    if (bytes < 1024LL * 1024LL * 1024LL) {
        return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    }
    return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 1) + " GB";
}

WatermarkSettingsDialog::WatermarkSettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Watermark Settings");
    setMinimumWidth(DpiScaler::scale(550));
    setMinimumHeight(DpiScaler::scale(500));

    setupUI();
    loadFromSettings();
    updatePreview();
}

void WatermarkSettingsDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    // Tab widget for organized settings
    auto& tm = ThemeManager::instance();
    m_tabWidget = new QTabWidget();
    m_tabWidget->setStyleSheet(QString(R"(
        QTabWidget::pane {
            border: 1px solid %1;
            border-radius: 4px;
            background-color: %2;
        }
        QTabBar::tab {
            background-color: %3;
            color: %4;
            padding: 8px 16px;
            border: 1px solid %1;
            border-bottom: none;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
        }
        QTabBar::tab:selected {
            background-color: %2;
            color: %5;
        }
    )")
        .arg(tm.borderSubtle().name())
        .arg(tm.surfacePrimary().name())
        .arg(tm.surface2().name())
        .arg(tm.textDisabled().name())
        .arg(tm.textPrimary().name()));

    // =====================================================
    // Video Settings Tab
    // =====================================================
    QWidget* videoTab = new QWidget();
    QVBoxLayout* videoLayout = new QVBoxLayout(videoTab);
    videoLayout->setSpacing(12);

    // --- Timing Group ---
    QGroupBox* timingGroup = new QGroupBox("Timing");
    QFormLayout* timingForm = new QFormLayout(timingGroup);

    m_intervalSpin = new QSpinBox();
    m_intervalSpin->setRange(10, 3600);
    m_intervalSpin->setValue(600);
    m_intervalSpin->setSuffix(" seconds");
    m_intervalSpin->setToolTip("Time between watermark appearances (default: 600s = 10 minutes)");
    timingForm->addRow("Interval:", m_intervalSpin);

    m_durationSpin = new QSpinBox();
    m_durationSpin->setRange(1, 60);
    m_durationSpin->setValue(3);
    m_durationSpin->setSuffix(" seconds");
    m_durationSpin->setToolTip("How long the watermark is visible each time");
    timingForm->addRow("Duration:", m_durationSpin);

    m_randomGateSpin = new QDoubleSpinBox();
    m_randomGateSpin->setRange(0.01, 1.0);
    m_randomGateSpin->setValue(0.15);
    m_randomGateSpin->setSingleStep(0.05);
    m_randomGateSpin->setDecimals(2);
    m_randomGateSpin->setToolTip("Random position trigger threshold (lower = more random positions)");
    timingForm->addRow("Random Gate:", m_randomGateSpin);

    videoLayout->addWidget(timingGroup);

    // --- Appearance Group ---
    QGroupBox* appearanceGroup = new QGroupBox("Appearance");
    QGridLayout* appearanceGrid = new QGridLayout(appearanceGroup);
    appearanceGrid->setSpacing(8);

    // Font
    appearanceGrid->addWidget(new QLabel("Font File:"), 0, 0);
    m_fontPathEdit = new QLineEdit();
    m_fontPathEdit->setPlaceholderText("System default (arial.ttf)");
    appearanceGrid->addWidget(m_fontPathEdit, 0, 1);
    m_browseFontBtn = ButtonFactory::createSecondary("Browse...", this);
    connect(m_browseFontBtn, &QPushButton::clicked, this, &WatermarkSettingsDialog::onBrowseFont);
    appearanceGrid->addWidget(m_browseFontBtn, 0, 2);

    // Primary text settings
    appearanceGrid->addWidget(new QLabel("Primary Font Size:"), 1, 0);
    m_primaryFontSizeSpin = new QSpinBox();
    m_primaryFontSizeSpin->setRange(10, 72);
    m_primaryFontSizeSpin->setValue(26);
    appearanceGrid->addWidget(m_primaryFontSizeSpin, 1, 1);

    appearanceGrid->addWidget(new QLabel("Primary Color:"), 2, 0);
    QHBoxLayout* primaryColorLayout = new QHBoxLayout();
    m_primaryColorEdit = new QLineEdit("#d4a760");
    m_primaryColorEdit->setMaximumWidth(DpiScaler::scale(100));
    primaryColorLayout->addWidget(m_primaryColorEdit);
    m_primaryColorBtn = ButtonFactory::createSecondary("Pick...", this);
    m_primaryColorBtn->setMaximumWidth(DpiScaler::scale(60));
    connect(m_primaryColorBtn, &QPushButton::clicked, this, [this]() {
        QColor color = QColorDialog::getColor(QColor(m_primaryColorEdit->text()), this, "Primary Color");
        if (color.isValid()) {
            m_primaryColorEdit->setText(color.name());
            updatePreview();
        }
    });
    primaryColorLayout->addWidget(m_primaryColorBtn);
    primaryColorLayout->addStretch();
    appearanceGrid->addLayout(primaryColorLayout, 2, 1, 1, 2);

    // Secondary text settings
    appearanceGrid->addWidget(new QLabel("Secondary Font Size:"), 3, 0);
    m_secondaryFontSizeSpin = new QSpinBox();
    m_secondaryFontSizeSpin->setRange(10, 72);
    m_secondaryFontSizeSpin->setValue(22);
    appearanceGrid->addWidget(m_secondaryFontSizeSpin, 3, 1);

    appearanceGrid->addWidget(new QLabel("Secondary Color:"), 4, 0);
    QHBoxLayout* secondaryColorLayout = new QHBoxLayout();
    m_secondaryColorEdit = new QLineEdit("white");
    m_secondaryColorEdit->setMaximumWidth(DpiScaler::scale(100));
    secondaryColorLayout->addWidget(m_secondaryColorEdit);
    m_secondaryColorBtn = ButtonFactory::createSecondary("Pick...", this);
    m_secondaryColorBtn->setMaximumWidth(DpiScaler::scale(60));
    connect(m_secondaryColorBtn, &QPushButton::clicked, this, [this]() {
        QColor color = QColorDialog::getColor(QColor(m_secondaryColorEdit->text()), this, "Secondary Color");
        if (color.isValid()) {
            m_secondaryColorEdit->setText(color.name());
            updatePreview();
        }
    });
    secondaryColorLayout->addWidget(m_secondaryColorBtn);
    secondaryColorLayout->addStretch();
    appearanceGrid->addLayout(secondaryColorLayout, 4, 1, 1, 2);

    videoLayout->addWidget(appearanceGroup);

    // --- Encoding Group ---
    QGroupBox* encodingGroup = new QGroupBox("Encoding (FFmpeg)");
    QFormLayout* encodingForm = new QFormLayout(encodingGroup);

    m_presetCombo = new QComboBox();
    m_presetCombo->addItems({"ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow"});
    m_presetCombo->setCurrentText("ultrafast");
    m_presetCombo->setToolTip("Encoding speed preset (faster = larger file, slower = smaller file)");
    encodingForm->addRow("Preset:", m_presetCombo);

    m_crfSpin = new QSpinBox();
    m_crfSpin->setRange(0, 51);
    m_crfSpin->setValue(23);
    m_crfSpin->setToolTip("Constant Rate Factor: 0 = lossless, 18 = visually lossless, 23 = default, 28 = small file");
    encodingForm->addRow("CRF (Quality):", m_crfSpin);

    m_copyAudioCheck = new QCheckBox("Copy audio stream (no re-encoding)");
    m_copyAudioCheck->setChecked(true);
    m_copyAudioCheck->setToolTip("Faster and preserves original audio quality");
    encodingForm->addRow("", m_copyAudioCheck);

    m_fastSegmentedCheck = new QCheckBox("Use shared fast-segment cache when compatible");
    m_fastSegmentedCheck->setToolTip(
        "Encodes only watermark windows for compatible MP4/H.264/AAC videos. "
        "Other media automatically uses the standard full encode.");
    encodingForm->addRow("", m_fastSegmentedCheck);

    videoLayout->addWidget(encodingGroup);

    QGroupBox* cacheGroup = new QGroupBox("Shared Clean-Segment Cache");
    QFormLayout* cacheForm = new QFormLayout(cacheGroup);

    QHBoxLayout* cachePathRow = new QHBoxLayout();
    m_segmentCacheDirEdit = new QLineEdit();
    m_segmentCacheDirEdit->setPlaceholderText(
        QString::fromStdString(Watermarker::getDefaultSegmentCacheDirectory()));
    m_segmentCacheDirEdit->setToolTip(
        "Leave empty to use the private per-user cache. Choose a fast local SSD when overriding it.");
    cachePathRow->addWidget(m_segmentCacheDirEdit);
    m_browseSegmentCacheBtn = ButtonFactory::createSecondary("Browse...", this);
    connect(m_browseSegmentCacheBtn, &QPushButton::clicked,
            this, &WatermarkSettingsDialog::onBrowseSegmentCache);
    cachePathRow->addWidget(m_browseSegmentCacheBtn);
    cacheForm->addRow("Cache folder:", cachePathRow);

    m_segmentCacheMaxGiBSpin = new QSpinBox();
    m_segmentCacheMaxGiBSpin->setRange(1, 2048);
    m_segmentCacheMaxGiBSpin->setValue(100);
    m_segmentCacheMaxGiBSpin->setSuffix(" GB");
    m_segmentCacheMaxGiBSpin->setToolTip(
        "Soft limit applied before the next watermark job. A running multi-member job may temporarily exceed it.");
    cacheForm->addRow("Maximum size:", m_segmentCacheMaxGiBSpin);

    m_segmentCacheMaxAgeDaysSpin = new QSpinBox();
    m_segmentCacheMaxAgeDaysSpin->setRange(1, 365);
    m_segmentCacheMaxAgeDaysSpin->setValue(30);
    m_segmentCacheMaxAgeDaysSpin->setSuffix(" days");
    cacheForm->addRow("Keep unused entries:", m_segmentCacheMaxAgeDaysSpin);

    QHBoxLayout* cacheStatusRow = new QHBoxLayout();
    m_segmentCacheStatusLabel = new QLabel("Checking...");
    m_segmentCacheStatusLabel->setWordWrap(true);
    cacheStatusRow->addWidget(m_segmentCacheStatusLabel, 1);
    m_clearSegmentCacheBtn = ButtonFactory::createOutline("Clear Cache", this);
    connect(m_clearSegmentCacheBtn, &QPushButton::clicked,
            this, &WatermarkSettingsDialog::onClearSegmentCache);
    cacheStatusRow->addWidget(m_clearSegmentCacheBtn);
    cacheForm->addRow("Current cache:", cacheStatusRow);

    connect(m_segmentCacheDirEdit, &QLineEdit::editingFinished,
            this, &WatermarkSettingsDialog::refreshSegmentCacheStatus);
    videoLayout->addWidget(cacheGroup);
    videoLayout->addStretch();

    m_tabWidget->addTab(videoTab, "Video");

    // =====================================================
    // PDF Settings Tab
    // =====================================================
    QWidget* pdfTab = new QWidget();
    QVBoxLayout* pdfLayout = new QVBoxLayout(pdfTab);
    pdfLayout->setSpacing(12);

    QGroupBox* pdfGroup = new QGroupBox("PDF Watermark Settings");
    QFormLayout* pdfForm = new QFormLayout(pdfGroup);

    m_pdfOpacitySpin = new QDoubleSpinBox();
    m_pdfOpacitySpin->setRange(0.1, 1.0);
    m_pdfOpacitySpin->setValue(0.3);
    m_pdfOpacitySpin->setSingleStep(0.1);
    m_pdfOpacitySpin->setDecimals(2);
    m_pdfOpacitySpin->setToolTip("Watermark transparency (0.1 = very transparent, 1.0 = opaque)");
    pdfForm->addRow("Opacity:", m_pdfOpacitySpin);

    m_pdfAngleSpin = new QSpinBox();
    m_pdfAngleSpin->setRange(-90, 90);
    m_pdfAngleSpin->setValue(45);
    m_pdfAngleSpin->setSuffix(" degrees");
    m_pdfAngleSpin->setToolTip("Watermark rotation angle (45 = diagonal)");
    pdfForm->addRow("Angle:", m_pdfAngleSpin);

    m_pdfCoverageSpin = new QDoubleSpinBox();
    m_pdfCoverageSpin->setRange(0.1, 1.0);
    m_pdfCoverageSpin->setValue(0.5);
    m_pdfCoverageSpin->setSingleStep(0.1);
    m_pdfCoverageSpin->setDecimals(2);
    m_pdfCoverageSpin->setToolTip("Fraction of pages to watermark (0.5 = 50% of pages, randomly selected)");
    pdfForm->addRow("Page Coverage:", m_pdfCoverageSpin);

    m_pdfPasswordCheck = new QCheckBox("Protect PDF with password");
    pdfForm->addRow("", m_pdfPasswordCheck);

    m_pdfPasswordEdit = new QLineEdit();
    m_pdfPasswordEdit->setEchoMode(QLineEdit::Password);
    m_pdfPasswordEdit->setPlaceholderText("Enter PDF password");
    m_pdfPasswordEdit->setEnabled(false);
    connect(m_pdfPasswordCheck, &QCheckBox::toggled, m_pdfPasswordEdit, &QLineEdit::setEnabled);
    pdfForm->addRow("Password:", m_pdfPasswordEdit);

    pdfLayout->addWidget(pdfGroup);
    pdfLayout->addStretch();

    m_tabWidget->addTab(pdfTab, "PDF");

    // =====================================================
    // Output Settings Tab
    // =====================================================
    QWidget* outputTab = new QWidget();
    QVBoxLayout* outputLayout = new QVBoxLayout(outputTab);
    outputLayout->setSpacing(12);

    QGroupBox* outputGroup = new QGroupBox("Output Settings");
    QFormLayout* outputForm = new QFormLayout(outputGroup);

    m_outputSuffixEdit = new QLineEdit("_wm");
    m_outputSuffixEdit->setToolTip("Suffix added to output filename (e.g., video_wm.mp4)");
    outputForm->addRow("Output Suffix:", m_outputSuffixEdit);

    m_overwriteCheck = new QCheckBox("Overwrite existing output files");
    m_overwriteCheck->setChecked(true);
    outputForm->addRow("", m_overwriteCheck);

    outputLayout->addWidget(outputGroup);

    // --- Dependencies Status ---
    QGroupBox* depsGroup = new QGroupBox("Dependencies");
    QVBoxLayout* depsLayout = new QVBoxLayout(depsGroup);

    QHBoxLayout* ffmpegRow = new QHBoxLayout();
    ffmpegRow->addWidget(new QLabel("FFmpeg:"));
    m_ffmpegStatusLabel = new QLabel("Checking...");
    ffmpegRow->addWidget(m_ffmpegStatusLabel);
    ffmpegRow->addStretch();
    depsLayout->addLayout(ffmpegRow);

    QHBoxLayout* pythonRow = new QHBoxLayout();
    pythonRow->addWidget(new QLabel("Python + PDF libs:"));
    m_pythonStatusLabel = new QLabel("Checking...");
    pythonRow->addWidget(m_pythonStatusLabel);
    pythonRow->addStretch();
    depsLayout->addLayout(pythonRow);

    QPushButton* checkDepsBtn = ButtonFactory::createOutline("Check Dependencies", this);
    connect(checkDepsBtn, &QPushButton::clicked, this, &WatermarkSettingsDialog::onCheckDependencies);
    depsLayout->addWidget(checkDepsBtn);

    outputLayout->addWidget(depsGroup);
    outputLayout->addStretch();

    m_tabWidget->addTab(outputTab, "Output");

    mainLayout->addWidget(m_tabWidget);

    // =====================================================
    // Preview Section
    // =====================================================
    QGroupBox* previewGroup = new QGroupBox("Preview");
    QVBoxLayout* previewLayout = new QVBoxLayout(previewGroup);

    m_previewLabel = new QLabel();
    m_previewLabel->setWordWrap(true);
    m_previewLabel->setStyleSheet(QString(R"(
        QLabel {
            background-color: %1;
            padding: 12px;
            border-radius: 4px;
            font-family: monospace;
        }
    )").arg(tm.surface2().name()));
    previewLayout->addWidget(m_previewLabel);

    mainLayout->addWidget(previewGroup);

    // =====================================================
    // Dialog Buttons
    // =====================================================
    QHBoxLayout* btnLayout = new QHBoxLayout();

    QPushButton* resetBtn = ButtonFactory::createOutline("Reset to Defaults", this);
    connect(resetBtn, &QPushButton::clicked, this, &WatermarkSettingsDialog::onResetDefaults);
    btnLayout->addWidget(resetBtn);

    btnLayout->addStretch();

    QPushButton* cancelBtn = ButtonFactory::createOutline("Cancel", this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);

    QPushButton* okBtn = ButtonFactory::createPrimary("OK", this);
    connect(okBtn, &QPushButton::clicked, this, [this]() {
        saveToSettings();
        accept();
    });
    btnLayout->addWidget(okBtn);

    mainLayout->addLayout(btnLayout);

    // Connect signals for live preview
    connect(m_primaryColorEdit, &QLineEdit::textChanged, this, &WatermarkSettingsDialog::updatePreview);
    connect(m_secondaryColorEdit, &QLineEdit::textChanged, this, &WatermarkSettingsDialog::updatePreview);
    connect(m_primaryFontSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &WatermarkSettingsDialog::updatePreview);
    connect(m_secondaryFontSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &WatermarkSettingsDialog::updatePreview);

    // Check dependencies on startup
    onCheckDependencies();
}

void WatermarkSettingsDialog::setConfig(const WatermarkConfig& config) {
    loadConfig(config);
    updatePreview();
}

WatermarkConfig WatermarkSettingsDialog::getConfig() const {
    WatermarkConfig config;

    // Video timing
    config.intervalSeconds = m_intervalSpin->value();
    config.durationSeconds = m_durationSpin->value();
    config.randomGate = m_randomGateSpin->value();

    // Appearance
    config.fontPath = m_fontPathEdit->text().toStdString();
    config.primaryFontSize = m_primaryFontSizeSpin->value();
    config.secondaryFontSize = m_secondaryFontSizeSpin->value();
    config.primaryColor = m_primaryColorEdit->text().toStdString();
    config.secondaryColor = m_secondaryColorEdit->text().toStdString();

    // Encoding
    config.preset = m_presetCombo->currentText().toStdString();
    config.crf = m_crfSpin->value();
    config.copyAudio = m_copyAudioCheck->isChecked();
    config.fastSegmentedEncode = m_fastSegmentedCheck->isChecked();
    config.segmentCacheDirectory = m_segmentCacheDirEdit->text().trimmed().toStdString();
    config.segmentCacheMaxBytes = static_cast<int64_t>(m_segmentCacheMaxGiBSpin->value())
        * 1024LL * 1024LL * 1024LL;
    config.segmentCacheMaxAgeDays = m_segmentCacheMaxAgeDaysSpin->value();

    // PDF
    config.pdfOpacity = m_pdfOpacitySpin->value();
    config.pdfAngle = m_pdfAngleSpin->value();
    config.pdfCoverage = m_pdfCoverageSpin->value();
    if (m_pdfPasswordCheck->isChecked()) {
        config.pdfPassword = m_pdfPasswordEdit->text().toStdString();
    }

    // Output
    config.outputSuffix = m_outputSuffixEdit->text().toStdString();
    config.overwrite = m_overwriteCheck->isChecked();

    return config;
}

void WatermarkSettingsDialog::loadConfig(const WatermarkConfig& config) {
    // Video timing
    m_intervalSpin->setValue(config.intervalSeconds);
    m_durationSpin->setValue(config.durationSeconds);
    m_randomGateSpin->setValue(config.randomGate);

    // Appearance
    m_fontPathEdit->setText(QString::fromStdString(config.fontPath));
    m_primaryFontSizeSpin->setValue(config.primaryFontSize);
    m_secondaryFontSizeSpin->setValue(config.secondaryFontSize);
    m_primaryColorEdit->setText(QString::fromStdString(config.primaryColor));
    m_secondaryColorEdit->setText(QString::fromStdString(config.secondaryColor));

    // Encoding
    m_presetCombo->setCurrentText(QString::fromStdString(config.preset));
    m_crfSpin->setValue(config.crf);
    m_copyAudioCheck->setChecked(config.copyAudio);
    m_fastSegmentedCheck->setChecked(config.fastSegmentedEncode);
    m_segmentCacheDirEdit->setText(QString::fromStdString(config.segmentCacheDirectory));
    const qint64 cacheGiB = qMax<qint64>(1, config.segmentCacheMaxBytes / (1024LL * 1024LL * 1024LL));
    m_segmentCacheMaxGiBSpin->setValue(static_cast<int>(qMin<qint64>(2048, cacheGiB)));
    m_segmentCacheMaxAgeDaysSpin->setValue(config.segmentCacheMaxAgeDays);
    refreshSegmentCacheStatus();

    // PDF
    m_pdfOpacitySpin->setValue(config.pdfOpacity);
    m_pdfAngleSpin->setValue(config.pdfAngle);
    m_pdfCoverageSpin->setValue(config.pdfCoverage);
    if (!config.pdfPassword.empty()) {
        m_pdfPasswordCheck->setChecked(true);
        m_pdfPasswordEdit->setText(QString::fromStdString(config.pdfPassword));
    }

    // Output
    m_outputSuffixEdit->setText(QString::fromStdString(config.outputSuffix));
    m_overwriteCheck->setChecked(config.overwrite);
}

void WatermarkSettingsDialog::saveToSettings() {
    QSettings settings(Settings::instance().configDirectory() + "/watermark.ini",
                       QSettings::IniFormat);
    settings.beginGroup("Watermark");

    // Video timing
    settings.setValue("intervalSeconds", m_intervalSpin->value());
    settings.setValue("durationSeconds", m_durationSpin->value());
    settings.setValue("randomGate", m_randomGateSpin->value());

    // Appearance
    settings.setValue("fontPath", m_fontPathEdit->text());
    settings.setValue("primaryFontSize", m_primaryFontSizeSpin->value());
    settings.setValue("secondaryFontSize", m_secondaryFontSizeSpin->value());
    settings.setValue("primaryColor", m_primaryColorEdit->text());
    settings.setValue("secondaryColor", m_secondaryColorEdit->text());

    // Encoding
    settings.setValue("preset", m_presetCombo->currentText());
    settings.setValue("crf", m_crfSpin->value());
    settings.setValue("copyAudio", m_copyAudioCheck->isChecked());
    settings.setValue("fastSegmentedEncode", m_fastSegmentedCheck->isChecked());
    settings.setValue("segmentCacheDirectory", m_segmentCacheDirEdit->text().trimmed());
    settings.setValue("segmentCacheMaxGiB", m_segmentCacheMaxGiBSpin->value());
    settings.setValue("segmentCacheMaxAgeDays", m_segmentCacheMaxAgeDaysSpin->value());

    // PDF
    settings.setValue("pdfOpacity", m_pdfOpacitySpin->value());
    settings.setValue("pdfAngle", m_pdfAngleSpin->value());
    settings.setValue("pdfCoverage", m_pdfCoverageSpin->value());
    settings.setValue("pdfPasswordEnabled", m_pdfPasswordCheck->isChecked());
    // Note: Don't save actual password to settings for security

    // Output
    settings.setValue("outputSuffix", m_outputSuffixEdit->text());
    settings.setValue("overwrite", m_overwriteCheck->isChecked());

    settings.endGroup();

    emit configChanged();
}

void WatermarkSettingsDialog::loadFromSettings() {
    QSettings settings(Settings::instance().configDirectory() + "/watermark.ini",
                       QSettings::IniFormat);
    settings.beginGroup("Watermark");

    // Video timing
    m_intervalSpin->setValue(settings.value("intervalSeconds", 600).toInt());
    m_durationSpin->setValue(settings.value("durationSeconds", 3).toInt());
    m_randomGateSpin->setValue(settings.value("randomGate", 0.15).toDouble());

    // Appearance
    m_fontPathEdit->setText(settings.value("fontPath", "").toString());
    m_primaryFontSizeSpin->setValue(settings.value("primaryFontSize", 26).toInt());
    m_secondaryFontSizeSpin->setValue(settings.value("secondaryFontSize", 22).toInt());
    m_primaryColorEdit->setText(settings.value("primaryColor", "#d4a760").toString());
    m_secondaryColorEdit->setText(settings.value("secondaryColor", "white").toString());

    // Encoding
    m_presetCombo->setCurrentText(settings.value("preset", "ultrafast").toString());
    m_crfSpin->setValue(settings.value("crf", 23).toInt());
    m_copyAudioCheck->setChecked(settings.value("copyAudio", true).toBool());
    m_fastSegmentedCheck->setChecked(settings.value("fastSegmentedEncode", false).toBool());
    m_segmentCacheDirEdit->setText(settings.value("segmentCacheDirectory").toString());
    m_segmentCacheMaxGiBSpin->setValue(settings.value("segmentCacheMaxGiB", 100).toInt());
    m_segmentCacheMaxAgeDaysSpin->setValue(settings.value("segmentCacheMaxAgeDays", 30).toInt());

    // PDF
    m_pdfOpacitySpin->setValue(settings.value("pdfOpacity", 0.3).toDouble());
    m_pdfAngleSpin->setValue(settings.value("pdfAngle", 45).toInt());
    m_pdfCoverageSpin->setValue(settings.value("pdfCoverage", 0.5).toDouble());
    m_pdfPasswordCheck->setChecked(settings.value("pdfPasswordEnabled", false).toBool());

    // Output
    m_outputSuffixEdit->setText(settings.value("outputSuffix", "_wm").toString());
    m_overwriteCheck->setChecked(settings.value("overwrite", true).toBool());

    settings.endGroup();
    refreshSegmentCacheStatus();
}

void WatermarkSettingsDialog::onBrowseFont() {
    QString fontPath = QFileDialog::getOpenFileName(this,
        "Select Font File",
        QString(),
        "Font Files (*.ttf *.otf);;All Files (*)");

    if (!fontPath.isEmpty()) {
        m_fontPathEdit->setText(fontPath);
    }
}

void WatermarkSettingsDialog::onBrowseSegmentCache() {
    QString initial = m_segmentCacheDirEdit->text().trimmed();
    if (initial.isEmpty()) {
        initial = QString::fromStdString(Watermarker::getDefaultSegmentCacheDirectory());
    }
    const QString directory = QFileDialog::getExistingDirectory(
        this, "Select Shared Segment Cache", initial,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!directory.isEmpty()) {
        m_segmentCacheDirEdit->setText(QDir::cleanPath(directory));
        refreshSegmentCacheStatus();
    }
}

void WatermarkSettingsDialog::onClearSegmentCache() {
    const int reply = QMessageBox::question(
        this,
        "Clear Segment Cache",
        "Delete all cached clean video segments? Future member runs will rebuild them from the source videos.",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }

    std::string error;
    if (!Watermarker::clearSegmentCache(m_segmentCacheDirEdit->text().trimmed().toStdString(), error)) {
        QMessageBox::warning(this, "Cache In Use",
            QString::fromStdString(error.empty() ? "The segment cache could not be cleared." : error));
    }
    refreshSegmentCacheStatus();
}

void WatermarkSettingsDialog::refreshSegmentCacheStatus() {
    const SegmentCacheStats stats = Watermarker::getSegmentCacheStats(
        m_segmentCacheDirEdit->text().trimmed().toStdString());
    if (!stats.error.empty()) {
        m_segmentCacheStatusLabel->setText("Unavailable: " + QString::fromStdString(stats.error));
        return;
    }

    m_segmentCacheStatusLabel->setText(QString("%1 across %2 reusable source %3%4")
        .arg(formatCacheBytes(stats.sizeBytes))
        .arg(stats.entryCount)
        .arg(stats.entryCount == 1 ? "entry" : "entries")
        .arg(stats.incompleteEntryCount > 0
            ? QString("; %1 incomplete").arg(stats.incompleteEntryCount)
            : QString()));
    m_segmentCacheStatusLabel->setToolTip(QString::fromStdString(stats.directory));
}

void WatermarkSettingsDialog::onResetDefaults() {
    int ret = QMessageBox::question(this, "Reset Settings",
        "Reset all watermark settings to defaults?",
        QMessageBox::Yes | QMessageBox::No);

    if (ret == QMessageBox::Yes) {
        WatermarkConfig defaults;
        loadConfig(defaults);
        updatePreview();
    }
}

void WatermarkSettingsDialog::onPreviewWatermark() {
    // Could show a sample watermarked image/video frame
    QMessageBox::information(this, "Preview",
        "Preview functionality coming soon.\n\n"
        "Current settings have been applied to the preview panel.");
}

void WatermarkSettingsDialog::onCheckDependencies() {
    auto& tm = ThemeManager::instance();

    // Check FFmpeg
    if (Watermarker::isFFmpegAvailable()) {
        m_ffmpegStatusLabel->setText(QString("<span style='color: %1;'>Available</span>")
            .arg(tm.supportSuccess().name()));
    } else {
        m_ffmpegStatusLabel->setText(QString("<span style='color: %1;'>Not found</span>")
            .arg(tm.supportError().name()));
    }

    // Check Python
    if (Watermarker::isPythonAvailable()) {
        m_pythonStatusLabel->setText(QString("<span style='color: %1;'>Available</span>")
            .arg(tm.supportSuccess().name()));
    } else {
        m_pythonStatusLabel->setText(QString("<span style='color: %1;'>Missing reportlab/PyPDF2</span>")
            .arg(tm.supportWarning().name()));
    }
}

void WatermarkSettingsDialog::updatePreview() {
    auto& tm = ThemeManager::instance();
    QString primaryColor = m_primaryColorEdit->text();
    QString secondaryColor = m_secondaryColorEdit->text();

    // Validate colors
    QColor pc(primaryColor);
    QColor sc(secondaryColor);
    if (!pc.isValid()) primaryColor = "#d4a760";
    if (!sc.isValid()) secondaryColor = "white";

    QString html = QString(R"(
        <div style='margin-bottom: 8px;'>
            <span style='color: %1; font-size: %2px; font-weight: bold;'>
                Easygroupbuys.com - Member #EGB001
            </span>
        </div>
        <div>
            <span style='color: %3; font-size: %4px;'>
                email@example.com - IP: 192.168.1.1
            </span>
        </div>
        <div style='margin-top: 12px; color: %9; font-size: 11px;'>
            Appears every %5s for %6s | Preset: %7 | CRF: %8
        </div>
    )")
        .arg(primaryColor)
        .arg(m_primaryFontSizeSpin->value())
        .arg(secondaryColor)
        .arg(m_secondaryFontSizeSpin->value())
        .arg(m_intervalSpin->value())
        .arg(m_durationSpin->value())
        .arg(m_presetCombo->currentText())
        .arg(m_crfSpin->value())
        .arg(tm.textSecondary().name());

    m_previewLabel->setText(html);
}

} // namespace MegaCustom
