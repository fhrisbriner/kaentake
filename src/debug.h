#pragma once

#ifdef _DEBUG
#define DEBUG_MESSAGE(FORMAT, ...) DebugMessage(FORMAT, __VA_ARGS__)
#else
#define DEBUG_MESSAGE(FORMAT, ...)
#endif


void DebugMessage(const char* sFormat, ...);

void ErrorMessage(const char* sFormat, ...);

void LogCrashReport(unsigned long dwError, const char* pszContext);

void LogInfo(const char* pszFormat, ...);

// LogInfo buffers and flushes on a timer. Call this before anything that can end the process
// (the crash handler does) so the tail of the log actually reaches disk.
void LogFlush();