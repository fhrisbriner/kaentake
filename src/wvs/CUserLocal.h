//
// Created by Gwen on 4/16/2026.
//
#pragma once
#include <cstdint>
#include "../ztl/tsingleton.h"
#include "../hook.h"
#include "secure.h"
#include "tempstat.h"


class CUserLocal : public TSingleton<CUserLocal, 0xBEBF98> {
public:
    MEMBER_AT(int, 0x88, m_avatar)
    MEMBER_AT(int, 0x570, m_isLeft)
    MEMBER_AT(IWzVector2DPtr, 0x11A4, m_pvc)
};