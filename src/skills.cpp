#include "hook.h"
#include "WzLib/IWzArchive.h"
#include "wvs/CUserLocal.h"
#include "wvs/CWvsContext.h"
#include "wvs/field.h"
#include "wvs/packet.h"
#include "wvs/mob.h"
#include "wvs/util.h"
#include "wvs/vecctrl.h"
#include <chrono>
#include <intsafe.h>
#include <random>
#include <string>
#include <sstream>
#include <unordered_map>


class _bstr_t;
using namespace std;
using chrono::duration_cast;
using chrono::milliseconds;
using chrono::system_clock;
chrono::time_point<chrono::steady_clock> jumptimer;
chrono::time_point<chrono::steady_clock> skilltimer;
chrono::time_point<chrono::steady_clock> immunetimer;
chrono::time_point<chrono::steady_clock> aniCancelTimer;
chrono::time_point<chrono::steady_clock> activeTimer;
chrono::time_point<chrono::steady_clock> standTimer;
chrono::time_point<chrono::steady_clock> movementLockTimer;
CVecCtrl* CVecPointer = nullptr;
bool jobPatchesApplied = false;
static std::mt19937 rng(std::random_device{}());
// These are going to be all our Addresses that we jump to depending on what we want our skill to do.
int combatStep = 0x00969026;    // requires further handling
int meleeAttack = 0x009690AE;   // depending on what you want requires further handling should just handle lt/rb skills;
int summonAttack = 0x009689DF;  //
int prepareAttack = 0x00969229; // requires further handling
int magicAttack = 0x0096928B;   // probably requires further handling? MAO doesn't use magic
int statChange = 0x00969284;    // you can use this for any buff and it will pass the skill being used to the server.
int dorecovery = 0x00969217;
int doBoundJump = 0x0096897A; // requires further handling
int shootAttack = 0x009690E9; // should work for basic rt/lb shooting skills.
int doTeleport = 0x00969146;
int mesoExplosion = 0x009681D3;
int smokeScreen = 0x00968209;
const DWORD dwAccuracyCalc = 0x0077F743;
const DWORD dwAccuracyCalcRetn = 0x0077F7E2;
bool siegeMode = false;
bool jumped = false;
int mastery = 0;        // level of the job's mastery skill
int masterySkillID = 0; // which mastery skill that level belongs to
int masteryValue = 0;   // Skill.wz `mastery` for that skill at that level -- drives the damage range
int sairIgnore = 0;
int nw = 0;
int wa = 0;
int tb = 0;
int dex = 0;
int str = 0;
int __int = 0;
int luk = 0;
int pirateCrit;
int speed = 100;
int critSkillID = 0;
int combat1 = 0x0096DABE;
int combat2 = 0x0096DACE;
int comba = 0x00967982;
int doHeal = 0x00967E2B;
bool hurricane = false;
bool flipX = false;
bool jumping = false;
bool wtfnot = false;
double tbw = 5.2;
double taxe = 4.9;
double oaxe = 4.2;
int mesos = 0;
bool doPath = false;
int xPath = 0;
int yPath = 0;
bool isLeftH = false;
DWORD newformulaaddr = 0x00BED58C;
DWORD taxeaddr = 0x00BED90C;
DWORD oaxeaddr = 0x00BED984;
DWORD ohsword = 0x00AFE858;
int LastMapID = 777777777;
int MapID = 0;
bool immune = false;
bool g_inOneTimeAction = false; // true while the LOCAL player is in a one-time action (attack/skill
                                // anim); set by tGetOneTimeAction. -1 from GetOneTimeAction == none.
int myCharacterid = 0;
bool firstLoad = true;
bool AttackMove = false;
int sparkID = 0;
int job = 0;
double strMultiplier = 0.28;
DWORD ZtlBussy = 0x004746DD;
int currStr = 4;
int PassiveSpeed = 0;
int weaponSpeed = 6;
int iframes = 0;
int verdentVeilSL = 0;
int dodgeCount = 0;
bool verdentActive = false;
bool timerRunning = false;
bool animationCancel = false;
bool cancelledSkill = false;
int comboAbility = 0;
bool anicomplete = false;
int lastusedskill = 0;
int rsLevel = 0;
int asLevel = 0;
int bsLevel = 0;
int backflip = 0;
int slam = 0;
int sharpenlevel = 0;
int poisonBonusLevel = 0; // level of the poison-damage passive (read in GetSkillLevel hook)
int rangerShred = 0;
int sniperShred = 0;
int barbShred = 0;
int duelistShred = 0;
int galeShot = 0;
int masterSkies = 0;
int hermitBoss = 0;
int stone = 0;
constexpr int POISON_PASSIVE_SKILLID = 2110009;

// NOT A SKILL
int doActiveJmpBack = 0x0096793B; // return to our existing code.

// Skill.wz carries a per-level `mastery` value, but the client never parses it: there is no
// "mastery" string anywhere in the exe, and SKILLLEVELDATA only holds `x`, which for every mastery
// skill is just the level (1->1 ... 10->10). That `x` is what the client's own mastery getter
// (0x00764795) returns and what the damage range was built from. Read the real value ourselves.
// Cached per (skill, level): the archive lookup is far too slow for the damage path.
static int GetSkillMasteryFromWz(int nSkillID, int nLevel) {
    if (nSkillID <= 0 || nLevel <= 0) return 0;

    static std::unordered_map<int, int> s_cache;
    const int key = nSkillID * 100 + nLevel;
    auto it = s_cache.find(key);
    if (it != s_cache.end()) return it->second;

    int value = 0;
    try {
        // Skill imgs are named after the 3-digit job prefix: 1100000 -> Skill/110.img.
        std::wstring path = L"Skill/" + std::to_wstring(nSkillID / 10000) + L".img";
        IWzPropertyPtr root = get_rm()->GetObjectA(path.c_str()).GetUnknown();
        if (root) {
            IWzPropertyPtr skill = root->item[L"skill"].GetUnknown();
            if (skill) {
                IWzPropertyPtr entry = skill->item[Ztl_bstr_t(std::to_wstring(nSkillID).c_str())].GetUnknown();
                if (entry) {
                    IWzPropertyPtr levels = entry->item[L"level"].GetUnknown();
                    if (levels) {
                        // Skill levels only go up to masterLevel in the data (10 for every mastery
                        // skill here), but +skill sources can push the learned level past that.
                        // Walk down to the highest level node that exists instead of missing and
                        // reporting 0 mastery, which would drop the damage range to its floor.
                        IWzPropertyPtr lv;
                        for (int lookup = nLevel; lookup > 0 && !lv; --lookup) {
                            lv = levels->item[Ztl_bstr_t(std::to_wstring(lookup).c_str())].GetUnknown();
                        }
                        if (lv) {
                            Ztl_variant_t v = lv->item[L"mastery"];
                            value = get_int32(v, 0);
                        }
                    }
                }
            }
        }
    } catch (...) {
        value = 0;
    }

    s_cache.emplace(key, value);
    return value;
}

int pleasejmpout = 0x00791C6C;
double int_multiplier = 4.2;
double str_multiplier = 4.0;
double Hundred = 100;
int topMAD = 0;
int botMAD = 0;
int totmagic = 0;
int pad = 0;
int summonSeekRangeX = 800; // gate-1 (sub_678ECC cave) search half-width around player (known-good value)
int summonSeekRangeY = 400; // gate-1 (sub_678ECC cave) search half-height around player
int summonReach = 1500;     // gate-2 summon attack reach written into v7[13] (+0x34); default ~500

// Attack-follow gate: seeking summons only acquire targets within this window after the local
// player last attacked/cast, so they fight when you fight and idle when you idle. <= 0 disables
// the gate (always auto-aggro, the old behavior). Stationary octopus-type summons don't use the
// seek path and are unaffected.
int summonFollowWindowMs = 6000;
DWORD lastPlayerAttackTick = 0; // GetTickCount() of last local-player attack/skill cast
int summonSeekGateOpen = 1;     // recomputed by UpdateSummonSeekGate before each summon seek
double clMultiplier = 1.25;

int get_weapon_type() {
    int localplayer = *reinterpret_cast<uintptr_t*>(0x00BEBF98);

    if (localplayer == 0) {
        return 0;
    }

    int weapon = *reinterpret_cast<uintptr_t*>(localplayer + 0x4EC);

    return (weapon / 10000) % 100;
}

void setMAD() {
    switch (get_weapon_type()) {
    case 32:
        int_multiplier = 4.2;
    case 37:
        int_multiplier = 4.0;
        break;
    case 38:
        int_multiplier = 5.2;
        break;
    case 43:
        str_multiplier = 4.6;
        break;
    case 44:
        str_multiplier = 4.0;
        break;
    default:
        int_multiplier = 1.0;
        break;
    }

    int int_ = CWvsContext::GetInstance()->get_m_basicStat().nINT.Fuse();
    int magic = CWvsContext::GetInstance()->get_m_secondaryStat().m_magic.Fuse();
    int bonusMagic = CWvsContext::GetInstance()->get_m_secondaryStat().m_bonusMagic.Fuse();
    if (job == 121 || job == 122) {
        int_ = CWvsContext::GetInstance()->get_m_basicStat().nSTR.Fuse();
        magic = pad;
        topMAD = (int_ * magic) / 100;
        botMAD = topMAD * .6;
        return;
    }
    if (magic < 0) {
        bonusMagic = 0;
    }
    int effectiveMagic = (magic + bonusMagic) - int_;
    if (effectiveMagic <= 0) {
        effectiveMagic = (magic + bonusMagic);
    }
    topMAD = (int_ * int_multiplier * effectiveMagic) / 100;
    totmagic = effectiveMagic;
    // Log("%7d, %7d", effectiveMagic, totmagic);
    if (masteryValue > 0) {
        botMAD = topMAD * (0.01 * masteryValue + 0.1);
    } else {
        botMAD = topMAD * 0.1;
    }
}

// ===== Instant cast for charge (keydown / prepare) skills ==================================
// Skills like 2321001 (Big Bang) are keydown skills: the vanilla dispatch sends them to
// CUserLocal::DoActiveSkill_Prepare (0x0096A86E), which only plays the charge-up and sends the
// prepare packet -- the attack itself waits for the key to come back up. Listing a skill here
// does two things:
//   1. this router sends it to DoActiveSkill_MagicAttack instead, so a single press fires the
//      attack outright with no charge, and
//   2. is_keydown_skill reports 0 for it, so nothing else in the client (MP consume in
//      CheckConsumeForActiveSkill, the keydown skill sound, cancel-on-hit in SetDamaged, macro
//      mapping) keeps treating it as a charge skill.
// Only for skills whose attack is a magic attack. A charge skill that attacks with a weapon
// (3121004 Hurricane, 3221001 Pierce, 5101004 Dash) needs meleeAttack/shootAttack instead --
// route those by hand in the chain below rather than adding them here.
static const std::vector<int> g_noChargeSkills = {
    2221021, // Big Bang (Bishop)
};

int __cdecl IsNoChargeSkill(int nSkillID) {
    return std::find(g_noChargeSkills.begin(), g_noChargeSkills.end(), nSkillID)
            != g_noChargeSkills.end();
}

void __declspec(naked) doActiveSkills() {
    __asm {
        // Instant-cast list first: charge skills we want to fire on a single press. esi (the
        // skill id), ebx and ebp are all callee-saved across the cdecl call, so the magic
        // branch below still finds what it needs.
            push esi
            call IsNoChargeSkill
            add esp, 4
            test eax, eax
            jnz magic

        // Warrior
            mov eax, 1001006
            cmp esi, eax
            je melee

            mov eax, 1001007
            cmp esi, eax
            je combat

                // knight
            mov eax, 1101016
            cmp esi, eax
            je melee

            mov eax, 1101015
            cmp esi, eax
            je buff

            mov eax, 1401007
            cmp esi, eax
            je buff

            mov eax, 1401015
            cmp esi, eax
            je buff

            mov eax, 1401016
            cmp esi, eax
            je melee

                // crusher

            mov eax, 1201018
            cmp esi, eax
            je buff

            mov eax, 1201016
            cmp esi, eax
            je buff

            mov eax, 1211000
            cmp esi, eax
            je melee


            mov eax, 1201012
            cmp esi, eax
            je melee

            mov eax, 1501016
            cmp esi, eax
            je buff

            mov eax, 1501012
            cmp esi, eax
            je melee

                // Crusader
            mov eax, 1111009
            cmp esi, eax
            je melee

            mov eax, 1111015
            cmp esi, eax
            je smoke

            mov eax, 1121002
            cmp esi, eax
            je buff

            mov eax, 1121012
            cmp esi, eax
            je melee

            mov eax, 1121021
            cmp esi, eax
            je buff



                // Spearman
            mov eax, 1211012
            cmp esi, eax
            je melee

            mov eax, 1211012
            cmp esi, eax
            je melee

            mov eax, 1211013
            cmp esi, eax
            je melee

            mov eax, 1211014
            cmp esi, eax
            je melee

            mov eax, 1221011
            cmp esi, eax
            je buff

            mov eax, 1221016
            cmp esi, eax
            je melee

            mov eax, 1221052
            cmp esi, eax
            je summons

            mov eax, 1201017
            cmp esi, eax
            je buff

            mov eax, 1210007
            cmp esi, eax
            je buff

                // Duelist

            mov eax, 1411003
            cmp esi, eax
            je buff

            mov eax, 1411005
            cmp esi, eax
            je melee

            mov eax, 1411006
            cmp esi, eax
            je melee

            mov eax, 1411008
            cmp esi, eax
            je melee

            mov eax, 1421004
            cmp esi, eax
            je melee

            mov eax, 1421003
            cmp esi, eax
            je melee

                // Barbarian

            mov eax, 1511006
            cmp esi, eax
            je buff

            mov eax, 1511009
            cmp esi, eax
            je melee

            mov eax, 1511008
            cmp esi, eax
            je melee

            mov eax, 1511003
            cmp esi, eax
            je melee

            mov eax, 1511007
            cmp esi, eax
            je buff

            mov eax, 1521003
            cmp esi, eax
            je buff

            mov eax, 1521011
            cmp esi, eax
            je buff

                // Magician
            mov eax, 2001010
            cmp esi, eax
            je teleport

                // Elementalist
            mov eax, 2101008
            cmp esi, eax
            je magic

            mov eax, 2101007
            cmp esi, eax
            je magic

            mov eax, 2111013
            cmp esi, eax
            je magic

            mov eax, 2401005
            cmp esi, eax
            je magic

            mov eax, 2401004
            cmp esi, eax
            je magic

            mov eax, 2401008
            cmp esi, eax
            je magic

            mov eax, 2401007
            cmp esi, eax
            je magic

                // Cleric
            mov eax, 2201010
            cmp esi, eax
            je buff

            mov eax, 2201011
            cmp esi, eax
            je buff

            mov eax, 2201012
            cmp esi, eax
            je buff

            mov eax, 2201013
            cmp esi, eax
            je magic

            mov eax, 2501010
            cmp esi, eax
            je heal

            mov eax, 2501011
            cmp esi, eax
            je buff

            mov eax, 2501012
            cmp esi, eax
            je buff

            mov eax, 2501013
            cmp esi, eax
            je magic

                // F/P Mage
            mov eax, 2111010
            cmp esi, eax
            je prepare

            mov eax, 2111011
            cmp esi, eax
            je buff

            mov eax, 2111012
            cmp esi, eax
            je buff

            mov eax, 2121007
            cmp esi, eax
            je prepare

            mov eax, 2121026
            cmp esi, eax
            je magic

            mov eax, 2121027
            cmp esi, eax
            je magic

            mov eax, 2121016
            cmp esi, eax
            je magic

            //BISHOP

            mov eax, 2221015
            cmp esi, eax
            je buff

            mov eax, 2221021
            cmp esi, eax
            je magic

            mov eax, 2221106
            cmp esi, eax
            je buff

            // IL ARCHMAGE

            mov eax, 2421005
            cmp esi, eax
            je summons

            mov eax, 2421006
            cmp esi, eax
            je magic

            mov eax, 2421007
            cmp esi, eax
            je magic

            mov eax, 2421014
            cmp esi, eax
            je buff



            //Paladin




            // Priest
            mov eax, 2211011
            cmp esi, eax
            je buff

            mov eax, 2211015
            cmp esi, eax
            je buff

            mov eax, 2211012
            cmp esi, eax
            je buff

            mov eax, 2211014
            cmp esi, eax
            je magic

            mov eax, 2211016
            cmp esi, eax
            je magic


                // IL Mage
            mov eax, 2411010
            cmp esi, eax
            je magic

            mov eax, 2411023
            cmp esi, eax
            je buff

            mov eax, 2411011
            cmp esi, eax
            je prepare

            mov eax, 2411012
            cmp esi, eax
            je magic

            mov eax, 2411013
            cmp esi, eax
            je buff

                // Bowman
            mov eax, 3001013
            cmp esi, eax
            je combat

            mov eax, 3111015
            cmp esi, eax
            je summons

            mov eax, 3111018
            cmp esi, eax
            je buff

                // hunter
            mov eax, 3101007
            cmp esi, eax
            je summons

            mov eax, 3101012
            cmp esi, eax
            je buff

            mov eax, 3401005
            cmp esi, eax
            je shoot

            mov eax, 3401007
            cmp esi, eax
            je summons

            mov eax, 3401012
            cmp esi, eax
            je buff

                // crossbowman

            mov eax, 3201016
            cmp esi, eax
            je shoot

            mov eax, 3201006
            cmp esi, eax
            je buff

            mov eax, 3211016
            cmp esi, eax
            je shoot

            mov eax, 3211015
            cmp esi, eax
            je shoot

            mov eax, 3601000
            cmp esi, eax
            je shoot

            mov eax, 3211014
            cmp esi, eax
            je buff

            mov eax, 3501005
            cmp esi, eax
            je shoot

            mov eax, 3501003
            cmp esi, eax
            je melee

            mov eax, 3501016
            cmp esi, eax
            je shoot

                // wind archer

            mov eax, 3411002
            cmp esi, eax
            je buff

            mov eax, 3411006
            cmp esi, eax
            je shoot

            mov eax, 3411007
            cmp esi, eax
            je shoot

            mov eax, 3411010
            cmp esi, eax
            je summons

            mov eax, 3411005
            cmp esi, eax
            je buff

                // sniper

            mov eax, 3511003
            cmp esi, eax
            je shoot

            mov eax, 3511008
            cmp esi, eax
            je melee

            mov eax, 3511004
            cmp esi, eax
            je shoot

            mov eax, 3511019
            cmp esi, eax
            je shoot

            mov eax, 3511002
            cmp esi, eax
            je buff


                // Thief 2nd
            mov eax, 4001004
            cmp esi, eax
            je buff

            mov eax, 4101008
            cmp esi, eax
            je shoot


            mov eax, 4401008
            cmp esi, eax
            je shoot

                // hermit

            mov eax, 4111021
            cmp esi, eax
            je shoot

                // Bandit
            mov eax, 4201014
            cmp esi, eax
            je melee

            mov eax, 4501005
            cmp esi, eax
            je melee

            mov eax, 4501014
            cmp esi, eax
            je melee

            mov eax, 4111017
            cmp esi, eax
            je summons

                // CB
            mov eax, 4211011
            cmp esi, eax
            je melee

            mov eax, 4211015
            cmp esi, eax
            je melee

            mov eax, 4211016
            cmp esi, eax
            je buff

            mov eax, 4211001
            cmp esi, eax
            je buff

                // ninja
            mov eax, 4411006
            cmp esi, eax
            je shoot

            mov eax, 4411004
            cmp esi, eax
            je melee

            mov eax, 4411019
            cmp esi, eax
            je shoot

            mov eax, 4411004
            cmp esi, eax
            je melee

                // thief 3rd job bandit

            mov eax, 4511006
            cmp esi, eax
            je meso

            mov eax, 4511013
            cmp esi, eax
            je buff

            mov eax, 4511016
            cmp esi, eax
            je buff

            mov eax, 4511003
            cmp esi, eax
            je melee

            mov eax, 4511007
            cmp esi, eax
            je buff

            mov eax, 5411021
            cmp esi, eax
            je melee

            mov eax, 5411002
            cmp esi, eax
            je melee

            mov eax, 5201007
            cmp esi, eax
            je jumpmove

            mov eax, 5411022
            cmp esi, eax
            je melee

            mov eax, 5411020
            cmp esi, eax
            je melee


            mov eax, 5411026
            cmp esi, eax
            je buff


            mov eax, 5511015
            cmp esi, eax
            je summons

            mov eax, 5511006
            cmp esi, eax
            je shoot

            mov eax, 5511002
            cmp esi, eax
            je summons

            mov eax, 5511014
            cmp esi, eax
            je summons

            mov eax, 5511017
            cmp esi, eax
            je shoot

            mov eax, 3601001
            cmp esi, eax
            je buff

            mov eax, 3601002
            cmp esi, eax
            je buff

            mov eax, 3601003
            cmp esi, eax
            je buff

            mov eax, 3601004
            cmp esi, eax
            je buff

            mov eax, 3601005
            cmp esi, eax
            je buff

            mov eax, 3601006
            cmp esi, eax
            je buff

            mov eax, 3601007
            cmp esi, eax
            je shoot

            mov eax, 5111007
            cmp esi, eax
            je buff

            mov eax, 5211012
            cmp esi, eax
            je buff

            mov eax, 5211016
            cmp esi, eax
            je buff

            mov eax, 5211017
            cmp esi, eax
            je shoot


                // Pirate 3rd
            mov eax, 5111010
            cmp esi, eax
            je buff

            mov eax, 5111013
            cmp esi, eax
            je melee

            mov eax, 5111011
            cmp esi, eax
            je buff

            mov eax, 5111012
            cmp esi, eax
            je buff

            mov eax, 5111014
            cmp esi, eax
            je summons

            mov eax, 5111015
            cmp esi, eax
            je summons

            mov eax, 5111016
            cmp esi, eax
            je buff

            mov eax, 5111017
            cmp esi, eax
            je shoot

            mov eax, 5511016
            cmp esi, eax
            je shoot

            mov eax, 4101006
            cmp esi, eax
            je buff

            mov eax, 4101009
            cmp esi, eax
            je jumpmove

            mov eax, 4401006
            cmp esi, eax
            je buff

            mov eax, 4201006
            cmp esi, eax
            je buff

            mov eax, 4111021
            cmp esi, eax
            je shoot

            mov eax, 2511004
            cmp esi, eax
            je buff

            mov eax, 2511006
            cmp esi, eax
            je magic

            mov eax, 2511001
            cmp esi, eax
            je buff

            mov eax, 4501006
            cmp esi, eax
            je buff

            mov eax, 2211015
            cmp esi, eax
            je buff

            mov eax, 2511011
            cmp esi, eax
            je buff

            mov eax, 4511001
            cmp esi, eax
            je buff

            mov eax, 5101009
            cmp esi, eax
            je melee

            mov eax, 5101010
            cmp esi, eax
            je melee

            mov eax, 2211013
            cmp esi, eax
            je buff

            mov eax, 3601009
            cmp esi, eax
            je melee

            mov eax, 3601010
            cmp esi, eax
            je buff

            mov eax, 1121002
            cmp esi, eax
            je buff

            mov eax, 1121012
            cmp esi, eax
            je melee

            mov eax, 1221011
            cmp esi, eax
            je buff

            mov eax, 1221016
            cmp esi, eax
            je melee

            mov eax, 1221021
            cmp esi, eax
            je melee

            mov eax, 1221052
            cmp esi, eax
            je summons


            mov eax, 1421009
            cmp esi, eax
            je melee

            mov eax, 1521002
            cmp esi, eax
            je buff

            mov eax, 1521100
            cmp esi, eax
            je melee

            mov eax, 1521229
            cmp esi, eax
            je melee

            mov eax, 3601011
            cmp esi, eax
            je buff



            mov eax, 2121005
            cmp esi, eax
            je summons

            mov eax, 2121026
            cmp esi, eax
            je magic

            mov eax, 2121027
            cmp esi, eax
            je magic

            mov eax, 2221000
            cmp esi, eax
            je buff

            mov eax, 2221015
            cmp esi, eax
            je buff

            mov eax, 2221018
            cmp esi, eax
            je magic

            mov eax, 2221021
            cmp esi, eax
            je magic

            mov eax, 2421005
            cmp esi, eax
            je summons

            mov eax, 2421007
            cmp esi, eax
            je magic

            mov eax, 2421014
            cmp esi, eax
            je buff

            mov eax, 2521009
            cmp esi, eax
            je magic

            mov eax, 2521014
            cmp esi, eax
            je magic

            mov eax, 2521023
            cmp esi, eax
            je buff

            mov eax, 2521032
            cmp esi, eax
            je buff

            mov eax, 3221007
            cmp esi, eax
            je shoot

            mov eax, 3221018
            cmp esi, eax
            je shoot

            mov eax, 3421005
            cmp esi, eax
            je buff

            mov eax, 3421008
            cmp esi, eax
            je buff

            mov eax, 3521007
            cmp esi, eax
            je shoot

            mov eax, 3521008
            cmp esi, eax
            je shoot

            mov eax, 3521012
            cmp esi, eax
            je buff

            mov eax, 3521052
            cmp esi, eax
            je shoot

            mov eax, 3601014
            cmp esi, eax
            je summons

            mov eax, 3601015
            cmp esi, eax
            je shoot

            mov eax, 4121019
            cmp esi, eax
            je shoot

            mov eax, 4121017
            cmp esi, eax
            je shoot

            mov eax, 4421015
            cmp esi, eax
            je shoot

            mov eax, 4521011
            cmp esi, eax
            je melee

            mov eax, 4521012
            cmp esi, eax
            je buff

            mov eax, 3601017
            cmp esi, eax
            je summons

            mov eax, 3601021
            cmp esi, eax
            je summons

            mov eax, 3601022
            cmp esi, eax
            je summons

            mov eax, 3601023
            cmp esi, eax
            je summons


            mov eax, 4521013
            cmp esi, eax
            je melee

            mov eax, 5121008
            cmp esi, eax
            je melee

            mov eax, 5121011
            cmp esi, eax
            je melee

            mov eax, 5121013
            cmp esi, eax
            je melee

            mov eax, 5421009
            cmp esi, eax
            je buff

            mov eax, 5521016
            cmp esi, eax
            je summons

            mov eax, 5521009
            cmp esi, eax
            je shoot

            mov eax, 5521003
            cmp esi, eax
            je shoot

            mov eax, 5421007
            cmp esi, eax
            je melee

            mov eax, 2421006
            cmp esi, eax
            je magic

            mov eax, 2301005
            jmp doActiveJmpBack

            melee : jmp meleeAttack
            summons : jmp summonAttack
            prepare : jmp prepareAttack
            magic : jmp magicAttack
            buff : jmp statChange
            combat : jmp combatStep
            recover : jmp dorecovery
            shoot : jmp shootAttack
            jumpmove : jmp doBoundJump
            teleport : jmp doTeleport
            smoke : jmp smokeScreen
            heal : jmp doHeal
            meso : jmp mesoExplosion
    }
}

struct Pattern {
    std::vector<BYTE> bytes;
    std::vector<bool> mask;
};

int getCurrentComboCount() {
    int localplayer = *reinterpret_cast<uintptr_t*>(0x00BEBF98);
    if (localplayer == 0) {
        return 0;
    }

    int comboCount = *reinterpret_cast<uintptr_t*>(localplayer + 0x3220);
    return comboCount;
}

Pattern ParsePattern(const char* pattern) {
    Pattern pat;
    std::stringstream ss(pattern);
    std::string byteStr;

    while (ss >> byteStr) {
        if (byteStr == "??" || byteStr == "?") {
            pat.bytes.push_back(0x00);
            pat.mask.push_back(false); // wildcard
        } else {
            pat.bytes.push_back((BYTE)strtoul(byteStr.c_str(), nullptr, 16));
            pat.mask.push_back(true); // exact match
        }
    }

    return pat;
}

auto GetCharacterData = (void*(__thiscall*)(CWvsContext*, void*))0x00425D0B;

unsigned int FindAoB(const char* patternStr, DWORD start, DWORD end, int skip) {
    Pattern pat = ParsePattern(patternStr);

    int foundCount = 0;

    for (DWORD i = start; i < end - pat.bytes.size(); i++) {
        bool found = true;

        for (size_t j = 0; j < pat.bytes.size(); j++) {
            BYTE memByte = *(BYTE*)(i + j);

            if (pat.mask[j] && memByte != pat.bytes[j]) {
                found = false;
                break;
            }
        }

        if (found) {
            if (foundCount == skip)
                return i;

            foundCount++;
        }
    }

    return 0;
}

void WriteDouble(const DWORD dwOriginAddress, const double dwValue) {
    DWORD dwOldProtect;
    VirtualProtect((LPVOID)dwOriginAddress, sizeof(double), PAGE_EXECUTE_READWRITE, &dwOldProtect);
    *(double*)dwOriginAddress = dwValue;
    VirtualProtect((LPVOID)dwOriginAddress, sizeof(double), dwOldProtect, &dwOldProtect);
}

void Patch1Array(const DWORD dwOriginAddress, unsigned char* ucValue, const int ucValueSize) {
    for (int i = 0; i < ucValueSize; i++) {
        const DWORD newAddr = dwOriginAddress + i;
        DWORD dwOldProtect;
        VirtualProtect((LPVOID)newAddr, sizeof(unsigned char), PAGE_EXECUTE_READWRITE, &dwOldProtect);
        *(unsigned char*)newAddr = ucValue[i];
        VirtualProtect((LPVOID)newAddr, sizeof(unsigned char), dwOldProtect, &dwOldProtect);
    }
}

bool IsSkipped(DWORD addr, const int skipAddresses[], int skipCount) {
    for (int i = 0; i < skipCount; i++) {
        if (addr == (DWORD)skipAddresses[i])
            return true;
    }
    return false;
}

class SKILLENTRY {
public:
    int skillId;
    ZXString<char> skillName;
    ZXString<char> description;
    int skillType;
};


typedef SKILLENTRY*(__fastcall* SkillInfo__GetSkill_t)(PVOID pThis, PVOID edx, int nSkillID);
static auto SkillInfo__GetSkill = reinterpret_cast<SkillInfo__GetSkill_t>(0x0075C755);

class SkillInfo {
public:
    static SkillInfo* GetInstance() {
        return *reinterpret_cast<SkillInfo**>(0x00BE78DC);
    }

    SKILLENTRY* GetSkill(int nSkillID) {
        return SkillInfo__GetSkill(GetInstance(), NULL, nSkillID);
    }
};

static bool MatchPatternAt(const Pattern& pat, DWORD addr) {
    for (size_t j = 0; j < pat.bytes.size(); j++) {
        if (pat.mask[j] && *(BYTE*)(addr + j) != pat.bytes[j])
            return false;
    }
    return true;
}

void ReplaceValue(const char* AoB, int value, const int skipAddresses[], int skipCount) {
    Pattern pat = ParsePattern(AoB);
    if (pat.bytes.empty())
        return;

    const DWORD start = 0x00700000;
    const DWORD end = 0x00AAAAAA;
    if (pat.bytes.size() >= end - start)
        return;

    const DWORD last = end - (DWORD)pat.bytes.size();
    for (DWORD i = start; i < last; i++) {
        if (MatchPatternAt(pat, i) && !IsSkipped(i, skipAddresses, skipCount))
            Patch4(i, value);
    }
}

void ReplaceValueSimple(const char* AoB, int value) {
    ReplaceValue(AoB, value, nullptr, 0);
}

struct ReplaceEntry {
    const char* aob;
    int value;
    const int* skipAddresses;
    int skipCount;
};

auto CUserLocal__SendSkillCancelRequest = (void(__thiscall*)(CUserLocal*, int))0x0096D873;

auto CUserLocal__IsInvincible = (int(__thiscall*)(CUserLocal*))0x00959708;
int __fastcall CUserLocal__IsInvincible_Hook(CUserLocal* pthis, void* edx) {
    int inv = CUserLocal__IsInvincible(pthis);
    if (inv == 1) {
        if (!verdentActive && verdentVeilSL > 0) {
            if (verdentVeilSL < 10)
                dodgeCount = 1;
            else if (verdentVeilSL < 20)
                dodgeCount = 2;
            else
                dodgeCount = 3;
            verdentActive = true;
        }
        if (dodgeCount > 0) {
            dodgeCount--;
        }
        if (dodgeCount == 0) {
            verdentActive = false;
            CUserLocal__SendSkillCancelRequest(pthis, 3110022);
        }
    }
    return inv;
}

void ReplaceValueBatch(const ReplaceEntry* entries, int count, DWORD start, DWORD end) {
    std::vector<Pattern> patterns;
    patterns.reserve(count);
    size_t maxLen = 0;
    for (int k = 0; k < count; k++) {
        if (!entries[k].aob) {
            patterns.emplace_back();
            continue;
        }
        patterns.push_back(ParsePattern(entries[k].aob));
        if (patterns.back().bytes.size() > maxLen)
            maxLen = patterns.back().bytes.size();
    }
    if (maxLen == 0 || maxLen >= end - start)
        return;

    const DWORD last = end - (DWORD)maxLen;
    for (DWORD i = start; i < last; i++) {
        for (int k = 0; k < count; k++) {
            if (patterns[k].bytes.empty())
                continue;
            if (MatchPatternAt(patterns[k], i) &&
                    !IsSkipped(i, entries[k].skipAddresses, entries[k].skipCount)) {
                Patch4(i, entries[k].value);
            }
        }
    }
}

void FillBytes(const DWORD dwOriginAddress, const unsigned char ucValue, const int nCount) {
    DWORD dwOldProtect;
    if (!VirtualProtect((LPVOID)dwOriginAddress, nCount, PAGE_EXECUTE_READWRITE, &dwOldProtect))
        return;
    memset((void*)dwOriginAddress, ucValue, nCount);
    VirtualProtect((LPVOID)dwOriginAddress, nCount, dwOldProtect, &dwOldProtect);
}


void CodeCave(void* ptrCodeCave, const DWORD dwOriginAddress, const int nNOPCount) {
    if (nNOPCount)
        FillBytes(dwOriginAddress, 0x90, nNOPCount);
    Patch1(dwOriginAddress, 0xe9); // jmp instruction
    Patch4(dwOriginAddress + 1, (int)(((int)ptrCodeCave - (int)dwOriginAddress) - 5));
}

// Mob WATK/MATK buffstat (attack up/down debuff), mirror of applyMobDefenseStat. Same temp-stat
// table (MobStat::DecodeTemporary @0x78B0B1): PAD(#1) nOption @ +0x28, reason @ +0x2C; MAD(#3)
// nOption @ +0x48, reason @ +0x4C. nOption is a FLAT delta ADDED to the base attack (down debuff is
// negative -> we take less damage). reason==0 -> inactive, base passes through unchanged. ms->nPAD /
// ms->nMAD hold the template-copied base (no temp stat folded in), so add the delta here.
static int applyMobAttackStat(const MobStat* stat, int baseAtk, bool magic) {
    if (!stat || IsBadReadPtr(const_cast<MobStat*>(stat), sizeof(MobStat))) {
        return baseAtk;
    }
    const char* base = reinterpret_cast<const char*>(stat);
    int option = *reinterpret_cast<const int*>(base + (magic ? 0x48 : 0x28));
    int reason = *reinterpret_cast<const int*>(base + (magic ? 0x4C : 0x2C));
    if (reason == 0) {
        return baseAtk; // stat not active -> no modifier
    }
    int adjusted = baseAtk + option;
    return adjusted < 0 ? 0 : adjusted;
}

// Hack B tunables: the defense-down debuffs are NOT broadcast to the client (only the paired attack
// down is). We mirror the broadcast attack-down onto the matching defense -- MAD->MDEF for magic,
// PAD->PDEF for physical. The ratio scales the borrowed magnitude: 1.0 == apply the same flat delta
// the attack slot carries. Raise/lower to taste.
double mdefDebuffRatio = 1.0;
double pdefDebuffRatio = 1.0;

// Mob WDEF/MDEF buffstat (defense up/down debuff). Runtime MobStat lays each combat stat out as a
// 16-byte block [effective, nOption@+4, reason@+8, expire@+0xC] (verified by dumping a live mob):
//   PAD eff@0x24 opt@0x28 reason@0x2C    PDD eff@0x34 opt@0x38 reason@0x3C
//   MAD eff@0x44 opt@0x48 reason@0x4C    MDD eff@0x54 opt@0x58 reason@0x5C
// nOption is a FLAT delta ADDED to the template defense (defense-DOWN debuff is negative). reason==0
// -> the stat has no active temp modifier and the template defense passes through unchanged.
static double  applyMobDefenseStat(const MobStat* stat, double templateDef, bool magic) {
    if (!stat || IsBadReadPtr(const_cast<MobStat*>(stat), sizeof(MobStat))) {
        return templateDef;
    }
    const char* base = reinterpret_cast<const char*>(stat);
    int option = *reinterpret_cast<const int*>(base + (magic ? 0x58 : 0x38));
    int reason = *reinterpret_cast<const int*>(base + (magic ? 0x5C : 0x3C));
    // Hack B: the debuff skills lower BOTH attack and defense, but the server only broadcasts the
    // attack temp stat (MAD opt@0x48/reason@0x4C, PAD opt@0x28/reason@0x2C). When there's no native
    // defense temp stat, mirror the active attack-down onto the matching defense so client damage
    // reflects the (server-side) defense reduction. MAD->MDEF for magic, PAD->PDEF for physical.
    if (reason == 0) {
        int atkOption = *reinterpret_cast<const int*>(base + (magic ? 0x48 : 0x28));
        int atkReason = *reinterpret_cast<const int*>(base + (magic ? 0x4C : 0x2C));
        if (atkReason != 0) {
            option = (int)(atkOption * (magic ? mdefDebuffRatio : pdefDebuffRatio));
            reason = atkReason;
        }
    }
    if (reason == 0) {
        return templateDef; // stat not active -> no modifier
    }
    double adjusted = templateDef + option;
    return adjusted < 0.0 ? 0.0 : adjusted;
}

auto MobACC = (int(__stdcall*)(MobStat*, void*, BasicStat*, SecondaryStat*, unsigned int))0x0079286E;
auto GetPDD = (int(__thiscall*)(SecondaryStat*, void*))0x0077E067;
auto GetMDD = (int(__thiscall*)(SecondaryStat*, void*))0x0077E141;

auto MobPDamage = (int(__thiscall*)(void*, MobStat*, void*, BasicStat*, SecondaryStat*, int, unsigned int, int*))0x0079309F;
int __fastcall MobPDamage_Hook(void* calc, void* edx, MobStat* ms, void* cd, BasicStat* bs, SecondaryStat* ss, int psd, unsigned int misschance, int* mesoGuard) {
    if (MobACC(ms, cd, bs, ss, misschance)) {
        return 0; // attack missed
    }
    int invincible = ss->m_invincible.Fuse();
    int mesoguard = ss->m_mesoGuard.Fuse();
    // psd is NOT a plain int: it's the MOBATTACKINFO* for the attack that hit us, and NULL for
    // touch damage. Verified in the original @ 0x79309F:
    //   base PAD = [psd+0x18] when psd != NULL (the attack node's own PADamage),
    //   base PAD = ms->nPAD (+0x24, template PAD) when psd == NULL (touch),
    // plus the PAD temp-stat option (+0x28) either way. Attacks can carry a custom PADamage in
    // their WZ attack node, so a skill hit must read the attack info, not the template PAD.
    // Fall back to the template PAD when the attack node has no/zero PADamage so custom attacks
    // without the node keep dealing touch-level damage instead of ~0.
    int basePAD = ms->nPAD;
    if (psd && !IsBadReadPtr(reinterpret_cast<void*>(psd), 0x1C)) {
        int attackPAD = *reinterpret_cast<int*>(psd + 0x18);
        if (attackPAD > 0) {
            basePAD = attackPAD;
        }
    }
    int mobPD = applyMobAttackStat(ms, basePAD, false); // fold WATK up/down debuff
    double baseDamage = mobPD;
    int level = bs->nLevel.Fuse();
    int mobLevel = ms->nLevel;
    int str = bs->nSTR.Fuse() / 10;
    int dex = bs->nDEX.Fuse() / 20;
    int luk = bs->nLUK.Fuse() / 20;
    int _int = bs->nINT.Fuse() / 40;
    double stonedef = stone * 0.015;
    double PDD = GetPDD(ss, cd) + str + dex + luk + _int;
    double reduciton = (PDD / (500 + PDD));
    double leveldiff = 1 + (level - mobLevel) * 0.005;
    int preSkillMitigationDamage = (baseDamage - ((baseDamage * reduciton) * leveldiff));
    if (stone > 0.0) {
        preSkillMitigationDamage -= preSkillMitigationDamage * stonedef;
    }
    if (invincible > 0) {
        preSkillMitigationDamage *= (1.0 - (invincible * 0.01));
    }
    if (mesoguard > 0) {
        preSkillMitigationDamage *= (1.0 - (mesoguard * 0.01));
    }
    if (preSkillMitigationDamage <= 0) {
        return 1;
    }
    return preSkillMitigationDamage;
}

auto magicACC = (int(__stdcall*)(MobStat*, void*, BasicStat*, SecondaryStat*, unsigned int))0x007929CA;

auto MobMDamage = (int(__thiscall*)(void*, MobStat*, void*, BasicStat*, SecondaryStat*, unsigned int, int*))0x0079345E;
int __fastcall MobMDamage_Hook(void* calc, void* edx, MobStat* ms, void* cd, BasicStat* bs, SecondaryStat* ss, unsigned int misschance, int* mesoGuard) {
    if (magicACC(ms, cd, bs, ss, misschance)) {
        return 0; // attack missed
    }
    int mesoguard = ss->m_mesoGuard.Fuse();
    double mesoGuardReduction = -mesoguard / 100;
    double reduction = 1.0 + mesoGuardReduction;
    int mobPD = applyMobAttackStat(ms, ms->nMAD, true); // fold MATK up/down debuff
    double baseDamage = mobPD;
    int level = bs->nLevel.Fuse();
    int mobLevel = ms->nLevel;
    int str = bs->nSTR.Fuse() / 40;
    int dex = bs->nDEX.Fuse() / 40;
    int luk = bs->nLUK.Fuse() / 20;
    int _int = bs->nINT.Fuse() / 10;
    double stonedef = stone * 0.015;
    double PDD = GetMDD(ss, cd) + str + dex + luk + _int;
    double reduciton = (PDD / (500 + PDD));
    double leveldiff = 1 + (level - mobLevel) * 0.005;
    int preSkillMitigationDamage = (baseDamage - ((baseDamage * reduciton) * leveldiff));
    if (stonedef > 0.0) {
        preSkillMitigationDamage -= preSkillMitigationDamage * stonedef;
    }
    if (mesoguard > 0) {
        preSkillMitigationDamage *= (1.0 - (mesoguard * 0.01));
    }
    if (preSkillMitigationDamage <= 0) {
        return 1;
    }
    return preSkillMitigationDamage;
}


inline int skipArray[]{
    0x0078E94F + 2,
    0x007661BF + 1,
    0x00791AAA,
    0x0078E957 + 2,
};


auto isCommandSkill = reinterpret_cast<int(__cdecl*)(int)>(0x00764721);
int isCommandSkill_hook(int a1) {
    return 0;
}

auto requiredComboCount = reinterpret_cast<int(__cdecl*)(int)>(0x00766986);
int requiredComboCount_hook(int skillID) {
    if (skillID == 5411022) {
        return 100;
    }
    if (skillID == 5511002 || skillID == 5511015) {
        return 100;
    }
    if (skillID == 5511014) {
        return 300;
    }
    return 0;
}

void comboStuff() {
    int job = CWvsContext::GetInstance()->get_m_basicStat().nJob.Fuse();
    int comboAbility = 0;
    if (job == 541) {
        comboAbility = 5410000;
    } else {
        comboAbility = 5510000;
    }
    Patch4(0x0077dfc4 + 2, comboAbility);
    Patch4(0x0077e1b5 + 2, comboAbility);
    Patch4(0x0077e0cf + 2, comboAbility);
    // PatchNop(0x00668BFE, 2);
    PatchNop(0x00668C02, 2);
    PatchNop(0x00668C07, 2);
    PatchNop(0x00668C0D, 2);
    Patch4(0x0094bdc8 + 1, 7000);
    Patch4(0x00960708 + 1, 7000);
    Patch4(0x0096095B + 1, 7000);


    ATTACH_HOOK(isCommandSkill, isCommandSkill_hook);
    ATTACH_HOOK(requiredComboCount, requiredComboCount_hook);
}

auto sparkThing = (int(__cdecl*)(int))0x7668B7;
int(__cdecl sparkThingHook)(int skillId) {
    if (skillId == 1201016 || skillId == 4111010 || skillId == 3411006 || skillId == 4121017 || skillId == 4421015 || skillId == 5521003 || skillId == 5511017) {
        return 1;
    }
    return sparkThing(skillId);
}

void skillHacks() {

    Patch4(0x00952360 + 1, 1101016); // il amplification
    Patch4(0x0094B4EC + 1, 1411003); // switch addy
    Patch4(0x00765A61 + 1, 121);     // skill root check
    Patch1(0x008C4077 + 2, 0x0);
    Patch1(0x008C407D, 0x0);
    Patch1(0x007AFDE1, 0x0);
    Patch1(0x0095CE05 + 2, 0x97);    // achilles
    Patch4(0x0095CE32 + 1, 1510005); // achilles
    Patch4(0x007657EC + 1, 2410001); // il amplification
    Patch4(0x007657CF + 1, 241);     // il amplification
    Patch4(0x007657EC + 1, 2410001); // il amplification
    Patch4(0x00765815 + 1, 251);     // paladin amplification
    Patch4(0x00765832 + 1, 2510000); // paladin amplification
                                     // ReplaceValue("8F A1 12 00", 1511003, skipArray, 4); // rush
    Patch1(0x009584F6 + 2, 0x51);    // eavsion boost skill WA
    Patch1(0x00958523 + 2, 0x52);    // eavsion boost skill WA
    Patch4(0x0078E1EB + 1, 1110010);

    // restore skill id 5221006 -> 5211016 at all 14 client refs
    Patch4(0x007665F1 + 4, 5211016);
    Patch4(0x007B2875 + 3, 5211016);
    Patch4(0x007B4839 + 3, 5211016);
    Patch4(0x00934481 + 2, 5211016);
    Patch4(0x00936B20 + 1, 5211016);
    Patch4(0x00936C4C + 1, 5211016);
    Patch4(0x00967173 + 1, 5211016);
    Patch4(0x009682CC + 1, 5211016);
    Patch4(0x00969E6C + 1, 5211016);
    Patch4(0x0096A2FA + 1, 5211016);
    Patch4(0x0096D8D0 + 2, 5211016);
    Patch4(0x00A123F4 + 1, 5211016);
    Patch4(0x00A12479 + 1, 5211016);
    Patch4(0x00A20384 + 1, 5211016);
}

bool isSkillIDMatched(int nSkillID) {
    static const int skillIDs[] = {

        // ===== Warrior =====
        1001006,
        1001007,

        // ===== Knight =====
        1101016,
        1101015,
        1401007,
        1401015,
        1401016,
        1201016,
        1201012,
        1211000,
        1501016,
        1501012,
        1521003,
        1521011,

        // ===== Crusader =====
        1111009,
        1111015,
        1121002,
        1121012,

        // ===== Spearman =====
        1211012,
        1211013,
        1211014,
        1211000,
        1221011,
        1221016,
        1221021,
        1221052,
        1121021,

        // ===== Duelist =====
        1411003,
        1411005,
        1411006,
        1411008,
        1421009,
        1421004,
        1421003,
        

        // ====== Crusher =====
        1201017,
        1210007,
        1201018,

        // ===== Barbarian =====
        1511008,
        1511003,
        1511007,
        1511006,
        1511009,
        1521002,
        1521100,
        1521229,

        // ===== Magician =====
        2001010,

        // ===== Elementalist =====
        2101008,
        2101007,
        2401005,
        2401004,
        2401008,
        2401007,

        // ===== Cleric =====
        2201010,
        2201011,
        2201012,
        2201013,
        2501010,
        2501011,
        2501012,
        2501013,

        // ===== FP Mage =====
        2111011,
        2111010,
        2111012,
        2111013,
        2121005,
        2121026,
        2121027,
        2121016,

        // ===== Priest =====
        2211011,
        2211012,
        2211014,
        2221015,
        2211016,
        2211013,
        2221000,
        2221015,
        2221018,
        2221021,

        // ===== IL Mage =====
        2411010,
        2411011,
        2411012,
        2411013,
        2411023,
        2421005,
        2421007,
        2421014,
        2421006,

        // ===== Holy Knight =====
        2511006,
        2511004,
        2511001,
        2511011,
        2521009,
        2521014,
        2521023,
        2521032,

        // ===== Priest =====
        2211004,
        2211015,

        // ===== Bowman =====z
        3001013,

        // ===== Hunter =====
        3101007,
        3101012,
        3111015,
        3111018,
        3401005,
        3401007,
        3401012,

        // ===== Crossbowman =====
        3201016,
        3201006,
        3501005,
        3501003,
        3501016,

        // ===== Wind Archer =====
        3411006,
        3411007,
        3411005,
        3411010,
        3421005,
        3421008,

        // ===== Sniper =====
        3511003,
        3511008,
        3511004,
        3511019,
        3511002,
        3521007,
        3521008,
        3521012,
        3521052,

        3211014,
        3211016,
        3211015,
        3221007,
        3221018,

        3601000,
        3601001,
        3601002,
        3601003,
        3601004,
        3601005,
        3601006,
        3601007,
        3601009,
        3601010,
        3601011,
        3601014,
        3601015,
        3601017,



        3601021,
        3601022,
        3601023,


        // ===== Thief =====
        4001004,

        // ===== Assassin =====
        4101009,
        4101008,
        4101006,
        4401009,
        4401008,
        4401006,

        // ===== Hermit =====
        4111020,
        4111021,
        4111017,
        4121019,
        4121017,
        4421015,

        // ===== Bandit =====
        4201014,
        4201016,
        4201006,
        4501005,
        4501014,
        4501006,

        // ===== Chief Bandit =====
        4211011,
        4211015,
        4211001,
        4211016,

        // ===== Ninja =====
        4411006,
        4411009,
        4411019,
        4411004,

        // ===== Bandit 3rd =====
        4511006,
        4511016,
        4511013,
        4511003,
        4511007,
        4511001,
        4521011,
        4521012,
        4521013,

        5211012,
        5211016,
        5111007,
        5201007,

        // ===== Gunslinger 2nd =====
        5501001,

        // ===== Marauder 3rd =====
        5111013,
        5111016,
        5111014,
        5121008,
        5121011,
        5121013,
        // 5501006, 5501002
        // ===== Brawler 2nd =====
        5401002,
        5401003,
        5101009,
        5101010,
        5211017,

        // ===== Comboist ====
        5411002,
        5411021,
        5411022,
        5411020,
        5411026,
        5421009,
        5421007,

        // ===== Summoner ====
        5511015,
        5511002,
        5511014,
        5511017,
        5511006,
        5521016,
        5521009,
        5521003,
    };

    return std::find(std::begin(skillIDs), std::end(skillIDs), nSkillID) != std::end(skillIDs);
}


auto CInPacket_Decode4Original = reinterpret_cast<int(__thiscall*)(CInPacket*)>(0x00406629);
auto CInPacket_Decode2Original = reinterpret_cast<short(__thiscall*)(CInPacket*)>(0x0042470C);
auto CInPacket_Decode1Original = reinterpret_cast<char(__thiscall*)(CInPacket*)>(0x004065F3);

void CInPacket_Decode2(CInPacket* pPacket, void* edx) {

    CInPacket_Decode2Original(pPacket);
}

auto dashOnDash = (int(__thiscall*)(void*, int))0x74c73a;
int __fastcall dashOnDash_hook(void* pThis, void* edx, int nDash) {
    return 0;
}

auto isDashingSkill = (int(__thiscall*)(void*))0x0095F900;
int __fastcall isDashingHook(void* pthis, void* edx) {
    return 0;
}

auto pGetSkillLevel = (int(__thiscall*)(int, void*, int, int))0x007616F6;

// ZMap<long,long,long>::GetAt(this, &key, &out) -> non-null on hit. Used to read skill maps raw.
auto pZMapJJJ_GetAt = (int*(__thiscall*)(void* self, const int* key, int* out))0x004E86A0;

// Learned skill level read RAW, bypassing CSkillInfo::GetSkillLevel's master-level clamp. That
// clamp (sub_4FB30C) caps the result at the number of level/N nodes defined in Skill.wz, so a skill
// with only 20 WZ level entries reads as 20 even when 40 points are learned. For passives whose
// effect we compute ourselves in C++ (the poison bonus) we want the true point count. Mirrors the
// tail of GetSkillLevel @ 0x7617D0: base map @ CharacterData+0x467, level-bonus map @ +0x47F.
int GetRawSkillLevel(void* charData, int skillID) {
    if (!charData) {
        return 0;
    }
    int key = skillID;
    int level = 0;
    if (!pZMapJJJ_GetAt(reinterpret_cast<char*>(charData) + 0x467, &key, &level)) {
        return 0;
    }
    int bonus = 0;
    if (pZMapJJJ_GetAt(reinterpret_cast<char*>(charData) + 0x47F, &key, &bonus)) {
        level += bonus;
    }
    return level;
}
bool hitonce = false;
int shadowSL = 0;

// Records which mastery skill the level came from, so the Skill.wz `mastery` for it can be read.
static int trackMastery(int nLevel, int nSkillID) {
    masterySkillID = nSkillID;
    return nLevel;
}
int(__fastcall GetSkillLevel)(int _this, void* edx, void* charData, int skillID, int skillEntry) {
    int i = skillID;
    int jobID = CWvsContext::GetInstance()->get_m_basicStat().nJob.Fuse();
    if (i) {
        pGetSkillLevel(_this, charData, i, skillEntry);
        if (jobID == 300 || jobID == 310 || jobID == 342 || jobID == 312 || jobID == 311 || jobID == 341) {
            mastery = trackMastery(pGetSkillLevel(_this, charData, 3100000, skillEntry), 3100000);
            critSkillID = 3000001;
        }
        if (jobID == 3120005) {
            mastery = trackMastery(pGetSkillLevel(_this, charData, 3120005, skillEntry), 3120005);
        }
        if (jobID == 320 || jobID == 321 || jobID == 322 || jobID == 351 || jobID == 352) {
            mastery = trackMastery(pGetSkillLevel(_this, charData, 3200000, skillEntry), 3200000);
            critSkillID = 3000001;
        }
        if (jobID == 3220004) {
            mastery  = trackMastery(pGetSkillLevel(_this, charData, 3220004, skillEntry), 3220004);
        }
        if (jobID == 410 || jobID == 411 || jobID == 412 || jobID == 441 || jobID == 442) {
            mastery = trackMastery(pGetSkillLevel(_this, charData, 4100000, skillEntry), 4100000);
            critSkillID = 4100001;
            if (hitonce && pGetSkillLevel(_this, charData, 4110020, skillEntry) > 10) {
                hitonce = false;
            }
            if (pGetSkillLevel(_this, charData, 4110020, skillEntry) > 10 && !hitonce) {
                shadowSL = pGetSkillLevel(_this, charData, 4110020, skillEntry);
                iframes = 1500 + pGetSkillLevel(_this, charData, 4110020, skillEntry) * 50;
                hitonce = true;
            }
            else if (!hitonce) {
                iframes = 1500 + pGetSkillLevel(_this, charData, 4110020, skillEntry) * 50;
            }
        }
        if (jobID == 412) {
            mastery  = trackMastery(pGetSkillLevel(_this, charData, 4120000, skillEntry), 4120000);
        }
        if (jobID == 420 || jobID == 421 || jobID == 422 || jobID == 451 || jobID == 452) {
            mastery = trackMastery(pGetSkillLevel(_this, charData, 4200000, skillEntry), 4200000);
            critSkillID = 4220006;
        }
        if (jobID == 422) {
            mastery = trackMastery(pGetSkillLevel(_this, charData, 4220006, skillEntry), 4220006);
        }
        if (jobID == 110 || jobID == 111 || jobID == 112 || jobID == 141 || jobID == 142) {
            mastery = trackMastery(pGetSkillLevel(_this, charData, 1100000, skillEntry), 1100000);
        }
        if (jobID == 142) {
            mastery = trackMastery(pGetSkillLevel(_this, charData, 1420000, skillEntry), 1420000);
        }
        if (jobID == 120 || jobID == 121 || jobID == 122 || jobID == 151 || jobID == 152) {
            mastery = trackMastery(pGetSkillLevel(_this, charData, 1200000, skillEntry), 1200000);
            critSkillID = 1210015;
        }
        if (jobID == 122) {
            mastery = trackMastery(pGetSkillLevel(_this, charData, 1220015, skillEntry), 1220015);
        }
        if (jobID == 210 || jobID == 211 || jobID == 212 || jobID == 241 || jobID == 242) {
            mastery = trackMastery(pGetSkillLevel(_this, charData, 2100001, skillEntry), 2100001);
        }
        if (jobID == 220 || jobID == 221 || jobID == 222 || jobID == 251 || jobID == 252) {
            mastery = trackMastery(pGetSkillLevel(_this, charData, 2200001, skillEntry), 2200001);
        }
        if (jobID == 252) {
            mastery = trackMastery(pGetSkillLevel(_this, charData, 2520002, skillEntry), 2520002);
        }

        if (jobID == 520 || jobID == 521 || jobID == 551 || jobID == 552) {
            mastery = trackMastery(pGetSkillLevel(_this, charData, 5200000, skillEntry), 5200000);
            sairIgnore = pGetSkillLevel(_this, charData, 5220013, skillEntry);
        }
        if (jobID == 522) {
            mastery  = trackMastery(pGetSkillLevel(_this, charData, 5220013, skillEntry), 5220013);
        }
        if (jobID == 510 || jobID == 511 || jobID == 512 || jobID == 541 || jobID == 542) {
            mastery = trackMastery(pGetSkillLevel(_this, charData, 5100001, skillEntry), 5100001);
        }
        if (jobID == 311 || jobID == 312) {
            PassiveSpeed = pGetSkillLevel(_this, charData, 3110000, skillEntry);
        }
        if (jobID == 341 || jobID == 342) {
            PassiveSpeed = pGetSkillLevel(_this, charData, 3410000, skillEntry);
        }
        if ((int)_ReturnAddress() == 0x0095855D) {
            return (pGetSkillLevel(_this, charData, 3410002, skillEntry));
        }
        tb = pGetSkillLevel(_this, charData, 15110000, skillEntry);
        if (pGetSkillLevel(_this, charData, critSkillID, skillEntry) > 0) {
            Patch4(0x007650DB + 1, critSkillID);
        }
        if (jobID == 0 || jobID == 100 || jobID == 200 || jobID == 300 || jobID == 400 || jobID == 500) {
            mastery = 0;
            masterySkillID = 0;
        }
        masteryValue = mastery > 0 ? GetSkillMasteryFromWz(masterySkillID, mastery) : 0;
    }
    mesos = pGetSkillLevel(_this, charData, 4511006, skillEntry);
    comboAbility = pGetSkillLevel(_this, charData, 5410000, skillEntry);
    verdentVeilSL = pGetSkillLevel(_this, charData, 3110022, skillEntry);
    asLevel = pGetSkillLevel(_this, charData, 1411005, skillEntry);
    rsLevel = pGetSkillLevel(_this, charData, 1411006, skillEntry);
    bsLevel = pGetSkillLevel(_this, charData, 4211015, skillEntry);
    backflip = pGetSkillLevel(_this, charData, 5101009, skillEntry);
    slam = pGetSkillLevel(_this, charData, 2511009, skillEntry);
    sharpenlevel = pGetSkillLevel(_this, charData, 3200013, skillEntry);
    poisonBonusLevel = GetRawSkillLevel(charData, POISON_PASSIVE_SKILLID); // raw: WZ caps at 20
    duelistShred = pGetSkillLevel(_this, charData, 1410012, skillEntry);
    rangerShred = pGetSkillLevel(_this, charData, 3110000, skillEntry);
    sniperShred = pGetSkillLevel(_this, charData, 3210018, skillEntry);
    barbShred = pGetSkillLevel(_this, charData, 1510008, skillEntry);
    galeShot = pGetSkillLevel(_this, charData, 3411007, skillEntry);
    masterSkies = pGetSkillLevel(_this, charData, 3410000, skillEntry);
    hermitBoss = pGetSkillLevel(_this, charData, 4110031, skillEntry);
    stone = pGetSkillLevel(_this, charData, 1510005, skillEntry);
    if ((int)_ReturnAddress() == 0x0095855D) {
        return pGetSkillLevel(_this, charData, 3410002, skillEntry);
    }
    return pGetSkillLevel(_this, charData, i, skillEntry);
}

auto CUIStatusBar__ChatLogAdd = (void*(__thiscall*)(int, const char*, int, int, int, void*))0x008DB070;

bool isCorrectWeapon(int nSkillID) {
    if (nSkillID >= 4 && nSkillID <= 999999) {
        if (get_weapon_type() >= 30) {
            return true;
        }
    }
    if (nSkillID >= 1001000 && nSkillID <= 1001007) {
        if (get_weapon_type() <= 44) {
            return true;
        }
    }
    if ((nSkillID >= 1101000 && nSkillID <= 1200000) || (nSkillID >= 1401000 && nSkillID <= 1501000)) {
        if (get_weapon_type() <= 33) {
            return true;
        }
    }
    if (nSkillID >= 1201000 && nSkillID < 1211000) {
        if (get_weapon_type() - 40 <= 4 && get_weapon_type() >= 40) {
            return true;
        }
    }
    if (nSkillID >= 1211000 && nSkillID < 1301000) {
        if (get_weapon_type() == 43 || get_weapon_type() == 44) {
            return true;
        }
    }
    if ((nSkillID >= 1511000 && nSkillID < 2000000) || nSkillID == 3601009) {
        if (get_weapon_type() == 40 || get_weapon_type() == 41 || get_weapon_type() == 42) {
            return true;
        }
    }
    if (nSkillID >= 2001000 && nSkillID < 2510000) {
        if (get_weapon_type() == 37 || get_weapon_type() == 38 || get_weapon_type() == 32) {
            return true;
        }
    }
    if ((nSkillID >= 2201000 && nSkillID < 2300000) || (nSkillID >= 2501000 && nSkillID < 3000000)) {
        if (get_weapon_type() == 32) {
            return true;
        }
    }
    if (nSkillID >= 3001000 && nSkillID < 3100000) {
        if (get_weapon_type() >= 45 && get_weapon_type() < 47) {
            return true;
        }
    }
    if ((nSkillID >= 3101000 && nSkillID < 3200000) || (nSkillID >= 3401000 && nSkillID < 3500000)) {
        if (get_weapon_type() == 45) {
            return true;
        }
    }
    if ((nSkillID >= 3201000 && nSkillID < 3300000) || (nSkillID >= 3501000 && nSkillID < 4000000)) {
        if (get_weapon_type() == 46) {
            return true;
        }
    }
    if (nSkillID >= 4001000 && nSkillID < 4100000) {
        if (get_weapon_type() == 47 || get_weapon_type() == 33) {
            return true;
        }
    }
    if ((nSkillID >= 4101000 && nSkillID < 4200000) || (nSkillID >= 4401000 && nSkillID < 4500000)) {
        if (get_weapon_type() == 47) {
            return true;
        }
    }
    if ((nSkillID >= 4201000 && nSkillID < 4300000) || (nSkillID >= 4501000 && nSkillID < 4600000)) {
        if (get_weapon_type() == 33) {
            return true;
        }
    }
    if ((nSkillID >= 5000000 && nSkillID < 5100000)) {
        if (get_weapon_type() == 48 || get_weapon_type() == 49) {
            return true;
        }
    }
    if ((nSkillID >= 5101000 && nSkillID < 5200000) || (nSkillID >= 5401000 && nSkillID < 5500000) || nSkillID == 15101006) {
        if (get_weapon_type() == 48) {
            return true;
        }
    }
    if ((nSkillID >= 5200000 && nSkillID < 5300000) || (nSkillID >= 5500000 && nSkillID < 5600000) || nSkillID == 3601010) {
        if (get_weapon_type() == 49) {
            return true;
        }
    }
    if (nSkillID == 4101008 || nSkillID == 14101006) {
        if (get_weapon_type() == 47) {
            return true;
        }
    }
    if (nSkillID == 3411004 || nSkillID == 5201005) {
        if (get_weapon_type() == 45) {
            return true;
        }
    }
    if (nSkillID == 2211013 || nSkillID == 2311005) {
        if (get_weapon_type() == 37 || get_weapon_type() == 38 || get_weapon_type() == 32) {
            return true;
        }
    }
    if (nSkillID >= 3601001 && nSkillID <= 3601007) {
        return true;
    }
    if (nSkillID > 10000000) {
        return true;
    }
    return false;
}

void doSpearPA() {
    WriteDouble(0x00BED58C, tbw);
    WriteDouble(0x00BED90C, taxe);
    switch (get_weapon_type()) {
    case 30:
        break;
    case 31:
        Patch4(0x0078F60A + 2, 0x00AFE850);
        Patch4(0x0078F6B0 + 2, 0x00AFE850);
        Patch4(0x008C2DFD + 2, 0x00AFE850);
        Patch4(0x008C2E46 + 2, 0x00AFE850);
        break;
    case 32:
        Patch4(0x0078F60A + 2, 0x00AFE850);
        Patch4(0x0078F6B0 + 2, 0x00AFE850);
        Patch4(0x008C2DFD + 2, 0x00AFE850);
        Patch4(0x008C2E46 + 2, 0x00AFE850);
        break;
    case 33:
        Patch4(0x0078F84F + 2, ohsword);
        Patch4(0x008C2F86 + 2, ohsword);
        break;
    case 40:
        break;
    case 41:
        Patch4(0x0078F1A4 + 2, taxeaddr);
        Patch4(0x0078F24A + 2, taxeaddr);
        Patch4(0x008C2C56 + 2, taxeaddr);
        Patch4(0x008C2C9F + 2, taxeaddr);
        break;
    case 42:
        Patch4(0x0078F1A4 + 2, newformulaaddr);
        Patch4(0x0078F24A + 2, newformulaaddr);
        Patch4(0x008C2C56 + 2, newformulaaddr);
        Patch4(0x008C2C9F + 2, newformulaaddr);
        break;
    case 43:
        Patch4(0x0078F3FB + 2, 0x00AFE850);
        Patch4(0x0078F4A8 + 2, 0x00AFE850);
        Patch4(0x008C2CE3 + 2, 0x00AFE850);
        Patch4(0x008C2D2C + 2, 0x00AFE850);
        break;
    case 44:
        Patch4(0x0078F3FB + 2, ohsword);
        Patch4(0x0078F4A8 + 2, ohsword);
        Patch4(0x008C2CE3 + 2, ohsword);
        Patch4(0x008C2D2C + 2, ohsword);
        break;
    case 45:
        Patch4(0x0078F042 + 2, ohsword);
        Patch4(0x008C2AEC + 2, ohsword);
        Patch4(0x008C2B35 + 2, ohsword);
        break;
    case 46:
        Patch4(0x0078F0EF + 2, newformulaaddr);
        Patch4(0x008C2BC9 + 2, newformulaaddr);
        Patch4(0x008C2C12 + 2, newformulaaddr);
        break;
    case 47:
        Patch4(0x0078FABD + 2, ohsword);
        Patch4(0x008C3032 + 2, ohsword);
        Patch4(0x008C309D + 2, ohsword);
        break;
    case 48:
        Patch4(0x0078FD81 + 2, ohsword);
        Patch4(0x008C320C + 2, ohsword);
        Patch4(0x008C3255 + 2, ohsword);
        break;
    case 49:
        Patch4(0x0078FE3E + 2, ohsword);
        Patch4(0x008C32E2 + 2, ohsword);
        Patch4(0x008C3299 + 2, ohsword);
        break;
    default:
        break;
    }
}

typedef const char*(__cdecl* get_job_name_t)(int jobId);
static auto get_job_name_hook = reinterpret_cast<get_job_name_t>(0x004A77EF);

const char* __cdecl get_job_name(int nJob) {
    switch (nJob) {
    case 0:
        return "Beginner";
    case 100:
        return "Warrior";
    case 200:
        return "Wizard";
    case 300:
        return "Archer";
    case 400:
        return "Rogue";
    case 500:
        return "Pirate";
    case 110:
        return "Knight";
    case 111:
        return "Crusader";
    case 112:
        return "Hero";
    case 140:
        return "Knight";
    case 141:
        return "Duelist";
    case 142:
        return "Dervish";
    case 120:
        return "Crusher";
    case 121:
        return "Lancer";
    case 122:
        return "Dragon Knight";
    case 150:
        return "Crusher";
    case 151:
        return "Barbarian";
    case 152:
        return "Berzerker";
    case 210:
        return "Elementalist";
    case 211:
        return "F/P Magician";
    case 212:
        return "F/P Archmage";
    case 220:
        return "Cleric";
    case 221:
        return "Priest";
    case 222:
        return "Bishop";
    case 240:
        return "Elementalist";
    case 241:
        return "I/L Magician";
    case 242:
        return "I/L Archmage";
    case 250:
        return "Cleric";
    case 251:
        return "Holy Knight";
    case 252:
        return "Paladin";
    case 310:
        return "Hunter";
    case 311:
        return "Ranger";
    case 312:
        return "Bowmaster";
    case 320:
        return "Crossbowman";
    case 321:
        return "Sniper";
    case 322:
        return "Marksman";
    case 340:
        return "Hunter";
    case 341:
        return "Wind Archer";
    case 342:
        return "Storm Strider";
    case 350:
        return "Crossbowman";
    case 351:
        return "Boltslinger";
    case 352:
        return "Epoch";
    case 410:
        return "Assassin";
    case 411:
        return "Hermit";
    case 412:
        return "Nightlord";
    case 420:
        return "Bandit";
    case 421:
        return "Chief Bandit";
    case 422:
        return "Shadower";
    case 440:
        return "Assassin";
    case 441:
        return "Ninja";
    case 442:
        return "Viper";
    case 450:
        return "Bandit";
    case 451:
        return "Smuggler";
    case 452:
        return "Tycoon";
    case 510:
        return "Brawler";
    case 511:
        return "Marauder";
    case 512:
        return "Buccaneer";
    case 520:
        return "Gunslinger";
    case 521:
        return "Outlaw";
    case 522:
        return "Corsair";
    case 540:
        return "Brawler";
    case 541:
        return "Striker";
    case 542:
        return "Dreadnought";
    case 550:
        return "Gunslinger";
    case 551:
        return "Captain";
    case 552:
        return "Admiral";
    default:
        return get_job_name_hook(nJob);
    }
}

auto isDarkSight_hook = (void*(__thiscall*)(void*))0x009581A9;

void*(__fastcall isDarkSight)(void* _this) {
    printf("0x%08X\n", (DWORD)_ReturnAddress());
    return isDarkSight_hook(_this);
}

auto AddRush = (void(__thiscall*)(CUserLocal*, int, int, int))0x009535C1;

void(__fastcall AddRush_Hook)(CUserLocal* _this, void* edx, int type, int vx, int delay) {
    int vx2 = vx;
    bool faceLeft = _this->m_isLeft % 2 == 0;
    vx2 *= faceLeft ? -1 : 1;
    printf("vx: %d", vx2);
    return AddRush(_this, 0, vx2, delay);
}

typedef void(__fastcall* SetFromWhenDoom_t)(MobStat* pThis, void* edx, MobTemplate* pTemplate);

typedef MobTemplate*(__cdecl* GetMobTemplate_t)(int templateId);
static auto GetMobTemplate = reinterpret_cast<GetMobTemplate_t>(0x0067CD28);

static std::unordered_map<MobTemplate*, int> g_TemplateIdByPtr;

MobTemplate* __cdecl GetMobTemplate_Hook(int templateId) {
    MobTemplate* p = GetMobTemplate(templateId);
    if (p) {
        g_TemplateIdByPtr[p] = templateId;
    }
    return p;
}

auto SetFromWhenDoom = (void(__thiscall*)(MobStat*, MobTemplate*))0x00789EFD;
void __fastcall SetFromWhenDoom_Hook(MobStat* pThis, void* edx, MobTemplate* pTemplate) {
    pTemplate = GetMobTemplate(100100);
    memcpy(pThis->aDamagedElemAttr, pTemplate->aDamagedElemAttr, sizeof(pThis->aDamagedElemAttr)); // might be interesting to change this later
    pThis->nPAD = 0;
    pThis->nMAD = 0;
    pThis->nACC = 0;
    pThis->nPDR = 0;
    pThis->nMDR = 0;
    pThis->nEVA = 0;
    pThis->nACC = 0;
    pThis->nSpeed = 0;
}


auto onDoomed = (void(__thiscall*)(Mob*, int))0x0066D6D4;

void __fastcall OnDoomed_Hook(Mob* pThis, void* edx, int bDoom) {
    int templateId = 0;
    if (bDoom) {
        auto it = g_TemplateIdByPtr.find(pThis->m_pTemplate);
        templateId = (it != g_TemplateIdByPtr.end()) ? it->second : 0;
        if (templateId != 0) {
            Patch4(0x0066D722 + 1, templateId);
        }
    }
    onDoomed(pThis, bDoom);
};

double owo = 0.0;

auto mesoFormulaHook = (int(__thiscall*)(void*, void*, BasicStat*, SecondaryStat*, MobStat*, int*, unsigned int, int*))0x00791FBC;
int __fastcall MesoFormula(void* pThis, PVOID edx, void* cd, BasicStat* bs, SecondaryStat* ss, MobStat* ms, int* anMoneyAmount,
        unsigned int dwDropFlag, int* aDamage) {
    long double ratio;
    int nAttackCount;
    int i;


    nAttackCount = 0;
    for (i = 0; i < 30; ++i) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<double> dist(.1 + masteryValue * 0.01, 1.00);
        owo = dist(gen);
        if (((1 << i) & dwDropFlag) != 0) {
            ratio = ((4.0 * bs->nLUK.Fuse() + bs->nSTR.Fuse() + bs->nDEX.Fuse()) * pad / 100) * owo;
            __int64 damage = (__int64)(ratio * (0.6 + (0.02 * mesos)));
            *aDamage = damage;
            ++nAttackCount;
            ++aDamage;
        }
        ++anMoneyAmount;
    }
    return nAttackCount;
};

__declspec(naked) void AdjustAccuracyCalc() {
    __asm {
        push dword ptr[ebx + 0x2C]
        lea eax, [ebx + 0x24]
        push eax
        call[ZtlBussy]
        add esp, 8
        mov[currStr], eax
        fild[currStr]
        fmul strMultiplier
        faddp st(2), st
        jmp dword ptr[dwAccuracyCalcRetn]
    }
}

auto SetDamaged_Hook = (void(__thiscall*)(void*, int, int, int, unsigned __int16, int*, int, int, int, int, int))0x009581A9;

void(__fastcall SetDamaged)(void* _this, void* edx,
        int nDamage,
        int vx,
        int vy,
        unsigned __int16 dwObstacleData,
        int* pMob,
        int nAttackIdx,
        int nDir,
        int nPowerGuard,
        int bCheckHitRemain,
        int bSendPacket) {

    if (!jobPatchesApplied) {
        int jobID = CWvsContext::GetInstance()->get_m_basicStat().nJob.Fuse();
        if (jobID == 341) {
            Patch1(0x009584F6 + 2, 0x55);
            Patch1(0x009584F6 + 3, 0x01);
            Patch4(0x00958535 + 2, 3410002);
            jobPatchesApplied = true;
        } else if (jobID == 342) {
            Patch1(0x009584F6 + 2, 0x56);
            Patch1(0x009584F6 + 3, 0x01);
            Patch4(0x00958535 + 2, 3410002);
            jobPatchesApplied = true;
        } else if (jobID == 441) {
            Patch1(0x009584F6 + 2, 0xB9);
            Patch1(0x009584F6 + 3, 0x01);
            Patch4(0x00958535 + 2, 4410002);
            jobPatchesApplied = true;
        } else if (jobID == 442) {
            Patch1(0x009584F6 + 2, 0xBA);
            Patch1(0x009584F6 + 3, 0x01);
            Patch4(0x00958535 + 2, 4410002);
            jobPatchesApplied = true;
        }
    }
    if (iframes > 0) {
        Patch4(0x009591FE + 1, iframes);
    }
    return SetDamaged_Hook(_this, nDamage, vx, vy, dwObstacleData, pMob, nAttackIdx, nDir, nPowerGuard, bCheckHitRemain,
            bSendPacket);
}

auto missileSpeed = (int(__cdecl*)(int, int, int))0x00942831;

int(__cdecl missileSpeed_Hook)(int a1, int a2, int a3) {
    if (a2 == 3211016 || a2 == 3601000 || a2 == 3411007) {
        return 60;
    }
    if (a2 == 3511003 || a2 == 5211017) {
        return 180;
    }
    return missileSpeed(a1, a2, a3);
}


// Hook to modify skill ID and offset
void AttachSkillOffsetMod() {
    // Change skill ID from 1320006 (0x142446) to 2510000 (0x2639A8)
    Patch4(0x007A5B69, 0x2639A8);

    // Change offset from edi+65h to edi+69h
    // First occurrence at 0x007A5B31
    Patch1(0x007A5B31 + 1, 0x69);
    // Second occurrence at 0x007A5B86
    Patch1(0x007A5B86 + 1, 0x69);
}

constexpr DWORD dwDoomShowAffectedSkill = 0x0066F26A;
constexpr DWORD dwShowAffectedSkillRetn = 0x0066F2AD;


_declspec(naked) void SetColorToDoom() {
    _asm {
        call onDoomed
        mov dword ptr[ebp-16], 0x78a0c5
        jmp dword ptr[dwShowAffectedSkillRetn]
    }
}


void changeMagicAttacks() {
    Patch4(0x00955D19 + 1, 2101008);
    Patch4(0x00955D24 + 1, 2101007);
    Patch4(0x00955D2F + 1, 2111010);
    Patch4(0x00955D3A + 1, 2111003); // Poison Mist?
    Patch4(0x00955D45 + 1, 2201010);
    Patch4(0x00955D50 + 1, 2211014);
    Patch4(0x00955D5B + 1, 2201013);
    Patch4(0x00955D66 + 1, 2511006);
    Patch4(0x00955D7C + 1, 2411012);
    Patch4(0x00955D87 + 1, 2111013);
    Patch4(0x00955D92 + 1, 2411011);
}

bool isCopyCatSkill(int skillId) {
    int job = skillId / 10000;
    int secondDigit = (job / 10) % 10;
    int third = skillId / 1000;
    int thirdDigit = (third / 10) % 10;
    // 2421006 is deliberately NOT here: it no longer borrows another skill's id, it carries its
    // own and gets 2221006's behavior through InstallIceDemonAlias instead.
    if (skillId == 3411004 || skillId == 4101008 || skillId == 2211013 ||
        skillId == 5521009 || skillId == 5421007) {
        return true;
    }
    if (thirdDigit == 0) {
        return secondDigit == 4 || secondDigit == 5;
    }
    return false;
}


auto meso_bag_handle = (int(__thiscall*)(void*, CInPacket*))0x00959A47;

int(__fastcall siegeModePacket)(void* _cuser, void* edx, CInPacket* a2) {
    DEBUG_MESSAGE("Packet Received.");
    int jobID = CWvsContext::GetInstance()->get_m_basicStat().nJob.Fuse();
    if (CInPacket_Decode1Original(a2) == 0 && jobID <= 322) {
        siegeMode = false;
        return 0;
    }
    siegeMode = true;
    return 1;
}

auto setInput = (int(__thiscall*)(void*, int, int))0x009B7B4A;

int(__fastcall setInput_hook)(void* _this, void* edx, int XInput, int YInput) {
    if (siegeMode && (int)_ReturnAddress() == 0x009CC0DF)
        return (setInput(_this, 0, 0));
    return setInput(_this, XInput, YInput);
}

// Raw byte offsets into the live CVecCtrl. Do NOT use the members declared on the class in
// vecctrl.h for these: that class inherits ZRefCounted and IWzVector2D, so every declared field
// lands past the base subobjects and `pvc->m_bWingsNow` reads the wrong dword entirely. Offsets
// verified against CVecCtrl::Wings (0x009B21DA), which sets *((DWORD*)this + 95) -> 0x17C, and the
// ctor (0x009B0F71), which zeroes 94/95.
static const unsigned int kVecVx = 0x50;
static const unsigned int kVecWingsNow = 0x17C;

static int& vecWingsNow(CVecCtrl* vc) {
    return *reinterpret_cast<int*>(reinterpret_cast<char*>(vc) + kVecWingsNow);
}

auto SetImpactNext = (void(__thiscall*)(CVecCtrl*, double, double))0x7a6353;
void(__fastcall SetImpactNext_Hook)(CVecCtrl* _this, void* edx, double x, double y) {
    // SetImpactNext's very first instruction clears the wings flag at 0x17C, so ANY knockback --
    // taking a hit above all -- ends the glide. Carry the flag across the call: a hit should
    // stagger the player and shove them around, not cancel the skill.
    const int wingsBefore = _this ? vecWingsNow(_this) : 0;

    int job = CWvsContext::GetInstance()->get_m_basicStat().nJob.Fuse();
    if (job >= 300 && (int)_ReturnAddress() == 0x0096DAFF && job < 400) {
        SetImpactNext(_this, -x, -150);
    } else {
        SetImpactNext(_this, x, y);
    }

    if (wingsBefore) vecWingsNow(_this) = wingsBefore;
}

auto isHerosWill = (int(__cdecl*)(int))0x00765E2D;
int(__cdecl isHerosWillHook)(int skillId) {
    if (skillId == 3101012) {
        return 1;
    }
    return 0;
}

bool firstjob(int nSkillID) {
    return nSkillID == 5001002 || nSkillID == 5001001;
}

bool secondjob(int nSkillID) {
    return nSkillID == 5101003 || nSkillID == 5101002;
}

bool thirdJob(int nSkillID) {
    return nSkillID / 10000 == 541;
}


auto pDoJump = (int(__thiscall*)(CUserLocal*, int))0x0094C383;

int(__fastcall CUserLocal_Jump)(CUserLocal* _this, void* edx, int a2) {
    auto elapsed = chrono::steady_clock::now() - jumptimer;
    if (siegeMode) {
        return 0;
    }
    if (elapsed < chrono::milliseconds(150)) {
        return 0;
    }
    if (a2 == 2) {
    }
    skilltimer = chrono::steady_clock::now();
    return pDoJump(_this, a2);
}

// Shared 600ms lockout across the movement skills: once one of them actually applies its
// impulse (movementLockTimer is armed next to SetImpactNext in the DoActiveSkill hook, so
// casts rejected earlier in the chain don't count), the group stays locked until the window
// expires. 5101010 sits fully outside the group (never blocked here, and the arm site skips
// it so it doesn't lock the others); 1511009 ignores the lockout but still arms it.
// Returns true when casting is allowed.
bool MovementLockOut(int nSkillID) {
    if (nSkillID != 5101010 && nSkillID != 1511009 && nSkillID != 1421003) {
        auto elapsed = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - movementLockTimer);
        return elapsed.count() >= 600;
    }
    return true;
}

auto IsOnRope = (int(__thiscall*)(CVecCtrl*))0x00705343;
auto IsOnLadder = (int(__thiscall*)(CVecCtrl*))0x00705309;
auto IsFreeFalling = (int(__thiscall*)(CVecCtrl*))0x0096c1d3;
auto IsFalling = (int(__thiscall*)(CVecCtrl*))0x0096c1aa;
auto SetMovePath = (void(__thiscall*)(CVecCtrl*, int))0x0052EF17;

void moveSkill(CUserLocal* pthis, int skillid) {
}

bool isMovementSkill(int skillid) {
    static const int skillIDs[] = {
        // Duelist
        1411005,
        1411006,
        1421004,
        1421003,
        // chief bandit
        4211015,
        // striker
        5411021,
        // brawler
        5101009,
        5101010,
        1511009,
    };
    return std::find(std::begin(skillIDs), std::end(skillIDs), skillid) != std::end(skillIDs);
}

bool isFallingSkill(int nSkillID) {
    return nSkillID == 5411021 || nSkillID == 5101009 || nSkillID == 3601009 || nSkillID == 1421004;
}

bool isRisingSkill(int nSkillID) {
    return nSkillID == 5101010 || nSkillID == 1511009 || nSkillID == 1421003;
}

DWORD dwShipSkills = 0x0096719D;
DWORD dwShipSkillsRet = 0x009671A3;

void __declspec(naked) shipSkills() {

    int nSkillID;
    DWORD nJumpBack;

    __asm mov[nSkillID], esi


    if (nSkillID == 5211017 || nSkillID == 5201006 || nSkillID == 5201007) {
        nJumpBack = 0x009673CF;
    }
    else if (nSkillID == 5211002) {
        nJumpBack = 0x009673CF;
    }
    else if (nSkillID == 20001050) {
        nJumpBack = 0x009673CF;
    }
    else if (nSkillID == 1054) {
        nJumpBack = 0x009673CF;
    }
    else if (nSkillID == 10001054) {
        nJumpBack = 0x009673CF;
    }
    else if (nSkillID == 20001054) {
        nJumpBack = 0x009673CF;
    }

    else {
        __asm cmp esi, 5211002
        nJumpBack = dwShipSkillsRet;
    }
    __asm jmp dword ptr[nJumpBack]
}


// CSkillInfo::CheckConsumeForActiveSkill @ 0x00764256 (cdecl): validates the skill's HP/MP/meso/
// item costs against current stats (SKILLLEVELDATA hpCon +0x94 / mpCon +0xA0 vs the secured
// current-HP/MP shorts at CharacterData +0x61/+0x71). Returns 1 = castable, 0 = no learned level,
// 2/3/4 = not enough HP/MP/mesos. Same check vanilla DoActiveSkill runs at 0x967759.
auto pCheckConsumeForActiveSkill =
        (int(__cdecl*)(void* pCharData, BasicStat* pBS, SecondaryStat* pSS, int nSkillID))0x00764256;

int CheckSkillConsume(int nSkillID) {
    void* zref[2] = { nullptr, nullptr };
    GetCharacterData(CWvsContext::GetInstance(), zref); // ZRef<CharacterData>; ptr at zref[1]
    if (!zref[1]) {
        return 0;
    }
    int result = pCheckConsumeForActiveSkill(zref[1],
            &CWvsContext::GetInstance()->get_m_basicStat(),
            &CWvsContext::GetInstance()->get_m_secondaryStat(), nSkillID);
    reinterpret_cast<void(__thiscall*)(void*, void*)>(0x00428C44)(zref, nullptr); // ZRef::_ReleaseRaw
    return result;
}

auto pDoActiveSkill = (int(__thiscall*)(CUserLocal*, int, int, int))0x00966F7A;

int(__fastcall CUserLocal__DoActiveSkill_Hook)(CUserLocal* _This, void* edx, int nSkillID, unsigned int nScanCode,
        int pnConsumeCheck) {
    // Torpedo (5211017) is mount-only, mirroring the native battleship skills (e.g. 5221007): it may
    // only be cast while riding a vehicle. CUserLocal+0x544 = riding vehicle id; /10000 yields the
    // prefix (190 = tamed mount, 193 = battleship). Off a mount it falls to the 0x9673CF allow path,
    // so block it here before the original DoActiveSkill runs.
    if (nSkillID == 5211017) {
        int nRidingVehicleID = *(int*)((char*)_This + 0x544);
        int nPrefix = nRidingVehicleID / 10000;
        if (nPrefix != 190 && nPrefix != 193) {
            return 0;
        }
    }
    // Attack-follow gate: any skill cast (attack, buff, or the summon itself) counts as the player
    // being active, so the summon starts fighting immediately after you act. Basic attacks are
    // covered separately in setAttackAction.
    lastPlayerAttackTick = GetTickCount();
    if (CWvsContext::GetInstance()->m_basicStat.nJob.Fuse() != job) {
        job = CWvsContext::GetInstance()->m_basicStat.nJob.Fuse();
        comboStuff();
    }
    if (job >= 151 && job <= 152) {
        Patch4(0x0074D165 + 3, 1511009);
        auto elapsed = chrono::steady_clock::now() - jumptimer;
        if (elapsed < chrono::milliseconds(150)) {
            return 0;
        }
    }
    if (job >= 510 && job <= 600) {
        Patch4(0x0074D165 + 3, 1511009);
    }
    if (timerRunning == false) {
        aniCancelTimer = chrono::steady_clock::now();
        activeTimer = chrono::steady_clock::now();
        timerRunning = true;
    }
    setMAD();
    if (isCopyCatSkill(nSkillID)) {
        if (nSkillID == 4101008) {
            return CUserLocal__DoActiveSkill_Hook(_This, edx, 14101006, nScanCode, pnConsumeCheck);
        }
        if (nSkillID == 3411004) {
            auto elapsed = chrono::steady_clock::now() - jumptimer;
            if (elapsed < chrono::milliseconds(150)) {
                return 0;
            }
            return CUserLocal__DoActiveSkill_Hook(_This, edx, 5201005, nScanCode, pnConsumeCheck);
        }
        if (nSkillID == 2211013) {
            return CUserLocal__DoActiveSkill_Hook(_This, edx, 2311005, nScanCode, pnConsumeCheck);
        }
        if (nSkillID == 5111016) {
            return CUserLocal__DoActiveSkill_Hook(_This, edx, 15101006, nScanCode, pnConsumeCheck);
        }
        if (nSkillID == 5521009) {
            return CUserLocal__DoActiveSkill_Hook(_This, edx, 5221009, nScanCode, pnConsumeCheck);
        }
        if (nSkillID == 5421007) {
            return CUserLocal__DoActiveSkill_Hook(_This, edx, 5121007, nScanCode, pnConsumeCheck);
        }
        return CUserLocal__DoActiveSkill_Hook(_This, edx, nSkillID - 300000, nScanCode, pnConsumeCheck);
    }

    if (!isCorrectWeapon(nSkillID)) {
        return 0;
    }

    // Mana gate: vanilla DoActiveSkill runs the consume check only deep inside the original call,
    // AFTER the side effects below (movement impulses, combo spends, attack patches) have already
    // fired -- and paths that return 0 before calling the original skip it entirely, giving free
    // casts with no MP. Check first; on failure hand straight to the original so it shows the
    // proper "not enough ..." message and nothing else happens.
    // if (CheckSkillConsume(nSkillID) != 1) {
    //     return pDoActiveSkill(_This, nSkillID, nScanCode, pnConsumeCheck);
    // }

    unsigned char arrayRemoveArrowRain[] = { 0x0F, 0x84, 0x5F, 0x00, 0x00, 0x00 };
    if (nSkillID == 3411006) {
        unsigned char arrayApply[] = { 0xE9, 0xF6, 0x00, 0x00, 0x00, 0x90 };
        Patch1Array(0x0095497e, arrayApply, sizeof(arrayApply));
    } else {
        Patch1Array(0x0095497e, arrayRemoveArrowRain, sizeof(arrayRemoveArrowRain));
    }

    if (siegeMode && nSkillID == 3211016) {
        return CUserLocal__DoActiveSkill_Hook(_This, edx, 3601000, nScanCode, pnConsumeCheck);
    }

    if (siegeMode && nSkillID == 3211015) {
        return CUserLocal__DoActiveSkill_Hook(_This, edx, 3601007, nScanCode, pnConsumeCheck);
    }
    if (nSkillID == 5211012 || nSkillID == 5111007) {
        default_random_engine generator(chrono::system_clock::now().time_since_epoch().count());
        uniform_int_distribution<int> distribution(1, 6);
        int roll = distribution(generator);
        CUserLocal__DoActiveSkill_Hook(_This, edx, 3601000 + roll, nScanCode, pnConsumeCheck);
    }

    if (nSkillID == 1511003) {
        Patch4(0x00952F20 + 3, 1511003);
    }

    // Movement impulses are computed up front but, for skills that actually cast, only
    // applied after pDoActiveSkill reports success — a cast the client rejects (no MP,
    // wrong weapon, cooldown, consume check) must not grant velocity.
    bool bApplyImpactOnSuccess = false;
    bool bArmAniCancelOnSuccess = false;
    int movepath = 0;
    double vx = 0.0;
    double vy = 0.0;
    CVecCtrl* pCv = nullptr;
    if (isMovementSkill(nSkillID) && !g_inOneTimeAction) {
        pCv = CVecCtrl::FromInterface(_This->m_pvc);
        if (IsOnRope(pCv) || IsOnLadder(pCv) || !MovementLockOut(nSkillID)) {
            return 0;
        }
        int skillLevel = 0;
        if (nSkillID == 1511009 && !IsFalling(pCv) && !IsFreeFalling(pCv)) {
            // Grounded rising jump is pure client-side motion: the real cast is skipped
            // (return 0 below), so there is no consume gate to defer behind — it applies
            // immediately, rate-limited by jumptimer.
            auto elapsedJump = chrono::steady_clock::now() - jumptimer;
            if (elapsedJump < chrono::milliseconds(250)) {
                return 0;
            }
            SetMovePath(pCv, 6);
            movementLockTimer = chrono::steady_clock::now();
            SetImpactNext(pCv, 0.0, -800.0);
            return 0;
        }
        if (nSkillID == 1511009) { // not grounded -> falling or free-falling
            vx = 300.0;
            vy = 1000.0;
        }
        // Airborne and not descending = rising (jump ascent, e.g. right after a rising
        // jump). The dashes below only get momentum on the ground or while falling, so a
        // rising-state cast is rejected outright instead of granting an air boost.
        const bool bRising = IsFreeFalling(pCv) && !IsFalling(pCv);
        if (nSkillID >= 1411005 && nSkillID <= 1411006) {
            if (bRising) {
                return 0;
            }
            if (nSkillID == 1411005) {
                skillLevel = asLevel;
            } else {
                skillLevel = rsLevel;
            }
            vx = 700.0 + skillLevel * 40;
            if (nSkillID == 1411006) {
                vx *= -1;
            }
        }
        if (nSkillID == 4211015) {
            if (bRising) {
                return 0;
            }
            skillLevel = bsLevel;
            vx = 1100.0;
        }
        if (nSkillID == 5101009 && ((IsFalling(pCv)) || IsFreeFalling(pCv))) {
            vx = -520.0;
        }
        if (nSkillID == 5411021 && ((IsFalling(pCv)) || IsFreeFalling(pCv))) {
            vx = 520.0;
            vy = -20.0;
        }
        if (nSkillID == 1421004 && ((IsFalling(pCv)) || IsFreeFalling(pCv))) {
            vx = 450.0;
            vy = -80.0;
        }
        if (nSkillID == 5101010) {
            if (!IsFreeFalling(pCv) && !IsFalling(pCv)) {
                bArmAniCancelOnSuccess = true;
                vy = -555.0;
            } else {
                return 0;
            }
        }
        if (nSkillID == 1421003) {
            if (!IsFreeFalling(pCv) && !IsFalling(pCv)) {
                bArmAniCancelOnSuccess = true;
                vy = -1100.0;
            } else {
                return 0;
            }
        }
        if (_This->m_isLeft % 2) {
            vx *= -1;
        }
        if (isFallingSkill(nSkillID) && !IsFalling(pCv) && !IsFreeFalling(pCv)) {
            return 0;
        }
        bApplyImpactOnSuccess = true;
    }
    if (nSkillID == 5411022) {
        if (getCurrentComboCount() < 100) {
            return 0;
        }
        CUserLocal__SendSkillCancelRequest(_This, 5410000);
    }
    if (nSkillID == 5511014) {
        if (getCurrentComboCount() < 2500) {
            return 0;
        }
    }
    if (nSkillID == 5511002) {
        if (getCurrentComboCount() < 500) {
            return 0;
        }
    }
    if (nSkillID == 5511015) {
        if (getCurrentComboCount() < 1500) {
            return 0;
        }
    }
    auto elapsed = chrono::steady_clock::now() - skilltimer;
    if (nSkillID == 3001013 || nSkillID == 1001007 || nSkillID == 1411005 || nSkillID == 1411006 || nSkillID == 4211015) {
        jumptimer = chrono::steady_clock::now();
    }
    if (elapsed < chrono::milliseconds(150)) {
        if ((nSkillID == 3001013) || nSkillID == 1001007 || nSkillID == 1411005 || nSkillID == 1411006 || nSkillID == 4211015) {
            return 0;
        }
    }
    if (isSkillIDMatched(nSkillID)) {
        doSpearPA();
        CodeCave(doActiveSkills, 0x0096792A, 0);
    } else {
        Patch1(0x0096792A, 0x0F);
        Patch1(0x0096792A + 1, 0x8F);
        Patch1(0x0096792A + 2, 0x71);
        Patch1(0x0096792A + 3, 0x09);
        Patch1(0x0096792A + 4, 0x00);
    }
    const int nResult = pDoActiveSkill(_This, nSkillID, nScanCode, pnConsumeCheck);
    // Deferred movement impulse: only a successful cast (consume checks passed, skill
    // actually fired — see CMacroSysMan::Update, which treats this return the same way)
    // gets its velocity applied.
    if (nResult && bApplyImpactOnSuccess) {
        if (isRisingSkill(nSkillID)) {
            SetMovePath(pCv, movepath);
        }
        if (nSkillID != 5101010) {
            movementLockTimer = chrono::steady_clock::now();
        }
        if (bArmAniCancelOnSuccess) {
            aniCancelTimer = chrono::steady_clock::now();
        }
        SetImpactNext(pCv, vx, vy);
    }
    return nResult;
}

auto is_guided_skill = (int(__cdecl*)(int))0x0076662D;

int(__cdecl is_guided_skill_hook)(int skillid) {
    if (skillid == 5211016 || skillid == 3201016) {
        return 1;
    }
    return is_guided_skill(skillid);
}

auto get_cool_time = (int(__cdecl*)(int))0x009535E3;

int(__cdecl get_cool_time_t)(int nSkillID) {
    if (nSkillID == 1001007 || nSkillID == 3001013) {
        return 1000;
    }
    if (nSkillID == 1411005 || nSkillID == 1411006) {
        return 900;
    }
    if (nSkillID == 5201006) {
        return 700;
    }
    if (nSkillID == 5411002) {
        return 750;
    }
    if (nSkillID == 5411021) {
        return 750;
    }
    if (nSkillID == 1421004) {
        return 750;
    }
    if (nSkillID == 1211000 || nSkillID == 1211014 || nSkillID == 1221016
            || nSkillID == 1221021) {
        return 1720;
    }
    return (get_cool_time(nSkillID));
}

// Shared cooldown LOCKOUT for the Charged-Blow group (1211000 / 1211014). get_cool_time only adds a
// per-skill attack DELAY (swing rate), which can't be shared across skills -- a real "using one locks
// out the other" needs a single expiry timer. DoActiveSkill_MeleeAttack @ 0x00969465 is the one
// chokepoint every melee attack skill passes through (it gates then calls TryDoingMeleeAttack), so we
// detour it: if either skill is on the shared cooldown, block; otherwise run the original and, if the
// attack fired, arm the shared timer for get_cool_time_t(skill) ms.
auto DoActiveSkill_MeleeAttack = (int(__thiscall*)(void*, const void*, void*))0x00969465;
static DWORD g_chargedBlowCdExpiry = 0; // GetTickCount() value until which 1211000/1211014 are locked

int __fastcall DoActiveSkill_MeleeAttack_hook(void* _this, void* edx, const void* pSkill, void* a3) {
    int skillId = pSkill ? *reinterpret_cast<const int*>(pSkill) : 0; // SKILLENTRY: skill id is first
    bool shared = (skillId == 1211000 || skillId == 1211014);
    DWORD now = GetTickCount();
    if (shared && now < g_chargedBlowCdExpiry) {
        return 0; // one of the pair is still cooling down -> block both
    }
    int result = DoActiveSkill_MeleeAttack(_this, pSkill, a3);
    if (shared && result) {
        g_chargedBlowCdExpiry = now + get_cool_time_t(skillId); // arm shared timer (1720ms)
    }
    return result;
}

auto hitMobInRect = (int(__cdecl*)(int))0x00766722;
int(__cdecl hitMobInRect_hook)(int skillId) {
    if (skillId == 4101008 || skillId == 3411006 || skillId == 4121017 || skillId == 4421015 || skillId == 5521003 || skillId == 5511017) {
        return 1;
    }
    return hitMobInRect(skillId);
}

auto remove_bullet_skill_hook = (int(__cdecl*)(int))0x007667EE;

int(__cdecl remove_bullets)(int nSkillID) {
    if (nSkillID == 3001004 || nSkillID == 5111017 || nSkillID == 3111009 || nSkillID == 3211016 || nSkillID == 3601000 || nSkillID == 3601007 || nSkillID == 3411006 || nSkillID == 3511003 || nSkillID == 4121017 || nSkillID == 4421015 || nSkillID == 5521003 || nSkillID == 5511017) {
        return 1;
    }
    return (remove_bullet_skill_hook(nSkillID));
}

void octoJump() {
    Patch4(0x0096C00A + 1, 0x00000000);
    Patch4(0x0096C021 + 3, 0x00000000);
    Patch4(0x0096C031 + 1, 0xFFFFFd80);
    Patch1(0x0096C012 + 2, 0x00);
    Patch1(0x0096C02E + 2, 0x4);
}

void propulsion() {
    Patch4(0x0096C00A + 1, 0xFFFFFD50);
    Patch4(0x0096C021 + 3, 0x00000250);
    Patch4(0x0096C031 + 1, 0x00000850);
    Patch1(0x0096C012 + 2, 0x00);
    Patch1(0x0096C02E + 2, 0x4);
}


void flashJump() {
    Patch4(0x0096C00A + 1, 0xFFFFFe8f);
    Patch4(0x0096C021 + 3, 0x00000150);
    Patch4(0x0096C031 + 1, 0xFFFFFe8f);
    Patch1(0x0096C02E + 2, 0x3);
}

// The client's own "is this a charge skill" test (1121001/1221001/1321001, 2121001, 2221001,
// 2321001, 3121004, 3221001, 4341002, 5101004, 5201002, 22121000, 22151001). Everything that
// still cares about the keydown state machine after the router has already sent the skill down
// the magic-attack path asks this, so answer 0 for the instant-cast list.
auto isKeydownSkill = (int(__cdecl*)(int))0x004FB08F;
int __cdecl isKeydownSkillHook(int nSkillID) {
    if (IsNoChargeSkill(nSkillID)) {
        return 0;
    }
    return isKeydownSkill(nSkillID);
}


auto elementCharge = (int(__cdecl*)(int))0x007908E7;
int __cdecl elementChargeHook(int skillid) {
    if (skillid == 3111018) {
        return 2;
    }
    return 0;
}

const DWORD FlashJumpVar = 0x0096BF52;
const DWORD FlashJumpRet = 0x0096BF12;

void __declspec(naked) FlashJumpAll() {
    _asm {
            cmp eax, 4101009
            je[flashJ]
            cmp eax, 5201007
            je[octoJ]
            jmp FlashJumpRet

            octoJ :
            push ebp
            mov ebp, esp
            call octoJump
            mov esp, ebp
            pop ebp
            jmp[fjvar]

            flashJ:
            push ebp
            mov ebp, esp
            call flashJump
            mov esp, ebp
            pop ebp
            jmp[fjvar]

            fjvar : jmp[FlashJumpVar]
    }
} //

auto calcpdamage_hook = (void*(__thiscall*)(int, int, int, int, int, int, int, int, int, int, int, int, int, int**, int,
        int, int, int, int, int, int))0x0078DF87;

void*(__fastcall CalcDamage__PDamage)(
        int _this,
        void* edx,
        int a2,
        int bs,
        int a4,
        int dwMobid,
        int a6,
        int a7,
        int nDamagePerMob,
        int nItemID,
        int a10,
        int a11,
        int nAction,
        int shadow_partner,
        int** a14,
        int a15,
        int a16,
        int a17,
        int a18,
        int a19,
        int a20,
        int a21) {
    printf("damagePerMob: %d, a9: %d, a10: %d, a14: %d, a19 = %d,", nDamagePerMob, nItemID, a10, a4, a19);
    return calcpdamage_hook(_this, a2, bs, a4, dwMobid, a6, a7, nDamagePerMob, nItemID, a10, a11,
            nAction, shadow_partner, a14, a15, a16, a17, a18, a19, a20, a21);
}

auto createWorldMap = (void(__thiscall*)(void*, int))0x009EB75B;
void (__fastcall noMap)(void* _this, void* edx, int) {
}

auto skillDelayHook = (int(__cdecl*)(int))0x00765047;

void __cdecl UpdateSummonSeekGate(); // defined near the summonSeekRect cave below

int(__cdecl summondelay)(int nSkillID) {
    // Attack-follow gate, turret-path coverage: octopus-type/stationary summons never touch the
    // sub_678ECC seek (gated in summonSeekRect), but every summon's attack scheduling asks this
    // function for its attack period. Gate shut -> report a 10-minute period so the attack is never
    // due; gate open -> normal period, so attacks resume on the next check. 10 min (not INT_MAX)
    // to keep tLast + delay arithmetic in the client far from signed overflow.
    UpdateSummonSeekGate();
    if (!summonSeekGateOpen) {
        return 600000;
    }
    // Summon attack period = 2500ms minus 50ms per learned level of the summon skill (higher level
    // -> attacks faster). Look up the player's level in nSkillID via CSkillInfo::GetSkillLevel.
    int lvl = 0;
    SkillInfo* si = SkillInfo::GetInstance();
    if (si) {
        void* zref[2] = { nullptr, nullptr };
        GetCharacterData(CWvsContext::GetInstance(), zref); // ZRef<CharacterData>; ptr at zref[1]
        if (zref[1]) {
            lvl = pGetSkillLevel(reinterpret_cast<int>(si), zref[1], nSkillID, 0);
            reinterpret_cast<void(__thiscall*)(void*, void*)>(0x00428C44)(zref, nullptr); // ZRef::_ReleaseRaw
        }
    }
    if (nSkillID == 5511015) {
        return 300;
    }
    int delay = 2500 - 50 * lvl;
    return delay < 0 ? 0 : delay;
}

auto MakeIncDecHpEffect = (void*(__thiscall*)(void*, int, int))0x0092EC50;

void(__fastcall skipIncDecHpEffect)(void* cuser, void* edx, int damage, int bguard) {
    return;
}

auto getCIDHook = (int(__thiscall*)(void*))0x007A6E53;

int(__fastcall GetCharacterId)(void* _this) {
    DEBUG_MESSAGE("%7d", getCIDHook(_this));
    myCharacterid = getCIDHook(_this);
    return getCIDHook(_this);
}

auto CMob_OnHit_Hook = (void(__thiscall*)(int, int, int, int, int, int, int, void*,
        int, int, int, int, int, int))0x00668B83;

void(__fastcall CMob__OnHit(
        int _this,
        void* edx,
        int dwCharacterId,
        int nSkillID,
        int nHitAction,
        int bLeft,
        int nDamage,
        int bCriticalAttack,
        void* nAttackIdx,
        int bChase,
        int nMoveType,
        int nBulletCashItemID,
        int nMoveEndingPosX,
        int nMoveEndingPosY,
        int a14)) {
    return CMob_OnHit_Hook(_this, dwCharacterId, nSkillID, nHitAction, bLeft, nDamage,
            bCriticalAttack, nAttackIdx, bChase, nMoveType, nBulletCashItemID, nMoveEndingPosX,
            nMoveEndingPosY, a14);
}

auto CMob_ShowDamage_Hook = (void(__thiscall*)(void*, int, int, int, int))0x006691D3;

void(__fastcall skipShowDamage)(void* cuser, void* edx, int nDamage,
        int nAttackIdx,
        int bCriticalAttack,
        int bHalfHeight) {
    return CMob_ShowDamage_Hook(cuser, nDamage, nAttackIdx, bCriticalAttack, bHalfHeight);
}

// int(__fastcall SecondaryStat__SetFrom)(int ss, void* edx, int cd, int bs, int fs, int a3, int a4, int a5) {
//
// }

auto pGetAttackSpeedDegree = (void(__thiscall*)(int, int, int, int))0x00765066;

int(__cdecl GetAttackSpeedDegree)(int nDegree, int nSkillID, int nWeaponBooster, int nPartyBooster) {
    int nWeaponDegree = nDegree;
    if (mastery > 0) {
        nWeaponDegree -= 2;
        if (nWeaponBooster != 0) {
            weaponSpeed += nWeaponBooster;
            nWeaponDegree += nWeaponBooster;
        }
    }
    weaponSpeed = nWeaponDegree;
    return nWeaponDegree;
}

auto octHook = (int(__cdecl*)(int))0x00766612;
int(__cdecl ltrbOcto)(int nSKillID) {
    return nSKillID == 3211002 || nSKillID == 3411010 || nSKillID == 4111017 || nSKillID == 5511015|| nSKillID == 5511014;
}

int(__cdecl octopus)(int nSkillID) {
    if (nSkillID == 3121013 || nSkillID == 5511015 || nSkillID == 5511014 || nSkillID == 5521016 || nSkillID == 5111015 || nSkillID == 4111017 || nSkillID == 3411010) {
        return 1;
    }
    return octHook(nSkillID);
}

auto ltrbshoothook = (int(__cdecl*)(int))0x00766722;

int(__cdecl ltrb)(int nSkillID) {
    if (nSkillID == 3211015 || nSkillID == 3411006 || nSkillID == 3001004 || nSkillID == 3601007 || nSkillID == 5111017 || nSkillID == 3511003 || nSkillID == 4121017 || nSkillID == 4421015 || nSkillID == 5521003 || nSkillID == 5511017) {
        return 1;
    }
    return ltrbshoothook(nSkillID);
}

auto get_vertical_adjust_of_attack_range = (int(__cdecl*)(int))0x0076664D;

int(__cdecl vertical)(int nSkillID) {
    if (nSkillID == 3601007 || nSkillID == 3511003) {
        return 60;
    }
    if (nSkillID == 3001004 || nSkillID == 3211015) {
        return 30;
    }
    if (nSkillID == 4111005) {
        return 120;
    }
    return get_vertical_adjust_of_attack_range(nSkillID);
}

auto is_skill_need_master_level = (int(__cdecl*)(int))0x004E8F04;

int(__cdecl masteryskill)(int jobId) {
    return 1; // all jobs return true
}

int critsjmp = 0x007650f5;
int nwthrow = 0x0078F881;

void _declspec(naked) dCrits() {
    _asm {
            cmp eax, 50
            pop ecx
            jmp[critsjmp]
    }
}


int jnejmp = 0x0078EEC1;
int jeclawjmp = 0x0078FAD8;

void _declspec(naked) Claw_5() {
    _asm {
            jne[lb1]
            je[lb2]
            lb1:
            jmp[jnejmp]
            lb2 :
            jmp[jeclawjmp]
    }
}

// 0078F886
// 0078FAD8
int jztos = 0x0078FAD8;
int jmpbacks = 0x0078F886;

void _declspec(naked) NW_Multi() {
    _asm {
            cmp eax, 14121001
            je[lb1]
            cmp eax, 0xD7511D
            jmp[jmpbacks]
            lb1 :
            jmp[jztos]
    }
}

int madcalcjmpout = 0x00791BAE;
int madcalcjmpback = 0x00791BB4;

void _declspec(naked) DamCalc() {
    _asm {
            push dword ptr[eax + 0xD8]
            add eax, 0xD0
            jmp[madcalcjmpback]
    }
}

double div100 = 0.01;


// Generate a random double
double random_number = 0.0;

void redoMagic() {
    double min = masteryValue > 0 ? (masteryValue * 0.01) + 0.1 : 0.15;
    min = min(min, 0.99);

    std::uniform_real_distribution<double> dist(min, 1.0);
    random_number = dist(rng);
}

void _declspec(naked) please() {
    _asm {
            fild topMAD
            call[redoMagic]
            fmul random_number
            fimul[ebp + 0x30] // damage from skill
            fdiv Hundred
            jmp[pleasejmpout]
    }
}


auto SetAttackAction_Hook = (signed int(__thiscall*)(int*, int, int, int*, int))0x0092EDB2;

int __fastcall setAttackAction(int* a1, void* edx, int a3, int a4, int* a5, int a6) {
    // Attack-follow gate: any local-player attack action (including basic attacks, which never go
    // through DoActiveSkill) opens the summon seek window. Fires for every CUser, so filter to the
    // CUserLocal singleton (0xBEBF98) or nearby players would drive your summon.
    if (reinterpret_cast<int>(a1) == *reinterpret_cast<int*>(0x00BEBF98)) {
        lastPlayerAttackTick = GetTickCount();
    }
    int wspeed = weaponSpeed;
    if (mastery <= 0) {
        switch (get_weapon_type()) {
        case 37:
            wspeed = 4;
            break;
        case 42:
            wspeed = 5;
            break;
        case 38:
            wspeed = 8;
            break;
        }
    } else {
        switch (get_weapon_type()) {
        case 37:
            wspeed = 2;
            break;
        case 42:
            wspeed = 3;
            break;
        case 38:
            wspeed = 6;
            break;
        }
    }
    return SetAttackAction_Hook(a1, a3, wspeed, a5, a6);
}

auto ShowSkillEffect_hook = (void(__thiscall*)(void*, void*, int, int, int, int, void*))0x00933990;

void __fastcall ShowSkillEffect(
        void* _this,
        void* ecx,
        void* pSkill,
        int nSLV,
        int nActionSpeed,
        int bLeft,
        int nLast,
        void* pPtOffset) {

    int wspeed = 0;
    if (mastery <= 0) {
        switch (get_weapon_type()) {
        case 37:
            wspeed = 4;
            break;
        case 42:
            wspeed = 5;
            break;
        case 38:
            wspeed = 8;
            break;
        default:
            wspeed = weaponSpeed;
        }
    } else {
        switch (get_weapon_type()) {
        case 37:
            wspeed = 4;
            break;
        case 42:
            wspeed = 3;
            break;
        case 38:
            wspeed = 6;
            break;
        default:
            wspeed = weaponSpeed;
        }
    }
    return ShowSkillEffect_hook(_this, pSkill, nSLV, wspeed, bLeft, nLast, pPtOffset);
}

auto LoadMapHook = (int(__thiscall*)(void*, int))0x00529BB4;

int __fastcall LoadMap(void* CMAPLOADABLE, void* ecx, int mid) {
    if (MapID != LastMapID) {
        LastMapID = MapID;
    }
    MapID = mid;
    return LoadMapHook(CMAPLOADABLE, mid);
}

auto ztlSecureFuse_double_check = (double(__cdecl*)(int, int))0x00539338;

double __cdecl ztlfuse_double(int a1, int a2) {
    uintptr_t ret = (uintptr_t)_ReturnAddress();
    double val = ztlSecureFuse_double_check(a1, a2);
    if (ret >= 0x0052EF51 && ret <= 0x0052EF81) {
        DEBUG_MESSAGE("%f", val);
        if (val == -700.0) {
            val = 700.0;
        }
    }
    return val;
}

// The client's get-mastery. Both damage-range users of it (CalcDamage::PDamage @0x0078E0D5 and
// CUIStatDetail::Draw @0x008C2B83) feed the return straight into (m * 5 + 10) * 0.009, so handing
// back the Skill.wz `mastery` only works together with the constant patches in PatchMasteryRange,
// which turn that into (m * 1 + 10) * 0.01 == 0.01 * mastery + 0.1. The remaining callers
// (SecondaryStat::SetFrom, the TryDoing*Attack trio) only test it against zero or pass it along.
auto mastery_Calcs_Hook = (int(__cdecl*)(int, int, int, int, int, int))0x00764795;

int __cdecl mCalc(int a1, int a2, int a3, int a4, int a5, int a6) {
    const unsigned int caller = reinterpret_cast<unsigned int>(_ReturnAddress());

    // The three attack paths pass this on as the AFTERIMAGE level -- the ghost trail under
    // Character/AfterImage/<weapon>.img, which only has nodes 0..10. A Skill.wz mastery value (up
    // to 60) has no node there and the trail breaks, and so does a learned mastery level above 10
    // once +skill sources push it there. Lock all of them to the level-0 trail.
    if (caller == 0x0095104A     // CUserLocal::TryDoingMeleeAttack
        || caller == 0x00953D4D  // CUserLocal::TryDoingShootAttack
        || caller == 0x00957448) // CUserLocal::TryDoingNormalAttack
    {
        return 0;
    }

    // Damage-range sites (CalcDamage::PDamage, CUIStatDetail::Draw) want the Skill.wz mastery,
    // paired with the constant patches in PatchMasteryRange. SecondaryStat::SetFrom, the only
    // other caller, just tests the result against zero.
    if (masteryValue < 0) {
        return 0;
    }
    if (masteryValue > 90) {
        return 90; // (90 + 10) * 0.01 == 1.0: a full-range roll, never an inverted one
    }
    return masteryValue;
}

// Damage-range constants for the two client sites above. Vanilla multiplies mastery by 5.0
// (dbl_AF8298) and scales by 0.009 (dbl_AFE8B8); both are shared globals used elsewhere, so we
// repoint the two fmul operands at our own doubles instead of editing theirs. The fadd of 10.0
// in between is already what we want.
static double kMasteryPerPoint = 1.0;
static double kMasteryScale = 0.01;

static void PatchMasteryRange() {
    Patch4(0x0078E0EA + 2, reinterpret_cast<unsigned int>(&kMasteryPerPoint)); // PDamage: fmul 5.0
    Patch4(0x0078E0F6 + 2, reinterpret_cast<unsigned int>(&kMasteryScale));    // PDamage: fmul 0.009
    Patch4(0x008C2A94 + 2, reinterpret_cast<unsigned int>(&kMasteryPerPoint)); // StatDetail: fmul 5.0
    Patch4(0x008C2AA0 + 2, reinterpret_cast<unsigned int>(&kMasteryScale));    // StatDetail: fmul 0.009
}

auto ztlSecureFuse_short = (unsigned int(__cdecl*)(int, int))0x004746DD;

unsigned int __cdecl ztlfuse_short(int a1, int a2) {
    return ztlSecureFuse_short(a1, a2);
}

auto getPAD = (int(__thiscall*)(void*, int, int))0x0077DF48;

int __fastcall getPAD_hook(void* thisptr, void* edx, int a2, int a3) {
    pad = getPAD(thisptr, a2, a3);
    return pad;
}

double ropebase = 5.0;

auto getSpeed = (int(__thiscall*)(void*))0x008C457C;

void ropeFormula() {
    // Keep the global `speed` exactly as before -- the wings CalcFloat hook reads it.
    speed = 100 + PassiveSpeed + CWvsContext::GetInstance()->get_m_secondaryStat().m_speed.Fuse();

    // Rope uses the engine's effective move speed (SecondaryStat::GetSpeed: the 100-based value,
    // already clamped + bonuses) of the LOCAL player. The old formula fed rope from m_speed.Fuse()
    // which decodes to 0, so rope was pinned at the 4.0 floor for everyone and never reflected the
    // character's speed. getSpeed is the original trampoline, so calling it does not re-enter the hook.
    void* ss = &CWvsContext::GetInstance()->get_m_secondaryStat();
    int moveSpeed = getSpeed(ss) + PassiveSpeed; // GetSpeed already includes the 100 base
    double rope = 4.0 + (moveSpeed / 100.0);
    if (rope < 4.0) {
        rope = 4.0;
    }
    if (ropebase != rope) {
        ropebase = rope;
        Patch4(0x009CC6F9 + 2, 0x00C1CF80); // switch addy
        WriteDouble(0x00C1CF80, rope);      // Addy speed control
    }
}


int __fastcall getSpeed_hook(void* thisptr, void* edx) {
    ropeFormula();
    return getSpeed(thisptr);
}


auto chainLightning_Hook = (int(__thiscall*)(SKILLENTRY*, int, int, int*, int))0x0075BF50;

// Returns the original AdjustDamageDecRate's BOOL. The caller (TryDoingMeleeAttack @ 0x951e68) does
// `test eax,eax; jnz` and SKIPS the damage-number display when this is non-zero. A void return left
// garbage in eax -- non-zero whenever the poison/status branch ran -- so damage numbers vanished on
// status-ailmented mobs even though the hit packet still sent. Always return 0 (normal: display).
int __fastcall drop_off_damage_skills(SKILLENTRY* a1, void* edx, int a3, int nOrder, int* aDamage, int a6) {
    int i;
    int nSkillID;
    nSkillID = a1->skillId;
    double dMultiplier = 1.0;
    double incRate = 0.0;
    double defenseShred = 1.0;
    if (nSkillID == 3601007 || nSkillID == 3211015) {
        incRate = 0.1;
    }
    if (nSkillID == 3001004 || nSkillID == 3201005 || nSkillID == 321015) {
        incRate = -0.2;
    }
    if (sharpenlevel > 0) {
        incRate += sharpenlevel * 0.01;
    }


    // Level-based outgoing reduction: deal 2% less damage per level the player is BELOW the target
    // mob. At or above the mob's level there is no level penalty.
    // The caller (CUserLocal::TryDoingMeleeAttack @ 0x951e52) passes aDamage == perTargetStruct +
    // 0x18, and *(Mob**)perTargetStruct is the target Mob* (same ptr CalcDamage::PDamage derefs to
    // reach mob->m_pTemplate @ +0x188). So recover the mob by backing up 0x18 bytes.
    double levelMult = 1.0;
    double defMult = 1.0;
    double poisonMult = 1.0;
    double air = 1.0;
    double bossMult = 1.0;
    bool isBoss = false;
    // NOTE: aDamage == perTargetStruct + 0x18 with *(perTargetStruct) == Mob* only on the MELEE /
    // SHOOT paths. TryDoingMagicAttack passes a different damage-array pointer, so aDamage-0x18 is
    // NOT a Mob* there -- validate before any deref or magic attacks fault.
    Mob* mob = *reinterpret_cast<Mob**>(reinterpret_cast<char*>(aDamage) - 0x18);
    // The recovered Mob* is only real on the MELEE/SHOOT paths. On other paths aDamage-0x18 is some
    // unrelated value (e.g. a status-effect pointer) that can still survive IsBadReadPtr, giving a
    // garbage mobLevel. That garbage drives levelMult hugely negative -> floored to 0 -> the target
    // takes 0 damage (false immunity) for EVERY class, independent of the poison level. Require a
    // sane monster level (1..400) before trusting the mob for any level/def/poison adjustment.
    int mobLevel = (mob && !IsBadReadPtr(mob, sizeof(Mob))) ? mob->m_stat.nLevel : 0;
    if (mob && mobLevel >= 1 && mobLevel <= 400) {
        int playerLevel = CWvsContext::GetInstance()->get_m_basicStat().nLevel.Fuse();
        if (playerLevel + 10 < mobLevel) {
            levelMult = 1.0 - 0.01 * (mobLevel - playerLevel);
            if (playerLevel + 20 < mobLevel) {
                levelMult = .8 + (mobLevel - playerLevel - 20) * -0.02;
            }
            if (levelMult < 0.00) {
                levelMult = 0.00; // floor so high-level mobs are hard, not literally immune
            }
        }
        // Defense, applied always: mirror the incoming PDD/(500+PDD) mitigation curve from
        // MobPDamage_Hook, but with the MOB's defense (template PDDamage/MDDamage + WDEF/MDEF debuff).
        // Magic vs physical is decided by the equipped weapon (wand 37 / staff 38 / 32 == magic), the
        // only reliable per-attack signal -- the magic CalcDamage::MDamage is invoked with a NULL mob
        // for AoE skills, so this drop-off hook (which always sees the real per-target mob) is the one
        // place mob magic defense can be applied. defMult = 1000/(1000+def) -> never zeroes.
        if (mob->m_pTemplate && !IsBadReadPtr(mob->m_pTemplate, sizeof(MobTemplate))) {
            // Boss flag lives in the template at +0x64 (0/1). EDIT bossMult below to taste.
            isBoss = (mob->m_pTemplate->bIsBoss.Fuse() != 0);
            if (isBoss) {
                bossMult = 1.0 + (0.015 * hermitBoss) ; // <-- set your boss damage modifier here
            }
            int wt = get_weapon_type();
            bool magic = (wt == 32 || wt == 37 || wt == 38);
            double mobDef = magic ? mob->m_pTemplate->nMDDamage.Fuse()
                                  : mob->m_pTemplate->nPDDamage.Fuse();
            mobDef = applyMobDefenseStat(&mob->m_stat, mobDef, magic); // fold WDEF/MDEF up/down debuff
            // Armor-break shreds are physical only -- never amplify magic.
            if (!magic && (sniperShred > 0 || rangerShred > 0 || duelistShred > 0 || galeShot > 0 || barbShred > 0 || sairIgnore > 0)) {
                if (nSkillID == 3411007) {
                    defenseShred -= 0.02 * galeShot;
                } else {
                    defenseShred -= (0.02 * (sniperShred + rangerShred + duelistShred + barbShred + sairIgnore));
                }
            }
            if (mobDef > 0.0) {
                defMult = 1000.0 / (1000.0 + (mobDef * defenseShred));
            }
        }

        int poisonReason = *reinterpret_cast<int*>(reinterpret_cast<char*>(&mob->m_stat) + 0xB0);
        if (poisonBonusLevel > 0 && poisonReason != 0) {
            poisonMult = 1.0 + 0.02 * poisonBonusLevel;
        }
    }

    // Master Skies: +2% damage vs mobs per skill level, but only while the player is airborne.
    // Read the local player's CVecCtrl off ms_pInstance (0x00BEBF98) and gate on IsFalling /
    // IsFreeFalling (covers the rise of a jump and the fall). No mob deref needed -- applies on
    // every path. masterSkies is the learned level of 3410000, read in GetSkillLevel.
    if (masterSkies > 0) {
        CUserLocal* localUser = *reinterpret_cast<CUserLocal**>(0x00BEBF98);
        if (localUser) {
            CVecCtrl* pCv = CVecCtrl::FromInterface(localUser->m_pvc);
            if (pCv && (IsFalling(pCv) || IsFreeFalling(pCv))) {
                air = 1.0 + 0.02 * masterSkies;
            }
        }
    }

    // Order-based drop-off is per TARGET, not per damage line: compute the multiplier once from
    // nOrder so every line of this hit shares it. (Previously this accumulated inside the loop, so
    // each successive line compounded nOrder*incRate again -> damage per hit fell off even though
    // incRate/nOrder were unchanged.)
    if (incRate != 0.0) {
        dMultiplier += nOrder * incRate;
    }
    if (dMultiplier <= 0) {
        dMultiplier = 0;
    }

    // Apply to ALL 15 lines. Do NOT early-out on a zero line: a missed hit is 0 in the middle of the
    // array, and bailing there left every later line at full damage (no reduction). Unused trailing
    // slots are already 0 and stay 0 (0 * mult == 0), so scanning the whole array is harmless.
    for (i = 0; i < 15; i++) {
        aDamage[i] = (int)((double)aDamage[i] * dMultiplier * levelMult * defMult * poisonMult * air * bossMult);
    }
    return 0; // BOOL: 0 = let the caller run the normal damage-number display path
}


// Summon damage rework. Replaces CalcDamage::PDamage/MDamage-for-summons (sub_79216D / sub_792595),
// which used a PAD-or-MAD / (1 + levelDiff) curve. Now mirrors regular player skills:
//   dmg = (statMult*primary + secondary) * attack / 100 * skillDmg%
// then the same level + def/(500+def) mitigation we apply to non-summon skills (see
// drop_off_damage_skills). __thiscall original args (from CSummoned::TryDoingAttackManual):
//   this=CalcDamage, a2=mobStatFused, a3=&MobStat, a4=CharacterData, a5=BasicStat, a6=SecondaryStat,
//   a7=skill, a8=skillBonusParam, a9=skillDamage%. a3 == &mob->m_stat (Mob+0x1A0), so recover Mob*
//   by backing up 0x1A0 to reach m_pTemplate (for PDDamage/MDDamage).

// Shared finisher: takes the player-stat term (statMult*primary + secondary), the attack stat
// (WATK for physical, MAD for magic) and the skill damage node %, then applies the mastery range
// roll plus level + defense mitigation. magicDefense picks MDDamage over PDDamage for the mob.
static int finishSummonDamage(MobStat* a3, BasicStat* a5, double statTerm, int attack, int skillDmgPct,
        bool magicDefense) {
    double base = statTerm * attack / 100.0;
    double dmg = base * (skillDmgPct / 100.0);

    // Mastery damage range, per line (matches MesoFormula / redoMagic in this file).
    double minMult = 0.1 + masteryValue * 0.01;
    if (minMult > 1.0) {
        minMult = 1.0;
    }
    std::uniform_real_distribution<double> dist(minMult, 1.0);
    dmg *= dist(rng);

    Mob* mob = reinterpret_cast<Mob*>(reinterpret_cast<char*>(a3) - 0x1A0);
    MobTemplate* tmpl = (a3 && !IsBadReadPtr(mob, sizeof(Mob))) ? mob->m_pTemplate : nullptr;
    (int)statTerm, attack, skillDmgPct, magicDefense, a3, mob, tmpl;

    // Same level + defense mitigation as non-summon skills.
    int playerLevel = a5->nLevel.Fuse();
    int mobLevel = a3->nLevel;
    if (playerLevel + 5 < mobLevel) {
        double levelMult = 1.0 - 0.01 * (mobLevel - playerLevel);
        if (levelMult < 0.05) {
            levelMult = 0.05;
        }
        dmg *= levelMult;
    }
    if (tmpl && !IsBadReadPtr(tmpl, sizeof(MobTemplate))) {
        double mobDef = magicDefense ? tmpl->nMDDamage.Fuse() : tmpl->nPDDamage.Fuse();
        mobDef = applyMobDefenseStat(a3, mobDef, magicDefense); // fold WDEF/MDEF up/down debuff
        if (mobDef > 0.0) {
            dmg *= 1000.0 / (1000.0 + mobDef);
        }
    }

    int result = (int)dmg;
    if (result <= 0 && base > 0.0) {
        result = 1; // never collapse a real hit into a 0 (engine treats 0 as a miss)
    }
    return result;
}

void SummonPull_OnHit(MobStat* pStat); // summon pull, defined with the rest of that block below

auto summonPDamage = (int(__thiscall*)(void*, int, MobStat*, int, BasicStat*, SecondaryStat*, int, int, int))0x0079216D;
int __fastcall summonPDamage_hook(void* calc, void* edx, int a2, MobStat* a3, int a4, BasicStat* a5,
        SecondaryStat* a6, int a7, int a8, int a9) {
    // Summon attacks are autonomous; on a bad hit (e.g. a stale/garbage mob or stat pointer) the
    // engine can hand us junk a3/a5. Deref them unguarded smashes the client, so bail to 0 damage.
    if (!a3 || !a5 || IsBadReadPtr(a3, sizeof(MobStat)) || IsBadReadPtr(a5, sizeof(BasicStat))) {
        return 0;
    }
    SummonPull_OnHit(a3); // no-op unless this summon pulls
    int str = a5->nSTR.Fuse();
    int dex = a5->nDEX.Fuse();
    int luk = a5->nLUK.Fuse();

    // Weapon stat multiplier + primary/secondary. Others fall through to a sane default.
    double statMult = 3.0;
    double primary = dex;
    double secondary = str;
    switch (get_weapon_type()) {
    case 43: // spear
    case 44: // polearm
        statMult = 4.6;
        primary = str;
        secondary = dex;
        break;
    case 45: // bow
    case 49: // gun
        statMult = 4.0;
        primary = dex;
        secondary = str;
        break;
    case 47:
        statMult = 4.0;
        primary = luk;
        secondary = dex;
        break;
    }


    double statTerm = statMult * primary + secondary;
    return finishSummonDamage(a3, a5, statTerm, pad, a9, false);
}

// Magic summons: 4.5 * INT, scaled by the actual total magic attack from equips/buffs
// (m_magic + m_bonusMagic), then the skill damage node %. Mitigated by the mob's MDDamage.
auto summonMDamage = (int(__thiscall*)(void*, int, MobStat*, int, BasicStat*, SecondaryStat*, int, int, int))0x00792595;
int __fastcall summonMDamage_hook(void* calc, void* edx, int a2, MobStat* a3, int a4, BasicStat* a5,
        SecondaryStat* a6, int a7, int a8, int a9) {
    if (!a3 || !a5 || IsBadReadPtr(a3, sizeof(MobStat)) || IsBadReadPtr(a5, sizeof(BasicStat))) {
        return 0;
    }
    SummonPull_OnHit(a3); // no-op unless this summon pulls
    int int_ = a5->nINT.Fuse();
    int magic = CWvsContext::GetInstance()->get_m_secondaryStat().m_magic.Fuse();
    int bonusMagic = CWvsContext::GetInstance()->get_m_secondaryStat().m_bonusMagic.Fuse();
    int mad = magic + bonusMagic;
    if (mad < 0) {
        mad = 0;
    }

    double statTerm = 4.5 * int_;
    return finishSummonDamage(a3, a5, statTerm, mad, a9, true);
}

// Gate 2 of summon seeking: the summon's own attack reach. After sub_678ECC finds a candidate mob
// near the player, TryDoingAttackManual rejects it (via sub_679084) unless it's within the summon's
// reach box = SummonedAttackInfo +0x30 (v7[12], X) / +0x34 (v7[13], Y). Those are the small WZ
// defaults, so the summon -- which hugs the player -- can't hit anything you walk away from.
// CSummonedBase::LoadAttackInfo builds that struct; inflate` the reach to summonSeekRange X/Y.
// __thiscall + NRV: ecx = this (CSummonedBase), stack = retbuf, bstr, attackIdx; returns retbuf in
// eax, with the attackInfo pointer at *(retbuf + 4).
// Summon mobCount comes from the summon's own attack-info node (SummonedAttackInfo +0x24, loaded
// from the "mobCount" WZ node), which on custom summons is missing/1 -- so the summon only ever hits
// one mob. Instead pull it from the player skill's level data, the same field the player's own
// attacks read: SKILLENTRY::GetLevelData(skill, level) -> SKILLLEVELDATA. Each field is a secured
// long: value at `valueOff`, key at valueOff+8, decoded with _ZtlSecureFuse<long>. mobCount lives at
// +0x130 and bulletCount/attackCount at +0x100 (see TryDoingMeleeAttack @ 0x9514a1 / 0x951bf0).
// Returns 0 on any failure so callers can keep their default.
auto summonGetLevelData = (int(__thiscall*)(void* skill, int level))0x00760F23;
auto summonSecureFuseLong = (int(__cdecl*)(const int* at, unsigned int key))0x00416563;
auto summonReleaseZRef = (void(__thiscall*)(void* zref, void* p))0x00428C44;

int GetSkillLevelDataLong(int skillId, int valueOff) {
    SkillInfo* si = SkillInfo::GetInstance();
    if (!si) {
        return 0;
    }
    SKILLENTRY* skill = si->GetSkill(skillId);
    if (!skill) {
        return 0;
    }

    // Player's learned level in this skill (clamped to master level -> always a valid level-data
    // index). GetCharacterData returns a ZRef<CharacterData>; CharacterData* is at zref[1].
    void* zref[2] = { nullptr, nullptr };
    GetCharacterData(CWvsContext::GetInstance(), zref);
    void* charData = zref[1];
    int level = charData ? pGetSkillLevel(reinterpret_cast<int>(si), charData, skillId, 0) : 0;
    if (zref[1]) {
        summonReleaseZRef(zref, nullptr);
    }
    if (level <= 0) {
        return 0;
    }

    int levelData = summonGetLevelData(skill, level);
    if (!levelData || IsBadReadPtr(reinterpret_cast<void*>(levelData), valueOff + 0xC)) {
        return 0;
    }
    return summonSecureFuseLong(reinterpret_cast<const int*>(levelData + valueOff),
            *reinterpret_cast<unsigned int*>(levelData + valueOff + 8));
}

int GetSkillMobCount(int skillId) {
    return GetSkillLevelDataLong(skillId, 0x130);
}

// ===== Summon pull =========================================================================
// Drags every mob a summon hits toward the summon. Client-side: mob positions are driven by
// whichever client the server made the mob's controller, and that client reports them back in
// its normal mob-move packets -- so nudging a mob we control propagates to the server and to
// everyone else for free. Mobs controlled by another player are skipped: our nudge would be
// cosmetic and their next move packet would snap it back.
//
// Hit set comes from the summon damage hooks (summonPDamage_hook / summonMDamage_hook), which
// the engine already calls once per hit mob inside CSummoned::TryDoingAttackManual with
// a3 == &mob->m_stat. Bracketing that call lets us collect the exact mobs that got hit
// without touching the attack loop itself.
//
// Add a summon skill id here to give it the pull, e.g. 3211002 (Silver Hawk) / 2311006
// (Summon Dragon) / 5211001 (Octopus). Empty = feature dormant, every summon behaves as before.
static const std::vector<int> g_summonPullSkills = {

};

// Tunables. Each hit closes summonPullPercent of the remaining gap; 100 = snap the mob onto
// the summon. summonPullMaxStep caps how far one hit can move a mob (0 = uncapped), so
// lowering the percent and setting a cap turns the vacuum back into a gradual drag.
int summonPullPercent = 100;  // % of the gap closed per hit
int summonPullMaxStep = 0;    // px, per-hit cap (0 = no cap)
int summonPullMaxDy   = 150;  // px, skip mobs this far above/below the summon (other platform)
int summonPullDeadzone = 12;  // px, close enough - stop nudging

// CMob::IsActive (0x00663922) == "this client controls the mob" (CMobPool::SetLocalMob ->
// SetActive(1), SetRemoteMob -> SetActive(0)).
auto CMob_IsActive = (int(__thiscall*)(void*))0x00663922;

// The client's own secure-double codec. These MUST be used for CVecCtrl position fields
// instead of the ZtlSecure template in secure.h: the template writes a checksum the client
// does not recompute the same way, and the client throws its secure-data exception on the
// next read (the "error code 5 / Access is denied" box). Same pair CVecCtrl__CalcFloat_hook
// below uses. Layout is ZtlSecure<double> { double at[2]; uint cs; } -> cs sits at at+0x10.
static auto pullFuseDouble = reinterpret_cast<double(__cdecl*)(double* at, unsigned int uCS)>(0x00539338);
static auto pullTearDouble = reinterpret_cast<unsigned int(__fastcall*)(double* at, double t)>(0x005393B6);

static double vecCtrlGetSecure(CVecCtrl* vc, unsigned int off) {
    char* p = reinterpret_cast<char*>(vc) + off;
    return pullFuseDouble(reinterpret_cast<double*>(p), *reinterpret_cast<unsigned int*>(p + 0x10));
}

static void vecCtrlSetSecure(CVecCtrl* vc, unsigned int off, double value) {
    char* p = reinterpret_cast<char*>(vc) + off;
    *reinterpret_cast<unsigned int*>(p + 0x10) = pullTearDouble(reinterpret_cast<double*>(p), value);
}

// Mob physics. CMob keeps two pointers into its vec ctrl and CMob::SetActive uses BOTH:
//   [CMob+0x118]-0xC  -- the block holding the live position/foothold as shorts at +0x1F2..
//   [CMob+0x11C]-0xC  -- the object whose vtable slot 1 is CVecCtrl::SetActive
// Repositioning a mob means handing SetActive a new x, exactly like the engine does when the
// server grants us control. Writing the secure doubles (m_x) instead does NOT work: physics
// recomputes them from the path every tick, so the write is gone within a frame.
static char* mobVecCtrlPath(Mob* mob) {
    if (!mob || IsBadReadPtr(mob, 0x120)) {
        return nullptr;
    }
    char* p = *reinterpret_cast<char**>(reinterpret_cast<char*>(mob) + 0x118) - 0xC;
    return (p && !IsBadWritePtr(p, 0x200)) ? p : nullptr;
}

static char* mobVecCtrlObj(Mob* mob) {
    if (!mob || IsBadReadPtr(mob, 0x120)) {
        return nullptr;
    }
    char* p = *reinterpret_cast<char**>(reinterpret_cast<char*>(mob) + 0x11C) - 0xC;
    return (p && !IsBadReadPtr(p, sizeof(void*))) ? p : nullptr;
}

// Boss flag out of the mob template (+0x64, secure 0/1). Unreadable mob or template -> not a boss,
// so a bad pointer never turns into a displacement we then apply to garbage.
static bool mobIsBoss(Mob* mob) {
    if (!mob || IsBadReadPtr(mob, 0x190)) {
        return false;
    }
    MobTemplate* tmpl = mob->m_pTemplate;
    if (!tmpl || IsBadReadPtr(tmpl, sizeof(MobTemplate))) {
        return false;
    }
    return tmpl->bIsBoss.Fuse() != 0;
}

// CWvsPhysicalSpace2D::GetFoothold on the singleton at 0x00BEBFA0.
auto space2dGetFoothold = (void*(__thiscall*)(void*, unsigned long))0x0050D811;

// CVecCtrl::SetActive(bActive, x, y, a4, a5, a6, pFoothold) -- vtable slot 1.
typedef void(__thiscall* VecCtrlSetActive_t)(void*, int, int, int, int, int, int, void*);

// Move a mob to nX using the engine's own reseat path.
static bool mobSetPosX(Mob* mob, int nX) {
    char* path = mobVecCtrlPath(mob);
    char* obj = mobVecCtrlObj(mob);
    if (!path || !obj) {
        return false;
    }
    const int y   = *reinterpret_cast<short*>(path + 0x1F4);
    const int a4  = *reinterpret_cast<short*>(path + 0x1F6);
    const int a5  = *reinterpret_cast<short*>(path + 0x1F8);
    const int a6  = *reinterpret_cast<unsigned char*>(path + 0x1FA);
    const unsigned long fhId = *reinterpret_cast<unsigned short*>(path + 0x1FC);
    void* pSpace = *reinterpret_cast<void**>(0x00BEBFA0);
    if (!pSpace) {
        return false;
    }
    void* pFoothold = space2dGetFoothold(pSpace, fhId);
    void** vt = *reinterpret_cast<void***>(obj);
    if (!vt || IsBadReadPtr(vt, 2 * sizeof(void*))) {
        return false;
    }
    reinterpret_cast<VecCtrlSetActive_t>(vt[1])(obj, 1, nX, y, a4, a5, a6, pFoothold);
    // Belt and braces: also poke the fields the engine reads back, on both candidate objects.
    // CVecCtrl::SetActive writes the secure doubles at +0x20/+0x38 and re-inits the CMovePath
    // at +0x1AC, but the position shorts at +0x1F2 are what CMob::SetActive reads, so keep
    // them in step rather than assuming which one physics rebuilds from.
    *reinterpret_cast<short*>(path + 0x1F2) = static_cast<short>(nX);
    vecCtrlSetSecure(reinterpret_cast<CVecCtrl*>(path), 0x20, static_cast<double>(nX));
    if (obj != path) {
        vecCtrlSetSecure(reinterpret_cast<CVecCtrl*>(obj), 0x20, static_cast<double>(nX));
    }
    return true;
}

// Cached render position. CMob::GetPos is `lea eax,[ecx+0x50C]` on the +4 base -> POINT at
// CMob+0x510. Kept in sync with the physics x so the attack packet encoded later in this same
// frame (it encodes GetPos) agrees with where we just put the mob.
static POINT* mobCachedPos(Mob* mob) {
    return reinterpret_cast<POINT*>(reinterpret_cast<char*>(mob) + 0x510);
}

// Position of any game object with the +4 IGObj base (CMob, CSummoned): vtable slot 4 is
// GetPos and returns a pointer to the object's POINT.
static const POINT* gobjPos(void* pObj) {
    if (!pObj || IsBadReadPtr(pObj, 8)) {
        return nullptr;
    }
    char* base = reinterpret_cast<char*>(pObj) + 4;
    void** vt = *reinterpret_cast<void***>(base);
    if (!vt || IsBadReadPtr(vt, 5 * sizeof(void*))) {
        return nullptr;
    }
    using GetPos_t = const POINT*(__thiscall*)(void*);
    const POINT* p = reinterpret_cast<GetPos_t>(vt[4])(base);
    return (p && !IsBadReadPtr(p, sizeof(POINT))) ? p : nullptr;
}

static bool isPullSummon(int nSkillID) {
    return std::find(g_summonPullSkills.begin(), g_summonPullSkills.end(), nSkillID)
            != g_summonPullSkills.end();
}

static void* g_pPullSummon = nullptr;    // non-null only inside a pulling summon's attack
static std::vector<Mob*> g_pullTargets;

// Called from the summon damage hooks, once per mob the summon hit.
void SummonPull_OnHit(MobStat* pStat) {
    if (!g_pPullSummon || !pStat) {
        return;
    }
    Mob* mob = reinterpret_cast<Mob*>(reinterpret_cast<char*>(pStat) - 0x1A0);
    if (IsBadReadPtr(mob, sizeof(Mob))) {
        return;
    }
    if (std::find(g_pullTargets.begin(), g_pullTargets.end(), mob) == g_pullTargets.end()) {
        g_pullTargets.push_back(mob);
    }
}

static void applySummonPull(void* pSummon) {
    const POINT* summonPos = gobjPos(pSummon);
    if (!summonPos) {
        LogInfo("SummonPull: no summon pos, targets=%d", (int)g_pullTargets.size());
        return;
    }
    for (Mob* mob : g_pullTargets) {
        if (IsBadReadPtr(mob, sizeof(Mob)) || !CMob_IsActive(mob)) {
            LogInfo("SummonPull: mob=%p skipped (bad ptr or not controlled by us)", mob);
            continue; // someone else controls it - moving it here would just desync
        }
        MobTemplate* tmpl = mob->m_pTemplate;
        if (tmpl && !IsBadReadPtr(tmpl, sizeof(MobTemplate)) && tmpl->bIsBoss.Fuse()) {
            continue; // bosses stay put
        }
        char* path = mobVecCtrlPath(mob);
        if (!path) {
            continue;
        }
        const double x = *reinterpret_cast<short*>(path + 0x1F2);
        const double y = *reinterpret_cast<short*>(path + 0x1F4);
        // Self-check: the path position must agree with the mob's cached render POINT. If it
        // doesn't, the block we resolved isn't this mob's -- skip rather than corrupt it.
        const POINT* cached = mobCachedPos(mob);
        if (fabs(x - cached->x) > 64.0 || fabs(y - cached->y) > 64.0) {
            LogInfo("SummonPull: mob=%p skipped (path/cached mismatch %.0f,%.0f vs %d,%d)",
                    mob, x, y, cached->x, cached->y);
            continue;
        }
        if (fabs(y - summonPos->y) > summonPullMaxDy) {
            LogInfo("SummonPull: mob=%p skipped (dy=%.0f > %d)", mob, fabs(y - summonPos->y), summonPullMaxDy);
            continue; // different platform
        }
        const double dx = summonPos->x - x;
        if (fabs(dx) <= summonPullDeadzone) {
            continue;
        }
        double step = dx * (summonPullPercent / 100.0);
        if (summonPullMaxStep > 0) {
            if (step > summonPullMaxStep) {
                step = summonPullMaxStep;
            } else if (step < -summonPullMaxStep) {
                step = -summonPullMaxStep;
            }
        }
        const double newX = x + step;
        if (!mobSetPosX(mob, static_cast<int>(newX))) {
            LogInfo("SummonPull: mob=%p reseat failed", mob);
            continue;
        }
        mobCachedPos(mob)->x = static_cast<LONG>(newX);
        // Read straight back: tells us whether CVecCtrl::SetActive took at all, versus taking
        // and then being reverted before the next attack (server mob-move for a mob we don't
        // actually control). ctrl = CMob+0x130 secure long: >0 ours, <0 someone else's.
        const int readBack = *reinterpret_cast<short*>(path + 0x1F2);
        const double dblX = pullFuseDouble(reinterpret_cast<double*>(path + 0x20),
                *reinterpret_cast<unsigned int*>(path + 0x30));
        const int ctrl = summonSecureFuseLong(
                reinterpret_cast<const int*>(reinterpret_cast<char*>(mob) + 0x130),
                *reinterpret_cast<unsigned int*>(reinterpret_cast<char*>(mob) + 0x138));
        LogInfo("SummonPull: mob=%p %.0f -> %.0f path=%p obj=%p short=%d dbl=%.0f ctrl=%d",
                mob, x, newX, path, mobVecCtrlObj(mob), readBack, dblX, ctrl);
    }
}

auto summonTryDoingAttackManual = (void(__thiscall*)(void*, int))0x007A4D42;
void __fastcall summonTryDoingAttackManual_hook(void* pSummon, void* edx, int tCur) {
    // this[45] (+0xB4) is the summon's skill id.
    const int nSkillID = (pSummon && !IsBadReadPtr(pSummon, 0xB8))
            ? *reinterpret_cast<int*>(reinterpret_cast<char*>(pSummon) + 0xB4) : 0;
    if (!isPullSummon(nSkillID)) {
        // Rate-limited so the log stays readable while still showing what id a summon reports.
        static DWORD lastLog = 0;
        const DWORD now = GetTickCount();
        if (now - lastLog > 2000) {
            lastLog = now;
            LogInfo("SummonPull: attack from summon skill=%d (not in pull list)", nSkillID);
        }
        summonTryDoingAttackManual(pSummon, tCur);
        return;
    }
    LogInfo("SummonPull: attack from pull summon skill=%d", nSkillID);
    g_pullTargets.clear();
    g_pPullSummon = pSummon;
    summonTryDoingAttackManual(pSummon, tCur);
    g_pPullSummon = nullptr;
    applySummonPull(pSummon);
    g_pullTargets.clear();
}

// ===== Rising Toss for other skills ========================================================
// CMob::OnHit already owns mob displacement: every hit ends in
//   CMob::GenerateMovePath(mob, a4, a5==0, 0, a9, a10, a12, a13, a14, bToss)
// and that last flag is what turns the normal knockback into the Rising Toss launch. The
// engine computes it (var_14 @ ebp-0x14) as roughly
//   (action == 10 || ...) && GetSkillLevel(charData, 21110003) && !isBoss-ish && ...
// i.e. gated on the player having Rising Toss learned. We cave the `push [ebp-0x14]` that
// feeds it to GenerateMovePath (0x00668D8E) and substitute a 1 when the skill that caused
// this hit is in the list below -- so any skill can toss, using the engine's own physics
// rather than us writing mob coordinates.
//
// The skill id of the hit is CMob::OnHit's arg_4 (ebp+0x70).
static const std::vector<int> g_tossSkills = {
    5101010, 1511009
};

static bool g_forceToss = false;

void __cdecl TossSkillCheck(int nSkillID, int nMobAction) {
    g_forceToss = std::find(g_tossSkills.begin(), g_tossSkills.end(), nSkillID) != g_tossSkills.end();
    // The flag only takes effect when GenerateMovePath sees mob action 6..8 (the hit actions)
    // and an action delay >= 90, so log both to see what the hits actually carry.
    static DWORD lastLog = 0;
    const DWORD now = GetTickCount();
    if (g_forceToss || now - lastLog > 1000) {
        lastLog = now;
        LogInfo("Toss: skill=%d mobAction=%d force=%d", nSkillID, nMobAction, (int)g_forceToss);
    }
}

// Entry probe for CMob::OnHit (called only from CMob::Update, once per queued hit). Dumps the
// values every outer condition in front of the displacement block tests, so we can see which
// one rejects instead of guessing:
//   attacker vs local char id (CUserLocal+0x11A8), mob ctrl state (mob+0x130 secure),
//   a10 (must be non-zero), and moveAbility (m_pTemplate+0x40 secure; 0 = immovable, which
//   sends the hit down the "effect only" path and never displaces).
void __cdecl OnHitLog(void* pMob, int nAttacker, int nSkillID, int a10) {
    static DWORD lastLog = 0;
    const DWORD now = GetTickCount();
    if (now - lastLog < 250) {
        return;
    }
    lastLog = now;
    int localId = 0;
    if (void* pUser = *reinterpret_cast<void**>(0x00BEBF98)) {
        localId = *reinterpret_cast<int*>(reinterpret_cast<char*>(pUser) + 0x11A8);
    }
    int ctrl = 0;
    int moveAbility = -1;
    if (pMob && !IsBadReadPtr(pMob, 0x190)) {
        char* mob = reinterpret_cast<char*>(pMob);
        ctrl = summonSecureFuseLong(reinterpret_cast<const int*>(mob + 0x130),
                *reinterpret_cast<unsigned int*>(mob + 0x138));
        char* tmpl = *reinterpret_cast<char**>(mob + 0x188);
        if (tmpl && !IsBadReadPtr(tmpl, 0x50)) {
            moveAbility = summonSecureFuseLong(reinterpret_cast<const int*>(tmpl + 0x40),
                    *reinterpret_cast<unsigned int*>(tmpl + 0x48));
        }
    }
    LogInfo("OnHit: skill=%d attacker=%d local=%d ctrl=%d a10=%d moveAbility=%d",
            nSkillID, nAttacker, localId, ctrl, a10, moveAbility);
}

static DWORD dwOnHitRet = 0x00668B88;

void __declspec(naked) OnHitEntryCave() {
    __asm {
        pushad
        mov     ebx, [esp + 0x20 + 0x04]    ; a2  = attacker char id
        mov     edx, [esp + 0x20 + 0x08]    ; a3  = skill id
        mov     esi, [esp + 0x20 + 0x24]    ; a10
        push    esi
        push    edx
        push    ebx
        push    ecx                         ; this = CMob*
        call    OnHitLog
        add     esp, 16
        popad
        mov     eax, 0x00AA1A50             ; overwritten instruction (EH prolog handler)
        jmp     [dwOnHitRet]
    }
}

// The gate in front of that call. CMob::OnHit only reaches GenerateMovePath when the mob's
// action timer (mob+0x3CC) is zero, the skill is one of three hardcoded ids, or the player is
// an Aran (job/100 == 21) / job 2000. That is why Rising Toss works and nothing else does --
// on any other class the whole hit-displacement path is skipped. This cave sits on the job
// lookup at 0x00668D72 and jumps straight to the call for skills in our list, which is exactly
// what the Aran branch does.
// Measured with the entry probe, a normal (non-Aran) hit looks like:
//   skill=5101010 attacker=6 local=6 ctrl=-3 a10=0 moveAbility=1
// so the branch dies on its first two tests -- `a10` is 0, and the mob control state is -3
// (another client controls it; the engine only wants -2 or > 0). Everything downstream,
// including the Aran job gate, is unreachable. This cave replaces the whole condition chain
// at 0x00668CC0: keep the "hit came from us" test, then for a listed skill jump straight to
// the GenerateMovePath call site, which is where TossFlagCave substitutes the toss flag.
// Reaching the engine's own call site turned out to be a dead end: every hit fails the branch
// on `a10 == 0` and `ctrl == -3` long before it, and a cave over the whole chain never ran
// either. So skip CMob::OnHit entirely and call the displacement primitive ourselves, from the
// per-hit entry (sub_66B05E, the "mob got hit by X" queue point, whose arg_0 is the attacker
// char id and arg_4 the skill id).
//
// Argument mapping recovered from the engine's own call
//   GenerateMovePath(mob, a4, a5==0, 0, a9, a10, a12, a13, a14, bToss):
//   a2 = mob hit action (must be 6..8 for the toss branch)
//   a3 = 64-bit, passed as two slots
//   a5 = hit action type; 10 is the Aran toss that calls sub_9BECC1(dbl_AF82B8)
//   a6 = attacker x, a9 = the toss flag (sub_9BECC1(dbl_AF82B0))
auto MobGenerateMovePath = (void(__thiscall*)(void*, int, int, int, int, int, int, int, int, int))0x0066B6FC;

void __cdecl TossApply(void* pMob, int nAttacker, int nSkillID) {
    if (std::find(g_tossSkills.begin(), g_tossSkills.end(), nSkillID) == g_tossSkills.end()) {
        return;
    }
    void* pUser = *reinterpret_cast<void**>(0x00BEBF98);
    if (!pUser || !pMob || IsBadReadPtr(pMob, 0x190)) {
        return;
    }
    if (nAttacker != *reinterpret_cast<int*>(reinterpret_cast<char*>(pUser) + 0x11A8)) {
        return; // only our own hits toss
    }
    if (mobIsBoss(reinterpret_cast<Mob*>(pMob))) {
        return; // bosses don't get launched -- the engine's own toss flag excludes them too
    }
    int userX = 0;
    if (IWzVector2D* pvc = reinterpret_cast<CUserLocal*>(pUser)->m_pvc) {
        if (CVecCtrl* vc = CVecCtrl::FromInterface(pvc)) {
            userX = static_cast<int>(pullFuseDouble(reinterpret_cast<double*>(reinterpret_cast<char*>(vc) + 0x20),
                    *reinterpret_cast<unsigned int*>(reinterpret_cast<char*>(vc) + 0x30)));
        }
    }
    LogInfo("Toss: applying to mob=%p skill=%d userX=%d", pMob, nSkillID, userX);
    MobGenerateMovePath(pMob, 6, 0, 0, 0, 10, userX, 0, 0, 1);
}

// ===== Knockback for other skills ==========================================================
// Same primitive as the toss, but down GenerateMovePath's plain hit-displacement path instead
// of the Aran launch: a2 = 6 (a hit action, required for the displacement switch), a5 = 8 (the
// 700-unit horizontal push with no lift), a9 = 0 (not a toss). The engine takes the SIGN of that
// push from a3 -- non-zero pushes +x, zero pushes -x -- so we set it from where the mob stands
// relative to us and the mob always flies away, never through the player.
static const std::vector<int> g_knockbackSkills = {
    1211000, // Charged Blow
};

void __cdecl KnockbackApply(void* pMob, int nAttacker, int nSkillID, int nHitIdx) {
    if (nHitIdx != 0) {
        return; // one push per attack, not one per hit of a multi-hit
    }
    if (std::find(g_knockbackSkills.begin(), g_knockbackSkills.end(), nSkillID)
            == g_knockbackSkills.end()) {
        return;
    }
    void* pUser = *reinterpret_cast<void**>(0x00BEBF98);
    if (!pUser || !pMob || IsBadReadPtr(pMob, 0x190)) {
        return;
    }
    if (nAttacker != *reinterpret_cast<int*>(reinterpret_cast<char*>(pUser) + 0x11A8)) {
        return; // only our own hits knock back
    }
    int userX = 0;
    if (IWzVector2D* pvc = reinterpret_cast<CUserLocal*>(pUser)->m_pvc) {
        if (CVecCtrl* vc = CVecCtrl::FromInterface(pvc)) {
            userX = static_cast<int>(vecCtrlGetSecure(vc, 0x20));
        }
    }
    // Mob x is the short at +0x1F2 of the vec-ctrl path block (the one mobSetPosX reseats).
    char* path = mobVecCtrlPath(reinterpret_cast<Mob*>(pMob));
    if (!path) {
        return;
    }
    const int mobX = *reinterpret_cast<short*>(path + 0x1F2);
    MobGenerateMovePath(pMob, 6, mobX >= userX ? 1 : 0, 0, 0, 8, userX, 0, 0, 0);
}

static DWORD dwHitQueueRet = 0x0066B063;

void __declspec(naked) HitQueueCave() {
    __asm {
        pushad
        mov     ebx, [esp + 0x20 + 0x04]    ; arg_0 = attacker char id
        mov     edx, [esp + 0x20 + 0x08]    ; arg_4 = skill id
        push    edx
        push    ebx
        push    ecx                         ; this = CMob*
        call    TossApply
        add     esp, 12
        mov     ecx, [esp + 0x18]           ; this = CMob*, reloaded (cdecl call clobbered it)
        mov     ebx, [esp + 0x20 + 0x04]    ; arg_0 = attacker char id
        mov     edx, [esp + 0x20 + 0x08]    ; arg_4 = skill id
        mov     eax, [esp + 0x20 + 0x20]    ; arg_14 hit index (the [ebp+24h] the engine reads)
        push    eax
        push    edx
        push    ebx
        push    ecx
        call    KnockbackApply
        add     esp, 16
        popad
        mov     eax, 0x00AA1A50             ; overwritten instruction (EH prolog handler)
        jmp     [dwHitQueueRet]
    }
}

static DWORD dwTossFlagRet = 0x00668D93;

void __declspec(naked) TossFlagCave() {
    __asm {
        pushad
        push    dword ptr [ebp + 0x78]      ; arg_C = mob hit action
        push    dword ptr [ebp + 0x70]      ; arg_4 = skill id of this hit
        call    TossSkillCheck
        add     esp, 8
        popad
        cmp     byte ptr [g_forceToss], 0
        jz      keepOriginal
        push    1                           ; force the Rising Toss launch
        jmp     done
    keepOriginal:
        push    dword ptr [ebp - 0x14]      ; overwritten instruction: engine's own flag
    done:
        xor     eax, eax                    ; overwritten instruction
        jmp     [dwTossFlagRet]
    }
}

auto loadSummonAttackInfo = (int(__thiscall*)(void*, int, void*, int))0x007ACB5A;
int __fastcall loadSummonAttackInfo_hook(void* thisCSB, void* edx, int retbuf, void* bstr, int attackIdx) {
    int ret = loadSummonAttackInfo(thisCSB, retbuf, bstr, attackIdx);
    int attackInfo = *reinterpret_cast<int*>(ret + 4);
    // bstr.m_Data holds the summon skill id. Regular octopus/bullet summons (octopus()/sub_766612)
    // worked fine BEFORE these edits precisely because the original code left their attack-info
    // untouched: forcing them onto the +0x80 rect path (and overriding mobCount/reach) is what broke
    // them. So only modify attack-info for summons we actually want changed: non-octopus summons (the
    // original behavior) PLUS the ltrb custom octopus summons (which want multi-hit and are covered by
    // the summonNullBulletGuard cave). Untouched regular octopus => behaves exactly as if this hook
    // were absent.
    bool modify = !octopus(reinterpret_cast<int>(bstr)) || ltrbOcto(reinterpret_cast<int>(bstr));
    if (modify && attackInfo && !IsBadWritePtr(reinterpret_cast<void*>(attackInfo), 0x88)) {
        // v7[13] (+0x34) is the summon's horizontal reach (sweep is +-this from the summon).
        *reinterpret_cast<int*>(attackInfo + 0x34) = summonReach;

        // Override mobCount (+0x24, v7[9]) with the skill's WZ mobCount so the summon hits as many
        // mobs as the skill says. ONLY accept a sane [1,14] value: GetSkillMobCount reads
        // SKILLLEVELDATA +0x130, and skills with no `mobCount` level node read a garbage slot;
        // v140.bSelfDestruct (= this field) caps FindHitMobInRect's writes into fixed 15-slot stack
        // arrays, so an out-of-range value would smash the stack. Out of range -> keep the WZ default.
        int mobCount = GetSkillMobCount(reinterpret_cast<int>(bstr));
        if (mobCount >= 1 && mobCount <= 14) {
            *reinterpret_cast<int*>(attackInfo + 0x24) = mobCount;
        }

        // Area-attack flag (+0x80, v7[32]): take the rect (FindHitMobInRect) multi-hit path.
        *reinterpret_cast<int*>(attackInfo + 0x80) = 1;
    }
    return ret;
}

// Summon seek code cave. Replaces the 4 coordinate lea/push pairs in sub_678ECC (0x678EDB..0x678EF1)
// that build the player +-300/+-100 mob-search box. Rebuilds the same 4 pushes (bottom,right,top,left
// -- the arg order dword_BF040C expects) but with the configurable summonSeekRange X/Y, then jumps
// back to the &rect push + call. At cave entry edi = playerY, ebx = playerX (loaded just above), and
// ecx is not yet touched (saved after our region), so only eax is clobbered -- safe.
// Attack-follow gate check, called from the summonSeekRect cave before each seek. Recomputes
// summonSeekGateOpen so the naked asm only has to test a global (no register juggling around the
// tick math). Not static: referenced by name from inline asm.
void __cdecl UpdateSummonSeekGate() {
    summonSeekGateOpen = summonFollowWindowMs <= 0
            || GetTickCount() - lastPlayerAttackTick <= (DWORD)summonFollowWindowMs;
}

DWORD summonSeekRectBack = 0x00678EF1;
void __declspec(naked) summonSeekRect() {
    __asm {
        pushad // C call clobbers eax/ecx/edx; edx liveness here unknown, so save everything
        call UpdateSummonSeekGate
        popad
        cmp summonSeekGateOpen, 0
        jne seekOpen
        // Gate shut (player hasn't attacked within summonFollowWindowMs): push a degenerate box at
        // far-off coords (all four edges = 0x7FFF0000) so the mob finder matches nothing and the
        // summon idles. Same 4-push shape as the open path, so stack layout is identical.
        mov eax, 0x7FFF0000
        push eax
        push eax
        push eax
        push eax
        jmp [summonSeekRectBack]
    seekOpen:
        mov eax, edi
        add eax, summonSeekRangeY // bottom = playerY + rangeY
        push eax
        mov eax, ebx
        add eax, summonSeekRangeX // right  = playerX + rangeX
        push eax
        mov eax, edi
        sub eax, summonSeekRangeY // top    = playerY - rangeY
        push eax
        mov eax, ebx
        sub eax, summonSeekRangeX // left   = playerX - rangeX
        push eax
        jmp [summonSeekRectBack]
    }
}

auto hook_bstr_t = (void(__thiscall*)(void*, const char*))0x00425ADD;

void(__fastcall bstrt)(void* Level, void* blah, const char* a2) {
    using namespace std;
    int tMAD = topMAD;
    int bMAD = botMAD;
    int getMad = totmagic;
    int getPad = pad;
    string toMad = to_string(bMAD) + " ~ " + to_string(tMAD);
    string magicStr = to_string(getMad);
    string padStr = to_string(getPad);
    const char* magicchar = magicStr.c_str();
    const char* sussychar = toMad.c_str();
    const char* weaponchar = padStr.c_str();
    int job = CWvsContext::GetInstance()->get_m_basicStat().nJob.Fuse();
    bool isMage = (job >= 200 && job < 300);
    if ((int)_ReturnAddress() == 0X008C35C9) {

        setMAD();
        if (isMage) {
            a2 = magicchar;
        } else {
            a2 = weaponchar;
        }
    }
    if ((int)_ReturnAddress() == 0X008C3400) {
        if (isMage) {
            a2 = sussychar;
        }
    }
    return hook_bstr_t(Level, a2);
}

auto DoActiveSkill_Prepare = (int(__thiscall*)(void*, int*, int, int))0x0096A86E;

int(__fastcall DoActiveSkill_Prepare_t)(void* _this, void* edx, int* pskill, int sLv, int scanCode) {
    return DoActiveSkill_Prepare(_this, pskill, sLv, scanCode);
}

auto is_keydown_skill = (int(__cdecl*)(int))0x004FB08F;

int(__cdecl is_keydown_skill_t)(int nSkillID) {
    if (nSkillID == 3121004 || nSkillID == 5221004 || nSkillID == 3111009) {
        return 1;
    }
    if (nSkillID == 1201013 || nSkillID == 1201016 || nSkillID == 2411011) {
        return 1;
    }
    return is_keydown_skill(nSkillID);
}

auto GetOneTimeAction = (int(__thiscall*)(void*))0x00451B6A;

int(__fastcall tGetOneTimeAction)(void* _this) {
    int ota = GetOneTimeAction(_this);
    // Only track the LOCAL player's avatar, which lives at CUserLocal::ms_pInstance + 0x88 (see
    // sub_74CB84 @ 0x74cc73: GetOneTimeAction(ms_pInstance + 136)). ota > -1 means an attack/skill
    // one-time action is currently playing; -1 means none.
    void* localUser = *reinterpret_cast<void**>(0x00BEBF98);
    if (localUser && _this == reinterpret_cast<char*>(localUser) + 0x88) {
        g_inOneTimeAction = (ota > -1);
    }
    return ota;
}

auto SetOneTimeAction = (int(__thiscall*)(int, int))0x004571AB;

int(__fastcall tSetOneTimeAction)(int _this, void* fuckyou, int a2) {
    return SetOneTimeAction(_this, a2);
}


auto OnResolveMoveAction = (int(__thiscall*)(int, int, int, int, void*))0x00936D99;

int(__fastcall tOnResolveMoveAction)(int _this, void* edx, int nInputX, int nInputY, int nCurMoveAction, void* pvc) {
    int orma = OnResolveMoveAction(_this, nInputX, nInputY, nCurMoveAction, pvc);
    // Log("%7d", nCurMoveAction);
    if (hurricane && nCurMoveAction != 7) {
        hurricane = false;
        SetOneTimeAction(_this + 132, 99);
    }
    return orma;
}

auto get_FlipX = (int(__stdcall*)(void*, int*))0x009B61F0;

int(__stdcall tget_flipX(void* _this, int* a2)) {
    int flipX = get_FlipX(_this, a2);

    return flipX;
}

auto thingyWindArcher = (int(__cdecl*)(int))0x00766867;
int(__cdecl windarcherhook)(int a1) {
    if (a1 == 3411006 || a1 == 4121017 || a1 == 4421015 || a1 == 5521003 || a1 == 5511017) {
        return 1;
    }
    return thingyWindArcher(a1);
}

auto OnSkillKeyDownEnd = (int(__thiscall*)(void*))0x0095BEDF;

int(__fastcall tOnSkillKeyDownEnd(void* _this)) {
    hurricane = false;
    Patch1(0x0095F97A, 0x7F);     // jmp
    Patch1(0x0095F97A + 1, 0x2C); // 0095F9D5
    Patch1(0x009CBFB0, 0x7E);     // jmp
    Patch1(0x0094C3BB, 0x0F);
    Patch1(0x0094C3BB + 1, 0x8F);
    Patch1(0x0094C3BB + 2, 0x12);
    Patch1(0x0094C3BB + 3, 0x00);
    Patch1(0x0094C3BB + 4, 0x00);
    Patch1(0x0094C3BB + 5, 0x00);
    return OnSkillKeyDownEnd(_this);
}

auto OriginalCVecCtrl__CalcFloat = (signed int(__thiscall*)(void*, int))0x009B2C3C; // v83

// Wings glide: keep full horizontal speed through a direction switch.
//
// The engine keeps steering: CalcFloat turns the player's movement keys into m_vx itself, through
// CInputSystem/DirectInput, so it honours remapped keys -- GetAsyncKeyState(VK_LEFT/RIGHT) reads
// false here and can't be used. What CalcFloat also does is ramp m_vx down to 0 and back up when
// the input direction flips, and bleed it off on the neutral frames between the key release and
// the opposite key press. That ramp is the lost momentum.
//
// So we take only the SIGN from the engine (that is the steering) and supply the MAGNITUDE
// ourselves: hold the glide's peak speed and re-apply it every frame. The turn then lands on the
// first frame the engine's m_vx crosses zero, at full speed, with no rebuild.
static CVecCtrl* s_wingsOwner = nullptr;
static double s_wingsMag = 0.0;   // magnitude held across the whole glide
static int s_wingsSign = 1;       // direction the movement keys last asked for
static bool s_wingsArmed = false; // true once a direction key is pressed; until then, hands off
static double s_wingsSaved = 0.0; // speed parked by the UP brake, handed back on the next steer

// ms_pInstance for CUserLocal (same read as the Master Skies airborne check above).
static CVecCtrl* GetLocalVecCtrl() {
    CUserLocal* localUser = *reinterpret_cast<CUserLocal**>(0x00BEBF98);
    return localUser ? CVecCtrl::FromInterface(localUser->m_pvc) : nullptr;
}

// Measured on this client (CalcFloat telemetry, 30ms frames):
//   * m_vx at 0x50 is what actually moves the player and our post-call write sticks -- a forced
//     300.0 came back as 299.970 on the next frame, so float drag is only ~0.03/frame.
//   * A glide carries its entry momentum unchanged (-204.000 held flat for seconds), but the
//     wings' OWN steering is worth about 9. Flip direction and the engine ramps m_vx across zero
//     at ~24 per 100ms and then settles at that 9: the inherited speed is gone for good. That
//     ramp is the lost momentum.
//   * GetAsyncKeyState reads LEFT/RIGHT/DOWN fine here (the telemetry logged 4 / 2 / 1 / 6).
//
// So: the engine keeps deciding WHERE you go, we decide HOW FAST. Sign comes from the movement
// keys, magnitude is held at the glide's peak, and both are written every frame -- but only after
// a direction key is actually pressed, so starting the glide no longer flings the player sideways
// at full speed. Until then the engine's own physics run untouched and we just watch the speed.
static const double kWingsMinSpeed = 200.0; // stock inherited glide sits near 204; steering alone is ~9

void __fastcall CVecCtrl__CalcFloat_hook(void* this_, void* _EDX, int tElapse) {
    // Call the original function first
    OriginalCVecCtrl__CalcFloat(this_, tElapse);

    CVecCtrl* pvc = reinterpret_cast<CVecCtrl*>(this_);
    // Local player only: CalcFloat runs for every floating object, and holding a remote player's
    // speed up from here would fight the movement packets that actually drive them.
    if (!pvc || pvc != GetLocalVecCtrl()) return;

    // Wings only. CalcFloat also runs for an ordinary jump or fall (WorkUpdateActive calls it
    // whenever the walk flag at 0x110 is clear), and those should keep stock physics.
    if (!vecWingsNow(pvc)) {
        s_wingsOwner = nullptr;
        return;
    }

    const double vx = vecCtrlGetSecure(pvc, kVecVx);
    const double mag = vx < 0.0 ? -vx : vx;

    if (s_wingsOwner != pvc) { // wings just started
        s_wingsOwner = pvc;
        s_wingsMag = mag; // seeded from the speed we flew in with, then grows with the glide
        s_wingsSign = vx < 0.0 ? -1 : 1;
        s_wingsArmed = false;
        s_wingsSaved = 0.0;
    }

    if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
        // Cancel the skill: clearing the wings flag drops the float's slow-fall and hands the
        // player back to normal gravity, which is what CVecCtrl::Wings turned on when it set it.
        vecWingsNow(pvc) = 0;
        s_wingsOwner = nullptr;
        return;
    }

    const bool left = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0;
    const bool right = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;

    if ((GetAsyncKeyState(VK_UP) & 0x8000) && !left && !right) {
        // Brake: park the speed we had, drop to a standstill, and go hands-off so the glide hovers.
        // UP only brakes on its own -- held together with a direction it is just an up-press during
        // a steer, and eating the momentum there would fight the steering.
        // Guarded on > 0 because this runs every frame UP is held -- otherwise the second frame
        // would overwrite the parked speed with the zero the first frame just set.
        if (s_wingsMag > 0.0) s_wingsSaved = s_wingsMag;
        vecCtrlSetSecure(pvc, kVecVx, 0.0);
        s_wingsMag = 0.0;
        s_wingsArmed = false;
        return;
    }

    // Track the peak even before arming, so momentum carried into the glide is still ours to keep.
    if (mag > s_wingsMag) s_wingsMag = mag;

    if (left != right) {
        if (!s_wingsArmed && s_wingsSaved > s_wingsMag) {
            s_wingsMag = s_wingsSaved; // steering again after a brake resumes at the parked speed
            s_wingsSaved = 0.0;
        }
        s_wingsSign = left ? -1 : 1;
        s_wingsArmed = true;
    }
    if (!s_wingsArmed) return; // no direction asked for yet: no launch, engine keeps the wheel

    // Magnitude only ever grows: a boost mid-glide is kept, the engine's post-flip collapse to ~9
    // is not. Floor keeps a glide that started slow from crawling.
    if (s_wingsMag < kWingsMinSpeed) s_wingsMag = kWingsMinSpeed;

    vecCtrlSetSecure(pvc, kVecVx, s_wingsSign * s_wingsMag);
}


auto isMoveableSkill = (int(__cdecl*)(int))0x0095F96F;

int(__cdecl isMoveableSkillt)(int nSkillID) {
    if (nSkillID == 3111009 || nSkillID == 3121004 || nSkillID == 5221004) {
        return 1;
    }
        return isMoveableSkill(nSkillID);
}


auto _is_attack_area_set_by_data = (int(__cdecl*)(int))0x7666CB;

int(__cdecl is_attack_area_set_by_data)(int nSkillID) {
    if (nSkillID == 4101008 || nSkillID == 4111012 || nSkillID == 5101012 || nSkillID == 5111017 || nSkillID == 3111009 || nSkillID == 3411006 || nSkillID == 4121017 || nSkillID == 4421015 || nSkillID == 5521003 || nSkillID == 5511017) {
        return 1;
    }
    return _is_attack_area_set_by_data(nSkillID);
}

// Null-bullet-sprite guard for CSummoned::ProcessAttack (0x7A4424). Each frame it walks the summon's
// pending bullet ATTACKEFFECTs and calls IWzResMan::GetObjectA(path) (0x7A47E4) to load each bullet's
// sprite, where path = ATTACKEFFECT+0x20 = attackInfo+0x74 (the summon attack's bullet-effect WZ
// node). Octopus/bullet summons whose WZ lacks that node (no `ball`/effect) get a NULL path, and
// RESMAN derefs it -> 0xC0000005 (crash @ RESMAN+0x38C0). The damage was already dealt in
// TryDoingAttackManual; ProcessAttack only renders the projectile, so when the path is null we skip
// the whole render block for that effect and continue the loop. Cave entry (0x7A44FC) replicates
// `lea ecx,[ebx+1Ch]; xor edi,edi` (ebx = effect node), then null-checks [ebx+0x20]: jump to the
// loop-continue (0x7A4D27) if null, else fall back into the original path at 0x7A4501. At this point
// no COM objects have been created (v109 unwind index = -1), so skipping is unwind-safe. Runs for
// every summon's effects but only diverts when the bullet path is genuinely null.
DWORD procAttackBack = 0x007A4501; // continue original (cmp [ecx],edi; ...)
DWORD procAttackSkip = 0x007A4D27; // loop-continue (cmp [var_38],0; jnz next)
void __declspec(naked) summonNullBulletGuard() {
    __asm {
        lea ecx, [ebx+1Ch] // replicate overwritten instruction (v104 = effect+0x1C)
        xor edi, edi // replicate (edi = 0, needed by original cmp [ecx],edi)
        cmp dword ptr [ebx+20h], 0 // bullet sprite path (attackInfo+0x74 copy) null?
        jz skipRender
        jmp [procAttackBack]
    skipRender:
        jmp [procAttackSkip]
    }
}


// Battleship riding-skill gate. DoActiveSkill's default branch at 0x009673A3 calls
// CUserLocal::CheckRidingVehicle (0x0095F9D9), which blocks EVERY skill on a 190/193-range
// vehicle (battleship = 1932000 -> "not available during the ride") and ignores the skill id.
// Torpedo (5211017) falls to this default branch instead of the shipSkills whitelist, so it's
// blocked. Intercept the call site (esi = skill id is live here) and route 5211017 to the
// allow path 0x009673CF (skips the riding gate, same effect the whitelist intends); every other
// skill keeps the original behavior (call CheckRidingVehicle, then jmp 0x009693F0).
DWORD torpedoAllowPath = 0x009673CF; // post-gate skill dispatch (allowed)
DWORD torpedoBlockPath = 0x009693F0; // common continuation after CheckRidingVehicle
DWORD checkRidingVehicleFn = 0x0095F9D9;

void __declspec(naked) shipTorpedoGate() {
    __asm {
        cmp     esi, 5211017
        je      allow
        cmp     esi, 5201007
        je      allow
        cmp     esi, 5201006
        je      allow
        cmp     esi, 5211012
        je      allow
        cmp     esi, 3601001
        je      allow
        cmp     esi, 3601002
        je      allow
        cmp     esi, 3601003
        je      allow
        cmp     esi, 3601004
        je      allow
        cmp     esi, 3601005
        je      allow
        cmp     esi, 3601006
        je      allow
        mov     ecx, edi // this -> CheckRidingVehicle (__thiscall)
        call    dword ptr [checkRidingVehicleFn]
        jmp     dword ptr [torpedoBlockPath]
    allow:
        jmp     dword ptr [torpedoAllowPath]
    }
}

void InitCorsairMods() {
    Patch1(0x00967235, 0x75);
    Patch1(0x00967191 + 1, 0x84);
    // ATTACH_HOOK(dwShipSkills, shipSkills);
    //  12 bytes at 0x009673A3 (mov ecx,edi; call CheckRidingVehicle; jmp loc_9693F0); 0x009673AF
    //  is the next jump target, untouched.
}


// TryDoingMeleeAttack @ 0x950921 only calls get_cool_time for a fixed set of skill IDs selected by
// the dispatch at 0x9518CD; every other skill falls through `jmp loc_95197E` @ 0x9518F9 and gets NO
// forced attack delay (spammable). 1211000 (Charged Blow) is one of those -- so the get_cool_time_t
// override for it (800ms) was never reached on the melee path. This code-cave replaces that 5-byte
// fall-through jmp: if edx (the skill id) == 1211000 it diverts to 0x95191E (the branch that calls
// get_cool_time and folds the result into the action delay); otherwise it jmps to the original
// loc_95197E. edx is untouched, so the get_cool_time(edx) call downstream still sees the skill id.
// ===== Skill-id aliasing ====================================================================
// Gives a custom skill another skill's client behavior without renumbering it or borrowing its
// id on the wire. The client recognizes such behavior through a handful of hardcoded
// `cmp <operand>, <skill id>` tests, so each one is replaced by a cave that ALSO matches the
// alias. The cave copies the original compare byte-for-byte and only swaps the immediate, which
// keeps it independent of the operand form -- both `cmp eax, imm32` (5 bytes) and
// `cmp [ebp+var], imm32` (7 bytes) appear among these sites.
//
// On a match the cave does NOT jump to the site's branch target: it only forces ZF and returns to
// the instruction after the compare, letting the site's own jz do the jumping. That matters
// because a site may carry instructions BETWEEN its compare and that jz (0x0096AEF4 has the
// `mov [edi],1` that arms the keydown state) -- branching early would skip them. `cmp esp,esp`
// sets ZF without touching a register or memory, so nothing else has to be preserved.
//
// Takes ALL of a site's aliases at once. It must: installing twice over the same address would
// read the first cave's `jmp` back as if it were the original compare. 0x00955ED4 has two callers
// (the Ice Demon alias and the multi-mob one), so this is not hypothetical.
//
// Cave layout: for each alias [cmp <same operand>, alias / je matched] /
//              <original compare> / jmp back / matched: cmp esp,esp / jmp back.
static void InstallSkillIdAlias(DWORD cmpAddr, int cmpLen, const std::vector<int>& aliasSkills) {
    if (aliasSkills.empty())
        return;

    const SIZE_T caveSize = aliasSkills.size() * (cmpLen + 6) + cmpLen + 32;
    BYTE* cave = (BYTE*)VirtualAlloc(nullptr, caveSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!cave)
        return;

    BYTE orig[8] = {};
    memcpy(orig, (void*)cmpAddr, cmpLen); // imm32 is always the last 4 bytes of the compare

    BYTE* p = cave;
    std::vector<DWORD*> matchRels;
    for (int nAliasSkill : aliasSkills) {
        memcpy(p, orig, cmpLen);
        memcpy(p + cmpLen - 4, &nAliasSkill, 4);
        p += cmpLen;
        // je matched -- target patched once the label is known
        *p++ = 0x0F; *p++ = 0x84;
        matchRels.push_back(reinterpret_cast<DWORD*>(p));
        p += 4;
    }
    // no alias matched: the original compare, so the site's own jz sees the flags it expects
    memcpy(p, orig, cmpLen);
    p += cmpLen;
    // jmp back to the instruction after the compare
    *p++ = 0xE9;
    DWORD rel = (cmpAddr + cmpLen) - ((DWORD)p + 4);
    memcpy(p, &rel, 4); p += 4;

    // matched: cmp esp,esp -> ZF = 1 without touching a register or memory, then back to the jz
    BYTE* matched = p;
    *p++ = 0x3B; *p++ = 0xE4;
    *p++ = 0xE9;
    rel = (cmpAddr + cmpLen) - ((DWORD)p + 4);
    memcpy(p, &rel, 4); p += 4;

    for (DWORD* relPtr : matchRels) {
        *relPtr = (DWORD)matched - (reinterpret_cast<DWORD>(relPtr) + 4);
    }

    // jmp cave, padded with nops out to the full length of the compare we replaced.
    BYTE patch[8];
    memset(patch, 0x90, sizeof(patch));
    patch[0] = 0xE9;
    DWORD relToCave = (DWORD)cave - cmpAddr - 5;
    memcpy(patch + 1, &relToCave, 4);
    WriteProcessMemory(GetCurrentProcess(), (void*)cmpAddr, patch, cmpLen, nullptr);
}

// 2421006 behaves as 2221006 (Ice Demon) does, while still being 2421006 everywhere -- the id the
// server sees, the id the consume/cooldown checks use, the id its own WZ data is read from.
// These are every hardcoded 2221006 test in the client except the DoActiveSkill routing compare
// (0x00967DDB), which the doActiveSkills router already covers by sending 2421006 to magicAttack.
// 0x00955ED4 is the one that matters most: it gates TryDoingMagicAttack's multi-target block, the
// chain-spread through sub_67886D @ 0x00955F08 that fans out to the skill's mobCount.
void InstallIceDemonAlias() {
    const std::vector<int> alias = { 2421006 };
    InstallSkillIdAlias(0x0075BF73, 5, alias); // SKILLENTRY::AdjustDamageDecRate
    InstallSkillIdAlias(0x00955ED4, 7, alias); // CUserLocal::TryDoingMagicAttack
    InstallSkillIdAlias(0x0098272F, 5, alias); // CUserRemote::OnMagicAttack
    InstallSkillIdAlias(0x00982966, 5, alias); // CUserRemote::OnMagicAttack
    InstallSkillIdAlias(0x00982F9F, 7, alias); // CUserRemote::OnMagicAttack
}


void InstallMeleeCooltimeGate() {
    const DWORD patchAddr   = 0x009518F9;
    const DWORD applyAddr   = 0x0095191E;
    const DWORD defaultAddr = 0x0095197E;

    BYTE* cave = (BYTE*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!cave)
        return;

    BYTE* p = cave;

    auto EmitCmpJe = [&](DWORD skillId)
    {
        // cmp edx, skillId
        *p++ = 0x81;
        *p++ = 0xFA;
        memcpy(p, &skillId, 4);
        p += 4;

        // je applyAddr
        *p++ = 0x0F;
        *p++ = 0x84;
        DWORD rel = applyAddr - ((DWORD)p + 4);
        memcpy(p, &rel, 4);
        p += 4;
    };

    EmitCmpJe(1211000);
    EmitCmpJe(1211014);
    EmitCmpJe(1221016);
    EmitCmpJe(1221021);

    // jmp defaultAddr
    *p++ = 0xE9;
    DWORD rel = defaultAddr - ((DWORD)p + 4);
    memcpy(p, &rel, 4);
    p += 4;

    // Redirect original jump to our cave
    DWORD relToCave = (DWORD)cave - patchAddr - 5;
    BYTE patch[5] = { 0xE9 };
    memcpy(patch + 1, &relToCave, 4);

    WriteProcessMemory(GetCurrentProcess(), (void*)patchAddr, patch, 5, nullptr);
}




// Damage-number show time lives in CMob::SetDamaged (sub_66B05E @ 0x0066B05E). For the common path
// it is `showTime = base(ebp+0x10) + hitIndex(ebp+0x24) * 120` (the imul/add @ 0x0066B124). This
// cave replicates that formula, then for skill 1211000 sets the show time to base + 600 (showMs) -- so
// its damage value appears 600ms after the hit (v16 is an absolute timestamp, not a relative delay, so
// we add to the base rather than replace it). Regardless of the action animation. ebx holds the
// skill id at this point (cmp ebx,... @ 0x0066B115) and is untouched by the formula, so it is safe to
// test. Overwrites the 7-byte imul+add (0x0066B124..0x0066B12A); returns to 0x0066B12B (mov edx,esi).
void InstallDamageShowTimeGate() {
    const DWORD patchAddr  = 0x0066B124;
    const DWORD returnAddr = 0x0066B12B;
    const DWORD showMs     = 660;

    BYTE* cave = (BYTE*)VirtualAlloc(nullptr, 96, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!cave)
        return;

    BYTE* p = cave;

    // Original instructions
    // imul eax, [ebp+24h]
    *p++ = 0x0F; *p++ = 0xAF; *p++ = 0x45; *p++ = 0x24;
    // add eax, [ebp+10h]
    *p++ = 0x03; *p++ = 0x45; *p++ = 0x10;

    BYTE* jnePatch = nullptr;

    auto EmitCmpJe = [&](DWORD skillId)
    {
        // cmp ebx, skillId
        *p++ = 0x81;
        *p++ = 0xFB;
        memcpy(p, &skillId, 4);
        p += 4;

        // je override
        *p++ = 0x0F;
        *p++ = 0x84;

        DWORD* relPtr = (DWORD*)p;
        p += 4;

        if (!jnePatch)
            jnePatch = (BYTE*)relPtr;

        DWORD rel = 0; // patched later
        memcpy(relPtr, &rel, 4);
    };

    EmitCmpJe(1211000);
    EmitCmpJe(1211014);
    EmitCmpJe(1221016);
    EmitCmpJe(1221021);

    // jmp returnAddr (if no matches)
    *p++ = 0xE9;
    BYTE* noMatchTarget = p;
    DWORD rel = returnAddr - ((DWORD)p + 4);
    memcpy(p, &rel, 4);
    p += 4;

    BYTE* overrideLabel = p;

    // mov eax, [ebp+10h]
    *p++ = 0x8B;
    *p++ = 0x45;
    *p++ = 0x10;

    // add eax, showMs
    *p++ = 0x05;
    memcpy(p, &showMs, 4);
    p += 4;

    // jmp returnAddr
    *p++ = 0xE9;
    rel = returnAddr - ((DWORD)p + 4);
    memcpy(p, &rel, 4);
    p += 4;

    // Fix up all JE targets
    BYTE* scan = cave + 7;
    while (scan < noMatchTarget)
    {
        if (scan[0] == 0x81 && scan[1] == 0xFB)
        {
            BYTE* je = scan + 6;
            DWORD* relPtr = (DWORD*)(je + 2);
            DWORD r = (DWORD)overrideLabel - ((DWORD)relPtr + 4);
            *relPtr = r;
            scan = je + 6;
        }
        else
        {
            ++scan;
        }
    }

    // Patch original code
    BYTE patch[7] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90 };
    DWORD relToCave = (DWORD)cave - patchAddr - 5;
    memcpy(patch + 1, &relToCave, 4);

    WriteProcessMemory(GetCurrentProcess(), (void*)patchAddr, patch, sizeof(patch), nullptr);
}


// Skills that behave like 14101006: the mob's hit action plays on the FIRST hit of an attack only,
// and the damage-number show time stays the one already computed (v14[2]) instead of being re-derived
// from the action animation on every hit. Append skill ids here -- nothing else needs to change.
static const std::vector<int> g_singleHitActionSkills = {
    1211000, 1221016// Charged Blow (pairs with InstallDamageShowTimeGate's flat 660ms number)
};

// CMob::SetDamaged (sub_66B05E) decides per hit whether to run the `.HitAfter` block @ 0x0066B2BB:
// that block loads the mob's hit-action WZ node, re-derives the damage show time from the action
// animation and pushes a fresh damaged-action entry -- i.e. it restarts the hit action on EVERY hit
// of a multi-hit attack. A hardcoded skill-id chain @ 0x0066B287..0x0066B2AF (5121004, 5121007,
// 15111004, 14101006, 11111006, 22171002) jumps instead to 0x0066B2B1, which skips that block
// whenever hitIdx != 0 -- so those skills show the hit action once per attack.
//
// This cave prepends g_singleHitActionSkills to that chain without sacrificing an existing entry: it
// overwrites the FIRST compare (`cmp eax, 5121004` @ 0x0066B287, 5 bytes) with a jmp to a cave that
// tests our ids (je -> 0x0066B2B1), then replicates the overwritten compare and jmps back to the
// original `jz` @ 0x0066B28C, so the whole vanilla chain still runs. eax = skill id at this point.
void InstallDamageBypassGate() {
    const DWORD patchAddr  = 0x0066B287; // cmp eax, 5121004  (5 bytes: 3D EC 23 4E 00)
    const DWORD jzBack     = 0x0066B28C; // the original `jz 0x66B2B1` that follows that cmp
    const DWORD bypassTgt  = 0x0066B2B1; // chain-hit target (hitIdx != 0 -> skip the .HitAfter block)
    const DWORD origCmpImm = 0x004E23EC; // 5121004 (the cmp immediate we overwrite, replicated below)

    if (g_singleHitActionSkills.empty())
        return;

    // 11 bytes per id (cmp imm32 = 5, je rel32 = 6) + 10 for the replicated cmp and the jmp back.
    const SIZE_T caveSize = g_singleHitActionSkills.size() * 11 + 10;
    BYTE* cave = (BYTE*)VirtualAlloc(nullptr, caveSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!cave)
        return;
    BYTE* p = cave;

    for (int id : g_singleHitActionSkills) {
        // cmp eax, id
        *p++ = 0x3D;
        memcpy(p, &id, 4); p += 4;
        // je bypassTgt
        *p++ = 0x0F; *p++ = 0x84;
        DWORD rel = bypassTgt - ((DWORD)p + 4);
        memcpy(p, &rel, 4); p += 4;
    }
    // cmp eax, origCmpImm   (replicate the overwritten compare so the jz @ jzBack still fires for it)
    *p++ = 0x3D;
    memcpy(p, &origCmpImm, 4); p += 4;
    // jmp jzBack
    *p++ = 0xE9;
    DWORD rel = jzBack - ((DWORD)p + 4);
    memcpy(p, &rel, 4); p += 4;

    // Redirect the original `cmp eax, 5121004` into the cave (exactly 5 bytes).
    DWORD relToCave = (DWORD)cave - patchAddr - 5;
    BYTE patch[5] = { 0xE9 };
    memcpy(patch + 1, &relToCave, 4);
    WriteProcessMemory(GetCurrentProcess(), (void*)patchAddr, patch, 5, nullptr);
}

int apReset = 0x008C7B27;
void _declspec(naked)removeHP() {
    _asm {
        push 999999
        push edi
        push 2000
        jmp[apReset]
    }
}


void AttachSkillEdits() {
    // Skip bullet-sprite render for summons whose attack has no bullet-effect WZ node (null path),
    // else IWzResMan::GetObjectA derefs null -> crash. 5 bytes (lea ecx,[ebx+1Ch]; xor edi,edi).
    CodeCave((void*)summonNullBulletGuard, 0x007A44FC, 0);
    // ATTACH_HOOK(MesoFormula, mesoFormulaHook);
    ATTACH_HOOK(getPAD, getPAD_hook);
    ATTACH_HOOK(hook_bstr_t, bstrt);
    ATTACH_HOOK(OriginalCVecCtrl__CalcFloat, CVecCtrl__CalcFloat_hook);
    ATTACH_HOOK(CUserLocal__IsInvincible, CUserLocal__IsInvincible_Hook);
    ATTACH_HOOK(MobPDamage, MobPDamage_Hook);
    ATTACH_HOOK(MobMDamage, MobMDamage_Hook);
    ATTACH_HOOK(summonPDamage, summonPDamage_hook);
    ATTACH_HOOK(summonMDamage, summonMDamage_hook);

    // Summon target seeking. The summon's attack mob-finder (sub_678ECC) only scans a hardcoded box
    // of player +-300px X / +-100px Y, then picks the nearest mob in it -- that's the "idles unless a
    // mob is right next to you" behavior. Cave over the box construction to widen it to
    // summonSeekRangeX/Y on both axes (X is disp32, but Y is disp8 and can't grow in place, so a cave
    // is needed to reach mobs on platforms above/below too).
    CodeCave((void*)summonSeekRect, 0x00678EDB, 22);
    ATTACH_HOOK(loadSummonAttackInfo, loadSummonAttackInfo_hook);
    // Summon pull: brackets the attack so the damage hooks can collect the mobs that got hit,
    // then drags the ones we control toward the summon. Dormant while g_summonPullSkills is empty.
    ATTACH_HOOK(summonTryDoingAttackManual, summonTryDoingAttackManual_hook);

    // Rising Toss for arbitrary skills, two caves in CMob::OnHit:
    //   0x00668D72 - open the Aran-only gate so listed skills reach GenerateMovePath at all
    //                (mov eax,[esi] 2 + mov ecx,esi 2 + call [eax+40h] 3 = 7 bytes, 2 to NOP)
    //   0x00668D8E - substitute the toss flag it passes as the last argument
    //                (push [ebp-0x14] 3 + xor eax,eax 2 = exactly the 5 the jmp needs)
    CodeCave((void*)OnHitEntryCave, 0x00668B83, 0);
    // Toss: call GenerateMovePath ourselves at the per-hit queue entry (sub_66B05E), because
    // CMob::OnHit's own path is unreachable for non-Aran hits (a10 == 0 / ctrl == -3).
    CodeCave((void*)HitQueueCave, 0x0066B05E, 0);
    // NOTE: the octoMultiHit seek-routing cave (0x7A5062) is intentionally NOT installed. All summons
    // now get the +0x80 rect path in loadSummonAttackInfo_hook, which gives stationary octopus
    // summons their multi-hit via FindHitMobInRect without the fragile seek/cave detour.
    ATTACH_HOOK(get_vertical_adjust_of_attack_range, vertical);
    ATTACH_HOOK(pDoActiveSkill, CUserLocal__DoActiveSkill_Hook);
    ATTACH_HOOK(missileSpeed, missileSpeed_Hook);
    ATTACH_HOOK(chainLightning_Hook, drop_off_damage_skills);
    // ATTACH_HOOK(AddRush, AddRush_Hook);
    ATTACH_HOOK(pGetSkillLevel, GetSkillLevel);
    ATTACH_HOOK(_is_attack_area_set_by_data, is_attack_area_set_by_data);
    // ATTACH_HOOK(ztlSecureFuse_short, ztlfuse_short);
    ATTACH_HOOK(mastery_Calcs_Hook, mCalc);
    PatchMasteryRange();
    // ATTACH_HOOK(calcpdamage_hook, CalcDamage__PDamage);
    ATTACH_HOOK(remove_bullet_skill_hook, remove_bullets);
    ATTACH_HOOK(octHook, octopus);
    // ATTACH_HOOK(ztlSecureFuse_double_check, ztlfuse_double);
    // ATTACH_HOOK(jobCode, jobCode_hook);
    ATTACH_HOOK(pDoJump, CUserLocal_Jump);
    ATTACH_HOOK(meso_bag_handle, siegeModePacket);
    ATTACH_HOOK(ltrbshoothook, ltrb);
    // ATTACH_HOOK(ShowSkillEffect_hook, ShowSkillEffect);
    ATTACH_HOOK(SetAttackAction_Hook, setAttackAction);
    ATTACH_HOOK(GetMobTemplate, GetMobTemplate_Hook);
    ATTACH_HOOK(SetFromWhenDoom, SetFromWhenDoom_Hook);
    ATTACH_HOOK(onDoomed, OnDoomed_Hook);
    ATTACH_HOOK(mesoFormulaHook, MesoFormula);
    ATTACH_HOOK(isHerosWill, isHerosWillHook);
    ATTACH_HOOK(skillDelayHook, summondelay);
    ATTACH_HOOK(GetOneTimeAction, tGetOneTimeAction); // keeps g_inOneTimeAction current
    ATTACH_HOOK(thingyWindArcher, windarcherhook);
    ATTACH_HOOK(SetDamaged_Hook, SetDamaged);
    ATTACH_HOOK(elementCharge, elementChargeHook);
    ATTACH_HOOK(isKeydownSkill, isKeydownSkillHook);
    CodeCave((void*)please, 0x00791C41, 4);
    CodeCave((void*)FlashJumpAll, 0x0096BF0B, 0);
    PatchNop(0x0096C073, 6);
    Patch4(0x00765CFC + 1, 341);
    Patch4(0x00765D19 + 1, 3410000);
    skillHacks();
    changeMagicAttacks();
    AttachSkillOffsetMod();
    // // Instant FA
    Patch1(0x0095795E, 0x83);
    Patch1(0x0095795E + 1, 0xC0);
    Patch1(0x0095795E + 2, 0x00);
    Patch1(0x0094DC26, 0xEB); // remove Custom spring conditions
    // dash can't cancel
    ATTACH_HOOK(dashOnDash, dashOnDash_hook);
    ATTACH_HOOK(pGetAttackSpeedDegree, GetAttackSpeedDegree);
    Patch1(0x94cdb0, 0xeb);
    ATTACH_HOOK(sparkThing, sparkThingHook);
    ATTACH_HOOK(isDashingSkill, isDashingHook);
    ATTACH_HOOK(is_keydown_skill, is_keydown_skill_t);

    // pheonix
    Patch4(0x007A6D6B + 2, 3111015);
    Patch4(0x007A53A4 + 2, 3111015);
    Patch4(0x007A5068 + 1, 3111015);
    Patch4(0x0075A53B + 1, 3111015);

    // octojump
    // Patch4(0x0096bf04 + 1, 5201007);
    Patch4(0x0096c062 + 1, 5201007);

    Patch4(0x004FB2ED, 4511006); // cmp esi, imm32  (sub_4FB292)
    Patch4(0x00504CC0, 4511006); // push imm32      (CDropPool::Update)
    Patch4(0x00504D15, 4511006); // push imm32      (CDropPool::Update)
    Patch4(0x00791FCF, 4511006); // push imm32      (CalcDamage_MesoExplosion)
    Patch4(0x00980543, 4511006); // cmp [ebp-10h], imm32 (CUserRemote::OnAttack)
    Patch4(0x009805D1, 4511006); // push imm32      (CUserRemote::OnAttack)
    Patch4(0x00981045, 4511006); // cmp [ebp-14h], imm32 (CUserRemote::OnMeleeAttack)
    Patch4(0x009810B0, 4511006);
    Patch4(0x00764C61, 2510000); // Dragon fury
    Patch1(0x00a2948a, 0xeb);
    Patch4(0x00A294D0 + 1, 2510000);
    Patch4(0x00937B02 + 1, 2510000);
    Patch4(0x009817AB + 1, 3411006);
    // TryDoingShootAttack @ 0x9548b9: 3111004 and 3211004 both `jz loc_954947`, which gives the
    // attack the extended +0x190 line-travel reach (the Avenger projectile behavior). Repurpose the
    // 3211004 slot (cmp ecx,imm32 @ 0x9548C5, imm at +2) so 3411006 takes that same path. 3111004
    // stays intact; the cross-class 3211004 is already shared/sacrificed in the property hooks.
    Patch4(0x009548C5 + 2, 3411006);

    Patch4(0x00790399 + 1, 4210100); // CalcDamage::PDamage skill-bonus slot (player-buff gated)
    Patch4(0x006319AA + 1, 4210100);
    Patch4(0x0094E335 + 1, 4210100);
    Patch4(0x00957282 + 1, 4210100);
    Patch4(0x00967070 + 1, 4210100);
    // Route 1211000 through get_cool_time in TryDoingMeleeAttack (else it bypasses the cooltime).
    // Attack-key skills go through TryDoingMeleeAttack, NOT DoActiveSkill_Prepare, so this melee
    // cave is still required for them; the DoActiveSkill_Prepare patch below only covers cast skills.
    InstallMeleeCooltimeGate();
    InstallIceDemonAlias();
    // Generalize the DoActiveSkill_Prepare cooldown map to ALL prepared/cast skills. Vanilla only
    // ran the cooldown check (@0x96A9CC) and the `map[skill] = now + get_cool_time(skill)` store
    // (@0x96B09C) for 5 hardcoded ids (1121001/1221001/1321001 + 2 others). NOP both `jnz` skips so
    // every skill falls into the blocks. get_cool_time returns 0 for unlisted skills -> expiry == now
    // -> the `now < expiry` check never blocks, so only get_cool_time_t entries get a real cooldown.
    Patch1(0x0096A9CC, 0x90); // jnz loc_96A9F4 -> nop (cooldown CHECK runs for all skills)
    Patch1(0x0096A9CD, 0x90);
    Patch1(0x0096B09C, 0x90); // jnz loc_96B0BE -> nop (cooldown STORE runs for all skills)
    Patch1(0x0096B09D, 0x90);
    // ShowTimeGate pins the damage-number time for its skills; BypassGate makes every skill in
    // g_singleHitActionSkills skip the action-tied .HitAfter recompute after the first hit, so the
    // mob's hit action plays once per attack (14101006 behavior) and the pinned time sticks.
    InstallDamageShowTimeGate();
    InstallDamageBypassGate();
    // don't cancel skills when hit
    Patch1(0x009592E7, 0xEB);
    // Battleship climb
    Patch1(0x009CC11F, 0xEB);
    CodeCave((void*)shipTorpedoGate, 0x009673A3, 12);
    Patch4(0x008c7cec, 1000000); // autoassign
    Patch4(0x008C7B75, 1000000); //mp assign
    CodeCave((void*)removeHP, 0x008C7B1F, 3);
    Patch4(0x006788F6 + 2, -400);
    Patch4(0x006788EE + 2, 400);
    Patch4(0x00955EF0 + 1, 150);
}

DWORD Bypass1 = 0x007540A3;
DWORD Bypass3 = 0x00755A24;
DWORD Bypass2 = 0x00755B4E;
DWORD Bypass4 = 0x00756C8B;


void CreateCodecaveAt(DWORD patchAddress, DWORD jmpto, DWORD jmpback) {
    // Allocate executable memory
    BYTE* cave = (BYTE*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!cave)
        return;

    BYTE* p = cave;

    // cmp eax, 206
    *p++ = 0x3D;
    *p++ = 0xCE;
    *p++ = 0x00;
    *p++ = 0x00;
    *p++ = 0x00; // cmp eax, 206
    *p++ = 0x0F;
    *p++ = 0x84; // je
    DWORD reljmp = jmpto - ((DWORD)p + 4);
    memcpy(p, &reljmp, 4);
    p += 4;

    // cmp eax, 207
    *p++ = 0x3D;
    *p++ = 0xCF;
    *p++ = 0x00;
    *p++ = 0x00;
    *p++ = 0x00;
    *p++ = 0x0F;
    *p++ = 0x84;
    reljmp = jmpto - ((DWORD)p + 4);
    memcpy(p, &reljmp, 4);
    p += 4;

    // cmp eax, 233
    *p++ = 0x3D;
    *p++ = 0xE9;
    *p++ = 0x00;
    *p++ = 0x00;
    *p++ = 0x00;
    *p++ = 0x0F;
    *p++ = 0x84;
    reljmp = jmpto - ((DWORD)p + 4);
    memcpy(p, &reljmp, 4);
    p += 4;

    // jmp back
    *p++ = 0xE9;
    reljmp = jmpback - ((DWORD)p + 4);
    memcpy(p, &reljmp, 4);

    // Final jump from original address to this codecave
    DWORD relToCave = (DWORD)cave - patchAddress - 5;
    BYTE patch[5] = { 0xE9 };
    memcpy(patch + 1, &relToCave, 4);
    WriteProcessMemory(GetCurrentProcess(), (void*)patchAddress, patch, 5, nullptr);
}

DWORD GetNearJumpTarget(DWORD instrAddr) {
    BYTE* bytes = reinterpret_cast<BYTE*>(instrAddr);
    if ((bytes[0] == 0x0F && (bytes[1] == 0x84 || bytes[1] == 0x85))) { // JE or JNZ
        int32_t relOffset = *reinterpret_cast<int32_t*>(bytes + 2);
        return instrAddr + 6 + relOffset;
    }
    return 0;
}


BYTE ReadByteAt(DWORD address) {
    return *(BYTE*)address;
}

void replace() {
    const char* searchAoB = "3D CF 00 00 00";
    Pattern pat = ParsePattern(searchAoB);
    std::vector<DWORD> foundAddresses;

    DWORD start = 0x00400000;
    DWORD end = 0x007AAAAA;

    for (DWORD i = start; i < end - pat.bytes.size(); i++) {
        bool found = true;
        for (size_t j = 0; j < pat.bytes.size(); j++) {
            if (pat.mask[j] && *(BYTE*)(i + j) != pat.bytes[j]) {
                found = false;
                break;
            }
        }
        if (found)
            foundAddresses.push_back(i);
    }

    for (DWORD addr : foundAddresses) {
        DWORD jmpto = addr + 7 + ReadByteAt(addr + 6);
        DWORD returnAfter = addr + 7;
        CreateCodecaveAt(addr, jmpto, returnAfter);
    }
}

void RechargeArrows() {
    replace();
    PatchNop(Bypass2, 6);
    PatchNop(Bypass3, 6);
    PatchNop(Bypass4, 6);
}

DWORD dwFireArrow = 0x00955DA8;
DWORD dwFireArrowRet = 0x00955DAD; // fail
DWORD dwFireSucc = 0x00956372;     // multi mob attack
__declspec(naked) void FireArrow() {
    __asm {
        cmp eax, 12121002
        je success
        cmp eax, 12121012
        je success
        cmp eax, 12121054
        je success
        cmp eax, 12121055
        je success
        cmp eax, 2121006
        je success
        cmp eax, 2121052
        je success
        cmp eax, 2121054
        je success
        cmp eax, 2221011
        je success
        cmp eax, 2321007
        je success
        cmp eax, 2221009
        je success

        cmp eax, 12001003
        je success

                // Mage 5th jobs
        cmp eax, 2131067
        je success
        cmp eax, 2231067
        je success
        cmp eax, 2331067
        je success
        cmp eax, 12131067
        je success
        cmp eax, 2131072
        je success
        cmp eax, 2231072
        je success
        cmp eax, 2331072
        je success
        cmp eax, 12131072
        je success
        cmp eax, 2131079
        je success
        cmp eax, 2231079
        je success
        cmp eax, 2331079
        je success
        cmp eax, 12131079
        je success


        cmp eax, 0x0021E3CB
        jmp dwFireArrowRet
        success :
        jmp dwFireSucc
    }
}

DWORD dwFireBulletAdd = 0x00956445;
DWORD dwFireBulletSucc = 0x0095645B;
DWORD dwFireBulletRet = 0x0095644E;
__declspec(naked) void FireArrowBullet() {
    __asm {
        cmp dword ptr[ebp - 0x14], 2221003
        je success
        cmp dword ptr[ebp - 0x14], 2101004
        je success
        cmp dword ptr[ebp - 0x14], 2301005
        je success
        cmp dword ptr[ebp - 0x14], 2321007
        je success
        jmp dwFireBulletRet
        success :
        jmp dwFireBulletSucc
    }
}

auto is_shoot_action = (int(__cdecl*)(int))0x004566F5;
int(__cdecl is_shoot_action_hook)(int nAction) {
    return 1;
}

auto hook_is_correct_upgrade = (int(__cdecl*)(int, int))0x004F5497;
int(__cdecl is_correct_upgrade_equip)(int nUItemID, int nEItemID) {
    int v2;
    int v3;

    if (nUItemID / 10000 == 204 && nEItemID / 1000000 == 1) {
        v2 = nUItemID / 100;
        if ((nUItemID / 100 == 20490 || v2 == 20491 && (nUItemID < 2049105 || nUItemID > 2049110)) && nEItemID / 100000 != 18) {
            return 1;
        }

        v3 = (nEItemID / 10000) % 100;

        // 2040000�2040099: Weapons excluding wands (37) and staffs (38)
        if (v2 == 20400) {
            return (v3 >= 30 && v3 <= 49 && v3 != 37 && v3 != 38);
        }

        // 2040100�2040199: Wands and Staffs
        if (v2 == 20401) {
            return (v3 == 37 || v3 == 38 || v3 == 32);
        }

        // 2040200�2040299: Hat, Top, Bottom, Shoes, Gloves
        if (v2 == 20402) {
            return (v3 == 0 || v3 == 4 || v3 == 6 || v3 == 7 || v3 == 8);
        }

        // 2040300�2040399: Face, Eye, Earrings, Cape, Ring, Pendant
        if (v2 == 20403) {
            return (v3 == 1 || v3 == 2 || v3 == 3 || v3 == 10 || v3 == 11 || v3 == 12 || v3 == 13 || v3 == 14);
        }

        // 2040400�2040499: Overall and Shield
        if (v2 == 20404) {
            return (v3 == 5 || v3 == 9);
        }

        // 2049200�2049299: Special case scrolls (e.g. Chaos Scrolls)
        if (v2 == 20492) {
            return (v3 >= 0 && v3 <= 13);
        }
    }
    return 0;
}
const DWORD MakeIncDecHPEffectDecode2 = 0x0042470C;
const DWORD UpdateHpStructure = 0x00938118;
const DWORD UpdateHpStructureRetn = 0x00938120;

__declspec(naked) void UpdateIncHpToShort() {
    __asm {
        call MakeIncDecHPEffectDecode2
        movzx eax, ax
                // mov eax, 1000
        jmp dword ptr[UpdateHpStructureRetn]
    }
}

auto CMapLoadable__SetFieldMagLevel = (int(__thiscall*)(void*))0x00642890;
int(__fastcall CMapLoadable__SetFieldMagLevel_t)(void* thisptr, void* edx) {
    return 0;
}

struct CUIToolTip;

typedef void(__thiscall* CUIToolTip__DrawItemTitle_t)(CUIToolTip* pThis, int y, const char* sText, _bstr_t* bEquip);
static auto CUIToolTip__DrawItemTitle = reinterpret_cast<CUIToolTip__DrawItemTitle_t>(0x008F49BC);

static void __fastcall DrawItemTitleHook(CUIToolTip* pThis, void* edx, int y, const char* sText, _bstr_t* bEquip) {
    if (sText && strchr(sText, '_')) {
        int skillID = 0;
        int amount = 0;

        if (sscanf_s(sText, "%d_%d", &skillID, &amount) == 2) {
            SkillInfo* pSkillInfo = SkillInfo::GetInstance();
            SKILLENTRY* pSkill = pSkillInfo ? pSkillInfo->GetSkill(skillID) : nullptr;

            if (pSkill && pSkill->skillName._m_pStr) {
                char szBuf[256];
                sprintf_s(
                        szBuf,
                        sizeof(szBuf),
                        "+%d to %s",
                        amount,
                        pSkill->skillName._m_pStr);

                return CUIToolTip__DrawItemTitle(pThis, y, szBuf, bEquip);
            }
        }
    }
    return CUIToolTip__DrawItemTitle(pThis, y, sText, bEquip);
}

auto DrawStat = (int(__thiscall*)(void*, void*))0x008C59FF;
int(__fastcall DrawStat_t)(void* thisptr, void* edx, void* pParam) {
    setMAD();
    return DrawStat(thisptr, pParam);
}

auto CanSendExclRequest = (int(__thiscall*)(CWvsContext*, int, int))0x00485BF7;
int(__fastcall CanSendExclRequest_Hook)(CWvsContext* pThis, void* edx, int a1, int a2) {
    if ((int)_ReturnAddress() != 0x00A23D0F) {
        return 1;
    }
    return CanSendExclRequest(pThis, a1, a2);
}

void AttachOtherHooks() {
    ATTACH_HOOK(hook_is_correct_upgrade, is_correct_upgrade_equip);
    Patch1(0x00620F2B + 1, 0x1F); // Password Remove character limit
    RechargeArrows();
    Patch4(0x0067DD1D + 1, 999999);
    Patch4(0x00793499 + 1, 999999);
    Patch4(0x00793107 + 1, 999999);
    Patch4(0x007926DD + 1, 999999);
    Patch4(0x0077E215 + 1, 999999);
    Patch4(0x00780620 + 1, 999999);

    ATTACH_HOOK(createWorldMap, noMap);

    // Close Range Attacks
    Patch1(0x009516C2, 0xE9);
    Patch1(0x009516C2 + 1, 0xc8);
    Patch1(0x009516C2 + 2, 0xfc);
    Patch1(0x009516C2 + 3, 0xff);
    Patch1(0x009516C2 + 4, 0xff);

    // Remove If you do not use your AP when you level up POP UP
    Patch1(0x00A20091, 0xEB);

    // Hide the "[Master Level]" line in skill tooltips. In CUIToolTip::SetToolTip_Skill @ 0x8F25D0
    // the block gated by `is_skill_need_master_level` (branch @ 0x8F28F4) pulls StringPool 3852,
    // formats it and draws it via sub_8F4535. Force that jz to always skip the block (jz->jmp) so
    // neither the label nor its spacing gap is emitted. 0F 84 CE000000 -> E9 CF000000 90.
    Patch1(0x008F28F4, 0xE9);
    Patch4(0x008F28F5, 0x000000CF);
    Patch1(0x008F28F9, 0x90);

    // Hair ID Fix
    Patch1(0x005C94FC + 2, 7);
    Patch1(0x005C94FF + 1, 0x8E);

    Patch1(0x0095795E, 0x83);
    Patch1(0x0095795E + 1, 0xC0);
    Patch1(0x0095795E + 2, 0x00);
    // WorldMap Cap Increase
    Patch1(0x009EA030, 0x81);
    Patch1(0x009EA031, 0xFE);
    Patch1(0x009EA032, 0xB4);

    // Disable the world map entirely: stub CWorldMapDlg::CreateWorldMapDlg @ 0x009EB366 to
    // `xor eax,eax; retn 8` (overwrites the 5-byte `mov eax, offset ehFuncInfo` prologue).
    // Every open path funnels through this one function -- the W hotkey
    // (CWvsContext::UseFuncKeyMapped, call @ 0x00A0781A), the minimap globe button
    // (CUIMiniMap::OnButtonClicked, call @ 0x0085AD47), the NPC world-map script path
    // (call @ 0x00840108) and two misc dialog paths (calls @ 0x0087E7B1 / 0x0087E834).
    // All five callers handle a 0 return: they pop the vanilla "the world map is not
    // available" notice and destruct their stack-constructed CWorldMapDlg cleanly, so
    // there's no leak and no half-created window.
    Patch1(0x009EB366, 0x33);   // xor eax, eax
    Patch1(0x009EB367, 0xC0);
    Patch1(0x009EB368, 0xC2);   // retn 8
    Patch1(0x009EB369, 0x08);
    Patch1(0x009EB36A, 0x00);


    // Maker Skill Instant
    Patch1(0x826F92 + 2, 0x08);
    Patch1(0x826F92 + 3, 0x01);
    Patch1(0x826F92 + 4, 0x00);
    Patch1(0x826F92 + 5, 0x00);
    // Maker: always allow the make/send to proceed. CUIItemMaker::StartItemMake @ 0x00826F25 gates on
    // recipe-type + IsAbleToMake/IsExistEmptySlot/IsEnoughMeso, returning 0 (no packet) on any failure.
    // After the window-ready check (@0x826F2F, kept), jmp straight to the make path (LABEL_12 @0x826F78),
    // skipping the dispatch + all 3 client-side validations. Replaces `mov eax,[esi+704h]` (6 bytes).
    Patch1(0x00826F31, 0xEB);          // jmp short 0x826F78
    Patch1(0x00826F32, 0x45);
    PatchNop(0x00826F33, 4);           // pad remainder of the overwritten mov
    // ...but the Create button is grayed out by CUIItemMaker::Update (@0x824A06): `if (!IsAbleToMake)
    // -> disable button + state=0`, and state must be 1 for StartItemMake to run. The actual send,
    // CUIItemMaker::RequestItemMake (@0x827096, packet 113), also gates on IsAbleToMake + IsEnoughMeso
    // + IsExistEmptySlot. Force all three to always return true (mov eax,1 ; ret) so the button stays
    // enabled, the state advances, and the make packet is always sent.
    unsigned char makerForceTrue[] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3 };
    Patch1Array(0x00822C66, makerForceTrue, sizeof(makerForceTrue)); // CUIItemMaker::IsAbleToMake
    Patch1Array(0x00826FB3, makerForceTrue, sizeof(makerForceTrue)); // CUIItemMaker::IsEnoughMeso
    Patch1Array(0x0082744F, makerForceTrue, sizeof(makerForceTrue)); // CUIItemMaker::IsExistEmptySlot
    // With IsAbleToMake forced true, RequestItemMake @0x827096 no longer bails on an empty selection and
    // the disassemble/monster-crystal path (v449==4 @0x827163) derefs a null *(this+384) -> crash.
    // Vanilla only null-guards v449==3 (@0x82711B: `cmp [esi+704h],3 ; jnz skip ; cmp [esi+600h],0 ; jz
    // return0`). Widen that jnz to jl so v449>=3 (disassemble AND crystal) also returns 0 when the item
    // slot (*(this+384) == [esi+600h]) is empty -- graceful return 0 instead of sending a bad packet.
    Patch1(0x00827121, 0x7C); // jnz loc_82712C -> jl loc_82712C
    // Allow usage of pots while in Dark Sight skill
    FillBytes(0x0094F6AB, 0x90, 6);
    // Allow double click pots while in Dark Sight skill
    FillBytes(0x004F0311, 0x90, 6);
    // //
    // //
    PatchNop(0x00957C2D, 6);
    //
    // // swear filter
    PatchNop(0x007A03C8, 2); // remove 3rd party censor (also removes the ?? spam)
    // // Remove You may not use this skill yet message
    //
    // // crit BYPASS
    //
    PatchNop(0x007650B3, 29);
    // // Bowman Action Bypass
    Patch1(0x0078EA69, 0xE9);
    Patch1(0x0078EA69 + 1, 0x81);
    Patch1(0x0078EA69 + 2, 0x01);
    Patch1(0x0078EA69 + 3, 0x00);
    Patch1(0x0078EA69 + 4, 0x00);
    Patch1(0x0078EA69 + 5, 0x00);
    Patch1(0x0078EA69 + 6, 0x90);
    // // Remove this card is already full blablabla..
    PatchNop(0x00A08283, 18);
    CodeCave(FireArrow, dwFireArrow, 5);
    CodeCave(FireArrowBullet, dwFireBulletAdd, 5);
    CodeCave(UpdateIncHpToShort, UpdateHpStructure, 8);

    unsigned char Uncap_Array[] = { 0x00, 0x00, 0xC0, 0xFF, 0xFF, 0xFF, 0xDF, 0x41 };
    Patch1Array(0x00AFE8A0, Uncap_Array, sizeof(Uncap_Array));
    Patch4(0x008C3304 + 1, 2147483647);

    unsigned char Uncap_Stat_Arr_1[] = { 0xFF, 0xFE, 12 };
    Patch1Array(0x00780620 + 1, Uncap_Stat_Arr_1, sizeof(Uncap_Stat_Arr_1));
    Patch1Array(0x0077E055 + 1, Uncap_Stat_Arr_1, sizeof(Uncap_Stat_Arr_1));
    Patch1Array(0x0077E12F + 1, Uncap_Stat_Arr_1, sizeof(Uncap_Stat_Arr_1));
    Patch1Array(0x0077E215 + 1, Uncap_Stat_Arr_1, sizeof(Uncap_Stat_Arr_1));
    Patch1Array(0x0078FF5F + 1, Uncap_Stat_Arr_1, sizeof(Uncap_Stat_Arr_1));
    Patch1Array(0x0079166C + 1, Uncap_Stat_Arr_1, sizeof(Uncap_Stat_Arr_1));
    Patch1Array(0x00791CD5 + 1, Uncap_Stat_Arr_1, sizeof(Uncap_Stat_Arr_1));
    Patch1Array(0x007806D0 + 1, Uncap_Stat_Arr_1, sizeof(Uncap_Stat_Arr_1)); // Accuracy uncap
    Patch1Array(0x00780702 + 1, Uncap_Stat_Arr_1, sizeof(Uncap_Stat_Arr_1)); // Avoidability uncap

    // Speed Cap Removal
    Patch4(0x00780746, 250);
    Patch4(0x008c4287, 250);
    Patch4(0x0094D91F, 250);

    // Skill up stuff
    Patch4(0x00A23D05 + 1, 200);

    // Enable Teleport mid air -
    // Ezrosia V2 ()newer ones) FillBytes(0x00957C2D, 0x90, 6);
    PatchNop(0x00957C2D, 6);


    // CritBypass
    PatchNop(0x007650B3, 29);

    // Crit damage: multiply the line by crit-damage%, instead of adding a
    // bonus computed off BASE damage. Vanilla CalcDamage::PDamage @0x790190:
    //   on crit roll, v329 = (v325-100)/100 * baseDmg + v329
    // (v325 = crit dmg %, e.g. 200; v329 = skill-scaled line dmg). For a 30%
    // skill that yields 0.30*base + 1.00*base = 130% of base.
    // Rewrite to v329 = v325/100 * v329 -> 0.30*base * 2.0 = 60% of base,
    // i.e. each line multiplied by the crit-damage factor. Crit-mark write
    // (*(a17+4*weaponID)=1) preserved; 40-byte region, tail padded with NOP.
    unsigned char CritMul[] = {
        0x8B, 0x4D, 0x44,                         // mov  ecx, [ebp+arg_3C]   (a17)
        0x8B, 0x45, 0x24,                         // mov  eax, [ebp+weaponID]
        0xC7, 0x04, 0x81, 0x01, 0x00, 0x00, 0x00, // mov dword [ecx+eax*4], 1
        0xDB, 0x45, 0xCC,                         // fild  [ebp+var_34]       (v325)
        0xDC, 0x4D, 0xE0,                         // fmul  [ebp+var_20]       (v329)
        0xDC, 0x0D, 0xF0, 0x14, 0xAF, 0x00,       // fmul  ds:dbl_AF14F0      (0.01)
        0xDD, 0x5D, 0xE0,                         // fstp  [ebp+var_20]       (v329)
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90,       // pad to 40 bytes
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90
    };
    Patch1Array(0x00790190, CritMul, sizeof(CritMul));

    // Same crit-damage rework for magic: CalcDamage::MDamage @0x791DC3.
    // Vanilla on crit roll: v121 = ((v117 - (v117<=200?100:200))/100 + 1.0) * v121
    // (v117 = crit dmg %, v121 = line dmg). For v117<=200 that already equals
    // v117/100, but >200 is clamped down by the -200. Rewrite to a plain
    // v121 = v117/100 * v121 so magic matches physical (line * crit factor,
    // no clamp). Crit-mark write (*(nDragonFury+a13)=1) preserved; 61-byte
    // region, tail padded with NOP.
    unsigned char MCritMul[] = {
        0x8B, 0x45, 0x40,                         // mov  eax, [ebp+nDragonFury]
        0x8B, 0x4D, 0x34,                         // mov  ecx, [ebp+arg_2C]   (a13)
        0xC7, 0x04, 0x01, 0x01, 0x00, 0x00, 0x00, // mov dword [ecx+eax], 1
        0xDB, 0x45, 0xDC,                         // fild  [ebp+var_24]       (v117)
        0xDC, 0x0D, 0xF0, 0x14, 0xAF, 0x00,       // fmul  ds:dbl_AF14F0      (0.01)
        0xDC, 0x4D, 0xF4,                         // fmul  [ebp+var_C]        (v121)
        0xDD, 0x5D, 0xF4,                         // fstp  [ebp+var_C]        (v121)
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90,       // pad to 61 bytes (33 NOP)
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0x90, 0x90, 0x90
    };
    Patch1Array(0x00791DC3, MCritMul, sizeof(MCritMul));

    // uiStat stuff
    // Patch1(0x008C35C9 + 1, 0x2C); // weapon def
    // Patch1(0x008C374A + 1, 0x1A); // weapon def
    // Patch1(0x008C39E9 + 1, 0x62); // weapon def
    // Patch1(0x008C3B9C + 1, 0x50); // weapon def
    // Patch1(0x008C3D4F + 1, 0x3E); // weapon def
    // Patch1(0x008C3F8E + 1, 0x74); // weapon def
    PatchNop(0x00668C04, 5);

    // jump move
    Patch1(0x009539FA, 0xE9);
    Patch4(0x009539FA + 1, 0x00953A11 - 0x009539FA - 5);
    Patch1(0x009559E5, 0xE9);
    Patch4(0x009559E5 + 1, 0x00955A20 - 0x009559E5 - 5);

    CodeCave((void*)NW_Multi, nwthrow, 0);
    Patch1(0x0078EDB1 + 1, 0x84);
    CodeCave((void*)Claw_5, 0x0078EDB1, 1);
    CodeCave((void*)DamCalc, 0x00791BAE, 1);
    Patch1(0x0076511E, 0xEB);
    Patch1(0x009F7A9B + 1, 0);

    Patch1(0x00620F2B + 1, 0x1F); // Password Remove character limit

    // mwlbhook BYPASS
    Patch1(0x0095385b, 0xEB);
    Patch1(0x00955783, 0xEB);
    Patch1(0x009509DC, 0xEB);
    Patch1(0x00957F16, 0xEB);

    Patch1(0x0095099A, 0xEB);
    Patch1(0x0095F1A3, 0xEB);
    Patch1(0x009571BB, 0xEB);
    Patch1(0x009571F6, 0xEB);
    // sp requirements stuff
    unsigned char sp_skip_array[] = { 0xe9, 0x08, 0x02, 0x00, 0x00, 0x90 };
    Patch1Array(0x008ad01a, sp_skip_array, sizeof(sp_skip_array));

    CodeCave(SetColorToDoom, dwDoomShowAffectedSkill, 5);
    Patch1(0x0066D780 + 1, 50); // Changes the delay to transition the color to 100ms
    FillBytes(0x0066D780 + 2, 0x90, 3);
    FillBytes(0x0066D81E, 0x90, 7); // Disables removing the mob when Doom happen


    Patch4(0x0078FE91 + 2, 0xAFE378);
    Patch4(0x0078FE6A + 2, 0xAFE378);


    Patch1(0x004905EB, 0xEB);
    Patch1(0x004CAA09, 0xEB); // Infinite chat 1 of 2 scroll through chat box
    Patch1(0x004CAA84, 0xEB); // Infinite chat 2 of 2 scroll through chat box
    // Remove "Repeating the same line over and over\r\ncan negatively affect other users." check allow spam text
    Patch1(0x00490607, 0xEB);
    Patch1(0x00490609, 0x27);
    // Remove "Too much chatting can disrupt\r\nother players' ability to play the game." check allow spam text
    Patch1(0x00490651, 0xEB);
    Patch1(0x00490652, 0x1D);
    // Pic Modifier - Allowed PIC to by typed
    PatchNop(0x004ca8ba, 2);
    Patch4(0x00956E6E + 2, 2411011);
    Patch4(0x0095C001 + 1, 2411011);
    Patch4(0x0095F97E + 1, 2411011);
    Patch4(0x0098067B + 1, 2411011);

    ATTACH_HOOK(getSpeed, getSpeed_hook);

    ATTACH_HOOK(setInput, setInput_hook);
    ATTACH_HOOK(is_skill_need_master_level, masteryskill);
    ATTACH_HOOK(get_job_name_hook, get_job_name);
    ATTACH_HOOK(is_shoot_action, is_shoot_action_hook);
    ATTACH_HOOK(get_cool_time, get_cool_time_t);
    ATTACH_HOOK(DoActiveSkill_MeleeAttack, DoActiveSkill_MeleeAttack_hook);

    ATTACH_HOOK(CUIToolTip__DrawItemTitle, DrawItemTitleHook);
    // ATTACH_HOOK(CMapLoadable__SetFieldMagLevel, CMapLoadable__SetFieldMagLevel_t);
    ATTACH_HOOK(DrawStat, DrawStat_t);
    ATTACH_HOOK(SetImpactNext, SetImpactNext_Hook);
    ATTACH_HOOK(CanSendExclRequest, CanSendExclRequest_Hook);
}


struct CharacterDataEx {
private:
    inline static CharacterDataEx* m_pInstance = nullptr;

public:
    LONGLONG m_liExp;

    CharacterDataEx() {
        /* setting default value as proof of concept. can be removed. */
        m_liExp = 0;
    }

    BYTE GetCharLevel();

    static CharacterDataEx* GetInstance() {
        if (!m_pInstance) {
            m_pInstance = new CharacterDataEx();
        }

        return m_pInstance;
    }
};


inline LONGLONG myArrayForCustomEXP[] = { 1, 15, 44, 96, 188, 312, 550, 731, 969, 1154, 1358, 1358, 1810, 2308, 2856, 3464, 4134, 4872, 5688, 6588, 7582, 8678, 9890, 11224, 12698, 14326, 16122, 18102, 20290, 22704, 25368, 28308, 31554, 35136, 39090, 43454, 48272, 53588, 59458, 65938, 73090, 80984, 89700, 99320, 109940, 121666, 134608, 148896, 164670, 182084, 201308, 222532, 245962, 271830, 300386, 331914, 366722, 405150, 447574, 494414, 546126, 603218, 666250, 735840, 812672, 897498, 991150, 1094548, 1208706, 1334744, 1473896, 1627532, 1797156, 1984432, 2191198, 2419484, 2671528, 2949804, 3257042, 3596258, 3970778, 4384278, 4840816, 5344870, 5901388, 6515830, 7194226, 7749044, 8173766, 8621760, 9094306, 9592748, 10118504, 10673072, 11258030, 11875046, 12525870, 13212362, 13936472, 14700264, 15505912, 16355710, 17252078, 18197566, 19194866, 20246818, 21356418, 22526822, 23761366, 25063564, 26437120, 27885948, 29414172, 31026142, 32726446, 34519930, 36411711, 38407166, 40511977, 42732137, 45073968, 47544106, 50149658, 52898026, 55797048, 58855000, 62080603, 65483059, 69071980, 72857660, 76799967, 80898812, 85154323, 89566484, 94135444, 98861092, 103743593, 108781820, 113976774, 119328755, 124837774, 130503871, 136326348, 142305196, 148440485, 154732337, 161180879, 167786237, 174548543, 181467931, 188544535, 195778490, 203169931, 210718995, 218425819, 226289542, 234310294, 242488218, 250823458, 259316158, 267966464, 276774523, 285740484, 294864497, 304146713, 313587286, 323186370, 332944120, 342860693, 352936247, 363170941, 373564936, 384118393, 394831476, 405704349, 416737176, 427930123, 439283358, 450797048, 462471364, 474306474, 486302550, 498459763, 510778285, 523258290, 535899953, 548703450, 561668958, 574796657, 588086725, 601539344, 615154694, 628932958, 642874321, 657078969, 671548089, 686281869, 701280499, 716544170, 732073074, 747867405, 763927359,  2205594688L, 2424154688L, 2668594688L, 2942194688L, 3489882688L, 4093322688L, 4758522688L, 5491962688L, 6300602688L, 7191922688L, 8173982688L, 9255474688L, 10445794688L, 11757434688L };
inline constexpr size_t maxLevelForCustomEXP = sizeof(myArrayForCustomEXP) / sizeof(myArrayForCustomEXP[0]);

inline LONGLONG get_next_level_exp() {
    BYTE lvl = CharacterDataEx::GetInstance()->GetCharLevel();

    if (lvl >= sizeof(myArrayForCustomEXP) / sizeof(myArrayForCustomEXP[0]))
        return 0;

    return myArrayForCustomEXP[lvl];
}

inline BYTE CharacterDataEx::GetCharLevel() {
    auto CUserLocal__GetCharacterLevel = (BYTE(__fastcall*)(PVOID pThis, PVOID edx))0x00949B15;

    PVOID CUserLocal__ms_pInstance = *reinterpret_cast<void**>(0x00BEBF98);
    if (!CUserLocal__ms_pInstance)
        return 0;

    return CUserLocal__GetCharacterLevel(CUserLocal__ms_pInstance, nullptr);
}

inline char* __cdecl itoa_ExpSwap(int value, PCHAR buffer, int radix) {
    _i64toa(CharacterDataEx::GetInstance()->m_liExp, buffer, radix);

    // TODO abbreviate large numbers to something like 14.123B or something -- maybe make toggleable through some UI setting??

    return buffer;
}

/* all arguments passed on the stack despite being a member function */
inline void __cdecl FormatExpString_Hook(ZXString<char>* pThis, const char* originalstring, int curexp, int nextlevelexp) {
    std::string s = std::to_string(CharacterDataEx::GetInstance()->m_liExp);
    s.append(" / ");
    s.append(std::to_string(get_next_level_exp()));

    pThis->Assign(s.c_str());
}

typedef void*(__cdecl* _lpfn_NextLevel_t)(int[]);
static auto _lpfn_NextLevel = reinterpret_cast<_lpfn_NextLevel_t>(0x0078D166);


inline auto CUIStatusBar__SetNumberValue_t = (void(__thiscall*)(void*, int, int, int, int, int, int, int))0x008D850B;
inline void __fastcall CUIStatusBar__SetNumberValue_Hook(void* pThis, void* edx, int hp, int hpMax, int mp, int mpMax, int exp, int expMax, int tempExp) {
    LONGLONG liExp = CharacterDataEx::GetInstance()->m_liExp;
    LONGLONG liExpMax = get_next_level_exp();

    /* this adjusts the exp bar gauge -- idk how else to do this lmao, we're essentially scaling the exp down until itll fit in the data type */
    while (liExpMax > INT_MAX || liExp > INT_MAX) {
        liExp >>= 2;
        liExpMax >>= 2;
    }

    exp = (int)liExp;
    expMax = (int)liExpMax;

    CUIStatusBar__SetNumberValue_t(pThis, hp, hpMax, mp, mpMax, exp, expMax, tempExp);
}

inline void* __fastcall _lpfn_NextLevel_Hook(LONGLONG expTable[maxLevelForCustomEXP]) // your max level is the size of your array
{
    memcpy(expTable, myArrayForCustomEXP, sizeof(myArrayForCustomEXP)); // ty to creator of github.com/PurpleMadness/CustomExpTable
    expTable[maxLevelForCustomEXP] = 0;                                 // insert your own formula or predefined array into this part. MUST MATCH server numbers
    return expTable;                                                    // currently using predefined array
}

int __fastcall ExpSwap__Decode4To8(CInPacket* pThis, void* edx) {
    LONGLONG liExp = pThis->Decode<LONGLONG>();
    CharacterDataEx::GetInstance()->m_liExp = liExp;
    return liExp < INT_MAX ? (INT)liExp : INT_MAX;
}

inline const char* __fastcall ZXString__GetConstCharString(ZXString<char>* pThis, PVOID edx) {
    std::string s = std::to_string(CharacterDataEx::GetInstance()->m_liExp); // need to include string lib

    pThis->Assign(s.c_str());

    return *pThis;
}


void InitExpOverride() {
    // SetHook(true, reinterpret_cast<void**>(&_lpfn_NextLevel), _lpfn_NextLevel_Hook);
    /* GW_CharacterStat::DecodeChangeStat -> hijack decode4 call and switch to decode8, then return int value */
    PatchCall(0x004E31B6, ExpSwap__Decode4To8);

    /* GW_CharacterStat::Decode -> hijack decode4 call and switch to decode8, then return int value */
    PatchCall(0x004E2C6E, ExpSwap__Decode4To8);

    ATTACH_HOOK(CUIStatusBar__SetNumberValue_t, CUIStatusBar__SetNumberValue_Hook);

    /* CWvsContext::OnStatChanged -> jmping over a segment that looks at exp and then makes pet talk if at a certain % -> cbf fixing this */
    Patch1(0x00A20116, 0xEB);

    /* CUIStat::OnMouseMove -> hijack displayed exp in tooltip when hovering in stat window */
    PatchCall(0x008C539D, FormatExpString_Hook);

    /* CUIStat::Draw -> hijack displayed exp in stat window */
    PatchCall(0x008C602E, ZXString__GetConstCharString);

    /* CUIStatusBar::ProcessToolTip -> hijack displayed exp in tooltip when hovering exp gauge in stat bar */
    PatchCall(0x008D78E3, FormatExpString_Hook);
    PatchCall(0x008D789F, FormatExpString_Hook);

    /* CUIStatusBar::SetNumberValue -> hijack displayed exp above exp gauge */
    Patch1(0x008DA406 + 1, 64); // increase string size allocation -- v207 = alloca(32)
    PatchCall(0x008DA418, itoa_ExpSwap);
}


auto GetCurFieldID = (unsigned int(__thiscall*)(CWvsContext*))0xA1238B;
auto PlayBGMFromMapInfo = (void(__thiscall*)(void*))0x64211E;
void __fastcall PlayBGMFromMapInfo_Hook(void* pThis, void* edx) {
    PlayBGMFromMapInfo(pThis);
}

auto PlayNextBGM = (void(__thiscall*)(void*))0x641db2;
void __fastcall PlayNextBgm_Hook(void* pThis, void* edx) {
}

auto RestoreBGM = (void(__thiscall*)(void*))0x642214;
void __fastcall RestoreBgm_Hook(void* pThis, void* edx) {
}

auto SoundMan__PlayBGM = (void(__thiscall*)(void*, void*, int, int, int, void*))0x43f301;
void __fastcall SoundMan__PlayBGM_Hook(void* pThis, void* edx, void* pBGM, int nBGMType, int nBGMVolume, int nBGMLoop, void* nBGMPriority) {
    // This fires as a map's BGM starts, which includes the first field the player enters after
    // picking a character. CWvsContext::GetInstance() was passed straight into a __thiscall with
    // no null check: during a stage transition the singleton can be absent, and dereferencing it
    // faults exactly where Wine clients have been dying. Treat "no context" the same as "not in
    // a field yet" (the -1 case) and let the BGM through.
    bool bPlay = (int)_ReturnAddress() == 0x5333A0;
    if (!bPlay) {
        CWvsContext* pContext = CWvsContext::GetInstance();
        LogInfo("CSoundMan::PlayBGM: context=%p type=%d vol=%d loop=%d", (void*)pContext, nBGMType, nBGMVolume, nBGMLoop);
        bPlay = !pContext || GetCurFieldID(pContext) == -1;
    }
    if (bPlay) {
        SoundMan__PlayBGM(pThis, pBGM, nBGMType, nBGMVolume, nBGMLoop, nBGMPriority);
    }
}

void BGMOverride() {
    ATTACH_HOOK(PlayBGMFromMapInfo, PlayBGMFromMapInfo_Hook);
    ATTACH_HOOK(PlayNextBGM, PlayNextBgm_Hook);
    ATTACH_HOOK(RestoreBGM, RestoreBgm_Hook);
    ATTACH_HOOK(SoundMan__PlayBGM, SoundMan__PlayBGM_Hook);
}
