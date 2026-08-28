#include "pch.h"

#include "skillupgrade.h"

#include "debug.h"
#include "hook.h"
#include "wvs/secure.h"

// ---------------------------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------------------------

struct SkillUpgrade {
    int nSourceSkillID;   // the skill whose points grant the boost (usually a passive)
    int nTargetSkillID;   // the skill whose level data gets raised
    int nFieldOff;        // which SKILLLEVELDATA field (SkillField::*)
    int nFlat;            // added once the source skill has at least one point
    int nPerLevel;        // added per point in the source skill
    int nMaxBonus;        // cap on the total added; 0 = uncapped
};

// One row per (source, target, field). Several rows may target the same field -- they add up.
//
// nFlat vs nPerLevel: a field like bulletCount wants a flat step (one extra arrow while the
// passive is learned at all), whereas prop or mobCount usually wants to scale. Both can be set on
// the same row; the total is nFlat + nPerLevel * sourceLevel, clamped to nMaxBonus.
static const SkillUpgrade g_skillUpgrades[] = {
        // 3120010 -> 3111006 Strafe: +1 bullet. Flat, so any point in the passive is the whole
        // upgrade; to make it scale instead, move the 1 into nPerLevel and cap it with nMaxBonus.
        { 3120010, 3111006, SkillField::kBulletCount, 1, 0, 0 },
};

static constexpr size_t kUpgradeCount = sizeof(g_skillUpgrades) / sizeof(g_skillUpgrades[0]);

// ---------------------------------------------------------------------------------------------
// Client plumbing
// ---------------------------------------------------------------------------------------------

// CSkillInfo is created long after the DLL attaches, so the instance is read through its global
// on every use rather than cached.
using t_GetSkill = void*(__fastcall*)(void* pThis, void* edx, int nSkillID);
static auto pCSkillInfo_GetSkill = reinterpret_cast<t_GetSkill>(0x0075C755);
static void** const kppSkillInfo = reinterpret_cast<void**>(0x00BE78DC);

// CSkillInfo::GetSkillLevel(CharacterData*, nSkillID, SKILLENTRY**) -> the learned level, already
// clamped to the skill's master level.
static auto pGetSkillLevel = reinterpret_cast<int(__thiscall*)(void*, void*, int, int)>(0x007616F6);
static auto pGetCharacterData = reinterpret_cast<void*(__thiscall*)(void*, void*)>(0x00425D0B);
static auto pReleaseZRef = reinterpret_cast<void(__thiscall*)(void*, void*)>(0x00428C44);
static void** const kppWvsContext = reinterpret_cast<void**>(0x00BE7918);

// SKILLENTRY::GetLevelData(nLevel) -> const SKILLLEVELDATA&. Everything that reads a skill's
// per-level numbers goes through here, which is why this is the hook point: the table is applied
// lazily, on the read, so it always reflects the player's current points without needing to know
// when they changed.
static auto pGetLevelData = reinterpret_cast<void*(__thiscall*)(void*, int)>(0x00760F23);

int GetLearnedSkillLevelSafe(int nSkillID) {
    void* pInfo = *kppSkillInfo;
    void* pContext = *kppWvsContext;
    if (!pInfo || !pContext) {
        return 0;
    }

    void* zref[2] = { nullptr, nullptr };
    pGetCharacterData(pContext, zref);
    void* pCharData = zref[1];
    int nLevel = pCharData ? pGetSkillLevel(pInfo, pCharData, nSkillID, 0) : 0;
    if (zref[1]) {
        pReleaseZRef(zref, nullptr);
    }
    return nLevel > 0 ? nLevel : 0;
}

// ---------------------------------------------------------------------------------------------
// Base-value cache
// ---------------------------------------------------------------------------------------------

// The WZ value of a field before any boost. Captured the first time a (skill, level, field) is
// seen -- always before the first write -- so the boost is recomputed from the base instead of
// compounding on itself, and so a respec can put the field back.
struct BaseValue {
    int nSkillID;
    int nLevel;
    int nFieldOff;
    int nBase;
};

static std::vector<BaseValue> g_baseValues;

static int* FindBase(int nSkillID, int nLevel, int nFieldOff) {
    for (BaseValue& bv : g_baseValues) {
        if (bv.nSkillID == nSkillID && bv.nLevel == nLevel && bv.nFieldOff == nFieldOff) {
            return &bv.nBase;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// Applying the table
// ---------------------------------------------------------------------------------------------

int GetSkillUpgradeBonus(int nTargetSkillID, int nFieldOff) {
    int nBonus = 0;
    for (size_t i = 0; i < kUpgradeCount; ++i) {
        const SkillUpgrade& up = g_skillUpgrades[i];
        if (up.nTargetSkillID != nTargetSkillID || up.nFieldOff != nFieldOff) {
            continue;
        }
        const int nSourceLevel = GetLearnedSkillLevelSafe(up.nSourceSkillID);
        if (nSourceLevel <= 0) {
            continue;
        }
        int nRow = up.nFlat + up.nPerLevel * nSourceLevel;
        if (up.nMaxBonus > 0 && nRow > up.nMaxBonus) {
            nRow = up.nMaxBonus;
        }
        nBonus += nRow;
    }
    return nBonus;
}

// Reads and writes with our own copies of the client's codec rather than calling the client's
// _ZtlSecureFuse<long> (0x00416563): that one throws ZException(5) on a checksum mismatch, which
// surfaces as the client's "Access is denied" box. ZtlSecure<int> in wvs/secure.h is the same
// algorithm -- value = at[0] ^ rotl(at[1], 5), checksum = at[1] + rotr(at[0] ^ 0xBAADF00D, 5) --
// so a field written here fuses cleanly when the client reads it back.
static void SyncField(void* pLevelData, int nSkillID, int nLevel, int nFieldOff, int nBonus) {
    if (!pLevelData) {
        return;
    }
    auto* pField = reinterpret_cast<ZtlSecure<int>*>(reinterpret_cast<char*>(pLevelData) + nFieldOff);
    if (IsBadWritePtr(pField, sizeof(ZtlSecure<int>))) {
        return;
    }

    int* pBase = FindBase(nSkillID, nLevel, nFieldOff);
    if (!pBase) {
        g_baseValues.push_back({ nSkillID, nLevel, nFieldOff, pField->Fuse() });
        pBase = &g_baseValues.back().nBase;
    }

    const int nWant = *pBase + nBonus;
    if (pField->Fuse() != nWant) {
        pField->Tear(nWant);
        LogInfo("[skillupgrade] %d level %d field +0x%X: %d -> %d", nSkillID, nLevel, nFieldOff,
                *pBase, nWant);
    }
}

// Which rows target this skill, as a set of (field, bonus) pairs -- rows sharing a field are
// summed by GetSkillUpgradeBonus, so each field is written once.
static void ApplyUpgrades(void* pLevelData, int nSkillID, int nLevel) {
    for (size_t i = 0; i < kUpgradeCount; ++i) {
        const SkillUpgrade& up = g_skillUpgrades[i];
        if (up.nTargetSkillID != nSkillID) {
            continue;
        }
        bool bAlreadyDone = false;
        for (size_t j = 0; j < i; ++j) {
            if (g_skillUpgrades[j].nTargetSkillID == nSkillID
                    && g_skillUpgrades[j].nFieldOff == up.nFieldOff) {
                bAlreadyDone = true;
                break;
            }
        }
        if (bAlreadyDone) {
            continue;
        }
        SyncField(pLevelData, nSkillID, nLevel, up.nFieldOff,
                GetSkillUpgradeBonus(nSkillID, up.nFieldOff));
    }
}

// A target skill's SKILLENTRY*, resolved on demand and remembered, so the hook costs one pointer
// compare per call in the overwhelmingly common case of a skill nothing upgrades.
struct TargetEntry {
    int nSkillID;
    void* pSkill;
};

static std::vector<TargetEntry> g_targets;
static bool g_targetsResolved = false;

static void ResolveTargets() {
    void* pInfo = *kppSkillInfo;
    if (!pInfo) {
        return;   // CSkillInfo not up yet; try again on the next call
    }
    g_targets.clear();
    bool bAllResolved = true;
    for (size_t i = 0; i < kUpgradeCount; ++i) {
        const int nID = g_skillUpgrades[i].nTargetSkillID;
        bool bSeen = false;
        for (const TargetEntry& te : g_targets) {
            if (te.nSkillID == nID) {
                bSeen = true;
                break;
            }
        }
        if (bSeen) {
            continue;
        }
        void* pSkill = pCSkillInfo_GetSkill(pInfo, nullptr, nID);
        if (pSkill) {
            g_targets.push_back({ nID, pSkill });
        } else {
            // Either the skill's WZ node has not been parsed yet (the first level-data reads
            // happen while skill data is still loading) or Data/Skill genuinely has no such id.
            // Both look the same here, so keep retrying rather than latching the row off; a row
            // whose id does not exist just re-does one map lookup per call and stays inert.
            bAllResolved = false;
        }
    }
    // Only stop looking once every target has an entry.
    g_targetsResolved = bAllResolved;
}

static int TargetSkillIDOf(void* pSkill) {
    if (!g_targetsResolved) {
        ResolveTargets();
    }
    for (const TargetEntry& te : g_targets) {
        if (te.pSkill == pSkill) {
            return te.nSkillID;
        }
    }
    return 0;
}

// Resolving a source skill's level goes through CSkillInfo::GetSkillLevel, which is free to read
// level data itself -- and a row whose source and target are the same skill would then re-enter
// here. The guard keeps the boost logic to one frame; nested calls just pass through.
static bool g_inApply = false;

void* __fastcall GetLevelData_hook(void* pSkill, void* edx, int nLevel) {
    // Level 0 is an out-of-bounds read, not a no-op. GetLevelData ends with
    //     return 484 * level + base - 484;
    // so level 0 hands back a pointer 484 bytes BEFORE the array, and callers that read a field
    // off it walk into unmapped memory. Several client sites invite exactly that: they check the
    // SKILLENTRY for null and then pass the level straight through without checking it, e.g.
    // SecondaryStat::GetIncPAD and its two siblings (0x0077DF24 tests the entry, 0x0077DF27 calls
    // this, 0x0077DF2C faults). Those run only while the energy-charge stat is set, which vanilla
    // only ever did for a pirate holding the skill -- a character who is charged WITHOUT the skill
    // learned reads level 0 and dies. Ours is exactly that character.
    //
    // Clamping to 1 turns the fault into reading the level-1 record. For a caller that should not
    // have got a bonus at all this is a small wrong number instead of a crash, and it protects
    // every such site at once rather than caving each one.
    if (nLevel < 1) {
        LOG_ONCE("[skillupgrade] GetLevelData level %d clamped to 1 (would read base-484)", nLevel);
        nLevel = 1;
    }
    void* pLevelData = pGetLevelData(pSkill, nLevel);
    if (g_inApply) {
        return pLevelData;
    }
    const int nSkillID = TargetSkillIDOf(pSkill);
    if (nSkillID) {
        g_inApply = true;
        ApplyUpgrades(pLevelData, nSkillID, nLevel);
        g_inApply = false;
    }
    return pLevelData;
}

void AttachSkillUpgrades() {
    // Always attached, even with an empty upgrade table: the hook also carries the level-0 clamp
    // above, which is a crash fix and must not depend on whether anyone has added an upgrade row.
    ATTACH_HOOK(pGetLevelData, GetLevelData_hook);
}
