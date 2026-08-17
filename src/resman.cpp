#include "pch.h"
#include "hook.h"
#include "debug.h"
#include "wvs/wvsapp.h"
#include "wvs/util.h"
#include "ztl/ztl.h"
#include <algorithm>
#include <vector>
#include <tuple>


static IWzNameSpacePtr g_pCustomNameSpace;
static std::vector<Ztl_bstr_t> g_vecOverrides;


void CWvsApp::InitializeResMan_hook() {
    try {
        IWzResManPtr& rm = get_rm();
        PcCreateObject<IWzResManPtr>(L"ResMan", rm, nullptr);
        CHECK_HR(rm->raw_SetResManParam(static_cast<enum RESMAN_PARAM>(RESMAN_PARAM::RC_AUTO_REPARSE | RESMAN_PARAM::RC_AUTO_SERIALIZE), -1, -1));

        IWzNameSpacePtr& root = get_root();
        PcCreateObject<IWzNameSpacePtr>(L"NameSpace", root, nullptr);
        PcSetRootNameSpace(root);

        IWzFileSystemPtr fs;
        PcCreateObject<IWzFileSystemPtr>(L"NameSpace#FileSystem", fs, nullptr);
        char sStartPath[MAX_PATH];
        GetModuleFileNameA(nullptr, sStartPath, MAX_PATH);
        Dir_BackSlashToSlash(sStartPath);
        Dir_upDir(sStartPath);
        strcat_s(sStartPath, MAX_PATH, "/Data");
        CHECK_HR(fs->raw_Init(static_cast<wchar_t*>(Ztl_bstr_t(sStartPath))));
        CHECK_HR(root->raw_Mount(L"/", fs, 0));
    } catch (const _com_error& e) {
        HRESULT hr = e.Error();
        ZException exception(hr);
        if (hr == 0x80070005) {
            hr = 0x22000005; // EC_INVALID_GAME_DATA_VERSION
        } else if (hr == 0x80070057) {
            hr = 0x22000003; // EC_NOT_ENOUGH_MEMORY
        } else {
            hr = 0x22000004; // EC_NO_DATA_PACAKGE
        }
        // CTerminateException::CTerminateException(&exception, hr);
        reinterpret_cast<void(__thiscall*)(void*, HRESULT)>(0x00401D50)(&exception, hr);
        throw exception;
    }
}
void CWvsApp::CleanUp_hook() {
    CWvsApp::CleanUp(this);
    g_pCustomNameSpace = nullptr;
}


// ===== Periodic resource cache eviction ====================================================
// SetResManParam above passes nRetainTime = nNameSpaceCacheTime = -1 (vanilla behavior), so
// ResMan never expires a parsed object on its own. The client's only eviction is
// FlushCachedObjects(180000) inside CField::Init @0x529326 -- map change only, and even then it
// keeps anything touched in the last 3 minutes. A boss fight that stays on one map decodes
// hundreds of MB of effect canvases and releases none of them, against a 2 GB address space.
//
// So drive the flush on a timer instead of on map transitions. FlushCachedObjects(nUsedBefore)
// drops cached objects untouched for nUsedBefore ms; it is the same call the client already
// makes, just more often and with a shorter horizon.
//
// A fixed interval alone is not enough. Observed crash at 13:09:38: the client went from 69% of
// address space used (largestFree 142MB) to 98% used with largestFree=0MB in FIVE seconds --
// private bytes 425MB -> 960MB -- and died. The whole collapse happened inside a single 30s
// flush window, so the timer never fired. Anything paced slower than the burst loses.
//
// So there are two triggers now: the ordinary interval, and a pressure trigger that fires as
// soon as free address space drops below a floor, with a much shorter eviction horizon. The
// pressure probe has to be cheap enough to run several times a second, which rules out the VA
// walk (~4000 VirtualQuery calls) -- GlobalMemoryStatusEx gives ullAvailVirtual in one call.
// It reports total free VA rather than the largest contiguous run, but the two track each other
// closely enough here to serve as a trigger.
int resManFlushEnabled = 1;
int resManFlushIntervalMs = 10000;    // ordinary cadence
int resManFlushUnusedMs = 30000;      // ordinary eviction horizon
int resManPressureCheckMs = 500;      // how often the cheap headroom probe runs
int resManEmergencyUnusedMs = 5000;   // emergency horizon: evict far more aggressively

// The pressure floor is a PERCENTAGE of the address-space ceiling, not a fixed byte count. It
// started as a flat 384MB, which was tuned against the 2047MB ceiling and meant "act at 81%
// used". After MapleStory.exe was made LARGE_ADDRESS_AWARE the ceiling became 4095MB and the
// same 384MB silently became "act at 91% used" -- so the trigger sat idle through the whole
// climb and the client drifted to 93% (3841MB) with largestFree down at 16MB, instead of being
// held back early. Same constant, opposite behavior, purely because the ceiling moved.
//
// As a percentage it tracks whatever ceiling the process actually has: ~410MB at 2GB (close to
// the old behavior, so nothing regresses if LAA is ever reverted) and ~820MB at 4GB.
int resManPressureFreePct = 20;

// Ceiling never changes for the life of the process, so resolve it once.
static unsigned int GetAddressSpaceMB() {
    static unsigned int s_uCeilingMB = 0;
    if (s_uCeilingMB == 0) {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        const uintptr_t uSpan = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress)
                - reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
        s_uCeilingMB = static_cast<unsigned int>(uSpan / (1024 * 1024));
    }
    return s_uCeilingMB;
}

// Do NOT add gain-based backoff here. Under sustained pressure each emergency flush reports
// availVirtual barely moving (observed 13:17:20-13:17:33: 164MB -> 164MB, twice a second), which
// reads as "this is doing nothing" and is wrong. It is a treadmill: the flush frees views that
// are immediately reconsumed, and the win only shows up in the aggregate. Same boss entry, with
// and without it:
//     crashed  13:09:38  priv 960MB  mapped 681MB  used 2009MB (98%)  largestFree 0MB
//     survived 13:17:24  priv 1046MB mapped 463MB  used 1875MB (91%)  largestFree 7MB
// Private bytes were HIGHER on the run that lived; the flush held mapped 218MB lower and that is
// the whole margin. Standing down when a single flush looks ineffective gives that margin back.
//
// Only the log volume needed fixing: one line per flush at 2/s buried the log, so they are
// aggregated into a periodic summary instead.
int resManEmergencyLogEveryMs = 10000;

static DWORD s_tLastFlush = 0;
static DWORD s_tLastPressureCheck = 0;
static DWORD s_tLastEmergencyLog = 0;
static unsigned int s_nEmergencyCount = 0;
static unsigned int s_uEmergencyLowMB = 0;

// Cheap headroom probe. Returns free virtual address space in MB, or 0 on failure.
static unsigned int GetAvailVirtualMB() {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) {
        return 0;
    }
    return static_cast<unsigned int>(ms.ullAvailVirtual / (1024ull * 1024ull));
}

void ResMan_FlushTick() {
    if (!resManFlushEnabled) {
        return;
    }

    const DWORD tNow = GetTickCount();

    // Pressure path first: it must be able to pre-empt the ordinary interval entirely.
    bool bEmergency = false;
    if (tNow - s_tLastPressureCheck >= static_cast<DWORD>(resManPressureCheckMs)) {
        s_tLastPressureCheck = tNow;
        const unsigned int uFloorMB = (GetAddressSpaceMB() * resManPressureFreePct) / 100;
        const unsigned int uAvailMB = GetAvailVirtualMB();

        // Logged once so the effective floor is visible: it is derived, and the whole reason this
        // is proportional is that a hardcoded one silently changed meaning when the ceiling moved.
        static bool s_bLoggedFloor = false;
        if (!s_bLoggedFloor) {
            s_bLoggedFloor = true;
            LogInfo("ResMan pressure floor: %uMB (%d%% of %uMB address space)",
                    uFloorMB, resManPressureFreePct, GetAddressSpaceMB());
        }

        if (uAvailMB != 0 && uAvailMB < uFloorMB) {
            bEmergency = true;
        }
    }

    if (!bEmergency && s_tLastFlush != 0
            && tNow - s_tLastFlush < static_cast<DWORD>(resManFlushIntervalMs)) {
        return;
    }
    s_tLastFlush = tNow;

    // Runs from the frame tick, which starts before InitializeResMan on the very first frames.
    IWzResManPtr& rm = get_rm();
    if (!rm) {
        return;
    }

    const int nUnusedMs = bEmergency ? resManEmergencyUnusedMs : resManFlushUnusedMs;

    // Measured on both sides, because private bytes alone badly under-report what this call does.
    // Observed 13:05:08: a flush that moved private only 1159MB -> 1105MB took mapped 501MB ->
    // 469MB, and an earlier one at 12:55:45 took largestFree from 1MB back to 56MB with holes
    // 892 -> 466 while freeing just 248KB of private bytes. The payoff is address-space
    // defragmentation, not raw byte count, so log the contiguity numbers.
    // Under pressure this can fire every resManPressureCheckMs, and MemStat_GetBrief walks the
    // whole VA map -- far too expensive to do twice per flush at that rate. Emergency flushes get
    // the cheap probe instead; the full picture still lands on the next MEMSTAT tick.
    if (bEmergency) {
        const unsigned int uBeforeMB = GetAvailVirtualMB();
        try {
            rm->FlushCachedObjects(nUnusedMs);
        } catch (const _com_error& e) {
            LogInfo("ResMan_FlushTick: emergency FlushCachedObjects failed hr=0x%08X", e.Error());
            return;
        }
        const unsigned int uAfterMB = GetAvailVirtualMB();

        // Aggregate rather than one line per flush: under pressure this runs twice a second and
        // the individual deltas are meaningless anyway (see the treadmill note above). What is
        // worth recording is how long pressure lasted and how low headroom actually got.
        ++s_nEmergencyCount;
        if (s_nEmergencyCount == 1 || uAfterMB < s_uEmergencyLowMB) {
            s_uEmergencyLowMB = uAfterMB;
        }
        if (tNow - s_tLastEmergencyLog >= static_cast<DWORD>(resManEmergencyLogEveryMs)) {
            s_tLastEmergencyLog = tNow;
            LogInfo("ResMan flush EMERGENCY(unused>%dms): %u flushes, availVirtual now %uMB, low %uMB",
                    nUnusedMs, s_nEmergencyCount, uAfterMB, s_uEmergencyLowMB);
            s_nEmergencyCount = 0;
            s_uEmergencyLowMB = uAfterMB;
        }
        return;
    }

    MemBrief before{};
    MemBrief after{};
    MemStat_GetBrief(before);
    try {
        rm->FlushCachedObjects(nUnusedMs);
    } catch (const _com_error& e) {
        LogInfo("ResMan_FlushTick: FlushCachedObjects failed hr=0x%08X", e.Error());
        return;
    }
    MemStat_GetBrief(after);

    LogInfo("ResMan flush(unused>%dms): priv %uMB->%uMB mapped %uMB->%uMB largestFree %uMB->%uMB holes %u->%u",
            nUnusedMs,
            before.uPrivateMB, after.uPrivateMB,
            before.uMappedMB, after.uMappedMB,
            before.uLargestFreeMB, after.uLargestFreeMB,
            before.nHoles, after.nHoles);
}


void AttachResManMod() {
    ATTACH_HOOK(CWvsApp::InitializeResMan, CWvsApp::InitializeResMan_hook);
    ATTACH_HOOK(CWvsApp::CleanUp, CWvsApp::CleanUp_hook);
}