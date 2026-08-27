#pragma once
#include "hook.h"
#include "wvs/stage.h"
#include "wvs/wnd.h"
#include "ztl/ztl.h"
#include "ztl/zmap.h"


class CMapLoadable : public CStage {
    using LayerList       = ZList<IWzGr2DLayerPtr>;
    using TaggedObjMap    = ZMap<const char*, ZRef<LayerList>, ZXString<char>>;

public:
    MEMBER_AT(IWzPropertyPtr, 0x2C, m_pPropFieldInfo)

    // THE FIELD BODY, and it is NOT the same object as m_pPropFieldInfo above.
    //
    // 2266 of the 5381 v83 maps (42%) carry an info/link and hold no field body of their own:
    // no back, no foothold, no tile. The client resolves the link at 0x00529730 and then swaps
    // ONLY the body: m_pPropFieldInfo (+0x2C) stays on the linking map while m_pPropField
    // (+0x30) becomes the target's. Proven, not inferred: RestoreBack @0x0063CBBA does
    // `push "back"` then `lea ecx,[ebx+0x30]`.
    //
    // This split is a landmine for any per-map runtime injection. A marker written to +0x2C
    // guarding a mutation reached through +0x30 is per-LINKING-map while the mutation is
    // per-TARGET, and the two never agree: 178 targets are shared and 99 maps link to
    // 100020100 alone. Always put the flag on the same cached object as the thing it guards.
    MEMBER_AT(IWzPropertyPtr, 0x30, m_pPropField)
    TaggedObjMap* m_mTagedObj() { return reinterpret_cast<TaggedObjMap*>(reinterpret_cast<uint8_t*>(this) + 0xB8); }
    MEMBER_AT(RECT, 0xF0, m_rcViewRange)
    MEMBER_HOOK(void, 0x00641EF1, RestoreViewRange) // resolution.cpp

    // weather.cpp owns the six below.
    MEMBER_HOOK(void, 0x00639B3D, LoadMap)      // weather.cpp
    MEMBER_HOOK(void, 0x006399EF, Update)       // weather.cpp (per-frame fade driver)
    MEMBER_HOOK(void, 0x0063A100, RestoreTile)  // weather.cpp (capture tiles for tint)
    MEMBER_HOOK(void, 0x0063AA7E, RestoreObj)   // weather.cpp (capture objects for tint)
    MEMBER_HOOK(void, 0x0063CBBA, RestoreBack)  // weather.cpp (LoadMap / SetFieldMagLevel / ReloadBack)
    MEMBER_HOOK(void*, 0x0063CD4E, MakeBack, int nIndex, void* pProp) // weather.cpp

    // lamps.cpp: builds ONE map object from its obj entry, so it is where an injected lamp or
    // glow can be tagged before the layer for it exists. The declared void* return is a fiction
    // the binary does not support -- the last thing the function does is Release its arg, so EAX
    // holds a refcount. MakeBack is declared the same way for the same reason.
    MEMBER_HOOK(void*, 0x0063AD16, MakeObj, int nLayer, IWzProperty* pObjProp) // lamps.cpp
};


// weather.cpp needs this one to capture an NPC's layer for the night tint. CNpc::Init builds the
// layer synchronously inside this call, so the layer exists by the time the hook returns.
class CNpcPool {
public:
    MEMBER_HOOK(void, 0x006D9993, OnNpcEnterField, void* pPacket)
};


class CField : public CMapLoadable {
public:
    MEMBER_AT(ZRef<CWnd>, 0x1C8, m_pClock) // ZRef<CClock>
};


inline CField* get_field() {
    return reinterpret_cast<CField*(__cdecl*)()>(0x00437A0C)();
}