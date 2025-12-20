#ifndef MEGACUSTOM_WATERMARKSETTINGSDIALOG_H
#define MEGACUSTOM_WATERMARKSETTINGSDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QTabWidget>

namespace MegaCustom {

struct WatermarkConfig;

/**
 * Dialog for configuring detailed watermark settings
 * Provides access to all WatermarkConfig options for video and PDF watermarking
 */
class WatermarkSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit WatermarkSettingsDialog(QWidget* parent = nullptr);
    ~WatermarkSettingsDialog() = default;

    /**
     * Set config to edit (loads values into UI)
     */
    void setConfig(const WatermarkConfig& config);

    /**
     * Get the configured settings
     */
    WatermarkConfig getConfig() const;

signals:
    void configChanged();

private slots:
    void onBrowseFont();
    void onResetDefaults();
    void onPreviewWatermark();
    void onCheckDependencies();
    void updatePreview();

private:
    void setupUI();
    void loadConfig(const WatermarkConfig& config);
    void saveToSettings();
    void loadFromSettings();

    // === Video Settings Tab ===
    // Timing
    QSpinBox* m_intervalSpin;         // Seconds between watermark appearances
    QSpinBox* m_durationSpin;         // How long watermark shows
    QDoubleSpinBox* m_randomGateSpin; // Random position trigger threshold

    // Appearance
    QLineEdit* m_fontPathEdit;
    QPushButton* m_browseFontBtn;
    QSpinBox* m_primaryFontSizeSpin;
    QSpinBox* m_secondaryFontSizeSpin;
    QLineEdit* m_primaryColorEdit;
    QLineEdit* m_secondaryColorEdit;
    QPushButton* m_primaryColorBtn;
    QPushButton* m_secondaryColorBtn;

    // Encoding
    QComboBox* m_presetCombo;
    QSpinBox* m_crfSpin;
    QCheckBox* m_copyAudioCheck;

    // === PDF Settings Tab ===
    QDoubleSpinBox* m_pdfOpacitySpin;
    QSpinBox* m_pdfAngleSpin;
    QDoubleSpinBox* m_pdfCoverageSpin;
    QLineEdit* m_pdfPasswordEdit;
    QCheckBox* m_pdfPasswordCheck;

    // === Output Settings Tab ===
    QLineEdit* m_outputSuffixEdit;
    QCheckBox* m_overwriteCheck;

    // === Preview ===
    QLabel* m_previewLabel;

    // === Status ===
    QLabel* m_ffmpegStatusLabel;
    QLabel* m_pythonStatusLabel;

    // Tab widget
    QTabWidget* m_tabWidget;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_WATERMARKSETTINGSDIALOG_H
