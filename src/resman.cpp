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


void AttachResManMod() {
    ATTACH_HOOK(CWvsApp::InitializeResMan, CWvsApp::InitializeResMan_hook);
    ATTACH_HOOK(CWvsApp::CleanUp, CWvsApp::CleanUp_hook);
}