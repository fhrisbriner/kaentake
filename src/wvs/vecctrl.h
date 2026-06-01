#pragma once
#include "secure.h"
#include <cstddef>

// Minimal CVecCtrl layout (v83 `Angel.exe`, base size 0x1B0).
// Reversed from CVecCtrl::CalcFloat (0x009B2C3C) / Wings (0x009B21DA) / ctor (0x009B0F71).
// Secure doubles use the same ZtlSecure<double> { double at[2]; uint cs; } layout the
// client uses: x/y position and vx/vy velocity. Read with .Fuse(), write with `= value`.
// Gaps left opaque (minimal scope) -- only fields we touch are named.

class CVecCtrl  : public ZRefCounted, public IWzVector2D {
public:
    void* vfptr;                 // 0x00 IWzVector2D / IUnknown vtable
    char  gap04[0x1C];           // 0x04 refcount, 2nd base vtable @0x0C, etc.
    ZtlSecure<double> m_x;       // 0x20 position x   (at 0x20, cs 0x30)
    ZtlSecure<double> m_y;       // 0x38 position y   (at 0x38, cs 0x48)
    ZtlSecure<double> m_vx;      // 0x50 velocity x   (at 0x50, cs 0x60)
    ZtlSecure<double> m_vy;      // 0x68 velocity y   (at 0x68, cs 0x78)
    char  gap80[0xF8];           // 0x80
    int   m_nJumpAttr;           // 0x178 cleared at start of Wings()
    int   m_bWingsNow;           // 0x17C set to 1 by Wings()
    char  gap180[0x20];          // 0x180
    void* m_pMoveData0;          // 0x1A0 TSecType<double>* physics array
    char  gap1A4[4];             // 0x1A4
    void* m_pMoveData1;          // 0x1A8 TSecType<double>* physics array
    char  gap1AC[4];             // 0x1AC -> CMovePath sub-object follows
    // Convert IWzVector2D* (from m_pvc) to CVecCtrl*: accounts for rc_vc multiple-inheritance offset
    static CVecCtrl* FromInterface(IWzVector2D* p) {
        return p ? reinterpret_cast<CVecCtrl*>(reinterpret_cast<char*>(p) - 0xC) : nullptr;
    }
};
