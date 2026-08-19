// Memory telemetry.
//
// The client is a 32-bit process with no LARGE_ADDRESS_AWARE flag (MapleStory.exe
// Characteristics=0x010F), so it has a hard 2 GB user address space. Data/ is ~20 GB of WZ on
// disk, and IWzResMan is configured to retain parsed objects forever -- CWvsApp::InitializeResMan
// passes SetResManParam(RC_AUTO_REPARSE | RC_AUTO_SERIALIZE, -1, -1), and the only eviction the
// vanilla client performs is FlushCachedObjects(180000) from CField::Init @0x529326, i.e. on map
// change only. A long fight on one map therefore never evicts anything.
//
// Two different things can kill the process under that setup and they need opposite fixes:
//   - running out of COMMIT (private bytes approach 2 GB)          -> flush harder, and/or LAA
//   - running out of contiguous ADDRESS SPACE while commit is low  -> fragmentation; LAA alone
//                                                                     will not save you
// Telling them apart needs the largest-free-VA-block number, not just a byte count, which is why
// this samples the VA map rather than only calling GetProcessMemoryInfo.

#include "pch.h"
#include "hook.h"
#include "debug.h"
#include "wvs/util.h"

#pragma comment(lib, "psapi.lib")


// Sampling cadence. 10s is frequent enough to catch a fight ramping up without adding meaningful
// log volume (the VA walk is a few thousand VirtualQuery calls, well under a millisecond).
int memStatIntervalMs = 10000;
int memStatEnabled = 1;

static DWORD s_tLastSample = 0;
static bool s_bLoggedAddressSpace = false;


struct VaSummary {
    SIZE_T uCommittedPrivate;  // MEM_COMMIT + MEM_PRIVATE -- heap, stacks, our own allocations
    SIZE_T uCommittedImage;    // MEM_COMMIT + MEM_IMAGE  -- loaded modules, roughly fixed
    SIZE_T uCommittedMapped;   // MEM_COMMIT + MEM_MAPPED -- file mapping views, i.e. WZ data
    SIZE_T uReserved;          // MEM_RESERVE without commit -- address space held but unbacked
    SIZE_T uFree;              // total unallocated
    SIZE_T uLargestFree;       // biggest single contiguous free run: the fragmentation signal
    unsigned int nFreeRegions; // how shattered the free space is
    unsigned int nRegions;
};

// Walks the whole user-mode VA range. uLargestFree is the number that matters: an allocation of
// N bytes fails once no free run of N survives, regardless of how much total free there is.
static void SampleVirtualAddressSpace(VaSummary& out) {
    ZeroMemory(&out, sizeof(out));

    SYSTEM_INFO si{};
    GetSystemInfo(&si);

    auto pAddress = static_cast<const unsigned char*>(si.lpMinimumApplicationAddress);
    const auto pMax = static_cast<const unsigned char*>(si.lpMaximumApplicationAddress);

    MEMORY_BASIC_INFORMATION mbi{};
    while (pAddress < pMax && VirtualQuery(pAddress, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        ++out.nRegions;
        if (mbi.State == MEM_FREE) {
            out.uFree += mbi.RegionSize;
            ++out.nFreeRegions;
            if (mbi.RegionSize > out.uLargestFree) {
                out.uLargestFree = mbi.RegionSize;
            }
        } else if (mbi.State == MEM_COMMIT) {
            // Image vs mapped matters: image is loaded modules and is basically constant, while
            // mapped is file-mapping views -- the WZ data. Only the latter is actionable.
            if (mbi.Type == MEM_PRIVATE) {
                out.uCommittedPrivate += mbi.RegionSize;
            } else if (mbi.Type == MEM_IMAGE) {
                out.uCommittedImage += mbi.RegionSize;
            } else {
                out.uCommittedMapped += mbi.RegionSize;
            }
        } else { // MEM_RESERVE
            out.uReserved += mbi.RegionSize;
        }

        const auto pNext = static_cast<const unsigned char*>(mbi.BaseAddress) + mbi.RegionSize;
        if (pNext <= pAddress) {
            break; // defensive: never spin if VirtualQuery reports a zero/wrapped region
        }
        pAddress = pNext;
    }
}

static unsigned int ToMB(SIZE_T uBytes) {
    return static_cast<unsigned int>(uBytes / (1024 * 1024));
}

// Compact snapshot for before/after comparisons. Private bytes alone are misleading around a
// resman flush: the flush's main effect is unmapping WZ views, which barely moves private bytes
// but can restore tens of MB of contiguous address space. largestFree/holes are what show that.
void MemStat_GetBrief(MemBrief& out) {
    VaSummary va{};
    SampleVirtualAddressSpace(va);
    out.uPrivateMB = ToMB(va.uCommittedPrivate);
    out.uMappedMB = ToMB(va.uCommittedMapped);
    out.uLargestFreeMB = ToMB(va.uLargestFree);
    out.nHoles = va.nFreeRegions;
}

// Private bytes only -- the single number worth comparing before/after a resman flush.
SIZE_T MemStat_GetPrivateBytes() {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        return 0;
    }
    return pmc.PrivateUsage;
}

void MemStat_LogNow(const char* pszReason) {
    // Log the address-space ceiling once. lpMaximumApplicationAddress is how the process reports
    // whether LARGE_ADDRESS_AWARE actually took effect: 0x7FFEFFFF without it, ~0xFFFEFFFF with
    // it on 64-bit Windows. After flipping the PE flag, check this line to confirm it applied.
    if (!s_bLoggedAddressSpace) {
        s_bLoggedAddressSpace = true;
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        const auto uMax = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);
        LogInfo("MEMSTAT addrspace: max=0x%08X (%u MB) largeAddressAware=%d",
                uMax, static_cast<unsigned int>(uMax / (1024 * 1024)), uMax > 0x7FFEFFFFu ? 1 : 0);
    }

    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        LogInfo("MEMSTAT [%s] private=%uMB workingSet=%uMB peakWorkingSet=%uMB pagefile=%uMB faults=%lu",
                pszReason ? pszReason : "tick",
                ToMB(pmc.PrivateUsage), ToMB(pmc.WorkingSetSize), ToMB(pmc.PeakWorkingSetSize),
                ToMB(pmc.PagefileUsage), pmc.PageFaultCount);
    }

    VaSummary va{};
    SampleVirtualAddressSpace(va);

    // used/limit is the headroom number. largestFree is the one that actually kills you: an
    // allocation of N bytes fails as soon as no single free run of N survives, no matter how much
    // total free is left.
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const SIZE_T uLimit = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress)
            - reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    const SIZE_T uUsed = va.uCommittedPrivate + va.uCommittedImage + va.uCommittedMapped + va.uReserved;

    // Percentage is computed on the MB values, not the byte values: SIZE_T is 32-bit here, so
    // uUsed * 100 overflows well before 2 GB and the ratio comes out as noise.
    const unsigned int uUsedMB = ToMB(uUsed);
    const unsigned int uLimitMB = ToMB(uLimit);
    LogInfo("MEMSTAT [%s] va: priv=%uMB image=%uMB mapped=%uMB reserved=%uMB | used=%uMB/%uMB (%u%%)",
            pszReason ? pszReason : "tick",
            ToMB(va.uCommittedPrivate), ToMB(va.uCommittedImage), ToMB(va.uCommittedMapped),
            ToMB(va.uReserved), uUsedMB, uLimitMB,
            uLimitMB ? (uUsedMB * 100) / uLimitMB : 0);
    LogInfo("MEMSTAT [%s] free: total=%uMB largestFree=%uMB holes=%u regions=%u",
            pszReason ? pszReason : "tick",
            ToMB(va.uFree), ToMB(va.uLargestFree), va.nFreeRegions, va.nRegions);
}

void MemStat_Tick() {
    if (!memStatEnabled) {
        return;
    }
    const DWORD tNow = GetTickCount();
    if (s_tLastSample != 0 && tNow - s_tLastSample < static_cast<DWORD>(memStatIntervalMs)) {
        return;
    }
    s_tLastSample = tNow;
    MemStat_LogNow("tick");
}


// Map change. CField::Init is where the vanilla client does its one and only
// FlushCachedObjects(180000), so sampling on both sides of it shows how much that call actually
// reclaims -- and, over a session, how much each map load adds that never comes back.
auto CField__Init = (void(__thiscall*)(void*, void*))0x00529269;
void __fastcall CField__Init_hook(void* pThis, void* edx, void* pParam) {
    MemStat_LogNow("field-init-before");
    CField__Init(pThis, pParam);
    MemStat_LogNow("field-init-after");
}


void AttachMemStat() {
    ATTACH_HOOK(CField__Init, CField__Init_hook);
}
