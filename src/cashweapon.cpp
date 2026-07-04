#include "pch.h"
#include "hook.h"
#include "wvs/util.h"
#include "ztl/ztl.h"
#include <windows.h>
#include <set>
#include <cwctype>


// Cash weapons (item IDs 1701000-1702999) store their avatar stance frames under
// subnodes named by the *base* weapon type (e.g. "30" 1H-sword, "33" dagger,
// "40" 2H-sword ...). The client draws the cash weapon by indexing the img with
// the type of the regular weapon the player actually wields. If the cash weapon
// img lacks a subnode for that type, the overlay can't resolve and the weapon
// fails to appear / equip cleanly -- so a cash weapon only works over the base
// types it happened to ship art for.
//
// Fix: the first time an equipped cash weapon is seen, alias every standard
// weapon-type subnode it is missing to a donor type it already has. The art is
// the cosmetic override and is near-identical across types, so reusing one
// stance for all base weapons makes the cash weapon equippable universally.
// Property-put on a loaded WZ node is honored at runtime (see inlink.cpp).

static std::set<int> g_sFixedCashWeapon;

// Standard avatar weapon-stance types the client may request for the overlay.
static const wchar_t* k_apszWeaponType[] = {
    L"30", L"31", L"32", L"33", L"37", L"38",
    L"40", L"41", L"42", L"43", L"44",
    L"45", L"46", L"47", L"48", L"49",
};

void FixCashWeaponImg(int nItemID) {
    // Cash weapon range only.
    if (nItemID / 1000 != 1701 && nItemID / 1000 != 1702) {
        return;
    }
    if (g_sFixedCashWeapon.count(nItemID)) {
        return;
    }

    wchar_t sPath[64];
    swprintf(sPath, 64, L"Character/Weapon/%08d.img", nItemID);
    IWzPropertyPtr pWeapon = get_rm()->GetObjectA(sPath).GetUnknown();
    if (!pWeapon) {
        return; // not loaded yet -- retry on a later avatar update
    }
    g_sFixedCashWeapon.insert(nItemID);

    // Find a donor stance node: first numeric-named child (skip info/icon/...).
    Ztl_bstr_t sDonor;
    IEnumVARIANTPtr pEnum = pWeapon->_NewEnum;
    while (true) {
        Ztl_variant_t vNext;
        ULONG uFetched;
        if (FAILED(pEnum->Next(1, &vNext, &uFetched)) || uFetched == 0) {
            break;
        }
        if (V_VT(&vNext) != VT_BSTR) {
            continue;
        }
        Ztl_bstr_t sName = V_BSTR(&vNext);
        const wchar_t* pszName = sName.GetBSTR();
        if (pszName && iswdigit(pszName[0])) {
            sDonor = sName;
            break;
        }
    }
    if (!sDonor.GetBSTR()) {
        DEBUG_MESSAGE("FixCashWeaponImg %d: no stance node, skipped", nItemID);
        return;
    }

    Ztl_variant_t vDonor = pWeapon->item[sDonor];

    int nAdded = 0;
    for (auto pszType : k_apszWeaponType) {
        Ztl_variant_t vExisting = pWeapon->item[pszType];
        if (V_VT(&vExisting) != VT_EMPTY && V_VT(&vExisting) != VT_ERROR) {
            continue; // already present
        }
        pWeapon->item[pszType] = vDonor; // alias missing type -> donor stance
        ++nAdded;
    }
    DEBUG_MESSAGE("FixCashWeaponImg %d: donor=%ls added=%d", nItemID, sDonor.GetBSTR(), nAdded);
}


// CItemInfo::IsAbleToStickWithWeapon decides whether a cash weapon may be worn
// over the player's current weapon. The stock check only reads the cash weapon's
// *first* stance subnode, so a cash weapon whose first type is one the client
// doesn't expect (e.g. 21/22 from a newer version) -- or that lacks a node for
// the current weapon type -- is rejected with "not available with the weapon
// you're currently using". Force it to always permit; the avatar overlay falls
// back via FixCashWeaponImg.
static auto CItemInfo__IsAbleToStickWithWeapon =
    reinterpret_cast<BOOL(__thiscall*)(void*, int)>(0x0046D39C);

BOOL __fastcall CItemInfo__IsAbleToStickWithWeapon_hook(void* _ECX, void* _EDX, int nItemID) {
    return TRUE;
}


void AttachCashWeaponMod() {
    ATTACH_HOOK(CItemInfo__IsAbleToStickWithWeapon, CItemInfo__IsAbleToStickWithWeapon_hook);
}
