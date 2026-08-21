#include "pch.h"
#include "hook.h"
#include "debug.h"
#include <windows.h>
#include <detours.h>
#include <psapi.h>


uintptr_t GetImageDelta() {
    static const uintptr_t delta = []() -> uintptr_t {
        HMODULE hModule = GetModuleHandleA(nullptr);
        if (!hModule) return 0;
        auto pDos = reinterpret_cast<PIMAGE_DOS_HEADER>(hModule);
        if (pDos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        auto pNt = reinterpret_cast<PIMAGE_NT_HEADERS>(
            reinterpret_cast<uintptr_t>(hModule) + pDos->e_lfanew);
        if (pNt->Signature != IMAGE_NT_SIGNATURE) return 0;
        uintptr_t uActual = reinterpret_cast<uintptr_t>(hModule);
        uintptr_t uDefault = static_cast<uintptr_t>(pNt->OptionalHeader.ImageBase);
        uintptr_t uDelta = uActual - uDefault;
        LogInfo("Image base: 0x%08X (default 0x%08X), delta 0x%08X",
            uActual, uDefault, uDelta);
        return uDelta;
    }();
    return delta;
}


namespace {

bool HexCharToByte(char c, unsigned char* b) {
    if ('0' <= c && c <= '9')
        *b = c - '0';
    else if ('A' <= c && c <= 'F')
        *b = 10 + (c - 'A');
    else if ('a' <= c && c <= 'f')
        *b = 10 + (c - 'a');
    else
        return false;
    return true;
}

size_t ParsePattern(const char* sPattern, unsigned char* abPattern, unsigned char* abMask) {
    size_t i = 0;
    while (*sPattern) {
        if (*sPattern == ' ') {
            sPattern++;
            continue;
        }
        if (sPattern[0] == '?' && sPattern[1] == '?') {
            abMask[i] = 0x00;
        } else {
            unsigned char high, low;
            if (!HexCharToByte(sPattern[0], &high) || !HexCharToByte(sPattern[1], &low)) {
                return 0;
            }
            abPattern[i] = (high << 4) | low;
            abMask[i] = 0xFF;
        }
        sPattern += 2;
        i += 1;
    }
    return i;
}

void* FindPattern(unsigned char* pModuleBase, size_t uModuleSize, unsigned char* abPattern, unsigned char* abMask, size_t uPatternSize) {
    if (uModuleSize < uPatternSize) {
        return nullptr;
    }
    for (size_t i = 0; i <= uModuleSize - uPatternSize; ++i) {
        size_t j;
        for (j = 0; j < uPatternSize; ++j) {
            if ((pModuleBase[i + j] & abMask[j]) != (abPattern[j] & abMask[j])) {
                break;
            }
        }
        if (j == uPatternSize) {
            return &pModuleBase[i];
        }
    }
    return nullptr;
}

} // namespace


bool AttachHook(void** ppTarget, void* pDetour) {
    LONG result;
    if (result = DetourTransactionBegin(); result != NO_ERROR) {
        DEBUG_MESSAGE("DetourTransactionBegin failed with : %d", result);
        return false;
    }
    if (result = DetourUpdateThread(GetCurrentThread()); result != NO_ERROR) {
        DEBUG_MESSAGE("DetourUpdateThread failed with : %d", result);
        DetourTransactionAbort();
        return false;
    }
    if (result = DetourAttach(ppTarget, pDetour); result != NO_ERROR) {
        DEBUG_MESSAGE("DetourAttach failed with : %d", result);
        DetourTransactionAbort();
        return false;
    }
    if (result = DetourTransactionCommit(); result != NO_ERROR) {
        DEBUG_MESSAGE("DetourTransactionCommit failed with : %d", result);
        DetourTransactionAbort();
        return false;
    }
    return true;
}

void* VMTHook(void* pInstance, void* pDetour, size_t uIndex) {
    void** vtable = *static_cast<void***>(pInstance);
    void* pTarget = vtable[uIndex];
    AttachHook(&pTarget, pDetour);
    return pTarget;
}

// NOTE: almost every caller is a namespace-scope static, so this runs during DLL load -- inside
// the loader lock, before DllMain returns. It therefore must NOT show a modal dialog:
// MessageBoxA there pumps messages under the loader lock (deadlock risk), and under Wine the box
// can land behind the client or off-screen, which looks exactly like "the game will not start".
//
// A missing export is also an EXPECTED outcome on Wine, not an error: MSWSOCK!WSPStartup in
// particular is documented right above WSPStartup_orig as something Wine may not provide. Every
// call site already null-checks and degrades. So log it and return null.
void* GetAddress(const char* sModuleName, const char* sProcName) {
    HMODULE hModule = GetModuleHandleA(sModuleName);
    if (!hModule) {
        hModule = LoadLibraryA(sModuleName);
        if (!hModule) {
            LogInfo("GetAddress: could not load library %s (error %lu)", sModuleName, GetLastError());
            return nullptr;
        }
    }
    FARPROC result = GetProcAddress(hModule, sProcName);
    if (!result) {
        LogInfo("GetAddress: %s!%s not found", sModuleName, sProcName);
    }
    return reinterpret_cast<void*>(result);
}

// Same reasoning as GetAddress: no modal dialogs on a path that can run during DLL load, and a
// pattern that does not match is an ordinary outcome under Wine (see the GR2D note in
// resolution.cpp, where the scan comes back empty).
void* GetAddressByPattern(const char* sModuleName, const char* sPattern) {
    HMODULE hModule = GetModuleHandleA(sModuleName);
    if (!hModule) {
        hModule = LoadLibraryA(sModuleName);
        if (!hModule) {
            LogInfo("GetAddressByPattern: could not load library %s (error %lu)", sModuleName, GetLastError());
            return nullptr;
        }
    }
    MODULEINFO mi;
    if (!GetModuleInformation(GetCurrentProcess(), hModule, &mi, sizeof(mi))) {
        LogInfo("GetAddressByPattern: no module information for %s", sModuleName);
        return nullptr;
    }
    unsigned char* pModuleBase = static_cast<unsigned char*>(mi.lpBaseOfDll);
    size_t uModuleSize = mi.SizeOfImage;

    unsigned char abPattern[1024];
    unsigned char abMask[1024];
    size_t uPatternSize = ParsePattern(sPattern, abPattern, abMask);
    if (uPatternSize == 0) {
        LogInfo("GetAddressByPattern: could not parse pattern \"%s\"", sPattern);
        return nullptr;
    }

    void* pAddress = FindPattern(pModuleBase, uModuleSize, abPattern, abMask, uPatternSize);
    if (!pAddress) {
        LogInfo("GetAddressByPattern: pattern \"%s\" not found in %s", sPattern, sModuleName);
    }
    return pAddress;
}

void PatchMemory(void* pAddress, void* pValue, size_t uSize) {
    DWORD flOldProtect;
    if (!VirtualProtect(pAddress, uSize, PAGE_EXECUTE_READWRITE, &flOldProtect))
        return;
    memcpy(pAddress, pValue, uSize);
    VirtualProtect(pAddress, uSize, flOldProtect, &flOldProtect);
}

void PatchAllByPattern(void* pStart, void* pEnd, const char* sPattern, void* pValue, size_t uSize) {
    unsigned char abPattern[1024];
    unsigned char abMask[1024];
    size_t uPatternSize = ParsePattern(sPattern, abPattern, abMask);
    if (uPatternSize == 0) {
        LogInfo("PatchAllByPattern: could not parse pattern \"%s\"", sPattern);
        return;
    }

    unsigned char* pCurrent = static_cast<unsigned char*>(pStart);
    while (pCurrent < pEnd) {
        size_t uRemainSize = reinterpret_cast<uintptr_t>(pEnd) - reinterpret_cast<uintptr_t>(pCurrent);
        void* pTarget = FindPattern(pCurrent, uRemainSize, abPattern, abMask, uPatternSize);
        if (!pTarget) {
            break;
        }
        PatchMemory(pTarget, pValue, uSize);
        pCurrent += uSize;
    }
}
