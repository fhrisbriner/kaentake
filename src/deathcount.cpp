// ============================================================
// deathcount.cpp  —  Expedition Death Count HUD panel
//
// A passive, server-driven HUD panel: an 89x56 art panel with two digit wells, drawn left of
// the clock. The server owns the number entirely — this module counts nothing, decides nothing
// and sends nothing. It receives an int on S2C 0x3727 and draws it.
//
// Wire contract (little-endian):
//     0x3727  EXPED_DEATH_COUNT   [i32 value]   value >= 0 -> show min(value,99) zero-padded
//                                               value <  0 -> hide
//     0x007D  SET_FIELD           OBSERVED, never consumed -> hide the panel
//
// The auto-hide on SET_FIELD is what keeps this correct without the client knowing anything:
// it owns no map list, so it can never be wrong about whether you are still in the run. Every
// warp blanks the panel and the server re-sends on entering an instance map. Leaving the run
// just means the value never comes back.
//
// -------------------------------------------------------------------------------------
// ADAPTATIONS from the reference guide, which was written against a different DLL:
//
//  * ORIGIN. The guide anchors the layer to the CWndMan LEFT-TOP UI origin via a
//    ScreenOriginLT() helper. There is no such helper here, and CWndMan::GetOrgWindowEx() is
//    DEAD in this client -- it throws, because ResetOrgWindow is never called (see the comment
//    above CreateDragIconLayer in storagebag.cpp, which hit exactly this and had to borrow a
//    live window origin instead). So this layer is created with NO origin and positioned in
//    absolute screen coordinates, recomputed from get_screen_width() so it still follows a
//    resolution change. If the panel ever renders in world space (scrolling with the map
//    instead of holding position), set deathCountBorrowStatusBarOrigin = 1 -- that borrows the
//    always-present status bar live HUD origin, the same trick storagebag.cpp uses.
//
//  * TICK. Driven from CWvsApp::CallUpdate_hook (bypass.cpp), the main-thread 30ms step, which
//    is where WZ and UI work belongs. NOT the render callback.
//
//  * PACKET. Routed from CClientSocket__ProcessPacket (packet.cpp). That hook peeks the opcode
//    and RESTORES m_uOffset, so on entry the cursor is still on the opcode and the body starts
//    at +2.
// ============================================================

#include "pch.h"
#include "hook.h"
#include "debug.h"
#include "wvs/packet.h"
#include "wvs/util.h"
#include "wvs/wnd.h"
#include "wvs/statusbar.h"
#include "ztl/ztl.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cwchar>

// ---- WZ nodes -----------------------------------------------------------------
static const wchar_t* const kNode_Backgrd   = L"UI/DeathCount.img/backgrd";
static const wchar_t* const kNode_NumberFmt = L"UI/DeathCount.img/number/%d";

// ---- panel geometry -----------------------------------------------------------
static constexpr int kPanelW = 89, kPanelH = 56;   // == the backgrd canvas, blitted at (0,0)

// The background art draws two dim ghost-8 wells where the digits belong. These are the
// measured centres of those wells, inside the panel canvas.
static constexpr double kWellTensCX = 59.5;        // left  well (tens)  centre X
static constexpr double kWellOnesCX = 69.5;        // right well (units) centre X
static constexpr double kWellCY     = 37.0;        // both wells centre Y

// Visible-content centres (alpha bounding-box midpoints) of each glyph. These exist because the
// glyphs are NOT uniform: digit 1 is drawn right-justified inside its own 12px canvas (content
// in columns 7..10, centre 8.5) while the others are centred (content 1..10, centre 5.5).
// Corner-aligning a glyph to its well puts the 1 about 3px right of where it belongs, which is
// very obvious on values like 15. Blitting content-centre onto well-centre makes that cancel.
static constexpr double kDigitCX[10] = {5.5, 8.5, 5.5, 4.5, 5.5, 5.5, 5.5, 4.5, 5.5, 5.5};
static constexpr double kDigitCY[10] = {8.0, 8.0, 8.0, 8.0, 7.0, 8.0, 8.0, 7.5, 8.0, 8.0};

// ---- visual knobs -------------------------------------------------------------
// The v83 clock is top-CENTRE and the panel goes wholly to its LEFT, tops level. The X knob is
// added to screenW/2 so the placement follows every resolution instead of being pinned to one.
int deathCountXOffset = -217;
int deathCountY       = 30;
static constexpr int kPanelZ = 1400;   // HUD band: above world/banners, below modal gauges

static constexpr int kMaxValue = 99;   // two digit wells

// See the ORIGIN note in the header comment. 0 = absolute screen coords (default).
int deathCountBorrowStatusBarOrigin = 0;

// THE one colour that ERASES an IWzCanvas: DrawRectangle BLENDS, so 0x00000000 is a no-op and
// would leave the previous digits showing underneath the new ones.
static constexpr unsigned int kCanvasClear = 0x00FFFFFFu;

// ---- state ---------------------------------------------------------------------
static int  s_nValue    = -1;      // last value the SERVER sent; < 0 == hidden
static bool s_bDirty    = false;   // a packet (or field change) landed -> the tick has work
static bool s_bDisabled = false;   // latched off on the first hard fault this session
static int  s_nArtState = -1;      // -1 unprobed, 0 probed absent, 1 loaded

static IWzCanvasPtr   s_pPanel;
static IWzCanvasPtr   s_pSprBackgrd;
static IWzCanvasPtr   s_apSprDigit[10];
static IWzGr2DLayer*  s_pLayer  = nullptr;
static int            s_nDrawn  = -2;   // last value actually composited
static int            s_nLayerX = -1;
static int            s_nLayerY = -1;


// ---- helpers -------------------------------------------------------------------
static IWzCanvasPtr LoadSprite(const wchar_t* sPath) {
    IWzCanvasPtr c;
    try { c = get_unknown(get_rm()->GetObjectA(const_cast<wchar_t*>(sPath))); } catch (...) {}
    return c;
}

static void Blit(IWzCanvasPtr dst, IWzCanvasPtr src, int x, int y) {
    if (!dst || !src) return;
    try { dst->CopyEx(x, y, src, CANVAS_ALPHATYPE::CA_REMOVEALPHA, 0, 0, 0, 0, 0, 0); } catch (...) {}
}

static IWzCanvasPtr MakeCanvas(int cw, int ch) {
    IWzCanvasPtr c;
    try {
        PcCreateObject<IWzCanvasPtr>(L"Canvas", c, nullptr);
        if (c) {
            c->Create(cw, ch, vtMissing, vtMissing);
            c->DrawRectangle(0, 0, cw, ch, kCanvasClear);
        }
    } catch (...) { c = nullptr; }
    return c;
}

// Absolute screen position of the panel, recomputed every tick so a resolution change follows.
static void PanelPos(int* px, int* py) {
    *px = get_screen_width() / 2 + deathCountXOffset;
    *py = deathCountY;
}

// Only consulted when deathCountBorrowStatusBarOrigin is on. The status bar is always present
// in-game and is a real CWnd, so its layer carries a live HUD origin.
static IUnknown* BorrowedOrigin() {
    if (!deathCountBorrowStatusBarOrigin) return nullptr;
    try {
        if (!CUIStatusBar::IsInstantiated()) return nullptr;
        CUIStatusBar* sb = CUIStatusBar::GetInstance();
        if (!sb) return nullptr;
        IWzGr2DLayerPtr& layer = sb->m_pLayer;
        if (!layer) return nullptr;
        return static_cast<IUnknown*>(layer->origin);
    } catch (...) { return nullptr; }
}

static void LayerShow(bool bShow) {
    if (!s_pLayer) return;
    try { s_pLayer->visible = bShow ? 1 : 0; } catch (...) {}
}

static void Release(const char* pszWhy) {
    if (s_pLayer) {
        LayerShow(false);
        try { s_pLayer->Release(); } catch (...) {}
        s_pLayer = nullptr;
        LogInfo("[deathcount] layer released (%s)", pszWhy ? pszWhy : "");
    }
    s_pPanel = nullptr;
    s_nDrawn = -2;
    s_nLayerX = s_nLayerY = -1;
}


// ---- art -----------------------------------------------------------------------
// Absence is a SUPPORTED state, never a crash: a client without DeathCount.img simply never
// shows the panel, logs one line, and stops asking.
static bool LoadArt() {
    if (s_nArtState == 0) return false;
    if (s_pSprBackgrd)    return true;

    IWzCanvasPtr pBg = LoadSprite(kNode_Backgrd);
    if (!pBg) {
        s_nArtState = 0;
        LogInfo("[deathcount] art pending (%ls absent) -- HUD dormant for this session", kNode_Backgrd);
        return false;
    }

    for (int i = 0; i < 10; ++i) {
        wchar_t sPath[96];
        _snwprintf_s(sPath, _countof(sPath), _TRUNCATE, kNode_NumberFmt, i);
        s_apSprDigit[i] = LoadSprite(sPath);
    }
    s_pSprBackgrd = pBg;
    s_nArtState = 1;
    LogInfo("[deathcount] art loaded");
    return true;
}


// ---- painting ------------------------------------------------------------------
static void BlitDigit(int nDigit, double dWellCX, double dWellCY) {
    if (nDigit < 0 || nDigit > 9 || !s_apSprDigit[nDigit]) {
        return;   // a glyph that failed to load leaves its well empty -- never a crash
    }
    const int x = static_cast<int>(std::lround(dWellCX - kDigitCX[nDigit]));
    const int y = static_cast<int>(std::lround(dWellCY - kDigitCY[nDigit]));
    Blit(s_pPanel, s_apSprDigit[nDigit], x, y);
}

static void Compose(int nShown) {
    if (!s_pPanel) return;
    try { s_pPanel->DrawRectangle(0, 0, kPanelW, kPanelH, kCanvasClear); } catch (...) {}
    Blit(s_pPanel, s_pSprBackgrd, 0, 0);
    if (nShown < 0) return;
    BlitDigit((nShown / 10) % 10, kWellTensCX, kWellCY);   // zero-padded, always two digits
    BlitDigit(nShown % 10,        kWellOnesCX, kWellCY);
}


// ---- layer ---------------------------------------------------------------------
// Built LAZILY on the tick, never at DLL attach: the graphics device does not exist yet at
// attach time, and neither does the status bar we may borrow an origin from.
static bool Build() {
    if (s_pLayer) return true;
    if (!LoadArt()) return false;

    try {
        IWzGr2DPtr& gr = get_gr();
        if (!gr) return false;

        IWzCanvasPtr pPanel = MakeCanvas(kPanelW, kPanelH);
        if (!pPanel) return false;

        int x = 0, y = 0;
        PanelPos(&x, &y);

        IWzGr2DLayerPtr layer = gr->CreateLayer(0, 0, kPanelW, kPanelH, kPanelZ,
                                                static_cast<IUnknown*>(pPanel), vtMissing);
        if (!layer) return false;

        if (IUnknown* pOrigin = BorrowedOrigin()) {
            try { layer->origin = pOrigin; } catch (...) {}
        }
        layer->width  = kPanelW;
        layer->height = kPanelH;
        layer->color  = 0xFFFFFFFF;
        layer->RelMove(x, y);
        layer->visible = 0;   // the tick turns it on once composed

        IWzGr2DLayer* raw = layer;
        raw->AddRef();

        s_pPanel  = pPanel;
        s_pLayer  = raw;
        s_nDrawn  = -2;
        s_nLayerX = x;
        s_nLayerY = y;
        LogInfo("[deathcount] layer built at %d,%d (z=%d)", x, y, kPanelZ);
        return true;
    } catch (...) {
        return false;
    }
}


// ---- tick ----------------------------------------------------------------------
static void Tick() {
    if (!s_bDirty && !s_pLayer) return;    // idle: two bool reads per step

    if (s_nValue < 0) {
        Release("value < 0");
        s_bDirty = false;
        return;
    }

    if (!Build()) {
        if (s_nArtState == 0) s_bDirty = false;   // no art -> stop asking
        return;                                   // otherwise retry next step
    }

    int x = 0, y = 0;                             // follow a resolution change
    PanelPos(&x, &y);
    if (x != s_nLayerX || y != s_nLayerY) {
        try { s_pLayer->RelMove(x, y); } catch (...) {}
        s_nLayerX = x;
        s_nLayerY = y;
    }

    const int nShown = (s_nValue > kMaxValue) ? kMaxValue : s_nValue;
    if (nShown != s_nDrawn) {
        Compose(nShown);
        s_nDrawn = nShown;
    }
    LayerShow(true);
    s_bDirty = false;
}


// ---- exports -------------------------------------------------------------------
// Routed here on S2C 0x3727, which packet.cpp then SWALLOWS. Body: [i32 value].
// Deliberately does NO engine work -- it records the number and marks the HUD dirty, so every
// WZ/graphics call stays on the main-thread step where it belongs.
void DeathCount_OnPacket(CInPacket* pPacket) {
    if (s_bDisabled || !pPacket) return;
    // ProcessPacket restored m_uOffset, so the cursor is still on the opcode: [op][i32].
    if (pPacket->m_uOffset + 2 + 4 > pPacket->m_uLength) return;
    const int nValue =
            *reinterpret_cast<const int*>(&pPacket->m_aRecvBuff.a[pPacket->m_uOffset + 2]);
    if (nValue == s_nValue) return;   // idempotent re-send
    s_nValue = nValue;
    s_bDirty = true;
}

// SET_FIELD went past -- OBSERVED, never consumed. Hide until the server says otherwise.
void DeathCount_OnFieldChange() {
    if (s_nValue < 0 && !s_pLayer) return;
    s_nValue = -1;
    s_bDirty = true;
    LayerShow(false);   // the actual release happens on the next step
}

void DeathCount_OnClientTick() {
    if (s_bDisabled) return;
    try { Tick(); }
    catch (...) {
        s_bDisabled = true;      // passive read-out: latch off, never take the client with us
        Release("fault");
        LogInfo("[deathcount] disabled for this session after a fault");
    }
}
