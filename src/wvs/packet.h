#pragma once
#include "ztl/zalloc.h"
#include "ztl/zcoll.h"
#include "ztl/zstr.h"
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>

#include <deque>
#include <vector>
#include <cstdint>
#include <cstdio>

struct PacketLog {
    uint16_t opcode;
    std::vector<uint8_t> data;
};

struct DecodeEvent {
    const char* type;
    uint32_t    offset;  // offset before the read
    uint32_t    size;    // bytes consumed
    uint64_t    value;   // decoded value widened to 64-bit
};

constexpr size_t MAX_PACKET_LOG = 20;
constexpr size_t MAX_DECODE_LOG = 128;

inline std::deque<PacketLog>   g_LastPackets;
inline std::deque<DecodeEvent> g_LastDecodes;

inline void LogDecode(const char* type, uint32_t offset, uint32_t size, uint64_t value) {
    g_LastDecodes.push_back({ type, offset, size, value });
    if (g_LastDecodes.size() > MAX_DECODE_LOG)
        g_LastDecodes.pop_front();
}

inline ZXString<char> namedObject;

class CInPacket;
void OnPacketCrash(CInPacket* p, const char* type, uint16_t opcode = 0);

class CInPacket {
public:
    int32_t m_bLoopback;
    int32_t m_nState;
    ZArray<char> m_aRecvBuff;
    uint16_t m_uLength;
    uint16_t m_uRawSeq;
    uint16_t m_uDataLen;
    uint32_t m_uOffset;

    // Decode functions with overflow protection
    static uint8_t Decode1(CInPacket *pthis) {
        if (pthis->m_uOffset + 1 > pthis->m_uLength) {
            OnPacketCrash(pthis, "Decode1");
            throw std::runtime_error("Decode1 overflow");
        }
        uint32_t offset = pthis->m_uOffset;
        uint8_t result = static_cast<uint8_t>(pthis->m_aRecvBuff.a[offset]);
        pthis->m_uOffset = offset + 1;
        LogDecode("Decode1", offset, 1, result);
        return result;
    }

    // Bounds probe for readers that parse a packet WITHOUT moving its offset (the Monster Book
    // modules read fields at fixed offsets off m_uOffset and must leave the cursor alone for the
    // stock dispatcher). Returns false rather than throwing, so a short packet is a skipped
    // feature instead of a client-visible exception.
    bool CanRead(unsigned int uCount) const {
        return m_uOffset + uCount <= m_uLength;
    }

    // Raw pointer at the current offset, for readers that index fields directly instead of
    // decoding. Callers reach the body at [2] because ProcessPacket restores the offset onto the
    // opcode. Always pair with CanRead -- this does no bounds checking of its own beyond the
    // offset itself.
    const uint8_t* CurrentPublic() const {
        if (m_uOffset > m_uLength) {
            return nullptr;
        }
        return reinterpret_cast<const uint8_t*>(&m_aRecvBuff.a[m_uOffset]);
    }

    static int16_t Decode2(CInPacket *pthis) {
        if (pthis->m_uOffset + 2 > pthis->m_uLength) {
            OnPacketCrash(pthis, "Decode2");
            throw std::runtime_error("Decode2 overflow");
        }
        uint32_t offset = pthis->m_uOffset;
        int16_t result = *reinterpret_cast<int16_t*>(&pthis->m_aRecvBuff.a[offset]);
        pthis->m_uOffset = offset + 2;
        LogDecode("Decode2", offset, 2, static_cast<uint64_t>(static_cast<uint16_t>(result)));
        return result;
    }

    static int32_t Decode4(CInPacket *pthis) {
        if (pthis->m_uOffset + 4 > pthis->m_uLength) {
            OnPacketCrash(pthis, "Decode4");
            throw std::runtime_error("Decode4 overflow");
        }
        uint32_t offset = pthis->m_uOffset;
        int32_t result = *reinterpret_cast<int32_t*>(&pthis->m_aRecvBuff.a[offset]);
        pthis->m_uOffset = offset + 4;
        LogDecode("Decode4", offset, 4, static_cast<uint64_t>(static_cast<uint32_t>(result)));
        return result;
    }

    static bool CInPacket_DecodeStr(CInPacket* packet, char* out, size_t maxLen) {
        if (!packet || !out || maxLen == 0) return false;

        uint16_t len = Decode2(packet);
        if (packet->m_uOffset + len > packet->m_uLength) {
            OnPacketCrash(packet, "DecodeStr");
            throw std::runtime_error("DecodeStr overflow");
        }

        if (len >= maxLen) len = maxLen - 1;
        for (uint16_t i = 0; i < len; i++) {
            out[i] = Decode1(packet);
        }
        out[len] = 0;
        return true;
    }

    static void DecodePacketToZXString(CInPacket* packet, ZXString<char>* out) {
        if (!packet || !out) return;

        uint8_t len = Decode1(packet);
        if (len == 0) { out->_m_pStr = nullptr; return; }

        char* buffer = static_cast<char*>(malloc(len + 1));
        if (!buffer) { out->_m_pStr = nullptr; return; }

        for (uint8_t i = 0; i < len; i++) {
            buffer[i] = Decode1(packet);
        }
        buffer[len] = 0;
        out->_m_pStr = buffer;

        printf("[DecodePacketToZXString] decoded: '%s'\n", out->_m_pStr);
        fflush(stdout);
    }

    // Instance wrappers
    uint8_t Decode1() { return Decode1(this); }
    int16_t Decode2() { return Decode2(this); }
    int32_t Decode4() { return Decode4(this); }
    void Skip(int nBytes) { m_uOffset += nBytes; }
    void Rewind(int nBytes) { m_uOffset -= nBytes; }

    template<typename T>
    T DecodeBuffer() {
        if (m_uOffset + sizeof(T) > m_uLength) {
            OnPacketCrash(this, "DecodeBuffer");
            throw std::runtime_error("DecodeBuffer overflow");
        }

        typedef int32_t(__fastcall* _DecodeBuffer_t)(CInPacket* pThis, void* edx, void* p, size_t nLen);
        static _DecodeBuffer_t _DecodeBuffer = reinterpret_cast<_DecodeBuffer_t>(0x004336A0);

        uint32_t offset = m_uOffset;
        T retval;
        _DecodeBuffer(this, nullptr, &retval, sizeof(T));
        uint64_t wide = 0;
        memcpy(&wide, &retval, sizeof(T) < sizeof(uint64_t) ? sizeof(T) : sizeof(uint64_t));
        LogDecode("DecodeBuffer", offset, sizeof(T), wide);
        return retval;
    }

    template<typename T>
    T Decode() {
        if (m_uOffset + sizeof(T) > m_uLength) {
            OnPacketCrash(this, "Decode<T>");
            return T{};
        }

        typedef INT(__fastcall* _DecodeBuffer_t)(CInPacket* pThis, PVOID edx, PVOID p, size_t nLen);
        static _DecodeBuffer_t _DecodeBuffer = reinterpret_cast<_DecodeBuffer_t>(0x00432257);

        uint32_t offset = m_uOffset;
        T retval;
        _DecodeBuffer(this, nullptr, &retval, sizeof(T));
        uint64_t wide = 0;
        memcpy(&wide, &retval, sizeof(T) < sizeof(uint64_t) ? sizeof(T) : sizeof(uint64_t));
        LogDecode("Decode<T>", offset, sizeof(T), wide);
        return retval;
    }
};

static_assert(sizeof(CInPacket) == 0x18);


class COutPacket {
protected:
    int32_t m_bLoopback;
    ZArray<uint8_t> m_aSendBuff;
    uint32_t m_uOffset;
    int32_t m_bIsEncryptedByShanda;

public:
    explicit COutPacket(int32_t nType) : m_aSendBuff(0x100) {
        Init(nType, 0, 0);
    }

    void Encode1(uint8_t n) { EncodeBuffer(&n, 1); }
    void Encode2(uint16_t n) { EncodeBuffer(&n, 2); }
    void Encode4(uint32_t n) { EncodeBuffer(&n, 4); }
    void EncodeStr(ZXString<char> s) {
        int32_t n = s.GetLength();
        Encode2(n);
        EncodeBuffer(s, n);
    }

    void EncodeBuffer(const void* p, uint32_t uSize) {
        EnlargeBuffer(uSize);
        memcpy(&m_aSendBuff[m_uOffset], p, uSize);
        m_uOffset += uSize;
    }

    void Init(int32_t nType, int32_t bLoopback, int32_t bTypeHeader1Byte) {
        m_bLoopback = bLoopback;
        m_uOffset = 0;
        if (nType != 0x7FFFFFFF) {
            if (bTypeHeader1Byte) Encode1(nType);
            else Encode2(nType);
        }
        m_bIsEncryptedByShanda = 0;
    }

protected:
    void EnlargeBuffer(uint32_t uSize) {
        uint32_t uCur = m_aSendBuff.GetCount();
        uint32_t uReq = m_uOffset + uSize;
        if (uCur < uReq) {
            do { uCur *= 2; } while (uCur < uReq);
            m_aSendBuff.Realloc(uCur, 0);
        }
    }
};



void PacketHooks();

static_assert(sizeof(COutPacket) == 0x10);