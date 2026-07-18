#pragma once
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <functional>
#include <random>
#include <string>

namespace Aestra {
namespace Audio {

struct AestraUUID {
    uint64_t low = 0;
    uint64_t high = 0;

    AestraUUID() = default;
    AestraUUID(uint64_t v) : low(v) {}

    static AestraUUID generate() {
        static const uint64_t processSalt = []() {
            std::random_device rd;
            const uint64_t r1 = static_cast<uint64_t>(rd()) << 32;
            const uint64_t r2 = static_cast<uint64_t>(rd());
            const uint64_t t = static_cast<uint64_t>(
                std::chrono::high_resolution_clock::now().time_since_epoch().count());
            return r1 ^ r2 ^ t;
        }();

        static std::atomic<uint64_t> counter{1};

        AestraUUID id;
        id.low = counter.fetch_add(1, std::memory_order_relaxed);
        id.high = processSalt;
        return id;
    }

    bool operator==(const AestraUUID& other) const { return low == other.low && high == other.high; }

    bool operator!=(const AestraUUID& other) const { return !(*this == other); }

    bool operator<(const AestraUUID& other) const {
        if (high != other.high) return high < other.high;
        return low < other.low;
    }

    bool operator<=(const AestraUUID& other) const { return !(other < *this); }
    bool operator>(const AestraUUID& other) const { return other < *this; }
    bool operator>=(const AestraUUID& other) const { return !(*this < other); }

    /**
     * @brief Convert to string representation
     */
    std::string toString() const {
        // Simple hex representation
        char buf[64];
        snprintf(buf, sizeof(buf), "%016" PRIx64 "%016" PRIx64, high, low);
        return std::string(buf);
    }

    /**
     * @brief Parse the toString() representation (32 lowercase/uppercase hex chars).
     * @param str Candidate string.
     * @param out Receives the parsed UUID on success (untouched on failure).
     * @return true when the string is exactly 32 hex characters.
     */
    static bool tryParse(const std::string& str, AestraUUID& out) {
        if (str.size() != 32) {
            return false;
        }
        uint64_t parsedHigh = 0;
        uint64_t parsedLow = 0;
        for (size_t i = 0; i < 32; ++i) {
            const char c = str[i];
            uint64_t digit;
            if (c >= '0' && c <= '9') {
                digit = static_cast<uint64_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                digit = static_cast<uint64_t>(c - 'a') + 10;
            } else if (c >= 'A' && c <= 'F') {
                digit = static_cast<uint64_t>(c - 'A') + 10;
            } else {
                return false;
            }
            if (i < 16) {
                parsedHigh = (parsedHigh << 4) | digit;
            } else {
                parsedLow = (parsedLow << 4) | digit;
            }
        }
        out.high = parsedHigh;
        out.low = parsedLow;
        return true;
    }
};

} // namespace Audio
} // namespace Aestra

using AestraUUID = Aestra::Audio::AestraUUID;

// Hash specialization for AestraUUID
namespace std {
template <> struct hash<Aestra::Audio::AestraUUID> {
    size_t operator()(const Aestra::Audio::AestraUUID& uuid) const noexcept {
        // Combine low and high using a hash combiner
        size_t h1 = std::hash<uint64_t>{}(uuid.low);
        size_t h2 = std::hash<uint64_t>{}(uuid.high);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};
} // namespace std
