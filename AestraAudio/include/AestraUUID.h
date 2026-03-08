#pragma once
#include <cstdint>
#include <cinttypes>
#include <functional>
#include <string>

namespace Aestra {
namespace Audio {

struct AestraUUID {
    uint64_t low = 0;
    uint64_t high = 0;

    AestraUUID() = default;
    AestraUUID(uint64_t v) : low(v) {}

    static AestraUUID generate() { return AestraUUID(1); }

    bool operator==(const AestraUUID& other) const { return low == other.low && high == other.high; }

    bool operator!=(const AestraUUID& other) const { return !(*this == other); }

    /**
     * @brief Convert to string representation
     */
    std::string toString() const {
        // Simple hex representation
        char buf[64];
        snprintf(buf, sizeof(buf), "%016" PRIx64 "%016" PRIx64, high, low);
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
