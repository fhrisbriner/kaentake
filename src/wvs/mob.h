//
// Created by Gwen on 2026-04-29.
//
#pragma once
#include <cstddef>
#include "../hook.h"


// Confirmed from IDA sub_789EFD (CMobStat::SetFromWhenDoom @ 0x00789EFD):
//   memcpy(this+4, pTemplate+280, 32)        -> aDamagedElemAttr
//   *(int*)(this+36)  = fuse(pTemplate+184)
//   *(int*)(this+68)  = fuse(pTemplate+208)
//   *(int*)(this+100) = fuse(pTemplate+232)
//   *(int*)(this+132) = fuse(pTemplate+148)
// The 4 stat slots written by the engine sit on a 32-byte stride.
// nPDR/nMDR/nSpeed are not touched by sub_789EFD; their offsets here continue
// the same stride and should be verified before relying on them.
class MobStat {
public:
    int nLevel; // 0x0
    int aDamagedElemAttr[8]; // 0x4
    int nPAD; // 0x24
    int nPAD_; // 0x28
    int rPAD_; // 0x2C
    int tPAD_; // 0x30
    int nPDR; // 0x34
    int nPDR_; // 0x38
    int rPDR_; // 0x3C
    int tPDR_; // 0x40
    int nMAD; // 0x44
    int nMAD_; // 0x48
    int rMAD_; // 0x4C
    int tMAD_; // 0x50
    int nMDR; // 0x54
    int nMDR_; // 0x58
    int rMDR_; // 0x5C
    int tMDR_; // 0x60
    int nACC; // 0x64
    int nACC_; // 0x68
    int rACC_; // 0x6C
    int tACC_; // 0x70
    int nEVA; // 0x74
    int nEVA_; // 0x78
    int rEVA_; // 0x7C
    int tEVA_; // 0x80
    int nSpeed; // 0x84
    int nSpeed_; // 0x88
    int rSpeed_; // 0x8C
    int tSpeed_; // 0x90
    char padding1[0x8C]; // 0x94
    int nPImmune_; // 0x120
    char padding2[0xC4]; // 0x124
    int bInvincible;
};

static_assert(offsetof(MobStat, aDamagedElemAttr) == 0x04, "MobStat::aDamagedElemAttr offset mismatch");
static_assert(offsetof(MobStat, nPAD) == 0x24, "MobStat::nPAD offset mismatch");
static_assert(offsetof(MobStat, nMAD) == 0x44, "MobStat::nMAD offset mismatch");
static_assert(offsetof(MobStat, nACC) == 0x64, "MobStat::nACC offset mismatch");
//static_assert(offsetof(MobStat, nEVA) == 0x84, "MobStat::nEVA offset mismatch");


// Confirmed from IDA sub_789EFD: aDamagedElemAttr is 32 bytes at +0x118 (decimal 280).
// Other fields below match the names already referenced in skills.cpp; offsets
// for the secured template-id fields are not yet verified against the binary.
struct MobTemplate {
    unsigned char _pad0[0x118];
    int  aDamagedElemAttr[8];                     // +0x118
    unsigned char _pad1[0x40];
    int  _ZtlSecureTear_dwTemplateID[2];          // +0x158..+0x15F (verify)
    unsigned int _ZtlSecureTear_dwTemplateID_CS;  // +0x160         (verify)
};

static_assert(offsetof(MobTemplate, aDamagedElemAttr) == 0x118, "MobTemplate::aDamagedElemAttr offset mismatch");


// Confirmed from IDA sub_66D6D4 (CMob::OnDoomed @ 0x0066D6D4):
//   *(this + 0x188)  -- non-null pointer, set/cleared as primary template
//   *(this + 0x18C)  -- non-null pointer set to GetMobTemplate(100101) (m_pTemplateByDoom)
//   sub_789EFD((this + 0x1A0), pTemplate)  -- MobStat lives inline at +0x1A0
struct Mob {
    unsigned char _pad0[0x188];
    MobTemplate*  m_pTemplate;        // +0x188
    MobTemplate*  m_pTemplateByDoom;  // +0x18C
    unsigned char _pad1[0x1A0 - 0x18C - sizeof(MobTemplate*)];
    MobStat       m_stat;             // +0x1A0
};

static_assert(offsetof(Mob, m_pTemplate)       == 0x188, "Mob::m_pTemplate offset mismatch");
static_assert(offsetof(Mob, m_pTemplateByDoom) == 0x18C, "Mob::m_pTemplateByDoom offset mismatch");
static_assert(offsetof(Mob, m_stat)            == 0x1A0, "Mob::m_stat offset mismatch");
