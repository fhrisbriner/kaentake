//
// Created by Gwen on 4/16/2026.
//
#pragma once
#include <cstdint>
#include "../ztl/tsingleton.h"
#include "../hook.h"
#include "secure.h"
#include "tempstat.h"

class BasicStat {
public:
    ZtlSecure<int>	nGender;
    ZtlSecure<int>	nLevel;
    ZtlSecure<int>	nJob;
    ZtlSecure<int>	nSTR;
    ZtlSecure<int>	nDEX;
    ZtlSecure<int>	nINT;
    ZtlSecure<int>	nLUK;
    ZtlSecure<int>	nPOP;
    ZtlSecure<int>	nMHP;
    ZtlSecure<int>	nMMP;
};

class SecondaryStat {
public:
    MEMBER_AT(ZtlSecure<int>, 0x60, m_magic)
    MEMBER_AT(ZtlSecure<int>, 0x6C, m_bonusMagic)
    MEMBER_AT(ZtlSecure<int>, 0x15C, m_speed)
    MEMBER_AT(ZtlSecure<int>, 0x294, m_invincible)
    MEMBER_AT(ZtlSecure<int>, 0x474, m_mesoGuard)
};

class CWvsContext : public TSingleton<CWvsContext, 0x00BE7918> {
public:
    MEMBER_AT(BasicStat, 0x20BC, m_basicStat)
    MEMBER_AT(SecondaryStat, 0x2134, m_secondaryStat)
    MEMBER_AT(CTemporaryStatView, 0x2EA8, m_temporaryStatView)

};
