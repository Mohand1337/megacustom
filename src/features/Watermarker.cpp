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
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
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
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
#ifdef _WIN32
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

std::string findWindowsExecutableOnPath(const wchar_t* executableName) {
    const DWORD required = SearchPathW(nullptr, executableName, nullptr, 0, nullptr, nullptr);
    if (required == 0) {
        return {};
    }

    std::vector<wchar_t> path(static_cast<size_t>(required) + 1, L'\0');
    const DWORD length = SearchPathW(
        nullptr,
        executableName,
        nullptr,
        static_cast<DWORD>(path.size()),
        path.data(),
        nullptr);
    if (length == 0 || length >= path.size()) {
        return {};
    }
    return wideToUtf8String(std::wstring(path.data(), length));
}
#endif

// Cross-platform function to get user home directory
std::string getHomeDirectory() {
#ifdef _WIN32
    if (const wchar_t* userProfile = _wgetenv(L"USERPROFILE")) {
        return wideToUtf8String(userProfile);
    }
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, path))) {
        return wideToUtf8String(path);
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
    std::vector<wchar_t> path(32768, L'\0');
    DWORD len = GetModuleFileNameW(NULL, path.data(), static_cast<DWORD>(path.size()));
    if (len > 0 && len < path.size()) {
        std::string exePath = wideToUtf8String(std::wstring(path.data(), len));
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
                             std::string& errorOutput,
                             const std::atomic<bool>* cancelled = nullptr) {
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
    bool cancellationSent = false;
    bool processFinished = false;
    while (!processFinished) {
        if (cancelled && cancelled->load() && !cancellationSent) {
            cancellationSent = true;
            TerminateProcess(processInfo.hProcess, ERROR_CANCELLED);
        }

        DWORD available = 0;
        if (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
            DWORD bytesRead = 0;
            const DWORD requested = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
            if (ReadFile(readPipe, buffer.data(), requested, &bytesRead, nullptr) && bytesRead > 0 && onOutput) {
                onOutput(buffer.data(), static_cast<size_t>(bytesRead));
            }
        }

        processFinished = WaitForSingleObject(processInfo.hProcess, 25) == WAIT_OBJECT_0;
    }

    // Drain output written immediately before process exit.
    while (true) {
        DWORD available = 0;
        if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) || available == 0) {
            break;
        }
        DWORD bytesRead = 0;
        const DWORD requested = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
        if (!ReadFile(readPipe, buffer.data(), requested, &bytesRead, nullptr) || bytesRead == 0) {
            break;
        }
        if (onOutput) {
            onOutput(buffer.data(), static_cast<size_t>(bytesRead));
        }
    }

    DWORD exitCode = 0;
    if (!GetExitCodeProcess(processInfo.hProcess, &exitCode)) {
        errorOutput = "GetExitCodeProcess failed: " + windowsErrorMessage(GetLastError());
        exitCode = static_cast<DWORD>(-1);
    }

    CloseHandle(readPipe);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    if (cancellationSent) {
        errorOutput = "Process cancelled.";
        return -2;
    }
    return static_cast<int>(exitCode);
}
#else
int runPosixProcessCapture(const std::vector<std::string>& args,
                           const std::function<void(const char*, size_t)>& onOutput,
                           std::string& errorOutput,
                           const std::atomic<bool>* cancelled = nullptr) {
    if (args.empty()) {
        errorOutput = "No process arguments provided";
        return -1;
    }

    int outputPipe[2] = {-1, -1};
    if (pipe(outputPipe) != 0) {
        errorOutput = std::string("pipe failed: ") + std::strerror(errno);
        return -1;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        errorOutput = std::string("fork failed: ") + std::strerror(errno);
        close(outputPipe[0]);
        close(outputPipe[1]);
        return -1;
    }

    if (pid == 0) {
        setpgid(0, 0);
        close(outputPipe[0]);
        dup2(outputPipe[1], STDOUT_FILENO);
        dup2(outputPipe[1], STDERR_FILENO);
        close(outputPipe[1]);

        const int nullInput = open("/dev/null", O_RDONLY);
        if (nullInput >= 0) {
            dup2(nullInput, STDIN_FILENO);
            close(nullInput);
        }

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const std::string& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        const std::string message = std::string("exec failed for ") + args.front() + ": " + std::strerror(errno) + "\n";
        const ssize_t ignored = write(STDERR_FILENO, message.data(), message.size());
        (void)ignored;
        _exit(127);
    }

    close(outputPipe[1]);
    setpgid(pid, pid);
    const int flags = fcntl(outputPipe[0], F_GETFL, 0);
    if (flags >= 0) {
        fcntl(outputPipe[0], F_SETFL, flags | O_NONBLOCK);
    }

    bool cancellationSent = false;
    bool childFinished = false;
    bool pipeClosed = false;
    int status = 0;
    auto cancellationTime = std::chrono::steady_clock::time_point{};
    std::array<char, 4096> buffer;

    while (!childFinished || !pipeClosed) {
        struct pollfd descriptor;
        descriptor.fd = outputPipe[0];
        descriptor.events = POLLIN | POLLHUP;
        descriptor.revents = 0;
        poll(&descriptor, 1, 50);

        while (!pipeClosed) {
            const ssize_t count = read(outputPipe[0], buffer.data(), buffer.size());
            if (count > 0) {
                if (onOutput) {
                    onOutput(buffer.data(), static_cast<size_t>(count));
                }
                continue;
            }
            if (count == 0) {
                pipeClosed = true;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                pipeClosed = true;
            }
            break;
        }

        if (!childFinished) {
            const pid_t waited = waitpid(pid, &status, WNOHANG);
            childFinished = waited == pid || (waited < 0 && errno == ECHILD);
        }

        if (cancelled && cancelled->load() && !cancellationSent && !childFinished) {
            cancellationSent = true;
            cancellationTime = std::chrono::steady_clock::now();
            kill(-pid, SIGTERM);
        } else if (cancellationSent && !childFinished
                   && std::chrono::steady_clock::now() - cancellationTime > std::chrono::seconds(2)) {
            kill(-pid, SIGKILL);
        }
    }

    close(outputPipe[0]);
    if (!childFinished) {
        waitpid(pid, &status, 0);
    }

    if (cancellationSent) {
        errorOutput = "Process cancelled.";
        return -2;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        errorOutput = "Process terminated by signal " + std::to_string(WTERMSIG(status));
        return 128 + WTERMSIG(status);
    }
    errorOutput = "Process ended without an exit status.";
    return -1;
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

std::string trimCopy(const std::string& value) {
    const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }).base();
    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

double parseRational(const std::string& value) {
    const auto slash = value.find('/');
    if (slash == std::string::npos) {
        return std::strtod(value.c_str(), nullptr);
    }

    const double num = std::strtod(value.substr(0, slash).c_str(), nullptr);
    const double den = std::strtod(value.substr(slash + 1).c_str(), nullptr);
    return den == 0.0 ? 0.0 : num / den;
}

uint64_t fnv1aAppend(uint64_t hash, const std::string& value) {
    constexpr uint64_t kPrime = 1099511628211ULL;
    for (unsigned char c : value) {
        hash ^= static_cast<uint64_t>(c);
        hash *= kPrime;
    }
    return hash;
}

std::string stableHashHex(const std::vector<std::string>& parts) {
    uint64_t hash = 1469598103934665603ULL;
    for (const auto& part : parts) {
        hash = fnv1aAppend(hash, part);
        hash = fnv1aAppend(hash, "\x1f");
    }

    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}

std::string formatSeconds(double seconds) {
    if (!std::isfinite(seconds)) {
        seconds = 0.0;
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(6) << seconds;
    return out.str();
}

std::string concatManifestEscape(const std::string& path) {
    std::string escaped = "'";
    for (char c : path) {
        if (c == '\'') {
            escaped += "'\\''";
        } else {
            escaped += c;
        }
    }
    escaped += "'";
    return escaped;
}

std::string ffmpegFilterEscape(const std::string& text) {
    std::string result;
    for (char c : text) {
        if (c == '\'' || c == ':' || c == '\\') {
            result += '\\';
        }
        result += c;
    }
    return result;
}

std::map<std::string, std::string> parseKeyValueProbe(const std::string& output) {
    std::map<std::string, std::string> values;
    for (const std::string& line : splitLines(output)) {
        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        values[line.substr(0, equals)] = trimCopy(line.substr(equals + 1));
    }
    return values;
}

struct SegmentProbe {
    std::string videoCodec;
    std::string audioCodec;
    std::string pixelFormat;
    std::string avgFrameRate;
    std::string realFrameRate;
    std::string timeBase;
    std::string fieldOrder;
    std::string colorTransfer;
    std::string colorPrimaries;
    int width = 0;
    int height = 0;
    double frameRate = 0.0;
    double realFrameRateValue = 0.0;
    double rotation = 0.0;
    bool hasClosedCaptions = false;
    double duration = 0.0;
    int videoStreams = 0;
    int audioStreams = 0;
    int otherStreams = 0;
    bool hasAudio = false;
    std::vector<double> keyframes;
};

struct VisibleWatermarkWindow {
    double sourceStart = 0.0;
    double sourceEnd = 0.0;
    double localStart = 0.0;
    double localEnd = 0.0;
    double xFraction = 0.0;
    double yFraction = 0.0;
};

struct EncodedSegmentWindow {
    double start = 0.0;
    double end = 0.0;
    std::vector<VisibleWatermarkWindow> visibleWindows;
};

struct PlannedPiece {
    double start = 0.0;
    double end = 0.0;
    bool watermark = false;
    int watermarkIndex = -1;
    std::string sourceSegmentPath;
    std::string memberSegmentPath;
};

double previousKeyframeAtOrBefore(const std::vector<double>& keyframes, double target) {
    double selected = 0.0;
    for (double keyframe : keyframes) {
        if (keyframe <= target + 0.0005) {
            selected = keyframe;
        } else {
            break;
        }
    }
    return selected;
}

double nextKeyframeAtOrAfter(const std::vector<double>& keyframes, double target, double duration) {
    for (double keyframe : keyframes) {
        if (keyframe >= target - 0.0005) {
            return std::min(keyframe, duration);
        }
    }
    return duration;
}

double deterministicFraction(const std::string& seed, double minValue = 0.0, double maxValue = 1.0) {
    const std::string hash = stableHashHex({seed});
    const uint64_t raw = std::strtoull(hash.substr(0, 12).c_str(), nullptr, 16);
    const double unit = static_cast<double>(raw % 1000000ULL) / 1000000.0;
    return minValue + (maxValue - minValue) * unit;
}

std::string ffprobePathFromFFmpegPath(const std::string& ffmpegPath) {
    if (ffmpegPath.empty()) {
        return "ffprobe";
    }
    size_t pos = ffmpegPath.rfind("ffmpeg");
    if (pos != std::string::npos) {
        return ffmpegPath.substr(0, pos) + "ffprobe" + ffmpegPath.substr(pos + 6);
    }
    return "ffprobe";
}

std::string buildScheduledDrawtextFilter(const WatermarkConfig& config,
                                         const std::vector<VisibleWatermarkWindow>& windows) {
    std::string fontFilePath = config.fontPath;
#ifdef _WIN32
    if (fontFilePath.empty()) {
        const char* windir = std::getenv("WINDIR");
        std::string winFontsDir = windir ? std::string(windir) + "\\Fonts" : "C:\\Windows\\Fonts";
        std::string arialPath = winFontsDir + "\\arial.ttf";
        if (fileExists(arialPath)) {
            fontFilePath = arialPath;
        }
    }
#endif
    std::replace(fontFilePath.begin(), fontFilePath.end(), '\\', '/');
#ifdef _WIN32
    if (fontFilePath.size() >= 2 && fontFilePath[1] == ':') {
        fontFilePath = fontFilePath.substr(2);
    }
#endif

    const std::string fontFile = fontFilePath.empty() ? "" : "fontfile=" + fontFilePath + ":";

    std::ostringstream filter;
    bool first = true;
    auto appendDrawtext = [&](const VisibleWatermarkWindow& window,
                              const std::string& text,
                              int fontSize,
                              const std::string& color,
                              bool secondary) {
        if (text.empty()) {
            return;
        }
        if (!first) {
            filter << ",";
        }
        first = false;

        filter << "drawtext=" << fontFile
               << "expansion=none:"
               << "text='" << ffmpegFilterEscape(text) << "':"
               << "fontsize=" << fontSize << ":"
               << "fontcolor=" << color << ":"
               << "x=(w-text_w)*" << formatSeconds(window.xFraction) << ":"
               << "y=((h-text_h-40)*" << formatSeconds(window.yFraction) << ")";
        if (secondary) {
            filter << "+30";
        }
        filter << ":enable=between(t\\," << formatSeconds(window.localStart)
               << "\\," << formatSeconds(window.localEnd) << ")";
    };

    for (const auto& window : windows) {
        appendDrawtext(window, config.primaryText, config.primaryFontSize, config.primaryColor, false);
        appendDrawtext(window, config.secondaryText, config.secondaryFontSize, config.secondaryColor, true);
    }

    return filter.str();
}

std::filesystem::path segmentCacheRoot(const std::string& configuredDirectory = {}) {
    namespace fs = std::filesystem;
    if (!configuredDirectory.empty()) {
        return toFsPath(configuredDirectory);
    }
    if (const char* configuredRoot = std::getenv("MEGACUSTOM_CONFIG_DIR")) {
        return fs::u8path(configuredRoot) / "cache" / "segment-cache";
    }

#ifdef _WIN32
    if (const wchar_t* localAppData = _wgetenv(L"LOCALAPPDATA")) {
        return fs::path(localAppData) / L"MegaCustom" / L"segment-cache";
    }
    return toFsPath(getHomeDirectory()) / ".megacustom" / "cache" / "segment-cache";
#elif defined(__APPLE__)
    return toFsPath(getHomeDirectory()) / "Library" / "Caches" / "MegaCustom" / "segment-cache";
#else
    if (const char* xdgCache = std::getenv("XDG_CACHE_HOME")) {
        return toFsPath(xdgCache) / "megacustom" / "segment-cache";
    }
    return toFsPath(getHomeDirectory()) / ".cache" / "megacustom" / "segment-cache";
#endif
}

bool pathIsWithin(const std::filesystem::path& candidate,
                  const std::filesystem::path& parent) {
    if (candidate.empty() || parent.empty()) {
        return false;
    }
    std::error_code ec;
    const auto candidatePath = std::filesystem::weakly_canonical(candidate, ec);
    if (ec) return false;
    const auto parentPath = std::filesystem::weakly_canonical(parent, ec);
    if (ec) return false;

    auto candidatePart = candidatePath.begin();
    auto parentPart = parentPath.begin();
    for (; parentPart != parentPath.end(); ++parentPart, ++candidatePart) {
        if (candidatePart == candidatePath.end()) {
            return false;
        }
#ifdef _WIN32
        const std::wstring candidateValue = candidatePart->native();
        const std::wstring parentValue = parentPart->native();
        if (CompareStringOrdinal(
                candidateValue.c_str(), static_cast<int>(candidateValue.size()),
                parentValue.c_str(), static_cast<int>(parentValue.size()), TRUE) != CSTR_EQUAL) {
            return false;
        }
#else
        if (*candidatePart != *parentPart) {
            return false;
        }
#endif
    }
    return true;
}

void setCacheDirectoryPermissions(const std::filesystem::path& path) {
#ifndef _WIN32
    std::error_code ec;
    std::filesystem::permissions(
        path,
        std::filesystem::perms::owner_all,
        std::filesystem::perm_options::replace,
        ec);
#else
    (void)path;
#endif
}

std::string uniqueWorkToken() {
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::ostringstream token;
#ifdef _WIN32
    token << _getpid();
#else
    token << getpid();
#endif
    token << "_" << std::this_thread::get_id() << "_" << now;
    return stableHashHex({token.str()});
}

bool isCacheKeyName(const std::string& name) {
    return name.size() == 16 && std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isxdigit(c) != 0;
    });
}

bool isCacheBuildLockName(const std::string& name) {
    return name.size() == 21 && name.substr(16) == ".lock"
        && isCacheKeyName(name.substr(0, 16));
}

bool isCacheUsageLockName(const std::string& name) {
    return name.size() > 21 && name.substr(16, 5) == ".use."
        && isCacheKeyName(name.substr(0, 16));
}

bool isCacheLockName(const std::string& name) {
    return isCacheBuildLockName(name) || isCacheUsageLockName(name);
}

constexpr const char* kCacheClearLockName = ".clear.lock";
constexpr const char* kCacheEntryMarkerName = ".megacustom-segment-cache";

bool looksLikeSegmentCacheEntry(const std::filesystem::path& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(path, ec) || ec) {
        return false;
    }
    if (fs::is_regular_file(path / kCacheEntryMarkerName, ec) && !ec) {
        return true;
    }
    ec.clear();
    for (fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        const std::string name = fromFsPath(it->path().filename());
        if (name == "source_segments.ready" || name == "keyframes.txt"
            || name.rfind("src_seg_", 0) == 0
            || name.rfind("work_", 0) == 0
            || name.rfind("member_", 0) == 0
            || name.rfind("source_segments.ready.", 0) == 0
            || name.rfind("keyframes.txt.", 0) == 0) {
            return true;
        }
    }
    return false;
}

int64_t directorySizeNoThrow(const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    int64_t total = 0;
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;
    while (!ec && it != end) {
        if (it->is_regular_file(ec) && !ec) {
            const auto size = it->file_size(ec);
            if (!ec && size <= static_cast<uintmax_t>(std::numeric_limits<int64_t>::max() - total)) {
                total += static_cast<int64_t>(size);
            }
        }
        ec.clear();
        it.increment(ec);
    }
    return total;
}

struct CacheEntryInfo {
    std::filesystem::path path;
    std::filesystem::file_time_type lastAccess{};
    int64_t sizeBytes = 0;
    bool complete = false;
};

std::vector<CacheEntryInfo> scanCacheEntries(const std::filesystem::path& root,
                                             SegmentCacheStats* stats = nullptr) {
    namespace fs = std::filesystem;
    std::vector<CacheEntryInfo> entries;
    std::error_code ec;
    if (!fs::exists(root, ec) || ec) {
        return entries;
    }

    for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec) || ec) {
            ec.clear();
            continue;
        }
        const std::string name = fromFsPath(it->path().filename());
        if (!isCacheKeyName(name) || !looksLikeSegmentCacheEntry(it->path())) {
            continue;
        }

        CacheEntryInfo entry;
        entry.path = it->path();
        const fs::path readyPath = entry.path / "source_segments.ready";
        entry.complete = fs::is_regular_file(readyPath, ec) && !ec;
        ec.clear();
        entry.lastAccess = fs::last_write_time(entry.complete ? readyPath : entry.path, ec);
        if (ec) {
            ec.clear();
            entry.lastAccess = fs::file_time_type::min();
        }
        if (entry.complete) {
            std::ifstream readyFile(readyPath, std::ios::binary);
            std::ostringstream readyBuffer;
            readyBuffer << readyFile.rdbuf();
            const auto readyValues = parseKeyValueProbe(readyBuffer.str());
            const auto sizeIt = readyValues.find("cache_bytes");
            if (sizeIt != readyValues.end()) {
                entry.sizeBytes = std::max<int64_t>(
                    0, std::strtoll(sizeIt->second.c_str(), nullptr, 10));
            }
        }
        if (entry.sizeBytes <= 0) {
            entry.sizeBytes = directorySizeNoThrow(entry.path);
        }
        entries.push_back(entry);

        if (stats) {
            stats->sizeBytes += entry.sizeBytes;
            if (entry.complete) {
                ++stats->entryCount;
            } else {
                ++stats->incompleteEntryCount;
            }
        }
    }
    return entries;
}

bool cacheEntryLocked(const std::filesystem::path& root,
                      const std::filesystem::path& entryPath) {
    const std::string cacheKey = fromFsPath(entryPath.filename());
    if (pathExistsNoThrow(root / (cacheKey + ".lock"))) {
        return true;
    }

    std::error_code ec;
    for (std::filesystem::directory_iterator it(
             root, std::filesystem::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        const std::string name = fromFsPath(it->path().filename());
        if (name.rfind(cacheKey + ".use.", 0) == 0) {
            return true;
        }
    }
    return false;
}

void pruneSegmentCacheNoThrow(const std::filesystem::path& root,
                              int64_t maxBytes,
                              int maxAgeDays) {
    namespace fs = std::filesystem;
    try {
        createDirectoriesOrThrow(root);

        const auto now = fs::file_time_type::clock::now();
        const auto staleBuildAge = std::chrono::hours(24);
        std::error_code ec;

        const fs::path clearLockPath = root / kCacheClearLockName;
        if (fs::exists(clearLockPath, ec) && !ec) {
            const auto modified = fs::last_write_time(clearLockPath, ec);
            if (!ec && now - modified > staleBuildAge) {
                fs::remove_all(clearLockPath, ec);
            } else {
                return;
            }
        }
        ec.clear();

        // Remove abandoned locks. Active builds are expected to finish well
        // inside a day; a day-old lock means the app or machine stopped mid-job.
        std::vector<fs::path> staleLocks;
        for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            const std::string name = fromFsPath(it->path().filename());
            if (!isCacheLockName(name)) {
                continue;
            }
            const auto modified = fs::last_write_time(it->path(), ec);
            if (!ec && now - modified > staleBuildAge) {
                staleLocks.push_back(it->path());
            }
            ec.clear();
        }
        for (const fs::path& staleLock : staleLocks) {
            fs::remove_all(staleLock, ec);
            ec.clear();
        }

        auto entries = scanCacheEntries(root);
        int64_t totalSize = 0;
        for (auto& entry : entries) {
            totalSize += entry.sizeBytes;
            if (cacheEntryLocked(root, entry.path)) {
                continue;
            }

            // Member-specific encoded chunks are work files, never reusable
            // clean cache. Remove leftovers from an interrupted prior run.
            std::vector<fs::path> staleWorkPaths;
            for (fs::directory_iterator child(entry.path, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && child != end; child.increment(ec)) {
                const std::string childName = fromFsPath(child->path().filename());
                if (child->is_directory(ec)
                    && (childName.rfind("work_", 0) == 0 || childName.rfind("member_", 0) == 0)) {
                    staleWorkPaths.push_back(child->path());
                } else if (child->is_regular_file(ec)
                           && (childName.rfind("source_segments.ready.", 0) == 0
                               || childName.rfind("keyframes.txt.", 0) == 0)) {
                    staleWorkPaths.push_back(child->path());
                }
                ec.clear();
            }
            for (const fs::path& staleWorkPath : staleWorkPaths) {
                fs::remove_all(staleWorkPath, ec);
                ec.clear();
            }
            if (!staleWorkPaths.empty()) {
                const int64_t updatedSize = directorySizeNoThrow(entry.path);
                totalSize -= std::max<int64_t>(0, entry.sizeBytes - updatedSize);
                entry.sizeBytes = updatedSize;
            }

            const auto age = entry.lastAccess == fs::file_time_type::min()
                ? staleBuildAge + std::chrono::hours(1)
                : now - entry.lastAccess;
            const bool incompleteAndStale = !entry.complete && age > staleBuildAge;
            const bool expired = entry.complete && maxAgeDays > 0
                && age > std::chrono::hours(24LL * maxAgeDays);
            if (incompleteAndStale || expired) {
                fs::remove_all(entry.path, ec);
                if (!ec) {
                    totalSize -= entry.sizeBytes;
                    entry.sizeBytes = 0;
                }
                ec.clear();
            }
        }

        if (maxBytes > 0 && totalSize > maxBytes) {
            std::sort(entries.begin(), entries.end(), [](const CacheEntryInfo& a, const CacheEntryInfo& b) {
                return a.lastAccess < b.lastAccess;
            });
            for (const auto& entry : entries) {
                if (totalSize <= maxBytes) {
                    break;
                }
                if (entry.sizeBytes <= 0 || cacheEntryLocked(root, entry.path)) {
                    continue;
                }
                fs::remove_all(entry.path, ec);
                if (!ec) {
                    totalSize -= entry.sizeBytes;
                }
                ec.clear();
            }
        }
    } catch (...) {
        // Cache maintenance is opportunistic and must never block watermarking.
    }
}

std::string quickSourceFingerprint(const std::filesystem::path& path, int64_t fileSize) {
    constexpr std::streamoff kSampleBytes = 64 * 1024;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }

    std::string first(static_cast<size_t>(std::min<int64_t>(fileSize, kSampleBytes)), '\0');
    input.read(first.data(), static_cast<std::streamsize>(first.size()));
    first.resize(static_cast<size_t>(input.gcount()));

    std::string last;
    if (fileSize > kSampleBytes) {
        input.clear();
        input.seekg(std::max<std::streamoff>(0, static_cast<std::streamoff>(fileSize) - kSampleBytes));
        last.resize(static_cast<size_t>(std::min<int64_t>(fileSize, kSampleBytes)), '\0');
        input.read(last.data(), static_cast<std::streamsize>(last.size()));
        last.resize(static_cast<size_t>(input.gcount()));
    }
    return stableHashHex({first, last});
}

class ScopedPathCleanup {
public:
    explicit ScopedPathCleanup(std::filesystem::path path = {}) : m_path(std::move(path)) {}
    ~ScopedPathCleanup() {
        if (!m_path.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(m_path, ec);
        }
    }
    void release() { m_path.clear(); }

private:
    std::filesystem::path m_path;
};

class CacheBuildLock {
public:
    ~CacheBuildLock() {
        release();
    }

    bool acquire(const std::filesystem::path& lockPath,
                 const std::function<bool()>& cacheReady,
                 const std::atomic<bool>& cancelled,
                 std::string& error) {
        namespace fs = std::filesystem;
        m_path = lockPath;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
        while (std::chrono::steady_clock::now() < deadline) {
            std::error_code ec;
            if (fs::create_directory(lockPath, ec)) {
                m_owned = true;
                setCacheDirectoryPermissions(lockPath);
                return true;
            }
            if (ec && ec != std::errc::file_exists) {
                error = "Could not create segment-cache lock: " + ec.message();
                return false;
            }
            if (cacheReady()) {
                return true;
            }
            if (cancelled.load()) {
                error = "Segment-cache build cancelled while waiting for another worker.";
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        error = "Timed out waiting for another worker to build the segment cache.";
        return false;
    }

    bool ownsLock() const { return m_owned; }
    void release() {
        if (!m_owned) {
            return;
        }
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
        m_owned = false;
    }

private:
    std::filesystem::path m_path;
    bool m_owned = false;
};

bool acquireCacheUsageLease(const std::filesystem::path& cacheRoot,
                            const std::string& cacheHash,
                            const std::atomic<bool>& cancelled,
                            std::filesystem::path& leasePath,
                            std::string& error) {
    namespace fs = std::filesystem;
    const fs::path clearLockPath = cacheRoot / kCacheClearLockName;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);

    while (std::chrono::steady_clock::now() < deadline) {
        if (cancelled.load()) {
            error = "Segment-cache use cancelled before a lease was acquired.";
            return false;
        }
        if (pathExistsNoThrow(clearLockPath)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        const fs::path candidate = cacheRoot /
            (cacheHash + ".use." + uniqueWorkToken());
        std::error_code ec;
        if (!fs::create_directory(candidate, ec)) {
            if (ec) {
                error = "Could not create segment-cache usage lease: " + ec.message();
                return false;
            }
            continue;
        }
        setCacheDirectoryPermissions(candidate);

        // Close the race with cache clearing: either the lease existed before
        // the clearer inspected active users, or this worker backs out and waits.
        if (pathExistsNoThrow(clearLockPath)) {
            fs::remove_all(candidate, ec);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        leasePath = candidate;
        return true;
    }

    error = "Timed out waiting for segment-cache maintenance to finish.";
    return false;
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

std::string Watermarker::getDefaultSegmentCacheDirectory() {
    try {
        return fromFsPath(segmentCacheRoot());
    } catch (...) {
        return {};
    }
}

SegmentCacheStats Watermarker::getSegmentCacheStats(const std::string& directory) {
    SegmentCacheStats stats;
    try {
        const std::filesystem::path root = segmentCacheRoot(directory);
        stats.directory = fromFsPath(root);
        scanCacheEntries(root, &stats);
    } catch (const std::exception& e) {
        stats.error = e.what();
    } catch (...) {
        stats.error = "Unknown error while reading the segment cache.";
    }
    return stats;
}

bool Watermarker::clearSegmentCache(const std::string& directory, std::string& error) {
    namespace fs = std::filesystem;
    error.clear();
    try {
        const fs::path root = segmentCacheRoot(directory);
        std::error_code ec;
        if (!fs::exists(root, ec)) {
            return !ec;
        }
        if (ec) {
            error = "Could not inspect segment cache: " + ec.message();
            return false;
        }

        pruneSegmentCacheNoThrow(root, 0, 0);

        const fs::path clearLockPath = root / kCacheClearLockName;
        if (!fs::create_directory(clearLockPath, ec)) {
            error = ec
                ? "Could not lock the segment cache for clearing: " + ec.message()
                : "The segment cache is already being cleared.";
            return false;
        }
        setCacheDirectoryPermissions(clearLockPath);
        ScopedPathCleanup clearLockCleanup(clearLockPath);

        for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            const std::string name = fromFsPath(it->path().filename());
            if (isCacheLockName(name)) {
                error = "The segment cache is currently in use by a watermark job.";
                return false;
            }
        }
        if (ec) {
            error = "Could not inspect segment cache: " + ec.message();
            return false;
        }

        std::vector<fs::path> cachePaths;
        for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            const std::string name = fromFsPath(it->path().filename());
            if (isCacheKeyName(name) && looksLikeSegmentCacheEntry(it->path())) {
                cachePaths.push_back(it->path());
            }
        }
        if (ec) {
            error = "Could not inspect segment cache entries: " + ec.message();
            return false;
        }

        // A custom cache path may also contain user files. Delete only the
        // directories that match MegaCustom's cache-key format and retain the root.
        for (const fs::path& cachePath : cachePaths) {
            fs::remove_all(cachePath, ec);
            if (ec) {
                error = "Could not clear segment cache entry: " + ec.message();
                return false;
            }
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
    } catch (...) {
        error = "Unknown error while clearing the segment cache.";
    }
    return false;
}

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

    // Resolve PATH with the wide Windows API so Unicode install paths survive.
    const std::string pathExecutable = findWindowsExecutableOnPath(L"ffmpeg.exe");
    if (!pathExecutable.empty()) {
        return pathExecutable;
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

    // Resolve PATH with wide APIs rather than parsing localized shell output.
    std::string systemPython = findWindowsExecutableOnPath(L"python.exe");
    if (systemPython.empty()) {
        systemPython = findWindowsExecutableOnPath(L"python3.exe");
    }
    if (!systemPython.empty()) {
        return systemPython;
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
    const std::string pythonPath = getPythonPath();
    if (pythonPath.empty()) {
        return false;
    }

    std::string output;
    std::string error;
#ifdef _WIN32
    const int exitCode = runWindowsProcessCapture(
#else
    const int exitCode = runPosixProcessCapture(
#endif
        {pythonPath, "-c", "import reportlab; import PyPDF2"},
        [&output](const char* data, size_t size) { output.append(data, size); },
        error);
    return exitCode == 0;
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
        if (fileExists(arialPath)) {
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
           << "expansion=none:"
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
               << "expansion=none:"
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
                                                          const std::string& output,
                                                          std::string* filterScriptPath) const {
    std::vector<std::string> cmd;

    std::string ffmpegPath = getFFmpegPath();
    cmd.push_back(ffmpegPath.empty() ? "ffmpeg" : ffmpegPath);
    cmd.push_back(m_config.overwrite ? "-y" : "-n");
    cmd.push_back("-i");
    cmd.push_back(input);

#ifdef _WIN32
    // On Windows, write the filter to a temp file and use -filter_script:v
    // to avoid filter-parser edge cases with drive letters. Each process gets
    // a unique file so concurrent jobs cannot overwrite another job's filter.
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path tempDir = fs::temp_directory_path(ec);
        if (ec) {
            tempDir = fs::current_path(ec);
        }
        const fs::path scriptPath = tempDir / toFsPath("megacustom_vf_" + uniqueWorkToken() + ".txt");
        std::ofstream file(scriptPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw std::runtime_error("Could not create temporary FFmpeg filter script.");
        }
        file << buildFFmpegFilter();
        file.close();
        if (!file) {
            throw std::runtime_error("Could not write temporary FFmpeg filter script.");
        }
        if (filterScriptPath) {
            *filterScriptPath = fromFsPath(scriptPath);
        }
        cmd.push_back("-filter_script:v");
        cmd.push_back(fromFsPath(scriptPath));
    }
#else
    (void)filterScriptPath;
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
        stderr_output,
        &m_cancelled);
#else
    int exitCode = runPosixProcessCapture(
        args,
        [&stdout_output](const char* data, size_t size) {
            stdout_output.append(data, size);
        },
        stderr_output,
        &m_cancelled);
#endif
    if (exitCode != 0 && stdout_output.empty() && !stderr_output.empty()) {
        stdout_output = stderr_output;
    }
    return exitCode;
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
#ifdef _WIN32
    int exitCode = runWindowsProcessCapture(
#else
    int exitCode = runPosixProcessCapture(
#endif
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
        processError,
        &m_cancelled);
    flushLine();

    if (exitCode != 0 && output.empty() && !processError.empty()) {
        output = processError;
    }

    return exitCode;
}

WatermarkResult Watermarker::executeSegmentedFFmpeg(const std::string& input,
                                                    const std::string& output) {
    WatermarkResult result;
    result.inputFile = input;
    result.outputFile = output;
    result.processingMode = "fast_segment_attempt";

    auto startTime = std::chrono::steady_clock::now();
    auto fail = [&](const std::string& reason) {
        result.error = reason;
        result.diagnostic = reason;
        LogManager::instance().log(LogLevel::Warning, LogCategory::Watermark,
            "fast_segmented_watermark_skip", reason, input);
        return result;
    };

    if (!isFFmpegAvailable()) {
        return fail("Fast segmented encode skipped: FFmpeg is not available.");
    }
    if (m_config.intervalSeconds <= 0 || m_config.durationSeconds <= 0) {
        return fail("Fast segmented encode skipped: interval and duration must be positive.");
    }

    namespace fs = std::filesystem;
    fs::path inputFs;
    fs::path outputFs;
    std::string pathError;
    if (!tryToFsPath(input, inputFs, &pathError)) {
        return fail("Fast segmented encode skipped: " + pathError);
    }
    if (!tryToFsPath(output, outputFs, &pathError)) {
        return fail("Fast segmented encode skipped: " + pathError);
    }
    if (!pathExistsNoThrow(inputFs, &pathError)) {
        return fail("Fast segmented encode skipped: input file not found.");
    }

    const std::string ext = lowerAscii(inputFs.extension().string());
    if (ext != ".mp4") {
        return fail("Fast segmented encode skipped: only MP4/H.264 files are enabled in this first version.");
    }

    if (!outputFs.parent_path().empty()) {
        createDirectoriesOrThrow(outputFs.parent_path());
    }
    const bool outputExistedBefore = pathExistsNoThrow(outputFs);
    if (outputExistedBefore && !m_config.overwrite) {
        return fail("Fast segmented encode skipped: output exists and overwrite is disabled.");
    }
    std::error_code equivalentError;
    if (fs::equivalent(inputFs, outputFs, equivalentError) && !equivalentError) {
        return fail("Fast segmented encode skipped: input and output paths must be different.");
    }

    int64_t inputSize = 0;
    if (!fileSizeNoThrow(inputFs, inputSize, &pathError)) {
        return fail("Fast segmented encode skipped: " + pathError);
    }
    if (inputSize <= 0) {
        return fail("Fast segmented encode skipped: input video is empty.");
    }
    result.inputSizeBytes = inputSize;

    std::error_code timeError;
    const auto mtime = fs::last_write_time(inputFs, timeError);
    if (timeError) {
        return fail("Fast segmented encode skipped: source modified time could not be read.");
    }
    std::error_code canonicalError;
    const fs::path canonicalInput = fs::weakly_canonical(inputFs, canonicalError);
    const std::string sourceIdentity = fromFsPath(canonicalError ? inputFs : canonicalInput);
    const std::string sourceIdentityHash = stableHashHex({sourceIdentity});
    const std::string sourceMtime = std::to_string(mtime.time_since_epoch().count());
    const std::string sourceFingerprint = quickSourceFingerprint(inputFs, inputSize);
    if (sourceFingerprint.empty()) {
        return fail("Fast segmented encode skipped: source fingerprint could not be read.");
    }

    const std::string cacheHash = stableHashHex({
        sourceIdentity,
        std::to_string(inputSize),
        sourceMtime,
        sourceFingerprint,
        std::to_string(m_config.intervalSeconds),
        std::to_string(m_config.durationSeconds),
        "fast_segment_v3"
    });
    const fs::path cacheRoot = segmentCacheRoot(m_config.segmentCacheDirectory);
    const fs::path outputDirectory = outputFs.parent_path();
    fs::path outputTreeRoot = outputDirectory;
    if (!m_config.segmentCacheOutputRoot.empty()
        && !tryToFsPath(m_config.segmentCacheOutputRoot, outputTreeRoot, &pathError)) {
        return fail("Fast segmented encode skipped: " + pathError);
    }
    if (pathIsWithin(cacheRoot, outputTreeRoot)
        || pathIsWithin(outputTreeRoot, cacheRoot)) {
        return fail("Fast segmented encode skipped: segment cache folder must be separate from the output tree.");
    }
    if (!m_segmentCacheMaintenanceDone.load()) {
        std::lock_guard<std::mutex> maintenanceLock(m_segmentCacheMaintenanceMutex);
        if (!m_segmentCacheMaintenanceDone.load()) {
            pruneSegmentCacheNoThrow(
                cacheRoot,
                std::max<int64_t>(0, m_config.segmentCacheMaxBytes),
                std::max(0, m_config.segmentCacheMaxAgeDays));
            m_segmentCacheMaintenanceDone.store(true);
        }
    }
    createDirectoriesOrThrow(cacheRoot);
    if (m_config.segmentCacheDirectory.empty()) {
        setCacheDirectoryPermissions(cacheRoot);
    }

    const fs::path cacheDir = cacheRoot / cacheHash;
    const fs::path readyPath = cacheDir / "source_segments.ready";
    const fs::path keyframeMapPath = cacheDir / "keyframes.txt";
    auto segmentPathFor = [&](size_t index) {
        std::ostringstream name;
        name << "src_seg_" << std::setw(5) << std::setfill('0') << index << ".mp4";
        return cacheDir / name.str();
    };

    const double duration = getVideoDuration(input);
    result.sourceDurationSeconds = duration;
    if (!(duration > 0.0) || !std::isfinite(duration)) {
        return fail("Fast segmented encode skipped: source duration could not be read.");
    }

    const std::string resolvedFFmpegPath = getFFmpegPath();
    const std::string ffmpegPath = resolvedFFmpegPath.empty() ? "ffmpeg" : resolvedFFmpegPath;
    const std::string ffprobePath = ffprobePathFromFFmpegPath(ffmpegPath);

    auto runProbe = [&](const std::vector<std::string>& args, std::string& out) {
        std::string err;
        const int code = runProcess(args, out, err);
        if (code != 0 && out.empty()) {
            out = err;
        }
        return code;
    };

    SegmentProbe probe;
    probe.duration = duration;

    bool reusedCompatibilityPlan = false;
    if (pathExistsNoThrow(readyPath)) {
        std::ifstream readyFile(readyPath, std::ios::binary);
        std::ostringstream readyBuffer;
        readyBuffer << readyFile.rdbuf();
        const auto readyValues = parseKeyValueProbe(readyBuffer.str());
        const auto readyValue = [&](const std::string& key) {
            auto it = readyValues.find(key);
            return it == readyValues.end() ? std::string() : it->second;
        };
        const double cachedDuration = std::strtod(readyValue("duration").c_str(), nullptr);
        const double durationTolerance = std::max(0.05, duration * 0.0001);
        reusedCompatibilityPlan = readyValue("version") == "3"
            && readyValue("compatibility") == "h264_yuv420p_aac_v1"
            && readyValue("source_identity") == sourceIdentityHash
            && readyValue("source_size") == std::to_string(inputSize)
            && readyValue("source_mtime") == sourceMtime
            && readyValue("fingerprint") == sourceFingerprint
            && std::isfinite(cachedDuration)
            && std::fabs(cachedDuration - duration) <= durationTolerance
            && (readyValue("has_audio") == "0" || readyValue("has_audio") == "1");
        if (reusedCompatibilityPlan) {
            probe.hasAudio = readyValue("has_audio") == "1";
        }
    }

    int probeCode = 0;
    if (!reusedCompatibilityPlan) {
        std::string videoProbeOutput;
        probeCode = runProbe({
            ffprobePath,
            "-v", "error",
            "-select_streams", "v:0",
            "-show_entries",
            "stream=codec_name,width,height,pix_fmt,avg_frame_rate,r_frame_rate,"
            "time_base,field_order,color_transfer,color_primaries,closed_captions",
            "-of", "default=noprint_wrappers=1",
            input
        }, videoProbeOutput);
        if (probeCode != 0 || videoProbeOutput.empty()) {
            return fail("Fast segmented encode skipped: ffprobe could not read the video stream.");
        }

        const auto videoValues = parseKeyValueProbe(videoProbeOutput);
        auto valueOrEmpty = [](const std::map<std::string, std::string>& values, const std::string& key) {
            auto it = values.find(key);
            return it == values.end() ? std::string() : it->second;
        };

        probe.videoCodec = lowerAscii(valueOrEmpty(videoValues, "codec_name"));
        probe.pixelFormat = lowerAscii(valueOrEmpty(videoValues, "pix_fmt"));
        probe.avgFrameRate = valueOrEmpty(videoValues, "avg_frame_rate");
        probe.realFrameRate = valueOrEmpty(videoValues, "r_frame_rate");
        probe.timeBase = valueOrEmpty(videoValues, "time_base");
        probe.fieldOrder = lowerAscii(valueOrEmpty(videoValues, "field_order"));
        probe.colorTransfer = lowerAscii(valueOrEmpty(videoValues, "color_transfer"));
        probe.colorPrimaries = lowerAscii(valueOrEmpty(videoValues, "color_primaries"));
        probe.hasClosedCaptions = std::atoi(valueOrEmpty(videoValues, "closed_captions").c_str()) != 0;
        probe.width = std::atoi(valueOrEmpty(videoValues, "width").c_str());
        probe.height = std::atoi(valueOrEmpty(videoValues, "height").c_str());
        probe.frameRate = parseRational(probe.avgFrameRate);
        probe.realFrameRateValue = parseRational(probe.realFrameRate);
        if (probe.frameRate <= 0.0) probe.frameRate = probe.realFrameRateValue;

        std::string streamProbeOutput;
        probeCode = runProbe({
            ffprobePath,
            "-v", "error",
            "-show_entries", "stream=codec_type,codec_name",
            "-of", "csv=p=0",
            input
        }, streamProbeOutput);
        if (probeCode != 0 || streamProbeOutput.empty()) {
            return fail("Fast segmented encode skipped: ffprobe could not read stream layout.");
        }

        for (const std::string& rawLine : splitLines(streamProbeOutput)) {
            const std::string line = trimCopy(rawLine);
            if (line.empty()) {
                continue;
            }
            const auto comma = line.find(',');
            const std::string codec = lowerAscii(comma == std::string::npos ? line : line.substr(0, comma));
            const std::string type = lowerAscii(comma == std::string::npos ? "" : line.substr(comma + 1));
            if (type == "video") {
                ++probe.videoStreams;
            } else if (type == "audio") {
                ++probe.audioStreams;
                if (probe.audioCodec.empty()) {
                    probe.audioCodec = codec;
                }
            } else {
                ++probe.otherStreams;
            }
        }
        probe.hasAudio = probe.audioStreams > 0;

        if (probe.videoCodec != "h264") {
            return fail("Fast segmented encode skipped: video codec is " + probe.videoCodec + ", expected h264.");
        }
        if (probe.videoStreams != 1) {
            return fail("Fast segmented encode skipped: expected exactly one video stream.");
        }
        if (probe.audioStreams > 1) {
            return fail("Fast segmented encode skipped: multiple audio streams are not supported yet.");
        }
        if (probe.hasAudio && probe.audioCodec != "aac") {
            return fail("Fast segmented encode skipped: audio codec is " + probe.audioCodec + ", expected aac.");
        }
        if (probe.otherStreams != 0) {
            return fail("Fast segmented encode skipped: subtitle/data/attachment streams are not supported yet.");
        }
        if (probe.width <= 0 || probe.height <= 0 || probe.frameRate <= 0.0) {
            return fail("Fast segmented encode skipped: video dimensions or frame rate could not be read.");
        }
        if (probe.pixelFormat != "yuv420p") {
            return fail("Fast segmented encode skipped: pixel format is " + probe.pixelFormat +
                        ", expected yuv420p for safe stream-copy stitching.");
        }
        if (probe.realFrameRateValue > 0.0
            && std::fabs(probe.realFrameRateValue - probe.frameRate) > 0.01) {
            return fail("Fast segmented encode skipped: variable frame-rate video is not enabled yet.");
        }
        if (!probe.fieldOrder.empty() && probe.fieldOrder != "unknown"
            && probe.fieldOrder != "progressive") {
            return fail("Fast segmented encode skipped: interlaced video requires standard encoding.");
        }
        if (probe.colorTransfer == "smpte2084" || probe.colorTransfer == "arib-std-b67"
            || probe.colorPrimaries == "bt2020") {
            return fail("Fast segmented encode skipped: HDR/WCG video requires standard encoding.");
        }
        if (probe.hasClosedCaptions) {
            return fail("Fast segmented encode skipped: embedded closed captions must be preserved by standard encoding.");
        }

        std::string rotationOutput;
        probeCode = runProbe({
            ffprobePath,
            "-v", "error",
            "-select_streams", "v:0",
            "-show_entries", "stream_tags=rotate:stream_side_data=rotation",
            "-of", "default=noprint_wrappers=1:nokey=1",
            input
        }, rotationOutput);
        if (probeCode != 0) {
            return fail("Fast segmented encode skipped: rotation metadata could not be checked.");
        }
        for (const std::string& rawLine : splitLines(rotationOutput)) {
            const std::string line = trimCopy(rawLine);
            if (!line.empty()) {
                const double rotation = std::strtod(line.c_str(), nullptr);
                if (std::isfinite(rotation) && std::fabs(rotation) > 0.01) {
                    probe.rotation = rotation;
                    return fail("Fast segmented encode skipped: rotated video requires standard encoding.");
                }
            }
        }
    }

    if (probe.hasAudio && !m_config.copyAudio) {
        return fail("Fast segmented encode skipped: audio re-encoding requires the standard full encode path.");
    }

    bool reusedKeyframeMap = false;
    if (pathExistsNoThrow(readyPath) && pathExistsNoThrow(keyframeMapPath)) {
        std::ifstream readyFile(readyPath, std::ios::binary);
        std::ostringstream readyBuffer;
        readyBuffer << readyFile.rdbuf();
        const auto readyValues = parseKeyValueProbe(readyBuffer.str());
        auto readyValue = [&](const std::string& key) {
            auto it = readyValues.find(key);
            return it == readyValues.end() ? std::string() : it->second;
        };
        const size_t expectedKeyframes = static_cast<size_t>(
            std::strtoull(readyValue("keyframes").c_str(), nullptr, 10));
        if (readyValue("version") == "3"
            && readyValue("source_identity") == sourceIdentityHash
            && readyValue("source_size") == std::to_string(inputSize)
            && readyValue("source_mtime") == sourceMtime
            && readyValue("fingerprint") == sourceFingerprint
            && expectedKeyframes > 0) {
            std::ifstream keyframeMap(keyframeMapPath);
            std::string line;
            while (std::getline(keyframeMap, line)) {
                const double t = std::strtod(trimCopy(line).c_str(), nullptr);
                if (std::isfinite(t) && t >= 0.0 && t <= duration + 0.5
                    && (probe.keyframes.empty() || t > probe.keyframes.back() + 0.001)) {
                    probe.keyframes.push_back(t);
                }
            }
            reusedKeyframeMap = probe.keyframes.size() == expectedKeyframes;
        }
    }

    if (!reusedKeyframeMap) {
        probe.keyframes.clear();
        std::string keyframeOutput;
        probeCode = runProbe({
            ffprobePath,
            "-v", "error",
            "-select_streams", "v:0",
            "-skip_frame", "nokey",
            "-show_entries", "frame=best_effort_timestamp_time",
            "-of", "csv=p=0",
            input
        }, keyframeOutput);
        if (probeCode != 0 || keyframeOutput.empty()) {
            return fail("Fast segmented encode skipped: keyframe map could not be read.");
        }

        for (const std::string& rawLine : splitLines(keyframeOutput)) {
            const std::string line = trimCopy(rawLine);
            if (line.empty()) {
                continue;
            }
            const double t = std::strtod(line.c_str(), nullptr);
            if (std::isfinite(t) && t >= 0.0 && t <= duration + 0.5) {
                if (probe.keyframes.empty() || std::fabs(probe.keyframes.back() - t) > 0.001) {
                    probe.keyframes.push_back(t);
                }
            }
        }
    }
    if (probe.keyframes.empty() || probe.keyframes.front() > 0.5) {
        probe.keyframes.insert(probe.keyframes.begin(), 0.0);
    }
    std::sort(probe.keyframes.begin(), probe.keyframes.end());

    std::vector<EncodedSegmentWindow> encodedWindows;
    const int intervalSeconds = m_config.intervalSeconds;
    const double visibleDuration = static_cast<double>(m_config.durationSeconds);
    int windowIndex = 0;
    for (double visibleStart = 0.0; visibleStart < duration - 0.001; visibleStart += intervalSeconds) {
        const double visibleEnd = std::min(visibleStart + visibleDuration, duration);
        if (visibleEnd <= visibleStart + 0.001) {
            continue;
        }

        const double segmentStart = previousKeyframeAtOrBefore(probe.keyframes, visibleStart);
        const double segmentEnd = nextKeyframeAtOrAfter(probe.keyframes, visibleEnd, duration);
        if (segmentEnd <= segmentStart + 0.001) {
            return fail("Fast segmented encode skipped: invalid keyframe window around watermark time.");
        }

        VisibleWatermarkWindow visible;
        visible.sourceStart = visibleStart;
        visible.sourceEnd = visibleEnd;
        visible.localStart = visibleStart - segmentStart;
        visible.localEnd = visibleEnd - segmentStart;
        const std::string positionSeed = fromFsPath(inputFs) + "|" + std::to_string(windowIndex) + "|" +
            formatSeconds(visibleStart) + "|" + std::to_string(intervalSeconds) + "|" +
            std::to_string(m_config.durationSeconds);
        visible.xFraction = deterministicFraction(positionSeed + "|x", 0.02, 0.98);
        visible.yFraction = deterministicFraction(positionSeed + "|y", 0.02, 0.92);

        if (!encodedWindows.empty() && segmentStart <= encodedWindows.back().end + 0.001) {
            EncodedSegmentWindow& merged = encodedWindows.back();
            const double mergedStart = merged.start;
            merged.end = std::max(merged.end, segmentEnd);
            visible.localStart = visible.sourceStart - mergedStart;
            visible.localEnd = visible.sourceEnd - mergedStart;
            merged.visibleWindows.push_back(visible);
        } else {
            EncodedSegmentWindow encoded;
            encoded.start = segmentStart;
            encoded.end = segmentEnd;
            encoded.visibleWindows.push_back(visible);
            encodedWindows.push_back(encoded);
        }
        ++windowIndex;
    }

    if (encodedWindows.empty()) {
        return fail("Fast segmented encode skipped: no watermark windows were planned.");
    }

    double encodedDuration = 0.0;
    for (const auto& window : encodedWindows) {
        encodedDuration += std::max(0.0, window.end - window.start);
    }
    if (encodedDuration / duration > 0.45) {
        return fail("Fast segmented encode skipped: padded watermark windows cover too much of the video.");
    }

    std::set<double> boundarySet;
    for (const auto& window : encodedWindows) {
        if (window.start > 0.001 && window.start < duration - 0.001) {
            boundarySet.insert(window.start);
        }
        if (window.end > 0.001 && window.end < duration - 0.001) {
            boundarySet.insert(window.end);
        }
    }
    if (boundarySet.empty()) {
        return fail("Fast segmented encode skipped: source is too short for useful segmentation.");
    }

    std::vector<double> boundaries(boundarySet.begin(), boundarySet.end());
    std::vector<double> points;
    points.push_back(0.0);
    points.insert(points.end(), boundaries.begin(), boundaries.end());
    points.push_back(duration);

    std::vector<PlannedPiece> pieces;
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const double pieceStart = points[i];
        const double pieceEnd = points[i + 1];
        if (pieceEnd <= pieceStart + 0.001) {
            continue;
        }

        PlannedPiece piece;
        piece.start = pieceStart;
        piece.end = pieceEnd;
        for (size_t w = 0; w < encodedWindows.size(); ++w) {
            if (std::fabs(pieceStart - encodedWindows[w].start) <= 0.01 &&
                std::fabs(pieceEnd - encodedWindows[w].end) <= 0.01) {
                piece.watermark = true;
                piece.watermarkIndex = static_cast<int>(w);
                break;
            }
        }
        pieces.push_back(piece);
    }
    if (pieces.empty()) {
        return fail("Fast segmented encode skipped: no segment pieces were planned.");
    }

    auto cacheIsReady = [&]() {
        if (!pathExistsNoThrow(readyPath)) {
            return false;
        }
        std::ifstream readyFile(readyPath, std::ios::binary);
        if (!readyFile) {
            return false;
        }
        std::ostringstream readyBuffer;
        readyBuffer << readyFile.rdbuf();
        const auto readyValues = parseKeyValueProbe(readyBuffer.str());
        const auto readyValue = [&](const std::string& key) {
            auto it = readyValues.find(key);
            return it == readyValues.end() ? std::string() : it->second;
        };
        if (readyValue("version") != "3"
            || readyValue("pieces") != std::to_string(pieces.size())
            || readyValue("source_identity") != sourceIdentityHash
            || readyValue("source_size") != std::to_string(inputSize)
            || readyValue("source_mtime") != sourceMtime
            || readyValue("fingerprint") != sourceFingerprint
            || readyValue("keyframes") != std::to_string(probe.keyframes.size())
            || !pathExistsNoThrow(keyframeMapPath)) {
            return false;
        }
        for (size_t i = 0; i < pieces.size(); ++i) {
            int64_t segmentSize = 0;
            if (!fileSizeNoThrow(segmentPathFor(i), segmentSize) || segmentSize <= 0) {
                return false;
            }
            if (readyValue("segment_" + std::to_string(i) + "_size")
                != std::to_string(segmentSize)) {
                return false;
            }
        }
        if (pathExistsNoThrow(segmentPathFor(pieces.size()))) {
            return false;
        }
        return true;
    };

    fs::path usageLeasePath;
    std::string usageLeaseError;
    if (!acquireCacheUsageLease(
            cacheRoot, cacheHash, m_cancelled, usageLeasePath, usageLeaseError)) {
        return fail("Fast segmented encode skipped: " + usageLeaseError);
    }
    ScopedPathCleanup usageLeaseCleanup(usageLeasePath);

    bool cacheReady = cacheIsReady();
    bool cacheHit = cacheReady;

    if (!cacheReady) {
        CacheBuildLock cacheLock;
        std::string lockError;
        if (!cacheLock.acquire(cacheRoot / (cacheHash + ".lock"), cacheIsReady,
                               m_cancelled, lockError)) {
            return fail("Fast segmented encode skipped: " + lockError);
        }

        cacheReady = cacheIsReady();
        cacheHit = cacheReady;
        if (!cacheReady && !cacheLock.ownsLock()) {
            return fail("Fast segmented encode skipped: cache became unavailable after waiting for its builder.");
        }

        if (!cacheReady) {
            std::error_code removeError;
            fs::remove_all(cacheDir, removeError);
            if (removeError) {
                return fail("Fast segmented encode skipped: invalid cache entry could not be replaced: "
                            + removeError.message());
            }

            if (m_config.segmentCacheMaxBytes > 0
                && inputSize > m_config.segmentCacheMaxBytes) {
                return fail("Fast segmented encode skipped: this source is larger than the configured cache limit.");
            }
            if (m_config.segmentCacheMaxBytes > 0) {
                const int64_t targetBeforeBuild = std::max<int64_t>(
                    1, m_config.segmentCacheMaxBytes - inputSize);
                pruneSegmentCacheNoThrow(
                    cacheRoot, targetBeforeBuild, std::max(0, m_config.segmentCacheMaxAgeDays));
            }

            std::error_code spaceError;
            const fs::space_info cacheSpace = fs::space(cacheRoot, spaceError);
            constexpr uintmax_t kCacheReserveBytes = 512ULL * 1024ULL * 1024ULL;
            const uintmax_t requiredCacheBytes = static_cast<uintmax_t>(inputSize) + kCacheReserveBytes;
            if (!spaceError && cacheSpace.available < requiredCacheBytes) {
                return fail("Fast segmented encode skipped: cache drive has insufficient free space (needs about " +
                            std::to_string(requiredCacheBytes) + " bytes, has " +
                            std::to_string(cacheSpace.available) + ").");
            }

            createDirectoriesOrThrow(cacheDir);
            setCacheDirectoryPermissions(cacheDir);
            {
                std::ofstream marker(cacheDir / kCacheEntryMarkerName,
                                     std::ios::binary | std::ios::trunc);
                marker << "version=3\n";
                marker.close();
                if (!marker) {
                    return fail("Fast segmented encode failed: cache entry marker could not be written.");
                }
            }

            std::ostringstream times;
            for (size_t i = 0; i < boundaries.size(); ++i) {
                if (i > 0) {
                    times << ",";
                }
                times << formatSeconds(boundaries[i]);
            }

            const fs::path pattern = cacheDir / "src_seg_%05d.mp4";
            std::vector<std::string> segmentCmd = {
                ffmpegPath,
                "-y",
                "-i", input,
                "-map", "0",
                "-c", "copy",
                "-f", "segment",
                "-segment_times", times.str(),
                "-segment_time_delta", "0.05",
                "-reset_timestamps", "1",
                fromFsPath(pattern)
            };

            reportProgress(input, 1, 1, 3.0, "building shared segment cache");
            std::string segmentOutput;
            const int segmentExit = runFFmpegWithProgress(segmentCmd, input, duration, segmentOutput);
            if (segmentExit != 0) {
                return fail("Fast segmented encode failed while building shared source cache: " +
                            summarizeProcessOutput(segmentOutput));
            }

            for (size_t i = 0; i < pieces.size(); ++i) {
                int64_t segmentSize = 0;
                if (!fileSizeNoThrow(segmentPathFor(i), segmentSize) || segmentSize <= 0) {
                    return fail("Fast segmented encode failed: shared source cache is incomplete.");
                }
            }
            if (pathExistsNoThrow(segmentPathFor(pieces.size()))) {
                return fail("Fast segmented encode failed: shared source cache produced an unexpected extra segment.");
            }

            const fs::path keyframeTemp = cacheDir / ("keyframes.txt." + uniqueWorkToken());
            {
                std::ofstream keyframeMap(keyframeTemp, std::ios::binary | std::ios::trunc);
                for (double keyframe : probe.keyframes) {
                    keyframeMap << formatSeconds(keyframe) << "\n";
                }
                keyframeMap.close();
                if (!keyframeMap) {
                    return fail("Fast segmented encode failed: keyframe map could not be cached.");
                }
            }
            removeError.clear();
            fs::rename(keyframeTemp, keyframeMapPath, removeError);
            if (removeError) {
                return fail("Fast segmented encode failed: keyframe map could not be committed: " +
                            removeError.message());
            }

            const fs::path readyTemp = cacheDir / ("source_segments.ready." + uniqueWorkToken());
            {
                std::ofstream ready(readyTemp, std::ios::binary | std::ios::trunc);
                ready << "version=3\n";
                ready << "pieces=" << pieces.size() << "\n";
                ready << "duration=" << formatSeconds(duration) << "\n";
                ready << "source_identity=" << sourceIdentityHash << "\n";
                ready << "source_size=" << inputSize << "\n";
                ready << "source_mtime=" << sourceMtime << "\n";
                ready << "fingerprint=" << sourceFingerprint << "\n";
                ready << "compatibility=h264_yuv420p_aac_v1\n";
                ready << "has_audio=" << (probe.hasAudio ? 1 : 0) << "\n";
                ready << "keyframes=" << probe.keyframes.size() << "\n";
                int64_t cachedSegmentBytes = 0;
                for (size_t i = 0; i < pieces.size(); ++i) {
                    int64_t segmentSize = 0;
                    fileSizeNoThrow(segmentPathFor(i), segmentSize);
                    cachedSegmentBytes += std::max<int64_t>(0, segmentSize);
                    ready << "segment_" << i << "_size=" << segmentSize << "\n";
                }
                ready << "cache_bytes=" << cachedSegmentBytes << "\n";
                ready.close();
                if (!ready) {
                    return fail("Fast segmented encode failed: cache-ready marker could not be written.");
                }
            }
            removeError.clear();
            fs::rename(readyTemp, readyPath, removeError);
            if (removeError) {
                return fail("Fast segmented encode failed: cache-ready marker could not be committed: " +
                            removeError.message());
            }
            cacheReady = true;
        }
        cacheLock.release();
    }

    if (!cacheReady) {
        return fail("Fast segmented encode skipped: shared segment cache is not ready.");
    }
    {
        std::error_code touchError;
        fs::last_write_time(readyPath, fs::file_time_type::clock::now(), touchError);
    }
    if (cacheHit) {
        reportProgress(input, 1, 1, 8.0, "reusing shared segment cache");
    }

    for (size_t i = 0; i < pieces.size(); ++i) {
        pieces[i].sourceSegmentPath = fromFsPath(segmentPathFor(i));
    }

    const fs::path memberDir = cacheDir / ("work_" + uniqueWorkToken());
    createDirectoriesOrThrow(memberDir);
    setCacheDirectoryPermissions(memberDir);
    ScopedPathCleanup memberCleanup(memberDir);

    int watermarkPieceCount = 0;
    for (const auto& piece : pieces) {
        if (piece.watermark) {
            ++watermarkPieceCount;
        }
    }

    int watermarkPieceIndex = 0;
    for (PlannedPiece& piece : pieces) {
        if (!piece.watermark) {
            continue;
        }
        if (m_cancelled.load()) {
            return fail("Fast segmented encode cancelled before the next watermark segment.");
        }

        const EncodedSegmentWindow& encoded = encodedWindows[static_cast<size_t>(piece.watermarkIndex)];
        std::ostringstream name;
        name << "wm_" << std::setw(5) << std::setfill('0') << watermarkPieceIndex << ".mp4";
        const fs::path memberSegment = memberDir / name.str();
        piece.memberSegmentPath = fromFsPath(memberSegment);

        const std::string filter = buildScheduledDrawtextFilter(m_config, encoded.visibleWindows);
        if (filter.empty()) {
            return fail("Fast segmented encode skipped: scheduled watermark filter was empty.");
        }

        std::vector<std::string> encodeCmd = {
            ffmpegPath,
            "-y",
            "-i", piece.sourceSegmentPath,
            "-map", "0",
            "-vf", filter,
            "-c:v", "libx264",
            "-crf", std::to_string(m_config.crf),
            "-preset", m_config.preset,
            "-pix_fmt", "yuv420p"
        };
        if (probe.hasAudio) {
            encodeCmd.push_back("-c:a");
            encodeCmd.push_back(m_config.copyAudio ? "copy" : "aac");
        }
        encodeCmd.push_back("-movflags");
        encodeCmd.push_back("+faststart");
        encodeCmd.push_back(piece.memberSegmentPath);

        const double basePercent = 10.0 + (70.0 * watermarkPieceIndex / std::max(1, watermarkPieceCount));
        reportProgress(input, 1, 1, basePercent, "encoding watermark segment");
        std::string encodeOutput;
        const int encodeExit = runFFmpegWithProgress(
            encodeCmd, input, std::max(0.1, piece.end - piece.start), encodeOutput);
        if (encodeExit != 0 || !pathExistsNoThrow(memberSegment)) {
            return fail("Fast segmented encode failed while encoding a watermark segment: " +
                        summarizeProcessOutput(encodeOutput));
        }
        ++watermarkPieceIndex;
    }

    const fs::path concatPath = memberDir / "concat.txt";
    {
        std::ofstream concat(concatPath);
        if (!concat) {
            return fail("Fast segmented encode failed: could not create concat manifest.");
        }
        for (const PlannedPiece& piece : pieces) {
            const std::string path = piece.watermark ? piece.memberSegmentPath : piece.sourceSegmentPath;
            concat << "file " << concatManifestEscape(path) << "\n";
        }
    }

    std::vector<std::string> concatCmd = {
        ffmpegPath,
        m_config.overwrite ? "-y" : "-n",
        "-f", "concat",
        "-safe", "0",
        "-i", fromFsPath(concatPath),
        "-i", input,
        "-map", "0",
        "-map_metadata", "1",
        "-map_chapters", "1",
        "-c", "copy"
    };

    if (m_config.embedMetadata) {
        if (!m_config.metadataTitle.empty()) {
            concatCmd.push_back("-metadata");
            concatCmd.push_back("title=" + m_config.metadataTitle);
        }
        if (!m_config.metadataAuthor.empty()) {
            concatCmd.push_back("-metadata");
            concatCmd.push_back("artist=" + m_config.metadataAuthor);
        }
        if (!m_config.metadataComment.empty()) {
            concatCmd.push_back("-metadata");
            concatCmd.push_back("comment=" + m_config.metadataComment);
        }
        if (!m_config.metadataKeywords.empty()) {
            concatCmd.push_back("-metadata");
            concatCmd.push_back("description=" + m_config.metadataKeywords);
        }
    }
    concatCmd.push_back("-movflags");
    concatCmd.push_back("+faststart");
    concatCmd.push_back(output);

    reportProgress(input, 1, 1, 88.0, "stitching segments");
    std::string concatOutput;
    const int concatExit = runFFmpegWithProgress(concatCmd, input, duration, concatOutput);
    if (concatExit != 0 || !pathExistsNoThrow(outputFs)) {
        if (!outputExistedBefore || m_config.overwrite) {
            std::error_code removeOutputError;
            fs::remove(outputFs, removeOutputError);
        }
        return fail("Fast segmented encode failed while stitching segments: " +
                    summarizeProcessOutput(concatOutput));
    }

    const double outputDuration = getVideoDuration(output);
    const double allowedDrift = std::max(0.25, duration * 0.001);
    if (!(outputDuration > 0.0) || std::fabs(outputDuration - duration) > allowedDrift) {
        if (!outputExistedBefore || m_config.overwrite) {
            std::error_code removeOutputError;
            fs::remove(outputFs, removeOutputError);
        }
        return fail("Fast segmented encode failed validation: output duration drift is " +
                    formatSeconds(std::fabs(outputDuration - duration)) + " seconds.");
    }

    // Decode a short sample across transition boundaries. This catches H.264
    // parameter or timestamp incompatibilities without decoding the full file.
    if (!boundaries.empty()) {
        const size_t maxSamples = 8;
        const size_t stride = std::max<size_t>(1, (boundaries.size() + maxSamples - 1) / maxSamples);
        for (size_t i = 0; i < boundaries.size(); i += stride) {
            if (m_cancelled.load()) {
                if (!outputExistedBefore || m_config.overwrite) {
                    std::error_code removeOutputError;
                    fs::remove(outputFs, removeOutputError);
                }
                return fail("Fast segmented encode cancelled during boundary validation.");
            }
            const double sampleStart = std::max(0.0, boundaries[i] - 0.20);
            std::string validationOutput;
            std::string validationError;
            const int validationExit = runProcess({
                ffmpegPath,
                "-v", "error",
                "-xerror",
                "-ss", formatSeconds(sampleStart),
                "-i", output,
                "-t", "0.50",
                "-map", "0:v:0",
                "-map", "0:a:0?",
                "-f", "null",
                "-"
            }, validationOutput, validationError);
            if (validationExit != 0) {
                if (!outputExistedBefore || m_config.overwrite) {
                    std::error_code removeOutputError;
                    fs::remove(outputFs, removeOutputError);
                }
                return fail("Fast segmented encode failed boundary validation near " +
                            formatSeconds(boundaries[i]) + "s: " +
                            summarizeProcessOutput(validationOutput + "\n" + validationError));
            }
        }
    }

    fs::path finalOutputFs;
    if (!tryToFsPath(output, finalOutputFs, &pathError) || !pathExistsNoThrow(finalOutputFs, &pathError)) {
        return fail("Fast segmented encode failed validation: final output is missing.");
    }

    int64_t finalOutputSize = 0;
    if (!fileSizeNoThrow(finalOutputFs, finalOutputSize, &pathError) || finalOutputSize <= 0) {
        if (!outputExistedBefore || m_config.overwrite) {
            std::error_code removeOutputError;
            fs::remove(finalOutputFs, removeOutputError);
        }
        return fail("Fast segmented encode failed validation: final output is empty.");
    }

    result.success = true;
    result.outputSizeBytes = finalOutputSize;
    result.processingMode = cacheHit ? "fast_segment_cache_hit" : "fast_segment_cache_build";
    result.segmentCacheHit = cacheHit;
    result.encodedDurationSeconds = encodedDuration;
    const std::string fastDetail = cacheHit
        ? (reusedCompatibilityPlan && reusedKeyframeMap
            ? "Reused cached clean segments and validated source plan; encoded "
            : (reusedKeyframeMap
                ? "Reused cached clean segments and keyframe plan; encoded "
                : "Reused cached clean segments after refreshing the keyframe plan; encoded "))
        : "Built shared clean-segment cache; encoded ";
    result.diagnostic = fastDetail
        + formatSeconds(encodedDuration) + "s of " + formatSeconds(duration) + "s (" +
        formatSeconds((encodedDuration / duration) * 100.0) + "%). Cache: " + fromFsPath(cacheDir);
    auto endTime = std::chrono::steady_clock::now();
    result.processingTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime).count();

    reportProgress(input, 1, 1, 100.0, "complete");
    LogManager::instance().logWatermark("fast_segmented_video_complete",
        "Fast segmented video watermark complete: " + output + " (" +
            std::to_string(result.processingTimeMs) + "ms, encoded " +
            formatSeconds(encodedDuration) + "s of " + formatSeconds(duration) + "s)",
        input);

    // v1 stored clean chunks beside each member output. They are not reused
    // across members and should not remain mixed with deliverable content.
    const fs::path legacyCache = outputFs.parent_path() / "_megacustom_segment_cache";
    if (legacyCache != cacheRoot) {
        std::error_code legacyCleanupError;
        fs::remove_all(legacyCache, legacyCleanupError);
    }

    return result;
}

WatermarkResult Watermarker::executeFFmpeg(const std::string& input,
                                            const std::string& output) {
    WatermarkResult result;
    result.inputFile = input;
    result.outputFile = output;
    result.processingMode = "full_encode";

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
    if (result.inputSizeBytes <= 0) {
        result.error = "Input video is empty: " + input;
        LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
            "input_empty", result.error, input);
        return result;
    }

    fs::path outputFs;
    if (!tryToFsPath(output, outputFs, &pathError)) {
        result.error = pathError + ": " + output;
        return result;
    }
    if (!outputFs.parent_path().empty()) {
        createDirectoriesOrThrow(outputFs.parent_path());
    }
    const bool outputExistedBefore = pathExistsNoThrow(outputFs);
    if (outputExistedBefore && !m_config.overwrite) {
        result.error = "Output video exists and overwrite is disabled: " + output;
        return result;
    }
    std::error_code equivalentError;
    if (fs::equivalent(inputFs, outputFs, equivalentError) && !equivalentError) {
        result.error = "Input and output video paths must be different.";
        return result;
    }

    // Get video duration for progress tracking
    double duration = getVideoDuration(input);
    result.sourceDurationSeconds = duration;
    reportProgress(input, 1, 1, 0.0, "encoding");

    // Build and execute command with progress tracking
    std::string filterScriptPath;
    auto cmd = buildFFmpegCommand(input, output, &filterScriptPath);
    ScopedPathCleanup filterScriptCleanup(
        filterScriptPath.empty() ? fs::path() : toFsPath(filterScriptPath));

    std::string ffmpegOutput;
    int exitCode = runFFmpegWithProgress(cmd, input, duration, ffmpegOutput);

    auto endTime = std::chrono::steady_clock::now();
    result.processingTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime).count();

    pathError.clear();
    const bool outputExists = pathExistsNoThrow(outputFs, &pathError);
    int64_t outputSize = 0;
    const bool outputSizeValid = outputExists && fileSizeNoThrow(outputFs, outputSize);
    if (exitCode == 0 && outputExists && outputSizeValid && outputSize > 0) {
        result.success = true;
        result.outputSizeBytes = outputSize;
        result.encodedDurationSeconds = duration;
        reportProgress(input, 1, 1, 100.0, "complete");
        LogManager::instance().logWatermark("video_complete",
            "Video watermark complete: " + output + " (" + std::to_string(result.processingTimeMs) + "ms)", input);
    } else {
        if (exitCode == -2 || m_cancelled.load()) {
            result.processingMode = "cancelled";
            result.error = "Video watermark cancelled.";
        } else if (exitCode != 0) {
            result.error = "FFmpeg failed (exit code " + std::to_string(exitCode) + "): " +
                           summarizeProcessOutput(ffmpegOutput);
        } else {
            result.error = "FFmpeg succeeded but output file is missing or empty: " + output;
            if (!pathError.empty()) {
                result.error += " (" + pathError + ")";
            }
        }
        if (!outputExistedBefore || m_config.overwrite) {
            std::error_code removeError;
            fs::remove(outputFs, removeError);
        }
        reportProgress(input, 1, 1, 0.0, "error");
        if (result.processingMode == "cancelled") {
            LogManager::instance().log(LogLevel::Info, LogCategory::Watermark,
                "video_watermark_cancelled", "Video watermark cancelled: " + input, input);
        } else {
            LogManager::instance().log(LogLevel::Error, LogCategory::Watermark,
                "video_watermark_failed", "Video watermark failed: " + input + " - " + result.error, input);
        }
    }

    return result;
}

WatermarkResult Watermarker::executePdfScript(const std::string& input,
                                               const std::string& output) {
    WatermarkResult result;
    result.inputFile = input;
    result.outputFile = output;
    result.processingMode = "pdf_watermark";

    auto startTime = std::chrono::steady_clock::now();

    LogManager::instance().logWatermark("pdf_start", "Starting PDF watermark: " + input, input);

    const std::string pythonPath = getPythonPath();
    if (pythonPath.empty()) {
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
    if (result.inputSizeBytes <= 0) {
        result.error = "Input PDF is empty: " + input;
        return result;
    }

    fs::path outputFs;
    if (!tryToFsPath(output, outputFs, &pathError)) {
        result.error = pathError + ": " + output;
        return result;
    }
    createDirectoriesOrThrow(outputFs.parent_path());
    const bool outputExistedBefore = pathExistsNoThrow(outputFs);
    if (outputExistedBefore && !m_config.overwrite) {
        result.error = "Output PDF exists and overwrite is disabled: " + output;
        return result;
    }
    std::error_code equivalentError;
    if (fs::equivalent(inputFs, outputFs, equivalentError) && !equivalentError) {
        result.error = "Input and output PDF paths must be different.";
        return result;
    }

    // Log resolved script path for debugging
    fprintf(stderr, "PDF watermark script: %s\n", scriptPath.c_str());

    // Build command using resolved Python path
    std::vector<std::string> cmd = {
        pythonPath, scriptPath,
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
    cmd.push_back("--angle");
    cmd.push_back(std::to_string(m_config.pdfAngle));
    if (!m_config.primaryColor.empty()) {
        cmd.push_back("--color");
        cmd.push_back(m_config.primaryColor);
    }
    cmd.push_back("--coverage");
    cmd.push_back(std::to_string(m_config.pdfCoverage));

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

    pathError.clear();
    const bool outputExists = pathExistsNoThrow(outputFs, &pathError);
    if (exitCode == 0 && outputExists && isStructurallyCompletePdf(outputFs)) {
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
        result.diagnostic = "PDF output was created successfully; ignored post-write Python encoding warning.";
        LogManager::instance().log(LogLevel::Warning, LogCategory::Watermark,
            "pdf_post_write_encoding_warning",
            "PDF output exists and is structurally complete after Python encoding warning: " + output,
            input);
    } else {
        if (exitCode == -2 || m_cancelled.load()) {
            result.processingMode = "cancelled";
            result.error = "PDF watermark cancelled.";
        } else if (exitCode != 0) {
            result.error = "PDF watermarking failed (exit code " + std::to_string(exitCode) + "): " +
                summarizeProcessOutput(stdout_out + "\n" + stderr_out);
        } else {
            result.error = "PDF script succeeded but output file is missing or incomplete: " + output;
            if (!pathError.empty()) {
                result.error += " (" + pathError + ")";
            }
        }
        if (!outputExistedBefore || m_config.overwrite) {
            std::error_code removeError;
            fs::remove(outputFs, removeError);
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
        std::string fastFallbackReason;
        if (m_config.fastSegmentedEncode) {
            WatermarkResult fastResult;
            fastResult.inputFile = inputPath;
            fastResult.outputFile = outPath;
            fastResult.processingMode = "fast_segment_attempt";
            try {
                fastResult = executeSegmentedFFmpeg(inputPath, outPath);
            } catch (const std::exception& e) {
                fastResult.error = std::string("Fast segmented encode skipped after a cache or filesystem error: ")
                    + e.what();
                fastResult.diagnostic = fastResult.error;
            } catch (...) {
                fastResult.error = "Fast segmented encode skipped after an unknown cache or filesystem error.";
                fastResult.diagnostic = fastResult.error;
            }
            if (fastResult.success) {
                reportProgress(inputPath, 1, 1, 100.0, "complete");
                return fastResult;
            }
            fastFallbackReason = fastResult.error;

            if (m_cancelled.load()) {
                fastResult.error = "Video watermark cancelled.";
                fastResult.diagnostic = "Fast segmented processing stopped after cancellation was requested.";
                fastResult.processingMode = "cancelled";
                reportProgress(inputPath, 1, 1, 100.0, "cancelled");
                return fastResult;
            }

            LogManager::instance().log(LogLevel::Info, LogCategory::Watermark,
                "fast_segmented_watermark_fallback",
                fastResult.error + " Falling back to standard full encode.",
                inputPath);
            reportProgress(inputPath, 1, 1, 0.0, "encoding fallback");
        }

        auto result = executeFFmpeg(inputPath, outPath);
        if (m_config.fastSegmentedEncode && !fastFallbackReason.empty()) {
            if (result.processingMode == "cancelled") {
                result.diagnostic = fastFallbackReason
                    + " Standard full encode was then cancelled by the user.";
            } else {
                result.processingMode = "full_encode_fallback";
                result.diagnostic = fastFallbackReason + (result.success
                    ? " Standard full encode completed successfully."
                    : " Standard full encode also failed.");
            }
        }
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
    WatermarkProgressCallback callbackCopy = m_progressCallback;

    return std::async(std::launch::async,
                      [inputPath, outputPath, configCopy, callbackCopy]() mutable {
        Watermarker worker;
        worker.setConfig(configCopy);
        worker.setProgressCallback(std::move(callbackCopy));
        return worker.watermarkVideo(inputPath, outputPath);
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
    result.processingMode = "audio_stream_copy";

    try {
        if (!isFFmpegAvailable()) {
            result.error = "FFmpeg not found. Required for audio metadata embedding.";
            return result;
        }

        auto startTime = std::chrono::steady_clock::now();

        std::string outPath = outputPath.empty() ? generateOutputPath(inputPath) : outputPath;
        result.outputFile = outPath;

        namespace fs = std::filesystem;
        fs::path inputFs;
        fs::path outputFs;
        std::string pathError;
        if (!tryToFsPath(inputPath, inputFs, &pathError)
            || !pathExistsNoThrow(inputFs, &pathError)) {
            result.error = pathError.empty() ? "Input audio file not found: " + inputPath : pathError;
            return result;
        }
        if (!fileSizeNoThrow(inputFs, result.inputSizeBytes, &pathError)
            || result.inputSizeBytes <= 0) {
            result.error = result.inputSizeBytes <= 0
                ? "Input audio file is empty: " + inputPath
                : pathError;
            return result;
        }
        if (!tryToFsPath(outPath, outputFs, &pathError)) {
            result.error = pathError;
            return result;
        }
        createDirectoriesOrThrow(outputFs.parent_path());
        const bool outputExistedBefore = pathExistsNoThrow(outputFs);
        if (outputExistedBefore && !m_config.overwrite) {
            result.error = "Output audio file exists and overwrite is disabled: " + outPath;
            return result;
        }
        std::error_code equivalentError;
        if (fs::equivalent(inputFs, outputFs, equivalentError) && !equivalentError) {
            result.error = "Input and output audio paths must be different.";
            return result;
        }

        LogManager::instance().logWatermark("audio_start", "Starting audio metadata: " + inputPath, inputPath);

        // Build FFmpeg command for metadata-only embedding (no re-encoding)
        std::string ffmpegPath = getFFmpegPath();
        std::vector<std::string> cmd = {
            ffmpegPath.empty() ? "ffmpeg" : ffmpegPath,
            m_config.overwrite ? "-y" : "-n", "-i", inputPath
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

        int64_t outputSize = 0;
        if (exitCode == 0 && fileExists(outPath)
            && fileSizeNoThrow(outputFs, outputSize) && outputSize > 0) {
            result.success = true;
            result.outputSizeBytes = outputSize;
            LogManager::instance().logWatermark("audio_complete",
                "Audio metadata complete: " + outPath + " (" + std::to_string(result.processingTimeMs) + "ms)", inputPath);
        } else {
            if (exitCode == -2 || m_cancelled.load()) {
                result.processingMode = "cancelled";
                result.error = "Audio metadata operation cancelled.";
            } else if (exitCode == 0) {
                result.error = "FFmpeg succeeded but output file not found: " + outPath;
            } else {
                result.error = "FFmpeg failed (exit code " + std::to_string(exitCode) + "): " +
                               summarizeProcessOutput(ffmpegOutput + "\n" + ffmpegErr);
            }
            if (!outputExistedBefore || m_config.overwrite) {
                std::error_code removeError;
                fs::remove(outputFs, removeError);
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
        result.processingMode = "passthrough_copy";
        namespace fs = std::filesystem;
        const auto startTime = std::chrono::steady_clock::now();
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

            if (!pathExistsNoThrow(inputFs, &pathError)) {
                throw std::runtime_error(pathError.empty()
                    ? "Input passthrough file not found"
                    : pathError);
            }
            if (!fileSizeNoThrow(inputFs, result.inputSizeBytes, &pathError)) {
                throw std::runtime_error(pathError);
            }

            createDirectoriesOrThrow(outFs.parent_path());
            const bool outputExistedBefore = pathExistsNoThrow(outFs);
            if (outputExistedBefore && !m_config.overwrite) {
                throw std::runtime_error("Output passthrough file exists and overwrite is disabled");
            }
            std::error_code equivalentError;
            if (fs::equivalent(inputFs, outFs, equivalentError) && !equivalentError) {
                throw std::runtime_error("Input and output passthrough paths must be different");
            }

            std::error_code ec;
            const fs::copy_options copyMode = m_config.overwrite
                ? fs::copy_options::overwrite_existing
                : fs::copy_options::none;
            fs::copy_file(inputFs, outFs, copyMode, ec);
            if (ec) {
                throw std::runtime_error("Failed to copy passthrough file: " + ec.message());
            }
            if (!fileSizeNoThrow(outFs, result.outputSizeBytes, &pathError)) {
                throw std::runtime_error(pathError);
            }
            result.success = true;
            result.processingTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime).count();
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
                results.push_back(watermarkVideo(inputPaths[i], outPath));
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
                    return watermarkVideo(inputPaths[i], outPath);
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

    (void)parallel;

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
