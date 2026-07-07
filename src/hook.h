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

template <typename T, typename U>
void PatchJmp(T pAddress, U pDestination) {
    uintptr_t uAddress = Rebase(TO_UINTPTR(pAddress));
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