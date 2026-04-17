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

void LogInfo(const char* pszFormat, ...) {
    char pszDest[1024];
    va_list argList;
    va_start(argList, pszFormat);
    StringCbVPrintfA(pszDest, sizeof(pszDest), pszFormat, argList);
    va_end(argList);

    OutputDebugStringA(pszDest);
    OutputDebugStringA("\n");

    time_t now = time(nullptr);
    char szTime[32];
    struct tm tmBuf;
    localtime_s(&tmBuf, &now);
    strftime(szTime, sizeof(szTime), "%Y-%m-%d %H:%M:%S", &tmBuf);

    char szPath[MAX_PATH];
    GetModuleFileNameA(nullptr, szPath, MAX_PATH);
    char* pLastSlash = strrchr(szPath, '\\');
    if (pLastSlash) *(pLastSlash + 1) = '\0';
    StringCbCatA(szPath, sizeof(szPath), "MapleNight.log");

    FILE* f = nullptr;
    fopen_s(&f, szPath, "a");
    if (f) {
        fprintf(f, "[%s] %s\n", szTime, pszDest);
        fclose(f);
    }
}