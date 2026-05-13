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
struct MobStat {
    int  nLevel;                 // +0x00
    int  aDamagedElemAttr[8];    // +0x04 .. +0x23  (32 bytes)
    int  nPAD;                   // +0x24
    int  _padPAD[7];
    int  nMAD;                   // +0x44
    int  _padMAD[7];
    int  nACC;                   // +0x64
    int  _padACC[7];
    int  nEVA;                   // +0x84
    int  _padEVA[7];
    int  nPDR;                   // +0xA4 (guess)
    int  _padPDR[7];
    int  nMDR;                   // +0xC4 (guess)
    int  _padMDR[7];
    int  nSpeed;                 // +0xE4 (guess)
    int  _padSpeed[7];
};

static_assert(offsetof(MobStat, aDamagedElemAttr) == 0x04, "MobStat::aDamagedElemAttr offset mismatch");
static_assert(offsetof(MobStat, nPAD) == 0x24, "MobStat::nPAD offset mismatch");
static_assert(offsetof(MobStat, nMAD) == 0x44, "MobStat::nMAD offset mismatch");
static_assert(offsetof(MobStat, nACC) == 0x64, "MobStat::nACC offset mismatch");
static_assert(offsetof(MobStat, nEVA) == 0x84, "MobStat::nEVA offset mismatch");


struct MobTemplate {
    unsigned char _pad0[0x118];
    int  aDamagedElemAttr[8];                     // +0x118
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
