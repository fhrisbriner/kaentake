#include "pch.h"
#include "hook.h"
#include "constants.h"
#include "debug.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <strsafe.h>
#include <ws2spi.h>
#include <ws2tcpip.h>
#include <intrin.h>

#pragma comment(lib, "ws2_32.lib")


typedef decltype(&SetUnhandledExceptionFilter) SetUnhandledExceptionFilter_t;
static SetUnhandledExceptionFilter_t SetUnhandledExceptionFilter_orig = reinterpret_cast<SetUnhandledExceptionFilter_t>(GetAddress("KERNEL32", "SetUnhandledExceptionFilter"));

// Crash logger. The client installs its own top-level filter (and under Wine the process just
// disappears), so a fault leaves nothing behind but a truncated log. This vectored handler runs
// on the *second* chance only -- i.e. after every SEH frame declined to handle it, which is the
// crash proper -- and writes the faulting address, the module it lands in, and the registers.
static LONG CALLBACK CrashLogHandler(EXCEPTION_POINTERS* pEx) {
    if (!pEx || !pEx->ExceptionRecord || !pEx->ContextRecord) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const DWORD code = pEx->ExceptionRecord->ExceptionCode;
    // Only report the ones that actually kill the process. The client throws C++ exceptions
    // (0xE06D7363) and uses SEH liberally during normal play; logging those is just noise.
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION
            && code != EXCEPTION_PRIV_INSTRUCTION && code != EXCEPTION_STACK_OVERFLOW
            && code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_IN_PAGE_ERROR) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    void* addr = pEx->ExceptionRecord->ExceptionAddress;
    char sModule[MAX_PATH] = "<unknown>";
    uintptr_t offset = 0;
    HMODULE hMod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(addr), &hMod) && hMod) {
        char sPath[MAX_PATH]{};
        if (GetModuleFileNameA(hMod, sPath, MAX_PATH)) {
            const char* leaf = strrchr(sPath, '\\');
            StringCchCopyA(sModule, MAX_PATH, leaf ? leaf + 1 : sPath);
        }
        offset = reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(hMod);
    }

    LogInfo("*** CRASH *** code=0x%08lX at 0x%08X (%s+0x%X)",
            code, reinterpret_cast<uintptr_t>(addr), sModule, offset);
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) {
        // ExceptionInformation[0]: 0 read, 1 write, 8 DEP. [1]: the address touched.
        LogInfo("*** CRASH *** %s access to 0x%08X",
                pEx->ExceptionRecord->ExceptionInformation[0] == 1 ? "write" :
                pEx->ExceptionRecord->ExceptionInformation[0] == 8 ? "execute" : "read",
                pEx->ExceptionRecord->ExceptionInformation[1]);
    }
    const CONTEXT* c = pEx->ContextRecord;
    LogInfo("*** CRASH *** EIP=%08X ESP=%08X EBP=%08X EAX=%08X EBX=%08X ECX=%08X EDX=%08X ESI=%08X EDI=%08X",
            c->Eip, c->Esp, c->Ebp, c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi);
    // Return addresses off the stack: enough to see which subsystem we died under.
    const uintptr_t* stack = reinterpret_cast<const uintptr_t*>(c->Esp);
    if (stack && !IsBadReadPtr(stack, 64 * sizeof(uintptr_t))) {
        char sLine[512];
        StringCchCopyA(sLine, sizeof(sLine), "*** CRASH *** stack:");
        int found = 0;
        for (int i = 0; i < 64 && found < 12; ++i) {
            const uintptr_t v = stack[i];
            // Anything inside the client image is a plausible return address.
            if (v >= 0x00400000 && v < 0x00C00000) {
                char sVal[16];
                StringCchPrintfA(sVal, sizeof(sVal), " %08X", v);
                StringCchCatA(sLine, sizeof(sLine), sVal);
                ++found;
            }
        }
        LogInfo("%s", sLine);
    }
    LogFlush(); // buffered from here on -- force the crash tail to disk before the process dies
    return EXCEPTION_CONTINUE_SEARCH; // let the normal crash path proceed
}

LPTOP_LEVEL_EXCEPTION_FILTER WINAPI SetUnhandledExceptionFilter_hook(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter) {
    // ZExceptionHandler::ZExceptionHandler - after dynamic initializers for ZAllocEx<T>::_s_alloc
    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) == 0x00796FDD) {
        // Second-chance vectored handler (last arg 0 = called after SEH), so we log the crash
        // no matter what the client's own filter does with it.
        AddVectoredContinueHandler(0, &CrashLogHandler);
        LogInfo("AttachClientHooks: crash logger installed");
        AttachClientHooks();
    }
    return SetUnhandledExceptionFilter_orig(lpTopLevelExceptionFilter);
}


typedef decltype(&CreateMutexA) CreateMutexA_t;
static CreateMutexA_t CreateMutexA_orig = reinterpret_cast<CreateMutexA_t>(GetAddress("KERNEL32", "CreateMutexA"));

HANDLE WINAPI CreateMutexA_hook(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCSTR lpName) {
    DEBUG_MESSAGE("CreateMutexA : %s", lpName);
    if (lpName && !strcmp(lpName, "WvsClientMtx")) {
        char sMutex[1024];
        sprintf_s(sMutex, 1024, "%s-%d", lpName, GetCurrentProcessId());
        lpName = sMutex;
        return CreateMutexA_orig(lpMutexAttributes, bInitialOwner, sMutex);
    }
    return CreateMutexA_orig(lpMutexAttributes, bInitialOwner, lpName);
}


typedef decltype(&CreateWindowExA) CreateWindowExA_t;
static CreateWindowExA_t CreateWindowExA_orig = reinterpret_cast<CreateWindowExA_t>(GetAddress("USER32", "CreateWindowExA"));
static WNDPROC g_WndProc;

LRESULT WndProc_hook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static POINT ptOffset;
    static bool bMoving;
    switch (msg) {
    case WM_SETCURSOR: {
        if (LOWORD(lParam) != HTCLIENT) {
            while (ShowCursor(TRUE) < 0)
                ;
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return 0;
        }
        break;
    }
    case WM_NCMOUSEMOVE:
    case WM_MOUSEMOVE:
        if (bMoving) {
            if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
                POINT ptCursor;
                GetCursorPos(&ptCursor);
                SetWindowPos(hWnd, NULL, ptCursor.x - ptOffset.x, ptCursor.y - ptOffset.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            } else {
                bMoving = false;
                ReleaseCapture();
            }
        }
        break;
    case WM_NCLBUTTONDOWN:
        if (wParam == HTMENU || wParam == HTLEFT) {
            break;
        } else if (wParam == HTCAPTION) {
            RECT rcWnd;
            POINT ptCursor;
            GetWindowRect(hWnd, &rcWnd);
            GetCursorPos(&ptCursor);
            ptOffset.x = ptCursor.x - rcWnd.left;
            ptOffset.y = ptCursor.y - rcWnd.top;
            SetCapture(hWnd);
            bMoving = true;
        }
        return 0;
    case WM_NCLBUTTONUP:
    case WM_LBUTTONUP:
        if (wParam == HTCLOSE) {
            PostQuitMessage(0);
        } else if (wParam == HTMINBUTTON && !(GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
            ShowWindow(hWnd, SW_MINIMIZE);
        }
        bMoving = false;
        ReleaseCapture();
        break;
    case WM_NCRBUTTONDOWN:
    case WM_NCRBUTTONUP:
        return 0;
    case WM_RBUTTONUP:
        if (!bMoving) {
            break;
        }
        return 0;
    }
    return CallWindowProcA(g_WndProc, hWnd, msg, wParam, lParam);
}

HWND WINAPI CreateWindowExA_hook(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam) {
    if (!lpClassName || strcmp(lpClassName, "MapleStoryClass") != 0) {
        return CreateWindowExA_orig(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
    }
    HWND hWnd = CreateWindowExA_orig(dwExStyle, lpClassName, lpWindowName, 0xCA0000, CW_USEDEFAULT, CW_USEDEFAULT, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
    g_WndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProc_hook)));
    return hWnd;
}


typedef decltype(&RegCreateKeyExA) RegCreateKeyExA_t;
static RegCreateKeyExA_t RegCreateKeyExA_orig = reinterpret_cast<RegCreateKeyExA_t>(GetAddress("ADVAPI32", "RegCreateKeyExA"));

typedef decltype(&RegOpenKeyExA) RegOpenKeyExA_t;
static RegOpenKeyExA_t RegOpenKeyExA_orig = reinterpret_cast<RegOpenKeyExA_t>(GetAddress("ADVAPI32", "RegOpenKeyExA"));

typedef decltype(&RegOpenKeyA) RegOpenKeyA_t;
static RegOpenKeyA_t RegOpenKeyA_orig = reinterpret_cast<RegOpenKeyA_t>(GetAddress("ADVAPI32", "RegOpenKeyA"));

typedef decltype(&RegCreateKeyA) RegCreateKeyA_t;
static RegCreateKeyA_t RegCreateKeyA_orig = reinterpret_cast<RegCreateKeyA_t>(GetAddress("ADVAPI32", "RegCreateKeyA"));

typedef decltype(&RegCreateKeyExW) RegCreateKeyExW_t;
static RegCreateKeyExW_t RegCreateKeyExW_orig = reinterpret_cast<RegCreateKeyExW_t>(GetAddress("ADVAPI32", "RegCreateKeyExW"));

typedef decltype(&RegOpenKeyExW) RegOpenKeyExW_t;
static RegOpenKeyExW_t RegOpenKeyExW_orig = reinterpret_cast<RegOpenKeyExW_t>(GetAddress("ADVAPI32", "RegOpenKeyExW"));

typedef decltype(&RegSetValueExA) RegSetValueExA_t;
static RegSetValueExA_t RegSetValueExA_orig = reinterpret_cast<RegSetValueExA_t>(GetAddress("ADVAPI32", "RegSetValueExA"));

static bool LooksLikeCConfigValueName(LPCSTR name) {
    if (!name || !*name) return false;
    static const char* prefixes[] = { "so", "ui", "scr", "mn", "jp", "go", "co", "si", "L", "RMA", "LMA", "LCWN", "Exec", "uiBin", "uiOpt", "uiWnd" };
    for (auto p : prefixes) {
        if (_strnicmp(name, p, strlen(p)) == 0) return true;
    }
    return false;
}

static bool SubKeyContainsWizetW(LPCWSTR lpSubKey) {
    if (!lpSubKey) return false;
    for (const wchar_t* p = lpSubKey; *p; ++p) {
        if (_wcsnicmp(p, L"Wizet", 5) == 0) return true;
    }
    return false;
}

static bool SubKeyContainsWizet(LPCSTR lpSubKey) {
    if (!lpSubKey) return false;
    for (const char* p = lpSubKey; *p; ++p) {
        if (_strnicmp(p, "Wizet", 5) == 0) return true;
    }
    return false;
}

LSTATUS WINAPI RegCreateKeyExA_hook(HKEY hKey, LPCSTR lpSubKey, DWORD Reserved, LPSTR lpClass, DWORD dwOptions, REGSAM samDesired, const LPSECURITY_ATTRIBUTES lpSecurityAttributes, PHKEY phkResult, LPDWORD lpdwDisposition) {
    bool intercept = SubKeyContainsWizet(lpSubKey);
    HKEY root = intercept ? HKEY_CURRENT_USER : hKey;
    if (intercept) {
        LogInfo("RegCreateKeyExA hKey=%p subkey='%s' sam=0x%lX (forced HKCU)", (void*)hKey, lpSubKey ? lpSubKey : "(null)", (unsigned long)samDesired);
    }
    LSTATUS status = RegCreateKeyExA_orig(root, lpSubKey, Reserved, lpClass, dwOptions, samDesired, lpSecurityAttributes, phkResult, lpdwDisposition);
    if (intercept) {
        LogInfo("RegCreateKeyExA result status=%ld disposition=%lu handle=%p", status, lpdwDisposition ? *lpdwDisposition : 0, phkResult ? (void*)*phkResult : nullptr);
    }
    return status;
}

LSTATUS WINAPI RegOpenKeyExA_hook(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult) {
    // Log every Open* — we're hunting for which API the game uses to open the Wizet config subkey.
    LogInfo("RegOpenKeyExA hKey=%p subkey='%s' sam=0x%lX", (void*)hKey, lpSubKey ? lpSubKey : "(null)", (unsigned long)samDesired);
    bool intercept = SubKeyContainsWizet(lpSubKey);
    HKEY root = intercept ? HKEY_CURRENT_USER : hKey;
    LSTATUS status = RegOpenKeyExA_orig(root, lpSubKey, ulOptions, samDesired, phkResult);
    LogInfo("RegOpenKeyExA result status=%ld handle=%p intercept=%d", status, phkResult ? (void*)*phkResult : nullptr, intercept ? 1 : 0);
    return status;
}

LSTATUS WINAPI RegOpenKeyA_hook(HKEY hKey, LPCSTR lpSubKey, PHKEY phkResult) {
    LogInfo("RegOpenKeyA hKey=%p subkey='%s'", (void*)hKey, lpSubKey ? lpSubKey : "(null)");
    bool intercept = SubKeyContainsWizet(lpSubKey);
    HKEY root = intercept ? HKEY_CURRENT_USER : hKey;
    LSTATUS status = RegOpenKeyA_orig(root, lpSubKey, phkResult);
    LogInfo("RegOpenKeyA result status=%ld handle=%p intercept=%d", status, phkResult ? (void*)*phkResult : nullptr, intercept ? 1 : 0);
    return status;
}

LSTATUS WINAPI RegCreateKeyA_hook(HKEY hKey, LPCSTR lpSubKey, PHKEY phkResult) {
    bool intercept = SubKeyContainsWizet(lpSubKey);
    HKEY root = intercept ? HKEY_CURRENT_USER : hKey;
    if (intercept) {
        LogInfo("RegCreateKeyA hKey=%p subkey='%s' (forced HKCU)", (void*)hKey, lpSubKey ? lpSubKey : "(null)");
    }
    LSTATUS status = RegCreateKeyA_orig(root, lpSubKey, phkResult);
    if (intercept) {
        LogInfo("RegCreateKeyA result status=%ld handle=%p", status, phkResult ? (void*)*phkResult : nullptr);
    }
    return status;
}

LSTATUS WINAPI RegCreateKeyExW_hook(HKEY hKey, LPCWSTR lpSubKey, DWORD Reserved, LPWSTR lpClass, DWORD dwOptions, REGSAM samDesired, const LPSECURITY_ATTRIBUTES lpSecurityAttributes, PHKEY phkResult, LPDWORD lpdwDisposition) {
    bool intercept = SubKeyContainsWizetW(lpSubKey);
    HKEY root = intercept ? HKEY_CURRENT_USER : hKey;
    if (intercept) {
        LogInfo("RegCreateKeyExW hKey=%p subkey='%ls' sam=0x%lX (forced HKCU)", (void*)hKey, lpSubKey ? lpSubKey : L"(null)", (unsigned long)samDesired);
    }
    LSTATUS status = RegCreateKeyExW_orig(root, lpSubKey, Reserved, lpClass, dwOptions, samDesired, lpSecurityAttributes, phkResult, lpdwDisposition);
    if (intercept) {
        LogInfo("RegCreateKeyExW result status=%ld disposition=%lu handle=%p", status, lpdwDisposition ? *lpdwDisposition : 0, phkResult ? (void*)*phkResult : nullptr);
    }
    return status;
}

LSTATUS WINAPI RegOpenKeyExW_hook(HKEY hKey, LPCWSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult) {
    LogInfo("RegOpenKeyExW hKey=%p subkey='%ls' sam=0x%lX", (void*)hKey, lpSubKey ? lpSubKey : L"(null)", (unsigned long)samDesired);
    bool intercept = SubKeyContainsWizetW(lpSubKey);
    HKEY root = intercept ? HKEY_CURRENT_USER : hKey;
    LSTATUS status = RegOpenKeyExW_orig(root, lpSubKey, ulOptions, samDesired, phkResult);
    LogInfo("RegOpenKeyExW result status=%ld handle=%p intercept=%d", status, phkResult ? (void*)*phkResult : nullptr, intercept ? 1 : 0);
    return status;
}

LSTATUS WINAPI RegSetValueExA_hook(HKEY hKey, LPCSTR lpValueName, DWORD Reserved, DWORD dwType, const BYTE* lpData, DWORD cbData) {
    if (LooksLikeCConfigValueName(lpValueName)) {
        DWORD valSnapshot = 0;
        if (dwType == REG_DWORD && lpData && cbData >= sizeof(DWORD)) {
            memcpy(&valSnapshot, lpData, sizeof(DWORD));
        }
        LSTATUS status = RegSetValueExA_orig(hKey, lpValueName, Reserved, dwType, lpData, cbData);
        LogInfo("RegSetValueExA handle=%p name='%s' type=%lu cbData=%lu dword=0x%lX status=%ld", (void*)hKey, lpValueName, (unsigned long)dwType, (unsigned long)cbData, (unsigned long)valSnapshot, status);
        return status;
    }
    return RegSetValueExA_orig(hKey, lpValueName, Reserved, dwType, lpData, cbData);
}


// Same probe bypass.cpp uses for its DirectSound guard (kept local: that one is static there).
static bool IsRunningUnderWine() {
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    return ntdll && GetProcAddress(ntdll, "wine_get_version") != nullptr;
}

typedef decltype(&WSPStartup) WSPStartup_t;
static WSPStartup_t WSPStartup_orig = reinterpret_cast<WSPStartup_t>(GetAddress("MSWSOCK", "WSPStartup"));
static WSPPROC_TABLE g_ProcTable;
static ULONG g_uNexonAddress;

constexpr const char* g_asOriginalAddress[] = {
    "63.251.217.2",
    "63.251.217.3",
    "63.251.217.4",
};

int WSPAPI WSPConnect_hook(SOCKET s, const struct sockaddr FAR* name, int namelen, LPWSABUF lpCallerData, LPWSABUF lpCalleeData, LPQOS lpSQOS, LPQOS lpGQOS, LPINT lpErrno) {
    // The saved proc table comes from whatever winsock provider the OS handed us. Under Wine
    // entries can be missing, and calling through one of those nulls crashes the client at the
    // exact moment it connects to the channel server -- i.e. on picking a character.
    if (!g_ProcTable.lpWSPConnect) {
        if (lpErrno) {
            *lpErrno = WSAEFAULT;
        }
        return SOCKET_ERROR;
    }
    if (!name || name->sa_family != AF_INET) {
        return g_ProcTable.lpWSPConnect(s, name, namelen, lpCallerData, lpCalleeData, lpSQOS, lpGQOS, lpErrno);
    }
    char sName[INET_ADDRSTRLEN];
    InetNtopA(AF_INET, &((sockaddr_in*)name)->sin_addr, sName, INET_ADDRSTRLEN);
    for (auto sAddress : g_asOriginalAddress) {
        if (strcmp(sName, sAddress)) {
            continue;
        }
        g_uNexonAddress = ((sockaddr_in*)name)->sin_addr.S_un.S_addr;
        InetPtonA(AF_INET, g_sServerHost ? g_sServerHost : CONSTANTS_DEFAULT_HOST, &((sockaddr_in*)name)->sin_addr.S_un.S_addr);
        if (g_nServerPort) {
            ((sockaddr_in*)name)->sin_port = htons(static_cast<u_short>(g_nServerPort));
        }
        break;
    }
    return g_ProcTable.lpWSPConnect(s, name, namelen, lpCallerData, lpCalleeData, lpSQOS, lpGQOS, lpErrno);
}

int WSPAPI WSPGetPeerName_hook(SOCKET s, struct sockaddr* name, LPINT namelen, LPINT lpErrNo) {
    if (!g_ProcTable.lpWSPGetPeerName) {
        if (lpErrNo) {
            *lpErrNo = WSAEFAULT;
        }
        return SOCKET_ERROR;
    }
    int result = g_ProcTable.lpWSPGetPeerName(s, name, namelen, lpErrNo);
    // Only rewrite a peer address the provider actually filled in. The old unconditional write
    // scribbled into `name` even when the call failed or the buffer was too small.
    if (result == 0 && name && name->sa_family == AF_INET && namelen && *namelen >= static_cast<int>(sizeof(sockaddr_in))
            && g_uNexonAddress) {
        ((sockaddr_in*)name)->sin_addr.S_un.S_addr = g_uNexonAddress;
    }
    return result;
}

int WSPAPI WSPStartup_hook(WORD wVersionRequested, LPWSPDATA lpWSPData, LPWSAPROTOCOL_INFOW lpProtocolInfo, WSPUPCALLTABLE UpcallTable, LPWSPPROC_TABLE lpProcTable) {
    if (!WSPStartup_orig) {
        return WSAEFAULT;
    }
    int result = WSPStartup_orig(wVersionRequested, lpWSPData, lpProtocolInfo, UpcallTable, lpProcTable);
    // Leave the provider alone unless it actually initialised. Installing our thunks over a
    // half-filled table is what makes this fatal rather than merely non-functional; connect()
    // in ws2_32 is hooked separately and already covers the redirect.
    if (result != 0 || !lpProcTable) {
        LogInfo("WSPStartup_hook: provider init failed (result=%d) - leaving proc table alone", result);
        return result;
    }
    g_ProcTable = *lpProcTable;
    if (g_ProcTable.lpWSPConnect) {
        lpProcTable->lpWSPConnect = &WSPConnect_hook;
    } else {
        LogInfo("WSPStartup_hook: provider has no WSPConnect - not hooking it");
    }
    if (g_ProcTable.lpWSPGetPeerName) {
        lpProcTable->lpWSPGetPeerName = &WSPGetPeerName_hook;
    } else {
        LogInfo("WSPStartup_hook: provider has no WSPGetPeerName - not hooking it");
    }
    return result;
}


typedef decltype(&connect) connect_t;
static connect_t connect_orig = reinterpret_cast<connect_t>(GetAddress("WS2_32", "connect"));

int WINAPI connect_hook(SOCKET s, const struct sockaddr* name, int namelen) {
    if (name && name->sa_family == AF_INET) {
        char sName[INET_ADDRSTRLEN];
        InetNtopA(AF_INET, &((sockaddr_in*)name)->sin_addr, sName, INET_ADDRSTRLEN);
        if (strstr(sName, CONFIG_NEXON_SEARCH)) {
            sockaddr_in redir = *(sockaddr_in*)name;
            g_uNexonAddress = redir.sin_addr.S_un.S_addr;
            InetPtonA(AF_INET, g_sServerHost ? g_sServerHost : CONSTANTS_DEFAULT_HOST, &redir.sin_addr.S_un.S_addr);
            if (g_nServerPort) {
                redir.sin_port = htons(static_cast<u_short>(g_nServerPort));
            }
            return connect_orig(s, (sockaddr*)&redir, sizeof(redir));
        }
    }
    return connect_orig(s, name, namelen);
}


typedef decltype(&getpeername) getpeername_t;
static getpeername_t getpeername_orig = reinterpret_cast<getpeername_t>(GetAddress("WS2_32", "getpeername"));

int WINAPI getpeername_hook(SOCKET s, struct sockaddr* name, int* namelen) {
    int result = getpeername_orig(s, name, namelen);
    if (result == 0 && name && name->sa_family == AF_INET && g_uNexonAddress) {
        ((sockaddr_in*)name)->sin_addr.S_un.S_addr = g_uNexonAddress;
    }
    return result;
}


void AttachSystemHooks() {
    LogInfo("AttachSystemHooks: attaching registry + window + WSP hooks");
    ATTACH_HOOK(SetUnhandledExceptionFilter_orig, SetUnhandledExceptionFilter_hook);
    ATTACH_HOOK(CreateMutexA_orig, CreateMutexA_hook);
    ATTACH_HOOK(CreateWindowExA_orig, CreateWindowExA_hook);
    ATTACH_HOOK(RegCreateKeyExA_orig, RegCreateKeyExA_hook);
    ATTACH_HOOK(RegOpenKeyExA_orig, RegOpenKeyExA_hook);
    ATTACH_HOOK(RegOpenKeyA_orig, RegOpenKeyA_hook);
    ATTACH_HOOK(RegCreateKeyA_orig, RegCreateKeyA_hook);
    ATTACH_HOOK(RegCreateKeyExW_orig, RegCreateKeyExW_hook);
    ATTACH_HOOK(RegOpenKeyExW_orig, RegOpenKeyExW_hook);
    ATTACH_HOOK(RegSetValueExA_orig, RegSetValueExA_hook);
    // Under Wine the WSP layer is skipped entirely. Wine's ws2_32 does not use the Windows
    // service-provider architecture the way this hook assumes, and swapping entries in the
    // proc table it hands back kills the client the moment it opens the channel-server socket
    // -- i.e. on double-clicking a character. The ws2_32 connect hook below performs the same
    // host redirect, so nothing is lost by leaving the provider alone.
    const bool bWine = IsRunningUnderWine();
    if (bWine) {
        LogInfo("AttachSystemHooks: Wine detected - skipping MSWSOCK WSP hooks (connect hook covers redirect)");
    }

    // GetAddress returns null when an export is missing (Wine's mswsock does not necessarily
    // provide WSPStartup). ATTACH_HOOK on a null target rebases the null and hands Detours a
    // garbage address, so check first and log which ones we skipped.
    if (WSPStartup_orig && !bWine) {
        ATTACH_HOOK(WSPStartup_orig, WSPStartup_hook);
    } else if (!bWine) {
        LogInfo("AttachSystemHooks: MSWSOCK!WSPStartup not found - relying on ws2_32 connect hook");
    }
    if (connect_orig) {
        ATTACH_HOOK(connect_orig, connect_hook);
    } else {
        LogInfo("AttachSystemHooks: WS2_32!connect not found - server redirect will NOT work");
    }
    if (getpeername_orig) {
        ATTACH_HOOK(getpeername_orig, getpeername_hook);
    } else {
        LogInfo("AttachSystemHooks: WS2_32!getpeername not found - skipping");
    }
}