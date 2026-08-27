#include "pch.h"
#include "debug.h"
#include <windows.h>
#include <strsafe.h>
#include <algorithm>
#include <ctime>
#include <string>
#include <vector>


void DebugMessage(const char* pszFormat, ...) {
    char pszDest[1024];
    size_t cbDest = 1024 * sizeof(char);
    va_list argList;
    va_start(argList, pszFormat);
    StringCbVPrintfA(pszDest, cbDest, pszFormat, argList);
    OutputDebugStringA(pszDest);
    fputs(pszDest, stdout);
    fputc('\n', stdout);
    va_end(argList);
}

void ErrorMessage(const char* pszFormat, ...) {
    char pszDest[1024];
    size_t cbDest = 1024 * sizeof(char);
    va_list argList;
    va_start(argList, pszFormat);
    StringCbVPrintfA(pszDest, cbDest, pszFormat, argList);
    // Log BEFORE the box. A modal dialog stops everything until the user clicks it, so without
    // this an ErrorMessage is indistinguishable in the log from a hard crash: both leave a log
    // that simply stops. Flushed too, because the process may never get another chance.
    LogInfo("*** ErrorMessage *** %s", pszDest);
    LogFlush();
    MessageBoxA(nullptr, pszDest, "Error", MB_ICONERROR);
    va_end(argList);
}

void LogCrashReport(unsigned long dwError, const char* pszContext) {
    // Resolve system error message
    LPSTR pszSysMsg = nullptr;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, dwError, 0, (LPSTR)&pszSysMsg, 0, nullptr
    );

    // Timestamp
    time_t now = time(nullptr);
    char szTime[32];
    struct tm tmBuf;
    localtime_s(&tmBuf, &now);
    strftime(szTime, sizeof(szTime), "%Y-%m-%d %H:%M:%S", &tmBuf);

    // Write log alongside the exe
    char szExePath[MAX_PATH];
    GetModuleFileNameA(nullptr, szExePath, MAX_PATH);
    char* pLastSlash = strrchr(szExePath, '\\');
    if (pLastSlash) *(pLastSlash + 1) = '\0';
    StringCbCatA(szExePath, sizeof(szExePath), "MapleNight_crash.log");

    FILE* f = nullptr;
    fopen_s(&f, szExePath, "a");
    if (f) {
        fprintf(f, "=== Crash Report [%s] ===\n", szTime);
        fprintf(f, "Context : %s\n", pszContext ? pszContext : "(none)");
        fprintf(f, "Error   : %lu (0x%08lX)\n", dwError, dwError);
        fprintf(f, "Message : %s", pszSysMsg ? pszSysMsg : "(unknown)\n");
        fprintf(f, "========================================\n\n");
        fclose(f);
    }

    if (pszSysMsg) LocalFree(pszSysMsg);
}

// ===== Log file ============================================================================
// LogInfo used to resolve the log path and open/close MapleNight.log on every single line, on
// the calling thread. That is harmless for startup tracing and ruinous in combat: the client
// logs per mob per hit, so a busy fight became hundreds of synchronous file open/close round
// trips per second on the game thread -- each one traversing the AV filter driver. Now the path
// is resolved once, the handle stays open, and writes are buffered with a timed flush.
//
// Anything that can be followed by process death has to force the bytes out itself: the crash
// handler calls LogFlush() explicitly, and atexit covers ordinary shutdown (which is what the
// short-lived launcher and updater rely on, since they share this file).

namespace {

constexpr DWORD kLogFlushIntervalMs = 1000;

// One log per run, in a logs\ folder beside the executable -- the same folder the launcher already
// uses for launcher.log and updater.log. A single appended MapleNight.log grew past 60MB across
// sessions, which made "what happened in THIS run" an archaeology exercise and locked the whole
// history behind one handle while the client held it.
//
// Name: MapleNight_<YYYYMMDD-HHMMSS>_<pid>.log. The pid matters as well as the timestamp: the
// launcher and the injected client are separate processes that can start inside the same second
// and both link this file.
constexpr int kLogsToKeep = 20;

// Deletes the oldest MapleNight_*.log files, keeping the newest kLogsToKeep (this run's included,
// since it is created first). Scoped to our own name pattern inside our own logs folder -- it will
// not touch launcher.log, updater.log, or anything else living there.
void PruneOldLogs(const std::string& sDir) {
    WIN32_FIND_DATAA fd{};
    const std::string sPattern = sDir + "MapleNight_*.log";
    HANDLE hFind = FindFirstFileA(sPattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        return;
    }

    struct Entry {
        std::string sName;
        ULONGLONG uWritten;
    };
    std::vector<Entry> files;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        ULARGE_INTEGER t;
        t.LowPart = fd.ftLastWriteTime.dwLowDateTime;
        t.HighPart = fd.ftLastWriteTime.dwHighDateTime;
        files.push_back({ fd.cFileName, t.QuadPart });
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);

    if (files.size() <= static_cast<size_t>(kLogsToKeep)) {
        return;
    }
    std::sort(files.begin(), files.end(),
            [](const Entry& a, const Entry& b) { return a.uWritten > b.uWritten; });
    for (size_t i = kLogsToKeep; i < files.size(); ++i) {
        DeleteFileA((sDir + files[i].sName).c_str());
    }
}

struct LogState {
    CRITICAL_SECTION cs{};
    FILE* f = nullptr;
    DWORD tLastFlush = 0;
    bool bDebugger = false;

    LogState() {
        InitializeCriticalSection(&cs);

        // Sampled once. OutputDebugStringA takes a global mutex and does cross-process signalling
        // even when nothing is listening, so in a hot path it is pure cost.
        bDebugger = IsDebuggerPresent() != FALSE;

        char szPath[MAX_PATH];
        GetModuleFileNameA(nullptr, szPath, MAX_PATH);
        char* pLastSlash = strrchr(szPath, '\\');
        if (pLastSlash) {
            *(pLastSlash + 1) = '\0';
        }

        SYSTEMTIME st{};
        GetLocalTime(&st);
        char szFile[64];
        StringCbPrintfA(szFile, sizeof(szFile), "MapleNight_%04u%02u%02u-%02u%02u%02u_%lu.log",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                GetCurrentProcessId());

        // logs\ beside the exe. If it cannot be made (read-only install, odd permissions) fall
        // back to the old single file rather than losing logging altogether.
        std::string sDir = std::string(szPath) + "logs\\";
        const bool bHaveDir = CreateDirectoryA(sDir.c_str(), nullptr)
                || GetLastError() == ERROR_ALREADY_EXISTS;
        std::string sLog = bHaveDir ? (sDir + szFile) : (std::string(szPath) + "MapleNight.log");

        fopen_s(&f, sLog.c_str(), "a");
        if (f && bHaveDir) {
            PruneOldLogs(sDir);
        }
        if (f) {
            setvbuf(f, nullptr, _IOFBF, 64 * 1024);
        }
        tLastFlush = GetTickCount();
        atexit(&LogFlush);
    }

    // Deliberately no destructor: tearing the handle down would race other threads still logging
    // during shutdown. atexit gets the buffered bytes out; the OS reclaims the handle either way.
};

LogState& GetLogState() {
    static LogState s; // thread-safe init; first use is always after CRT startup
    return s;
}

} // namespace

void LogFlush() {
    LogState& st = GetLogState();
    EnterCriticalSection(&st.cs);
    if (st.f) {
        fflush(st.f);
        st.tLastFlush = GetTickCount();
    }
    LeaveCriticalSection(&st.cs);
}

void LogInfo(const char* pszFormat, ...) {
    char pszDest[1024];
    va_list argList;
    va_start(argList, pszFormat);
    StringCbVPrintfA(pszDest, sizeof(pszDest), pszFormat, argList);
    va_end(argList);

    LogState& st = GetLogState();

    if (st.bDebugger) {
        OutputDebugStringA(pszDest);
        OutputDebugStringA("\n");
    }

    time_t now = time(nullptr);
    char szTime[32];
    struct tm tmBuf;
    localtime_s(&tmBuf, &now);
    strftime(szTime, sizeof(szTime), "%Y-%m-%d %H:%M:%S", &tmBuf);

    EnterCriticalSection(&st.cs);
    if (st.f) {
        fprintf(st.f, "[%s] %s\n", szTime, pszDest);
        const DWORD tNow = GetTickCount();
        if (tNow - st.tLastFlush >= kLogFlushIntervalMs) {
            fflush(st.f);
            st.tLastFlush = tNow;
        }
    }
    LeaveCriticalSection(&st.cs);
}