#include "wvs/packet.h"
#include <fstream>
#include <mutex>
#include <string>

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