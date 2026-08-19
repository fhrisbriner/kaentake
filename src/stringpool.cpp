#include "pch.h"
#include "hook.h"
#include "stringpool.h"
#include "ztl/ztl.h"

#include <cstring>
#include <unordered_map>
#include <vector>

#define REPLACE_STRING(INDEX, NEW_STRING) \
    do { \
        static char sEncoded[GetLength(NEW_STRING) + 2]; \
        EncodeString(INDEX, NEW_STRING, sEncoded); \
    } while (0)


class StringPool {
public:
    inline static auto ms_aKey = reinterpret_cast<const unsigned char*>(0x00B001EC);
    inline static auto ms_aString = reinterpret_cast<const char**>(0x00BDC9D4);

    class Key {
    public:
        ZArray<unsigned char> m_aKey;
        Key(const unsigned char* pKey, unsigned int nKeySize, unsigned int nSeed) {
            reinterpret_cast<void(__thiscall*)(Key*, const unsigned char*, unsigned int, unsigned int)>(0x0079E780)(this, pKey, nKeySize, nSeed);
        }
    };
    static_assert(sizeof(Key) == 0x4);
};

constexpr size_t GetLength(const char* s) {
    size_t n = 0;
    while (s[n]) {
        ++n;
    }
    return n;
}

void EncodeString(int nIdx, const char* sSource, char* sDestination) {
    StringPool::Key keygen(StringPool::ms_aKey, 0x10, 0);
    size_t n = strlen(sSource);
    for (size_t i = 0; i < n; ++i) {
        unsigned char key = keygen.m_aKey[i % 0x10];
        sDestination[i + 1] = sSource[i] ^ key;
        if (static_cast<uint8_t>(sSource[i]) == static_cast<uint8_t>(key)) {
            sDestination[i + 1] = key;
        }
    }
    sDestination[0] = 0;
    sDestination[n + 1] = 0;
    StringPool::ms_aString[nIdx] = sDestination;
}


// Runtime twin of REPLACE_STRING, for callers whose text is not a compile-time literal.
// See stringpool.h for why each index gets its own buffer rather than a shared scratch one.
void SetStringPoolString(int nIdx, const char* sText) {
    if (!sText) {
        return;
    }
    static std::unordered_map<int, std::vector<char>> s_aBuffer;
    std::vector<char>& buf = s_aBuffer[nIdx];
    // EncodeString writes [0]=0, [1..n]=encoded, [n+1]=0.
    buf.assign(std::strlen(sText) + 2, 0);
    EncodeString(nIdx, sText, buf.data());
}

void AttachStringPoolMod() {
    REPLACE_STRING(1163, "Maple Night");
}