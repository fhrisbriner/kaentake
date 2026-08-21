#pragma once

// MapleNight.ini [Debug] switches.
//
// These exist so a player on a broken setup can bisect the DLL without a custom build: turn one
// part off, relaunch, report whether the problem moved. README-linux.md walks users through
// exactly that table, so every switch listed there has to actually do something -- a documented
// switch that silently does nothing produces confidently wrong bug reports.
//
// Read ONCE, lazily, from MapleNight.ini next to the exe, and every value is logged on first
// read. All default to off, so an absent or empty [Debug] section behaves exactly like today.
struct DebugFlags {
    bool bDisableAllClientHooks;  // attach nothing (breaks the client -- diagnostic only)
    bool bMinimalHooks;           // only what the client needs to run
    bool bDisableResolution;      // no resolution switching (also drops the bag window)
    bool bDisableSkills;          // no skill edits
    bool bDisableSound;           // skip audio init entirely
    bool bDisableSoundGuard;      // skip the Wine DirectSound workaround
    bool bDisableWSP;             // never install the MSWSOCK provider hooks
    bool bForceWSP;               // install them even under Wine
    bool bDisablePeerName;        // skip the getpeername address spoof
    bool bForcePeerName;          // install it even under Wine
    bool bVerbose;                // extra logging on the login -> in-game path
};

const DebugFlags& GetDebugFlags();

// Convenience for the login -> in-game tracing gated behind Verbose=1.
#define VERBOSE_LOG(FORMAT, ...)                            \
    do {                                                    \
        if (GetDebugFlags().bVerbose) {                     \
            LogInfo("[verbose] " FORMAT, __VA_ARGS__);      \
        }                                                   \
    } while (0)
