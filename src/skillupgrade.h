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
//   kProp        TryDoingShootAttack   @0x00954E63  (knockback roll)
//   kBulletCount TryDoingShootAttack   @0x009547A2  (shots fired per attack)
//   kMobCount    TryDoingMeleeAttack   @0x009514A1  (mobs gathered per attack)
// Any other field works too once its offset is known -- the table takes a raw offset.
namespace SkillField {
constexpr int kProp = 0xF4;
constexpr int kBulletCount = 0x100;
constexpr int kMobCount = 0x130;
}   // namespace SkillField

// The local character's learned level in a skill, clamped to its master level, or 0 when the
// character has no points in it. Safe to call at any time: it answers 0 while CSkillInfo or
// CWvsContext are still null, which they are for much of startup.
int GetLearnedSkillLevelSafe(int nSkillID);

// Bonus this character's learned skills currently add to `nTargetSkillID`'s `nFieldOff`.
// 0 when nothing applies. Exposed so other modules (tooltips, damage preview) can agree with
// what combat does.
int GetSkillUpgradeBonus(int nTargetSkillID, int nFieldOff);

// Installs the SKILLENTRY::GetLevelData hook. Called from AttachSkillEdits.
void AttachSkillUpgrades();
