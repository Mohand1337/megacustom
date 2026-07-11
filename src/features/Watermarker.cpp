#include "features/Watermarker.h"
#include "core/LogManager.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <array>
#include <thread>
#include <future>
#include <filesystem>
#include <stdexcept>
#include <functional>
#include <vector>

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

// Construct a std::filesystem::path from a UTF-8 std::string.
//
// Why: Qt's QString::toStdString() returns UTF-8, but on Windows the
// default fs::path(std::string) constructor decodes the bytes using the
// active code page (CP1252 etc.), which mangles non-ASCII characters such
// as the curly apostrophe U+2019 ("'"). Files like
// "...What's New in Meta Ads..." would then fail fs::exists() even though
// the file is on disk. Routing through fs::u8path treats the bytes as
// UTF-8, matching how Qt produced them.
std::filesystem::path toFsPath(const std::string& utf8) {
#ifdef _WIN32
    return std::filesystem::u8path(utf8);
#else
    return std::filesystem::path(utf8);
#endif
}

// Convert a std::filesystem::path back to a UTF-8 std::string.
// On Windows we need u8string() so non-ASCII chars survive the trip back;
// path::string() would re-encode in the active code page. The std::u8string
// branch covers a future bump to C++20 where u8string() returns std::u8string
// instead of std::string.
std::string fromFsPath(const std::filesystem::path& p) {
#ifdef _WIN32
    auto s = p.u8string();
    #if defined(__cpp_char8_t)
        return std::string(reinterpret_cast<const char*>(s.data()), s.size());
    #else
        return s;
    #endif
#else
    return p.string();
#endif
}

// Open a process pipe for a command whose bytes are UTF-8.
//
// _popen on Windows decodes the command line via the active code page
// (CP1252 on most installs), which mangles UTF-8 characters such as the
// curly apostrophe U+2019 in file paths into mojibake (Whatâ€™s) before
// cmd.exe ever sees them. ffmpeg and the PDF Python script then try to
// open a path that no longer matches what is on disk and fail with
// "No such file or directory" / "Input file not found".
//
// Routing through _wpopen lets us hand cmd.exe a UTF-16 command line
// directly, so non-ASCII paths survive intact. _pclose accepts a stream
// from either flavor, so the rest of the read loop is unchanged.
FILE* utf8Popen(const std::string& cmd, const char* mode) {
#ifdef _WIN32
    auto toWide = [](const char* src) -> std::wstring {
        if (!src || !*src) return std::wstring();
        int wlen = MultiByteToWideChar(CP_UTF8, 0, src, -1, nullptr, 0);
        if (wlen <= 1) return std::wstring();
        std::wstring out(static_cast<size_t>(wlen - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, src, -1, out.data(), wlen);
        return out;
    };
    std::wstring wcmd = toWide(cmd.c_str());
    std::wstring wmode = toWide(mode);
    return _wpopen(wcmd.c_str(), wmode.c_str());
#else
    return popen(cmd.c_str(), mode);
#endif
}

#ifdef _WIN32
std::wstring utf8ToWideString(const std::string& input) {
    if (input.empty()) {
        return std::wstring();
    }

    int wlen = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), nullptr, 0);
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (wlen <= 0) {
        flags = 0;
        wlen = MultiByteToWideChar(
            CP_UTF8, flags, input.data(), static_cast<int>(input.size()), nullptr, 0);
    }
    if (wlen <= 0) {
        throw std::runtime_error("Failed to convert process argument from UTF-8 to UTF-16");
    }

    std::wstring output(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(
        CP_UTF8, flags, input.data(), static_cast<int>(input.size()), output.data(), wlen);
    return output;
}

std::string wideToUtf8String(const std::wstring& input) {
    if (input.empty()) {
        return std::string();
    }

    int len = WideCharToMultiByte(
        CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return std::string();
    }

    std::string output(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, input.data(), static_cast<int>(input.size()), output.data(), len, nullptr, nullptr);
    return output;
}

std::string windowsErrorMessage(DWORD errorCode) {
    LPWSTR rawMessage = nullptr;
    DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&rawMessage),
        0,
        nullptr);

    std::wstring message;
    if (length > 0 && rawMessage) {
        message.assign(rawMessage, rawMessage + length);
        LocalFree(rawMessage);
    } else {
        message = L"Windows error " + std::to_wstring(errorCode);
    }

    while (!message.empty() && (message.back() == L'\n' || message.back() == L'\r' || message.back() == L' ')) {
        message.pop_back();
    }
    return wideToUtf8String(message);
}

std::wstring quoteWindowsProcessArg(const std::wstring& arg) {
    if (arg.empty()) {
        return L"\"\"";
    }

    if (arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return arg;
    }

    std::wstring quoted = L"\"";
    size_t backslashCount = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashCount;
        } else if (ch == L'"') {
            quoted.append(backslashCount * 2 + 1, L'\\');
            quoted.push_back(ch);
            backslashCount = 0;
        } else {
            quoted.append(backslashCount, L'\\');
            backslashCount = 0;
            quoted.push_back(ch);
        }
    }

    quoted.append(backslashCount * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

std::wstring buildWindowsProcessCommandLine(const std::vector<std::string>& args) {
    std::wstring commandLine;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            commandLine.push_back(L' ');
        }
        commandLine += quoteWindowsProcessArg(utf8ToWideString(args[i]));
    }
    return commandLine;
}

int runWindowsProcessCapture(const std::vector<std::string>& args,
                             const std::function<void(const char*, size_t)>& onOutput,
                             std::string& errorOutput) {
    if (args.empty()) {
        errorOutput = "No process arguments provided";
        return -1;
    }

    SECURITY_ATTRIBUTES securityAttributes;
    ZeroMemory(&securityAttributes, sizeof(securityAttributes));
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &securityAttributes, 0)) {
        errorOutput = "CreatePipe failed: " + windowsErrorMessage(GetLastError());
        return -1;
    }

    if (!SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0)) {
        DWORD err = GetLastError();
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        errorOutput = "SetHandleInformation failed: " + windowsErrorMessage(err);
        return -1;
    }

    HANDLE nulInput = CreateFileW(
        L"NUL",
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &securityAttributes,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (nulInput == INVALID_HANDLE_VALUE) {
        nulInput = nullptr;
    }

    STARTUPINFOW startupInfo;
    ZeroMemory(&startupInfo, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = nulInput;
    startupInfo.hStdOutput = writePipe;
    startupInfo.hStdError = writePipe;

    PROCESS_INFORMATION processInfo;
    ZeroMemory(&processInfo, sizeof(processInfo));

    std::wstring commandLine;
    try {
        commandLine = buildWindowsProcessCommandLine(args);
    } catch (const std::exception& e) {
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        if (nulInput) {
            CloseHandle(nulInput);
        }
        errorOutput = e.what();
        return -1;
    }

    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');

    BOOL created = CreateProcessW(
        nullptr,
        mutableCommandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);

    DWORD createError = GetLastError();
    CloseHandle(writePipe);
    if (nulInput) {
        CloseHandle(nulInput);
    }

    if (!created) {
        CloseHandle(readPipe);
        errorOutput = "CreateProcessW failed for " + args.front() + ": " + windowsErrorMessage(createError);
        return -1;
    }

    std::array<char, 4096> buffer;
    DWORD bytesRead = 0;
    while (ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) &&
           bytesRead > 0) {
        if (onOutput) {
            onOutput(buffer.data(), static_cast<size_t>(bytesRead));
        }
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode = 0;
    if (!GetExitCodeProcess(processInfo.hProcess, &exitCode)) {
        errorOutput = "GetExitCodeProcess failed: " + windowsErrorMessage(GetLastError());
        exitCode = static_cast<DWORD>(-1);
    }

    CloseHandle(readPipe);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    return static_cast<int>(exitCode);
}
#endif

// Check if a file exists (use std::filesystem for Unicode path support on Windows)
bool fileExists(const std::string& path) {
    try {
        std::error_code ec;
        return std::filesystem::exists(toFsPath(path), ec) && !ec;
    } catch (const std::exception&) {
        return false;
    }
}

bool tryToFsPath(const std::string& path, std::filesystem::path& out, std::string* error = nullptr) {
    try {
        out = toFsPath(path);
        return true;
    } catch (const std::exception& e) {
        if (error) {
            *error = std::string("Invalid filesystem path: ") + e.what();
        }
    } catch (...) {
        if (error) {
            *error = "Invalid filesystem path: unknown conversion error";
        }
    }
    return false;
}

bool pathExistsNoThrow(const std::filesystem::path& path, std::string* error = nullptr) {
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec && error) {
        *error = "Filesystem exists check failed: " + ec.message();
    }
    return !ec && exists;
}

bool fileSizeNoThrow(const std::filesystem::path& path, int64_t& size, std::string* error = nullptr) {
    std::error_code ec;
    const auto rawSize = std::filesystem::file_size(path, ec);
    if (ec) {
        if (error) {
            *error = "Filesystem size check failed: " + ec.message();
        }
        return false;
    }
    size = static_cast<int64_t>(rawSize);
    return true;
}

void createDirectoriesOrThrow(const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        throw std::runtime_error("Failed to create output directory: " + ec.message());
    }
}

bool candidateExistsOrThrow(const std::filesystem::path& path) {
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) {
        throw std::runtime_error("Failed to check output path: " + ec.message());
    }
    return exists;
}

bool isStructurallyCompletePdf(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size < 16) {
        return false;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }

    char header[5] = {};
    in.read(header, sizeof(header));
    if (std::string(header, sizeof(header)) != "%PDF-") {
        return false;
    }

    const std::streamoff tailSize = static_cast<std::streamoff>(std::min<uintmax_t>(size, 4096));
    in.clear();
    in.seekg(-tailSize, std::ios::end);
    std::string tail(static_cast<size_t>(tailSize), '\0');
    in.read(tail.data(), tailSize);
    return tail.find("%%EOF") != std::string::npos;
}

bool looksLikePostWriteEncodingFailure(const std::string& output) {
    std::string lower = output;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lower.find("charmap") != std::string::npos
        || lower.find("codec can't encode") != std::string::npos
        || lower.find("unicodeencodeerror") != std::string::npos;
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

WatermarkResult failedWatermarkResult(const std::string& input,
                                      const std::string& output,
                                      const std::string& error) {
    WatermarkResult result;
    result.inputFile = input;
    result.outputFile = output;
    result.error = error;
    return result;
}

std::string summarizeProcessOutput(std::string output) {
    output.erase(std::remove(output.begin(), output.end(), '\0'), output.end());
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }

    if (output.empty()) {
        return "No process output was captured.";
    }

    constexpr size_t kMaxErrorChars = 6000;
    if (output.size() <= kMaxErrorChars) {
        return output;
    }

    return "[showing last " + std::to_string(kMaxErrorChars) + " chars]\n" +
           output.substr(output.size() - kMaxErrorChars);
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
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

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
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lower.length() >= 4 && lower.substr(lower.length() - 4) == ".pdf";
}

bool Watermarker::isAudioFile(const std::string& path) {
    std::vector<std::string> extensions = {
        ".mp3", ".flac", ".wav", ".aac", ".ogg", ".m4a", ".wma", ".opus"
    };

    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    for (const auto& ext : extensions) {
        if (lower.length() >= ext.length() &&
            lower.substr(lower.length() - ext.length()) == ext) {
            return true;
        }
    }
    return false;
}

int64_t Watermarker::getFileSize(const std::string& path) {
    namespace fs = std::filesystem;
    fs::path fsPath;
    if (!tryToFsPath(path, fsPath)) {
        return 0;
    }

    std::string error;
    if (!pathExistsNoThrow(fsPath, &error)) {
        return 0;
    }

    int64_t size = 0;
    return fileSizeNoThrow(fsPath, size) ? size : 0;
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
#ifdef _WIN32
    int exitCode = runWindowsProcessCapture(
        args,
        [&stdout_output](const char* data, size_t size) {
            stdout_output.append(data, size);
        },
        stderr_output);
    if (exitCode != 0 && stdout_output.empty() && !stderr_output.empty()) {
        stdout_output = stderr_output;
    }
    return exitCode;
#else
    // Build command string for shell execution with proper escaping
    std::ostringstream cmdStream;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) cmdStream << " ";
        // Use proper shell escaping for all arguments
        cmdStream << shellEscape(args[i]);
    }
    cmdStream << " 2>&1";

    std::string cmd = cmdStream.str();

    FILE* pipe = utf8Popen(cmd, "r");
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
#endif
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

    std::vector<std::string> cmd = {
        ffprobePath,
        "-v", "error",
        "-show_entries", "format=duration",
        "-of", "default=noprint_wrappers=1:nokey=1",
        inputPath
    };

    std::string output, errorOutput;
    int exitCode = runProcess(cmd, output, errorOutput);
    if (exitCode != 0 || output.empty()) {
        return 0.0;
    }

    return std::strtod(output.c_str(), nullptr);
}

int Watermarker::runFFmpegWithProgress(const std::vector<std::string>& args,
                                        const std::string& inputFile,
                                        double durationSeconds,
                                        std::string& output) {
#ifdef _WIN32
    double lastPercent = 0.0;
    std::string line;

    auto flushLine = [&]() {
        if (line.empty()) {
            return;
        }

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
    };

    std::string processError;
    int exitCode = runWindowsProcessCapture(
        args,
        [&](const char* data, size_t size) {
            for (size_t i = 0; i < size; ++i) {
                char ch = data[i];
                if (ch == '\r' || ch == '\n') {
                    flushLine();
                } else {
                    line += ch;
                }
            }
        },
        processError);
    flushLine();

    if (exitCode != 0 && output.empty() && !processError.empty()) {
        output = processError;
    }

    return exitCode;
#else
    // Build command string
    std::ostringstream cmdStream;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) cmdStream << " ";
        cmdStream << shellEscape(args[i]);
    }
    cmdStream << " 2>&1";

    std::string cmd = cmdStream.str();

    FILE* pipe = utf8Popen(cmd, "r");
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
#endif
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

    // Check input file exists (use std::filesystem for Unicode path support)
    namespace fs = std::filesystem;
    fs::path inputFs;
    std::string pathError;
    if (!tryToFsPath(input, inputFs, &pathError)) {
        result.error = pathError + ": " + input;
        LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
            "input_path_invalid", result.error, input);
        return result;
    }
    if (!pathExistsNoThrow(inputFs, &pathError)) {
        result.error = "Input file not found: " + input;
        if (!pathError.empty()) {
            result.error += " (" + pathError + ")";
        }
        LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
            "input_not_found", result.error);
        return result;
    }
    if (!fileSizeNoThrow(inputFs, result.inputSizeBytes, &pathError)) {
        result.error = pathError + ": " + input;
        LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
            "input_size_failed", result.error, input);
        return result;
    }

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

    fs::path outputFs;
    pathError.clear();
    const bool outputPathValid = tryToFsPath(output, outputFs, &pathError);
    const bool outputExists = outputPathValid && pathExistsNoThrow(outputFs, &pathError);
    if (exitCode == 0 && outputExists) {
        result.success = true;
        fileSizeNoThrow(outputFs, result.outputSizeBytes);
        reportProgress(input, 1, 1, 100.0, "complete");
        LogManager::instance().logWatermark("video_complete",
            "Video watermark complete: " + output + " (" + std::to_string(result.processingTimeMs) + "ms)", input);
    } else {
        if (exitCode != 0) {
            result.error = "FFmpeg failed (exit code " + std::to_string(exitCode) + "): " +
                           summarizeProcessOutput(ffmpegOutput);
        } else {
            result.error = "FFmpeg succeeded but output file not found: " + output;
            if (!pathError.empty()) {
                result.error += " (" + pathError + ")";
            }
        }
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
    namespace fs = std::filesystem;
    if (!fileExists(scriptPath)) {
        result.error = "PDF watermark script not found at: " + scriptPath;
        LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
            "script_not_found", result.error);
        return result;
    }

    // Check input file exists (use std::filesystem for Unicode path support)
    fs::path inputFs;
    std::string pathError;
    if (!tryToFsPath(input, inputFs, &pathError)) {
        result.error = pathError + ": " + input;
        LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
            "input_path_invalid", result.error, input);
        return result;
    }
    if (!pathExistsNoThrow(inputFs, &pathError)) {
        result.error = "Input file not found: " + input;
        if (!pathError.empty()) {
            result.error += " (" + pathError + ")";
        }
        LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
            "input_not_found", result.error);
        return result;
    }
    if (!fileSizeNoThrow(inputFs, result.inputSizeBytes, &pathError)) {
        result.error = pathError + ": " + input;
        LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
            "input_size_failed", result.error, input);
        return result;
    }

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

    fs::path outputFs;
    pathError.clear();
    const bool outputPathValid = tryToFsPath(output, outputFs, &pathError);
    const bool outputExists = outputPathValid && pathExistsNoThrow(outputFs, &pathError);
    if (exitCode == 0 && outputExists) {
        result.success = true;
        fileSizeNoThrow(outputFs, result.outputSizeBytes);
        LogManager::instance().logWatermark("pdf_complete",
            "PDF watermark complete: " + output + " (" + std::to_string(result.processingTimeMs) + "ms)", input);
    } else if (exitCode != 0
               && outputExists
               && isStructurallyCompletePdf(outputFs)
               && looksLikePostWriteEncodingFailure(stdout_out + "\n" + stderr_out)) {
        result.success = true;
        fileSizeNoThrow(outputFs, result.outputSizeBytes);
        result.error = "PDF output was created successfully; ignored post-write Python encoding warning.";
        LogManager::instance().log(LogLevel::Warning, LogCategory::Watermark,
            "pdf_post_write_encoding_warning",
            "PDF output exists and is structurally complete after Python encoding warning: " + output,
            input);
    } else {
        if (exitCode != 0) {
            result.error = "PDF watermarking failed (exit code " + std::to_string(exitCode) + "): " + stdout_out;
        } else {
            result.error = "PDF script succeeded but output file not found: " + output;
            if (!pathError.empty()) {
                result.error += " (" + pathError + ")";
            }
        }
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

    fs::path inputFs;
    std::string pathError;
    if (!tryToFsPath(inputPath, inputFs, &pathError)) {
        throw std::runtime_error(pathError);
    }
    fs::path filename = inputFs.filename();

    // Determine base output directory
    fs::path basePath;
    if (!outputDir.empty()) {
        if (!tryToFsPath(outputDir, basePath, &pathError)) {
            throw std::runtime_error(pathError);
        }
    } else if (!rootDir.empty()) {
        if (!tryToFsPath(rootDir, basePath, &pathError)) {
            throw std::runtime_error(pathError);
        }
    } else {
        basePath = inputFs.parent_path();
    }

    // Compute relative subdirectory structure from rootDir
    fs::path relativeSub;
    if (!rootDir.empty()) {
        fs::path inputParent = inputFs.parent_path();
        fs::path rootFs;
        if (!tryToFsPath(rootDir, rootFs, &pathError)) {
            throw std::runtime_error(pathError);
        }
        if (inputParent != rootFs) {
            // Use lexically_relative for safe relative path computation
            relativeSub = inputParent.lexically_relative(rootFs);
            // If relative path starts with "..", it's outside rootDir — ignore
            const std::string relativeText = fromFsPath(relativeSub);
            if (!relativeSub.empty() && relativeText.substr(0, 2) == "..") {
                relativeSub.clear();
            }
        }
    }

    // Build: basePath / memberId / relativeSub / filename
    fs::path memberFs;
    if (!tryToFsPath(memberId, memberFs, &pathError)) {
        throw std::runtime_error(pathError);
    }
    fs::path memberDir = basePath / memberFs / relativeSub;
    createDirectoriesOrThrow(memberDir);

    // Check for existing file and add numbered suffix if needed
    fs::path candidatePath = memberDir / filename;
    if (candidateExistsOrThrow(candidatePath)) {
        std::string fnStr = fromFsPath(filename);
        size_t lastDot = fnStr.find_last_of('.');
        std::string baseName = (lastDot == std::string::npos) ?
            fnStr : fnStr.substr(0, lastDot);
        std::string ext = (lastDot == std::string::npos) ?
            "" : fnStr.substr(lastDot);
        for (int n = 1; n < 1000; ++n) {
            fs::path candidateName;
            const std::string candidateText = baseName + " (" + std::to_string(n) + ")" + ext;
            if (!tryToFsPath(candidateText, candidateName, &pathError)) {
                throw std::runtime_error(pathError);
            }
            candidatePath = memberDir / candidateName;
            if (!candidateExistsOrThrow(candidatePath)) {
                break;
            }
        }
    }
    return fromFsPath(candidatePath);
}

std::string Watermarker::generateOutputPath(const std::string& inputPath,
                                             const std::string& outputDir,
                                             const std::string& rootDir) const {
    namespace fs = std::filesystem;

    fs::path inputFs;
    std::string pathError;
    if (!tryToFsPath(inputPath, inputFs, &pathError)) {
        throw std::runtime_error(pathError);
    }
    std::string filename = fromFsPath(inputFs.filename());

    // Find extension
    size_t lastDot = filename.find_last_of('.');
    std::string baseName = (lastDot == std::string::npos) ?
        filename : filename.substr(0, lastDot);
    std::string ext = (lastDot == std::string::npos) ?
        "" : filename.substr(lastDot);

    // Build output path
    fs::path sourceDir = inputFs.parent_path();
    fs::path outDir;
    if (outputDir.empty()) {
        outDir = sourceDir;
    } else if (!tryToFsPath(outputDir, outDir, &pathError)) {
        throw std::runtime_error(pathError);
    }

    // Preserve subfolder structure when rootDir is provided
    if (!rootDir.empty() && !outputDir.empty()) {
        fs::path rootFs;
        if (!tryToFsPath(rootDir, rootFs, &pathError)) {
            throw std::runtime_error(pathError);
        }
        fs::path inputParent = inputFs.parent_path();
        if (inputParent != rootFs) {
            fs::path relativeSub = inputParent.lexically_relative(rootFs);
            const std::string relativeText = fromFsPath(relativeSub);
            if (!relativeSub.empty() && relativeText.substr(0, 2) != "..") {
                outDir = outDir / relativeSub;
                createDirectoriesOrThrow(outDir);
            }
        }
    }

    // Only add suffix when output goes to the same directory as the source
    // (to avoid overwriting the original). When a different output directory
    // is specified, the original is safe and the suffix is unnecessary.
    std::string suffix;
    if (outputDir.empty() || outDir == sourceDir) {
        suffix = m_config.outputSuffix;
    }

    // Check if output file already exists; if so, append (1), (2), etc.
    fs::path candidateName;
    if (!tryToFsPath(baseName + suffix + ext, candidateName, &pathError)) {
        throw std::runtime_error(pathError);
    }
    fs::path candidatePath = outDir / candidateName;
    if (candidateExistsOrThrow(candidatePath)) {
        for (int n = 1; n < 1000; ++n) {
            const std::string candidateText = baseName + suffix + " (" + std::to_string(n) + ")" + ext;
            if (!tryToFsPath(candidateText, candidateName, &pathError)) {
                throw std::runtime_error(pathError);
            }
            candidatePath = outDir / candidateName;
            if (!candidateExistsOrThrow(candidatePath)) {
                break;
            }
        }
    }
    return fromFsPath(candidatePath);
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
    WatermarkResult fallback;
    fallback.inputFile = inputPath;
    fallback.outputFile = outputPath;

    try {
        std::string outPath = outputPath.empty() ?
            generateOutputPath(inputPath, "") : outputPath;
        fallback.outputFile = outPath;

        reportProgress(inputPath, 1, 1, 0.0, "encoding");
        auto result = executeFFmpeg(inputPath, outPath);
        reportProgress(inputPath, 1, 1, 100.0, result.success ? "complete" : "error");

        return result;
    } catch (const std::exception& e) {
        fallback.error = std::string("Video watermark failed before processing: ") + e.what();
    } catch (...) {
        fallback.error = "Video watermark failed before processing: unknown error";
    }

    reportProgress(inputPath, 1, 1, 100.0, "error");
    LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
        "video_watermark_exception", fallback.error, inputPath);
    return fallback;
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
    WatermarkResult fallback;
    fallback.inputFile = inputPath;
    fallback.outputFile = outputPath;

    try {
        std::string outPath = outputPath.empty() ?
            generateOutputPath(inputPath, "") : outputPath;
        fallback.outputFile = outPath;

        reportProgress(inputPath, 1, 1, 0.0, "processing");
        auto result = executePdfScript(inputPath, outPath);
        reportProgress(inputPath, 1, 1, 100.0, result.success ? "complete" : "error");

        return result;
    } catch (const std::exception& e) {
        fallback.error = std::string("PDF watermark failed before processing: ") + e.what();
    } catch (...) {
        fallback.error = "PDF watermark failed before processing: unknown error";
    }

    reportProgress(inputPath, 1, 1, 100.0, "error");
    LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
        "pdf_watermark_exception", fallback.error, inputPath);
    return fallback;
}

WatermarkResult Watermarker::watermarkAudio(const std::string& inputPath,
                                              const std::string& outputPath) {
    WatermarkResult result;
    result.inputFile = inputPath;

    try {
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

        if (exitCode == 0 && fileExists(outPath)) {
            result.success = true;
            result.outputSizeBytes = getFileSize(outPath);
            LogManager::instance().logWatermark("audio_complete",
                "Audio metadata complete: " + outPath + " (" + std::to_string(result.processingTimeMs) + "ms)", inputPath);
        } else {
            if (exitCode == 0) {
                result.error = "FFmpeg succeeded but output file not found: " + outPath;
            } else {
                result.error = "FFmpeg failed (exit code " + std::to_string(exitCode) + "): " +
                               summarizeProcessOutput(ffmpegOutput);
            }
            LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
                "audio_metadata_failed", "Audio metadata failed: " + inputPath + " - " + result.error, inputPath);
        }
    } catch (const std::exception& e) {
        result.error = std::string("Audio metadata failed before processing: ") + e.what();
        LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
            "audio_metadata_exception", result.error, inputPath);
    } catch (...) {
        result.error = "Audio metadata failed before processing: unknown error";
        LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
            "audio_metadata_exception", result.error, inputPath);
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
        // Passthrough: copy unsupported files as-is (e.g., .vtt, .docx, .txt)
        WatermarkResult result;
        result.inputFile = inputPath;
        namespace fs = std::filesystem;
        try {
            std::string outPath = outputPath.empty() ? generateOutputPath(inputPath, "") : outputPath;
            result.outputFile = outPath;

            fs::path inputFs;
            fs::path outFs;
            std::string pathError;
            if (!tryToFsPath(inputPath, inputFs, &pathError)) {
                throw std::runtime_error(pathError);
            }
            if (!tryToFsPath(outPath, outFs, &pathError)) {
                throw std::runtime_error(pathError);
            }

            createDirectoriesOrThrow(outFs.parent_path());
            std::error_code ec;
            fs::copy_file(inputFs, outFs, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                throw std::runtime_error("Failed to copy passthrough file: " + ec.message());
            }
            result.success = true;
            LogManager::instance().logWatermark("passthrough_copy",
                "Copied passthrough file: " + outPath, inputPath);
        } catch (const std::exception& e) {
            result.error = std::string("Failed to copy passthrough file: ") + e.what();
            LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
                "passthrough_failed", result.error, inputPath);
        } catch (...) {
            result.error = "Failed to copy passthrough file: unknown error";
            LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
                "passthrough_failed", result.error, inputPath);
        }
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

            try {
                std::string outPath = generateOutputPath(inputPaths[i], outputDir);
                results.push_back(executeFFmpeg(inputPaths[i], outPath));
            } catch (const std::exception& e) {
                results.push_back(failedWatermarkResult(
                    inputPaths[i], "", std::string("Video batch path failed: ") + e.what()));
            } catch (...) {
                results.push_back(failedWatermarkResult(
                    inputPaths[i], "", "Video batch path failed: unknown error"));
            }
        }
    } else {
        // Parallel processing using std::async
        std::vector<std::future<WatermarkResult>> futures;

        for (size_t i = 0; i < inputPaths.size() && !m_cancelled; ++i) {
            if (futures.size() >= static_cast<size_t>(parallel)) {
                // Wait for one to complete
                auto& f = futures.front();
                try {
                    results.push_back(f.get());
                } catch (const std::exception& e) {
                    results.push_back(failedWatermarkResult(
                        "", "", std::string("Video batch worker failed: ") + e.what()));
                } catch (...) {
                    results.push_back(failedWatermarkResult(
                        "", "", "Video batch worker failed: unknown error"));
                }
                futures.erase(futures.begin());
            }

            try {
                std::string outPath = generateOutputPath(inputPaths[i], outputDir);
                futures.push_back(std::async(std::launch::async, [this, &inputPaths, i, outPath]() {
                    return executeFFmpeg(inputPaths[i], outPath);
                }));
            } catch (const std::exception& e) {
                results.push_back(failedWatermarkResult(
                    inputPaths[i], "", std::string("Video batch path failed: ") + e.what()));
            } catch (...) {
                results.push_back(failedWatermarkResult(
                    inputPaths[i], "", "Video batch path failed: unknown error"));
            }
        }

        // Wait for remaining
        for (auto& f : futures) {
            try {
                results.push_back(f.get());
            } catch (const std::exception& e) {
                results.push_back(failedWatermarkResult(
                    "", "", std::string("Video batch worker failed: ") + e.what()));
            } catch (...) {
                results.push_back(failedWatermarkResult(
                    "", "", "Video batch worker failed: unknown error"));
            }
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

        try {
            std::string outPath = generateOutputPath(inputPaths[i], outputDir);
            results.push_back(executePdfScript(inputPaths[i], outPath));
        } catch (const std::exception& e) {
            results.push_back(failedWatermarkResult(
                inputPaths[i], "", std::string("PDF batch path failed: ") + e.what()));
        } catch (...) {
            results.push_back(failedWatermarkResult(
                inputPaths[i], "", "PDF batch path failed: unknown error"));
        }
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
        fs::path scanRoot;
        std::string pathError;
        if (!tryToFsPath(inputDir, scanRoot, &pathError)) {
            throw std::runtime_error(pathError);
        }

        if (recursive) {
            for (const auto& entry : fs::recursive_directory_iterator(scanRoot)) {
                std::error_code ec;
                if (entry.is_regular_file(ec) && !ec) {
                    std::string p = fromFsPath(entry.path());
                    if (isVideoFile(p)) videoFiles.push_back(p);
                    else if (isPdfFile(p)) pdfFiles.push_back(p);
                    else if (isAudioFile(p)) audioFiles.push_back(p);
                }
            }
        } else {
            for (const auto& entry : fs::directory_iterator(scanRoot)) {
                std::error_code ec;
                if (entry.is_regular_file(ec) && !ec) {
                    std::string p = fromFsPath(entry.path());
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
            try {
                std::string outPath = generateOutputPath(audioFiles[i], outputDir);
                results.push_back(watermarkAudio(audioFiles[i], outPath));
            } catch (const std::exception& e) {
                results.push_back(failedWatermarkResult(
                    audioFiles[i], "", std::string("Audio directory path failed: ") + e.what()));
            } catch (...) {
                results.push_back(failedWatermarkResult(
                    audioFiles[i], "", "Audio directory path failed: unknown error"));
            }
        }
    }

    return results;
}

} // namespace MegaCustom
