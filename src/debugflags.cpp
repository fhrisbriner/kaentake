// ============================================================
// debugflags.cpp  —  MapleNight.ini [Debug] switches
//
// See debugflags.h for why these exist. The one design rule worth stating: this is read from the
// FIRST caller, which is AttachSystemHooks inside DllMain. So it must not touch the client, must
// not allocate anything exotic, and must not depend on anything the client sets up later. Plain
// Win32 profile reads against a path derived from the module handle.
// ============================================================

#include "pch.h"
#include "debugflags.h"
#include "debug.h"

#include <windows.h>
#include <string>

static const char kSection[] = "Debug";

static std::string GetIniPath() {
    char path[MAX_PATH]{};
    // Module dir, not the cwd: the client changes directory during startup. Same reasoning as
    // resolution.cpp's copy of this.
    if (!GetModuleFileNameA(nullptr, path, MAX_PATH)) {
        return "MapleNight.ini";
    }
    std::string s(path);
    const size_t slash = s.find_last_of("\\/");
    if (slash == std::string::npos) {
        return "MapleNight.ini";
    }
    return s.substr(0, slash + 1) + "MapleNight.ini";
}

static bool ReadFlag(const char* pszKey, const char* pszIni) {
    return GetPrivateProfileIntA(kSection, pszKey, 0, pszIni) != 0;
}

const DebugFlags& GetDebugFlags() {
    static DebugFlags s_flags{};
    static bool s_bLoaded = false;
    if (s_bLoaded) {
        return s_flags;
    }
    s_bLoaded = true;

    const std::string ini = GetIniPath();
    s_flags.bDisableAllClientHooks = ReadFlag("DisableAllClientHooks", ini.c_str());
    s_flags.bMinimalHooks          = ReadFlag("MinimalHooks", ini.c_str());
    s_flags.bDisableResolution     = ReadFlag("DisableResolution", ini.c_str());
    s_flags.bDisableSkills         = ReadFlag("DisableSkills", ini.c_str());
    s_flags.bDisableSound          = ReadFlag("DisableSound", ini.c_str());
    s_flags.bDisableSoundGuard     = ReadFlag("DisableSoundGuard", ini.c_str());
    s_flags.bDisableWSP            = ReadFlag("DisableWSP", ini.c_str());
    s_flags.bForceWSP              = ReadFlag("ForceWSP", ini.c_str());
    s_flags.bDisablePeerName       = ReadFlag("DisablePeerName", ini.c_str());
    s_flags.bForcePeerName         = ReadFlag("ForcePeerName", ini.c_str());
    s_flags.bVerbose               = ReadFlag("Verbose", ini.c_str());

    // Announce the ACTIVE ones by name. The whole point of these switches is to make a bug report
    // interpretable, so the log has to say which build the user was actually running -- otherwise
    // "I turned DisableSkills on and it still crashed" is unverifiable.
    char sActive[512];
    sActive[0] = '\0';
    const struct { const char* psz; bool b; } aAll[] = {
        {"DisableAllClientHooks", s_flags.bDisableAllClientHooks},
        {"MinimalHooks",          s_flags.bMinimalHooks},
        {"DisableResolution",     s_flags.bDisableResolution},
        {"DisableSkills",         s_flags.bDisableSkills},
        {"DisableSound",          s_flags.bDisableSound},
        {"DisableSoundGuard",     s_flags.bDisableSoundGuard},
        {"DisableWSP",            s_flags.bDisableWSP},
        {"ForceWSP",              s_flags.bForceWSP},
        {"DisablePeerName",       s_flags.bDisablePeerName},
        {"ForcePeerName",         s_flags.bForcePeerName},
        {"Verbose",               s_flags.bVerbose},
    };
    for (const auto& f : aAll) {
        if (!f.b) {
            continue;
        }
        if (sActive[0]) {
            strcat_s(sActive, sizeof(sActive), " ");
        }
        strcat_s(sActive, sizeof(sActive), f.psz);
    }

    if (sActive[0]) {
        LogInfo("DebugFlags: ACTIVE -> %s  (from %s)", sActive, ini.c_str());
    } else {
        LogInfo("DebugFlags: none set (%s)", ini.c_str());
    }
    return s_flags;
}
