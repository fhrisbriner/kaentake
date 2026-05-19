#include "debug.h"
#include "constants.h"

#include <windows.h>
#include <windowsx.h>
#include <detours.h>
#include <gdiplus.h>
#include <urlmon.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <cstdarg>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cwctype>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifndef MN_UPDATE_ENABLED
#define MN_UPDATE_ENABLED "0"
#endif

#ifndef MN_UPDATE_LATEST_VERSION
#define MN_UPDATE_LATEST_VERSION ""
#endif

#ifndef MN_UPDATE_BASE_URL
#define MN_UPDATE_BASE_URL ""
#endif

#ifndef MN_RUNTIME_VERSION
#define MN_RUNTIME_VERSION ""
#endif

#ifndef MN_UPDATE_S3_ENDPOINT
#define MN_UPDATE_S3_ENDPOINT ""
#endif

#ifndef MN_UPDATE_S3_BUCKET
#define MN_UPDATE_S3_BUCKET ""
#endif

#ifndef MN_UPDATE_S3_REGION
#define MN_UPDATE_S3_REGION ""
#endif

namespace {

constexpr int kWindowWidth = 720;
constexpr int kWindowHeight = 405;
constexpr int kProgressHeight = 10;
constexpr int kProgressMargin = 34;
constexpr int kUpdateButtonWidth = 130;
constexpr int kUpdateButtonHeight = 36;
constexpr int kUpdateButtonX = kWindowWidth - kProgressMargin - kUpdateButtonWidth;
constexpr int kUpdateButtonY = 310;
constexpr int kCloseButtonSize = 28;
constexpr int kCloseButtonX = kWindowWidth - kCloseButtonSize - 16;
constexpr int kCloseButtonY = 16;
constexpr UINT WM_LAUNCHER_PROGRESS = WM_APP + 1;
constexpr UINT_PTR kAnimationTimerId = 1;

struct DataFile {
    std::wstring path;
    uint64_t size;
};

struct LauncherProgressState {
    std::mutex mutex;
    std::wstring status = L"Preparing update check...";
    std::wstring detail;
    double progress = 0.0;
    bool complete = false;
    bool failed = false;
    bool showUpdateButton = false;
    bool updateAccepted = false;
    bool updateCancelled = false;
};

struct LauncherWindow {
    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
    ULONG_PTR gdiplusToken = 0;
    std::unique_ptr<Gdiplus::Image> background;
    LauncherProgressState state;
};

LauncherWindow *g_launcherWindow = nullptr;
bool g_launcherExitRequested = false;

void SetProgress(
    HWND hwnd,
    const std::wstring &status,
    const std::wstring &detail,
    double progress,
    bool complete = false,
    bool failed = false);
void PumpLauncherMessages();

std::wstring AnimatedStatusText(std::wstring status, bool complete, bool failed) {
    if (complete || failed || status.rfind(L"Downloading ", 0) != 0) {
        return status;
    }

    while (!status.empty() && status.back() == L'.') {
        status.pop_back();
    }

    const DWORD dotCount = (GetTickCount() / 450) % 4;
    status.append(dotCount, L'.');
    return status;
}

std::string WideToUtf8(const std::wstring &value) {
    if (value.empty()) {
        return "";
    }

    const int required = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return "";
    }

    std::string result(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), required, nullptr, nullptr);
    return result;
}

#ifdef _DEBUG
std::mutex g_logMutex;

void DebugLog(const wchar_t *format, ...) {
    wchar_t message[1024]{};
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(message, _TRUNCATE, format, args);
    va_end(args);

    SYSTEMTIME now{};
    GetLocalTime(&now);

    wchar_t line[1280]{};
    swprintf_s(
        line,
        L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] %s\r\n",
        now.wYear,
        now.wMonth,
        now.wDay,
        now.wHour,
        now.wMinute,
        now.wSecond,
        now.wMilliseconds,
        message
    );

    const std::string utf8 = WideToUtf8(line);
    if (utf8.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_logMutex);
    CreateDirectoryW(L"logs", nullptr);
    HANDLE file = CreateFileW(L"logs\\launcher.log", FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD written = 0;
    WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(file);
}
#else
void DebugLog(const wchar_t *, ...) {}
#endif

std::wstring Utf8ToWide(const char *value) {
    if (!value || !*value) {
        return L"";
    }

    const int required = MultiByteToWideChar(CP_UTF8, 0, value, -1, nullptr, 0);
    if (required <= 1) {
        return L"";
    }

    std::wstring result(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value, -1, result.data(), required);
    result.resize(static_cast<size_t>(required - 1));
    return result;
}

std::wstring Utf8ToWide(const std::string &value) {
    return Utf8ToWide(value.c_str());
}

void TrimInPlace(std::wstring &value) {
    if (!value.empty() && value.front() == L'\xfeff') {
        value.erase(value.begin());
    }
    while (!value.empty() && iswspace(value.front())) {
        value.erase(value.begin());
    }
    while (!value.empty() && iswspace(value.back())) {
        value.pop_back();
    }
}

void TrimInPlace(std::string &value) {
    while (!value.empty() && isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
}

std::wstring ReadTextFile(const std::wstring &path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return L"";
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 1024 * 1024) {
        CloseHandle(file);
        return L"";
    }

    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)) {
        CloseHandle(file);
        return L"";
    }
    CloseHandle(file);
    bytes.resize(read);

    std::wstring text = Utf8ToWide(bytes.c_str());
    TrimInPlace(text);
    return text;
}

bool WriteBytesToFile(const std::wstring &path, const std::string &bytes) {
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        std::wstring current;
        for (size_t i = 0; i < slash; ++i) {
            current.push_back(path[i]);
            if (path[i] == L'\\' || path[i] == L'/') {
                if (!current.empty()) {
                    CreateDirectoryW(current.c_str(), nullptr);
                }
            }
        }
        CreateDirectoryW(path.substr(0, slash).c_str(), nullptr);
    }

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        DebugLog(L"WriteBytesToFile failed to open %s error=%lu", path.c_str(), GetLastError());
        return false;
    }

    DWORD written = 0;
    const bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
    if (!ok) {
        DebugLog(L"WriteBytesToFile failed to write %s error=%lu written=%lu expected=%llu", path.c_str(), GetLastError(), written, static_cast<unsigned long long>(bytes.size()));
    }
    CloseHandle(file);
    return ok;
}

bool WriteTextFile(const std::wstring &path, const std::wstring &text) {
    return WriteBytesToFile(path, WideToUtf8(text));
}

std::wstring ResolveLatestDataVersion() {
    return Utf8ToWide(MN_UPDATE_LATEST_VERSION);
}

void EnsureLauncherBackgroundAvailable() {
    constexpr wchar_t backgroundPath[] = L"Data\\launcher_bg.png";
    if (GetFileAttributesW(backgroundPath) != INVALID_FILE_ATTRIBUTES) {
        return;
    }
    if (MN_UPDATE_BASE_URL[0] == '\0') {
        DebugLog(L"Launcher background download skipped: base URL is empty");
        return;
    }

    CreateDirectoryW(L"Data", nullptr);

    std::wstring baseUrl = Utf8ToWide(MN_UPDATE_BASE_URL);
    while (!baseUrl.empty() && baseUrl.back() == L'/') {
        baseUrl.pop_back();
    }

    const std::wstring url = baseUrl + L"/launcher_bg.png";
    const HRESULT result = URLDownloadToFileW(nullptr, url.c_str(), backgroundPath, 0, nullptr);
    if (FAILED(result)) {
        DebugLog(L"Launcher background download failed hr=0x%08lx url=%s", static_cast<unsigned long>(result), url.c_str());
        DeleteFileW(backgroundPath);
        return;
    }

    DebugLog(L"Launcher background downloaded from %s", url.c_str());
}

bool HasSuffix(const std::wstring &value, const std::wstring &suffix) {
    return value.size() >= suffix.size()
        && _wcsicmp(value.c_str() + value.size() - suffix.size(), suffix.c_str()) == 0;
}

bool IsGitPath(const std::wstring &path) {
    return path.find(L"\\.git\\") != std::wstring::npos || HasSuffix(path, L"\\.git");
}

bool IsLaunchVerifiedPath(const std::wstring &path) {
    return path.rfind(L"Data\\Skill\\", 0) == 0
        || path.rfind(L"Data\\Mob\\", 0) == 0
        || path.rfind(L"Data\\Map\\", 0) == 0;
}

void EnumerateFiles(const std::wstring &directory, std::vector<DataFile> &files) {
    const std::wstring pattern = directory + L"\\*";
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L"..")) {
            continue;
        }

        std::wstring path = directory + L"\\" + data.cFileName;
        if (IsGitPath(path)) {
            continue;
        }

        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            EnumerateFiles(path, files);
            continue;
        }

        if (!IsLaunchVerifiedPath(path)) {
            continue;
        }

        ULARGE_INTEGER size{};
        size.HighPart = data.nFileSizeHigh;
        size.LowPart = data.nFileSizeLow;
        files.push_back({path, size.QuadPart});
    } while (FindNextFileW(find, &data));

    FindClose(find);
}

uint32_t Crc32Update(uint32_t crc, const uint8_t *data, DWORD size) {
    static uint32_t table[256]{};
    static std::once_flag initialized;
    std::call_once(initialized, []() {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t value = i;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value & 1) != 0 ? 0xEDB88320u ^ (value >> 1) : value >> 1;
            }
            table[i] = value;
        }
    });

    uint32_t current = ~crc;
    for (DWORD i = 0; i < size; ++i) {
        current = table[(current ^ data[i]) & 0xFF] ^ (current >> 8);
    }
    return ~current;
}

bool ComputeFileCrc(const std::wstring &path, std::atomic<uint64_t> &bytesDone) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        DebugLog(L"CRC failed to open file: %s error=%lu", path.c_str(), GetLastError());
        return false;
    }

    std::vector<uint8_t> buffer(1024 * 1024);
    uint32_t crc = 0;
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
            DebugLog(L"CRC failed to read file: %s error=%lu", path.c_str(), GetLastError());
            CloseHandle(file);
            return false;
        }
        if (read == 0) {
            break;
        }
        crc = Crc32Update(crc, buffer.data(), read);
        bytesDone.fetch_add(read, std::memory_order_relaxed);
    }

    CloseHandle(file);
    return true;
}

void SetProgress(
    HWND hwnd,
    const std::wstring &status,
    const std::wstring &detail,
    double progress,
    bool complete,
    bool failed) {
    if (!g_launcherWindow) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_launcherWindow->state.mutex);
        g_launcherWindow->state.status = status;
        g_launcherWindow->state.detail = detail;
        g_launcherWindow->state.progress = std::clamp(progress, 0.0, 1.0);
        g_launcherWindow->state.complete = complete;
        g_launcherWindow->state.failed = failed;
    }
    PostMessageW(hwnd, WM_LAUNCHER_PROGRESS, 0, 0);
}

void SetUpdatePrompt(HWND hwnd, const std::wstring &status, const std::wstring &detail, bool show) {
    if (!g_launcherWindow) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_launcherWindow->state.mutex);
        g_launcherWindow->state.status = status;
        g_launcherWindow->state.detail = detail;
        g_launcherWindow->state.progress = 0.0;
        g_launcherWindow->state.complete = false;
        g_launcherWindow->state.failed = false;
        g_launcherWindow->state.showUpdateButton = show;
        if (show) {
            g_launcherWindow->state.updateAccepted = false;
            g_launcherWindow->state.updateCancelled = false;
        }
    }
    PostMessageW(hwnd, WM_LAUNCHER_PROGRESS, 0, 0);
}

bool WasUpdateAccepted() {
    if (!g_launcherWindow) {
        return true;
    }

    std::lock_guard<std::mutex> lock(g_launcherWindow->state.mutex);
    return g_launcherWindow->state.updateAccepted;
}

bool IsCloseButtonHit(int x, int y) {
    return x >= kCloseButtonX
        && x <= kCloseButtonX + kCloseButtonSize
        && y >= kCloseButtonY
        && y <= kCloseButtonY + kCloseButtonSize;
}

bool WasUpdateCancelled() {
    if (!g_launcherWindow) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_launcherWindow->state.mutex);
    return g_launcherWindow->state.updateCancelled;
}

void CancelUpdatePrompt() {
    g_launcherExitRequested = true;
    if (!g_launcherWindow) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_launcherWindow->state.mutex);
    g_launcherWindow->state.updateCancelled = true;
    g_launcherWindow->state.showUpdateButton = false;
    if (g_launcherWindow->hwnd) {
        InvalidateRect(g_launcherWindow->hwnd, nullptr, FALSE);
    }
}

void PumpLauncherMessages() {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void SleepWithMessagePump(DWORD milliseconds) {
    const DWORD start = GetTickCount();
    while (GetTickCount() - start < milliseconds) {
        PumpLauncherMessages();
        Sleep(16);
    }
}

bool IsUpdateEnabled() {
#ifdef _DEBUG
    return false;
#else
    const std::string enabled = MN_UPDATE_ENABLED;
    return !enabled.empty() && enabled != "0" && _stricmp(enabled.c_str(), "false") != 0;
#endif
}

std::wstring QuoteCommandArg(const std::wstring &value) {
    std::wstring result = L"\"";
    for (const wchar_t ch : value) {
        if (ch == L'"') {
            result += L"\\\"";
        } else {
            result.push_back(ch);
        }
    }
    result += L"\"";
    return result;
}

bool RunProcessAndWait(const std::wstring &exe, std::wstring commandLine, DWORD &exitCode) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(
        exe.c_str(),
        commandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startup,
        &process
    );
    if (!started) {
        DebugLog(L"Process start failed exe=%s error=%lu", exe.c_str(), GetLastError());
        return false;
    }

    WaitForSingleObject(process.hProcess, INFINITE);
    exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

bool StartProcessDetached(const std::wstring &exe, std::wstring commandLine) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(
        exe.c_str(),
        commandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startup,
        &process
    );
    if (!started) {
        DebugLog(L"Detached process start failed exe=%s error=%lu", exe.c_str(), GetLastError());
        return false;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

bool CompletePendingUpdaterReplacement() {
    constexpr wchar_t updaterExe[] = L"Updater.exe";
    constexpr wchar_t updaterNew[] = L"Updater.new.exe";
    constexpr wchar_t updaterOld[] = L"Updater.old.exe";

    if (GetFileAttributesW(updaterNew) == INVALID_FILE_ATTRIBUTES) {
        return true;
    }

    DebugLog(L"Pending Updater.new.exe detected");
    for (int attempt = 0; attempt < 80; ++attempt) {
        DeleteFileW(updaterOld);
        if (MoveFileExW(updaterExe, updaterOld, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
            && MoveFileExW(updaterNew, updaterExe, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(updaterOld);
            DebugLog(L"Updater.exe replacement completed");
            return true;
        }

        const DWORD error = GetLastError();
        if (GetFileAttributesW(updaterExe) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW(updaterOld) != INVALID_FILE_ATTRIBUTES) {
            MoveFileExW(updaterOld, updaterExe, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        }
        DebugLog(L"Updater.exe replacement attempt failed attempt=%d error=%lu", attempt + 1, error);
        Sleep(100);
    }

    DebugLog(L"Updater.exe replacement failed after retries");
    return false;
}

std::wstring CurrentRuntimeVersion() {
    std::wstring version = ReadTextFile(L"runtime.version");
    if (version.empty()) {
        version = Utf8ToWide(MN_RUNTIME_VERSION);
    }
    return version;
}

bool CheckAndStartRuntimeUpdaterIfNeeded() {
    constexpr wchar_t updaterExe[] = L"Updater.exe";
    if (!IsUpdateEnabled()) {
        DebugLog(L"Runtime update skipped: updates disabled");
        return false;
    }
    if (MN_UPDATE_BASE_URL[0] == '\0') {
        DebugLog(L"Runtime update skipped: base URL is empty");
        return false;
    }
    if (GetFileAttributesW(updaterExe) == INVALID_FILE_ATTRIBUTES) {
        DebugLog(L"Runtime update skipped: Updater.exe missing");
        return false;
    }

    const std::wstring baseUrl = Utf8ToWide(MN_UPDATE_BASE_URL);
    const std::wstring localVersion = CurrentRuntimeVersion();
    std::wstring checkCommand =
        QuoteCommandArg(updaterExe)
        + L" runtime-update --install-dir . --work-dir .mn-runtime-update --check-only --base-url "
        + QuoteCommandArg(baseUrl)
        + L" --local-version "
        + QuoteCommandArg(localVersion);

    DWORD checkExitCode = 1;
    DebugLog(L"Runtime update check command=%s", checkCommand.c_str());
    if (!RunProcessAndWait(updaterExe, checkCommand, checkExitCode)) {
        DebugLog(L"Runtime update check failed to start");
        return false;
    }
    DebugLog(L"Runtime update check exitCode=%lu", checkExitCode);
    if (checkExitCode != 11) {
        return false;
    }

    wchar_t pid[32]{};
    swprintf_s(pid, L"%lu", GetCurrentProcessId());
    std::wstring updateCommand =
        QuoteCommandArg(updaterExe)
        + L" runtime-update --install-dir . --work-dir .mn-runtime-update --base-url "
        + QuoteCommandArg(baseUrl)
        + L" --local-version "
        + QuoteCommandArg(localVersion)
        + L" --parent-pid "
        + pid;

    DebugLog(L"Runtime update available; starting updater command=%s", updateCommand.c_str());
    return StartProcessDetached(updaterExe, updateCommand);
}

bool VerifyRuntimeBinaries() {
#ifdef _DEBUG
    DebugLog(L"Runtime binary verification skipped: Debug build");
    return true;
#else
    constexpr wchar_t updaterExe[] = L"Updater.exe";
    if (MN_UPDATE_BASE_URL[0] == '\0') {
        DebugLog(L"Runtime binary verification skipped: base URL is empty");
        return true;
    }
    if (GetFileAttributesW(updaterExe) == INVALID_FILE_ATTRIBUTES) {
        DebugLog(L"Runtime binary verification failed: Updater.exe missing");
        return false;
    }

    const std::wstring localVersion = CurrentRuntimeVersion();
    if (localVersion.empty()) {
        DebugLog(L"Runtime binary verification skipped: runtime version is empty");
        return true;
    }

    const std::wstring baseUrl = Utf8ToWide(MN_UPDATE_BASE_URL);
    std::wstring command =
        QuoteCommandArg(updaterExe)
        + L" runtime-verify --install-dir . --work-dir .mn-runtime-verify --base-url "
        + QuoteCommandArg(baseUrl)
        + L" --local-version "
        + QuoteCommandArg(localVersion);

    DWORD exitCode = 1;
    DebugLog(L"Runtime binary verification command=%s", command.c_str());
    if (!RunProcessAndWait(updaterExe, command, exitCode)) {
        DebugLog(L"Runtime binary verification failed to start");
        return false;
    }

    DebugLog(L"Runtime binary verification exitCode=%lu", exitCode);
    return exitCode == 0;
#endif
}

bool StartRuntimeRepairUpdater() {
#ifdef _DEBUG
    return false;
#else
    constexpr wchar_t updaterExe[] = L"Updater.exe";
    if (MN_UPDATE_BASE_URL[0] == '\0') {
        DebugLog(L"Runtime repair skipped: base URL is empty");
        return false;
    }
    if (GetFileAttributesW(updaterExe) == INVALID_FILE_ATTRIBUTES) {
        DebugLog(L"Runtime repair skipped: Updater.exe missing");
        return false;
    }

    const std::wstring localVersion = CurrentRuntimeVersion();
    if (localVersion.empty()) {
        DebugLog(L"Runtime repair skipped: runtime version is empty");
        return false;
    }

    wchar_t pid[32]{};
    swprintf_s(pid, L"%lu", GetCurrentProcessId());
    const std::wstring baseUrl = Utf8ToWide(MN_UPDATE_BASE_URL);
    std::wstring repairCommand =
        QuoteCommandArg(updaterExe)
        + L" runtime-update --install-dir . --work-dir .mn-runtime-repair --repair --base-url "
        + QuoteCommandArg(baseUrl)
        + L" --local-version "
        + QuoteCommandArg(localVersion)
        + L" --parent-pid "
        + pid;

    DebugLog(L"Runtime repair starting command=%s", repairCommand.c_str());
    return StartProcessDetached(updaterExe, repairCommand);
#endif
}

void SetUpdaterEnvironment() {
    if (MN_UPDATE_BASE_URL[0] != '\0') {
        SetEnvironmentVariableW(L"MN_UPDATE_BASE_URL", Utf8ToWide(MN_UPDATE_BASE_URL).c_str());
    }
    if (MN_UPDATE_S3_ENDPOINT[0] != '\0') {
        SetEnvironmentVariableW(L"MN_UPDATE_S3_ENDPOINT", Utf8ToWide(MN_UPDATE_S3_ENDPOINT).c_str());
    }
    if (MN_UPDATE_S3_BUCKET[0] != '\0') {
        SetEnvironmentVariableW(L"MN_UPDATE_S3_BUCKET", Utf8ToWide(MN_UPDATE_S3_BUCKET).c_str());
    }
    if (MN_UPDATE_S3_REGION[0] != '\0') {
        SetEnvironmentVariableW(L"MN_UPDATE_S3_REGION", Utf8ToWide(MN_UPDATE_S3_REGION).c_str());
    }
}

std::vector<std::string> SplitProgressLine(const std::string &line) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= line.size()) {
        const size_t pos = line.find('|', start);
        if (pos == std::string::npos) {
            parts.push_back(line.substr(start));
            break;
        }
        parts.push_back(line.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

uint64_t ParseProgressUInt64(const std::vector<std::string> &parts, size_t index) {
    if (index >= parts.size() || parts[index].empty()) {
        return 0;
    }
    return _strtoui64(parts[index].c_str(), nullptr, 10);
}

std::wstring ShortProgressPath(const std::string &detail) {
    const size_t slash = detail.find_last_of("/\\");
    std::string name = slash == std::string::npos ? detail : detail.substr(slash + 1);
    constexpr const char payloadExtension[] = ".payload";
    constexpr const char payloadsExtension[] = ".payloads";
    if (name.size() > strlen(payloadsExtension) && _stricmp(name.c_str() + name.size() - strlen(payloadsExtension), payloadsExtension) == 0) {
        name.resize(name.size() - strlen(payloadsExtension));
    } else if (name.size() > strlen(payloadExtension) && _stricmp(name.c_str() + name.size() - strlen(payloadExtension), payloadExtension) == 0) {
        name.resize(name.size() - strlen(payloadExtension));
    } else if (_stricmp(name.c_str(), "payloads") == 0) {
        name = "files";
    }
    return Utf8ToWide(name);
}

std::wstring FormatDownloadSpeed(uint64_t bytesPerSecond) {
    wchar_t text[64]{};
    const double mib = static_cast<double>(bytesPerSecond) / (1024.0 * 1024.0);
    if (mib >= 1.0) {
        swprintf_s(text, L"%.1f MB/s", mib);
        return text;
    }

    const double kib = static_cast<double>(bytesPerSecond) / 1024.0;
    swprintf_s(text, L"%.0f KB/s", kib);
    return text;
}

bool HandleUpdaterProgressLine(HWND hwnd, const std::string &line) {
    if (line.rfind("PROGRESS|", 0) != 0) {
        return false;
    }

    const auto parts = SplitProgressLine(line);
    if (parts.size() < 5) {
        return false;
    }

    const std::string &phase = parts[1];
    const int current = (std::max)(0, atoi(parts[2].c_str()));
    const int total = (std::max)(1, atoi(parts[3].c_str()));
    double fraction = std::clamp(static_cast<double>(current) / static_cast<double>(total), 0.0, 1.0);
    const uint64_t bytesPerSecond = ParseProgressUInt64(parts, 5);
    const uint64_t overallBytes = ParseProgressUInt64(parts, 8);
    const uint64_t overallTotal = ParseProgressUInt64(parts, 9);
    if (overallTotal > 0 && overallBytes <= overallTotal) {
        fraction = std::clamp(static_cast<double>(overallBytes) / static_cast<double>(overallTotal), 0.0, 1.0);
    }

    double start = 0.10;
    double end = 0.15;
    std::wstring status = L"Applying update...";
    if (phase == "latest") {
        status = L"Checking latest version...";
        start = 0.10;
        end = 0.14;
    } else if (phase == "manifest") {
        status = L"Downloading patch manifest...";
        start = 0.14;
        end = 0.18;
    } else if (phase == "download") {
        status = L"Downloading patch files...";
        start = 0.18;
        end = 0.70;
    } else if (phase == "bootstrap-download") {
        status = L"Downloading game files...";
        start = 0.18;
        end = 0.70;
    } else if (phase == "apply") {
        status = L"Applying patch...";
        start = 0.70;
        end = 0.92;
    } else if (phase == "repair-download") {
        status = L"Downloading repair files...";
        start = 0.18;
        end = 0.70;
    } else if (phase == "repair") {
        status = L"Repairing game files...";
        start = 0.70;
        end = 0.92;
    } else if (phase == "verify-manifest") {
        status = L"Downloading verification manifest...";
        start = 0.04;
        end = 0.10;
    } else if (phase == "verify") {
        status = L"Verifying game files...";
        start = 0.10;
        end = 0.98;
    }

    wchar_t countText[96]{};
    if (bytesPerSecond > 0) {
        swprintf_s(countText, L"%d / %d (%s)", current, total, FormatDownloadSpeed(bytesPerSecond).c_str());
    } else {
        swprintf_s(countText, L"%d / %d", current, total);
    }
    std::wstring detail = countText;
    const std::wstring item = ShortProgressPath(parts[4]);
    if (!item.empty()) {
        detail += L"  ";
        detail += item;
    }

    SetProgress(hwnd, status, detail, start + ((end - start) * fraction));
    return true;
}

bool RunExternalUpdater(HWND hwnd, std::wstring &installedVersion) {
    constexpr wchar_t updaterExe[] = L"Updater.exe";
    if (GetFileAttributesW(updaterExe) == INVALID_FILE_ATTRIBUTES) {
        DebugLog(L"Updater.exe not found next to launcher");
        return false;
    }

    SetProgress(hwnd, L"Applying update...", L"Starting Updater.exe", 0.10);
    PumpLauncherMessages();

    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;
    HANDLE pipeRead = nullptr;
    HANDLE pipeWrite = nullptr;
    if (!CreatePipe(&pipeRead, &pipeWrite, &securityAttributes, 0)) {
        DebugLog(L"Updater.exe pipe creation failed error=%lu", GetLastError());
        return false;
    }
    SetHandleInformation(pipeRead, HANDLE_FLAG_INHERIT, 0);

    HANDLE logFile = INVALID_HANDLE_VALUE;
#ifdef _DEBUG
    CreateDirectoryW(L"logs", nullptr);
    logFile = CreateFileW(
        L"logs\\updater.log",
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
#endif

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = pipeWrite;
    startup.hStdError = pipeWrite;

    PROCESS_INFORMATION process{};
    std::wstring commandLine =
        QuoteCommandArg(updaterExe)
        + L" update --data-dir Data --work-dir Data\\.mn-update --workers 8 --maple-version GMS";

    SetUpdaterEnvironment();
    DebugLog(L"Starting Updater.exe command=%s", commandLine.c_str());
    const BOOL started = CreateProcessW(
        updaterExe,
        commandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startup,
        &process
    );

    if (!started) {
        DebugLog(L"Updater.exe start failed error=%lu", GetLastError());
        CloseHandle(pipeRead);
        CloseHandle(pipeWrite);
        if (logFile != INVALID_HANDLE_VALUE) {
            CloseHandle(logFile);
        }
        return false;
    }
    CloseHandle(pipeWrite);

    std::thread outputReader([hwnd, pipeRead, logFile]() {
        std::string pending;
        char buffer[4096]{};
        DWORD read = 0;
        while (ReadFile(pipeRead, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
            if (logFile != INVALID_HANDLE_VALUE) {
                DWORD written = 0;
                WriteFile(logFile, buffer, read, &written, nullptr);
            }

            pending.append(buffer, buffer + read);
            for (;;) {
                const size_t newline = pending.find('\n');
                if (newline == std::string::npos) {
                    break;
                }
                std::string line = pending.substr(0, newline);
                pending.erase(0, newline + 1);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                HandleUpdaterProgressLine(hwnd, line);
            }
        }

        if (!pending.empty()) {
            if (!pending.empty() && pending.back() == '\r') {
                pending.pop_back();
            }
            HandleUpdaterProgressLine(hwnd, pending);
        }
        CloseHandle(pipeRead);
    });

    const DWORD startTick = GetTickCount();
    DWORD wait = WAIT_TIMEOUT;
    bool terminatedByExit = false;
    while ((wait = WaitForSingleObject(process.hProcess, 50)) == WAIT_TIMEOUT) {
        PumpLauncherMessages();
        if (g_launcherExitRequested && !terminatedByExit) {
            DebugLog(L"Updater.exe terminating: launcher exit requested");
            TerminateProcess(process.hProcess, 0xC000013AL);
            terminatedByExit = true;
        }
    }

    if (outputReader.joinable()) {
        outputReader.join();
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (logFile != INVALID_HANDLE_VALUE) {
        CloseHandle(logFile);
    }

    DebugLog(L"Updater.exe completed wait=%lu exitCode=%lu elapsedMs=%lu", wait, exitCode, GetTickCount() - startTick);
    if (wait != WAIT_OBJECT_0 || exitCode != 0) {
        return false;
    }

    installedVersion = ReadTextFile(L"Data\\version");
    DebugLog(L"Updater.exe installed version: %s", installedVersion.c_str());
    return !installedVersion.empty();
}

bool RunExternalVerifier(HWND hwnd) {
    constexpr wchar_t updaterExe[] = L"Updater.exe";
    if (GetFileAttributesW(updaterExe) == INVALID_FILE_ATTRIBUTES) {
        DebugLog(L"Updater.exe not found next to launcher for verification");
        return false;
    }

    SetProgress(hwnd, L"Verifying game files...", L"Starting Updater.exe", 0.04);
    PumpLauncherMessages();

    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;
    HANDLE pipeRead = nullptr;
    HANDLE pipeWrite = nullptr;
    if (!CreatePipe(&pipeRead, &pipeWrite, &securityAttributes, 0)) {
        DebugLog(L"Updater.exe verify pipe creation failed error=%lu", GetLastError());
        return false;
    }
    SetHandleInformation(pipeRead, HANDLE_FLAG_INHERIT, 0);

    HANDLE logFile = INVALID_HANDLE_VALUE;
#ifdef _DEBUG
    CreateDirectoryW(L"logs", nullptr);
    logFile = CreateFileW(
        L"logs\\updater-verify.log",
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
#endif

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = pipeWrite;
    startup.hStdError = pipeWrite;

    PROCESS_INFORMATION process{};
    std::wstring commandLine =
        QuoteCommandArg(updaterExe)
        + L" verify --data-dir Data --work-dir Data\\.mn-verify --workers 2 --maple-version GMS";

    SetUpdaterEnvironment();
    DebugLog(L"Starting Updater.exe verify command=%s", commandLine.c_str());
    const BOOL started = CreateProcessW(
        updaterExe,
        commandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startup,
        &process
    );

    if (!started) {
        DebugLog(L"Updater.exe verify start failed error=%lu", GetLastError());
        CloseHandle(pipeRead);
        CloseHandle(pipeWrite);
        if (logFile != INVALID_HANDLE_VALUE) {
            CloseHandle(logFile);
        }
        return false;
    }
    CloseHandle(pipeWrite);

    std::thread outputReader([hwnd, pipeRead, logFile]() {
        std::string pending;
        char buffer[4096]{};
        DWORD read = 0;
        while (ReadFile(pipeRead, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
            if (logFile != INVALID_HANDLE_VALUE) {
                DWORD written = 0;
                WriteFile(logFile, buffer, read, &written, nullptr);
            }

            pending.append(buffer, buffer + read);
            for (;;) {
                const size_t newline = pending.find('\n');
                if (newline == std::string::npos) {
                    break;
                }
                std::string line = pending.substr(0, newline);
                pending.erase(0, newline + 1);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                HandleUpdaterProgressLine(hwnd, line);
            }
        }

        if (!pending.empty()) {
            if (!pending.empty() && pending.back() == '\r') {
                pending.pop_back();
            }
            HandleUpdaterProgressLine(hwnd, pending);
        }
        CloseHandle(pipeRead);
    });

    const DWORD startTick = GetTickCount();
    DWORD wait = WAIT_TIMEOUT;
    bool terminatedByExit = false;
    while ((wait = WaitForSingleObject(process.hProcess, 50)) == WAIT_TIMEOUT) {
        PumpLauncherMessages();
        if (g_launcherExitRequested && !terminatedByExit) {
            DebugLog(L"Updater.exe terminating: launcher exit requested");
            TerminateProcess(process.hProcess, 0xC000013AL);
            terminatedByExit = true;
        }
    }

    if (outputReader.joinable()) {
        outputReader.join();
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (logFile != INVALID_HANDLE_VALUE) {
        CloseHandle(logFile);
    }

    DebugLog(L"Updater.exe verify completed wait=%lu exitCode=%lu elapsedMs=%lu", wait, exitCode, GetTickCount() - startTick);
    if (wait != WAIT_OBJECT_0 || exitCode != 0) {
        SetProgress(hwnd, L"Verification failed", L"Please repair or update again", 1.0, true, true);
        SleepWithMessagePump(1400);
        return false;
    }

    SetProgress(hwnd, L"Game files verified", L"Ready to launch", 1.0, true, false);
    SleepWithMessagePump(450);
    return true;
}

bool WaitForUpdateButtonIfNeeded(HWND hwnd, std::wstring &installedVersion) {
    DebugLog(
        L"Update check start enabled=%S compiledLatest=%S endpoint=%S bucket=%S region=%S",
        MN_UPDATE_ENABLED,
        MN_UPDATE_LATEST_VERSION,
        MN_UPDATE_S3_ENDPOINT,
        MN_UPDATE_S3_BUCKET,
        MN_UPDATE_S3_REGION
    );

    if (!IsUpdateEnabled()) {
        DebugLog(L"Update check skipped: disabled");
        return true;
    }

    SetProgress(hwnd, L"Checking latest version...", L"Contacting update storage", 0.0);
    PumpLauncherMessages();

    const std::wstring latestVersion = ResolveLatestDataVersion();
    if (latestVersion.empty()) {
        DebugLog(L"Update check skipped: latest version is empty");
        return true;
    }

    std::wstring localVersion = ReadTextFile(L"Data\\version");
    if (localVersion.empty()) {
        localVersion = L"0.0.0";
        DebugLog(L"Local Data version missing or empty; defaulting to %s", localVersion.c_str());
    } else {
        DebugLog(L"Local Data version: %s", localVersion.c_str());
    }

    if (_wcsicmp(localVersion.c_str(), latestVersion.c_str()) == 0) {
        DebugLog(L"Data already up to date: %s", localVersion.c_str());
        SetProgress(hwnd, L"Data is up to date", L"Version " + localVersion, 1.0, true, false);
        SleepWithMessagePump(300);
        return true;
    }

    std::wstring detail = L"Data " + localVersion + L" -> " + latestVersion;

    DebugLog(L"Update available: local=%s latest=%s", localVersion.c_str(), latestVersion.c_str());
    SetUpdatePrompt(hwnd, L"Update available", detail, true);
    while (!WasUpdateAccepted() && !WasUpdateCancelled()) {
        PumpLauncherMessages();
        Sleep(16);
    }
    if (WasUpdateCancelled()) {
        DebugLog(L"Update cancelled by user");
        return false;
    }

    DebugLog(L"Update button accepted");
    SetUpdatePrompt(hwnd, L"Update confirmed", L"Preparing patch", false);
    if (!RunExternalUpdater(hwnd, installedVersion)) {
        SetProgress(hwnd, L"Update failed", L"Please try again", 1.0, true, true);
        SleepWithMessagePump(1400);
        return false;
    }
    SetProgress(hwnd, L"Update applied", L"Checking game files before launch", 1.0, true, false);
    SleepWithMessagePump(450);
    return true;
}

std::wstring FormatVerifyDetail(uint64_t filesDone, uint64_t fileCount, uint64_t bytesDone, uint64_t totalBytes) {
    wchar_t buffer[160]{};
    swprintf_s(
        buffer,
        L"%llu / %llu files   %.1f / %.1f MiB",
        static_cast<unsigned long long>(filesDone),
        static_cast<unsigned long long>(fileCount),
        bytesDone / 1024.0 / 1024.0,
        totalBytes / 1024.0 / 1024.0
    );
    return buffer;
}

bool VerifyDataWithCrc(HWND hwnd) {
    const DWORD startTick = GetTickCount();
    DebugLog(L"Data verification start");
    SetProgress(hwnd, L"Scanning Data...", L"Looking for files", 0.0);

    std::vector<DataFile> files;
    EnumerateFiles(L"Data", files);
    uint64_t totalBytes = 0;
    for (const auto &file : files) {
        totalBytes += file.size;
    }
    DebugLog(L"Data verification scan complete files=%llu bytes=%llu", static_cast<unsigned long long>(files.size()), static_cast<unsigned long long>(totalBytes));

    if (files.empty()) {
        DebugLog(L"Data verification skipped: no matching files");
        SetProgress(hwnd, L"Data check skipped", L"No Data folder was found", 1.0, true, false);
        SleepWithMessagePump(450);
        return true;
    }

    std::atomic<size_t> nextIndex{0};
    std::atomic<uint64_t> filesDone{0};
    std::atomic<uint64_t> bytesDone{0};
    std::atomic<bool> failed{false};

    constexpr int workerCount = 2;
    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    for (int i = 0; i < workerCount; ++i) {
        workers.emplace_back([&]() {
            for (;;) {
                const size_t index = nextIndex.fetch_add(1, std::memory_order_relaxed);
                if (index >= files.size()) {
                    break;
                }
                if (!ComputeFileCrc(files[index].path, bytesDone)) {
                    failed.store(true, std::memory_order_relaxed);
                }
                filesDone.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    while (filesDone.load(std::memory_order_relaxed) < files.size()) {
        const uint64_t doneBytes = bytesDone.load(std::memory_order_relaxed);
        const uint64_t doneFiles = filesDone.load(std::memory_order_relaxed);
        const double progress = totalBytes == 0 ? 1.0 : static_cast<double>(doneBytes) / static_cast<double>(totalBytes);
        SetProgress(hwnd, L"Verifying Data...", FormatVerifyDetail(doneFiles, files.size(), doneBytes, totalBytes), progress);

        PumpLauncherMessages();
        Sleep(33);
    }

    for (auto &worker : workers) {
        worker.join();
    }

    const bool ok = !failed.load(std::memory_order_relaxed);
    DebugLog(
        L"Data verification complete ok=%d files=%llu bytes=%llu elapsedMs=%lu",
        ok,
        static_cast<unsigned long long>(files.size()),
        static_cast<unsigned long long>(totalBytes),
        GetTickCount() - startTick
    );
    SetProgress(
        hwnd,
        ok ? L"Data verified" : L"Data verification failed",
        ok ? FormatVerifyDetail(files.size(), files.size(), totalBytes, totalBytes) : L"At least one file could not be read",
        1.0,
        true,
        !ok
    );
    SleepWithMessagePump(ok ? 450 : 1400);
    return ok;
}

LRESULT CALLBACK LauncherWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST: {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        if (IsCloseButtonHit(pt.x, pt.y)) {
            return HTCLIENT;
        }
        if (g_launcherWindow) {
            bool overUpdate = false;
            {
                std::lock_guard<std::mutex> lock(g_launcherWindow->state.mutex);
                overUpdate = g_launcherWindow->state.showUpdateButton
                    && pt.x >= kUpdateButtonX
                    && pt.x <= kUpdateButtonX + kUpdateButtonWidth
                    && pt.y >= kUpdateButtonY
                    && pt.y <= kUpdateButtonY + kUpdateButtonHeight;
            }
            if (overUpdate) {
                return HTCLIENT;
            }
        }
        return HTCAPTION;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);

        HDC memoryDc = CreateCompatibleDC(hdc);
        HBITMAP memoryBitmap = CreateCompatibleBitmap(hdc, kWindowWidth, kWindowHeight);
        HGDIOBJ oldBitmap = SelectObject(memoryDc, memoryBitmap);

        Gdiplus::Graphics graphics(memoryDc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.Clear(Gdiplus::Color(255, 20, 24, 32));

        Gdiplus::Rect bounds(0, 0, kWindowWidth, kWindowHeight);
        if (g_launcherWindow && g_launcherWindow->background && g_launcherWindow->background->GetLastStatus() == Gdiplus::Ok) {
            graphics.DrawImage(g_launcherWindow->background.get(), bounds);
        } else {
            Gdiplus::LinearGradientBrush bg(bounds, Gdiplus::Color(255, 20, 24, 32), Gdiplus::Color(255, 48, 63, 73), Gdiplus::LinearGradientModeForwardDiagonal);
            graphics.FillRectangle(&bg, bounds);
            Gdiplus::SolidBrush accent(Gdiplus::Color(55, 255, 204, 93));
            graphics.FillEllipse(&accent, 470, -100, 340, 260);
            Gdiplus::SolidBrush shade(Gdiplus::Color(90, 9, 12, 18));
            graphics.FillRectangle(&shade, 0, 0, kWindowWidth, kWindowHeight);
        }

        LauncherProgressState snapshot;
        if (g_launcherWindow) {
            std::lock_guard<std::mutex> lock(g_launcherWindow->state.mutex);
            snapshot.status = g_launcherWindow->state.status;
            snapshot.detail = g_launcherWindow->state.detail;
            snapshot.progress = g_launcherWindow->state.progress;
            snapshot.complete = g_launcherWindow->state.complete;
            snapshot.failed = g_launcherWindow->state.failed;
            snapshot.showUpdateButton = g_launcherWindow->state.showUpdateButton;
        }

        Gdiplus::FontFamily titleFamily(L"Segoe UI");
        Gdiplus::Font titleFont(&titleFamily, 32, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font statusFont(&titleFamily, 18, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::Font detailFont(&titleFamily, 13, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush textBrush(Gdiplus::Color(245, 255, 255, 255));
        Gdiplus::SolidBrush detailBrush(Gdiplus::Color(205, 224, 230, 232));
        Gdiplus::SolidBrush statusOverlay(Gdiplus::Color(150, 14, 16, 20));

        graphics.DrawString(L"Maple Night", -1, &titleFont, Gdiplus::PointF(34.0f, 42.0f), &textBrush);

        {
            const bool closeActive = snapshot.showUpdateButton;
            Gdiplus::RectF closeRect(
                static_cast<Gdiplus::REAL>(kCloseButtonX),
                static_cast<Gdiplus::REAL>(kCloseButtonY),
                static_cast<Gdiplus::REAL>(kCloseButtonSize),
                static_cast<Gdiplus::REAL>(kCloseButtonSize)
            );
            Gdiplus::SolidBrush closeBrush(closeActive ? Gdiplus::Color(185, 32, 36, 42) : Gdiplus::Color(95, 38, 40, 44));
            Gdiplus::Pen closeBorder(closeActive ? Gdiplus::Color(220, 210, 218, 224) : Gdiplus::Color(120, 118, 124, 130), 1.0f);
            Gdiplus::Pen closeMark(closeActive ? Gdiplus::Color(235, 255, 255, 255) : Gdiplus::Color(125, 150, 154, 160), 2.0f);
            graphics.FillRectangle(&closeBrush, closeRect);
            graphics.DrawRectangle(&closeBorder, closeRect.X, closeRect.Y, closeRect.Width, closeRect.Height);
            graphics.DrawLine(&closeMark, closeRect.X + 8.0f, closeRect.Y + 8.0f, closeRect.X + closeRect.Width - 8.0f, closeRect.Y + closeRect.Height - 8.0f);
            graphics.DrawLine(&closeMark, closeRect.X + closeRect.Width - 8.0f, closeRect.Y + 8.0f, closeRect.X + 8.0f, closeRect.Y + closeRect.Height - 8.0f);
        }

        graphics.FillRectangle(&statusOverlay, 24, 292, kWindowWidth - 48, 64);
        const std::wstring statusText = AnimatedStatusText(snapshot.status, snapshot.complete, snapshot.failed);
        graphics.DrawString(statusText.c_str(), -1, &statusFont, Gdiplus::PointF(34.0f, 304.0f), &textBrush);
        graphics.DrawString(snapshot.detail.c_str(), -1, &detailFont, Gdiplus::PointF(34.0f, 330.0f), &detailBrush);

        if (snapshot.showUpdateButton) {
            Gdiplus::RectF buttonRect(
                static_cast<Gdiplus::REAL>(kUpdateButtonX),
                static_cast<Gdiplus::REAL>(kUpdateButtonY),
                static_cast<Gdiplus::REAL>(kUpdateButtonWidth),
                static_cast<Gdiplus::REAL>(kUpdateButtonHeight)
            );
            Gdiplus::SolidBrush buttonBrush(Gdiplus::Color(235, 55, 159, 96));
            Gdiplus::Pen buttonBorder(Gdiplus::Color(255, 122, 241, 163), 1.0f);
            graphics.FillRectangle(&buttonBrush, buttonRect);
            graphics.DrawRectangle(
                &buttonBorder,
                buttonRect.X,
                buttonRect.Y,
                buttonRect.Width,
                buttonRect.Height
            );

            Gdiplus::StringFormat buttonFormat;
            buttonFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
            buttonFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            Gdiplus::Font buttonFont(&titleFamily, 15, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            graphics.DrawString(L"Update", -1, &buttonFont, buttonRect, &buttonFormat, &textBrush);
        }

        const int barX = kProgressMargin;
        const int barY = kWindowHeight - kProgressMargin;
        const int barW = kWindowWidth - (kProgressMargin * 2);
        Gdiplus::SolidBrush barBack(Gdiplus::Color(120, 0, 0, 0));
        graphics.FillRectangle(&barBack, barX, barY, barW, kProgressHeight);

        const int fillW = static_cast<int>(barW * snapshot.progress);
        Gdiplus::SolidBrush barFill(snapshot.failed ? Gdiplus::Color(255, 230, 72, 72) : Gdiplus::Color(255, 70, 214, 128));
        graphics.FillRectangle(&barFill, barX, barY, fillW, kProgressHeight);

        BitBlt(hdc, 0, 0, kWindowWidth, kWindowHeight, memoryDc, 0, 0, SRCCOPY);
        SelectObject(memoryDc, oldBitmap);
        DeleteObject(memoryBitmap);
        DeleteDC(memoryDc);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LAUNCHER_PROGRESS:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_TIMER:
        if (wParam == kAnimationTimerId) {
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    case WM_LBUTTONUP: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        if (g_launcherWindow && IsCloseButtonHit(x, y)) {
            CancelUpdatePrompt();
            return 0;
        }
        if (g_launcherWindow
            && x >= kUpdateButtonX
            && x <= kUpdateButtonX + kUpdateButtonWidth
            && y >= kUpdateButtonY
            && y <= kUpdateButtonY + kUpdateButtonHeight) {
            std::lock_guard<std::mutex> lock(g_launcherWindow->state.mutex);
            if (g_launcherWindow->state.showUpdateButton) {
                g_launcherWindow->state.showUpdateButton = false;
                g_launcherWindow->state.updateAccepted = true;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    }
    case WM_CLOSE:
        CancelUpdatePrompt();
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

bool RunUpdaterWindow(HINSTANCE instance) {
    DebugLog(L"Updater window start");
    if (!IsUpdateEnabled()) {
        DebugLog(L"Updater window skipped: updates disabled");
        return true;
    }

    Gdiplus::GdiplusStartupInput gdiplusInput{};
    LauncherWindow window{};
    window.instance = instance;
    if (Gdiplus::GdiplusStartup(&window.gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) {
        DebugLog(L"GDI+ startup failed; running verification without updater window");
        return RunExternalVerifier(nullptr);
    }

    EnsureLauncherBackgroundAvailable();
    window.background = std::make_unique<Gdiplus::Image>(L"Data\\launcher_bg.png");
    DebugLog(L"Launcher background status=%d", window.background ? static_cast<int>(window.background->GetLastStatus()) : -1);
    g_launcherWindow = &window;

    WNDCLASSW wc{};
    wc.lpfnWndProc = LauncherWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"MapleNightUpdaterWindow";
    RegisterClassW(&wc);

    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);
    const int x = (screenW - kWindowWidth) / 2;
    const int y = (screenH - kWindowHeight) / 2;
    window.hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        wc.lpszClassName,
        L"Maple Night",
        WS_POPUP,
        x,
        y,
        kWindowWidth,
        kWindowHeight,
        nullptr,
        nullptr,
        instance,
        nullptr
    );

    if (!window.hwnd) {
        DebugLog(L"Updater window creation failed error=%lu; running verification without window", GetLastError());
        window.background.reset();
        Gdiplus::GdiplusShutdown(window.gdiplusToken);
        g_launcherWindow = nullptr;
        return RunExternalVerifier(nullptr);
    }

    ShowWindow(window.hwnd, SW_SHOW);
    UpdateWindow(window.hwnd);
    SetTimer(window.hwnd, kAnimationTimerId, 250, nullptr);
    std::wstring installedVersion;
    const bool ok = WaitForUpdateButtonIfNeeded(window.hwnd, installedVersion) && RunExternalVerifier(window.hwnd);
    if (ok && !installedVersion.empty()) {
        if (WriteTextFile(L"Data\\version", installedVersion)) {
            DebugLog(L"Wrote Data version: %s", installedVersion.c_str());
        } else {
            DebugLog(L"Failed to write Data version: %s", installedVersion.c_str());
        }
    }
    DebugLog(L"Updater window complete ok=%d", ok);
    KillTimer(window.hwnd, kAnimationTimerId);
    DestroyWindow(window.hwnd);
    window.background.reset();
    Gdiplus::GdiplusShutdown(window.gdiplusToken);
    g_launcherWindow = nullptr;
    return ok;
}

}


int WINAPI PatchExecutionLevel() {
    DebugLog(L"PatchExecutionLevel start");
    HANDLE hFile = CreateFileA(
        "MapleStory.exe",
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        DebugLog(L"PatchExecutionLevel failed to open MapleStory.exe error=%lu", GetLastError());
        ErrorMessage("Error opening MapleStory.exe [%d]", GetLastError());
        return 1;
    }

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart <= 0 || fileSize.QuadPart > 64 * 1024 * 1024) {
        DebugLog(L"PatchExecutionLevel failed to read size error=%lu", GetLastError());
        ErrorMessage("Error reading MapleStory.exe size [%d]", GetLastError());
        CloseHandle(hFile);
        return 1;
    }

    std::vector<char> bytes(static_cast<size_t>(fileSize.QuadPart));
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, bytes.data(), static_cast<DWORD>(bytes.size()), &bytesRead, NULL) || bytesRead != bytes.size()) {
        DebugLog(L"PatchExecutionLevel failed to read bytes error=%lu bytesRead=%lu expected=%llu", GetLastError(), bytesRead, static_cast<unsigned long long>(bytes.size()));
        ErrorMessage("Error reading MapleStory.exe [%d]", GetLastError());
        CloseHandle(hFile);
        return 1;
    }

    const char *patched = "level=\"asInvoker\"           ";
    const char *original = "level=\"requireAdministrator\"";
    const auto patchedLen = strlen(patched);
    const auto originalLen = strlen(original);

    auto patchedIt = std::search(bytes.begin(), bytes.end(), patched, patched + patchedLen);
    if (patchedIt != bytes.end()) {
        DebugLog(L"PatchExecutionLevel already patched");
        CloseHandle(hFile);
        return 0;
    }

    auto originalIt = std::search(bytes.begin(), bytes.end(), original, original + originalLen);
    if (originalIt == bytes.end()) {
        DebugLog(L"PatchExecutionLevel manifest marker not found");
        ErrorMessage("Could not find MapleStory.exe execution level manifest");
        CloseHandle(hFile);
        return 1;
    }
    CloseHandle(hFile);

    if (!CopyFileA("MapleStory.exe", "MapleStory.exe.backup", FALSE) && GetLastError() != ERROR_FILE_EXISTS) {
        DebugLog(L"PatchExecutionLevel failed to create backup error=%lu", GetLastError());
        ErrorMessage("Error creating backup for MapleStory.exe [%d]", GetLastError());
        return 1;
    }

    hFile = CreateFileA(
        "MapleStory.exe",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        DebugLog(L"PatchExecutionLevel failed to open writable MapleStory.exe error=%lu", GetLastError());
        ErrorMessage("Error opening MapleStory.exe for patching [%d]", GetLastError());
        return 1;
    }

    LARGE_INTEGER liOffset;
    liOffset.QuadPart = static_cast<LONGLONG>(std::distance(bytes.begin(), originalIt));
    DWORD bytesWritten = 0;
    if (!SetFilePointerEx(hFile, liOffset, NULL, FILE_BEGIN) ||
        !WriteFile(hFile, patched, static_cast<DWORD>(patchedLen), &bytesWritten, NULL) ||
        bytesWritten != patchedLen) {
        DebugLog(L"PatchExecutionLevel write failed error=%lu bytesWritten=%lu expected=%zu", GetLastError(), bytesWritten, patchedLen);
        ErrorMessage("Error patching MapleStory.exe [%d]", GetLastError());
        CloseHandle(hFile);
        return 1;
    }
    CloseHandle(hFile);
    DebugLog(L"PatchExecutionLevel patched successfully");
    return 0;
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    g_launcherExitRequested = false;
    DebugLog(L"Launcher start commandLine=%s", GetCommandLineW());

    int argc;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        DebugLog(L"CommandLineToArgvW failed error=%lu", GetLastError());
        ErrorMessage("Error parsing command line arguments \"%s\"", lpCmdLine);
        return 1;
    }
    bool verifyOnly = false;
    for (int i = 0; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--patch-execution-level")) {
            DebugLog(L"Launcher mode: --patch-execution-level");
            LocalFree(argv);
            return PatchExecutionLevel();
        }
        if (!wcscmp(argv[i], L"--verify-only")) {
            verifyOnly = true;
        }
    }
    LocalFree(argv);
    DebugLog(L"Launcher args parsed verifyOnly=%d", verifyOnly);

    CompletePendingUpdaterReplacement();
    if (CheckAndStartRuntimeUpdaterIfNeeded()) {
        DebugLog(L"Launcher exiting: runtime updater started");
        return 0;
    }
    if (!VerifyRuntimeBinaries()) {
        DebugLog(L"Launcher exiting: runtime binary verification failed");
        if (StartRuntimeRepairUpdater()) {
            DebugLog(L"Launcher exiting: runtime repair updater started");
            return 0;
        }
        ErrorMessage("Runtime verification failed");
        return 1;
    }

    if (!RunUpdaterWindow(hInstance)) {
        if (g_launcherExitRequested) {
            DebugLog(L"Launcher exiting: update cancelled");
            return 0;
        }
        DebugLog(L"Launcher exiting: data verification failed");
        ErrorMessage("Data verification failed");
        return 1;
    }
    if (verifyOnly) {
        DebugLog(L"Launcher exiting verify-only success");
        return 0;
    }

    if (PatchExecutionLevel() != 0) {
        DebugLog(L"Launcher exiting: PatchExecutionLevel failed");
        return 1;
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(STARTUPINFOA);

    if (!DetourCreateProcessWithDllExA(
        "MapleStory.exe",
        lpCmdLine,
        NULL,
        NULL,
        FALSE,
        CREATE_SUSPENDED,
        NULL,
        NULL,
        &si,
        &pi,
        CONSTANTS_DLL_NAME,
        NULL
    )) {
        DWORD dwError = GetLastError();
        DebugLog(L"DetourCreateProcessWithDllExA failed error=%lu", dwError);
        LogCrashReport(dwError, "DetourCreateProcessWithDllExA(MapleStory.exe, " CONSTANTS_DLL_NAME ")");
        LPSTR sErrorMessage = nullptr;
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, dwError, 0, (LPSTR)&sErrorMessage, 0, nullptr);
        ErrorMessage("Could not start MapleStory.exe [%d]\n%s", dwError, sErrorMessage);
        LocalFree(sErrorMessage);
        return 1;
    }
    DebugLog(L"MapleStory.exe started pid=%lu", pi.dwProcessId);
    ResumeThread(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD dwExitCode;
    if (!GetExitCodeProcess(pi.hProcess, &dwExitCode)) {
        DebugLog(L"GetExitCodeProcess failed error=%lu", GetLastError());
        ErrorMessage("GetExitCodeProcess failed [%d]", GetLastError());
        return 1;
    }
    DebugLog(L"MapleStory.exe exited code=%lu", dwExitCode);
    return dwExitCode;
}
