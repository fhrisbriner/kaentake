#include "pch.h"
#include "debug.h"
#include <windows.h>
#include <strsafe.h>
#include <ctime>


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
        StringCbCatA(szPath, sizeof(szPath), "MapleNight.log");

        fopen_s(&f, szPath, "a");
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