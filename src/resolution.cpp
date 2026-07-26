#include "pch.h"
#include "hook.h"
#include "constants.h"
#include "debug.h"
#include "wvs/config.h"
#include "wvs/wnd.h"
#include "wvs/wndman.h"
#include "wvs/tooltip.h"
#include "wvs/tempstat.h"
#include "wvs/statusbar.h"
#include "wvs/ctrlwnd.h"
#include "wvs/stage.h"
#include "wvs/field.h"
#include "wvs/rtti.h"
#include "wvs/util.h"
#include "ztl/ztl.h"
#include "wvs/CWvsContext.h"

#include <windows.h>
#include <strsafe.h>
#include <intrin.h>

#define SCREEN_WIDTH_MAX     1920
#define SCREEN_HEIGHT_MAX    1080
#define SCREEN_MESSAGE_WIDTH 400


// Resolution list shown in the system options combo box. nWidth/nHeight are the *logical*
// size the game renders and lays out UI at; nScale is the pixel doubling factor, so the
// window the user sees is (nWidth * nScale) x (nHeight * nScale).
//
// Pixel doubling keeps the D3D backbuffer at the logical size and lets the windowed present
// stretch it over the larger client area — one game pixel becomes an nScale x nScale block.
// That is what makes the v83 UI usable on a high-DPI monitor: a 2x mode has the pixel density
// of its logical size, not the tiny UI a native 4K mode would give.
//
// Order matters: the index is what gets written to mnScreenResolution, so existing entries
// must keep their position and new ones get appended.
struct RESOLUTION {
    const char* sLabel;
    int nWidth;
    int nHeight;
    int nScale;
};

// 2x entries removed for now (cursor/UI issues); the pixel-doubling plumbing below stays and
// goes dormant while every entry has nScale = 1. Re-add e.g. { "1600 x 1200 (2x)", 800, 600, 2 }
// to bring a mode back — append only, existing indices are saved in mnScreenResolution.
static const RESOLUTION g_aResolution[] = {
    { "800 x 600",          800,  600, 1 },
    { "1024 x 768",        1024,  768, 1 },
    { "1366 x 768",        1366,  768, 1 },
    { "1600 x 900",        1600,  900, 1 },
    { "1920 x 1080",       1920, 1080, 1 },
    { "1280 x 720",        1280,  720, 1 },
};

static constexpr int RESOLUTION_COUNT = static_cast<int>(_countof(g_aResolution));
static constexpr int RESOLUTION_DEFAULT = 5; // 1280 x 720

static ZRef<CCtrlComboBox> g_cbResolution;
static int g_nResolution = 0;
static int g_nScreenWidth = 800;
static int g_nScreenHeight = 600;
static int g_nAdjustCenterY = 0;
static int g_nPixelScale = 1;

void set_screen_resolution(int nResolution, bool bSave);

int get_screen_width() {
    return g_nScreenWidth;
}

int get_screen_height() {
    return g_nScreenHeight;
}

int get_adjust_cy() {
    return g_nAdjustCenterY;
}

int get_pixel_scale() {
    return g_nPixelScale;
}

void get_default_position(int nUIType, int* pnDefaultX, int* pnDefaultY) {
    int nDefaultX;
    int nDefaultY;
    switch (nUIType) {
    case 4:
        nDefaultX = 8;
        nDefaultY = 8;
        break;
    case 8:
        nDefaultX = 500;
        nDefaultY = 50;
        break;
    case 9:
    case 22:
        nDefaultX = (nUIType == 9) ? 250 : 500;
        nDefaultY = 100;
        break;
    case 14:
        nDefaultX = 600;
        nDefaultY = 35;
        break;
    case 15:
        nDefaultX = 730;
        nDefaultY = 400;
        break;
    case 18:
        nDefaultX = 11;
        nDefaultY = 24;
        break;
    case 20:
        nDefaultX = 720;
        nDefaultY = 80;
        break;
    case 23:
    case 31:
    case 33:
        nDefaultX = 100;
        nDefaultY = 100;
        break;
    case 24:
    case 25:
    case 26:
    case 27:
    case 29:
    case 32:
        nDefaultX = 244;
        nDefaultY = 105;
        break;
    case 30:
        nDefaultX = 769;
        nDefaultY = 343;
        break;
    default:
        nDefaultX = 8 * (3 * nUIType + 3);
        nDefaultY = nDefaultX;
        break;
    }
    if (pnDefaultX) {
        *pnDefaultX = nDefaultX;
    }
    if (pnDefaultY) {
        *pnDefaultY = nDefaultY;
    }
}


static auto set_stage = reinterpret_cast<void(__cdecl*)(CStage*, void*)>(0x00777347);
void __cdecl set_stage_hook(CStage* pStage, void* pParam) {
    // Leaving gameplay (incoming stage is neither CField nor the transient CInterStage a
    // map change passes through): close the DLL's bag window before the stage flips —
    // engine teardown only destroys its own UI, so the bag would linger over the login
    // screen after logout. Map changes (CField/CInterStage) keep it open, like vanilla UI.
    if (!pStage || (!pStage->IsKindOf(reinterpret_cast<const CRTTI*>(0x00BED758))
                 && !pStage->IsKindOf(reinterpret_cast<const CRTTI*>(0x00BED874)))) {
        BagWindow_OnLeaveField();
    }
    // CField::ms_RTTI_CField - change resolution before set_stage
    if (pStage && pStage->IsKindOf(reinterpret_cast<const CRTTI*>(0x00BED758))) {
        set_screen_resolution(g_nResolution, 0);
        set_stage(pStage, pParam);
        return;
    }
    set_stage(pStage, pParam);
    // !CInterStage::ms_RTTI_CInterStage - change resolution after set_stage
    if (pStage && !pStage->IsKindOf(reinterpret_cast<const CRTTI*>(0x00BED874))) {
        set_screen_resolution(0, 0);
    }
}


void CConfig::GetUIWndPos_hook(int nUIType, int* x, int* y, int* op) {
    CConfig::GetUIWndPos(this, nUIType, x, y, op);
    if (*x < -5 || *x > get_screen_width() - 6 || *y < -5 || *y > get_screen_height() - 6) {
        get_default_position(nUIType, x, y);
    }
}

void CConfig::LoadCharacter_hook(int nWorldID, unsigned int dwCharacterId) {
    CConfig::LoadCharacter(this, nWorldID, dwCharacterId);
    for (size_t i = 0; i < 34; ++i) {
        int nDefaultX;
        int nDefaultY;
        get_default_position(i, &nDefaultX, &nDefaultY);

        char sBuffer[1024];
        sprintf_s(sBuffer, 1024, "uiWndX%d", i);
        m_nUIWnd_X[i] = GetOpt_Int(GLOBAL_OPT, sBuffer, nDefaultX, -5, get_screen_width() - 6);
        sprintf_s(sBuffer, 1024, "uiWndY%d", i);
        m_nUIWnd_Y[i] = GetOpt_Int(GLOBAL_OPT, sBuffer, nDefaultY, -5, get_screen_height() - 6);
    }
}

void CConfig::LoadGlobal_hook() {
    CConfig::LoadGlobal(this);
    g_nResolution = GetOpt_Int(GLOBAL_OPT, "mnScreenResolution", RESOLUTION_DEFAULT, 0, RESOLUTION_COUNT - 1);
    LogInfo("CConfig::LoadGlobal_hook: g_nResolution=%d", g_nResolution);
}

void CConfig::SaveGlobal_hook() {
    LogInfo("CConfig::SaveGlobal_hook: writing g_nResolution=%d", g_nResolution);
    SetOpt_Int(GLOBAL_OPT, "mnScreenResolution", g_nResolution);
    CConfig::SaveGlobal(this);
    LogInfo("CConfig::SaveGlobal_hook: original SaveGlobal returned");
}

void CConfig::ApplySysOpt_hook(void* pSysOpt, int bApplyVideo) {
    LogInfo("CConfig::ApplySysOpt_hook: pSysOpt=%p bApplyVideo=%d cbResolution=%d select=%d",
            pSysOpt, bApplyVideo, g_cbResolution ? 1 : 0,
            g_cbResolution ? g_cbResolution->m_nSelect : -1);
    CConfig::ApplySysOpt(this, pSysOpt, bApplyVideo);
    if (pSysOpt && bApplyVideo && g_cbResolution) {
        set_screen_resolution(g_cbResolution->m_nSelect, true);
    }
}


class CUISysOpt {
public:
    MEMBER_HOOK(void, 0x00994163, OnCreate, void* pData)
    MEMBER_HOOK(void, 0x007FF4AA, Destructor)
};

void CUISysOpt::OnCreate_hook(void* pData) {
    CUISysOpt::OnCreate(this, pData);

    CCtrlComboBox::CREATEPARAM paramComboBox;
    paramComboBox.nBackColor = 0xFFEEEEEE;
    paramComboBox.nBackFocusedColor = 0xFFA5A198;
    paramComboBox.nBorderColor = 0xFF999999;

    g_cbResolution = new CCtrlComboBox();
    g_cbResolution->CreateCtrl(this, 2000, 0, 76, 338, 166, 18, &paramComboBox);
    unsigned int dwResolutionParam = 0;
    for (const auto& resolution : g_aResolution) {
        g_cbResolution->AddItem(resolution.sLabel, dwResolutionParam++);
    }
    g_cbResolution->SetSelect(g_nResolution);
}

void CUISysOpt::Destructor_hook() {
    CUISysOpt::Destructor(this);
    g_cbResolution = nullptr;
}


class CInputSystem : public TSingleton<CInputSystem, 0x00BEC33C> {
public:
    MEMBER_AT(HWND, 0x0, m_hWnd)
    MEMBER_AT(IWzVector2DPtr, 0x9B0, m_pVectorCursor)
    MEMBER_HOOK(void, 0x0059A0CB, SetCursorVectorPos, int x, int y)
    MEMBER_HOOK(int, 0x0059A887, SetCursorPos, int x, int y)

    int GetCursorPos(POINT* lpPoint) {
        if (!::GetCursorPos(lpPoint) || !::ScreenToClient(m_hWnd, lpPoint)) {
            return 0;
        }
        // Window client coords are scaled pixels, the rest of the game works in logical ones.
        lpPoint->x /= get_pixel_scale();
        lpPoint->y /= get_pixel_scale();
        return 1;
    }
};

// x/y are logical coords: the WM_MOUSE* lParam is converted from window-client (scaled)
// coords once, in CWndMan__TranslateMessage_hook, before anything reads it. The only other
// callers are CInputSystem::UpdateMouse (DirectInput, fullscreen-only where scale is 1) and
// SetCursorPos_hook below, which already work in logical coords.
void CInputSystem::SetCursorVectorPos_hook(int x, int y) {
    m_pVectorCursor->RelMove(x - get_screen_width() / 2, y - get_screen_height() / 2 - get_adjust_cy());
}

int CInputSystem::SetCursorPos_hook(int x, int y) {
    // The game warps the cursor in logical coords; the OS wants scaled client coords.
    x = zclamp(x, 0, get_screen_width());
    y = zclamp(y, 0, get_screen_height());
    SetCursorVectorPos_hook(x, y);
    POINT pt;
    pt.x = x * get_pixel_scale();
    pt.y = y * get_pixel_scale();
    return ::ClientToScreen(m_hWnd, &pt) && ::SetCursorPos(pt.x, pt.y);
}


static auto CWndMan__TranslateMessage = reinterpret_cast<int(__thiscall*)(CWndMan*, unsigned int*, unsigned int*, int*, int*)>(0x009E7D77);

int __fastcall CWndMan__TranslateMessage_hook(CWndMan* pThis, void* _EDX, unsigned int* puMsg, unsigned int* puWParam, int* pnLParam, int* pnResult) {
    // Mouse lParam arrives in window-client (scaled) coords. Convert to logical once, here,
    // before anything reads it: both CInputSystem::SetCursorVectorPos and CWndMan::ProcessMouse
    // (UI hit testing) consume this same lParam inside the original function.
    if (g_nPixelScale > 1 && *puMsg >= WM_MOUSEFIRST && *puMsg <= WM_MOUSELAST) {
        const int x = static_cast<short>(LOWORD(*pnLParam)) / g_nPixelScale;
        const int y = static_cast<short>(HIWORD(*pnLParam)) / g_nPixelScale;
        *pnLParam = (x & 0xFFFF) | (y << 16);
    }
    return CWndMan__TranslateMessage(pThis, puMsg, puWParam, pnLParam, pnResult);
}

void CWndMan::Constructor_hook(HWND hWnd) {
    CWndMan::Constructor(this, hWnd);
    for (int i = 0; i < UIOrigin::Origin_NUM; ++i) {
        PcCreateObject<IWzVector2DPtr>(L"Shape2D#Vector2D", ms_pOrgWindowEx[i], nullptr);
    }
    PcCreateObject<IWzVector2DPtr>(L"Shape2D#Vector2D", ms_pOrgStatusBar, nullptr);
    PcCreateObject<IWzVector2DPtr>(L"Shape2D#Vector2D", ms_pOrgScreenMsg, nullptr);
    PcCreateObject<IWzVector2DPtr>(L"Shape2D#Vector2D", ms_pOrgQuickSlot, nullptr);
    ResetOrgWindow();
}

void CWndMan::Destructor_hook() {
    CWndMan::Destructor(this);
    for (int i = 0; i < UIOrigin::Origin_NUM; ++i) {
        ms_pOrgWindowEx[i] = nullptr;
    }
    ms_pOrgStatusBar = nullptr;
    ms_pOrgScreenMsg = nullptr;
    ms_pOrgQuickSlot = nullptr;
}

IWzVector2DPtr* CWndMan::GetOrgWindow_hook(IWzVector2DPtr* result) {
    auto ret = reinterpret_cast<uintptr_t>(_ReturnAddress());
    switch (ret) {
    case 0x00533B77: // CField::ShowMobHPTag
    case 0x005555B7: // CField_Dojang::OnClock
    case 0x0058C91C: // CFloatNotice::CreateFloatNotice
    case 0x006CD787: // CNoticeQuestProgress::CNoticeQuestProgress
        result->GetInterfacePtr() = GetOrgWindowEx(CWnd::UIOrigin::Origin_CT);
        break;
    case 0x009603F2: // CUserLocal::DrawCombo
        result->GetInterfacePtr() = GetOrgWindowEx(CWnd::UIOrigin::Origin_RT);
        break;
    case 0x0053502B: // CField::ShowScreenEffect
        result->GetInterfacePtr() = GetOrgWindowEx(CWnd::UIOrigin::Origin_CC);
        break;
    case 0x008DEB75: // CUIStatusBar::FlashHPBar
    case 0x008DEE11: // CUIStatusBar::FlashMPBar
        result->GetInterfacePtr() = ms_pOrgStatusBar;
        break;
    case 0x0089AF82: // CUIScreenMsg::CUIScreenMsg
        result->GetInterfacePtr() = ms_pOrgScreenMsg;
        break;
    case 0x008D15EE: // CUIStatusBar::OnCreate - CQuickSlot
        result->GetInterfacePtr() = ms_pOrgQuickSlot;
        break;
    default:
        if (ret >= 0x00554005 && ret <= 0x0055478F) { // CField_Dojang::Init
            result->GetInterfacePtr() = GetOrgWindowEx(Origin_CT);
        } else if (ret >= 0x008D01B2 && ret <= 0x008D3ADF) { // CUIStatusBar::OnCreate
            result->GetInterfacePtr() = ms_pOrgStatusBar;
        } else {
            result->GetInterfacePtr() = m_pOrgWindow;
        }
        break;
    }
    result->AddRef();
    return result;
}

void CWnd::CreateWnd_hook(int l, int t, int w, int h, int z, int bScreenCoord, void* pData, int bSetFocus) {
    CWnd::CreateWnd(this, l, t, w, h, z, bScreenCoord, pData, bSetFocus);
    if (!bScreenCoord) {
        return;
    }
    auto ret = reinterpret_cast<uintptr_t>(_ReturnAddress());
    switch (ret) {
    case 0x005362BC: // CField::OnClock(CField*, int)
    case 0x0053638B: // CField::OnClock(CField*, int)
    case 0x00545D24: // CField_Battlefield::OnClock(CField*, int)
    case 0x0056042E: // CField_Massacre::OnClock(CField*, int)
    case 0x00578B30: // CField_SpaceGAGA::OnClock(CField*, int)
    case 0x00A24D15: // CWvsContext::SetEventTimer(CWvsContext*, int)
        m_pLayer->origin = static_cast<IUnknown*>(CWndMan::GetInstance()->GetOrgWindowEx(CWnd::UIOrigin::Origin_CT));
        return;
    case 0x0045A5EF: // CAvatarMegaphone::CAvatarMegaphone
        m_pLayer->origin = static_cast<IUnknown*>(CWndMan::GetInstance()->GetOrgWindowEx(CWnd::UIOrigin::Origin_RT));
        return;
    case 0x004EDAEB: // CDialog::CreateDlg(CDialog*, int, int, int, void*)
    case 0x004EDB9A: // CDialog::CreateDlg(CDialog*, const wchar_t*, int, void*)
    case 0x004EDAB3: // CDialog::CreateDlg(CDialog*, int, int, int, int, int, int, void*)
    case 0x007F202C: // CUISkillEffectChange::CUISkillEffectChange
    case 0x00897BD8: // CUIRevive::CUIRevive
        m_pLayer->origin = static_cast<IUnknown*>(CWndMan::GetInstance()->GetOrgWindowEx(CWnd::UIOrigin::Origin_CC));
        return;
    case 0x0051FA03: // CFadeWnd::CreateFadeWnd
    case 0x008CFD65: // CUIStatusBar::CUIStatusBar
        m_pLayer->origin = static_cast<IUnknown*>(CWndMan::ms_pOrgStatusBar);
        return;
    }
}

static auto CWnd__OnMoveWnd = reinterpret_cast<void(__thiscall*)(CWnd*, int, int)>(0x009DEB57);

void MoveWndToAbsPos(CWnd* pWnd, int l, int t) {
    IWzVector2DPtr pAbsOrigin = CWndMan::GetInstance()->m_pOrgWindow;
    IWzVector2DPtr pWndOrigin = static_cast<IUnknown*>(pWnd->m_pLayer->origin);
    int nOffsetX = pAbsOrigin->x - pWndOrigin->x;
    int nOffsetY = pAbsOrigin->y - pWndOrigin->y;
    pWnd->m_pLayer->RelMove(l + nOffsetX, t + nOffsetY);
}

void __fastcall CWnd__OnMoveWnd_hook(CWnd* pThis, void* _EDX, int l, int t) {
    int nLeft = pThis->GetAbsLeft();
    int nTop = pThis->GetAbsTop();
    int nWidth = pThis->m_pLayer->width;
    int nHeight = pThis->m_pLayer->height;
    // Save m_ptCursorRel
    POINT pt;
    CInputSystem::GetInstance()->GetCursorPos(&pt);
    POINT ptRel;
    ptRel.x = pt.x - nLeft;
    ptRel.y = pt.y - nTop;
    if (pThis->m_ptCursorRel.x == -1 && pThis->m_ptCursorRel.y == -1) {
        pThis->m_ptCursorRel.x = ptRel.x;
        pThis->m_ptCursorRel.y = ptRel.y;
    }
    // Iterate ZList<CWnd*> for window snapping
    RECT rcThis;
    SetRect(&rcThis, nLeft, nTop, nLeft + nWidth, nTop + nHeight);
    auto pos = CWndMan::ms_lpWindow.GetHeadPosition();
    while (pos) {
        auto pNext = CWndMan::ms_lpWindow.GetNext(pos);
        if (pNext == pThis || pThis->IsMyAddOn(pNext) ||
                pNext == CUIStatusBar::GetInstance()) {
            continue;
        }
        int nNextLeft = pNext->GetAbsLeft();
        int nNextTop = pNext->GetAbsTop();
        int nNextWidth = pNext->m_pLayer->width;
        int nNextHeight = pNext->m_pLayer->height;
        RECT rcNext, rcIntersect;
        SetRect(&rcNext, nNextLeft - 10, nNextTop - 10, nNextLeft + nNextWidth + 10, nNextTop + nNextHeight + 10);
        if (!IntersectRect(&rcIntersect, &rcThis, &rcNext)) {
            continue;
        }
        if (abs(nLeft - nNextLeft - nNextWidth) <= 10) {
            MoveWndToAbsPos(pThis, nNextLeft + nNextWidth, nTop);
        }
        if (abs(nLeft - nNextLeft + nWidth) <= 10) {
            MoveWndToAbsPos(pThis, nNextLeft - nWidth, nTop);
        }
        if (abs(nTop - nNextTop - nNextHeight) <= 10) {
            MoveWndToAbsPos(pThis, nLeft, nNextTop + nNextHeight);
        }
        if (abs(nTop - nNextTop + nHeight) <= 10) {
            MoveWndToAbsPos(pThis, nLeft, nNextTop - nHeight);
        }
    }
    // Window snapping to screen border
    if (abs(nLeft) <= 10) {
        MoveWndToAbsPos(pThis, 0, nTop);
    }
    if (abs(nTop) <= 10) {
        MoveWndToAbsPos(pThis, nLeft, 0);
    }
    if (abs(nLeft + nWidth - get_screen_width()) <= 10) {
        MoveWndToAbsPos(pThis, get_screen_width() - nWidth, nTop);
    }
    if (abs(nTop + nHeight - get_screen_height()) <= 10) {
        MoveWndToAbsPos(pThis, nLeft, get_screen_height() - nHeight);
    }
    // Handle m_ptCursorRel
    if (abs(pThis->m_ptCursorRel.x - ptRel.x) > 15) {
        MoveWndToAbsPos(pThis, pt.x - pThis->m_ptCursorRel.x, nTop);
    }
    if (abs(pThis->m_ptCursorRel.y - ptRel.y) > 15) {
        MoveWndToAbsPos(pThis, nLeft, pt.y - pThis->m_ptCursorRel.y);
    }
}


class CUtilDlgEx : public CWnd { // CDialog
public:
    MEMBER_AT(int, 0x98, m_wndWidth)
    MEMBER_AT(int, 0x9C, m_wndHeight)
    MEMBER_HOOK(void, 0x009A3E38, CreateUtilDlgEx)
};

static RECT& sRectQuestDlg = *reinterpret_cast<RECT*>(0x00BE2DF0);

void CUtilDlgEx::CreateUtilDlgEx_hook() {
    int nLeft = zclamp<int>(sRectQuestDlg.left - m_wndWidth / 2, 0, get_screen_width());
    int nTop = zclamp<int>(sRectQuestDlg.top - m_wndHeight / 2, 0, get_screen_height());
    // CDialog::CreateDlg(this, nLeft, nTop, m_wndWidth, m_wndHeight, 10, 1, 0);
    CWnd::CreateWnd(this, nLeft, nTop, m_wndWidth, m_wndHeight, 10, 1, nullptr, 1);
}


IWzCanvasPtr* CUIToolTip::MakeLayer_hook(IWzCanvasPtr* result, int nLeft, int nTop, int bDoubleOutline, int bLogin, int bCharToolTip, unsigned int uColor) {
    CUIToolTip::MakeLayer(this, result, nLeft, nTop, bDoubleOutline, bLogin, bCharToolTip, uColor);
    if (!bCharToolTip) {
        if (nLeft < 0) {
            nLeft = 0;
        }
        if (nTop < 0) {
            nTop = 0;
        }
        int nBoundX = get_screen_width() - 1;
        if (nLeft + m_nWidth > nBoundX) {
            nLeft = nBoundX - m_nWidth;
        }
        int nBoundY = get_screen_height() - 1;
        if (nTop + m_nHeight > nBoundY) {
            nTop = nBoundY - m_nHeight;
        }
        m_pLayer->RelMove(nLeft, nTop);
    }
    return result;
}


class CUIContextMenu : public CWnd { // CDialog
public:
    MEMBER_AT(int, 0xCC, m_nBtNumber)
};

void __fastcall CUIContextMenu__CreateDlg_hook(CUIContextMenu* pThis, void* _EDX, int l, int t, int w, int h, int z, int bScreenCoord, void* pData) {
    POINT ptCursor;
    CInputSystem::GetInstance()->GetCursorPos(&ptCursor);
    int nWidth = 100;
    int nHeight = 15 * (pThis->m_nBtNumber + 2);
    if (ptCursor.x + nWidth > get_screen_width()) {
        ptCursor.x = get_screen_width() - nWidth;
    }
    if (ptCursor.y + nHeight > get_screen_height()) {
        ptCursor.y = get_screen_height() - nHeight;
    }
    CWnd::CreateWnd(pThis, ptCursor.x, ptCursor.y, nWidth, nHeight, 10, 1, pData, 1);
}


void CTemporaryStatView::AdjustPosition_hook() {
    int nOffsetX = (get_screen_width() / 2) - 3 + (-32 * m_lTemporaryStat.GetCount());
    int nOffsetY = (get_screen_height() / 2) + get_adjust_cy() - 23;
    auto pos = m_lTemporaryStat.GetHeadPosition();
    while (pos) {
        auto pNext = m_lTemporaryStat.GetNext(pos);
        pNext->pLayer->RelMove((32 - pNext->pLayer->width) / 2 + nOffsetX, (32 - pNext->pLayer->height) / 2 - nOffsetY);
        pNext->pLayerShadow->RelMove((32 - pNext->pLayerShadow->width) / 2 + nOffsetX, (32 - pNext->pLayerShadow->height) / 2 - nOffsetY);
        nOffsetX += 32;
    }
}

int CTemporaryStatView::ShowToolTip_hook(CUIToolTip& uiToolTip, const POINT& ptCursor, int rx, int ry) {
    POINT ptAdjust = { ptCursor.x + 800 - get_screen_width(), ptCursor.y };
    return CTemporaryStatView::ShowToolTip(this, uiToolTip, ptAdjust, rx, ry);
}

int CTemporaryStatView::FindIcon_hook(const POINT& ptCursor, int& nType, int& nID) {
    POINT ptAdjust = { ptCursor.x + 800 - get_screen_width(), ptCursor.y };
    return CTemporaryStatView::FindIcon(this, ptAdjust, nType, nID);
}


int CUIStatusBar::GetShortCutIndexByPos_hook(int x, int y) {
    x = x + CWndMan::ms_pOrgStatusBar->x - CWndMan::ms_pOrgQuickSlot->x;
    y = y + CWndMan::ms_pOrgStatusBar->y - CWndMan::ms_pOrgQuickSlot->y;
    return CUIStatusBar::GetShortCutIndexByPos(this, x, y);
}

class CWvsPhysicalSpace2D : public TSingleton<CWvsPhysicalSpace2D, 0x00BEBFA0> {
public:
    MEMBER_AT(RECT, 0x24, m_rcMBR)
};

void CMapLoadable::RestoreViewRange_hook() {
    auto pSpace2D = CWvsPhysicalSpace2D::GetInstance();
    m_rcViewRange.left = get_int32(m_pPropFieldInfo->item[L"VRLeft"], pSpace2D->m_rcMBR.left - 20) + get_screen_width() / 2;
    m_rcViewRange.top = get_int32(m_pPropFieldInfo->item[L"VRTop"], pSpace2D->m_rcMBR.top - 60) + get_screen_height() / 2;
    m_rcViewRange.right = get_int32(m_pPropFieldInfo->item[L"VRRight"], pSpace2D->m_rcMBR.right + 20) - get_screen_width() / 2;
    m_rcViewRange.bottom = get_int32(m_pPropFieldInfo->item[L"VRBottom"], pSpace2D->m_rcMBR.bottom + 190) - get_screen_height() / 2;
    if (m_rcViewRange.right - m_rcViewRange.left <= 0) {
        int mid = (m_rcViewRange.left + m_rcViewRange.right) / 2;
        m_rcViewRange.left = mid;
        m_rcViewRange.right = mid;
    }
    if (m_rcViewRange.bottom - m_rcViewRange.top <= 0) {
        int mid = (m_rcViewRange.top + m_rcViewRange.bottom) / 2;
        m_rcViewRange.top = mid;
        m_rcViewRange.bottom = mid;
    }
    m_rcViewRange.top += get_adjust_cy();
    m_rcViewRange.bottom += get_adjust_cy();
}

static auto CMapLoadable__MakeGrid_jmp = 0x0063EAD6;
static auto CMapLoadable__MakeGrid_ret = 0x0063EADC;
void __declspec(naked) CMapLoadable__MakeGrid_hook() {
    __asm {
        sar     ecx, 1                          ; overwritten instructions
        neg     eax
        sub     eax, ecx
        sub     eax, g_nAdjustCenterY           ; eax -= g_nAdjustCenterY
        jmp     [ CMapLoadable__MakeGrid_ret ]
    }
}

HRESULT __stdcall CMapLoadable__raw_WrapClip_hook(IWzVector2D* pThis, VARIANT pOrigin, int nWrapLeft, int nWrapTop, unsigned int uWrapWidth, unsigned int uWrapHeight, VARIANT bClip) {
    nWrapLeft = nWrapLeft + 400 - (SCREEN_WIDTH_MAX / 2);
    nWrapTop = nWrapTop + 300 - (SCREEN_HEIGHT_MAX / 2) - ((SCREEN_HEIGHT_MAX - 600) / 2);
    uWrapWidth = uWrapWidth - 800 + SCREEN_WIDTH_MAX;
    uWrapHeight = uWrapHeight - 600 + SCREEN_HEIGHT_MAX;
    return pThis->raw_WrapClip(pOrigin, nWrapLeft, nWrapTop, uWrapWidth, uWrapHeight, bClip);
}

HRESULT __stdcall CField_LimitedView__raw_Copy_hook(IWzCanvas* pThis, int nDstLeft, int nDstTop, IWzCanvas* pSource, VARIANT nAlpha) {
    nDstLeft = nDstLeft + (SCREEN_WIDTH_MAX / 2) - 400;
    nDstTop = nDstTop + (SCREEN_HEIGHT_MAX / 2) - 300 + ((SCREEN_HEIGHT_MAX - 600) / 2);
    return pThis->raw_Copy(nDstLeft, nDstTop, pSource, nAlpha);
}

HRESULT __fastcall CField_LimitedView__CopyEx_hook(IWzCanvas* pThis, void* _EDX, int nDstLeft, int nDstTop, IWzCanvas* pSource, CANVAS_ALPHATYPE nAlpha, int nWidth, int nHeight, int nSrcLeft, int nSrcTop, int nSrcWidth, int nSrcHeight, const Ztl_variant_t& pAdjust) {
    nDstLeft = nDstLeft + (SCREEN_WIDTH_MAX / 2) - 400;
    nDstTop = nDstTop + (SCREEN_HEIGHT_MAX / 2) - 300 + ((SCREEN_HEIGHT_MAX - 600) / 2);
    return pThis->CopyEx(nDstLeft, nDstTop, pSource, nAlpha, nWidth, nHeight, nSrcLeft, nSrcTop, nSrcWidth, nSrcHeight, pAdjust);
}


HRESULT __stdcall CUIScreenMsg__raw_RelMove_hook(IWzVector2D* pThis, int nX, int nY, VARIANT nTime, VARIANT nType) {
    nX = nX + 290 - SCREEN_MESSAGE_WIDTH;
    if (!CONSTANTS_CENTER_STATUSBAR && get_screen_width() > 800 && CUIStatusBar::GetInstance()->m_bQuickSlotUp) {
        nY = nY + 443 - 365;
    }
    return pThis->raw_RelMove(nX, nY, nTime, nType);
}

HRESULT __fastcall CUIScreenMsg__RelMove_hook(IWzVector2D* pThis, void* _EDX, int nX, int nY, const Ztl_variant_t& nTime, const Ztl_variant_t& nType) {
    nX = nX + 290 - SCREEN_MESSAGE_WIDTH;
    if (!CONSTANTS_CENTER_STATUSBAR && get_screen_width() > 800 && CUIStatusBar::GetInstance()->m_bQuickSlotUp) {
        nY = nY + 443 - 365;
    }
    return pThis->RelMove(nX, nY, nTime, nType);
}


class CUIMiniMap : public CUIWnd, public TSingleton<CUIMiniMap, 0x00BED788> {
};

int __stdcall CField__ShowMobHPTag_hook1() {
    // Only the base 800 x 600 logical layout needs the boss HP bar pushed past the minimap.
    if (CUIMiniMap::IsInstantiated() && get_screen_width() == 800 && get_screen_height() == 600) {
        return CUIMiniMap::GetInstance()->m_width;
    }
    return 0;
}


class CWzGr2D : public IWzGr2D {
public:
    struct SCREENMODE {
        unsigned char pad0[0x5C];
        MEMBER_AT(int, 0x0, nWidth)
        MEMBER_AT(int, 0x4, nHeight)
        MEMBER_AT(int, 0x58, bFullScreen)
    };

    MEMBER_AT(SCREENMODE, 0x20, m_screenMode)
    MEMBER_AT(int, 0x90, m_bInitialized)
    MEMBER_AT(int, 0x94, m_hrErrorCode)

    typedef int(__thiscall* FindScreenMode_t)(CWzGr2D*, SCREENMODE*, int, int, int, int);
    inline static FindScreenMode_t FindScreenMode;

    HRESULT ScreenResolution(int nWidth, int nHeight) {
        if (!nWidth || !nHeight) {
            return E_INVALIDARG;
        }
        if (m_screenMode.nWidth == nWidth && m_screenMode.nHeight == nHeight) {
            return S_OK;
        }
        SCREENMODE mode;
        if (!m_bInitialized || !FindScreenMode(this, &mode, m_screenMode.bFullScreen, nWidth, nHeight, 0)) {
            return E_FAIL;
        }
        m_screenMode = mode;
        m_hrErrorCode = 0x88760869; // D3DERR_DEVICENOTRESET
        return S_OK;
    }
};

static uintptr_t CWzGr2D__AdjustCenterY_jmp;
static uintptr_t CWzGr2D__AdjustCenterY_ret;
void __declspec(naked) CWzGr2D__AdjustCenterY_hook() {
    __asm {
        pushfd                                  ; push flags onto stack
        sub     ecx, g_nAdjustCenterY           ; nCenterY -= nAdjustCenterY
        mov     [ ebp - 0x14 ], ecx
        lea     edx, [ esi + 0xC4 ]             ; overwritten instruction
        popfd                                   ; pop flags from stack
        jmp     [ CWzGr2D__AdjustCenterY_ret ]
    }
}


// Pixel doubling plumbing.
//
// The game and Gr2D keep running at the logical resolution — Gr2D's screen mode, the D3D
// backbuffer and every UI coordinate stay at get_screen_width() x get_screen_height(). Only
// the OS window is grown to logical * scale, and the windowed present stretches the backbuffer
// over it, so one game pixel covers a scale x scale block.
//
// Two things have to be intercepted for that to hold:
//   - SetWindowPos: Gr2D resizes the window to its screen mode when the device is reset
//     (it resolves user32 dynamically, so hooking the export catches it), which would undo
//     the scaled size right after we set it.
//   - IDirect3D8::CreateDevice / IDirect3DDevice8::Reset: force a COPY swap chain, the swap
//     effect that is defined to stretch on present, and pin the backbuffer to the logical size.

static HWND get_game_window() {
    return CInputSystem::IsInstantiated() ? CInputSystem::GetInstance()->m_hWnd : nullptr;
}

static decltype(&::SetWindowPos) g_pfnSetWindowPos = ::SetWindowPos;

static void get_scaled_window_size(HWND hWnd, int* pcx, int* pcy) {
    RECT rc = { 0, 0, g_nScreenWidth * g_nPixelScale, g_nScreenHeight * g_nPixelScale };
    AdjustWindowRectEx(&rc, GetWindowLongA(hWnd, GWL_STYLE), GetMenu(hWnd) != nullptr,
                       GetWindowLongA(hWnd, GWL_EXSTYLE));
    *pcx = rc.right - rc.left;
    *pcy = rc.bottom - rc.top;
}

static BOOL WINAPI SetWindowPos_hook(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) {
    if (g_nPixelScale > 1 && !(uFlags & SWP_NOSIZE) && hWnd && hWnd == get_game_window()) {
        get_scaled_window_size(hWnd, &cx, &cy);
    }
    return g_pfnSetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

static void resize_game_window() {
    HWND hWnd = get_game_window();
    if (!hWnd) {
        return;
    }
    int cx = 0;
    int cy = 0;
    get_scaled_window_size(hWnd, &cx, &cy);
    g_pfnSetWindowPos(hWnd, nullptr, 0, 0, cx, cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// Minimal d3d8 declarations - only the presentation parameter layout and the two vtable slots
// are needed, and d3d8.h isn't part of this toolchain.
struct D3DPRESENT_PARAMETERS_8 {
    UINT BackBufferWidth;
    UINT BackBufferHeight;
    DWORD BackBufferFormat;
    UINT BackBufferCount;
    DWORD MultiSampleType;
    DWORD SwapEffect;
    HWND hDeviceWindow;
    BOOL Windowed;
    BOOL EnableAutoDepthStencil;
    DWORD AutoDepthStencilFormat;
    DWORD Flags;
    UINT FullScreen_RefreshRateInHz;
    UINT FullScreen_PresentationInterval;
};

#define D3DSWAPEFFECT_COPY_8      3
#define IDIRECT3D8_CREATEDEVICE   15
#define IDIRECT3DDEVICE8_RESET    14

typedef void*(WINAPI* Direct3DCreate8_t)(UINT uSDKVersion);
typedef HRESULT(WINAPI* CreateDevice_t)(void* pThis, UINT uAdapter, DWORD dwDeviceType, HWND hFocusWindow,
                                        DWORD dwBehaviorFlags, D3DPRESENT_PARAMETERS_8* pParam, void** ppDevice);
typedef HRESULT(WINAPI* Reset_t)(void* pThis, D3DPRESENT_PARAMETERS_8* pParam);

static Direct3DCreate8_t g_pfnDirect3DCreate8 = nullptr;
static CreateDevice_t g_pfnCreateDevice = nullptr;
static Reset_t g_pfnReset = nullptr;

static void adjust_present_param(D3DPRESENT_PARAMETERS_8* pParam) {
    if (!pParam || !pParam->Windowed || g_nPixelScale <= 1) {
        return;
    }
    pParam->SwapEffect = D3DSWAPEFFECT_COPY_8;
    pParam->BackBufferCount = 1;
    pParam->BackBufferWidth = g_nScreenWidth;
    pParam->BackBufferHeight = g_nScreenHeight;
}

static HRESULT WINAPI Reset_hook(void* pThis, D3DPRESENT_PARAMETERS_8* pParam) {
    adjust_present_param(pParam);
    return g_pfnReset(pThis, pParam);
}

static HRESULT WINAPI CreateDevice_hook(void* pThis, UINT uAdapter, DWORD dwDeviceType, HWND hFocusWindow,
                                        DWORD dwBehaviorFlags, D3DPRESENT_PARAMETERS_8* pParam, void** ppDevice) {
    adjust_present_param(pParam);
    HRESULT hr = g_pfnCreateDevice(pThis, uAdapter, dwDeviceType, hFocusWindow, dwBehaviorFlags, pParam, ppDevice);
    if (SUCCEEDED(hr) && ppDevice && *ppDevice && !g_pfnReset) {
        g_pfnReset = reinterpret_cast<Reset_t>(VMTHook(*ppDevice, CastHook(&Reset_hook), IDIRECT3DDEVICE8_RESET));
    }
    return hr;
}

static void* WINAPI Direct3DCreate8_hook(UINT uSDKVersion) {
    void* pD3D = g_pfnDirect3DCreate8(uSDKVersion);
    if (pD3D && !g_pfnCreateDevice) {
        g_pfnCreateDevice = reinterpret_cast<CreateDevice_t>(VMTHook(pD3D, CastHook(&CreateDevice_hook), IDIRECT3D8_CREATEDEVICE));
    }
    return pD3D;
}

static void set_pixel_scale(int nScale) {
    auto gr = reinterpret_cast<CWzGr2D*>(get_gr().GetInterfacePtr());
    if (!gr || gr->m_screenMode.bFullScreen) {
        // Fullscreen already sets the display mode to the logical size, so the monitor is the
        // thing doing the upscale - doubling on top of that would just crop the screen.
        nScale = 1;
    }
    const int nPixelScale = nScale < 1 ? 1 : nScale;
    if (nPixelScale != g_nPixelScale) {
        LogInfo("set_pixel_scale: %d -> %d (logical %dx%d)", g_nPixelScale, nPixelScale, g_nScreenWidth, g_nScreenHeight);
    }
    g_nPixelScale = nPixelScale;
    resize_game_window();
}


void set_screen_resolution(int nResolution, bool bSave) {
    if (nResolution < 0 || nResolution >= RESOLUTION_COUNT) {
        nResolution = 0;
    }
    const int nScreenWidth = g_aResolution[nResolution].nWidth;
    const int nScreenHeight = g_aResolution[nResolution].nHeight;
    // The pixel scale follows the saved preference, not the requested index: the login stage
    // forces index 0 (800 x 600 logical) and the window shouldn't change size between login
    // and field for a user running a 2x mode.
    const int nPreference = bSave ? nResolution : g_nResolution;
    const int nScale = g_aResolution[zclamp(nPreference, 0, RESOLUTION_COUNT - 1)].nScale;
    if (nScreenWidth != g_nScreenWidth || nScreenHeight != g_nScreenHeight) {
        auto gr = reinterpret_cast<CWzGr2D*>(get_gr().GetInterfacePtr());
        HRESULT hr = gr->ScreenResolution(nScreenWidth, nScreenHeight);
        if (SUCCEEDED(hr)) {
            g_nScreenWidth = nScreenWidth;
            g_nScreenHeight = nScreenHeight;
            g_nAdjustCenterY = (g_nScreenHeight - 600) / 2;
            if (CWndMan::IsInstantiated()) {
                CWndMan::GetInstance()->ResetOrgWindow();
                // Adjust CUtilDlgEx position
                sRectQuestDlg.top = get_screen_height() / 2;
                sRectQuestDlg.left = get_screen_width() / 2;
            }
            if (CWvsContext::IsInstantiated()) {
                CWvsContext::GetInstance()->m_temporaryStatView.AdjustPosition_hook();
            }
            CField* field = get_field();
            if (field) {
                field->RestoreViewRange_hook();
                // CMapLoadable::ReloadBack
                reinterpret_cast<void(__thiscall*)(CMapLoadable*)>(0x00644491)(field);
            }
        } else {
            // ScreenResolution failed (most likely FindScreenMode rejected the requested
            // mode in fullscreen — monitor doesn't list it as a supported display mode).
            // Don't drop the user's preference: still update g_nResolution below so the
            // choice persists and we can retry on the next session/stage transition.
            LogInfo("set_screen_resolution: ScreenResolution(%d, %d) FAILED hr=0x%08lX (mode not supported, keeping preference)",
                    nScreenWidth, nScreenHeight, (unsigned long)hr);
        }
    }
    if (bSave) {
        g_nResolution = nResolution;
        LogInfo("set_screen_resolution: bSave -> g_nResolution=%d (screen=%dx%d)", g_nResolution, g_nScreenWidth, g_nScreenHeight);
    }
    // After the screen mode, so the scaled window size and the present parameters used by the
    // pending device reset are both computed from the resolution we just switched to.
    set_pixel_scale(nScale);
}


void AttachResolutionMod() {
    ATTACH_HOOK(set_stage, set_stage_hook);
    ATTACH_HOOK(CConfig::GetUIWndPos, CConfig::GetUIWndPos_hook);
    ATTACH_HOOK(CConfig::LoadCharacter, CConfig::LoadCharacter_hook);
    ATTACH_HOOK(CConfig::LoadGlobal, CConfig::LoadGlobal_hook);
    ATTACH_HOOK(CConfig::SaveGlobal, CConfig::SaveGlobal_hook);
    ATTACH_HOOK(CConfig::ApplySysOpt, CConfig::ApplySysOpt_hook);
    ATTACH_HOOK(CUISysOpt::OnCreate, CUISysOpt::OnCreate_hook);
    ATTACH_HOOK(CUISysOpt::Destructor, CUISysOpt::Destructor_hook);
    Patch4(0x009945BC + 1, 372);               // CUISysOpt::OnCreate - shift ok/cancel buttons
    Patch4(0x009F7078 + 1, SCREEN_HEIGHT_MAX); // CWvsApp::CreateWndManager - nHeight
    Patch4(0x009F707D + 1, SCREEN_WIDTH_MAX);  // CWvsApp::CreateWndManager - nWidth

    // Pixel doubling: keep the window at logical * scale (Gr2D resizes it back on every device
    // reset) and the D3D backbuffer at the logical size, so the windowed present upscales it.
    AttachHook(reinterpret_cast<void**>(&g_pfnSetWindowPos), CastHook(&SetWindowPos_hook));
    if (void* pDirect3DCreate8 = GetAddress("d3d8.dll", "Direct3DCreate8")) {
        g_pfnDirect3DCreate8 = reinterpret_cast<Direct3DCreate8_t>(pDirect3DCreate8);
        AttachHook(reinterpret_cast<void**>(&g_pfnDirect3DCreate8), CastHook(&Direct3DCreate8_hook));
    }

    ATTACH_HOOK(CInputSystem::SetCursorVectorPos, CInputSystem::SetCursorVectorPos_hook);
    ATTACH_HOOK(CInputSystem::SetCursorPos, CInputSystem::SetCursorPos_hook);
    ATTACH_HOOK(CWndMan__TranslateMessage, CWndMan__TranslateMessage_hook);
    ATTACH_HOOK(CWndMan::Constructor, CWndMan::Constructor_hook);
    ATTACH_HOOK(CWndMan::Destructor, CWndMan::Destructor_hook);
    ATTACH_HOOK(CWndMan::GetOrgWindow, CWndMan::GetOrgWindow_hook);
    ATTACH_HOOK(CWnd::CreateWnd, CWnd::CreateWnd_hook);
    ATTACH_HOOK(CWnd__OnMoveWnd, CWnd__OnMoveWnd_hook);

    // CUtilDlgEx::CreateUtilDlgEx - adjust for screen bounds
    ATTACH_HOOK(CUtilDlgEx::CreateUtilDlgEx, CUtilDlgEx::CreateUtilDlgEx_hook);

    // CUIToolTip::MakeLayer - handle maximum bounds for CUIToolTip
    ATTACH_HOOK(CUIToolTip::MakeLayer, CUIToolTip::MakeLayer_hook);

    // CUIContextMenu::CUIContextMenu - reposition right click menu
    PatchCall(0x009966E3, CUIContextMenu__CreateDlg_hook);

    // CTemporaryStatView - reposition buff display
    ATTACH_HOOK(CTemporaryStatView::AdjustPosition, CTemporaryStatView::AdjustPosition_hook);
    ATTACH_HOOK(CTemporaryStatView::ShowToolTip, CTemporaryStatView::ShowToolTip_hook);
    ATTACH_HOOK(CTemporaryStatView::FindIcon, CTemporaryStatView::FindIcon_hook);

    // CUIStatusBar - handle quickslot position
    Patch4(0x008CFD50 + 1, SCREEN_WIDTH_MAX); // CUIStatusBar::CUIStatusBar
    ATTACH_HOOK(CUIStatusBar::GetShortCutIndexByPos, CUIStatusBar::GetShortCutIndexByPos_hook);

    // CMapLoadable - handle view range
    ATTACH_HOOK(CMapLoadable::RestoreViewRange, CMapLoadable::RestoreViewRange_hook);
    PatchJmp(CMapLoadable__MakeGrid_jmp, &CMapLoadable__MakeGrid_hook);

    // CMapLoadable::TransientLayer_Weather - weather effects
    PatchCall(0x0064106B, &CMapLoadable__raw_WrapClip_hook, 6);
    Patch4(0x0064043E + 1, SCREEN_WIDTH_MAX / 2);
    Patch4(0x00640443 + 1, SCREEN_HEIGHT_MAX / 2);
    Patch4(0x00640599 + 2, SCREEN_WIDTH_MAX / 2 - 10);
    Patch4(0x006405BA + 2, SCREEN_HEIGHT_MAX - 10);
    Patch4(0x00640606 + 1, SCREEN_WIDTH_MAX);
    Patch4(0x00640618 + 1, SCREEN_HEIGHT_MAX);
    Patch4(0x00640626 + 1, SCREEN_WIDTH_MAX / 2);
    Patch4(0x00640639 + 1, SCREEN_WIDTH_MAX);
    Patch4(0x0064064B + 1, SCREEN_HEIGHT_MAX);
    Patch4(0x00640656 + 2, -SCREEN_WIDTH_MAX / 2);
    Patch4(0x006406C3 + 1, SCREEN_WIDTH_MAX);
    Patch4(0x006406D5 + 1, SCREEN_HEIGHT_MAX);
    Patch4(0x006406FA + 2, SCREEN_HEIGHT_MAX / 2);

    // CField_LimitedView::Init
    Patch4(0x0055B808 + 1, SCREEN_HEIGHT_MAX);                                      // m_pCanvasDark->raw_Create - uHeight
    Patch4(0x0055B80D + 1, SCREEN_WIDTH_MAX);                                       // m_pCanvasDark->raw_Create - uWidth
    Patch4(0x0055B884 + 1, SCREEN_HEIGHT_MAX);                                      // m_pCanvasDark->raw_DrawRectangle - uHeight
    Patch4(0x0055BB2F + 1, -SCREEN_HEIGHT_MAX / 2 - (SCREEN_HEIGHT_MAX - 600) / 2); // m_pLayerDark->raw_RelMove - nY
    Patch4(0x0055BB35 + 1, -SCREEN_WIDTH_MAX / 2);                                  // m_pLayerDark->raw_RelMove - nX
    // CField_LimitedView::DrawViewRange
    PatchCall(0x0055BEFE, &CField_LimitedView__raw_Copy_hook, 6);
    PatchCall(0x0055C08E, &CField_LimitedView__CopyEx_hook);
    PatchCall(0x0055C1DD, &CField_LimitedView__CopyEx_hook);

    // CUIScreenMsg - screen message width
    Patch4(0x0089AF33 + 1, SCREEN_MESSAGE_WIDTH);              // CUIScreenMsg::CUIScreenMsg
    Patch4(0x0089B2C6 + 1, SCREEN_MESSAGE_WIDTH);              // CUIScreenMsg::ScrMsg_Add
    PatchCall(0x0089B6FE, &CUIScreenMsg__raw_RelMove_hook, 6); // CUIScreenMsg::LayoutScrMsg
    PatchCall(0x0089BA13, &CUIScreenMsg__RelMove_hook);        // CUIScreenMsg::MoveScrMsg

    // CSlideNotice - sliding notice width
    Patch4(0x007E15BE + 1, SCREEN_WIDTH_MAX); // CSlideNotice::CSlideNotice
    Patch4(0x007E16BE + 1, SCREEN_WIDTH_MAX); // CSlideNotice::OnCreate
    Patch4(0x007E1E07 + 2, SCREEN_WIDTH_MAX); // CSlideNotice::SetMsg

    // CField::ShowMobHPTag - boss hp bar position
    PatchCall(0x00533705, &CField__ShowMobHPTag_hook1, 15); // nLeft

    // CUIEquip::IsMyAddon - fix equip window stuttering when moving
    Patch4(0x007FDF30 + 2, 0x5B4); // offsetof(CUIEquip, m_pUIPetEquip)

    // Gr2D_DX8.dll
    CWzGr2D::FindScreenMode = reinterpret_cast<CWzGr2D::FindScreenMode_t>(GetAddressByPattern("GR2D_DX8.DLL", "B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 EC 68"));
    CWzGr2D__AdjustCenterY_jmp = reinterpret_cast<uintptr_t>(GetAddressByPattern("GR2D_DX8.DLL", "8D 96 C4 00 00 00"));
    CWzGr2D__AdjustCenterY_ret = CWzGr2D__AdjustCenterY_jmp + 6;
    PatchJmp(CWzGr2D__AdjustCenterY_jmp, &CWzGr2D__AdjustCenterY_hook);

    // Force canvas textures to ARGB8888 instead of ARGB4444.
    // GR2D's format picker (sub_50402E1B) probes CheckDeviceFormat and, for the auto path, prefers
    // A4R4G4B4 (26) when the device supports it -- so every canvas texture is created 16-bit and our
    // 8888 (Format2) WZ bitmaps get downconverted to 4444. The selection is:
    //     neg eax; sbb eax,eax; and eax,5; add eax,0x15   ->  26 if A4R4G4B4 supported, else 21
    // Patching `and eax,5` -> `and eax,0` forces the result to 0x15 (D3DFMT_A8R8G8B8 = 21) always,
    // so canvases stay full 32-bit. (DXT3 branch left intact: skipping it would corrupt any
    // DXT3-compressed/format-1026 canvas, and the live downconvert here is the 4444 path, not DXT3.)
    if (BYTE* pFmtSel = reinterpret_cast<BYTE*>(
            GetAddressByPattern("GR2D_DX8.DLL", "39 7D DC 74 09 C7 45 E0 44 58 54 33"))) {
        BYTE zero = 0x00;
        PatchMemory(pFmtSel + 20, &zero, sizeof(zero)); // and eax,5 -> and eax,0  (force A8R8G8B8)
    }
}