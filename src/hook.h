#pragma once
#include "debug.h"
#include <cstdint>
#include <type_traits>

uintptr_t GetImageDelta();

inline uintptr_t Rebase(uintptr_t addr) {
    return addr + GetImageDelta();
}

#define REBASE(ADDR) Rebase(static_cast<uintptr_t>(ADDR))

#ifdef _DEBUG
#define ATTACH_HOOK(TARGET, DETOUR) \
    ((TARGET) = (decltype(TARGET))(Rebase((uintptr_t)(TARGET))), \
     AttachHook(reinterpret_cast<void**>(&TARGET), CastHook(&DETOUR)) ? true : (ErrorMessage("Failed to attach detour function \"%s\" at target address : 0x%08X.", #DETOUR, TARGET), false))
#else
#define ATTACH_HOOK(TARGET, DETOUR) \
    ((TARGET) = (decltype(TARGET))(Rebase((uintptr_t)(TARGET))), \
     AttachHook(reinterpret_cast<void**>(&TARGET), CastHook(&DETOUR)))
#endif

#define MEMBER_AT(T, OFFSET, NAME) \
    __declspec(property(get = get_##NAME, put = set_##NAME)) T NAME; \
    __forceinline const T& get_##NAME() const { \
        return *reinterpret_cast<const T*>(reinterpret_cast<uintptr_t>(this) + OFFSET); \
    } \
    __forceinline T& get_##NAME() { \
        return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(this) + OFFSET); \
    } \
    __forceinline void set_##NAME(const T& value) { \
        *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(this) + OFFSET) = const_cast<T&>(value); \
    } \
    __forceinline void set_##NAME(T& value) { \
        *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(this) + OFFSET) = value; \
    }

#define MEMBER_ARRAY_AT(T, OFFSET, NAME, N) \
    __declspec(property(get = get_##NAME)) T(&NAME)[N]; \
    __forceinline T(&get_##NAME())[N] { \
        return *reinterpret_cast<T(*)[N]>(reinterpret_cast<uintptr_t>(this) + OFFSET); \
    }

#define MEMBER_HOOK(T, ADDRESS, NAME, ...) \
    inline static auto NAME = reinterpret_cast<T(__thiscall*)(void*, __VA_ARGS__)>(Rebase(static_cast<uintptr_t>(ADDRESS))); \
    T NAME##_hook(__VA_ARGS__);

#define TO_UINTPTR(VALUE) ((uintptr_t)(VALUE))

#define TO_PVOID(VALUE) ((void*)(VALUE))


// called in injector.cpp -> DllMain
void AttachSystemHooks();

// called in system.cpp -> SetUnhandledExceptionFilter_hook
void AttachClientBypass();
void AttachClientInlink();
void AttachStringPoolMod();
void AttachResManMod();
void AttachAvatarDataMod();
void AttachItemEffectMod();
void AttachCashWeaponMod();
void AttachResolutionMod();
void AttachMobHpTagMod();
void AttachToolTipMod();
void AttachIconIconMod();
void AttachTempStatMod();
void AttachSkillEdits();
void AttachOtherHooks();
void InitExpOverride();
void PacketHooks();
void AttachMapObjectFade();
void BGMOverride();
void AttachBagWindowMod();
void BagWindow_OnLeaveField();   // storagebag.cpp — close the bag window on logout/stage exit
void AttachMemStat();            // memstat.cpp — samples memory around CField::Init (map change)

// Per-frame ticks, driven from CWvsApp::CallUpdate_hook. Both self-throttle on GetTickCount, so
// calling them every frame is cheap; the intervals live next to their implementations.
void ResMan_FlushTick();         // resman.cpp  — periodic IWzResMan::FlushCachedObjects
void MemStat_Tick();             // memstat.cpp — periodic memory sample
void MemStat_LogNow(const char* pszReason);
SIZE_T MemStat_GetPrivateBytes();

// Compact VA snapshot. largestFree/holes are the fragmentation signal -- a flush can leave
// private bytes almost unchanged while restoring tens of MB of contiguous address space.
struct MemBrief {
    unsigned int uPrivateMB;
    unsigned int uMappedMB;
    unsigned int uLargestFreeMB;
    unsigned int nHoles;
};
void MemStat_GetBrief(MemBrief& out);


#define LOGGED_STEP(CALL) do { LogInfo("AttachClientHooks: -> " #CALL); CALL; LogInfo("AttachClientHooks: <- " #CALL); } while (0)

inline void AttachClientHooks() {
    //AllocConsole();
    FILE* fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    freopen_s(&fDummy, "CONIN$", "r", stdin);
    setvbuf(stdout, nullptr, _IONBF, 0);
    LogInfo("AttachClientHooks: begin, image delta=0x%08X", GetImageDelta());
    (AttachClientBypass());
    //AttachClientInlink();
    (AttachStringPoolMod());
    (AttachResManMod());
    (AttachAvatarDataMod());
    (AttachItemEffectMod());
    (AttachCashWeaponMod());
    (AttachResolutionMod());
    (AttachMobHpTagMod());
    (AttachToolTipMod());
    (AttachIconIconMod());
    (AttachTempStatMod());
    (AttachSkillEdits());
    (AttachOtherHooks());
    (InitExpOverride());
    (PacketHooks());
    (AttachMapObjectFade());
    (BGMOverride());
    (AttachBagWindowMod());
    (AttachMemStat());
}
template <typename T>
constexpr auto CastHook(T fn) -> void* {
    union {
        T fn;
        void* p;
    } u;
    u.fn = fn;
    return u.p;
}

bool AttachHook(void** ppTarget, void* pDetour);

void* VMTHook(void* pInstance, void* pDetour, size_t uIndex);

void* GetAddress(const char* sModuleName, const char* sProcName);

void* GetAddressByPattern(const char* sModuleName, const char* sPattern);

void PatchMemory(void* pAddress, void* pValue, size_t uSize);

void PatchAllByPattern(void* pStart, void* pEnd, const char* sPattern, void* pValue, size_t uSize);


template <typename T>
void Patch1(T pAddress, unsigned char uValue) {
    PatchMemory(TO_PVOID(Rebase(TO_UINTPTR(pAddress))), &uValue, sizeof(uValue));
}

template <typename T>
void Patch4(T pAddress, unsigned int uValue) {
    PatchMemory(TO_PVOID(Rebase(TO_UINTPTR(pAddress))), &uValue, sizeof(uValue));
}

template <typename T>
void PatchStr(T pAddress, const char* sValue) {
    PatchMemory(TO_PVOID(Rebase(TO_UINTPTR(pAddress))), TO_PVOID(sValue), strlen(sValue));
}

template <typename T>
void PatchNop(T pAddress, size_t uCount) {
    void* pValue = malloc(uCount);
    if (!pValue) return;
    memset(pValue, 0x90, uCount);
    PatchMemory(TO_PVOID(Rebase(TO_UINTPTR(pAddress))), pValue, uCount);
    free(pValue);
}

// E9/E8 rel32 encode a SIGNED 32-bit displacement, so the target has to sit within +-2GB of the
// patch site. Today that is automatic: without LARGE_ADDRESS_AWARE the entire address space is
// below 0x80000000, so any DLL base is within 2GB of the client image at 0x00400000.
//
// Under LAA that guarantee disappears. The address space runs to 0xFFFEFFFF, and if ASLR ever
// relocates MapleNight.dll above ~0x80400000 every one of these patches would silently encode a
// wrapped displacement -- each hooked client function jumping to a wild address, intermittently,
// on whichever machines drew an unlucky base. The linker pins the DLL to 0x10000000
// (/DYNAMICBASE:NO) so this cannot happen; this check exists so that if the base ever moves
// again the failure is a log line instead of an unexplained crash.
inline bool CheckRel32(uintptr_t uFrom, uintptr_t uTo, const char* pszWhat) {
    const long long llRel =
            static_cast<long long>(uTo) - static_cast<long long>(uFrom) - 5ll;
    if (llRel < INT32_MIN || llRel > INT32_MAX) {
        LogInfo("%s: rel32 OUT OF RANGE (0x%08X -> 0x%08X, delta %lld) -- patch SKIPPED",
                pszWhat, uFrom, uTo, llRel);
        return false;
    }
    return true;
}

template <typename T, typename U>
void PatchJmp(T pAddress, U pDestination) {
    uintptr_t uAddress = Rebase(TO_UINTPTR(pAddress));
    if (!CheckRel32(uAddress, TO_UINTPTR(pDestination), "PatchJmp")) {
        return;
    }
    unsigned char bOp = 0xE9;
    unsigned int uRel = TO_UINTPTR(pDestination) - uAddress - 5;
    PatchMemory(TO_PVOID(uAddress), &bOp, sizeof(bOp));
    PatchMemory(TO_PVOID(uAddress + 1), &uRel, sizeof(uRel));
}

template <typename T, typename U>
void PatchCall(T pAddress, U pDestination, size_t uSize = 5) {
    if (uSize < 5) {
        ErrorMessage("Cannot PatchCall at 0x%08X with uSize = %d", TO_UINTPTR(pAddress), uSize);
        return;
    }
    uintptr_t uAddress = Rebase(TO_UINTPTR(pAddress));
    if (!CheckRel32(uAddress, TO_UINTPTR(pDestination), "PatchCall")) {
        return;
    }
    unsigned char bOp = 0xE8;
    unsigned int uRel = TO_UINTPTR(pDestination) - uAddress - 5;
    PatchMemory(TO_PVOID(uAddress), &bOp, sizeof(bOp));
    PatchMemory(TO_PVOID(uAddress + 1), &uRel, sizeof(uRel));
    if (uSize > 5) {
        void* pNop = malloc(uSize - 5);
        if (pNop) {
            memset(pNop, 0x90, uSize - 5);
            PatchMemory(TO_PVOID(uAddress + 5), pNop, uSize - 5);
            free(pNop);
        }
    }
}

template <typename T>
void PatchRetZero(T pAddress) {
    const char sBytes[3] = { '\x33', '\xC0', '\xC3' };
    PatchMemory(TO_PVOID(Rebase(TO_UINTPTR(pAddress))), TO_PVOID(sBytes), sizeof(sBytes));
}