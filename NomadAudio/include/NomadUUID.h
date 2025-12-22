// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once
#include <cstdint>
#include <string>
#include <random>
#include <cstdio>
#include <cstring>

namespace Nomad {

/**
 * @brief Generic 128-bit UUID for stable identity
 */
struct NomadUUID {
    uint64_t high = 0;
    uint64_t low = 0;
    
    NomadUUID() = default;
    NomadUUID(uint64_t h, uint64_t l) : high(h), low(l) {}
    
    bool isValid() const { return high != 0 || low != 0; }
    bool operator==(const NomadUUID& other) const { return high == other.high && low == other.low; }
    bool operator!=(const NomadUUID& other) const { return !(*this == other); }
    bool operator<(const NomadUUID& other) const { 
        return high < other.high || (high == other.high && low < other.low); 
    }
    
    std::string toString() const {
        char buf[37];
        snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%012llx",
                 static_cast<uint32_t>(high >> 32),
                 static_cast<uint16_t>(high >> 16),
                 static_cast<uint16_t>(high),
                 static_cast<uint16_t>(low >> 48),
                 low & 0xFFFFFFFFFFFFULL);
        return std::string(buf);
    }
    
    static NomadUUID generate() {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint64_t> dis;
        NomadUUID uuid;
        uuid.high = dis(gen);
        uuid.low = dis(gen);
        // Set version 4 (random) and variant bits
        uuid.high = (uuid.high & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
        uuid.low = (uuid.low & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
        return uuid;
    }
    
    static NomadUUID fromString(const std::string& str) {
        NomadUUID uuid;
        if (str.length() >= 36) {
            unsigned int a, b, c, d;
            unsigned long long e;
            if (sscanf(str.c_str(), "%8x-%4x-%4x-%4x-%12llx", &a, &b, &c, &d, &e) == 5) {
                uuid.high = (static_cast<uint64_t>(a) << 32) | 
                           (static_cast<uint64_t>(b) << 16) | 
                           static_cast<uint64_t>(c);
                uuid.low = (static_cast<uint64_t>(d) << 48) | e;
            }
        }
        return uuid;
    }
};

} // namespace Nomad
