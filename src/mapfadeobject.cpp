//
// Created by Gwen on 4/12/2026.
//

#include "mapfadeobject.h"
#include "hook.h"
#include "wvs/field.h"
#include "wvs/packet.h"
#include "ztl/zstr.h"

#include <vector>

// Original function pointers
auto OnSetMapObjectVisible_t = reinterpret_cast<void(__thiscall*)(CMapLoadable*, CInPacket*)>(0x006449D2);
static auto CInPacket_DecodeStr = reinterpret_cast<ZXString<char>*(__thiscall*)(CInPacket*, ZXString<char>*)>(0x00484140);

int FADE_DURATION_MS = 0;
int FADE_STEPS = 50;
int FADE_STEP_MS = 0;

using LayerPtr = IWzGr2DLayerPtr;
using LayerList = ZList<LayerPtr>;

struct FadeLayer {
    IWzGr2DLayer* layer;
    IWzVector2D* alphaVec;
};

struct FadeRequest {
    std::vector<FadeLayer> layers;
    bool show;
};

DWORD WINAPI FadeThreadProc(LPVOID param) {
    FadeRequest* req = (FadeRequest*)param;

    __try {
        for (int step = 0; step <= FADE_STEPS; step++) {
            float t = (float)step / (float)FADE_STEPS;
            int alpha = req->show ? (int)(t * 255.0f) : 255 - (int)(t * 255.0f);

            if (step == 0 && req->show) {
                for (auto& fl : req->layers)
                    fl.layer->put_visible(1);
            }

            for (auto& fl : req->layers)
                fl.alphaVec->raw_Move(alpha, 0);

            if (step < FADE_STEPS)
                Sleep(FADE_STEP_MS);
        }

        for (auto& fl : req->layers)
            fl.alphaVec->Release();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DebugMessage("[Fade] Exception in fade thread");
    }

    delete req;
    return 0;
}

// Find tagged layer list via manual ZMap iteration
// PAIR layout: [0]=ZRecyclable [1]=pNext [2]=key(char*) [3]=wchar_key [4]=value(ZRef.p)
static LayerList* FindTaggedLayerList(CMapLoadable* pThis, const char* tagName) {
    uint8_t* pMap = (uint8_t*)pThis->m_mTagedObj();
    uint32_t** apTable = *(uint32_t***)(pMap + 4);
    uint32_t tableSize = *(uint32_t*)(pMap + 8);
    uint32_t mapCount = *(uint32_t*)(pMap + 12);

    if (mapCount == 0 || !apTable) return nullptr;

    for (uint32_t bucket = 0; bucket < tableSize && bucket < 256; bucket++) {
        uint32_t* pPair = (uint32_t*)apTable[bucket];
        while (pPair) {
            const char* pairKey = (const char*)pPair[2];
            if (pairKey && strcmp(pairKey, tagName) == 0)
                return reinterpret_cast<LayerList*>(pPair[4]);
            pPair = (uint32_t*)pPair[1];
        }
    }
    return nullptr;
}

void FadeMapObject(CMapLoadable* pThis, const char* tagName, bool bShow) {
    __try {
        LayerList* pList = FindTaggedLayerList(pThis, tagName);
        if (!pList) return;

        auto* req = new FadeRequest();
        req->show = bShow;

        LayerPtr* pos = pList->GetHeadPosition();
        while (pos) {
            LayerPtr& layerRef = LayerList::GetNext(pos);
            IWzGr2DLayer* pLayer = layerRef;
            if (pLayer) {
                IWzVector2D* alphaVec = nullptr;
                pLayer->get_alpha(&alphaVec);
                if (alphaVec)
                    req->layers.push_back({pLayer, alphaVec});
            }
        }

        if (req->layers.empty()) {
            delete req;
            return;
        }


        int32_t currentAlpha = 0;
        req->layers[0].alphaVec->get_x(&currentAlpha);
        int targetAlpha = bShow ? 255 : 0;
        if (currentAlpha == targetAlpha) {
            for (auto& fl : req->layers)
                fl.alphaVec->Release();
            delete req;
            return;
        }

        HANDLE hThread = CreateThread(nullptr, 0, FadeThreadProc, req, 0, nullptr);
        if (hThread) {
            CloseHandle(hThread);
        } else {
            for (auto& fl : req->layers) {
                if (bShow) fl.layer->put_visible(1);
                fl.alphaVec->raw_Move(bShow ? 255 : 0, 0);
                fl.alphaVec->Release();
            }
            delete req;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DebugMessage("[Fade] Exception for tag '%s'", tagName);
    }
}

void __fastcall OnSetMapObjectVisible_Hook(CMapLoadable* pThis, void* edx, CInPacket* iPacket)
{
    DebugMessage("[Fade] OnSetMapObjectVisible");
    FADE_DURATION_MS = CInPacket::Decode4(iPacket);
    FADE_STEP_MS = FADE_DURATION_MS / FADE_STEPS;
    unsigned char count = CInPacket::Decode1(iPacket);
    for (int i = 0; i < count; i++) {
        DebugMessage("[Fade] Decoding object %d", i);
        ZXString<char> name;
        CInPacket_DecodeStr(iPacket, &name);
        unsigned char bVisible = CInPacket::Decode1(iPacket);

        FadeMapObject(pThis, (const char*)name, bVisible != 0);
    }
}

auto SetObjectState_t = reinterpret_cast<void(__thiscall*)(void*, void*, int)>(0x00642ACA);

void AttachMapObjectFade() {
    ATTACH_HOOK(OnSetMapObjectVisible_t, OnSetMapObjectVisible_Hook);
}
