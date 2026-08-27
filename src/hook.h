#pragma once
#include "debug.h"
#include "debugflags.h"
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

// Breadcrumb marker the ported Monster Book modules put at the top of each hook body. It exists
// so a crash dump can name the feature that was running; the vectored crash logger covers that
// already, so it compiles to nothing.
#define MUSH_FEATURE(NAME) ((void)0)

// The ported Monster Book modules were written against a crash-attribution and quarantine
// subsystem ("Mush") that this DLL does not have. Our equivalent is the vectored crash logger in
// system.cpp, which reports EIP and a stack scan, so the registration calls are no-ops:
//
//   REGISTER_CODECAVE / MushRegisterCode  name a patched code address so a crash inside it can be
//                                         attributed. Ours resolves addresses from the log instead.
//   MushFeatureQuarantined                asks whether a startup crash LOOP was detected and this
//                                         feature should sit this session out. We have no such
//                                         detector, so it always answers false and the feature
//                                         always installs. If a Monster Book module ever starts a
//                                         boot-loop, that is the switch to make real.
#define REGISTER_CODECAVE(ADDR, NAME) ((void)(ADDR), (void)(NAME))
inline void MushRegisterCode(uintptr_t uAddr, const char* pszName) { (void)uAddr; (void)pszName; }
inline bool MushFeatureQuarantined(const char* pszName) { (void)pszName; return false; }

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

// weather.cpp + weatherfx.cpp + lamps.cpp. One entry point for the whole render half: lamps.cpp's
// own attach is called from inside it, because MakeObj is one more member of the same
// CMapLoadable pipeline. Owns seven addresses -- LoadMap (0x00639B3D), Update (0x006399EF),
// RestoreTile (0x0063A100), RestoreObj (0x0063AA7E), RestoreBack (0x0063CBBA), MakeBack
// (0x0063CD4E), CNpcPool::OnNpcEnterField (0x006D9993) -- plus IWzGr2D::CreateLayer (0x00426C7E)
// as its layer-capture point and MakeObj (0x0063AD16) for lamps. Disjoint from resolution.cpp,
// which owns RestoreViewRange, the MakeGrid cave and the native weather WrapClip PatchCall.
//
// MUST install after PacketHooks() (the world clock arrives through that dispatcher as 0x373D)
// and after AttachResolutionMod() (weatherwind reads back a fall-distance immediate that
// resolution.cpp rewrites).
void AttachWeatherMod();
// weatherwind.cpp: one code cave at 0x006406F3 replacing branch 0's per-particle horizontal
// spread with a server-seeded prevailing gust. Independent of AttachWeatherMod -- it patches
// inside the weather BUILDER, which that module does not touch.
void AttachWeatherWindMod();

// Per-frame ticks, driven from CWvsApp::CallUpdate_hook. Both self-throttle on GetTickCount, so
// calling them every frame is cheap; the intervals live next to their implementations.
void ResMan_FlushTick();         // resman.cpp  — periodic IWzResMan::FlushCachedObjects
void MemStat_Tick();             // memstat.cpp — periodic memory sample
class CInPacket;

// mistexplosion.cpp — Mist Explosion (2121040). Detonates the local player's Poison Mist clouds:
// damage to every mob inside each cloud's own LTRB, then the cloud is consumed. Returns how many
// mists went off; 0 is an ordinary outcome (none down, or none of them ours).
int MistExplosion_BeginCast();   // before the cast: >0 if clouds exist, and suppresses the
                                 // cast's own targeting so only the clouds deal damage
void MistExplosion_EndCast();    // after the cast, whatever its outcome: lifts the suppression
int MistExplosion_Detonate();
void MistExplosion_OnClientTick();   // releases staggered damage lines as they come due
void MistExplosion_CheckSuppressionLeak(int nSkillID);  // self-heals a leaked target suppression
void AttachMistExplosionMod();
extern int mistExplosionSkillId;

// skills.cpp — one secured long out of SKILLLEVELDATA at the player's LEARNED level.
// Offsets (verified in CSummoned::Init @0x007A342C..0x007A346A and the summon work):
//   +0x1C  damage        +0x34  mad
//   +0x100 attackCount   +0x130 mobCount
int GetSkillLevelDataLong(int skillId, int valueOff);

// skills.cpp — the player's LEARNED level in a skill, clamped by the client to a valid
// level-data index. 0 means the skill is not learned.
int GetLearnedSkillLevel(int skillId);

// skills.cpp — overwrite the instruction at dwOriginAddress with `jmp ptrCodeCave` (E9 rel32),
// NOPing nNOPCount bytes first when the replaced instruction is longer than the five the jump
// needs. Silently skips the patch if the target is out of rel32 range.

// skills.cpp — magic damage for one mob using the server's own formula (setMAD/topMAD + the
// mastery range roll + level/MDDamage mitigation). nSkillDmgPct is the skill's WZ `damage`/`mad`.
// nSkillID != 0 additionally applies CalcSkillDamageMultiplier (level, mob defence, poison,
// airborne, boss, order drop-off) and rolls a critical, writing 1 to pbCritOut on a crit.
// nSkillID == 0 is the original summon behaviour.
int MagicSkillDamageOnMob(void* pMob, int nSkillDmgPct, int nSkillID = 0, int nOrder = 0,
                          int* pbCritOut = nullptr);

// CUIMonsterBook::SetTabEnable (monsterBook.cpp); USE_MONSTER_BOOK_OPEN.
// PAIRED with a server-side data fix: String.wz/MonsterBook.img must carry an entry for every
// card mob, else CMonsterBookMan has no record and Basic Info prints "HP : (null)".
void AttachMonsterBookMod();

void AttachMonsterBookFoundInMod();   // Found In row click -> world map at that field
void AttachMonsterBookDropsMod();     // drop chance % label on each Dropping icon (S2C 0x372C type 0)
void MonsterBookDrops_OnClientTick();
void MonsterBookDrops_OnPacket(CInPacket* pPacket);
void AttachMonsterBookSearchMod();    // two search fields: mob name (local) + item name (types 1/2)
void MonsterBookSearch_OnClientTick();
void MonsterBookSearch_OnPacket(CInPacket* pPacket);

// Shared right-page arbitration. DEFINED IN monsterBook.cpp so neither of the two big modules has
// to link against the other: the drops module must not paint its % labels while the search
// module item-result view owns the right page.
void MonsterBookSearch_SetItemResultView(bool bOn);
bool MonsterBookSearch_IsItemResultView();

// deathcount.cpp — Expedition Death Count HUD. Server-driven and passive: OnPacket only records
// the value, OnClientTick does all WZ/graphics work on the main-thread step.
void DeathCount_OnPacket(CInPacket* pPacket);   // S2C 0x3727, swallowed by packet.cpp
void DeathCount_OnFieldChange();                // S2C 0x007D SET_FIELD, observed only
void DeathCount_OnClientTick();

// coloringprism.cpp / weapontint.cpp -- Coloring Prism (item 5782000).
// weapontint.cpp owns the recolour, the tint table and both opcodes (C2S 0x372E / S2C 0x372F)
// and installs the ONE Detour of the pair, on CAvatar::PrepareActionLayer. coloringprism.cpp
// owns the window and, since nothing else in this DLL owns it, the CDraggableItem::OnDoubleClicked
// detour that opens it. Full API in weapontint.h.
void AttachWeaponTintMod();
void AttachColoringPrismMod();
void WeaponTint_HandleSync(CInPacket* pPacket);   // S2C 0x372F, swallowed by packet.cpp
void WeaponTint_Tick();                           // main-thread step
bool ColorPrism_HandleItemDrop(void* pTo, int invType, int invPos);   // from the OnDropped hook
void ColorPrism_OnServerOpen();                   // S2C 0x372F subtype 4, receive thread: records only
void ColorPrism_OnClientTick();                   // main-thread step: opens the window if asked

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

    const DebugFlags& dbg = GetDebugFlags();
    if (dbg.bDisableAllClientHooks) {
        // Stock client. This is the baseline README-linux.md asks users to establish: if the
        // problem survives THIS, it is not MapleNight. Nothing below installs.
        LogInfo("AttachClientHooks: DisableAllClientHooks=1 - attaching NOTHING (stock client)");
        return;
    }

    (AttachClientBypass());
    //AttachClientInlink();
    if (dbg.bMinimalHooks) {
        // Only what the client needs to run: the bypass above (window, mutex, socket redirect)
        // plus the resource manager, which the client cannot start without. Everything cosmetic
        // and every gameplay edit is skipped.
        (AttachResManMod());
        LogInfo("AttachClientHooks: MinimalHooks=1 - bypass + resman only");
        return;
    }

    (AttachStringPoolMod());
    (AttachResManMod());
    (AttachAvatarDataMod());
    (AttachItemEffectMod());
    (AttachCashWeaponMod());
    if (!dbg.bDisableResolution) {
        (AttachResolutionMod());
    } else {
        LogInfo("AttachClientHooks: DisableResolution=1 - skipping resolution + bag window");
    }
    (AttachMobHpTagMod());
    (AttachToolTipMod());
    (AttachIconIconMod());
    (AttachTempStatMod());
    if (!dbg.bDisableSkills) {
        (AttachSkillEdits());
    } else {
        LogInfo("AttachClientHooks: DisableSkills=1 - skipping skill edits");
    }
    (AttachOtherHooks());
    (InitExpOverride());
    (PacketHooks());
    (AttachMapObjectFade());
    (BGMOverride());
    if (!dbg.bDisableResolution) {
        (AttachBagWindowMod());   // README: DisableResolution also disables the bag window
    }
    (AttachMemStat());
    (AttachMistExplosionMod());
    // Monster Book. DROPS installs the Dropping paging patch that SEARCH later reads, so the
    // order of these two matters; the other two are independent.
    (AttachMonsterBookMod());        // open the book up: colour icons, 0-counters, art/HP/MP, tabs
    (AttachMonsterBookFoundInMod()); // Found In rows clickable -> world map
    (AttachMonsterBookDropsMod());   // drop chance % on the Dropping icons
    (AttachMonsterBookSearchMod());  // mob-name + item-name search fields
    // Coloring Prism. The tint mod must come first: it owns the render hook and the tint table
    // the window reads, and the window's own detour dispatches into it.
    (AttachWeaponTintMod());
    (AttachColoringPrismMod());
    // Weather. AFTER PacketHooks() (0x373D arrives through that dispatcher) and AFTER
    // AttachResolutionMod() (weatherwind reads an immediate resolution.cpp rewrites), and before
    // the first stage is constructed -- LoadMap fires on the very first map.
    (AttachWeatherMod());
    (AttachWeatherWindMod());
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

void CodeCave(void* ptrCodeCave, const DWORD dwOriginAddress, const int nNOPCount);

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