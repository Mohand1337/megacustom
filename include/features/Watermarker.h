#ifndef WATERMARKER_H
#define WATERMARKER_H

#include <string>
#include <vector>
#include <functional>
#include <map>
#include <cstdint>
#include <future>

namespace MegaCustom {

/**
 * Watermark configuration for video/PDF processing
 */
struct WatermarkConfig {
    // Primary text (line 1)
    std::string primaryText;      // e.g., "Easygroupbuys.com - Member #EGB001"
    std::string secondaryText;    // e.g., "email@example.com - IP: 1.2.3.4"

    // Timing (for video)
    int intervalSeconds = 600;    // Time between appearances (default: 10 min)
    int durationSeconds = 3;      // How long watermark shows
    double randomGate = 0.15;     // Random position trigger threshold

    // Appearance
    std::string fontPath;         // Path to font file (empty = system default)
    int primaryFontSize = 26;
    int secondaryFontSize = 22;
    std::string primaryColor = "#d4a760";   // Golden color
    std::string secondaryColor = "white";

    // Encoding (for video)
    std::string preset = "ultrafast";  // FFmpeg preset: ultrafast/fast/medium/slow
    int crf = 23;                      // Quality (18-28, lower = better)
    bool copyAudio = true;             // Copy audio stream without re-encoding

    // PDF-specific
    double pdfOpacity = 0.3;           // Watermark opacity (0.0-1.0)
    int pdfAngle = 45;                 // Rotation angle in degrees
    double pdfCoverage = 0.5;          // Fraction of pages to watermark (0.0-1.0)
    std::string pdfPassword;           // Optional PDF password

    // Output
    std::string outputSuffix = "_wm";  // Suffix for output filename
    bool overwrite = true;             // Overwrite existing output files
};

/**
 * Result of a watermark operation
 */
struct WatermarkResult {
    bool success = false;
    std::string inputFile;
    std::string outputFile;
    std::string error;
    int64_t processingTimeMs = 0;
    int64_t inputSizeBytes = 0;
    int64_t outputSizeBytes = 0;
};

/**
 * Progress callback for watermark operations
 */
struct WatermarkProgress {
    std::string currentFile;
    int currentIndex = 0;
    int totalFiles = 0;
    double percentComplete = 0.0;
    std::string status;  // "encoding", "processing", "complete", "error"
};

using WatermarkProgressCallback = std::function<void(const WatermarkProgress&)>;

/**
 * Watermarker class
 * Handles video watermarking (FFmpeg) and PDF watermarking (Python script)
 */
class Watermarker {
public:
    Watermarker();
    ~Watermarker() = default;

    // ==================== Configuration ====================

    /**
     * Set watermark configuration
     */
    void setConfig(const WatermarkConfig& config) { m_config = config; }
    WatermarkConfig getConfig() const { return m_config; }

    /**
     * Set progress callback
     */
    void setProgressCallback(WatermarkProgressCallback callback) {
        m_progressCallback = callback;
    }

    /**
     * Check if FFmpeg is available
     */
    static bool isFFmpegAvailable();

    /**
     * Check if Python with required modules is available
     */
    static bool isPythonAvailable();

    /**
     * Get path to bundled PDF watermark script
     */
    static std::string getPdfScriptPath();

    // ==================== Video Watermarking ====================

    /**
     * Watermark a single video file
     * @param inputPath Path to input video
     * @param outputPath Path to output video (empty = auto-generate)
     * @return Result with success status and output path
     */
    WatermarkResult watermarkVideo(const std::string& inputPath,
                                   const std::string& outputPath = "");

    /**
     * Async version of watermarkVideo - runs FFmpeg in background
     * @param inputPath Path to input video
     * @param outputPath Path to output video (empty = auto-generate)
     * @return Future that will contain the result
     */
    std::future<WatermarkResult> watermarkVideoAsync(const std::string& inputPath,
                                                     const std::string& outputPath = "");

    /**
     * Watermark video with member-specific text
     * Uses member's watermark fields to build text
     * @param inputPath Path to input video
     * @param memberId Member ID to get watermark info from
     * @param outputDir Output directory (empty = same as input)
     * @return Result with success status
     */
    WatermarkResult watermarkVideoForMember(const std::string& inputPath,
                                            const std::string& memberId,
                                            const std::string& outputDir = "");

    /**
     * Async version of watermarkVideoForMember
     */
    std::future<WatermarkResult> watermarkVideoForMemberAsync(const std::string& inputPath,
                                                               const std::string& memberId,
                                                               const std::string& outputDir = "");

    /**
     * Batch watermark multiple videos
     * @param inputPaths List of input video paths
     * @param outputDir Output directory
     * @param parallel Number of parallel processes (default: 1)
     * @return Results for each file
     */
    std::vector<WatermarkResult> watermarkVideoBatch(
        const std::vector<std::string>& inputPaths,
        const std::string& outputDir,
        int parallel = 1);

    // ==================== PDF Watermarking ====================

    /**
     * Watermark a single PDF file
     * @param inputPath Path to input PDF
     * @param outputPath Path to output PDF (empty = auto-generate)
     * @return Result with success status
     */
    WatermarkResult watermarkPdf(const std::string& inputPath,
                                 const std::string& outputPath = "");

    /**
     * Watermark PDF with member-specific text
     * @param inputPath Path to input PDF
     * @param memberId Member ID to get watermark info from
     * @param outputDir Output directory (empty = same as input)
     * @return Result with success status
     */
    WatermarkResult watermarkPdfForMember(const std::string& inputPath,
                                          const std::string& memberId,
                                          const std::string& outputDir = "");

    /**
     * Batch watermark multiple PDFs
     * @param inputPaths List of input PDF paths
     * @param outputDir Output directory
     * @param parallel Number of parallel processes (default: 1)
     * @return Results for each file
     */
    std::vector<WatermarkResult> watermarkPdfBatch(
        const std::vector<std::string>& inputPaths,
        const std::string& outputDir,
        int parallel = 1);

    // ==================== Auto-Detection ====================

    /**
     * Watermark a file (auto-detect type from extension)
     * @param inputPath Path to input file
     * @param outputPath Path to output file (empty = auto-generate)
     * @return Result with success status
     */
    WatermarkResult watermarkFile(const std::string& inputPath,
                                  const std::string& outputPath = "");

    /**
     * Watermark all supported files in a directory
     * @param inputDir Input directory path
     * @param outputDir Output directory path
     * @param recursive Process subdirectories
     * @param parallel Number of parallel processes
     * @return Results for each file
     */
    std::vector<WatermarkResult> watermarkDirectory(
        const std::string& inputDir,
        const std::string& outputDir,
        bool recursive = false,
        int parallel = 1);

    // ==================== Utility ====================

    /**
     * Check if file is a supported video format
     */
    static bool isVideoFile(const std::string& path);

    /**
     * Check if file is a PDF
     */
    static bool isPdfFile(const std::string& path);

    /**
     * Generate output path from input path
     */
    std::string generateOutputPath(const std::string& inputPath,
                                   const std::string& outputDir = "") const;

    /**
     * Cancel ongoing batch operation
     */
    void cancel() { m_cancelled = true; }
    bool isCancelled() const { return m_cancelled; }

private:
    WatermarkConfig m_config;
    WatermarkProgressCallback m_progressCallback;
    bool m_cancelled = false;

    // Build FFmpeg filter string for video watermark
    std::string buildFFmpegFilter() const;

    // Build FFmpeg command line
    std::vector<std::string> buildFFmpegCommand(const std::string& input,
                                                 const std::string& output) const;

    // Execute FFmpeg command
    WatermarkResult executeFFmpeg(const std::string& input,
                                  const std::string& output);

    // Execute Python PDF script
    WatermarkResult executePdfScript(const std::string& input,
                                     const std::string& output);

    // Run external process and capture output
    int runProcess(const std::vector<std::string>& args,
                   std::string& stdout_output,
                   std::string& stderr_output);

    // Run ffmpeg with real-time progress parsing
    int runFFmpegWithProgress(const std::vector<std::string>& args,
                              const std::string& inputFile,
                              double durationSeconds,
                              std::string& output);

    // Get video duration using ffprobe
    double getVideoDuration(const std::string& inputPath);

    // Report progress
    void reportProgress(const std::string& file, int current, int total,
                        double percent, const std::string& status);

    // Get file size
    static int64_t getFileSize(const std::string& path);
};

} // namespace MegaCustom

#endif // WATERMARKER_H
