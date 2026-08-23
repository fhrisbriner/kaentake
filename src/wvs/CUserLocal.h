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
    // NOT a boolean despite the name -- this is the avatar's m_nMoveAction (CUser+0x88 is
    // m_CAvatar, CAvatar+0x4E8 is m_nMoveAction, and 0x88+0x4E8 = 0x570). Values are action
    // codes like 4 or 9; CAvatar::SetMoveAction groups them with (a2 & ~1), so bit 0 is the
    // direction and everything above it is the action. Facing LEFT is the EVEN case:
    //     bool faceLeft = m_isLeft % 2 == 0;
    // Testing it for non-zero reads "left" almost always and is the obvious way to get this wrong.
    MEMBER_AT(int, 0x570, m_isLeft)
    MEMBER_AT(IWzVector2DPtr, 0x11A4, m_pvc)
};