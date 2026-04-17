#pragma once
#include <cstdint>
#include <cassert>
#include <functional>

// Forward declarations of your types
template<typename T> struct ZXString; // your ZXString

namespace detail {

    // Reconstructed _PAIR for ZMap<char const*, CHANGING_OBJECT, ZXString<char>>
    // size == 0x18
    template <typename K, typename V, typename S>
    struct ZMapPair : public ZRecyclable< ZMapPair<K,V,S>, 16, ZMapPair<K,V,S> >
    {
        ZMapPair*     pNext;   // 0x04
        S             key;     // 0x08  (ZXString<char>)
        V             value;   // 0x0C
        // sizeof must be 0x18 when compiled with same packing as client
    };
} // namespace detail



template <typename K, typename V, typename S>
class ZMap
{
public:
    using Pair = detail::ZMapPair<K,V,S>;

    // NOTE: these are typical members; reverse to confirm offsets if you need them.
    int32_t    nBuckets;  // number of buckets
    Pair**     buckets;   // pointer to buckets array (bucket heads)
    int32_t    nSize;     // items count
    void*      allocator; // internal allocator/pool

    // Find returns pointer to Pair or nullptr
    Pair* Find(const K key, std::function<uint32_t(const K)> hasher)
    {
        if (!buckets || nBuckets <= 0) return nullptr;
        uint32_t h = hasher(key);
        uint32_t idx = h % static_cast<uint32_t>(nBuckets);
        for (Pair* p = buckets[idx]; p; p = p->pNext) {
            // compare keys via ZXString or raw C string as appropriate
            // assume S has operator== or c_str()
            if (CompareKey(p->key, key)) return p;
        }
        return nullptr;
    }

    // Simple Insert (prepends)
    Pair* Insert(const S& keyValue, const V& value, std::function<uint32_t(const K)> hasher,
                 std::function<Pair*()> allocPair)
    {
        if (!buckets || nBuckets <= 0) return nullptr;
        uint32_t h = hasher(keyValue.c_str());
        uint32_t idx = h % static_cast<uint32_t>(nBuckets);

        Pair* p = allocPair();
        if (!p) return nullptr;
        // placement-construct key/value in p if needed (here assume trivial assign)
        p->pNext = buckets[idx];
        p->key = keyValue;
        p->value = value;
        buckets[idx] = p;
        ++nSize;
        return p;
    }

    // Remove by key (returns true if removed)
    bool Remove(const K key, std::function<uint32_t(const K)> hasher)
    {
        if (!buckets || nBuckets <= 0) return false;
        uint32_t h = hasher(key);
        uint32_t idx = h % static_cast<uint32_t>(nBuckets);
        Pair* prev = nullptr;
        for (Pair* p = buckets[idx]; p; prev = p, p = p->pNext) {
            if (CompareKey(p->key, key)) {
                if (prev) prev->pNext = p->pNext;
                else buckets[idx] = p->pNext;
                // recycle p via allocator (client-specific)
                --nSize;
                return true;
            }
        }
        return false;
    }

    // Iterate all pairs
    template<typename F>
    void ForEach(F&& fn)
    {
        if (!buckets) return;
        for (int i = 0; i < nBuckets; ++i) {
            for (Pair* p = buckets[i]; p; p = p->pNext) {
                fn(p);
            }
        }
    }

private:
    // Comparison helper (adjust if S semantics differ)
    static bool CompareKey(const S& a, const K b)
    {
        // if K is const char* and S = ZXString<char>, compare c_str()
        if constexpr (std::is_same<K, const char*>::value) {
            return (b && a.c_str() && strcmp(a.c_str(), b) == 0);
        } else {
            return a == b;
        }
    }
};

using ZStrHash_t = uint32_t(__cdecl*)(const char*); // calling conv may vary per client

// global pointer - fill it by signature scanning the client or hardcode if you have address
static ZStrHash_t g_ZStrHash = nullptr;

// Example fallback hasher (only as temporary fallback; not guaranteed identical)
static uint32_t FallbackZStrHash(const char* s)
{
    uint32_t h = 0;
    if (!s) return 0;
    while (*s) {
        char c = *s++;
        // example: lowercase-insensitive additive-rolling (NOT guaranteed)
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        h = h * 1315423911u + static_cast<unsigned char>(c);
    }
    return h;
}

inline uint32_t HashString(const char* s)
{
    if (g_ZStrHash) return g_ZStrHash(s);
    return FallbackZStrHash(s);
}

