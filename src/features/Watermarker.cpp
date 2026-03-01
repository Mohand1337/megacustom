#include "features/Watermarker.h"
#include "core/LogManager.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <array>
#include <thread>
#include <future>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <io.h>
#include <process.h>
#define popen _popen
#define pclose _pclose
#define WEXITSTATUS(x) (x)
// S_IXUSR doesn't exist on Windows - define as 0 (Windows uses file extension for executability)
#ifndef S_IXUSR
#define S_IXUSR 0
#endif
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
// Cross-platform function to get user home directory
std::string getHomeDirectory() {
#ifdef _WIN32
    char* userProfile = std::getenv("USERPROFILE");
    if (userProfile) {
        return std::string(userProfile);
    }
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, path))) {
        return std::string(path);
    }
    return "C:\\Users\\Default";
#else
    const char* home = std::getenv("HOME");
    return home ? std::string(home) : "/tmp";
#endif
}

// Get the directory containing the executable (for portable deployment)
std::string getExecutableDir() {
#ifdef _WIN32
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        std::string exePath(path);
        size_t pos = exePath.find_last_of("\\/");
        if (pos != std::string::npos) {
            return exePath.substr(0, pos);
        }
    }
    return ".";
#else
    char path[1024];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len > 0) {
        path[len] = '\0';
        std::string exePath(path);
        size_t pos = exePath.find_last_of("/");
        if (pos != std::string::npos) {
            return exePath.substr(0, pos);
        }
    }
    return ".";
#endif
}

// Check if a file exists
bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}
} // anonymous namespace for getHomeDirectory

namespace MegaCustom {

namespace {
// Properly escape a string for shell command execution
// Uses single quotes which prevent ALL shell interpretation, escaping embedded single quotes
std::string shellEscape(const std::string& arg) {
#ifdef _WIN32
    // Always quote on Windows — paths from `where` or portable deployment
    // may contain spaces (e.g. "C:\Users\Admin\Desktop\My App\ffmpeg.exe").
    // Use "" (doubled double-quote) for escaping embedded quotes, which is
    // the correct escape sequence inside cmd.exe double-quoted strings.
    std::string result = "\"";
    for (char c : arg) {
        if (c == '"') result += "\"\"";  // cmd.exe doubled-quote escape
        else result += c;
    }
    result += "\"";
    return result;
#else
    // Unix: single-quote escaping
    std::string result = "'";
    for (char c : arg) {
        if (c == '\'') {
            result += "'\\''";
        } else {
            result += c;
        }
    }
    result += "'";
    return result;
#endif
}
} // anonymous namespace

// ==================== Constructor ====================

Watermarker::Watermarker() {
    // Set default font path to bundled fonts
    // Check multiple locations in order of priority:
    // 1. Portable deployment (adjacent to executable)
    // 2. Project development path

    std::string exeDir = getExecutableDir();
    std::string homeDir = getHomeDirectory();

    std::vector<std::string> fontPaths;

#ifdef _WIN32
    // Portable: fonts in bin/fonts adjacent to executable
    fontPaths.push_back(exeDir + "\\bin\\fonts\\arial.ttf");
    fontPaths.push_back(exeDir + "\\fonts\\arial.ttf");
    // Development path
    if (!homeDir.empty()) {
        fontPaths.push_back(homeDir + "\\projects\\Mega - SDK\\mega-custom-app\\bin\\fonts\\arial.ttf");
    }
#else
    // Portable: fonts in bin/fonts adjacent to executable
    fontPaths.push_back(exeDir + "/bin/fonts/arial.ttf");
    fontPaths.push_back(exeDir + "/fonts/arial.ttf");
    // Development path
    if (!homeDir.empty()) {
        fontPaths.push_back(homeDir + "/projects/Mega - SDK/mega-custom-app/bin/fonts/arial.ttf");
    }
#endif

    // Find the first existing font file
    for (const auto& path : fontPaths) {
        if (fileExists(path)) {
            m_config.fontPath = path;
            break;
        }
    }
}

// ==================== Static Utility Methods ====================

std::string Watermarker::getFFmpegPath() {
    std::vector<std::string> paths;
    std::string exeDir = getExecutableDir();
    std::string homeDir = getHomeDirectory();

#ifdef _WIN32
    // Windows: Check portable deployment first, then common locations
    paths = {
        exeDir + "\\ffmpeg.exe",  // Portable: adjacent to executable
        exeDir + "\\bin\\ffmpeg.exe",  // Portable: in bin subfolder
        "C:\\ffmpeg\\bin\\ffmpeg.exe",
        "C:\\Program Files\\ffmpeg\\bin\\ffmpeg.exe",
        "C:\\Program Files (x86)\\ffmpeg\\bin\\ffmpeg.exe"
    };

    // Add project bin path for Windows (development)
    if (!homeDir.empty()) {
        paths.push_back(homeDir + "\\projects\\Mega - SDK\\mega-custom-app\\bin\\ffmpeg.exe");
    }

    for (const auto& path : paths) {
        if (fileExists(path)) {
            return path;
        }
    }

    // Try PATH via where command
    FILE* pipe = popen("where ffmpeg 2>nul", "r");
    if (pipe) {
        char buffer[512];
        if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            pclose(pipe);
            std::string result(buffer);
            // Trim trailing newline/whitespace
            while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
                result.pop_back();
            return result;
        }
        pclose(pipe);
    }
#else
    // Unix: Check portable deployment first, then common locations
    paths = {
        exeDir + "/ffmpeg",  // Portable: adjacent to executable
        exeDir + "/bin/ffmpeg",  // Portable: in bin subfolder
        "/usr/bin/ffmpeg",
        "/usr/local/bin/ffmpeg",
        "/snap/bin/ffmpeg"
    };

    // Add project bin path (development)
    if (!homeDir.empty()) {
        paths.push_back(homeDir + "/projects/Mega - SDK/mega-custom-app/bin/ffmpeg");
    }

    for (const auto& path : paths) {
        struct stat st;
        if (stat(path.c_str(), &st) == 0 && (st.st_mode & S_IXUSR)) {
            return path;
        }
    }

    // Try PATH
    FILE* pipe = popen("which ffmpeg 2>/dev/null", "r");
    if (pipe) {
        char buffer[512];
        if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            pclose(pipe);
            std::string result(buffer);
            while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
                result.pop_back();
            return result;
        }
        pclose(pipe);
    }
#endif

    return "";
}

bool Watermarker::isFFmpegAvailable() {
    return !getFFmpegPath().empty();
}

std::string Watermarker::getPythonPath() {
    std::string exeDir = getExecutableDir();

#ifdef _WIN32
    // Check portable bundled Python first (deployed by CI)
    std::string portablePython = exeDir + "\\python\\python.exe";
    if (fileExists(portablePython)) {
        return portablePython;
    }

    // Try system Python (Windows uses 'python' not 'python3')
    FILE* pipe = popen("where python 2>nul", "r");
    if (pipe) {
        char buffer[512];
        if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            pclose(pipe);
            std::string result(buffer);
            while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
                result.pop_back();
            return result;
        }
        pclose(pipe);
    }
#else
    // Unix: prefer python3
    FILE* pipe = popen("which python3 2>/dev/null", "r");
    if (pipe) {
        char buffer[512];
        if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            pclose(pipe);
            std::string result(buffer);
            while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
                result.pop_back();
            return result;
        }
        pclose(pipe);
    }

    // Fallback to python
    pipe = popen("which python 2>/dev/null", "r");
    if (pipe) {
        char buffer[512];
        if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            pclose(pipe);
            std::string result(buffer);
            while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
                result.pop_back();
            return result;
        }
        pclose(pipe);
    }
#endif

    return "";
}

bool Watermarker::isPythonAvailable() {
    return !getPythonPath().empty();
}

std::string Watermarker::getPdfScriptPath() {
    // Check multiple locations in order of priority:
    // 1. Portable deployment (adjacent to executable)
    // 2. Current directory
    // 3. Project development path

    std::string exeDir = getExecutableDir();
    std::string homeDir = getHomeDirectory();

    std::vector<std::string> scriptPaths;

#ifdef _WIN32
    // Portable: scripts folder adjacent to executable
    scriptPaths.push_back(exeDir + "\\scripts\\pdf_watermark.py");
    // Current directory
    scriptPaths.push_back(".\\scripts\\pdf_watermark.py");
    // Development path
    if (!homeDir.empty()) {
        scriptPaths.push_back(homeDir + "\\projects\\Mega - SDK\\mega-custom-app\\scripts\\pdf_watermark.py");
    }
#else
    // Portable: scripts folder adjacent to executable
    scriptPaths.push_back(exeDir + "/scripts/pdf_watermark.py");
    // Current directory
    scriptPaths.push_back("./scripts/pdf_watermark.py");
    // Development path
    if (!homeDir.empty()) {
        scriptPaths.push_back(homeDir + "/projects/Mega - SDK/mega-custom-app/scripts/pdf_watermark.py");
    }
#endif

    // Return the first existing script
    for (const auto& path : scriptPaths) {
        if (fileExists(path)) {
            return path;
        }
    }

    // Return portable path as fallback (will show clear error if missing)
#ifdef _WIN32
    return exeDir + "\\scripts\\pdf_watermark.py";
#else
    return exeDir + "/scripts/pdf_watermark.py";
#endif
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

bool Watermarker::isAudioFile(const std::string& path) {
    std::vector<std::string> extensions = {
        ".mp3", ".flac", ".wav", ".aac", ".ogg", ".m4a", ".wma", ".opus"
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

    std::string fontFilePath = m_config.fontPath;
#ifdef _WIN32
    // On Windows, fontconfig is typically not configured, so we must specify
    // a font file explicitly. Use Arial as a safe default.
    if (fontFilePath.empty()) {
        const char* windir = std::getenv("WINDIR");
        std::string winFontsDir = windir ? std::string(windir) + "\\Fonts" : "C:\\Windows\\Fonts";
        std::string arialPath = winFontsDir + "\\arial.ttf";
        struct stat st;
        if (stat(arialPath.c_str(), &st) == 0) {
            fontFilePath = arialPath;
        }
    }
#endif
    // Use forward slashes for FFmpeg compatibility
    std::replace(fontFilePath.begin(), fontFilePath.end(), '\\', '/');

#ifdef _WIN32
    // Strip drive letter (e.g. "C:/Windows/..." -> "/Windows/...") because
    // FFmpeg's filter option parser treats ':' as a delimiter and no escaping
    // method (\: or single quotes) prevents it. The drive-relative path
    // "/Windows/Fonts/arial.ttf" resolves to the current drive's root.
    if (fontFilePath.size() >= 2 && fontFilePath[1] == ':') {
        fontFilePath = fontFilePath.substr(2);
    }
#endif

    std::string fontFile = fontFilePath.empty() ? "" :
        "fontfile=" + fontFilePath + ":";

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

    std::string ffmpegPath = getFFmpegPath();
    cmd.push_back(ffmpegPath.empty() ? "ffmpeg" : ffmpegPath);
    cmd.push_back("-y");  // Overwrite output
    cmd.push_back("-i");
    cmd.push_back(input);

#ifdef _WIN32
    // On Windows, write the filter to a temp file and use -filter_script:v
    // to avoid cmd.exe mangling colons/quotes in the filter string.
    {
        std::string filterText = buildFFmpegFilter();
        std::string tempDir;
        const char* tmp = std::getenv("TEMP");
        if (tmp) tempDir = tmp;
        else tempDir = ".";
        m_filterScriptPath = tempDir + "\\megacustom_vf.txt";
        std::ofstream f(m_filterScriptPath);
        f << filterText;
        f.close();
    }
    cmd.push_back("-filter_script:v");
    cmd.push_back(m_filterScriptPath);
#else
    cmd.push_back("-vf");
    cmd.push_back(buildFFmpegFilter());
#endif

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

    // Embed metadata if configured
    if (m_config.embedMetadata) {
        if (!m_config.metadataTitle.empty()) {
            cmd.push_back("-metadata");
            cmd.push_back("title=" + m_config.metadataTitle);
        }
        if (!m_config.metadataAuthor.empty()) {
            cmd.push_back("-metadata");
            cmd.push_back("artist=" + m_config.metadataAuthor);
        }
        if (!m_config.metadataComment.empty()) {
            cmd.push_back("-metadata");
            cmd.push_back("comment=" + m_config.metadataComment);
        }
        if (!m_config.metadataKeywords.empty()) {
            cmd.push_back("-metadata");
            cmd.push_back("description=" + m_config.metadataKeywords);
        }
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

    // On Windows, wrap the entire command in outer quotes so cmd.exe /c
    // passes inner quoted paths verbatim (cmd.exe Rule 2 workaround).
#ifdef _WIN32
    std::string cmd = "\"" + cmdStream.str() + "\"";
#else
    std::string cmd = cmdStream.str();
#endif

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

double Watermarker::getVideoDuration(const std::string& inputPath) {
    // Derive ffprobe path from ffmpeg path
    std::string ffmpegPath = getFFmpegPath();
    std::string ffprobePath = "ffprobe";
    if (!ffmpegPath.empty()) {
        // Replace "ffmpeg" with "ffprobe" in the resolved path
        size_t pos = ffmpegPath.rfind("ffmpeg");
        if (pos != std::string::npos) {
            ffprobePath = ffmpegPath.substr(0, pos) + "ffprobe" + ffmpegPath.substr(pos + 6);
        }
    }

    // Use ffprobe to get video duration
    std::string cmd = shellEscape(ffprobePath) + " -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 ";
    cmd += shellEscape(inputPath);
#ifdef _WIN32
    cmd += " 2>nul";
    // Outer-quote wrapping for cmd.exe Rule 2 workaround
    cmd = "\"" + cmd + "\"";
#else
    cmd += " 2>/dev/null";
#endif

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return 0.0;
    }

    char buffer[64];
    double duration = 0.0;
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        duration = std::strtod(buffer, nullptr);
    }
    pclose(pipe);

    return duration;
}

int Watermarker::runFFmpegWithProgress(const std::vector<std::string>& args,
                                        const std::string& inputFile,
                                        double durationSeconds,
                                        std::string& output) {
    // Build command string
    std::ostringstream cmdStream;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) cmdStream << " ";
        cmdStream << shellEscape(args[i]);
    }
    cmdStream << " 2>&1";

#ifdef _WIN32
    std::string cmd = "\"" + cmdStream.str() + "\"";
#else
    std::string cmd = cmdStream.str();
#endif

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        output = "Failed to execute command";
        return -1;
    }

    double lastPercent = 0.0;

    // Read character by character — FFmpeg outputs progress with \r (carriage
    // return) not \n, so fgets() would block until the process finishes.
    std::string line;
    int ch;
    while ((ch = fgetc(pipe)) != EOF) {
        if (ch == '\r' || ch == '\n') {
            if (!line.empty()) {
                output += line + "\n";

                // Parse ffmpeg time progress from output like: "time=00:01:23.45"
                size_t timePos = line.find("time=");
                if (timePos != std::string::npos && durationSeconds > 0) {
                    std::string timeStr = line.substr(timePos + 5);
                    int hours = 0, minutes = 0;
                    double seconds = 0.0;
                    if (sscanf(timeStr.c_str(), "%d:%d:%lf", &hours, &minutes, &seconds) >= 1) {
                        double currentTime = hours * 3600.0 + minutes * 60.0 + seconds;
                        double percent = (currentTime / durationSeconds) * 100.0;
                        percent = std::min(percent, 99.0);

                        if (percent - lastPercent >= 1.0) {
                            lastPercent = percent;
                            reportProgress(inputFile, 1, 1, percent, "encoding");
                        }
                    }
                }
                line.clear();
            }
        } else {
            line += static_cast<char>(ch);
        }
    }
    // Capture any trailing output
    if (!line.empty()) {
        output += line + "\n";
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

    // Get video duration for progress tracking
    double duration = getVideoDuration(input);
    reportProgress(input, 1, 1, 0.0, "encoding");

    // Build and execute command with progress tracking
    auto cmd = buildFFmpegCommand(input, output);

    std::string ffmpegOutput;
    int exitCode = runFFmpegWithProgress(cmd, input, duration, ffmpegOutput);

    auto endTime = std::chrono::steady_clock::now();
    result.processingTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime).count();

    if (exitCode == 0 && stat(output.c_str(), &st) == 0) {
        result.success = true;
        result.outputSizeBytes = st.st_size;
        reportProgress(input, 1, 1, 100.0, "complete");
        LogManager::instance().logWatermark("video_complete",
            "Video watermark complete: " + output + " (" + std::to_string(result.processingTimeMs) + "ms)", input);
    } else {
        result.error = "FFmpeg failed (exit code " + std::to_string(exitCode) + "): " + ffmpegOutput;
        reportProgress(input, 1, 1, 0.0, "error");
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

    // Log resolved script path for debugging
    fprintf(stderr, "PDF watermark script: %s\n", scriptPath.c_str());

    // Build command using resolved Python path
    std::string pythonPath = getPythonPath();
    std::vector<std::string> cmd = {
        pythonPath.empty() ? "python" : pythonPath, scriptPath,
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

    // Embed metadata if configured
    if (m_config.embedMetadata) {
        if (!m_config.metadataTitle.empty()) {
            cmd.push_back("--metadata-title");
            cmd.push_back(m_config.metadataTitle);
        }
        if (!m_config.metadataAuthor.empty()) {
            cmd.push_back("--metadata-author");
            cmd.push_back(m_config.metadataAuthor);
        }
        if (!m_config.metadataComment.empty()) {
            cmd.push_back("--metadata-subject");
            cmd.push_back(m_config.metadataComment);
        }
        if (!m_config.metadataKeywords.empty()) {
            cmd.push_back("--metadata-keywords");
            cmd.push_back(m_config.metadataKeywords);
        }
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

std::string Watermarker::buildMemberOutputPath(const std::string& inputPath,
                                                const std::string& outputDir,
                                                const std::string& memberId,
                                                const std::string& rootDir) const {
    // Per-member watermarking outputs to a member subdirectory preserving
    // the original folder structure. Structure:
    //   outputDir/memberId/relative/path/original_filename.ext
    //
    // When rootDir is provided (from "Add Folder"), the relative path between
    // rootDir and inputPath's parent is preserved under the member directory.

    namespace fs = std::filesystem;

    fs::path inputFs(inputPath);
    fs::path filename = inputFs.filename();

    // Determine base output directory
    fs::path basePath;
    if (!outputDir.empty()) {
        basePath = fs::path(outputDir);
    } else if (!rootDir.empty()) {
        basePath = fs::path(rootDir);
    } else {
        basePath = inputFs.parent_path();
    }

    // Compute relative subdirectory structure from rootDir
    fs::path relativeSub;
    if (!rootDir.empty()) {
        fs::path inputParent = inputFs.parent_path();
        fs::path rootFs(rootDir);
        if (inputParent != rootFs) {
            // Use lexically_relative for safe relative path computation
            relativeSub = inputParent.lexically_relative(rootFs);
            // If relative path starts with "..", it's outside rootDir — ignore
            if (!relativeSub.empty() && relativeSub.string().substr(0, 2) == "..") {
                relativeSub.clear();
            }
        }
    }

    // Build: basePath / memberId / relativeSub / filename
    fs::path memberDir = basePath / memberId / relativeSub;
    fs::create_directories(memberDir);

    // Check for existing file and add numbered suffix if needed
    std::string candidate = (memberDir / filename).string();
    struct stat stCheck;
    if (stat(candidate.c_str(), &stCheck) == 0) {
        std::string fnStr = filename.string();
        size_t lastDot = fnStr.find_last_of('.');
        std::string baseName = (lastDot == std::string::npos) ?
            fnStr : fnStr.substr(0, lastDot);
        std::string ext = (lastDot == std::string::npos) ?
            "" : fnStr.substr(lastDot);
        for (int n = 1; n < 1000; ++n) {
            candidate = (memberDir / (baseName + " (" + std::to_string(n) + ")" + ext)).string();
            if (stat(candidate.c_str(), &stCheck) != 0) {
                break;
            }
        }
    }
    return candidate;
}

std::string Watermarker::generateOutputPath(const std::string& inputPath,
                                             const std::string& outputDir) const {
    namespace fs = std::filesystem;

    fs::path inputFs(inputPath);
    std::string filename = inputFs.filename().string();

    // Find extension
    size_t lastDot = filename.find_last_of('.');
    std::string baseName = (lastDot == std::string::npos) ?
        filename : filename.substr(0, lastDot);
    std::string ext = (lastDot == std::string::npos) ?
        "" : filename.substr(lastDot);

    // Build output path
    fs::path sourceDir = inputFs.parent_path();
    fs::path outDir = outputDir.empty() ? sourceDir : fs::path(outputDir);

    // Only add suffix when output goes to the same directory as the source
    // (to avoid overwriting the original). When a different output directory
    // is specified, the original is safe and the suffix is unnecessary.
    std::string suffix;
    if (outputDir.empty() || outDir == sourceDir) {
        suffix = m_config.outputSuffix;
    }

    // Check if output file already exists; if so, append (1), (2), etc.
    std::string candidate = (outDir / (baseName + suffix + ext)).string();
    struct stat stCheck;
    if (stat(candidate.c_str(), &stCheck) == 0) {
        for (int n = 1; n < 1000; ++n) {
            candidate = (outDir / (baseName + suffix + " (" + std::to_string(n) + ")" + ext)).string();
            if (stat(candidate.c_str(), &stCheck) != 0) {
                break;
            }
        }
    }
    return candidate;
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

// Async video watermarking - runs FFmpeg in background thread
std::future<WatermarkResult> Watermarker::watermarkVideoAsync(
    const std::string& inputPath,
    const std::string& outputPath) {

    // Capture config by value for thread safety
    WatermarkConfig configCopy = m_config;

    return std::async(std::launch::async, [this, inputPath, outputPath, configCopy]() {
        WatermarkConfig saved = m_config;
        m_config = configCopy;
        auto result = this->watermarkVideo(inputPath, outputPath);
        m_config = saved;
        return result;
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

WatermarkResult Watermarker::watermarkAudio(const std::string& inputPath,
                                              const std::string& outputPath) {
    WatermarkResult result;
    result.inputFile = inputPath;

    if (!isFFmpegAvailable()) {
        result.error = "FFmpeg not found. Required for audio metadata embedding.";
        return result;
    }

    auto startTime = std::chrono::steady_clock::now();

    std::string outPath = outputPath.empty() ? generateOutputPath(inputPath) : outputPath;
    result.outputFile = outPath;
    result.inputSizeBytes = getFileSize(inputPath);

    LogManager::instance().logWatermark("audio_start", "Starting audio metadata: " + inputPath, inputPath);

    // Build FFmpeg command for metadata-only embedding (no re-encoding)
    std::string ffmpegPath = getFFmpegPath();
    std::vector<std::string> cmd = {
        ffmpegPath.empty() ? "ffmpeg" : ffmpegPath,
        "-y", "-i", inputPath
    };

    // Add metadata flags
    if (m_config.embedMetadata) {
        if (!m_config.metadataTitle.empty()) {
            cmd.push_back("-metadata");
            cmd.push_back("title=" + m_config.metadataTitle);
        }
        if (!m_config.metadataAuthor.empty()) {
            cmd.push_back("-metadata");
            cmd.push_back("artist=" + m_config.metadataAuthor);
        }
        if (!m_config.metadataComment.empty()) {
            cmd.push_back("-metadata");
            cmd.push_back("comment=" + m_config.metadataComment);
        }
        if (!m_config.metadataKeywords.empty()) {
            cmd.push_back("-metadata");
            cmd.push_back("description=" + m_config.metadataKeywords);
        }
    }

    // If no metadata, just copy the file
    if (!m_config.embedMetadata &&
        m_config.metadataTitle.empty() && m_config.metadataAuthor.empty() &&
        m_config.metadataComment.empty() && m_config.metadataKeywords.empty()) {
        // Still use FFmpeg to copy — ensures consistent output format
    }

    // Copy all streams without re-encoding
    cmd.push_back("-codec");
    cmd.push_back("copy");
    cmd.push_back(outPath);

    std::string ffmpegOutput, ffmpegErr;
    int exitCode = runProcess(cmd, ffmpegOutput, ffmpegErr);

    auto endTime = std::chrono::steady_clock::now();
    result.processingTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime).count();

    if (exitCode == 0) {
        result.success = true;
        result.outputSizeBytes = getFileSize(outPath);
        LogManager::instance().logWatermark("audio_complete",
            "Audio metadata complete: " + outPath + " (" + std::to_string(result.processingTimeMs) + "ms)", inputPath);
    } else {
        result.error = "FFmpeg failed (exit code " + std::to_string(exitCode) + "): " + ffmpegOutput;
        LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
            "audio_metadata_failed", "Audio metadata failed: " + inputPath + " - " + result.error, inputPath);
    }

    return result;
}

WatermarkResult Watermarker::watermarkFile(const std::string& inputPath,
                                            const std::string& outputPath) {
    if (isVideoFile(inputPath)) {
        return watermarkVideo(inputPath, outputPath);
    } else if (isPdfFile(inputPath)) {
        return watermarkPdf(inputPath, outputPath);
    } else if (isAudioFile(inputPath)) {
        return watermarkAudio(inputPath, outputPath);
    } else {
        WatermarkResult result;
        result.inputFile = inputPath;
        result.error = "Unsupported file type. Supported: video, PDF, and audio (mp3, flac, etc.)";
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
    std::vector<std::string> audioFiles;

    // Scan directory for supported files using std::filesystem (cross-platform)
    namespace fs = std::filesystem;
    try {
        if (recursive) {
            for (const auto& entry : fs::recursive_directory_iterator(inputDir)) {
                if (entry.is_regular_file()) {
                    std::string p = entry.path().string();
                    if (isVideoFile(p)) videoFiles.push_back(p);
                    else if (isPdfFile(p)) pdfFiles.push_back(p);
                    else if (isAudioFile(p)) audioFiles.push_back(p);
                }
            }
        } else {
            for (const auto& entry : fs::directory_iterator(inputDir)) {
                if (entry.is_regular_file()) {
                    std::string p = entry.path().string();
                    if (isVideoFile(p)) videoFiles.push_back(p);
                    else if (isPdfFile(p)) pdfFiles.push_back(p);
                    else if (isAudioFile(p)) audioFiles.push_back(p);
                }
            }
        }
    } catch (const std::exception& e) {
        LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
            "directory_scan_failed", "Failed to scan directory: " + inputDir + " - " + e.what());
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

    // Process audio files (metadata embedding)
    if (!audioFiles.empty() && !m_cancelled) {
        for (size_t i = 0; i < audioFiles.size() && !m_cancelled; ++i) {
            reportProgress(audioFiles[i], i + 1, audioFiles.size(),
                          (double)i / audioFiles.size() * 100.0, "processing");
            std::string outPath = generateOutputPath(audioFiles[i], outputDir);
            results.push_back(watermarkAudio(audioFiles[i], outPath));
        }
    }

    return results;
}

} // namespace MegaCustom
