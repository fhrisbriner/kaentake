#include "wvs/packet.h"
#include <fstream>
#include <mutex>
#include <string>

#include "constants.h"
#include "hook.h"
//
// Created by Gwen on 4/8/2026.
//


inline std::mutex g_logMutex;
inline const char* g_logFile = "PacketCrashLogs.txt";

inline void WriteLogToFile(const std::string& text) {
    std::lock_guard<std::mutex> lock(g_logMutex); // thread-safe
    std::ofstream ofs(g_logFile, std::ios::app);
    if (ofs.is_open()) {
        ofs << text << std::flush;
    }
}


void OnPacketCrash(CInPacket* p, const char* type, uint16_t opcode) {
    std::string log;
    log += "\n==== PACKET DECODE CRASH ====\n";
    log += "Crash type: " + std::string(type) + "\n";
    log += "Opcode: 0x" + std::to_string(opcode) + "\n";

    if (p) {
        unsigned int maxDump = (p->m_uLength < 256) ? p->m_uLength : 256;
        log += "Offset: " + std::to_string(p->m_uOffset) + " / " + std::to_string(p->m_uLength) + "\n";

        log += "Data (hex): ";
        for (unsigned int i = 0; i < maxDump; i++)
            log += (p->m_aRecvBuff.a[i] < 16 ? "0" : "") + std::to_string((unsigned char)p->m_aRecvBuff.a[i]) + " ";
        if (p->m_uLength > maxDump)
            log += "... (truncated, total " + std::to_string(p->m_uLength) + " bytes)";

        log += "\nASCII: ";
        for (unsigned int i = 0; i < maxDump; i++) {
            char c = p->m_aRecvBuff.a[i];
            log += (c >= 32 && c <= 126 ? c : '.');
        }
        log += "\n";
    }

    // Last N decodes leading up to crash
    log += "\n---- Last " + std::to_string(g_LastDecodes.size()) + " Decodes ----\n";
    for (const auto& d : g_LastDecodes) {
        char hex[32];
        snprintf(hex, sizeof(hex), "0x%llX", (unsigned long long)d.value);
        log += std::string(d.type) + "  offset=" + std::to_string(d.offset)
            + "  size=" + std::to_string(d.size)
            + "  value=" + hex + "\n";
    }

    // Last N packets
    log += "\n---- Last " + std::to_string(g_LastPackets.size()) + " Packets ----\n";
    for (const auto& plog : g_LastPackets) {
        char hex[8];
        snprintf(hex, sizeof(hex), "0x%04X", plog.opcode);
        log += std::string("Opcode: ") + hex + ", Length: " + std::to_string(plog.data.size()) + "\n";
    }

    log += "==== END PACKET DUMP ====\n\n";

    WriteLogToFile(log);
}
// helper to dump packets when needed


// Expedition Death Count wire contract. 0x3727 sits in a custom high range the v83 client has
// no handler for; SET_FIELD is stock and must never be consumed.
static constexpr uint16_t kDeathCountOpcode      = 0x3727;
static constexpr uint16_t kDeathCountFieldOpcode = 0x007D;   // SET_FIELD

// S2C MONSTER_BOOK_RESULT. One opcode, three payload kinds distinguished by a leading byte:
// 0 = a mob drop table with live chances (Dropping tab), 1 = item-name search hits, 2 = the mobs
// that drop one item. Client-mod-only; the stock v83 client has no handler for it.
static constexpr uint16_t kMonsterBookResultOpcode = 0x372C;

typedef void(__thiscall* CClientSocket__ProcessPacket_t)(uintptr_t ecx, CInPacket* iPacket);
auto _CClientSocket__ProcessPacket = reinterpret_cast<CClientSocket__ProcessPacket_t>(0x004965F1);

void __fastcall CClientSocket__ProcessPacket(uintptr_t ecx, uintptr_t edx, CInPacket* iPacket) {
    if (iPacket) {
        uint16_t opcode = 0;
        unsigned int oldOffset = iPacket->m_uOffset;

        // Try reading opcode safely
        try {
            opcode = CInPacket::Decode2(iPacket);
        } catch (...) {
            OnPacketCrash(iPacket, "Decode2 (opcode read failed)", opcode);
        }

        // Restore offset so client logic is unaffected
        iPacket->m_uOffset = oldOffset;

        // Reset decode trace so the log only shows decodes for this packet
        g_LastDecodes.clear();

        // Copy raw packet into ring buffer
        try {
            PacketLog log;
            log.opcode = opcode;
            log.data.resize(iPacket->m_uLength);

            for (uint16_t i = 0; i < iPacket->m_uLength; ++i)
                log.data[i] = static_cast<uint8_t>(iPacket->m_aRecvBuff.a[i]);

            g_LastPackets.push_back(log);
            if (g_LastPackets.size() > MAX_PACKET_LOG)
                g_LastPackets.pop_front();
        } catch (...) {
            printf("[PacketHook] Failed to copy packet into log\n");
        }

        // ---- Expedition Death Count HUD (deathcount.cpp) --------------------------------
        // SET_FIELD is OBSERVED, never consumed: the stock client owns it (it is the whole field
        // build), so we take the panel down before it runs and let the packet fall through.
        if (opcode == kDeathCountFieldOpcode) {
            try { DeathCount_OnFieldChange(); } catch (...) {}
        }
        // 0x3727 is SWALLOWED -- the v83 client has no handler for it, which is exactly why this
        // opcode was chosen. Returning here keeps it from reaching the stock dispatcher.
        if (opcode == kDeathCountOpcode) {
            try { DeathCount_OnPacket(iPacket); } catch (...) {}
            return;
        }

#if USE_MONSTER_BOOK_DROPS || USE_MONSTER_BOOK_SEARCH
        if (opcode == kMonsterBookResultOpcode) {
            // Both readers get the same packet and each ignores the subtypes that are not its
            // own, so order does not matter. Each works off a guarded CanRead, only RECORDS data
            // and never moves the packet offset -- the views are rebuilt on the main-thread
            // flush. SWALLOWED: letting an unknown opcode reach the stock handler closes client.
#if USE_MONSTER_BOOK_DROPS
            try { MonsterBookDrops_OnPacket(iPacket); } catch (...) {}
#endif
#if USE_MONSTER_BOOK_SEARCH
            try { MonsterBookSearch_OnPacket(iPacket); } catch (...) {}
#endif
            return;
        }
#endif

        // Try calling original safely
        try {
            _CClientSocket__ProcessPacket(ecx, iPacket);
        } catch (...) {
            OnPacketCrash(iPacket, "Original ProcessPacket threw exception", opcode);
        }
    } else {
        _CClientSocket__ProcessPacket(ecx, iPacket);
    }
}

void PacketHooks() {
    ATTACH_HOOK(_CClientSocket__ProcessPacket, CClientSocket__ProcessPacket);
}