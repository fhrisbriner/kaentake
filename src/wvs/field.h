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
    MEMBER_AT(IWzPropertyPtr, 0x30, m_pPropFieldInfo2)
    TaggedObjMap* m_mTagedObj() { return reinterpret_cast<TaggedObjMap*>(reinterpret_cast<uint8_t*>(this) + 0xB8); }
    MEMBER_AT(RECT, 0xF0, m_rcViewRange)
    MEMBER_HOOK(void, 0x00641EF1, RestoreViewRange) // resolution.cpp

};


class CField : public CMapLoadable {
public:
    MEMBER_AT(ZRef<CWnd>, 0x1C8, m_pClock) // ZRef<CClock>
};


inline CField* get_field() {
    return reinterpret_cast<CField*(__cdecl*)()>(0x00437A0C)();
}