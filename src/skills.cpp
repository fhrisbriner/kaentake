#include "hook.h"
#include "WzLib/IWzArchive.h"
#include "wvs/CUserLocal.h"
#include "wvs/CWvsContext.h"
#include "wvs/mob.h"
#include "wvs/packet.h"

#include <chrono>
#include <intsafe.h>
#include <random>
#include <string>
#include <sstream>

using namespace std;
using chrono::duration_cast;
using chrono::milliseconds;
using chrono::system_clock;
chrono::time_point<chrono::steady_clock> jumptimer;
chrono::time_point<chrono::steady_clock> skilltimer;
chrono::time_point<chrono::steady_clock> immunetimer;
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
const DWORD dwAccuracyCalc = 0x0077F743;
const DWORD dwAccuracyCalcRetn = 0x0077F7E2;
bool siegeMode = false;
bool jumped = false;
int mastery = 0;
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
double oaxe = 4.3;
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
int myCharacterid = 0;
bool firstLoad = true;
bool AttackMove = false;
int sparkID = 0;
double strMultiplier = 0.28;
DWORD ZtlBussy = 0x004746DD;

// NOT A SKILL
int doActiveJmpBack = 0x0096793B; // return to our existing code.

int pleasejmpout = 0x00791C6C;
double int_multiplier = 4.2;
double Hundred = 100;
int topMAD = 0;
int botMAD = 0;
int totmagic = 0;
int pad = 0;
double clMultiplier = 1.25;
int currStr = 4;

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
        int_multiplier = 4.6;
        break;
    default:
        int_multiplier = 1.0;
        break;
    }

    int int_ = CWvsContext::GetInstance()->get_m_basicStat().nINT.Fuse();
    int magic = CWvsContext::GetInstance()->get_m_secondaryStat().m_magic.Fuse();
    int bonusMagic = CWvsContext::GetInstance()->get_m_secondaryStat().m_bonusMagic.Fuse();
    int effectiveMagic = (magic + bonusMagic) - int_;
    topMAD = (int_ * int_multiplier * effectiveMagic) / 100;
    totmagic = effectiveMagic;
    // Log("%7d, %7d", effectiveMagic, totmagic);
    if (mastery > 0) {
        botMAD = topMAD * (0.05 * mastery + 0.1);
    } else {
        botMAD = topMAD * 0.1;
    }
}

void __declspec(naked) doActiveSkills() {
    __asm {
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

                // Spearman
            mov eax, 1211012
            cmp esi, eax
            je melee

            mov eax, 1211013
            cmp esi, eax
            je melee

            mov eax, 1211014
            cmp esi, eax
            je buff

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
            je heal

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


                // Priest
            mov eax, 2211011
            cmp esi, eax
            je buff

            mov eax, 2221015
            cmp esi, eax
            je buff

            mov eax, 2211012
            cmp esi, eax
            je buff

            mov eax, 2211014
            cmp esi, eax
            je magic

            mov eax, 2211015
            cmp esi, eax
            je buff

                // IL Mage
            mov eax, 2411010
            cmp esi, eax
            je magic

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

                // sniper

            mov eax, 3511003
            cmp esi, eax
            je shoot

            mov eax, 3511008
            cmp esi, eax
            je shoot

            mov eax, 3511004
            cmp esi, eax
            je shoot

                // Thief 2nd
            mov eax, 4001004
            cmp esi, eax
            je buff

                // assasin
            mov eax, 4101009
            cmp esi, eax
            je jumpmove

            mov eax, 4101008
            cmp esi, eax
            je shoot

            mov eax, 4401009
            cmp esi, eax
            je jumpmove

            mov eax, 4401008
            cmp esi, eax
            je shoot

                // hermit

            mov eax, 4111010
            cmp esi, eax
            je buff

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

                // CB
            mov eax, 4211011
            cmp esi, eax
            je melee

            mov eax, 4211015
            cmp esi, eax
            je melee

            mov eax, 4211001
            cmp esi, eax
            je buff

                // ninja
            mov eax, 4411006
            cmp esi, eax
            je shoot

            mov eax, 4411006
            cmp esi, eax
            je shoot

            mov eax, 4411009
            cmp esi, eax
            je buff

            mov eax, 4411019
            cmp esi, eax
            je shoot

                // thief 3rd job bandit

            mov eax, 4511006
            cmp esi, eax
            je meso

            mov eax, 45110131
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

            mov eax, 5411022
            cmp esi, eax
            je melee

            mov eax, 5411020
            cmp esi, eax
            je melee

            mov eax, 5511015
            cmp esi, eax
            je summons

            mov eax, 5511002
            cmp esi, eax
            je summons

            mov eax, 5511014
            cmp esi, eax
            je summons


            mov eax, 5211014
            cmp esi, eax
            je summons

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
            je shoot

            mov eax, 5111015
            cmp esi, eax
            je summons

            mov eax, 5111016
            cmp esi, eax
            je shoot

            mov eax, 5111017
            cmp esi, eax
            je shoot

            mov eax, 4101006
            cmp esi, eax
            je buff


            mov eax, 4401006
            cmp esi, eax
            je buff

            mov eax, 4201006
            cmp esi, eax
            je buff

            mov eax, 4111009
            cmp esi, eax
            je shoot

            mov eax, 2511004
            cmp esi, eax
            je buff

            mov eax, 2511006
            cmp esi, eax
            je magic

            mov eax, 4501006
            cmp esi, eax
            je buff

            mov eax, 4201006
            cmp esi, eax
            je buff

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

void ReplaceValueBatch(const ReplaceEntry* entries, int count, DWORD start, DWORD end) {
    std::vector<Pattern> patterns;
    patterns.reserve(count);
    size_t maxLen = 0;
    for (int k = 0; k < count; k++) {
        patterns.push_back(ParsePattern(entries[k].aob));
        if (patterns.back().bytes.size() > maxLen)
            maxLen = patterns.back().bytes.size();
    }
    if (maxLen == 0 || maxLen >= end - start)
        return;

    const DWORD last = end - (DWORD)maxLen;
    for (DWORD i = start; i < last; i++) {
        for (int k = 0; k < count; k++) {
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


static const ReplaceEntry kCZakEntries[] = {
    { "00 47 86 00", 88, nullptr, 0 },
    { "01 47 86 00", 88, nullptr, 0 },
    { "02 47 86 00", 88, nullptr, 0 },
    { "03 47 86 00", 88, nullptr, 0 },
    { "04 47 86 00", 88, nullptr, 0 },
    { "05 47 86 00", 88, nullptr, 0 },
    { "06 47 86 00", 88, nullptr, 0 },
    { "07 47 86 00", 88, nullptr, 0 },
    { "08 47 86 00", 88, nullptr, 0 },
    { "09 47 86 00", 88, nullptr, 0 },
    { "0A 47 86 00", 88, nullptr, 0 },
};

void fixCZak() {
    ReplaceValueBatch(kCZakEntries, sizeof(kCZakEntries) / sizeof(kCZakEntries[0]),
            0x00700000, 0x00AAAAAA);
}

void fixCHT() {
    ReplaceValueBatch(kCZakEntries, sizeof(kCZakEntries) / sizeof(kCZakEntries[0]),
            0x00700000, 0x00AAAAAA);
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
    return 0;
}

void comboStuff() {
    Patch4(0x0077dfc4 + 2, 5410000);
    Patch4(0x0077e1b5 + 2, 5410000);
    Patch4(0x0077e0cf + 2, 5410000);
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

void replaceSpark() {
    int jobID = CWvsContext::GetInstance()->get_m_basicStat().nJob.Fuse();
    if (sparkID > 0) {
        return;
    }
    if (jobID < 200) {
        sparkID = 1201016;
    } else {
        sparkID = 4111010;
    }
    static const ReplaceEntry kSkillEntries[] = {
        { "5E 93 E6 00", sparkID, skipArray, 4 },
    };
    ReplaceValueBatch(kSkillEntries, sizeof(kSkillEntries) / sizeof(kSkillEntries[0]),
            0x00700000, 0x00AAAAAA);
}

void skillHacks() {

    // static const ReplaceEntry kSkillEntries[] = {
    //     { "ED 23 4E 00", 1101016, skipArray, 4 },
    //     { "5E 93 E6 00", 1201016, skipArray, 4 }, // spark
    //     { "4A 1C 23 00", 2201010, skipArray, 4 },
    //     { "30 FD 13 00", 1210010, skipArray, 4 },
    //     {}
    // };
    // ReplaceValueBatch(kSkillEntries, sizeof(kSkillEntries) / sizeof(kSkillEntries[0]),
    //         0x00700000, 0x00AAAAAA);
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
}

bool isSkillIDMatched(int nSkillID) {
    static const int skillIDs[] = {

        // ===== Warrior =====
        1001006, 1001007,

        // ===== Knight =====
        1101016, 1101015,
        1401007, 1401015, 1401016,
        1201016, 1201012,
        1501016, 1501012,

        // ===== Crusader =====
        1111009,

        // ===== Spearman =====
        1211012, 1211013, 1211014,

        // ===== Duelist =====
        1411003, 1411005, 1411006, 1411008,

        // ====== Crusher =====
        1201017, 1210007, 1201018,

        // ===== Barbarian =====
        1511008, 1511003, 1511007, 1511006, 1511009,

        // ===== Magician =====
        2001010,

        // ===== Elementalist =====
        2101008, 2101007,
        2401005, 2401004, 2401008, 2401007,

        // ===== Cleric =====
        2201010, 2201011, 2201012, 2201013,
        2501010, 2501011, 2501012, 2501013,

        // ===== FP Mage =====
        2111011, 2111010, 2111012,

        // ===== Priest =====
        2211011,
        2211012, 2211014, 2211015,

        // ===== IL Mage =====
        2411010, 2411011, 2411012, 2411013,

        // ===== Holy Knight =====
        2511006, 2511004,

        // ===== Priest =====
        2211004, 2221015,

        // ===== Bowman =====
        3001013,

        // ===== Hunter =====
        3101007, 3101012,
        3401005, 3401007, 3401012,

        // ===== Crossbowman =====
        3201016, 3201006,
        3501005, 3501003, 3501016,

        // ===== Wind Archer =====
        3411006, 3411007, 3411005,

        // ===== Sniper =====
        3511003, 3511008, 3511004,

        3211014, 3211016, 3211015,

        3601000,

        // ===== Thief =====
        4001004,

        // ===== Assassin =====
        4101009, 4101008, 4101006,
        4401009, 4401008, 4401006,

        // ===== Hermit =====
        4111010, 4111009,

        // ===== Bandit =====
        4201014, 4201006,
        4501005, 4501014, 4501006,

        // ===== Chief Bandit =====
        4211011, 4211015, 4211001,

        // ===== Ninja =====
        4411006, 4411009, 4411019,

        // ===== Bandit 3rd =====
        4511006, 4511013, 4511003, 4511007, 4511001,

        // ===== Gunslinger 2nd =====
        5501001,

        // ===== Marauder 3rd =====
        5111013,
        // 5501006, 5501002
        // ===== Brawler 2nd =====
        5401002, 5401003,

        // ===== Comboist ====
        5411002, 5411021, 5411022, 5411020,

        // ===== Summoner ====
        5511015, 5511002, 5511014
    };

    return std::find(std::begin(skillIDs), std::end(skillIDs), nSkillID) != std::end(skillIDs);
}


auto CInPacket_Decode4Original = reinterpret_cast<int(__thiscall*)(CInPacket*)>(0x00406629);
auto CInPacket_Decode2Original = reinterpret_cast<short(__thiscall*)(CInPacket*)>(0x0042470C);
auto CInPacket_Decode1Original = reinterpret_cast<char(__thiscall*)(CInPacket*)>(0x004065F3);

void CInPacket_Decode2(CInPacket* pPacket, void* edx) {

    CInPacket_Decode2Original(pPacket);
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
    if (nSkillID >= 1511000 && nSkillID < 2000000) {
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

    if ((nSkillID >= 5101000 && nSkillID < 5200000) || (nSkillID >= 5401000 && nSkillID < 5500000)) {
        if (get_weapon_type() == 48) {
            return true;
        }
    }
    if ((nSkillID >= 5200000 && nSkillID < 5300000) || (nSkillID >= 5500000 && nSkillID < 5600000)) {
        if (get_weapon_type() == 49) {
            return true;
        }
    }
    return false;
}

void doSpearPA() {
    WriteDouble(0x00BED58C, tbw);
    WriteDouble(0x00BED90C, oaxe);
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
        Patch4(0x0078F60A + 2, oaxeaddr);
        Patch4(0x0078F6B0 + 2, oaxeaddr);
        Patch4(0x008C2DFD + 2, oaxeaddr);
        Patch4(0x008C2E46 + 2, oaxeaddr);
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
        return "Swordlord";
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
        return "Berserker";
    case 210:
        return "Elementalist";
    case 211:
        return "F/P Magician";
    case 212:
        return "Archmagician F/P";
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
        return "Archmagician I/L";
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
        return "Stormshot";
    case 350:
        return "Crossbowman";
    case 351:
        return "Sentinel";
    case 352:
        return "Boltslinger";
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
        return "Reaper";
    case 450:
        return "Bandit";
    case 451:
        return "Smuggler";
    case 452:
        return "Mesomaster";
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
        return "Canoneer";
    case 540:
        return "Brawler";
    case 541:
        return "Striker";
    case 542:
        return "Tidemaster";
    case 550:
        return "Gunslinger";
    case 551:
        return "Captain";
    case 552:
        return "Corsair";
    default:
        return get_job_name_hook(nJob);
    }
}

auto isDarkSight_hook = (void*(__thiscall*)(void*))0x009581A9;

void*(__fastcall isDarkSight)(void* _this) {
    printf("0x%08X\n", (DWORD)_ReturnAddress());
    return isDarkSight_hook(_this);
}

auto isLeft = (int(__thiscall*)(void*))0x00451E42;

int(__fastcall isLeft_Hook)(void* _this) {
    if (isLeft(_this) == 1) {
        isLeftH = true;
    } else {
        isLeftH = false;
    }
    return isLeft(_this);
}

auto AddRush = (void(__thiscall*)(void*, int, int, int))0x009535C1;

void(__fastcall AddRush_Hook)(void* _this, void* edx, int a2, int vx, int a4) {
    switch ((int)_ReturnAddress()) {
    case 0x00952F74:
    case 0x00952F8C:
        DEBUG_MESSAGE("rush override");
        a2 = 1000;
    default:
        a2 = a2;
    }
    a2 *= isLeftH ? -1 : 1;
    return AddRush(_this, a2, vx, a4);
}

auto CMovePath__MakeMovePath = (void*(__thiscall*)(void*, int, void*, void*, void*,
        unsigned __int16, unsigned __int16, int, int, int, int))0x0068ab85;

void*(__fastcall CMovePath__MakeMovePath_Hook)(
        void* _this,
        void* edx,
        int nAttr,
        void* pfh,
        void* pfhStart,
        void** pLR,
        __int16 x,
        __int16 y,
        int vx,
        int vy,
        int nMoveAction,
        int tElapse) {
    if ((DWORD)_ReturnAddress() == 0x0052EFDA) {
        DEBUG_MESSAGE("nAttr: %d, x: %d, y: %d, vx: %d, vy: %d, nMoveAction: %d, Elapse: %d", nAttr, x, y, vx, vy, nMoveAction,
                tElapse);
        return CMovePath__MakeMovePath(_this, 8, pfh, pfhStart, pLR, 125, y, vx, vy, nMoveAction, 60);
    }
    return CMovePath__MakeMovePath(_this, nAttr, pfh, pfhStart, pLR, x, y, vx, vy, nMoveAction, tElapse);
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
    if ((MapID > 450001060 && MapID <= 450001067) || MapID == 401060100) {
        // all attacks will hit if not a shadowshifter proc
        Patch1(0x007930C5, 0xEB);
        Patch1(0x00793484, 0xEB);
    } else {
        Patch1(0x007930C5, 0x74);
        Patch1(0x00793484, 0x74);
    }
    if ((!immune && LastMapID == 450001064) || (!immune && LastMapID == 401060101) || (!immune && LastMapID == 450001060)) {
        immunetimer = chrono::steady_clock::now();
        immune = true;
        LastMapID = MapID;
    }
    auto elapsed = chrono::steady_clock::now() - immunetimer;
    if (elapsed < chrono::milliseconds(5000) && immune) {
        return;
    }
    immune = false;
    return SetDamaged_Hook(_this, nDamage, vx, vy, dwObstacleData, pMob, nAttackIdx, nDir, nPowerGuard, bCheckHitRemain,
            bSendPacket);
}

auto missileSpeed = (int(__cdecl*)(int, int, int))0x00942831;

int(__cdecl missileSpeed_Hook)(int a1, int a2, int a3) {
    if (a2 == 3211016 || a2 == 3601000) {
        return 60;
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


void changeMagicAttacks() {
    Patch4(0x00955D19 + 1, 2101008);
    Patch4(0x00955D24 + 1, 2101007);
    Patch4(0x00955D2F + 1, 2111010);
    Patch4(0x00955D3A + 1, 2111003);
    Patch4(0x00955D45 + 1, 2201010);
    Patch4(0x00955D50 + 1, 2211014);
    Patch4(0x00955D5B + 1, 2201013);
    Patch4(0x00955D66 + 1, 2511006);
    Patch4(0x00955D7C + 1, 2411012);
    Patch4(0x00955D87 + 1, 2111002);
}


auto hook_error = (void(__stdcall*)(int))0x00A5FDE4;

void(__stdcall lol)(int errornum) {
    printf("Error at: 0x%08X\n", (DWORD)_ReturnAddress());
    return hook_error(errornum);
}

auto hook_exception = (void(__stdcall*)(DWORD, DWORD))0x00A60BB7;

void(__stdcall texception)(DWORD errornum, DWORD othererror) {
    printf("Error at: 0x%08X\n", (DWORD)_ReturnAddress());
    return hook_exception(errornum, othererror);
}

bool isCopyCatSkill(int skillId) {
    int job = skillId / 10000;
    int secondDigit = (job / 10) % 10;
    int third = skillId / 1000;
    int thirdDigit = (third / 10) % 10;

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

void moveOffsets(int skillID) {
    if (skillID == 4211015 && isLeftH) {
        xPath = 300;
        doPath = true;
        return;
    }
    if (skillID == 4211015 && !isLeftH) {
        xPath = -300;
        doPath = true;
        return;
    }
    if (skillID == 1411005 && isLeftH) {
        xPath = 300;
        doPath = true;
        return;
    }
    if (skillID == 1411005 && !isLeftH) {
        xPath = -300;
        doPath = true;
        return;
    }
    if (skillID == 1411006 && isLeftH) {
        xPath = -300;
        doPath = true;
        return;
    }
    if (skillID == 1411006 && !isLeftH) {
        xPath = 300;
        doPath = true;
        return;
    }
    if (skillID == 1511003 && isLeftH) {
        xPath = 300;
        doPath = true;
        return;
    }
    if (skillID == 1511003 && !isLeftH) {
        xPath = -300;
        doPath = true;
        return;
    }
    if (skillID == 5411021 && isLeftH) {
        xPath = 300;
        doPath = true;
        return;
    }
    if (skillID == 5411021 && !isLeftH) {
        xPath = -300;
        doPath = true;
        return;
    }
    xPath = 0;
    doPath = false;
    return;
}

bool flying = false;


auto pDoActiveSkill = (int(__thiscall*)(CUserLocal*, int, int, int))0x00966F7A;

int(__fastcall CUserLocal__DoActiveSkill_Hook)(CUserLocal* _This, void* edx, int nSkillID, unsigned int nScanCode,
        int pnConsumeCheck) {
    setMAD();
    if (isCopyCatSkill(nSkillID)) {
        return CUserLocal__DoActiveSkill_Hook(_This, edx, nSkillID - 300000, nScanCode, pnConsumeCheck);
    }
    if (!isCorrectWeapon(nSkillID)) {
        return 0;
    }
    if (siegeMode && nSkillID == 3211016) {
        return CUserLocal__DoActiveSkill_Hook(_This, edx, 3601000, nScanCode, pnConsumeCheck);
    }

    if (nSkillID == 4211015) {
        Patch4(0x00952F20 + 3, 4211015);
    }
    if (nSkillID == 1511003) {
        Patch4(0x00952F20 + 3, 1511003);
    }
    if (nSkillID == 1411005) {
        Patch4(0x00952F20 + 3, 1411005);
    }
    if (nSkillID == 1411006) {
        Patch4(0x00952F20 + 3, 1411006);
    }
    if (nSkillID == 5411021) {
        Patch4(0x00952F20 + 3, 5411021);
    }
    DEBUG_MESSAGE("Combo: %d", getCurrentComboCount());
    if (nSkillID == 5411022) {
        if (getCurrentComboCount() < 100) {
            return 0;
        }
        CUserLocal__SendSkillCancelRequest(_This, 5410000);
    }
    moveOffsets(nSkillID);
    if (nSkillID == 3001013) {
        PatchNop(combat1, 2);
        Patch1(combat2, 0xf7);
        Patch1(combat2 + 1, 0xd8);
    }
    auto elapsed = chrono::steady_clock::now() - skilltimer;
    if (nSkillID == 3001013 || nSkillID == 1001007) {
        jumptimer = chrono::steady_clock::now();
    }
    if (elapsed < chrono::milliseconds(150)) {
        if ((nSkillID == 3001013) || nSkillID == 1001007) {
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
    return pDoActiveSkill(_This, nSkillID, nScanCode, pnConsumeCheck);
}

auto LoadSkillRoot_hook = (int(__cdecl*)(int, int, void*, int))0x0076119A;

int(__cdecl LoadSkillRoot)(int skillid, int exception, void* a4, int a5) {
    DEBUG_MESSAGE("%7d", skillid);
    return LoadSkillRoot_hook(skillid, exception, a4, a5);
}

auto is_guided_skill = (int(__cdecl*)(int))0x0076662D;

int(__cdecl is_guided_skill_hook)(int skillid) {
    if (skillid == 5211016 || skillid == 3201016) {
        return 1;
    }
    return is_guided_skill(skillid);
}

auto pGetSkillLevel = (int(__thiscall*)(int, void*, int, int))0x007616F6;

int(__fastcall GetSkillLevel)(int _this, void* edx, void* charData, int skillID, int skillEntry) {
    int i = skillID;
    int jobID = CWvsContext::GetInstance()->get_m_basicStat().nJob.Fuse();
    currStr =CWvsContext::GetInstance()->get_m_basicStat().nSTR.Fuse();
    if (i) {
        pGetSkillLevel(_this, charData, i, skillEntry);
        if (jobID == 310 || jobID == 342 || jobID == 312 || jobID == 311 || jobID == 341) {
            mastery = pGetSkillLevel(_this, charData, 3100000, skillEntry);
            critSkillID = 3000001;
        }
        if (jobID == 320 || jobID == 321 || jobID == 322 || jobID == 351 || jobID == 352) {
            mastery = pGetSkillLevel(_this, charData, 3200000, skillEntry);
            critSkillID = 3000001;
        }
        if (jobID == 410 || jobID == 411 || jobID == 412 || jobID == 441 || jobID == 442) {
            mastery = pGetSkillLevel(_this, charData, 4100000, skillEntry);
            critSkillID = 4100001;
        }
        if (jobID == 420 || jobID == 421 || jobID == 422 || jobID == 421 || jobID == 422) {
            mastery = pGetSkillLevel(_this, charData, 4200000, skillEntry);
        }
        if (jobID == 110 || jobID == 111 || jobID == 112 || jobID == 141 || jobID == 142) {
            mastery = pGetSkillLevel(_this, charData, 1100000, skillEntry);
        }
        if (jobID == 120 || jobID == 121 || jobID == 122 || jobID == 151 || jobID == 152) {
            mastery = pGetSkillLevel(_this, charData, 1200000, skillEntry);
            critSkillID = 1210015;
        }
        if (jobID == 210 || jobID == 211 || jobID == 212 || jobID == 241 || jobID == 242) {
            mastery = pGetSkillLevel(_this, charData, 2100001, skillEntry);
            DEBUG_MESSAGE("Mastery: %d", mastery);
        }
        if (jobID == 220 || jobID == 221 || jobID == 222 || jobID == 251 || jobID == 252) {
            mastery = pGetSkillLevel(_this, charData, 2200001, skillEntry);
        }

        if (jobID == 520 || jobID == 521 || jobID == 551 || jobID == 552 || jobID == 522) {
            mastery = pGetSkillLevel(_this, charData, 5200000, skillEntry);
        }
        if (jobID == 510 || jobID == 511 || jobID == 512 || jobID == 541 || jobID == 542) {
            mastery = pGetSkillLevel(_this, charData, 5110000, skillEntry);
        }
        tb = pGetSkillLevel(_this, charData, 15110000, skillEntry);
        mesos = pGetSkillLevel(_this, charData, 4511006, skillEntry);
        if (pGetSkillLevel(_this, charData, critSkillID, skillEntry) > 0) {
            Patch4(0x007650DB + 1, critSkillID);
        }
    }

    if ((int)_ReturnAddress() == 0x0095855D) {
        return pGetSkillLevel(_this, charData, 3410002, skillEntry);
    }
    return pGetSkillLevel(_this, charData, i, skillEntry);
}

auto get_cool_time = (int(__cdecl*)(int))0x009535E3;

int(__cdecl get_cool_time_t)(int nSkillID) {
    if (nSkillID == 1001007 || nSkillID == 3001013) {
        return 1000;
    }
    if (nSkillID == 5101003 || nSkillID == 5101002) {
        return 100;
    }
    return (get_cool_time(nSkillID));
}

auto remove_bullet_skill_hook = (int(__cdecl*)(int))0x007667EE;

int(__cdecl remove_bullets)(int nSkillID) {
    if (nSkillID == 4111012 || nSkillID == 5101012 || nSkillID == 5111017 || nSkillID == 3111009 || nSkillID == 3211016 || nSkillID == 3601000) {
        return 1;
    }
    return (remove_bullet_skill_hook(nSkillID));
}

void applyVelocityChange() {
    Patch4(0x0096C00A + 1, 0xFFFFFEA2);
    Patch4(0x0096C021 + 3, 0x0000015E);
    Patch4(0x0096C031 + 1, 0xFFFFFF06);
}

void restoreVelocityChange() {
    Patch4(0x0096C00A + 1, 0xFFFFFD55);
    Patch4(0x0096C021 + 3, 0x0000025E);
    Patch4(0x0096C031 + 1, 0xFFFFFD50);
}

const DWORD FlashJumpVar = 0x0096BF52;
const DWORD FlashJumpRet = 0x0096BF12;

void __declspec(naked) FlashJumpAll() {
    _asm {
            cmp eax, 4101009
            je[applyOverride]
            jmp FlashJumpRet

            applyOverride :
            push ebp
            mov ebp, esp
            call applyVelocityChange
            mov esp, ebp
            pop ebp
            jmp[fjvar]

            applyDefault:
            push ebp
            mov ebp, esp
            call restoreVelocityChange
            mov esp, ebp
            pop ebp
            jmp[fjvar]

            fjvar : jmp[FlashJumpVar]
    }
} //

auto pDoJump = (int(__thiscall*)(int, int))0x0094C383;

int(__fastcall CUserLocal_Jump)(int _this, void* edx, int a2) {
    auto elapsed = chrono::steady_clock::now() - jumptimer;
    if (siegeMode) {
        return 0;
    }
    if (elapsed < chrono::milliseconds(150)) {
        return 0;
    }
    skilltimer = chrono::steady_clock::now();
    return pDoJump(_this, a2);
}

auto calcpdamage_hook = (void*(__thiscall*)(int, int, int, int, int, int, int, int, int, int, int, int, int, int, int,
        int, int, int, int, int, int))0x0078DF87;

void*(__fastcall CalcDamage__PDamage)(
        int _this,
        void* edx,
        int a2,
        int bs,
        int a4,
        int a5,
        int a6,
        int a7,
        int nDamagePerMob,
        int nItemID,
        int a10,
        int a11,
        int nAction,
        int shadow_partner,
        int a14,
        int a15,
        int a16,
        int a17,
        int a18,
        int a19,
        int a20,
        int a21) {
    switch (get_weapon_type()) {
    case 45:
    case 46:
    case 47:
    case 49:
        return calcpdamage_hook(_this, a2, bs, a4, a5, a6, a7, nDamagePerMob, nItemID, a10, 1,
                nAction, shadow_partner, a14, a15, a16, a17, a18, a19, a20, a21);
    default:
        return calcpdamage_hook(_this, a2, bs, a4, a5, a6, a7, nDamagePerMob, nItemID, a10, a11,
                nAction, shadow_partner, a14, a15, a16, a17, a18, a19, a20, a21);
    }
}


auto skillDelayHook = (int(__cdecl*)(int))0x00765047;

int(__cdecl summondelay)(int nSkillID) {
    return 0;
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

// auto SecondaryStat__SetFrom_Hook = (void(__thiscall*)(int, int, int, int, int, int, int))0x0077F4C9;
// int(__fastcall SecondaryStat__SetFrom)(int ss, void* edx, int cd, int bs, int fs, int a3, int a4, int a5) {
//
// }

auto pGetAttackSpeedDegree = (void(__thiscall*)(int, int, int, int))0x00765066;

int(__cdecl GetAttackSpeedDegree)(int nDegree, int nSkillID, int nWeaponBooster, int nPartyBooster) {
    int nWeaponDegree = nDegree;
    if (mastery > 0) {
        nWeaponDegree -= 2;
    }
    return nWeaponDegree;
}

auto octHook = (int(__cdecl*)(int))0x00766612;

int(__cdecl octopus)(int nSkillID) {
    if (nSkillID == 3121013 || nSkillID == 5511015 || nSkillID == 5511014 || nSkillID == 5521016 || nSkillID == 5111015) {
        return 1;
    }
    return octHook(nSkillID);
}

auto ltrbshoothook = (int(__cdecl*)(int))0x00766722;

int(__cdecl ltrb)(int nSkillID) {
    if (nSkillID == 5101012 || nSkillID == 4111012 || nSkillID == 4101008 || nSkillID == 3101011 || nSkillID == 3111009 || nSkillID == 3001004) {
        return 1;
    }
    return ltrbshoothook(nSkillID);
}

auto get_vertical_adjust_of_attack_range = (int(__cdecl*)(int))0x0076664D;

int(__cdecl vertical)(int nSkillID) {
    return 500;
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
    double min = mastery > 0 ? (mastery * 0.05) + 0.1 : 0.10;
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

auto chainLightning_Hook = (signed int(__thiscall*)(int*, int, int, int*, int))0x0075BF50;

int __fastcall drop_off_damage_skills(int* a1, void* edx, int a3, int a4, int* a5, int a6) {
    int* v6;
    int i;
    for (i = 0; i < 15; i++) {
        double dMultiplier = 1.2;

        int j;

        for (j = 0; j < i; j++) {
            dMultiplier *= 1.2;
        }

        *(double*)(0x00BDB470 + i * sizeof(double)) = dMultiplier;
    }
    // Log("%7d", chainLightning_Hook(a1, a2, a3, a4 ,a5, a6));
    return chainLightning_Hook(a1, a3, a4, a5, a6);
}


// auto SetAttackAction_Hook = (signed int(__thiscall*)(int*, int, int, int*, int))0x0092EDB2;
//
// int __fastcall setAttackAction(int* a1, void* edx, int a3, int a4, int* a5, int a6) {
//     int nWeaponDegree;
//     switch (get_weapon_type()) {
//     case 32:
//     case 37:
//         nWeaponDegree = 4;
//         break;
//     case 38:
//         nWeaponDegree = 7;
//         break;
//     default:
//         nWeaponDegree = a3;
//         break;
//     }
//     return SetAttackAction_Hook(a1, a3, nWeaponDegree, a5, a6);
// }

auto ShowSkillEffect_hook = (void(__thiscall*)(void*, void*, int, int, int, int, tagPOINT*))0x00933990;

void __fastcall ShowSkillEffect(
        void* _this,
        void* ecx,
        void* pSkill,
        int nSLV,
        int nActionSpeed,
        int bLeft,
        int nLast,
        tagPOINT* pPtOffset) {
    int nWeaponDegree = 4;
    switch (get_weapon_type()) {
    case 32:
    case 37:
        nWeaponDegree = 4;
        break;
    case 38:
        nWeaponDegree = 7;
        break;
    default:
        nWeaponDegree = nActionSpeed;
        break;
    }
    return ShowSkillEffect_hook(_this, pSkill, nSLV, nWeaponDegree, bLeft, nLast, pPtOffset);
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

auto mastery_Calcs_Hook = (int(__cdecl*)(int, int, int, int, int, int))0x00764795;

int __cdecl mCalc(int a1, int a2, int a3, int a4, int a5, int a6) {
    if (mastery > 10 || mastery < 0) {
        return 10;
    }
    return mastery;
}

auto ztlSecureFuse_short = (unsigned int(__cdecl*)(int, int))0x004746DD;

unsigned int __cdecl ztlfuse_short(int a1, int a2) {
    return ztlSecureFuse_short(a1, a2);
}

auto getPAD = (int(__thiscall*)(void*, int, int))0x0077DF48;

int __fastcall getPAD_hook(void* thisptr, void* edx, int a2, int a3) {
    pad = getPAD(thisptr, a2, a3);
    return getPAD(thisptr, a2, a3);
}

double ropebase = 3.0;

void ropeFormula() {
    double rope;
    speed = 100 + CWvsContext::GetInstance()->get_m_secondaryStat().m_speed.Fuse();
    rope = 3.0 * (speed / 100.0);
    if (rope < 3.0) {
        rope = 3.0;
    }
    if (ropebase != rope) {
        ropebase = rope;
        Patch4(0x009CC6F9 + 2, 0x00C1CF80); // switch addy
        WriteDouble(0x00C1CF80, rope);      // Addy speed control
    }
}


auto getSpeed = (int(__thiscall*)(void*))0x008C457C;

int __fastcall getSpeed_hook(void* thisptr, void* edx) {
    ropeFormula();
    return getSpeed(thisptr);
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
    if ((int)_ReturnAddress() == 0X008C35C9) {
        int job = CWvsContext::GetInstance()->get_m_basicStat().nJob.Fuse();
        bool isMage = (job >= 200 && job < 300);
        setMAD();
        if (isMage) {
            a2 = magicchar;
        } else {
            a2 = weaponchar;
        }
    }
    if ((int)_ReturnAddress() == 0X008C3400) {
        int job = CWvsContext::GetInstance()->get_m_basicStat().nJob.Fuse();
        bool isMage = (job >= 200 && job < 300);
        if (isMage) {
            a2 = sussychar;
        }
    }
    return hook_bstr_t(Level, a2);
}


auto AniCancel = (void(__thiscall*)(void*, int))0x00453A29;

void(__fastcall ClearActionLayer_t)(void* _this, void* dead, int a2) {
    return AniCancel(_this, a2);
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
    if (nSkillID == 1201013 || nSkillID == 1201016) {
        return 1;
    }
    return 0;
}

auto GetOneTimeAction = (int(__thiscall*)(void*))0x00451B6A;

int(__fastcall tGetOneTimeAction)(void* _this) {
    int ota = GetOneTimeAction(_this);
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


auto isMoveableSkill = (int(__cdecl*)(int))0x0095F96F;

int(__cdecl isMoveableSkillt)(int nSkillID) {
    if (nSkillID == 3111009 || nSkillID == 3121004 || nSkillID == 5221004) {
        return 1;
    } else {
        return isMoveableSkill(nSkillID);
    }
}

auto _is_attack_area_set_by_data = (int(__cdecl*)(int))0x7666CB;

int(__cdecl is_attack_area_set_by_data)(int nSkillID) {
    if (nSkillID == 4101008 || nSkillID == 4111012 || nSkillID == 5101012 || nSkillID == 5111017 || nSkillID == 3111009) {
        return 1;
    }
    return _is_attack_area_set_by_data(nSkillID);
}

void AttachSkillEdits() {
    // ATTACH_HOOK(MesoFormula, mesoFormulaHook);
    ATTACH_HOOK(getPAD, getPAD_hook);
    ATTACH_HOOK(hook_bstr_t, bstrt);
    // ATTACH_HOOK(ClearActionLayer_t, ClearActionLayer_t);
    // ATTACH_HOOK(DoActiveSkill_Prepare_t, DoActiveSkill_Prepare_t);
    // ATTACH_HOOK(is_keydown_skill_t, is_keydown_skill_t);
    // ATTACH_HOOK(tGetOneTimeAction, tGetOneTimeAction);
    // // ATTACH_HOOK(tSetOneTimeAction, tSetOneTimeAction);
    // // ATTACH_HOOK(tOnResolveMoveAction, tOnResolveMoveAction);
    // // ATTACH_HOOK(tget_flipX, tget_flipX);
    // ATTACH_HOOK(tOnSkillKeyDownEnd, tOnSkillKeyDownEnd);
    // ATTACH_HOOK(isMoveableSkillt, isMoveableSkillt);
    ATTACH_HOOK(pDoActiveSkill, CUserLocal__DoActiveSkill_Hook);
    ATTACH_HOOK(missileSpeed, missileSpeed_Hook);
    // ATTACH_HOOK(chainLightning_Hook, chainLightning_Hook);
    ATTACH_HOOK(AddRush, AddRush_Hook);
    ATTACH_HOOK(pGetSkillLevel, GetSkillLevel);
    ATTACH_HOOK(_is_attack_area_set_by_data, is_attack_area_set_by_data);
    // ATTACH_HOOK(ztlSecureFuse_short, ztlfuse_short);
    // ATTACH_HOOK(mastery_Calcs_Hook, mCalc);
    // ATTACH_HOOK(calcpdamage_hook, CalcDamage__PDamage);
    // ATTACH_HOOK(remove_bullet_skill_hook, remove_bullets);
    // ATTACH_HOOK(ztlSecureFuse_double_check, ztlfuse_double);
    // ATTACH_HOOK(jobCode, jobCode_hook);
    ATTACH_HOOK(meso_bag_handle, siegeModePacket);
    ATTACH_HOOK(ShowSkillEffect_hook, ShowSkillEffect);
    // ATTACH_HOOK(SetAttackAction_Hook, setAttackAction);
    CodeCave((void*)please, 0x00791C41, 4);
    CodeCave((void*)FlashJumpAll, 0x0096BF0B, 0);
    PatchNop(0x0096C073, 6);
    CodeCave((void*)DamCalc, 0x00791BAE, 1);
    skillHacks();
    changeMagicAttacks();
    AttachSkillOffsetMod();
    // // Instant FA
    Patch1(0x0095795E, 0x83);
    Patch1(0x0095795E + 1, 0xC0);
    Patch1(0x0095795E + 2, 0x00);

    // Re-point the engine's hardcoded Meso Explosion skill ID (4211006) to 4511006.
    // Eight immediate operands across CDropPool::Update, CalcDamage_MesoExplosion,
    // CUserRemote::OnAttack/OnMeleeAttack, and the sub_4FB292 skill-type filter.
    Patch4(0x004FB2ED, 4511006); // cmp esi, imm32  (sub_4FB292)
    Patch4(0x00504CC0, 4511006); // push imm32      (CDropPool::Update)
    Patch4(0x00504D15, 4511006); // push imm32      (CDropPool::Update)
    Patch4(0x00791FCF, 4511006); // push imm32      (CalcDamage_MesoExplosion)
    Patch4(0x00980543, 4511006); // cmp [ebp-10h], imm32 (CUserRemote::OnAttack)
    Patch4(0x009805D1, 4511006); // push imm32      (CUserRemote::OnAttack)
    Patch4(0x00981045, 4511006); // cmp [ebp-14h], imm32 (CUserRemote::OnMeleeAttack)
    Patch4(0x009810B0, 4511006); // push imm32      (CUserRemote::OnMeleeAttack)

    comboStuff();
}


#include "hook.h"
#include "wvs/packet.h"
#include "sstream"

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

typedef void(__fastcall* SetFromWhenDoom_t)(MobStat* pThis, void* edx, MobTemplate* pTemplate);
static auto SetFromWhenDoom = reinterpret_cast<SetFromWhenDoom_t>(0x00789EFD);

typedef MobTemplate*(__cdecl* GetMobTemplate_t)(int templateId);
static auto GetMobTemplate = reinterpret_cast<GetMobTemplate_t>(0x0067CD28);

SetFromWhenDoom_t Hook_FromWhenDoom = [](MobStat* pThis, void* edx, MobTemplate* pTemplate) -> void {
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
};

typedef void(__fastcall* OnDoomed_t)(Mob* pThis, void* edx, int bDoom);
static auto OnDoomed = reinterpret_cast<OnDoomed_t>(0x0066D6D4);

OnDoomed_t Hook_Doom = [](Mob* pThis, void* edx, int bDoom) -> void {
    int templateId = 0;
    if (bDoom) {
        templateId = ZtlSecureFuse(pThis->m_pTemplate->_ZtlSecureTear_dwTemplateID, pThis->m_pTemplate->_ZtlSecureTear_dwTemplateID_CS);
        Patch4(0x0066D722 + 1, templateId);
    }
    OnDoomed(pThis, edx, bDoom);
    if (templateId != 0) {
        MobTemplate* mobTemplate = GetMobTemplate(100100);
        MobTemplate* currMonsterTemplate = GetMobTemplate(templateId);
        mobTemplate->_ZtlSecureTear_dwTemplateID[0] = currMonsterTemplate->_ZtlSecureTear_dwTemplateID[0];
        mobTemplate->_ZtlSecureTear_dwTemplateID[1] = currMonsterTemplate->_ZtlSecureTear_dwTemplateID[1];
        mobTemplate->_ZtlSecureTear_dwTemplateID_CS = currMonsterTemplate->_ZtlSecureTear_dwTemplateID_CS;
        pThis->m_pTemplateByDoom = mobTemplate;
    }
};

auto mesoFormulaHook = (int(__thiscall*)(void *, void *, BasicStat*, SecondaryStat*, MobStat *, int *, unsigned int, int *))0x00791FBC;
int __fastcall MesoFormula(void *pThis, PVOID edx, void *cd, BasicStat *bs, SecondaryStat *ss, MobStat *ms, int *anMoneyAmount,
                           unsigned int dwDropFlag, int *aDamage) {
    long double ratio;
    int nAttackCount;
    int i;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<double> dist(.1 + mastery * 0.05, 1.00);
    random_number = dist(gen);

    nAttackCount = 0;
    for (i = 0; i < 15; ++i) {
        if (((1 << i) & dwDropFlag) != 0) {
            ratio = ((3.6 * bs->nLUK.Fuse() + bs->nSTR.Fuse() + bs->nDEX.Fuse()) * pad / 100) * random_number;
            __int64 damage = (__int64)(ratio * (0.6 + (0.02 * mesos)));
            *aDamage = damage;
            ++nAttackCount;
            ++aDamage;
        }
        ++anMoneyAmount;
    }
    return nAttackCount;
};

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
            return (v3 == 37 || v3 == 38);
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

    // Close Range Attacks
    Patch1(0x009516C2, 0xE9);
    Patch1(0x009516C2 + 1, 0xc8);
    Patch1(0x009516C2 + 2, 0xfc);
    Patch1(0x009516C2 + 3, 0xff);
    Patch1(0x009516C2 + 4, 0xff);

    // Remove If you do not use your AP when you level up POP UP
    Patch1(0x00A20091, 0xEB);

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


    // Maker Skill Instant
    Patch1(0x826F92 + 2, 0x08);
    Patch1(0x826F92 + 3, 0x01);
    Patch1(0x826F92 + 4, 0x00);
    Patch1(0x826F92 + 5, 0x00);
    // Allow usage of pots while in Dark Sight skill
    FillBytes(0x0094F6AB, 0x90, 6);
    // Allow double click pots while in Dark Sight skill
    FillBytes(0x004F0311, 0x90, 6);
    // //
    // // // Super Tubi
    FillBytes(0x00485C01, 0x90, 2);
    FillBytes(0x00485C21, 0x90, 2);
    FillBytes(0x00485C32, 0x90, 2);
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

    // Enable Teleport mid air -
    // Ezrosia V2 ()newer ones) FillBytes(0x00957C2D, 0x90, 6);
    PatchNop(0x00957C2D, 6);


    // CritBypass
    PatchNop(0x007650B3, 29);

    // uiStat stuff
    Patch1(0x008C35C9 + 1, 0x2C); // weapon def
    Patch1(0x008C374A + 1, 0x1A); // weapon def
    Patch1(0x008C39E9 + 1, 0x62); // weapon def
    Patch1(0x008C3B9C + 1, 0x50); // weapon def
    Patch1(0x008C3D4F + 1, 0x3E); // weapon def
    Patch1(0x008C3F8E + 1, 0x74); // weapon def
    PatchNop(0x00668C04, 5);

    CodeCave((void*)NW_Multi, nwthrow, 0);
    Patch1(0x0078EDB1 + 1, 0x84);
    CodeCave((void*)Claw_5, 0x0078EDB1, 1);
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

    // strength accuracy calc
    //CodeCave(AdjustAccuracyCalc, dwAccuracyCalc, 5);

    ATTACH_HOOK(getSpeed, getSpeed_hook);

    ATTACH_HOOK(setInput, setInput_hook);
    ATTACH_HOOK(is_skill_need_master_level, masteryskill);
    ATTACH_HOOK(get_job_name_hook, get_job_name);
    ATTACH_HOOK(is_shoot_action, is_shoot_action_hook);
    ATTACH_HOOK(get_cool_time, get_cool_time_t);

    ATTACH_HOOK(CUIToolTip__DrawItemTitle, DrawItemTitleHook);
    ATTACH_HOOK(CMapLoadable__SetFieldMagLevel, CMapLoadable__SetFieldMagLevel_t);
    ATTACH_HOOK(DrawStat, DrawStat_t);
    ATTACH_HOOK(SetFromWhenDoom, Hook_FromWhenDoom);
    ATTACH_HOOK(OnDoomed, Hook_Doom);
    ATTACH_HOOK(mesoFormulaHook, MesoFormula);
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


inline LONGLONG myArrayForCustomEXP[] = { 1, 15, 44, 96, 188, 312, 550, 731, 969, 1154, 1358, 1358, 1810, 2308, 2856, 3464, 4134, 4872, 5688, 6588, 7582, 8678, 9890, 11224, 12698, 14326, 16122, 18102, 20290, 22704, 25368, 28308, 31554, 35136, 39090, 43454, 48272, 53588, 59458, 65938, 73090, 80984, 89700, 99320, 109940, 121666, 134608, 148896, 164670, 182084, 201308, 222532, 245962, 271830, 300386, 331914, 366722, 405150, 447574, 494414, 546126, 603218, 666250, 735840, 812672, 897498, 991150, 1094548, 1208706, 1334744, 1473896, 1627532, 1797156, 1984432, 2191198, 2419484, 2671528, 2949804, 3257042, 3596258, 3970778, 4384278, 4840816, 5344870, 5901388, 6515830, 7194226, 7749044, 8173766, 8621760, 9094306, 9592748, 10118504, 10673072, 11258030, 11875046, 12525870, 13212362, 13936472, 14700264, 15505912, 16355710, 17252078, 18197566, 19194866, 20246818, 21356418, 22526822, 23761366, 25063564, 26437120, 27885948, 29414172, 31026142, 32726446, 34519930, 36411696, 38407132, 40511916, 42732042, 45073830, 47543950, 50149432, 52897694, 55796562, 58854286, 62079574, 65481608, 69070074, 72855188, 76796801, 80894913, 85149524, 89560634, 94128243, 98852351, 103732958, 108770064, 113963669, 119313773, 124820376, 130483478, 136303079, 142279179, 148411778, 154700876, 161146473, 167748569, 174507164, 181422258, 188493851, 195721943, 203106534, 210647624, 218345213, 226199301, 234209888, 242376974, 250700559, 259180643, 267817226, 276610308, 285559889, 294665969, 303928548, 313347626, 322923203, 332655279, 342543854, 352588928, 362790501, 373148573, 383663144, 394334214, 405161783, 416145851, 427286418, 438583484, 450037049, 461647113, 473413676, 485336738, 497416299, 509652359, 522044918, 534593976, 547299533, 560161589, 573180144, 586355198, 599686751, 613174803, 626819354, 640620404, 654577953, 668692001, 682962548, 697389594, 711973139, 726713183, 741609726, 756662768, 757566672, 758562688, 759658688, 760874688, 762214688, 763690688, 765322688, 767122688, 769110688, 771302688, 775686688, 780534688, 785870688, 791766688, 798278688, 805462688, 813382688, 822134688, 831790688, 842446688, 863638688, 887110688, 913030688, 941558688, 972950688, 1007462688, 1046286688, 1089006688, 1136206688, 1188470688, 1293102688, 1408462688, 1536002688, 1677434688, 1834746688, 2009962688, 2205594688L, 2424154688L, 2668594688L, 2942194688L, 3489882688L, 4093322688L, 4758522688L, 5491962688L, 6300602688L, 7191922688L, 8173982688L, 9255474688L, 10445794688L, 11757434688L };

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
