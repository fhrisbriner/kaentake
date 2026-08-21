// ============================================================
// mistexplosion.cpp  —  Mist Explosion (2121040)
//
// Detonates Poison Mist clouds the local player created: every mob standing inside a mist's
// footprint takes the skill's damage, and the mist is consumed.
//
// The whole feature turns on one fact about the client: a mist is a CAffectedArea, and the area
// object carries the exact LTRB the server gave it. So the explosion radius is not something we
// invent -- it is read straight out of the cloud that is already on screen, which is why it lines
// up with what the player sees.
//
// ---- CAffectedArea layout -------------------------------------------------------------------
// Established from CAffectedAreaPool::OnAffectedAreaCreated (0x00431A63), which decodes the
// creation packet field by field into a freshly allocated area:
//
//     +0x00  nAreaId          Decode4   @0x431A92   (the id OnAffectedAreaRemoved takes)
//     +0x04  nType            Decode4   @0x431A9C   2 = smoke/mist, 3 = item area
//     +0x08  dwOwnerCharId    Decode4   @0x431AA6
//     +0x0C  nSkillId         Decode4   @0x431AAF   compared against 2111003 @0x431D3B
//     +0x10  nSkillLevel      Decode1   @0x431ABC
//     +0x14  tStart                     @0x431B50
//     +0x18  tEnd                       @0x431CC3   tStart + 1000 * duration
//     +0x20  RECT (l,t,r,b)   DecodeBuffer(16) @0x431AD2, stored @0x431B34..0x431B37
//
// The +0x04 type and +0x20 rect are corroborated by IsSmokeAreaByPoint (0x0043193A), which tests
// `area[1] == 2` and then calls PtInRect on `area + 8` (= byte +0x20).
//
// ---- Pool iteration --------------------------------------------------------------------------
// Also from IsSmokeAreaByPoint: the areas hang off pool[4] as a ZList whose links sit BEHIND the
// node. Walking it is
//     node = pool[4];
//     while (node) { next = *(node - 16 + 4); area = *(node + 4); node = next ? next + 16 : 0; }
// which is reproduced verbatim in ForEachArea below.
// ============================================================

#include "pch.h"
#include "hook.h"
#include "debug.h"
#include "constants.h"
#include "wvs/packet.h"
#include "wvs/util.h"
#include "ztl/ztl.h"

#include <windows.h>
#include <vector>

// ---- addresses ---------------------------------------------------------------
static void** const kppAffectedAreaPool = reinterpret_cast<void**>(0x00BEBF68);
static void** const kppMobPool          = reinterpret_cast<void**>(0x00BEBFA4);
static void** const kppUserLocal        = reinterpret_cast<void**>(0x00BEBF98);

using t_GetSkill = void*(__thiscall*)(void*, int);
static auto CSkillInfo_GetSkill = reinterpret_cast<t_GetSkill>(0x0075C755);
static void** const kppSkillInfo = reinterpret_cast<void**>(0x00BE78DC);

// CUserLocal +0x11A8 is the local character id (CUserLocal::Update reads it at 0x0094B34A to
// hand to IsSmokeAreaByPoint as the "mine" comparand -- exactly the test we need here).
static constexpr int kOfsUserLocalCharId = 0x11A8;

// long CMobPool::FindHitMobInRect(const RECT&, CMob** apMob, long nMax, CMob* pExclude,
//                                 long, long, unsigned long, int)
//
// EIGHT stack args, from `retn 20h` at 0x00678603 and confirmed by the eight pushes at the
// client's own call site (0x009565A9..0x009565BE). IDA's mangled name lists nine and is stale --
// the same trap ShowSkillEffect and CMob::OnHit both had. The extra argument this typedef used to
// declare was pushed on every call and never popped by the callee; an EBP frame absorbed it, but
// it was four bytes of stack leaked per cloud per cast.
using t_FindHitMobInRect = long(__thiscall*)(void*, const RECT*, void**, long, void*,
                                             long, long, unsigned long, int);
static auto FindHitMobInRect = reinterpret_cast<t_FindHitMobInRect>(0x00678476);

// The engine's own teardown for one area: fades its layers out and unlinks it. Takes the area id
// off a CInPacket, so we hand it a synthetic one (see RemoveArea). Reusing this rather than
// unlinking by hand is deliberate -- it is what gives the cloud its dissolve instead of having it
// vanish between frames.
using t_OnAffectedAreaRemoved = void*(__thiscall*)(void* pool, void* pInPacket);
static auto OnAffectedAreaRemoved = reinterpret_cast<t_OnAffectedAreaRemoved>(0x0043234D);

// ---- tunables ----------------------------------------------------------------
// How much room is left on this thread's stack, as " [stack=NNNNb]".
//
// Every crash in this feature's history resolves to the SAME instruction: `push ebx` at +0x1F of
// the CRT's common_vsprintf, immediately after it does `sub esp, 474h`. That is a guard-page hit,
// not corruption -- the stack simply ran out, which is also why the vectored handler never logged
// anything (it needs stack to run) and why the process vanished with no dialog. vsprintf is not
// the culprit; it just has the largest frame nearby, so it is always the one that tips over.
//
// The TEB carries the real bounds, so this is a couple of reads and no API call.
static const char* StackBrief() {
    static char s_sBrief[48];
    NT_TIB* pTib = reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    if (!pTib) {
        s_sBrief[0] = 0;
        return s_sBrief;
    }
    const char* pLimit = static_cast<const char*>(pTib->StackLimit);
    char cHere;
    const ptrdiff_t nLeft = &cHere - pLimit;
    _snprintf_s(s_sBrief, sizeof(s_sBrief), _TRUNCATE, "  [stack=%ldb]", static_cast<long>(nLeft));
    return s_sBrief;
}

// Flushed breadcrumbs through the detonation.
//
// A run on 2026-08-21 stopped dead one statement after the survey: no fault, no ExitProcess, no
// crash log -- the signature of a HANG rather than a crash, which leaves nothing behind at all.
// With the log buffered, "where did it stop" was unanswerable; each of these costs one flush and
// turns the next occurrence into an exact statement.
//
// Deliberately unconditional. They only run during a cast, and a skill that can take the client
// down is worth a dozen lines per use until it does not.
static void Step(const char* pszWhat) {
    LogInfo("[mistexplosion] step: %s%s", pszWhat, StackBrief());
    LogFlush();
}

int mistExplosionSkillId = 2121040;

// One attack per cloud, each originating at that cloud, instead of a single pooled attack.
//
// Costs MP per cloud. The server runs attackEffect.applyTo(player) for EVERY attack packet
// (AbstractDealDamageHandler ~line 146), so five clouds is five times the skill's MP cost. That
// is the whole reason this is a switch and not just the new behaviour.
//
// It also drops the cross-cloud de-duplication: a mob standing in two overlapping clouds is hit
// once by each, which is the point -- five clouds means five attacks -- but it does mean overlap
// is now worth more damage than spread.
int mistExplosionPerMistAttack = 1;

// Master switch, default OFF.
//
// This feature hooks CMobPool::FindHitMobInRect, which is on the target-search path of EVERY magic
// attack and every summon in the game -- not just this skill. A bug in it does not break Mist
// Explosion, it breaks combat. It disconnected a client during testing, so it stays off until the
// cast path is proven; set to 1 to test.
int mistExplosionEnabled = 1;

// Only Poison Mist. Kept as a list so another mist can be added without touching the logic, but
// the default is deliberately just the one: every other mist-type area in the client (Smokescreen,
// Ice Demon, Flame Gear) has its own semantics and would need its own damage story.
static const std::vector<int> g_mistSourceSkills = {
    2111003,   // Poison Mist
};

// FindHitMobInRect writes into a caller-supplied array with NO bound check of its own beyond the
// count we pass, so this cap and the array size below must stay in step.
static constexpr int kMaxMobsPerMist = 15;
// No type gate. It used to require type == 2, taken from IsSmokeAreaByPoint's `area[1] == 2` --
// but that function looks for SMOKE specifically (Smokescreen), not mists in general. A live
// survey showed Poison Mist areas arrive as type 1, so the check silently matched nothing:
//     area=1000000090 type=1 owner=48 skill=2111003 rect=(92,-118,692,202)   0 match
// The skill-id whitelist below is the precise filter and the type adds nothing to it, so type is
// now recorded in the log and not used as a condition.

static bool IsMistSourceSkill(int nSkillId) {
    for (int id : g_mistSourceSkills) {
        if (id == nSkillId) {
            return true;
        }
    }
    return false;
}

// ---- area access -------------------------------------------------------------
struct AffectedArea {
    int nAreaId;      // +0x00
    int nType;        // +0x04
    int dwOwnerId;    // +0x08
    int nSkillId;     // +0x0C
    RECT rc;          // +0x20
};

static bool ReadArea(void* pArea, AffectedArea& out) {
    if (!pArea || IsBadReadPtr(pArea, 0x30)) {
        return false;
    }
    const int* p = reinterpret_cast<const int*>(pArea);
    out.nAreaId   = p[0];
    out.nType     = p[1];
    out.dwOwnerId = p[2];
    out.nSkillId  = p[3];
    out.rc = *reinterpret_cast<const RECT*>(reinterpret_cast<const char*>(pArea) + 0x20);
    return true;
}

static int GetLocalCharId() {
    void* pUser = *kppUserLocal;
    if (!pUser || IsBadReadPtr(pUser, kOfsUserLocalCharId + 4)) {
        return 0;
    }
    return *reinterpret_cast<int*>(reinterpret_cast<char*>(pUser) + kOfsUserLocalCharId);
}

// Walks the pool. Snapshotting rather than acting inline is not optional: the explosion REMOVES
// areas, and mutating the list while iterating it is how you get a torn link.
template <typename TFn>
static void ForEachArea(TFn&& fn) {
    void* pPool = *kppAffectedAreaPool;
    if (!pPool || IsBadReadPtr(pPool, 0x14)) {
        return;
    }
    int node = reinterpret_cast<int*>(pPool)[4];
    int nGuard = 0;
    while (node && ++nGuard < 512) {   // guard: a corrupt link must not spin the game thread
        const int* pNode = reinterpret_cast<const int*>(node);
        if (IsBadReadPtr(reinterpret_cast<const void*>(node - 16), 24)) {
            return;
        }
        const int next = *reinterpret_cast<const int*>(node - 16 + 4);
        void* pArea = reinterpret_cast<void**>(const_cast<int*>(pNode))[1];
        node = next ? next + 16 : 0;
        if (pArea) {
            fn(pArea);
        }
    }
}

// ---- removal -----------------------------------------------------------------
// OnAffectedAreaRemoved reads one Decode4 (the area id) off a CInPacket. Rather than replicate
// its teardown -- layer fade, canvas removal, unlink, and a second pass over the whole pool -- we
// hand it a minimal packet whose buffer is just that id.
//
// Decode4 reads at m_uOffset and bounds-checks against m_uLength, so a 4-byte buffer with
// length 4 and offset 0 is exactly what it expects and nothing else is touched.
static void RemoveArea(int nAreaId) {
    void* pPool = *kppAffectedAreaPool;
    if (!pPool) {
        return;
    }

    int nId = nAreaId;   // Decode4 reads this directly out of the buffer

    // A RAW buffer shaped like CInPacket -- NOT a real one. CInPacket holds a ZArray<char> whose
    // destructor calls ZAllocEx::s_Free(a - 1); a real CInPacket on the stack, pointed at stack
    // memory, would hand a stack address to the game allocator the moment it left scope. The
    // offsets come from the real class so this cannot silently drift if the layout changes.
    alignas(8) char pkt[sizeof(CInPacket)]{};
    *reinterpret_cast<char**>(pkt + offsetof(CInPacket, m_aRecvBuff))    = reinterpret_cast<char*>(&nId);
    *reinterpret_cast<uint16_t*>(pkt + offsetof(CInPacket, m_uLength))   = 4;
    *reinterpret_cast<uint16_t*>(pkt + offsetof(CInPacket, m_uDataLen))  = 4;
    *reinterpret_cast<uint32_t*>(pkt + offsetof(CInPacket, m_uOffset))   = 0;

    try {
        OnAffectedAreaRemoved(pPool, pkt);
    } catch (...) {
    }
}

// ---- effect ------------------------------------------------------------------
// "Draw the skill's effect onto the clouds we are exploding."
//
// There is no general "play this animation at this point" entry point in this client that is
// usable from here. The three candidates all fail for concrete reasons, recorded so nobody
// re-walks them: CUser::ShowSkillEffect (0x00933990) draws relative to the user and its trailing
// POINT* is dead -- the function is `retn 14h`, five stack args, and IDA's six-arg symbol with
// the POINT is stale. CAnimationDisplayer::Effect_General takes _com_ptr_t parameters BY VALUE,
// so calling it means constructing COM smart pointers by hand. RegisterFireCrackerAnimation has
// a clean C ABI but its list (displayer+0xF8) is only walked by NonFieldUpdate, so nothing it
// registers renders on a field. RegisterFootHoldAnimation snaps to footholds and steps the rect
// by the WZ's `effectDistance` -- a missing or zero value is an infinite loop on the game thread.
//
// What DOES place a WZ animation across an arbitrary LTRB is the affected-area pool itself: it is
// what draws the mist in the first place. OnAffectedAreaCreated is the exact mirror of the
// OnAffectedAreaRemoved we already drive, and takes the same kind of synthetic packet, so the
// explosion is drawn by the same machinery that drew the cloud -- at the cloud's own rect.
using t_OnAffectedAreaCreated = void*(__thiscall*)(void* pool, void* pInPacket);
static auto OnAffectedAreaCreated = reinterpret_cast<t_OnAffectedAreaCreated>(0x00431A63);

// OFF by default: synthesising OnAffectedAreaCreated is not safe.
//
// Three runs on 2026-08-21 died the same way once 2121040 gained its `tile` node and this path
// began doing real work. The breadcrumbs are unambiguous -- "spawn: call" is written and flushed,
// "spawn: returned" is not -- and every fault resolves to the CRT's vsprintf, which is simply the
// next code to run rather than the culprit: the call corrupts the process and the following log
// line falls over.
//
// OnAffectedAreaCreated is far more than the decoder its sibling OnAffectedAreaRemoved is. It
// builds COM objects, loads resources through IWzResMan, and carries its own EH frame, and it
// expects a real CInPacket delivered by the packet loop rather than a stack buffer shaped like
// one. The packet layout is confirmed correct (DecodeBuffer @0x432257: buffer +8, length +12,
// offset +20) and the field order matches byte for byte, so the fault is in the call's
// expectations, not its input.
//
// Set to 1 only to keep investigating. The durable fix is to let the SERVER spawn the visual --
// it already places mists at a chosen Point for POISON_MIST_STRIKE and SCORCHED_EARTH -- so the
// client receives a REAL area-created packet through the normal path and none of this is needed.
int mistExplosionDrawEffect = 0;
int mistExplosionEffectMs   = 900;

// Which skill's artwork gets drawn, kept separate from which skill CASTS the explosion.
//
// The branch this feeds reads SKILLENTRY+0x88, which the WZ loader fills only from a `tile` node
// -- `tile/<variant>/<frame>`, canvases. 2121040 has effect/effect0/hit and no tile, so until one
// is authored there is nothing to draw and the gate below refuses it.
//
// Two stock skills already carry a tile and are already in the client's own accepted list, so
// setting this to either shows the mechanism working with no WZ change at all:
//   4221006  Smokescreen  -- one variant, 8 frames, 511x318, a wide ground-hugging puff
//   2111003  Poison Mist  -- 11 variants, 2 frames each, 135x95, tiled across the rect
// Both bypass the cave entirely (the client accepts them unmodified), which also makes them a
// clean way to tell a WZ problem apart from a patch problem.
int mistExplosionEffectSkillId = 2121040;

// Teach the client that 2121040 is an area skill worth drawing. Independent of
// mistExplosionDrawEffect on purpose.
//
// These were one switch until the synthesis path proved unsafe, and they are not the same thing.
// The gate is a compare redirect and nothing more; it does not call anything, and it is what lets
// a mist the SERVER spawns for 2121040 render at all, since the stock accepted set is only
// {130, 131, 2111003, 4221006, 12111005, 14111006, 22161003}. The server now spawns that blast
// itself (MapleMap.detonatePlayerMists), so the gate is the whole client-side requirement and the
// synthesis is not needed.
// Depends entirely on the art in skill/<id>/tile being tileable.
//
// The branch this opens (sub_432776 @0x00432776) steps ACROSS the rect by a fixed 35-54px, but
// steps DOWN it by the canvas's own height. A frame one pixel tall therefore collapses the
// vertical step to ~1px, and a 320px-tall cloud produces some 320 layers per column instead of
// two or three -- thousands of Gr2D layers and canvases in a single frame. That is heap
// corruption, and it presents as a crash seconds later with nothing useful in the log.
//
// Cost of getting it wrong is the whole client, so the art matters as much as the code: frames
// must be uniform in height and none may be tiny. The stock nodes are exactly that (2111003 is
// 2 frames of 135x95, 4221006 is 8 of 511x318). A skill's `effect` node is NOT a substitute --
// it is a zoom sequence that usually opens on a 1x1 spacer.
//
// The alternative, if the art is ever a problem again, needs no client patch at all: map the id
// to a natively-accepted skill in Mist.getMistVisualSkillId(), the way that function already
// handles POISON_MIST_STRIKE -> 14111006 and SCORCHED_EARTH -> 12111005.
int mistExplosionAreaGate = 1;

// Server area ids are small sequential positives. Ours are stamped into a range it will never
// reach, so a synthetic effect area can never be confused with (or removed as) a real one.
static constexpr int kEffectAreaIdBase = 0x40000000;
static int g_nEffectAreaSeq = 0;

// OnAffectedAreaCreated only draws for a hardcoded set of skill ids: 130 and 131 (item areas),
// then 2111003 Poison Mist, 4221006 Smokescreen, 12111005 Flame Gear, 14111006 Smokescreen(NW),
// 22161003. The test is a CUMULATIVE `sub ecx, imm / jz` chain at 0x00431D27, which is why it
// cannot be edited one entry at a time -- changing any immediate shifts every entry after it.
//
// So this does not edit the chain. It prepends to it: a cave at the 5-byte `mov eax, 82h` that
// opens the chain answers from the list below and, on a miss, performs the instruction it
// replaced and drops into the original chain untouched. Every stock id keeps working.
std::vector<int> g_areaEffectSkills = { 2121040 };

static int g_bAreaSkillMatch = 0;

// SKILLENTRY+0x88 is the skill's cached area-animation UOL. The WZ loader fills it in only when
// the skill actually has the node for it, and 2121040 (effect/effect0/hit) does not.
//
// A null there is NOT harmless. IWzResMan::GetObjectA dereferences it and faults inside the
// resource manager -- observed 2026-08-21 as 0xC0000005 reading 0x00000000 at RESMAN.DLL+0x38C0,
// with 0x00431F60 on the stack, which is GetObjectA's call site in this very branch. So the
// answer has to be no unless the node is really there.
static void* GetAreaEffectUol(int nSkillId) {
    void* pInfo = *kppSkillInfo;
    if (!pInfo) {
        return nullptr;   // skill info is not loaded yet; nothing to draw with
    }
    void* pSkill = nullptr;
    try {
        pSkill = CSkillInfo_GetSkill(pInfo, nSkillId);
    } catch (...) {
        return nullptr;
    }
    if (!pSkill || IsBadReadPtr(pSkill, 0x8C)) {
        return nullptr;
    }
    return reinterpret_cast<void**>(static_cast<char*>(pSkill))[34];   // +0x88
}

// ---- tiling ------------------------------------------------------------------
// sub_432776 lays an area's artwork out like this:
//
//   columnX = rect.left
//   while columnX < rect.right:
//       arc = height * (abs(columnX - centreX) / width * 0.5)   <- dome, sags at the edges
//       y   = rect.top + arc
//       while y < rect.bottom - arc:
//           place a RANDOM frame at (columnX, y)
//           y += canvasHeight * 0.8                             <- 20% vertical overlap
//       columnX += random(0..19) + 35                           <- 35..54px columns
//
// Four constants, all baked in, all shared with Poison Mist and Smokescreen. Changing them in
// place would restyle every area skill in the game, so instead the two doubles are read through
// pointers we own and the two immediates come from a cave -- and this function, which the gate
// calls on EVERY area creation, decides which set is live before the layout runs.
static constexpr int    kStockTileStepBase  = 35;
static constexpr int    kStockTileStepRange = 20;
static constexpr double kStockTileYFactor   = 0.8;
static constexpr double kStockTileArcFactor = 0.5;

// Live values, read by the client mid-layout. Seeded to stock so anything that runs before the
// first area creation still behaves exactly as the unmodified client.
static int    g_bTileCentre    = 0;   // 0 keeps the stock top-left origin exactly
static int    g_nTileOriginX   = 0;
static int    g_nTileOriginY   = 0;
static int    g_nTileStepBase  = kStockTileStepBase;
static int    g_nTileStepRange = kStockTileStepRange;
static double g_dTileYFactor   = kStockTileYFactor;
static double g_dTileArcFactor = kStockTileArcFactor;

// Tunables for OUR skills only. Wider columns and a flatter arc read as one blast rather than a
// field of repeated sprites, which is what a large explosion frame wants.
int    mistTileStepBase  = 600;    // px between columns, before jitter
int    mistTileStepRange = 0;    // extra 0..N-1 px of jitter per column (0 = perfectly even)
double mistTileYFactor   = 1.0;   // vertical step as a fraction of frame height
double mistTileArcFactor = 0.1;  // 0 = flat band, 0.5 = the stock dome

// Where the walk BEGINS. The layout starts at the rect's top-left corner and marches right and
// down -- correct for a cloud filling its footprint, wrong for a single blast meant to sit in the
// middle of one. The frames are anchored near their own centres (2121040's are origin (212,262)
// on 411x419), so the first one lands centred ON the top-left corner with half of it outside the
// cloud. With a step wider than the rect only one column is ever placed, so that first frame IS
// the explosion and where it starts is where the explosion appears.
int mistTileCentre  = 1;   // begin at the rect's centre instead of its top-left corner
int mistTileOriginX = 0;   // extra px applied to the first column, after centring
int mistTileOriginY = 0;   // extra px applied to the first row, after centring

// Refusing here is the whole safety story: on a no, the cave performs the instruction it replaced
// and the stock chain runs, which is exactly the unmodified client.
int __cdecl IsAreaEffectSkill(int nSkillId) {
    bool bMine = false;
    for (int id : g_areaEffectSkills) {
        if (id == nSkillId) {
            bMine = true;
            break;
        }
    }

    // Selected here because this is the one place that sees the skill id before the layout runs.
    // Every other area skill is handed the stock numbers, so nothing else changes appearance.
    g_bTileCentre    = bMine ? mistTileCentre    : 0;
    g_nTileOriginX   = bMine ? mistTileOriginX   : 0;
    g_nTileOriginY   = bMine ? mistTileOriginY   : 0;
    g_nTileStepBase  = bMine ? mistTileStepBase  : kStockTileStepBase;
    g_nTileStepRange = bMine ? mistTileStepRange : kStockTileStepRange;
    g_dTileYFactor   = bMine ? mistTileYFactor   : kStockTileYFactor;
    g_dTileArcFactor = bMine ? mistTileArcFactor : kStockTileArcFactor;

    // A zero range would be a divide-by-zero inside the client's own layout loop.
    if (g_nTileStepRange < 1) {
        g_nTileStepRange = 1;
    }
    // A non-positive column step never advances -- an infinite loop allocating layers, which is
    // the same failure that a 1px frame produced.
    if (g_nTileStepBase < 1) {
        g_nTileStepBase = 1;
    }

    return bMine ? (GetAreaEffectUol(nSkillId) ? 1 : 0) : 0;
}

// The column-advance at the tail of sub_432776's outer loop:
//   004329DC  6A 14           push 14h                 ; jitter range
//   004329DE  33 D2           xor  edx, edx
//   004329E0  59              pop  ecx
//   004329E1  F7 F1           div  ecx                 ; edx = random % 20
//   004329E3  8B 45 F0        mov  eax, [ebp-10h]      ; current column x
//   004329E6  8D 44 10 23     lea  eax, [eax+edx+23h]  ; + jitter + 35
//   004329EA  3B 47 08        cmp  eax, [edi+8]        ; resume: vs rect.right
// Fourteen bytes replaced, resuming at the compare with the new x in eax.
static constexpr DWORD kTileStepPatch  = 0x004329DC;
static constexpr int   kTileStepLen    = 14;
static constexpr DWORD kTileStepResume = 0x004329EA;

// Operand fields of `fmul qword ptr ds:[addr]` -- the 4-byte absolute address, not the opcode.
//   00432984  DC 0D 40 0D AF 00   fmul qword ds:dbl_AF0D40   (0.8, vertical step)
//   004327F2  DC 0D 48 0D AF 00   fmul qword ds:dbl_AF0D48   (0.5, arc)
// Repointing these at our own doubles is what makes the layout per-skill: the constants stay
// untouched in the client image, so any skill we do not claim reads exactly what it always did.
static constexpr DWORD kTileYFactorOperand   = 0x00432986;
static constexpr DWORD kTileArcFactorOperand = 0x004327F4;

// Start of the column walk:
//   004327B3  89 75 F0        mov [ebp-10h], esi   ; v57 = rect.left   <- the origin
//   004327B6  89 45 EC        mov [ebp-14h], eax   ; v56 = height
//   004327B9  8D 04 0E        lea eax, [esi+ecx]   ; resume: (left+right) for the centre calc
static constexpr DWORD kTileInitPatch  = 0x004327B3;
static constexpr int   kTileInitLen    = 6;
static constexpr DWORD kTileInitResume = 0x004327B9;

// Start of the row walk, just after the arc is folded in and converted to an int:
//   0043280C  89 45 10        mov [ebp+10h], eax   ; (int)(rect.top + arc)   <- the origin
//   0043280F  DB 45 10        fild dword [ebp+10h]
//   00432812  DD 55 C8        fstp qword [ebp-38h] ; v49 = first row y
//   00432815  DB 47 0C        fild dword [edi+0Ch] ; resume: rect.bottom
static constexpr DWORD kTileYPatch  = 0x0043280C;
static constexpr int   kTileYLen    = 9;
static constexpr DWORD kTileYResume = 0x00432815;

static DWORD pTileStepResume = kTileStepResume;
static DWORD pTileInitResume = kTileInitResume;
static DWORD pTileYResume    = kTileYResume;

// esi = rect.left, ecx = rect.right, eax = height -- all three are live at the resume point and
// are preserved. edx is free: the instruction right after the resume overwrites it with cdq.
void __declspec(naked) TileInitCave() {
    __asm {
        mov     edx, esi
        cmp     [g_bTileCentre], 0
        je      mn_tile_x_keep
        add     edx, ecx                ; left + right
        sar     edx, 1                  ; centre
    mn_tile_x_keep:
        add     edx, [g_nTileOriginX]
        mov     [ebp - 0x10], edx       ; v57, the first column
        mov     [ebp - 0x14], eax       ; v56 = height, the displaced instruction
        jmp     [pTileInitResume]
    }
}

// eax = (int)(rect.top + arc) on entry; edi is the rect pointer. The fild/fstp below are the
// client's own two instructions reproduced verbatim -- the FPU still holds the arc value later
// rows depend on, so the stack must be left exactly as it was found.
void __declspec(naked) TileYCave() {
    __asm {
        cmp     [g_bTileCentre], 0
        je      mn_tile_y_keep
        mov     eax, [edi + 4]          ; rect.top
        add     eax, [edi + 0x0C]       ; + rect.bottom
        sar     eax, 1                  ; centre
    mn_tile_y_keep:
        add     eax, [g_nTileOriginY]
        mov     [ebp + 0x10], eax
        fild    dword ptr [ebp + 0x10]
        fstp    qword ptr [ebp - 0x38]
        jmp     [pTileYResume]
    }
}

// eax holds CRand32::Random's result on entry; ebp is the client's frame, so [ebp-10h] is still
// the current column. ecx was clobbered by the original (pop/div) and is left alone here; edx is
// set by div exactly as before; the resume point issues its own cmp, so flags are free.
void __declspec(naked) TileStepCave() {
    __asm {
        xor     edx, edx
        div     [g_nTileStepRange]      ; edx = random % range
        mov     eax, [ebp - 0x10]       ; current column x
        add     eax, edx                ; + jitter
        add     eax, [g_nTileStepBase]  ; + base step
        jmp     [pTileStepResume]
    }
}

static constexpr DWORD kAreaChainPatch  = 0x00431D2A;   // mov eax, 82h   (B8 82 00 00 00)
static constexpr DWORD kAreaChainResume = 0x00431D2F;   // sub ecx, eax   -- the stock chain
static constexpr DWORD kAreaChainGroupA = 0x00431EEF;   // Poison Mist's branch: tiles across LTRB

// Defined above the asm on purpose: MSVC resolves inline-asm symbols as it parses, and a DWORD
// declared afterwards is silently taken for an undefined label instead of a variable.
static DWORD pAreaChainResume = kAreaChainResume;
static DWORD pAreaChainGroupA = kAreaChainGroupA;

// ecx holds the skill id (loaded at 0x00431D27), eax is dead -- the replaced instruction is the
// one that first assigns it. GROUP-A re-reads the id from [ebp+8] rather than a register, so
// jumping straight there needs nothing set up.
// The label below is deliberately NOT named `accept`. MSVC's inline assembler resolves a bare
// identifier against the global symbol table BEFORE its own labels, and <winsock2.h> (via
// windows.h) declares accept() -- so `jnz accept` assembles to `jne __imp__accept@12` and jumps
// into ws2_32 on every match. It links cleanly, because that symbol genuinely exists.
void __declspec(naked) AreaSkillGateCave() {
    __asm {
        pushad
        push    dword ptr [esp + 24]        ; original ECX -- pushad puts it at +24
        call    IsAreaEffectSkill
        add     esp, 4
        mov     [g_bAreaSkillMatch], eax
        popad
        cmp     [g_bAreaSkillMatch], 0
        jnz     mn_area_match
        mov     eax, 82h                    ; the instruction the cave replaced
        jmp     [pAreaChainResume]
    mn_area_match:
        jmp     [pAreaChainGroupA]
    }
}

// Narrow a foreign wide string by hand, under SEH, into a fixed buffer.
//
// Its own function on purpose: __try cannot share a function with C++ objects that need
// unwinding, and this must not be inlined into a caller that has any.
//
// The previous version of the caller killed the client three times running -- an access violation
// inside the CRT's own vsprintf, which is why nothing reached the log: the crash WAS the logging.
// It passed this pointer straight to %ls (asking the CRT to walk foreign memory during a locale
// conversion) and guarded it with IsBadStringPtrW, which is documented as able to disable stack
// expansion in other threads and kill the process outright. Neither is worth doing to print a
// diagnostic line.
static void CopyWideAscii(const wchar_t* pw, char* pOut, size_t cbOut) {
    if (!pOut || cbOut == 0) {
        return;
    }
    pOut[0] = 0;
    if (!pw) {
        return;
    }
    __try {
        size_t n = 0;
        for (; n < cbOut - 1; ++n) {
            const wchar_t wc = pw[n];
            if (!wc) {
                break;
            }
            // These are WZ paths: ASCII by construction. Anything else means a wrong pointer,
            // and '?' says so without pretending to transcode.
            pOut[n] = (wc > 0 && wc < 0x80) ? static_cast<char>(wc) : '?';
        }
        pOut[n] = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        strcpy_s(pOut, cbOut, "(faulted while reading)");
    }
}

// Whether anything draws at all comes down to one field: SKILLENTRY+0x88, the skill's cached
// area-animation UOL, which the WZ loader fills in only when the skill has the node for it.
// sub_43229E (the accessor OnAffectedAreaCreated uses) just copies it and bumps a refcount at
// +8, so a skill without that node leaves it null -- and GetObjectA then faults on it rather than
// quietly drawing nothing. See GetAreaEffectUol; the gate refuses the skill when it is null.
//
// 2121040 has effect/effect0/hit but no tile or mob node, so this is the thing most likely to be
// wrong on first run. Logging Poison Mist's alongside ours names the node to copy and says
// plainly whether ours resolved.
static void LogAreaEffectUol(int nSkillId) {
    void* pInfo = *kppSkillInfo;
    if (!pInfo) {
        return;
    }
    void* pSkill = nullptr;
    try {
        pSkill = CSkillInfo_GetSkill(pInfo, nSkillId);
    } catch (...) {
        return;
    }
    if (!pSkill || IsBadReadPtr(pSkill, 0x8C)) {
        LogInfo("[mistexplosion] uol skill=%d: NO SKILLENTRY", nSkillId);
        return;
    }
    void* pStr = reinterpret_cast<void**>(static_cast<char*>(pSkill))[34];   // +0x88
    if (!pStr) {
        LogInfo("[mistexplosion] uol skill=%d: +0x88 NULL -- no area animation node in WZ", nSkillId);
        return;
    }
    // _bstr_t::Data_t: { BSTR m_wstr; char* m_str; ULONG m_RefCount; }. The refcount at +8 is
    // exactly the field sub_43229E bumps, which is what pins the layout down. So the characters
    // are one pointer deeper -- +0x88 itself is the wrapper, not the text.
    const wchar_t* pw = *reinterpret_cast<const wchar_t* const*>(pStr);
    const char*    pa = *reinterpret_cast<const char* const*>(static_cast<char*>(pStr) + 4);

    char sUol[160];
    CopyWideAscii(pw, sUol, sizeof(sUol));
    LogInfo("[mistexplosion] uol skill=%d: Data_t=%p wstr=%p astr=%p uol=\"%s\"",
            nSkillId, pStr, pw, pa, sUol);

    // Raw bytes too. If the layout guess above is ever wrong the string reads as empty and says
    // nothing, whereas the hex always shows what is actually there.
    char hex[3 * 24 + 1];
    hex[0] = 0;
    if (!IsBadReadPtr(pStr, 24)) {
        for (int i = 0; i < 24; ++i) {
            char b[4];
            _snprintf(b, sizeof(b), "%02X ", static_cast<unsigned char*>(pStr)[i]);
            strcat_s(hex, sizeof(hex), b);
        }
    }
    LogInfo("[mistexplosion] uol skill=%d: Data_t bytes: %s", nSkillId, hex);
}

// Draws one explosion at one cloud's footprint.
//
// The field order below is exactly what OnAffectedAreaCreated decodes, in order: id, type, owner,
// skill, level(1), delay(2), the LTRB, elemAttr. nType must not be 3 -- that is the cash-item
// branch, which resolves a completely different WZ path. Live Poison Mist areas report type 1.
static void SpawnEffectArea(const RECT& rc, int nSkillId, int nSkillLevel, int nDurationMs) {
    void* pPool = *kppAffectedAreaPool;
    if (!pPool || nSkillLevel <= 0) {
        return;
    }
    // Same check the gate makes. Without a UOL the area would still be created and linked into
    // the pool, just with no layers -- a live, invisible area sitting there until it expires.
    if (!GetAreaEffectUol(nSkillId)) {
        return;
    }

    Step("spawn: build");
    unsigned char body[39]{};
    unsigned char* w = body;
    auto put4 = [&w](int v) { memcpy(w, &v, 4); w += 4; };
    auto put2 = [&w](short v) { memcpy(w, &v, 2); w += 2; };

    const int nAreaId = kEffectAreaIdBase | ((++g_nEffectAreaSeq) & 0x0FFFFFFF);
    put4(nAreaId);
    put4(1);                                    // nType
    put4(GetLocalCharId());
    put4(nSkillId);
    *w++ = static_cast<unsigned char>(nSkillLevel);
    put2(0);                                    // delay
    memcpy(w, &rc, sizeof(RECT)); w += sizeof(RECT);
    put4(0);                                    // elemAttr

    // Same raw-buffer trick as RemoveArea, and for the same reason: a real CInPacket owns a
    // ZArray<char> whose destructor would hand this stack address to the game allocator.
    // Padded, with a canary past the end. CInPacket's real layout is confirmed (buffer +8,
    // length +12, offset +20, so 24 bytes), but this buffer is handed to a large client function
    // that normally receives a REAL packet, and a stack smash here would present exactly as the
    // crash being chased -- an access violation in whatever allocates the next big stack frame,
    // nowhere near the actual fault. The slack makes that survivable and the canary makes it
    // visible instead of leaving it to be inferred.
    alignas(8) char pkt[sizeof(CInPacket) + 64]{};
    const uint32_t kCanary = 0xC0FFEE01;
    memcpy(pkt + sizeof(CInPacket), &kCanary, sizeof(kCanary));
    *reinterpret_cast<char**>(pkt + offsetof(CInPacket, m_aRecvBuff))    = reinterpret_cast<char*>(body);
    *reinterpret_cast<uint16_t*>(pkt + offsetof(CInPacket, m_uLength))   = sizeof(body);
    *reinterpret_cast<uint16_t*>(pkt + offsetof(CInPacket, m_uDataLen))  = sizeof(body);
    *reinterpret_cast<uint32_t*>(pkt + offsetof(CInPacket, m_uOffset))   = 0;

    Step("spawn: call");
    try {
        OnAffectedAreaCreated(pPool, pkt);
    } catch (...) {
        Step("spawn: threw");
        return;
    }
    // Read the canary BEFORE logging anything. The failure being chased kills the process inside
    // the CRT's formatting, so a check that runs after a log line never runs at all -- which is
    // exactly what happened the first time this was written.
    uint32_t uCheck = 0;
    memcpy(&uCheck, pkt + sizeof(CInPacket), sizeof(uCheck));
    const bool bSmashed = (uCheck != kCanary);
    Step("spawn: returned");
    if (bSmashed) {
        LogInfo("[mistexplosion] *** STACK CANARY SMASHED *** 0x%08X != 0x%08X"
                " -- OnAffectedAreaCreated wrote past the fake CInPacket", uCheck, kCanary);
        LogFlush();
    }

    // Lifetime, set here rather than in WZ. OnAffectedAreaCreated derives tEnd from the skill's
    // level-data `time` -- seconds, and meant for a buff. An attack skill has no reason to carry
    // one, and inventing a `time` in 212.img would also be read by the server. Overwriting the
    // field on the area we just made keeps the whole change client-side.
    Step("spawn: fixup");
    ForEachArea([&](void* pArea) {
        int* p = reinterpret_cast<int*>(pArea);
        if (IsBadReadPtr(pArea, 0x1C) || p[0] != nAreaId) {
            return;
        }
        p[6] = p[5] + nDurationMs;   // +0x18 tEnd = +0x14 tStart + duration
    });
    Step("spawn: done");
}

// ---- damage ------------------------------------------------------------------
// Damage comes from MagicSkillDamageOnMob (skills.cpp), which is this server's own magic formula:
// setMAD() -> topMAD, then the mastery range roll, the skill's damage percent, and the level /
// MDDamage mitigation every other skill here uses. Deliberately NOT a private formula -- a new
// skill on its own curve is a balance bug that only shows up months later.
//
// STILL MISSING: the server never hears about any of this. The numbers below are computed and
// shown locally; making them real needs the mob list encoded into an ATTACKINFO array and sent
// through the client's own magic-attack request (SendSkillUseRequest 0x0096D399, and the encode
// loop in TryDoingMagicAttack 0x0095571F). Until that exists the mobs take no actual damage.
// SKILLLEVELDATA offsets, read at the player's LEARNED level so the skill scales with itself.
static constexpr int kOfsLevelData_Mad         = 0x34;    // `mad` (290 at level 30)
static constexpr int kOfsLevelData_AttackCount = 0x100;   // `attackCount` (6)
static constexpr int kOfsLevelData_MobCount    = 0x130;   // `mobCount` (6)

// Only used if the WZ read fails; 2121040 carries its own numbers.
static constexpr int kFallbackDamagePct  = 290;
static constexpr int kFallbackAttackCnt  = 1;

int MistExplosion_GetMobCount() {
    const int n = GetSkillLevelDataLong(mistExplosionSkillId, kOfsLevelData_MobCount);
    // Sanity-gated exactly like the summon mobCount override: this caps writes into a fixed-size
    // array, so an out-of-range WZ value must fall back rather than smash the stack.
    if (n >= 1 && n <= kMaxMobsPerMist) {
        return n;
    }
    return kMaxMobsPerMist;
}

void MistExplosion_ApplyDamage(const RECT& rc, void** apMob, int nMobs) {
    (void)rc;
    if (!apMob || nMobs <= 0) {
        return;
    }

    int nDamagePct = GetSkillLevelDataLong(mistExplosionSkillId, kOfsLevelData_Mad);
    if (nDamagePct <= 0) {
        nDamagePct = kFallbackDamagePct;
    }
    int nAttackCount = GetSkillLevelDataLong(mistExplosionSkillId, kOfsLevelData_AttackCount);
    if (nAttackCount < 1 || nAttackCount > 15) {
        nAttackCount = kFallbackAttackCnt;
    }

    for (int i = 0; i < nMobs; ++i) {
        void* pMob = apMob[i];
        if (!pMob) {
            continue;
        }
        // One roll PER LINE, not one roll reused: the mastery range is rolled inside
        // MagicSkillDamageOnMob, so calling it attackCount times is what gives the skill its
        // spread instead of six identical numbers.
        int nTotal = 0;
        for (int nLine = 0; nLine < nAttackCount; ++nLine) {
            nTotal += MagicSkillDamageOnMob(pMob, nDamagePct);
        }
        LogInfo("[mistexplosion] mob=%p total=%d (%d lines x %d%%)",
                pMob, nTotal, nAttackCount, nDamagePct);
    }

    static bool s_bWarned = false;
    if (!s_bWarned) {
        s_bWarned = true;
        LogInfo("[mistexplosion] *** NOT SENT TO SERVER *** damage is computed and logged only;"
                " the attack packet is not built yet, so mobs take no real damage");
    }
}

static void SendMistAttack(const std::vector<void*>& mobs, int nDamagePct, int nAttackCount,
                           const POINT* pOrigin);

// ---- the skill ---------------------------------------------------------------
// Returns how many mists were detonated. Zero is an ordinary outcome (no mists down, or none of
// them ours) and must stay silent rather than erroring.
int MistExplosion_Detonate() {
    const int nLocalId = GetLocalCharId();
    if (!nLocalId) {
        return 0;
    }

    // Snapshot first -- see ForEachArea.
    std::vector<AffectedArea> mine;
    ForEachArea([&](void* pArea) {
        AffectedArea a{};
        if (!ReadArea(pArea, a)) {
            return;
        }
        if (a.dwOwnerId != nLocalId) {
            return;   // somebody else's cloud
        }
        if (!IsMistSourceSkill(a.nSkillId)) {
            return;
        }
        mine.push_back(a);
    });

    // Report the whole survey, not just the hits. "only one mist exploded" needs to distinguish
    // "only one was found" from "several were found and the loop stopped early", and without this
    // the log cannot tell them apart.
    int nSeen = 0, nOwned = 0;
    ForEachArea([&](void* pArea) {
        AffectedArea a{};
        if (!ReadArea(pArea, a)) {
            return;
        }
        ++nSeen;
        if (a.dwOwnerId == nLocalId) {
            ++nOwned;
        }
        LogInfo("[mistexplosion]   area=%d type=%d owner=%d skill=%d rect=(%ld,%ld,%ld,%ld)%s",
                a.nAreaId, a.nType, a.dwOwnerId, a.nSkillId,
                a.rc.left, a.rc.top, a.rc.right, a.rc.bottom,
                (a.dwOwnerId == nLocalId && IsMistSourceSkill(a.nSkillId)) ? "  <- MATCH" : "");
    });
    LogInfo("[mistexplosion] survey: %d area(s) in pool, %d owned by me (%d), %d match",
            nSeen, nOwned, nLocalId, static_cast<int>(mine.size()));
    LogFlush();   // the survey is the last thing written before anything risky happens

    if (mine.empty()) {
        return 0;
    }

    Step("mobpool");
    void* pMobPool = *kppMobPool;
    if (!pMobPool) {
        return 0;
    }

    // Resolved once: the effect is spawned per cloud, and this walks the character's skill
    // record every time it is asked.
    // Level of the ART skill, not the cast skill -- they can differ, and a borrowed skill is
    // usually not one this character has learned. It only indexes SKILLLEVELDATA for a duration
    // that SpawnEffectArea overwrites anyway, so 1 is a safe floor; what matters is that it is a
    // valid index rather than 0.
    Step("effect-level");
    int nEffectLevel = GetLearnedSkillLevel(mistExplosionEffectSkillId);
    if (nEffectLevel <= 0) {
        nEffectLevel = 1;
    }

    // Reported from here, not from AttachMistExplosionMod: hooks are installed during startup,
    // long before CSkillInfo exists, so asking at attach time printed nothing at all.
    Step("uol-report");
    static bool s_bUolReported = false;
    if (mistExplosionDrawEffect && !s_bUolReported) {
        s_bUolReported = true;
        LogAreaEffectUol(2111003);              // known-good, names the node to copy
        LogAreaEffectUol(mistExplosionEffectSkillId);
    }

    // Hoisted above the loop: with one attack per cloud these are needed on every iteration, and
    // they each walk the character's skill record.
    Step("skill-data");
    int nDamagePct = GetSkillLevelDataLong(mistExplosionSkillId, kOfsLevelData_Mad);
    if (nDamagePct <= 0) {
        nDamagePct = kFallbackDamagePct;
    }
    int nAttackCount = GetSkillLevelDataLong(mistExplosionSkillId, kOfsLevelData_AttackCount);
    if (nAttackCount < 1 || nAttackCount > 15) {
        nAttackCount = kFallbackAttackCnt;
    }

    std::vector<void*> allMobs;
    int nDetonated = 0;
    int nAttacksSent = 0;
    for (const AffectedArea& a : mine) {
        Step("mobcount");
        const long nMaxMobs = MistExplosion_GetMobCount();

        Step("find-mobs");
        void* apMob[kMaxMobsPerMist]{};
        long nFound = 0;
        try {
            nFound = FindHitMobInRect(pMobPool, &a.rc, apMob, nMaxMobs,
                                      nullptr, 0, 0, 0, 0);
        } catch (...) {
            nFound = 0;
        }
        Step("find-mobs done");
        if (nFound < 0) {
            nFound = 0;
        }
        if (nFound > kMaxMobsPerMist) {
            nFound = kMaxMobsPerMist;   // never trust a count against a fixed array
        }

        LogInfo("[mistexplosion] mist area=%d skill=%d rect=(%ld,%ld,%ld,%ld) mobs=%ld",
                a.nAreaId, a.nSkillId, a.rc.left, a.rc.top, a.rc.right, a.rc.bottom, nFound);

        std::vector<void*> mistMobs;
        for (long k = 0; k < nFound; ++k) {
            if (apMob[k]) {
                mistMobs.push_back(apMob[k]);
            }
        }
        // Draw BEFORE the removal. RemoveArea fades the cloud's own layers out, and the effect
        // has to already be on screen when that starts or the two read as one flicker instead of
        // the cloud going up.
        if (mistExplosionDrawEffect) {
            Step("spawn-effect");
            SpawnEffectArea(a.rc, mistExplosionEffectSkillId, nEffectLevel, mistExplosionEffectMs);
        }
        Step("remove-area");
        RemoveArea(a.nAreaId);
        Step("remove-area done");
        ++nDetonated;

        if (mistExplosionPerMistAttack) {
            // Origin is the cloud's own footprint: horizontally centred, on its bottom edge,
            // which is where the mist sits on the ground rather than the middle of its volume.
            POINT ptOrigin = { (a.rc.left + a.rc.right) / 2, a.rc.bottom };
            if (!mistMobs.empty()) {
                Step("send-attack");
                SendMistAttack(mistMobs, nDamagePct, nAttackCount, &ptOrigin);
                Step("send-attack done");
                ++nAttacksSent;
            }
            LogInfo("[mistexplosion] cloud %d/%d at (%ld,%ld): %d mob(s)%s",
                    nDetonated, static_cast<int>(mine.size()), ptOrigin.x, ptOrigin.y,
                    static_cast<int>(mistMobs.size()), mistMobs.empty() ? " - no attack" : "");
            // Flushed per cloud, not per line. These exist to survive a crash, and buffered
            // output dies with the process -- a cast that kills the client would otherwise leave
            // exactly the log that stops mid-record. One flush per cloud is a handful per cast.
            LogFlush();
        } else {
            allMobs.insert(allMobs.end(), mistMobs.begin(), mistMobs.end());
        }
    }

    if (mistExplosionPerMistAttack) {
        LogInfo("[mistexplosion] %d mist(s), %d attack(s) sent, %d%% x %d line(s) each"
                " -- NOTE: server charges MP per attack",
                nDetonated, nAttacksSent, nDamagePct, nAttackCount);
        LogFlush();   // last line of the cast; unflushed it is lost to whatever happens next
        return nDetonated;
    }

    // De-dup, preserving order.
    std::vector<void*> uniq;
    for (void* m : allMobs) {
        bool bDup = false;
        for (void* u : uniq) {
            if (u == m) { bDup = true; break; }
        }
        if (!bDup) {
            uniq.push_back(m);
        }
    }

    LogInfo("[mistexplosion] %d mist(s), %d unique mob(s), %d%% x %d line(s)",
            nDetonated, static_cast<int>(uniq.size()), nDamagePct, nAttackCount);
    SendMistAttack(uniq, nDamagePct, nAttackCount, nullptr);   // pooled: origin is the caster
    return nDetonated;
}

// ---- cast-time target substitution -------------------------------------------
// This is what makes the skill real. CUserLocal::TryDoingMagicAttack builds its attack rect from
// the skill's lt/rb (sub_4144D4 @0x00956398), offsets it onto the player (@0x0095656D), and hands
// it to CMobPool::FindHitMobInRect (@0x009565BF) to pick targets. Everything after that -- damage
// per mob, the ATTACKINFO array, the packet the SERVER accepts -- is the client's own code.
//
// So instead of building an attack packet by hand, we substitute the TARGET LIST at that one call:
// during a Mist Explosion cast the rect the client computed is discarded and the mobs come from
// the mist footprints instead. The client then does the rest exactly as it would for any magic
// skill, which is why the damage counts server-side.
//
// It also answers "explode more than one": the substitution unions the results of every owned
// mist, so one cast covers them all, while each mist still contributes only the mobs standing in
// ITS OWN rect -- no mobs in the gaps between clouds.
// Set for the duration of a Mist Explosion cast that HAS clouds to consume. While it is set the
// client's own target search comes back empty, so the cast hits nothing on its own and the only
// damage is the per-cloud attacks.
//
// Without it a mob standing both inside a cloud AND inside the skill's own lt/rb around the
// player is hit twice: once by the cast, once by that cloud's detonation.
//
// Only when clouds exist. A cast with nothing to detonate keeps its normal behaviour rather than
// silently becoming a skill that does nothing.
static int s_bSuppressCastTargets = 0;

// The client's own target search inside CUserLocal::TryDoingMagicAttack:
//   009565A1  8B 0D A4BFBE00   mov  ecx, dword_BEBFA4   ; the mob pool
//   009565A9  57 x5 ...        push the eight arguments
//   009565BF  E8 B21ED2FF      call CMobPool::FindHitMobInRect
//   009565C4  8B F0            mov  esi, eax            ; resume: the hit count
// Caving the CALL, not the function: FindHitMobInRect is on the target path of every attack in
// the game, and a global hook here was already tried once and had to be removed.
static constexpr DWORD kCastTargetsPatch  = 0x009565BF;
static constexpr DWORD kCastTargetsResume = 0x009565C4;

static DWORD pFindHitMobInRectRaw = 0x00678476;
static DWORD pCastTargetsResume   = kCastTargetsResume;

// ecx already holds the mob pool and is untouched here. The call is a real call so the original
// still pops its own eight arguments; on the suppressed path we pop those 32 bytes ourselves and
// hand back a count of zero. The resume point only does `mov esi, eax`, so flags are free.
void __declspec(naked) CastTargetsCave() {
    __asm {
        cmp     [s_bSuppressCastTargets], 0
        jnz     mn_no_cast_targets
        call    [pFindHitMobInRectRaw]
        jmp     [pCastTargetsResume]
    mn_no_cast_targets:
        add     esp, 32                 ; the eight args the callee would have cleaned
        xor     eax, eax                ; zero mobs found
        jmp     [pCastTargetsResume]
    }
}

static bool s_bCastInFlight = false;
static std::vector<AffectedArea> s_castMists;

// Called immediately BEFORE the client's cast. Decides whether this cast has clouds to consume,
// and if so suppresses the cast's own targeting for its duration.
int MistExplosion_BeginCast() {
    s_castMists.clear();
    s_bCastInFlight = false;
    s_bSuppressCastTargets = 0;

    if (!mistExplosionEnabled) {
        return 0;
    }
    const int nLocalId = GetLocalCharId();
    if (!nLocalId) {
        return 0;
    }
    ForEachArea([&](void* pArea) {
        AffectedArea a{};
        if (!ReadArea(pArea, a)) {
            return;
        }
        if (a.dwOwnerId == nLocalId && IsMistSourceSkill(a.nSkillId)) {
            s_castMists.push_back(a);
        }
    });
    if (s_castMists.empty()) {
        return 0;
    }
    s_bCastInFlight = true;
    s_bSuppressCastTargets = 1;
    LogInfo("[mistexplosion] cast begins: %d mist(s) -- own targeting suppressed",
            static_cast<int>(s_castMists.size()));
    return static_cast<int>(s_castMists.size());
}

// Called immediately AFTER the client's cast, whatever its outcome.
//
// Clearing the suppression here and not in the detonation is deliberate: a cast the consume checks
// reject never reaches MistExplosion_Detonate, and a flag left set would blank the target list of
// every attack that followed.
//
// It does NOT consume the mists. Removal belongs to the detonation, which owns the whole sequence
// -- collect the mobs in a cloud, draw it, remove it, attack for it -- and doing it here as well
// would tear the clouds down before the detonation had read them.
void MistExplosion_EndCast() {
    s_bSuppressCastTargets = 0;
    s_castMists.clear();
    s_bCastInFlight = false;
}

// ---- the attack packet -------------------------------------------------------
// CP_UserMagicAttack (0x2E), mirrored field-for-field from the tail of
// CUserLocal::TryDoingMagicAttack. Every offset below is the disassembly at the address quoted;
// one wrong field here is a disconnect, so nothing is inferred.
//
//   0x956D9A  COutPacket(0x2E)
//   0x956DC8  Encode1  get_field()[+0x134]            field key (portal counter)
//   0x956DDF  Encode1  (nMobs << 4) | nAttackCount
//   0x956DEE  Encode4  skillId
//   0x956E55  Encode4  <crc from sub_75C017>          0 when the skill entry is null
//   0x956E61  Encode4  <value from sub_7608E6>        0 likewise
//   0x956EA4  Encode1  0
//   0x956EC1  Encode2  display/action word
//   0x956ED2  Encode1  attack speed
//   0x956EEA  Encode1  (mastery << 4) | ?
//   0x956EF8  Encode4  <projectile/starter id, 0 for magic>
//   then per mob, from 0x956F11:
//   0x956F2D  Encode4  fuse(mob+0x17C, mob+0x184)     mob id
//   0x956F3E  Encode1  hit action
//   0x956F5C  Encode1  (sub_663FE2(mob) << 7) | (x & 0x7F)
//   0x956F6D  Encode1  ?
//   0x956FCF  Encode1  (templateChanged << 7) | (mob[0x4C4] & 0x7F)
//   0x956FE8  Encode2  mob x
//   0x957002  Encode2  mob y
//   then one Encode4 per damage line
static constexpr int kOpcode_UserMagicAttack = 0x2E;

using t_SendPacket = void(__thiscall*)(void* pSocket, COutPacket* p);
static auto CClientSocket_SendPacket = reinterpret_cast<t_SendPacket>(0x0049637B);
static void** const kppClientSocket = reinterpret_cast<void**>(0x00BE7914);

using t_GetField = void*(__cdecl*)();
static auto GetField = reinterpret_cast<t_GetField>(0x00437A0C);   // ?get_field@@YAPAVCField@@XZ

using t_SecureFuseK = unsigned long(__cdecl*)(const unsigned long*, unsigned int);
// The mob id uses the K (unsigned long) instantiation, not J: TryDoingMagicAttack calls
// ??$_ZtlSecureFuse@K@@YAKQBKI@Z at 0x00956F1F. 0x00416563 is the J twin and a different
// function -- close enough to look right and wrong enough to matter.
static auto SecureFuseK = reinterpret_cast<t_SecureFuseK>(0x004E8152);

using t_MobIsLeft = unsigned char(__thiscall*)(void*);
static auto MobIsLeft = reinterpret_cast<t_MobIsLeft>(0x00663FE2);

static unsigned long GetMobId(void* pMob) {
    if (!pMob || IsBadReadPtr(pMob, 0x188)) {
        return 0;
    }
    char* m = static_cast<char*>(pMob);
    return SecureFuseK(reinterpret_cast<const unsigned long*>(m + 0x17C),
                       *reinterpret_cast<unsigned int*>(m + 0x184));
}

// Dry run by default. This packet has never been sent by this DLL, and a malformed attack packet
// is a disconnect, not an error message. With this on the bytes are logged and nothing leaves the
// client, so the format can be checked against a real magic attack before risking a session.
int mistExplosionDryRun = 0;

// Rebuilt against a CAPTURED working packet rather than inferred from disassembly. The first
// attempt read the encode loop, stopped at 0x0095700E on `call [eax+14h]`, and assumed damages
// followed -- that call is the SECOND position getter, so the packet was truncated mid-structure.
// The server accepted the bytes and silently did nothing with them.
//
// Reference: Magic Claw (2001005), 1 mob x 2 hits, 59 bytes, captured from this client:
//   2E 00 | 01 | 12 | 6D881E00 | A55762FC | BD5F83A8 | 00 | 1D00 | 06 | 06 | 2B94DC2C
//   7CCA9A3B | 06 | 81 | 01 | 01 | 8802 F8FF | 8802 F8FF | 5001 | dmg dmg
//   4CDA4A89 | A901 | 2A00
//
// Position is encoded TWICE, then a 2-byte value, and the packet ends with an 8-byte trailer --
// all three of which the previous version omitted entirely.
static constexpr uint16_t kAttackDisplay   = 0x002A;   // real Poison Mist uses 2A; Magic Claw 1D
static constexpr uint8_t  kAttackByte19    = 0x06;     // capture off 19
static constexpr uint8_t  kAttackByte20    = 0x06;     // capture off 20
static constexpr uint8_t  kMobHitAction    = 0x06;     // capture off 29
static constexpr uint16_t kMobTail         = 0x02EE;   // ATTACKINFO+0x10, an action delay:
                                                       // Poison Mist 750, Magic Claw 336

using t_GetUpdateTime = long(__cdecl*)();
static auto GetUpdateTime = reinterpret_cast<t_GetUpdateTime>(0x00987257);   // ?get_update_time@@YAJXZ

// Encoded once per mob AFTER its damage lines (0x0095706B). Not a packet checksum -- it is a
// cached per-mob value at mob[0x4D8], refreshed when the dirty flag at mob[0x4D4] is set, which is
// why the same mob produced identical bytes in separate captures (63CA9A3B -> 57A8BA51 twice).
// CMob::OnHit -- what actually applies a hit on the client: HP bar, damage numbers, hit reaction.
// Sending the packet alone was never going to show anything; a real attack calls this itself.
// Signature is the one already reverse-engineered in skills.cpp (CMob_OnHit_Hook, 0x00668B83):
//   (this, dwCharacterId, nSkillID, nHitAction, bLeft, nDamage, bCriticalAttack, nAttackIdx,
//    bChase, nMoveType, nBulletCashItemID, nMoveEndingPosX, nMoveEndingPosY, a14)
// Stack args, in order: dwCharacterId, nSkillID, nHitAction, bLeft, nDamage, bCriticalAttack,
// nDamageIndex, bChase, nMoveType, nBulletCashItemID, nMoveEndingPosX, nMoveEndingPosY, a14.
//
// nDamageIndex is what makes a multi-line hit LOOK like one. CMob::OnHit hands it to
// CMob::ShowDamage (0x006691D3), which positions the number at
//     y = mobY - 15 - index * (bHalfHeight ? 15 : 30)
// so it is the number's row: 0 sits on the mob, 1 sits 30px above it, and so on. Passing 0 for
// every line -- which this typedef's old `void*` invited, since nullptr was the obvious thing to
// put there -- draws all of them at the same height, and nine numbers on one pixel row read as a
// single garbled hit rather than a nine-line attack.
//
// The count is 13 stack args, from `retn 34h` at 0x00669187. IDA's mangled name
// (?OnHit@CMob@@IAEXKJJHJHJHJJJJHH@Z) claims 14 and is stale, exactly like ShowSkillEffect's.
using t_CMobOnHit = void(__thiscall*)(void*, int, int, int, int, int, int, int,
                                      int, int, int, int, int, int);
static auto CMob_OnHit = reinterpret_cast<t_CMobOnHit>(0x00668B83);

using t_MobHash = uint32_t(__thiscall*)(void*);
static auto MobPacketHash = reinterpret_cast<t_MobHash>(0x006711AC);

// Reads the object's position through IGObj vtable slot 4 (+0x10). CMob and CUserLocal share the
// same +4 IGObj base, so one helper covers both.
// nSlot 4 (+0x10) is the current position; slot 5 (+0x14) is the second one the packet carries.
// They are usually equal but not always -- one capture had 0x182F vs 0x182D -- so the client reads
// two different getters rather than encoding the same value twice, and so do we now.
static bool ReadObjPos(void* pObj, short& x, short& y, int nSlot = 4) {
    x = 0; y = 0;
    if (!pObj || IsBadReadPtr(pObj, 8)) {
        return false;
    }
    try {
        void** pBase = reinterpret_cast<void**>(static_cast<char*>(pObj) + 4);
        void** vt = static_cast<void**>(*pBase);
        if (!vt || IsBadReadPtr(vt, 0x18)) {
            return false;
        }
        using t_GetPos = long*(__thiscall*)(void*);
        long* pt = reinterpret_cast<t_GetPos>(vt[nSlot])(pBase);
        if (!pt || IsBadReadPtr(pt, 8)) {
            return false;
        }
        x = static_cast<short>(pt[0]);
        y = static_cast<short>(pt[1]);
        return true;
    } catch (...) {
        return false;
    }
}

// Apply the hit on the client as well as telling the server. On by default: a skill that shows
// nothing looks broken even when the packet is correct.
int mistExplosionLocalDamage = 1;

// ---- staggered damage lines --------------------------------------------------
// Milliseconds between one damage number and the next.
//
// The client stages its own multi-line hits rather than drawing them at once: CMob::OnHit is
// reached only from CMob::Update (0x006681CF, the single xref), which walks a ZList at mob+0x124
// and fires each entry when `update_time - entry.tDue >= 0`. Each queued hit therefore carries
// its own due time, and that is what makes a multi-hit skill cascade instead of flashing all its
// numbers on one frame.
//
// Feeding that list directly means building entries whose every field is _ZtlSecureFuse-encoded
// with a per-field key -- a lot of fragile machinery to reproduce for a cosmetic delay. Holding
// the lines here and releasing them on the client tick gets the same result with none of it.
int mistExplosionHitDelayMs = 90;

using t_GetMob = void*(__thiscall*)(void*, unsigned long);
static auto CMobPool_GetMob = reinterpret_cast<t_GetMob>(0x00441AE8);   // retn 4, one arg

// A mob can die, be looted and be freed between one frame and the next, so nothing here holds a
// CMob*. The id is re-resolved through the pool every tick and a hit whose mob has gone is simply
// dropped -- which is also the correct outcome: a dead mob should not sprout three more numbers.
struct PendingHit {
    unsigned long uMobId;
    int  nDamage;
    int  nIndex;
    int  bLeft;
    int  nPosX;
    int  nPosY;
    long tDue;
};
static std::vector<PendingHit> g_pendingHits;

// Bounded so a pathological cast cannot grow this without limit: 15 mobs x 15 lines is the most
// one attack can legitimately produce, and a few casts may overlap.
static constexpr size_t kMaxPendingHits = 512;

// Drains the lines that have come due. Called from the client's frame tick.
void MistExplosion_OnClientTick() {
    if (g_pendingHits.empty()) {
        return;
    }
    void* pPool = *kppMobPool;
    if (!pPool) {
        g_pendingHits.clear();   // no pool means no map; these can never resolve
        return;
    }

    long tNow = 0;
    try {
        tNow = GetUpdateTime();
    } catch (...) {
        return;
    }
    const int nLocalCharId = GetLocalCharId();

    size_t nWrite = 0;
    for (size_t i = 0; i < g_pendingHits.size(); ++i) {
        const PendingHit& h = g_pendingHits[i];

        // Signed difference, so this stays correct across the tick counter wrapping -- the same
        // comparison CMob::Update makes against its own queue.
        if (tNow - h.tDue < 0) {
            g_pendingHits[nWrite++] = h;   // not yet; keep it
            continue;
        }

        void* pMob = nullptr;
        try {
            pMob = CMobPool_GetMob(pPool, h.uMobId);
        } catch (...) {
            pMob = nullptr;
        }
        if (!pMob) {
            continue;   // gone since it was queued; drop it
        }
        try {
            CMob_OnHit(pMob, nLocalCharId, mistExplosionSkillId, kMobHitAction,
                       h.bLeft, h.nDamage, 0, h.nIndex, 0, 0, 0, h.nPosX, h.nPosY, 0);
        } catch (...) {
            // one bad mob must not strand the rest of the queue
        }
    }
    g_pendingHits.resize(nWrite);
}


// Report the explosion as ONE combined hit rather than attackCount separate lines. The lines are
// still rolled individually -- that is where the mastery spread comes from -- and then summed, so
// the SERVER-SIDE TOTAL is identical either way; only the presentation changes.
//
// The packet's damage count changes with it (numDamage = 1 when combined), because the count byte
// and the number of Encode4s that follow have to agree or the server reads the next mob's id as a
// damage value.
//
// Off: the explosion hits attackCount times, and every line is its own magic-damage roll. Each
// cloud is a separate attack with its own rolls too, so two clouds over the same mob produce two
// independent spreads rather than one doubled number.
//
// Worth knowing, since it reverses an earlier decision: separate lines are also the SAFER of the
// two against the server's damage check. AbstractDealDamageHandler reads one int per line and
// tests each against calcDmgMax -- a per-LINE maximum. Nine merged lines are compared against
// that same single-line ceiling, which is what put the combined number near DAMAGE_HACK's
// threshold; nine ordinary lines sit comfortably under it.
int mistExplosionCombineDamage = 0;


static void SendMistAttack(const std::vector<void*>& mobs, int nDamagePct, int nAttackCount,
                           const POINT* pOrigin) {
    if (mobs.empty() || nAttackCount < 1) {
        return;
    }
    const int nLocalCharId = GetLocalCharId();
    // Both halves of the count byte are 4 bits.
    int nMobs = static_cast<int>(mobs.size());
    if (nMobs > 15) nMobs = 15;
    if (nAttackCount > 15) nAttackCount = 15;

    void* pField = nullptr;
    try { pField = GetField(); } catch (...) {}
    const unsigned char bFieldKey =
            (pField && !IsBadReadPtr(pField, 0x135))
            ? *(static_cast<unsigned char*>(pField) + 0x134) : 0;

    // Capture off 8 comes from sub_75C017, which is simply pSkillEntry[13] (0x0075C017).
    uint32_t uSkillField = 0;
    try {
        void* pSkillInfo = *kppSkillInfo;
        if (pSkillInfo) {
            void* pEntry = CSkillInfo_GetSkill(pSkillInfo, mistExplosionSkillId);
            if (pEntry && !IsBadReadPtr(pEntry, 0x38)) {
                uSkillField = *reinterpret_cast<uint32_t*>(static_cast<char*>(pEntry) + 0x34);
            }
        }
    } catch (...) {}

    COutPacket p(kOpcode_UserMagicAttack);
    p.Encode1(bFieldKey);
    const int nLinesInPacket = mistExplosionCombineDamage ? 1 : nAttackCount;
    p.Encode1(static_cast<uint8_t>((nMobs << 4) | nLinesInPacket));
    p.Encode4(static_cast<uint32_t>(mistExplosionSkillId));
    p.Encode4(uSkillField);
    p.Encode4(0);                     // capture off 12 -- varies per cast, source not yet identified
    p.Encode1(0);
    p.Encode2(kAttackDisplay);
    p.Encode1(kAttackByte19);
    p.Encode1(kAttackByte20);
    p.Encode4(static_cast<uint32_t>(GetUpdateTime()));   // off 21 is a tick, not a projectile id

    for (int i = 0; i < nMobs; ++i) {
        void* pMob = mobs[i];
        char* m = static_cast<char*>(pMob);
        p.Encode4(GetMobId(pMob));
        p.Encode1(kMobHitAction);
        unsigned char bLeft = 0;
        try { bLeft = MobIsLeft(pMob); } catch (...) {}
        p.Encode1(static_cast<uint8_t>((bLeft << 7) | 0x01));
        p.Encode1(0x01);
        p.Encode1(0x01);
        short x = 0, y = 0;
        ReadObjPos(pMob, x, y);
        p.Encode2(static_cast<uint16_t>(x));            // GetPos, vtable slot 4
        p.Encode2(static_cast<uint16_t>(y));
        short x2 = x, y2 = y;
        ReadObjPos(pMob, x2, y2, 5);                   // the second getter, vtable slot 5
        p.Encode2(static_cast<uint16_t>(x2));
        p.Encode2(static_cast<uint16_t>(y2));
        p.Encode2(kMobTail);
        (void)m;

        // Roll each line once, then use the SAME values for both the packet and the local hit --
        // rolling twice would mean the number on screen and the number the server is told differ.
        int aDamage[15]{};
        long long llTotal = 0;
        for (int nLine = 0; nLine < nAttackCount; ++nLine) {
            aDamage[nLine] = MagicSkillDamageOnMob(pMob, nDamagePct);
            llTotal += aDamage[nLine];
        }
        if (llTotal > 0x7FFFFFFFll) {
            llTotal = 0x7FFFFFFFll;   // the field is a signed int; never let the sum wrap negative
        }
        const int nCombined = static_cast<int>(llTotal);

        if (mistExplosionCombineDamage) {
            p.Encode4(static_cast<uint32_t>(nCombined));
        } else {
            for (int nLine = 0; nLine < nAttackCount; ++nLine) {
                p.Encode4(static_cast<uint32_t>(aDamage[nLine]));
            }
        }

        // Apply locally. Without this the skill is silent no matter what the server does: the
        // client only draws damage for hits it applies itself. Kept in step with the packet so one
        // number on screen means one number sent.
        if (mistExplosionLocalDamage) {
            const int nLocalLines = mistExplosionCombineDamage ? 1 : nAttackCount;
            long tBase = 0;
            try {
                tBase = GetUpdateTime();
            } catch (...) {
            }
            for (int nLine = 0; nLine < nLocalLines; ++nLine) {
                const int nShow = mistExplosionCombineDamage ? nCombined : aDamage[nLine];

                // The first line lands now. Deferring it too would put a frame of nothing between
                // the cast and any feedback, and the point of the delay is the cascade after the
                // first number, not before it.
                if (nLine == 0 || mistExplosionHitDelayMs <= 0) {
                    try {
                        CMob_OnHit(pMob, nLocalCharId, mistExplosionSkillId, kMobHitAction,
                                   bLeft ? 1 : 0, nShow, 0, nLine, 0, 0, 0, x, y, 0);
                    } catch (...) {
                        break;   // one bad mob must not take the rest of the cast with it
                    }
                    continue;
                }
                if (g_pendingHits.size() >= kMaxPendingHits) {
                    continue;
                }
                PendingHit h{};
                h.uMobId  = GetMobId(pMob);
                h.nDamage = nShow;
                h.nIndex  = nLine;
                h.bLeft   = bLeft ? 1 : 0;
                h.nPosX   = x;
                h.nPosY   = y;
                h.tDue    = tBase + nLine * mistExplosionHitDelayMs;
                g_pendingHits.push_back(h);
            }
        }

        LogInfo("[mistexplosion] mob=%p %d line(s) -> %d total%s",
                pMob, nAttackCount, nCombined,
                mistExplosionCombineDamage ? " (combined into 1)" : " (sent as separate lines)");
        uint32_t uHash = 0;
        try { uHash = MobPacketHash(pMob); } catch (...) {}
        p.Encode4(uHash);                              // per mob, not per packet
    }

    // Packet trailer is a position and nothing else. Proved by a captured Poison Mist with ZERO
    // mobs: 29 bytes total, of which only "CD 17 9A 0A" followed the header.
    //
    // When the attack belongs to one cloud this carries that cloud's origin rather than the
    // caster's feet, so the packet says where the blast came from. That is a client-side nicety:
    // AttackInfo.position exists on the server but is never assigned or read, and the distance
    // check uses player.getPosition() against each monster -- not anything in this packet.
    short px = 0, py = 0;
    if (pOrigin) {
        px = static_cast<short>(pOrigin->x);
        py = static_cast<short>(pOrigin->y);
    } else {
        ReadObjPos(*kppUserLocal, px, py);
    }
    p.Encode2(static_cast<uint16_t>(px));
    p.Encode2(static_cast<uint16_t>(py));

    if (mistExplosionDryRun) {
        const char* base = reinterpret_cast<const char*>(&p);
        const uint8_t* buf = *reinterpret_cast<const uint8_t* const*>(base + 4);
        const uint32_t uLen = *reinterpret_cast<const uint32_t*>(base + 8);
        LogInfo("[mistexplosion] DRY RUN: built 0x2E, %d mob(s) x %d line(s), %u bytes, NOT sent",
                nMobs, nAttackCount, uLen);
        if (buf && uLen && uLen < 4096 && !IsBadReadPtr(buf, uLen)) {
            char sLine[3 * 32 + 8];
            for (uint32_t off = 0; off < uLen; off += 32) {
                int nPos = 0;
                for (uint32_t i = off; i < uLen && i < off + 32; ++i) {
                    nPos += _snprintf_s(sLine + nPos, sizeof(sLine) - nPos, _TRUNCATE, "%02X ", buf[i]);
                }
                LogInfo("[mistexplosion]   %04X: %s", off, sLine);
            }
        }
        return;
    }
    void* pSocket = *kppClientSocket;
    if (!pSocket) {
        return;
    }
    try {
        CClientSocket_SendPacket(pSocket, &p);
        LogInfo("[mistexplosion] sent 0x2E: %d mob(s) x %d line(s)", nMobs, nAttackCount);
    } catch (...) {
        LogInfo("[mistexplosion] SendPacket threw");
    }
}

// ---- outgoing attack capture -------------------------------------------------
// The server accepted our 0x2E without complaint and then did nothing with it, which means one of
// the header fields we encode as 0 is wrong -- and guessing which would be another blind round.
// So capture what the client sends for a REAL attack and diff the two.
//
// Hooks CClientSocket::SendPacket and dumps any outgoing attack opcode:
//     0x2C CP_UserMeleeAttack   0x2D CP_UserShootAttack   0x2E CP_UserMagicAttack
// Cast an ordinary attack skill with this on and the log gets a reference packet built entirely by
// the client's own encoder, with every field populated the way the server expects.
int mistExplosionCaptureAttacks = 1;

static void DumpOutPacket(const char* pszTag, COutPacket* p) {
    if (!p) {
        return;
    }
    // COutPacket keeps its members protected; read positionally (wvs/packet.h):
    // +0 m_bLoopback, +4 m_aSendBuff.a, +8 m_uOffset.
    const char* base = reinterpret_cast<const char*>(p);
    const uint8_t* buf = *reinterpret_cast<const uint8_t* const*>(base + 4);
    const uint32_t uLen = *reinterpret_cast<const uint32_t*>(base + 8);
    if (!buf || !uLen || uLen > 4096 || IsBadReadPtr(buf, uLen)) {
        return;
    }
    LogInfo("[mistexplosion] %s outgoing, %u bytes", pszTag, uLen);
    char sLine[3 * 32 + 8];
    for (uint32_t off = 0; off < uLen; off += 32) {
        int nPos = 0;
        for (uint32_t i = off; i < uLen && i < off + 32; ++i) {
            nPos += _snprintf_s(sLine + nPos, sizeof(sLine) - nPos, _TRUNCATE, "%02X ", buf[i]);
        }
        LogInfo("[mistexplosion]   %04X: %s", off, sLine);
    }
}

auto CClientSocket_SendPacket_orig =
        reinterpret_cast<t_SendPacket>(0x0049637B);   // ?SendPacket@CClientSocket@@QAEXABVCOutPacket@@@Z

void __fastcall CClientSocket_SendPacket_hook(void* pSocket, void* edx, COutPacket* p) {
    if (mistExplosionCaptureAttacks && p) {
        const char* base = reinterpret_cast<const char*>(p);
        const uint8_t* buf = *reinterpret_cast<const uint8_t* const*>(base + 4);
        const uint32_t uLen = *reinterpret_cast<const uint32_t*>(base + 8);
        if (buf && uLen >= 2 && !IsBadReadPtr(buf, 2)) {
            const uint16_t op = static_cast<uint16_t>(buf[0] | (buf[1] << 8));
            if (op == 0x2C || op == 0x2D || op == 0x2E) {
                char sTag[32];
                _snprintf_s(sTag, sizeof(sTag), _TRUNCATE, "ATTACK 0x%02X", op);
                DumpOutPacket(sTag, p);
            }
        }
    }
    CClientSocket_SendPacket_orig(pSocket, p);
}

void AttachMistExplosionMod() {
    if (!mistExplosionEnabled) {
        LogInfo("[mistexplosion] DISABLED (mistExplosionEnabled=0) - no hooks installed");
        return;   // the FindHitMobInRect hook is global; not installing it is the safe default
    }
    // Independent of the area gate: this decides what the cast HITS, not what it draws. Inert
    // until MistExplosion_BeginCast raises the flag, so it costs one compare per attack.
    CodeCave(reinterpret_cast<void*>(CastTargetsCave), kCastTargetsPatch, 0);
    LogInfo("[mistexplosion] cast-target suppression armed @0x%08X", kCastTargetsPatch);

    if (mistExplosionCaptureAttacks) {
        ATTACH_HOOK(CClientSocket_SendPacket_orig, CClientSocket_SendPacket_hook);
        LogInfo("[mistexplosion] outgoing attack capture ON (0x2C/0x2D/0x2E)");
    }
    if (mistExplosionAreaGate) {
        CodeCave(reinterpret_cast<void*>(AreaSkillGateCave), kAreaChainPatch, 0);
        LogInfo("[mistexplosion] area gate ON: %d extra skill(s) accepted for area rendering",
                static_cast<int>(g_areaEffectSkills.size()));

        // Tiling last: it is only reachable through an area the gate accepted.
        CodeCave(reinterpret_cast<void*>(TileStepCave), kTileStepPatch, kTileStepLen);
        CodeCave(reinterpret_cast<void*>(TileInitCave), kTileInitPatch, kTileInitLen);
        CodeCave(reinterpret_cast<void*>(TileYCave),    kTileYPatch,    kTileYLen);
        Patch4(kTileYFactorOperand,   reinterpret_cast<unsigned int>(&g_dTileYFactor));
        Patch4(kTileArcFactorOperand, reinterpret_cast<unsigned int>(&g_dTileArcFactor));
        LogInfo("[mistexplosion] tiling: step=%d+0..%d yFactor=%.2f arc=%.2f origin=%s%+d%+d"
                " (stock %d+0..%d/%.2f/%.2f/top-left kept for every other area skill)",
                mistTileStepBase, mistTileStepRange - 1, mistTileYFactor, mistTileArcFactor,
                mistTileCentre ? "centre" : "top-left", mistTileOriginX, mistTileOriginY,
                kStockTileStepBase, kStockTileStepRange - 1, kStockTileYFactor, kStockTileArcFactor);
    }
    if (mistExplosionDrawEffect) {
        LogInfo("[mistexplosion] WARNING: client-side effect synthesis is ON (art=skill %d, %dms)"
                " -- this path crashes; the server draws the blast now",
                mistExplosionEffectSkillId, mistExplosionEffectMs);
    }
    LogInfo("[mistexplosion] armed: skill %d, sources={2111003}", mistExplosionSkillId);
}
