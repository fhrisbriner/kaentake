#pragma once

// Skill-upgrade registry: let one skill (usually a passive) raise a field in ANOTHER skill's
// level data. Adding points to 3120010 giving 3111006 an extra bullet is one row in a table.
//
// The client drives what actually happens in combat -- it decides how many bullets a shot fires,
// how many mobs a swing gathers -- so the boost is applied by rewriting the target skill's
// SKILLLEVELDATA in memory. The server has a mirror of the same table
// (constants/skills/SkillUpgrades.java) for the places where it independently reads the same
// field (ammo consumption, per-bullet effect rolls). Rows added on one side must be added on the
// other or the two disagree.

// SKILLLEVELDATA field offsets. Every field is a ZtlSecure<int> (value, key, checksum = 12 bytes),
// read by the client with _ZtlSecureFuse<long>(levelData + off, *(levelData + off + 8)).
// Verified against the client's own reads:
//   kDamage      CalcDamage::PDamage   @0x0078E239  (skill damage %)
//   kProp        TryDoingShootAttack   @0x00954E63  (knockback roll)
//   kAttackCount TryDoingShootAttack   @0x009547A2  (damage LINES per target)
//   kBulletCount TryDoingShootAttack   @0x00954381  (projectiles drawn/fired per attack)
//   kMobCount    TryDoingMeleeAttack   @0x009514A1  (mobs gathered per attack)
// Any other field works too once its offset is known -- the table takes a raw offset.
//
// kAttackCount and kBulletCount are easy to swap by mistake and the mistake is quiet, because the
// client clamps BOTH to a minimum of 1 -- so pointing a row at the wrong one of the pair changes
// nothing visible rather than erroring. They are told apart at their use sites:
//   +0x10C lands in var_88.bLeft, whose only job is the projectile-drawing loop @0x00954553
//          (`imul eax, [ebp+var_88.bLeft]`) -- this is the one that means "how many arrows".
//   +0x100 is multiplied into the per-target damage-line count and nothing else.
// Strafe (3111006) carries `bulletCount` and no `attackCount`, so +0x100 reads 0 for it.
namespace SkillField {
constexpr int kDamage = 0xD0;
constexpr int kProp = 0xF4;
constexpr int kAttackCount = 0x100;
constexpr int kBulletCount = 0x10C;
constexpr int kMobCount = 0x130;
}   // namespace SkillField

// The local character's learned level in a skill, clamped to its master level, or 0 when the
// character has no points in it. Safe to call at any time: it answers 0 while CSkillInfo or
// CWvsContext are still null, which they are for much of startup.
int GetLearnedSkillLevelSafe(int nSkillID);

// Bonus this character's learned skills currently ADD to `nTargetSkillID`'s `nFieldOff`.
// 0 when nothing applies. Exposed so other modules (tooltips, damage preview) can agree with
// what combat does.
//
// Only covers Add rows, whose bonus is spelled out in the table itself. An AddFromSource row draws
// its bonus from another skill's per-level data and so needs the level -- ask GetSkillUpgradeValue.
int GetSkillUpgradeBonus(int nTargetSkillID, int nFieldOff);

// The value `nTargetSkillID`'s `nFieldOff` should hold at `nLevel` once the whole table is applied,
// both modes included. `nBase` is the field's own WZ value at that level. Returns nBase unchanged
// when no row applies or the source skill's data is not loaded yet.
int GetSkillUpgradeValue(int nTargetSkillID, int nLevel, int nFieldOff, int nBase);

// Installs the SKILLENTRY::GetLevelData hook. Called from AttachSkillEdits.
void AttachSkillUpgrades();
