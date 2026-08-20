#pragma once

#include <set>

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

// Alias kept because several modules were written against this name. Same sink as LogInfo.
#define LogMessage LogInfo

// Rate-limited logging, for diagnostics on a path that runs every frame or once per sprite.
// Without these, one bad item floods the log at render rate and buries everything else.
//
// LOG_ONCE        -- fires the first time THIS call site is reached, then never again.
// LOG_ONCE_PER_ID -- fires once per distinct id at this call site, so one broken item logs once
//                    while every other item still gets its own line. The seen-set is per site,
//                    static, and never cleared: bounded by the number of distinct ids the site
//                    actually sees, which for face/item ids is small.
#define LOG_ONCE(FORMAT, ...)                                            do {                                                                     static bool s_bLoggedOnce_ = false;                                  if (!s_bLoggedOnce_) {                                                   s_bLoggedOnce_ = true;                                               LogInfo(FORMAT, __VA_ARGS__);                                    }                                                                } while (0)

#define LOG_ONCE_PER_ID(ID, FORMAT, ...)                                 do {                                                                     static std::set<int> s_seenIds_;                                     if (s_seenIds_.insert(static_cast<int>(ID)).second) {                    LogInfo(FORMAT, __VA_ARGS__);                                    }                                                                } while (0)