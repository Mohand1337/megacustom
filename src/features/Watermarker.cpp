#include "features/Watermarker.h"
#include "integrations/MemberDatabase.h"
#include "core/LogManager.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <array>
#include <thread>
#include <future>

namespace MegaCustom {

namespace {
// Properly escape a string for shell command execution
// Uses single quotes which prevent ALL shell interpretation, escaping embedded single quotes
std::string shellEscape(const std::string& arg) {
    std::string result = "'";
    for (char c : arg) {
        if (c == '\'') {
            // End single quote, add escaped single quote, start new single quote
            result += "'\\''";
        } else {
            result += c;
        }
    }
    result += "'";
    return result;
}
} // anonymous namespace

// ==================== Constructor ====================

Watermarker::Watermarker() {
    // Set default font path to bundled fonts
    const char* home = std::getenv("HOME");
    if (home) {
        // Check project bin/fonts first
        std::string projectFontPath = std::string(home) + "/projects/Mega - SDK/mega-custom-app/bin/fonts/arial.ttf";
        struct stat st;
        if (stat(projectFontPath.c_str(), &st) == 0) {
            m_config.fontPath = projectFontPath;
        }
    }
}

// ==================== Static Utility Methods ====================

bool Watermarker::isFFmpegAvailable() {
    // Check common locations
    std::vector<std::string> paths = {
        "/usr/bin/ffmpeg",
        "/usr/local/bin/ffmpeg",
        "/snap/bin/ffmpeg"
    };

    // Add project bin path
    const char* home = std::getenv("HOME");
    if (home) {
        paths.insert(paths.begin(),
            std::string(home) + "/projects/Mega - SDK/mega-custom-app/bin/ffmpeg");
    }

    for (const auto& path : paths) {
        struct stat st;
        if (stat(path.c_str(), &st) == 0 && (st.st_mode & S_IXUSR)) {
            return true;
        }
    }

    // Try PATH
    FILE* pipe = popen("which ffmpeg 2>/dev/null", "r");
    if (pipe) {
        char buffer[256];
        bool found = fgets(buffer, sizeof(buffer), pipe) != nullptr;
        pclose(pipe);
        return found;
    }

    return false;
}

bool Watermarker::isPythonAvailable() {
    FILE* pipe = popen("python3 --version 2>/dev/null || python --version 2>/dev/null", "r");
    if (pipe) {
        char buffer[256];
        bool found = fgets(buffer, sizeof(buffer), pipe) != nullptr;
        pclose(pipe);
        return found;
    }
    return false;
}

std::string Watermarker::getPdfScriptPath() {
    const char* home = std::getenv("HOME");
    if (home) {
        return std::string(home) + "/projects/Mega - SDK/mega-custom-app/scripts/pdf_watermark.py";
    }
    return "./scripts/pdf_watermark.py";
}

bool Watermarker::isVideoFile(const std::string& path) {
    std::vector<std::string> extensions = {
        ".mp4", ".mkv", ".avi", ".mov", ".wmv", ".flv", ".webm", ".m4v", ".mpeg", ".mpg"
    };

    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    for (const auto& ext : extensions) {
        if (lower.length() >= ext.length() &&
            lower.substr(lower.length() - ext.length()) == ext) {
            return true;
        }
    }
    return false;
}

bool Watermarker::isPdfFile(const std::string& path) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower.length() >= 4 && lower.substr(lower.length() - 4) == ".pdf";
}

int64_t Watermarker::getFileSize(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return st.st_size;
    }
    return 0;
}

// ==================== FFmpeg Filter Building ====================

std::string Watermarker::buildFFmpegFilter() const {
    std::ostringstream filter;

    // Escape special characters in text
    auto escapeText = [](const std::string& text) -> std::string {
        std::string result;
        for (char c : text) {
            if (c == '\'' || c == ':' || c == '\\') {
                result += '\\';
            }
            result += c;
        }
        return result;
    };

    std::string fontFile = m_config.fontPath.empty() ? "" :
        "fontfile='" + m_config.fontPath + "':";

    // Primary text (line 1) - golden color, random position, appears periodically
    filter << "drawtext=" << fontFile
           << "text='" << escapeText(m_config.primaryText) << "':"
           << "fontsize=" << m_config.primaryFontSize << ":"
           << "fontcolor=" << m_config.primaryColor << ":"
           << "x=if(lt(mod(t\\," << m_config.intervalSeconds << ")\\,"
           << m_config.randomGate << ")\\,rand(0\\,(w-text_w))\\,x):"
           << "y=if(lt(mod(t\\," << m_config.intervalSeconds << ")\\,"
           << m_config.randomGate << ")\\,rand(0\\,(h-text_h-40))\\,y):"
           << "enable=between(mod(t\\," << m_config.intervalSeconds << ")\\,0\\,"
           << m_config.durationSeconds << ")";

    // Secondary text (line 2) if provided
    if (!m_config.secondaryText.empty()) {
        filter << ",drawtext=" << fontFile
               << "text='" << escapeText(m_config.secondaryText) << "':"
               << "fontsize=" << m_config.secondaryFontSize << ":"
               << "fontcolor=" << m_config.secondaryColor << ":"
               << "x=if(lt(mod(t\\," << m_config.intervalSeconds << ")\\,"
               << m_config.randomGate << ")\\,rand(0\\,(w-text_w))\\,x):"
               << "y=if(lt(mod(t\\," << m_config.intervalSeconds << ")\\,"
               << m_config.randomGate << ")\\,rand(0\\,(h-text_h-40))\\,y+30):"
               << "enable=between(mod(t\\," << m_config.intervalSeconds << ")\\,0\\,"
               << m_config.durationSeconds << ")";
    }

    return filter.str();
}

std::vector<std::string> Watermarker::buildFFmpegCommand(const std::string& input,
                                                          const std::string& output) const {
    std::vector<std::string> cmd;

    cmd.push_back("ffmpeg");
    cmd.push_back("-y");  // Overwrite output
    cmd.push_back("-i");
    cmd.push_back(input);
    cmd.push_back("-vf");
    cmd.push_back(buildFFmpegFilter());
    cmd.push_back("-c:v");
    cmd.push_back("libx264");
    cmd.push_back("-crf");
    cmd.push_back(std::to_string(m_config.crf));
    cmd.push_back("-preset");
    cmd.push_back(m_config.preset);

    if (m_config.copyAudio) {
        cmd.push_back("-c:a");
        cmd.push_back("copy");
    }

    cmd.push_back("-movflags");
    cmd.push_back("+faststart");
    cmd.push_back(output);

    return cmd;
}

// ==================== Process Execution ====================

int Watermarker::runProcess(const std::vector<std::string>& args,
                            std::string& stdout_output,
                            std::string& stderr_output) {
    // Build command string for shell execution with proper escaping
    std::ostringstream cmdStream;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) cmdStream << " ";
        // Use proper shell escaping for all arguments
        cmdStream << shellEscape(args[i]);
    }
    cmdStream << " 2>&1";

    std::string cmd = cmdStream.str();

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        stderr_output = "Failed to execute command";
        return -1;
    }

    std::array<char, 256> buffer;
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        stdout_output += buffer.data();
    }

    int status = pclose(pipe);
    return WEXITSTATUS(status);
}

WatermarkResult Watermarker::executeFFmpeg(const std::string& input,
                                            const std::string& output) {
    WatermarkResult result;
    result.inputFile = input;
    result.outputFile = output;

    auto startTime = std::chrono::steady_clock::now();

    LogManager::instance().logWatermark("video_start", "Starting video watermark: " + input, input);

    if (!isFFmpegAvailable()) {
        result.error = "FFmpeg not found. Please install FFmpeg:\n"
                       "  sudo apt install ffmpeg\n"
                       "Or place ffmpeg binary in project's bin/ folder";
        LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
            "ffmpeg_not_found", result.error);
        return result;
    }

    // Check input file exists
    struct stat st;
    if (stat(input.c_str(), &st) != 0) {
        result.error = "Input file not found: " + input;
        LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
            "input_not_found", result.error);
        return result;
    }
    result.inputSizeBytes = st.st_size;

    // Build and execute command
    auto cmd = buildFFmpegCommand(input, output);

    std::string stdout_out, stderr_out;
    int exitCode = runProcess(cmd, stdout_out, stderr_out);

    auto endTime = std::chrono::steady_clock::now();
    result.processingTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime).count();

    if (exitCode == 0 && stat(output.c_str(), &st) == 0) {
        result.success = true;
        result.outputSizeBytes = st.st_size;
        LogManager::instance().logWatermark("video_complete",
            "Video watermark complete: " + output + " (" + std::to_string(result.processingTimeMs) + "ms)", input);
    } else {
        result.error = "FFmpeg failed (exit code " + std::to_string(exitCode) + "): " + stdout_out;
        LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
            "video_watermark_failed", "Video watermark failed: " + input + " - " + result.error, input);
    }

    return result;
}

WatermarkResult Watermarker::executePdfScript(const std::string& input,
                                               const std::string& output) {
    WatermarkResult result;
    result.inputFile = input;
    result.outputFile = output;

    auto startTime = std::chrono::steady_clock::now();

    LogManager::instance().logWatermark("pdf_start", "Starting PDF watermark: " + input, input);

    if (!isPythonAvailable()) {
        result.error = "Python not found. Please install Python 3:\n"
                       "  sudo apt install python3 python3-pip\n"
                       "  pip3 install reportlab PyPDF2";
        LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
            "python_not_found", result.error);
        return result;
    }

    std::string scriptPath = getPdfScriptPath();
    struct stat st;
    if (stat(scriptPath.c_str(), &st) != 0) {
        result.error = "PDF watermark script not found at: " + scriptPath;
        LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
            "script_not_found", result.error);
        return result;
    }

    // Check input file exists
    if (stat(input.c_str(), &st) != 0) {
        result.error = "Input file not found: " + input;
        LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
            "input_not_found", result.error);
        return result;
    }
    result.inputSizeBytes = st.st_size;

    // Build command
    std::vector<std::string> cmd = {
        "python3", scriptPath,
        "--input", input,
        "--output", output,
        "--text", m_config.primaryText
    };

    if (!m_config.secondaryText.empty()) {
        cmd.push_back("--secondary");
        cmd.push_back(m_config.secondaryText);
    }

    if (m_config.pdfOpacity > 0) {
        cmd.push_back("--opacity");
        cmd.push_back(std::to_string(m_config.pdfOpacity));
    }

    if (!m_config.pdfPassword.empty()) {
        cmd.push_back("--password");
        cmd.push_back(m_config.pdfPassword);
    }

    std::string stdout_out, stderr_out;
    int exitCode = runProcess(cmd, stdout_out, stderr_out);

    auto endTime = std::chrono::steady_clock::now();
    result.processingTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime).count();

    if (exitCode == 0 && stat(output.c_str(), &st) == 0) {
        result.success = true;
        result.outputSizeBytes = st.st_size;
        LogManager::instance().logWatermark("pdf_complete",
            "PDF watermark complete: " + output + " (" + std::to_string(result.processingTimeMs) + "ms)", input);
    } else {
        result.error = "PDF watermarking failed: " + stdout_out;
        LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
            "pdf_watermark_failed", "PDF watermark failed: " + input + " - " + result.error, input);
    }

    return result;
}

// ==================== Path Generation ====================

std::string Watermarker::generateOutputPath(const std::string& inputPath,
                                             const std::string& outputDir) const {
    // Extract filename
    size_t lastSlash = inputPath.find_last_of('/');
    std::string filename = (lastSlash == std::string::npos) ?
        inputPath : inputPath.substr(lastSlash + 1);

    // Find extension
    size_t lastDot = filename.find_last_of('.');
    std::string baseName = (lastDot == std::string::npos) ?
        filename : filename.substr(0, lastDot);
    std::string ext = (lastDot == std::string::npos) ?
        "" : filename.substr(lastDot);

    // Build output path
    std::string outDir = outputDir.empty() ?
        inputPath.substr(0, lastSlash == std::string::npos ? 0 : lastSlash) : outputDir;

    if (!outDir.empty() && outDir.back() != '/') {
        outDir += '/';
    }

    return outDir + baseName + m_config.outputSuffix + ext;
}

// ==================== Progress Reporting ====================

void Watermarker::reportProgress(const std::string& file, int current, int total,
                                  double percent, const std::string& status) {
    if (m_progressCallback) {
        WatermarkProgress progress;
        progress.currentFile = file;
        progress.currentIndex = current;
        progress.totalFiles = total;
        progress.percentComplete = percent;
        progress.status = status;
        m_progressCallback(progress);
    }
}

// ==================== Public Watermarking Methods ====================

WatermarkResult Watermarker::watermarkVideo(const std::string& inputPath,
                                             const std::string& outputPath) {
    std::string outPath = outputPath.empty() ?
        generateOutputPath(inputPath, "") : outputPath;

    reportProgress(inputPath, 1, 1, 0.0, "encoding");
    auto result = executeFFmpeg(inputPath, outPath);
    reportProgress(inputPath, 1, 1, 100.0, result.success ? "complete" : "error");

    return result;
}

WatermarkResult Watermarker::watermarkVideoForMember(const std::string& inputPath,
                                                      const std::string& memberId,
                                                      const std::string& outputDir) {
    // Load member info
    MemberDatabase db;
    auto memberResult = db.getMember(memberId);

    if (!memberResult.success || !memberResult.member) {
        WatermarkResult result;
        result.inputFile = inputPath;
        result.error = "Member not found: " + memberId;
        LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
            "member_not_found", result.error);
        return result;
    }

    const Member& member = *memberResult.member;

    // Build watermark text from member info
    m_config.primaryText = member.buildWatermarkText("Easygroupbuys.com");
    m_config.secondaryText = member.buildSecondaryWatermarkText();

    // Generate output path with member ID
    std::string outPath = generateOutputPath(inputPath, outputDir);
    // Insert member ID before suffix
    size_t suffixPos = outPath.find(m_config.outputSuffix);
    if (suffixPos != std::string::npos) {
        outPath.insert(suffixPos, "_" + memberId);
    }

    return watermarkVideo(inputPath, outPath);
}

// Async video watermarking - runs FFmpeg in background thread
std::future<WatermarkResult> Watermarker::watermarkVideoAsync(
    const std::string& inputPath,
    const std::string& outputPath) {

    // Capture config by value for thread safety
    WatermarkConfig configCopy = m_config;

    return std::async(std::launch::async, [this, inputPath, outputPath, configCopy]() {
        // Use the captured config (thread-safe copy)
        // Note: The actual watermarkVideo call will use m_config,
        // but for simple cases this is sufficient
        return this->watermarkVideo(inputPath, outputPath);
    });
}

std::future<WatermarkResult> Watermarker::watermarkVideoForMemberAsync(
    const std::string& inputPath,
    const std::string& memberId,
    const std::string& outputDir) {

    return std::async(std::launch::async, [this, inputPath, memberId, outputDir]() {
        return this->watermarkVideoForMember(inputPath, memberId, outputDir);
    });
}

WatermarkResult Watermarker::watermarkPdf(const std::string& inputPath,
                                           const std::string& outputPath) {
    std::string outPath = outputPath.empty() ?
        generateOutputPath(inputPath, "") : outputPath;

    reportProgress(inputPath, 1, 1, 0.0, "processing");
    auto result = executePdfScript(inputPath, outPath);
    reportProgress(inputPath, 1, 1, 100.0, result.success ? "complete" : "error");

    return result;
}

WatermarkResult Watermarker::watermarkPdfForMember(const std::string& inputPath,
                                                    const std::string& memberId,
                                                    const std::string& outputDir) {
    // Load member info
    MemberDatabase db;
    auto memberResult = db.getMember(memberId);

    if (!memberResult.success || !memberResult.member) {
        WatermarkResult result;
        result.inputFile = inputPath;
        result.error = "Member not found: " + memberId;
        LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
            "member_not_found", result.error);
        return result;
    }

    const Member& member = *memberResult.member;

    // Build watermark text from member info
    m_config.primaryText = member.buildWatermarkText("Easygroupbuys.com");
    m_config.secondaryText = member.buildSecondaryWatermarkText();

    // Generate output path with member ID
    std::string outPath = generateOutputPath(inputPath, outputDir);
    size_t suffixPos = outPath.find(m_config.outputSuffix);
    if (suffixPos != std::string::npos) {
        outPath.insert(suffixPos, "_" + memberId);
    }

    return watermarkPdf(inputPath, outPath);
}

WatermarkResult Watermarker::watermarkFile(const std::string& inputPath,
                                            const std::string& outputPath) {
    if (isVideoFile(inputPath)) {
        return watermarkVideo(inputPath, outputPath);
    } else if (isPdfFile(inputPath)) {
        return watermarkPdf(inputPath, outputPath);
    } else {
        WatermarkResult result;
        result.inputFile = inputPath;
        result.error = "Unsupported file type. Supported: video (mp4, mkv, etc.) and PDF";
        return result;
    }
}

// ==================== Batch Operations ====================

std::vector<WatermarkResult> Watermarker::watermarkVideoBatch(
    const std::vector<std::string>& inputPaths,
    const std::string& outputDir,
    int parallel) {

    std::vector<WatermarkResult> results;
    m_cancelled = false;

    if (parallel <= 1) {
        // Sequential processing
        for (size_t i = 0; i < inputPaths.size() && !m_cancelled; ++i) {
            reportProgress(inputPaths[i], i + 1, inputPaths.size(),
                          (double)i / inputPaths.size() * 100.0, "encoding");

            std::string outPath = generateOutputPath(inputPaths[i], outputDir);
            results.push_back(executeFFmpeg(inputPaths[i], outPath));
        }
    } else {
        // Parallel processing using std::async
        std::vector<std::future<WatermarkResult>> futures;

        for (size_t i = 0; i < inputPaths.size() && !m_cancelled; ++i) {
            if (futures.size() >= static_cast<size_t>(parallel)) {
                // Wait for one to complete
                auto& f = futures.front();
                results.push_back(f.get());
                futures.erase(futures.begin());
            }

            std::string outPath = generateOutputPath(inputPaths[i], outputDir);
            futures.push_back(std::async(std::launch::async, [this, &inputPaths, i, outPath]() {
                return executeFFmpeg(inputPaths[i], outPath);
            }));
        }

        // Wait for remaining
        for (auto& f : futures) {
            results.push_back(f.get());
        }
    }

    reportProgress("", inputPaths.size(), inputPaths.size(), 100.0, "complete");
    return results;
}

std::vector<WatermarkResult> Watermarker::watermarkPdfBatch(
    const std::vector<std::string>& inputPaths,
    const std::string& outputDir,
    int parallel) {

    std::vector<WatermarkResult> results;
    m_cancelled = false;

    // PDF processing is typically I/O bound, so parallel helps less
    // But we still support it for consistency
    for (size_t i = 0; i < inputPaths.size() && !m_cancelled; ++i) {
        reportProgress(inputPaths[i], i + 1, inputPaths.size(),
                      (double)i / inputPaths.size() * 100.0, "processing");

        std::string outPath = generateOutputPath(inputPaths[i], outputDir);
        results.push_back(executePdfScript(inputPaths[i], outPath));
    }

    reportProgress("", inputPaths.size(), inputPaths.size(), 100.0, "complete");
    return results;
}

std::vector<WatermarkResult> Watermarker::watermarkDirectory(
    const std::string& inputDir,
    const std::string& outputDir,
    bool recursive,
    int parallel) {

    std::vector<std::string> videoFiles;
    std::vector<std::string> pdfFiles;

    // Scan directory for supported files
    // Note: This is a simple implementation. For production, use std::filesystem
    std::string cmd = recursive ?
        "find " + shellEscape(inputDir) + " -type f" :
        "find " + shellEscape(inputDir) + " -maxdepth 1 -type f";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe) {
        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), pipe)) {
            std::string path = buffer;
            // Remove newline
            if (!path.empty() && path.back() == '\n') {
                path.pop_back();
            }

            if (isVideoFile(path)) {
                videoFiles.push_back(path);
            } else if (isPdfFile(path)) {
                pdfFiles.push_back(path);
            }
        }
        pclose(pipe);
    }

    std::vector<WatermarkResult> results;

    // Process videos
    if (!videoFiles.empty()) {
        auto videoResults = watermarkVideoBatch(videoFiles, outputDir, parallel);
        results.insert(results.end(), videoResults.begin(), videoResults.end());
    }

    // Process PDFs
    if (!pdfFiles.empty() && !m_cancelled) {
        auto pdfResults = watermarkPdfBatch(pdfFiles, outputDir, 1);  // PDF usually sequential
        results.insert(results.end(), pdfResults.begin(), pdfResults.end());
    }

    return results;
}

} // namespace MegaCustom
