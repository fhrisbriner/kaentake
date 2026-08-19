// ============================================================
// getmobinfobyid.cpp  —  String/Mob.img mob-name resolver
//
// Written for the ported Monster Book modules, which expect a GetMobNameById(int) they can call
// as a fallback when their own tables miss. Kept as its own translation unit (rather than folded
// into one of the book modules) because nothing about it is Monster Book specific.
//
// String/Mob.img is a flat property list keyed by the decimal mob id, each entry carrying a
// "name" string:
//     String/Mob.img/100100/name  ->  "Blue Snail"
//
// The search path does ~860 of these in a row when it builds a result page, so the results are
// memoised and the img root is held open for the process lifetime. Both matter: re-resolving the
// root per lookup turned a page build into a visible stall during development of the original.
// ============================================================

#include "pch.h"
#include "getmobinfobyid.h"
#include "wvs/util.h"
#include "ztl/ztl.h"

#include <string>
#include <unordered_map>

static const char kUnknown[] = "Unknown";

std::string GetMobNameById(int nMobId) {
    if (nMobId <= 0) {
        return kUnknown;
    }

    static std::unordered_map<int, std::string> s_cache;
    auto it = s_cache.find(nMobId);
    if (it != s_cache.end()) {
        return it->second;
    }

    std::string sName = kUnknown;
    try {
        // Held across calls: the resource manager hands back the same object anyway, but going
        // through GetObjectA per lookup is the expensive part.
        static IWzPropertyPtr s_pMobStr;
        if (!s_pMobStr) {
            Ztl_variant_t vRoot = get_object_or_empty(L"String/Mob.img");
            s_pMobStr = get_unknown(vRoot);
        }

        if (s_pMobStr) {
            wchar_t sKey[16];
            _snwprintf_s(sKey, _countof(sKey), _TRUNCATE, L"%d", nMobId);

            Ztl_variant_t vMob = get_item_or_empty(s_pMobStr, sKey);
            IWzPropertyPtr pMob = get_unknown(vMob);
            if (pMob) {
                Ztl_variant_t vName = get_item_or_empty(pMob, L"name");
                if (V_VT(&vName) != VT_EMPTY && V_VT(&vName) != VT_ERROR) {
                    Ztl_variant_t vStr;
                    if (SUCCEEDED(ZComAPI::ZComVariantChangeType(&vStr, &vName, 0, VT_BSTR))
                            && V_BSTR(&vStr)) {
                        // BSTR is UTF-16; the callers all want narrow. The names are ASCII in
                        // v83, so a plain narrowing pass is enough and avoids a codepage call.
                        const wchar_t* w = V_BSTR(&vStr);
                        std::string out;
                        for (; *w; ++w) {
                            out.push_back(static_cast<char>(*w & 0xFF));
                        }
                        if (!out.empty()) {
                            sName = out;
                        }
                    }
                }
            }
        }
    } catch (...) {
        sName = kUnknown;
    }

    s_cache.emplace(nMobId, sName);
    return sName;
}
