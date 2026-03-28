#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
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
        snprintf(buf, sizeof(buf), "%016llx%016llx", static_cast<unsigned long long>(high),
                 static_cast<unsigned long long>(low));
        return std::string(buf);
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
